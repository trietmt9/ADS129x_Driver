/*
 * ADS1298 EMG acquisition and streaming, 4 channels at 1000 SPS.
 *
 *   SETUP   -> device-tree handles, GPIO directions, hardware reset pulse
 *   CONFIG  -> data rate / gain / reference via ads_emg_init(), park the
 *              unused channels 5-8, verify the chip ID
 *   READ    -> RDATAC, drain DRDY-driven samples straight into the block buffer
 *   STREAM  -> emit one framed binary DATA packet per block, plus an INFO
 *              packet every second
 *
 * The acquisition path is deliberately direct: ads_emg_read_frame() writes the
 * sign-extended samples into `block`, and once `block` is full it becomes the
 * payload of a wire frame. The driver's ring buffers and DSP double buffer are
 * bypassed - they cost two extra copies per sample and ~4 KB of RAM to arrive at
 * exactly the same numbers, because nothing here filters. (filters/src/iir.c is
 * not built; band-passing happens on the host, where it is tunable without a
 * reflash - see software/ARCHITECTURE.md 6.4b.)
 *
 * Which channels are streamed is a bitmask, EMG_CHANNEL_MASK - see section 0.
 * In RDATAC the device always clocks out all 8 channels (a 27-byte frame), so
 * the driver parses the whole packet and we transmit the selected slices; the
 * mask travels in the DATA frame so the host can label the traces with the
 * right lead names instead of counting up from CH1.
 *
 * Wire protocol: software/ARCHITECTURE.md section 4. The frame magic is
 * non-ASCII, so these binary frames and ordinary Zephyr LOG_* output coexist on
 * one UART - the host decodes frames and passes everything else through to its
 * console pane.
 */
#include <main.h>

#include <emg_frame.h>
#include <emg_stream.h>

#include <zephyr/drivers/uart.h>

/* ============== 0. Acquisition geometry ==============
 *
 * Which ADS1298 channels to stream, as a bitmask (bit 0 = CH1 .. bit 7 = CH8).
 * Override at configure time, e.g.
 *     west build -b nucleo_f767zi . -- -DEMG_CHANNEL_MASK=0x0F
 *
 * The default is CH2 | CH3, which is a property of the bench hardware rather
 * than a preference. On the ADS1298ECG-FE (SBAU171) the DB15 inputs reach the
 * channels through JP26-JP33 in the factory ECG configuration, and only two
 * channels have BOTH inputs on a real electrode terminal:
 *
 *     CH1 = V6 - WCT     CH2 = LA - RA  (LEAD I)
 *     CH3 = LL - RA      CH4 = V2 - WCT   (LEAD II on CH3)
 *
 * The WCT channels are not merely less useful - with a 3-lead source (RA, LA,
 * LL only, which is what an ECG simulator provides) V6 and V2 are open
 * circuits. This front end is DC-coupled as populated, so an open PGA input
 * drifts to a rail and streams full-scale mash, and a railed input inside the
 * shared RLD loop degrades the good channels as well. Streaming them was
 * costing two of four traces and half the link budget to display noise.
 *
 * For the custom FES board, where J1-J4 wire four real differential pairs to
 * IN1P/N..IN4P/N, the right value is 0x0F.
 */
/* How many limb electrodes are actually attached. This is not a preference -
 * it decides which leads exist and, more importantly, which inputs may be fed
 * into the right-leg-drive loop.
 *
 *   3 = RA, LA, RL          -> LEAD I only          (CH2)
 *   4 = RA, LA, LL, RL      -> LEAD I and LEAD II   (CH2, CH3)
 *
 * Override at configure time:
 *     west build -b nucleo_f767zi . -- -DEMG_ELECTRODES=4
 */
#ifndef EMG_ELECTRODES
#define EMG_ELECTRODES 3
#endif

#if EMG_ELECTRODES == 3
  /* LEAD II = LL - RA, and LL is not connected, so CH3 has an open positive
   * input. Do not stream it: this front end is DC-coupled, so an open PGA
   * input drifts to a rail and delivers full-scale mash. */
  #define EMG_DEFAULT_CH_MASK  (EMG_CH_MASK_BIT(2))
  #define EMG_RLD_SENSP        ADS129x_ECG_RLD_SENSP_RA_LA
  #define EMG_RLD_SENSN        ADS129x_ECG_RLD_SENSN_RA_LA
  #define EMG_WCT1             ADS129x_ECG_WCT1_OFF
  #define EMG_WCT2             ADS129x_ECG_WCT2_OFF
#elif EMG_ELECTRODES == 4
  #define EMG_DEFAULT_CH_MASK  (EMG_CH_MASK_BIT(2) | EMG_CH_MASK_BIT(3))
  #define EMG_RLD_SENSP        ADS129x_ECG_RLD_SENSP_3LEAD
  #define EMG_RLD_SENSN        ADS129x_ECG_RLD_SENSN_3LEAD
  #define EMG_WCT1             ADS129x_ECG_WCT1_3LEAD
  #define EMG_WCT2             ADS129x_ECG_WCT2_3LEAD
#else
  #error "EMG_ELECTRODES must be 3 (RA,LA,RL) or 4 (RA,LA,LL,RL)"
#endif

/* ---- Analog-path changes are OPT-IN ----------------------------------------
 *
 * Everything below defaults to the behaviour this firmware had BEFORE the
 * registers were first written, because each one changes how the front end
 * behaves electrically and none has been validated on this hardware:
 *
 *   RLD derivation   closes a feedback loop through the electrodes and the body
 *   Lead-off detect  injects a DC current into the inputs being measured
 *
 * A diagnostic that alters the signal is not a safe default. Turn them on
 * deliberately, one at a time, and keep the one that helps:
 *
 *     -DEMG_RLD_ACTIVE=ON    close the RLD loop (common-mode rejection)
 *     -DEMG_LEADOFF=ON       electrode-integrity detection
 */

/* Passive-RLD escape hatch.
 *
 *     west build -b nucleo_f767zi . -- -DEMG_RLD_PASSIVE=ON
 *
 * With no electrode selected, the RLD amplifier simply holds RLDOUT at RLDREF -
 * a mid-supply DC bias with NO feedback path. That is unconditionally stable,
 * and it is what this firmware did before RLD_SENSP/N were first written
 * (they had never been written at all, so they sat at their 0x00 reset).
 *
 * Closing the loop buys common-mode rejection, but an RLD loop is a real
 * feedback system: its phase margin depends on electrode impedance, cable
 * capacitance and the body between them. If it is under-compensated for the
 * hardware in use it oscillates, and the symptom is a burst of oscillation
 * appearing the moment the electrodes complete the loop - not noise, and not
 * anything the digital path can fix.
 *
 * So this is a diagnostic, not a preference: if the burst disappears with
 * passive RLD, the loop is unstable and the derivation is the cause.
 */
#ifndef EMG_RLD_ACTIVE
#undef EMG_RLD_SENSP
#undef EMG_RLD_SENSN
#define EMG_RLD_SENSP  0x00u
#define EMG_RLD_SENSN  0x00u
#endif

/* Lead-off detection. OFF by default: DC detection drives a 6 nA source into
 * every monitored input, and that current develops a voltage across the
 * electrode and source impedance in series with the signal being measured. It
 * is a genuinely useful diagnostic - it is the only thing that distinguishes
 * "noisy" from "not attached" - but it is not free, so it is not always on. */
#ifdef EMG_LEADOFF
#define EMG_LOFF_REG    ((_95_percent << ADS129x_LOFF_COMP_TH_POS) |          \
			 (current_source_mode << ADS129x_LOFF_VLEAD_OFF_EN_POS) | \
			 (_6nA << ADS129x_LOFF_ILEAD_OFF_POS) |               \
			 (DC_lead_off_detection_enable << ADS129x_LOFF_FLEAD_OFF_POS))
#define EMG_CONFIG4_REG (lead_off_comparators_enable << 1)
#define EMG_LOFF_SENSP  EMG_CHANNEL_MASK
#define EMG_LOFF_SENSN  EMG_CHANNEL_MASK
#else
#define EMG_LOFF_REG    0x00u
#define EMG_CONFIG4_REG 0x00u
#define EMG_LOFF_SENSP  0x00u
#define EMG_LOFF_SENSN  0x00u
#endif

#ifndef EMG_CHANNEL_MASK
#define EMG_CHANNEL_MASK  EMG_DEFAULT_CH_MASK
#endif

/* Population count of an 8-bit constant, usable in array bounds. */
#define EMG_POPCOUNT8(m)  ((((m) >> 0) & 1u) + (((m) >> 1) & 1u) + \
			   (((m) >> 2) & 1u) + (((m) >> 3) & 1u) + \
			   (((m) >> 4) & 1u) + (((m) >> 5) & 1u) + \
			   (((m) >> 6) & 1u) + (((m) >> 7) & 1u))

#define EMG_ACTIVE_CHANNELS  EMG_POPCOUNT8(EMG_CHANNEL_MASK)
#define EMG_SAMPLE_RATE_HZ   1000 /* fmod_div_512 in high-resolution mode     */

BUILD_ASSERT(EMG_CHANNEL_MASK != 0u && EMG_CHANNEL_MASK <= 0xFFu,
	     "EMG_CHANNEL_MASK must select at least one of CH1..CH8");

/* ============== 0b. Self-test mode ==============
 * Build with `west build -b <board> . -- -DEMG_SELFTEST=ON` to disconnect the
 * PGA inputs from the electrodes and feed every channel the ADS1298's own
 * internal calibration signal instead: a square wave of +/- (VREFP-VREFN)/2400
 * = +/- 1 mV at fCLK / 2^21 = 0.977 Hz.
 *
 * This is the one measurement that partitions the system. The test signal is
 * generated inside the ADC, downstream of the multiplexer and the electrodes,
 * so if the host plots a clean 1 Hz +/- 1000 uV square wave then the SPI
 * transfer, the 24-bit unpacking, the channel transpose, the framing, the CRC,
 * the UART and the host's decode and display are all proven correct together,
 * and anything wrong with the real signal is analog - electrodes, cabling,
 * jumpers or reference. If the square wave is instead noisy or scrambled, the
 * fault is digital and none of the analog side is worth investigating yet.
 */

#ifdef EMG_SELFTEST
#define EMG_MUX_SETTING  test_signal
#define EMG_INT_TEST     test_signal_generated_internal
#else
#define EMG_MUX_SETTING  normal_electrode_input
#define EMG_INT_TEST     test_signal_driven_external
#endif

/* ============== 0c. Ramp test mode ==============
 * Build with `-DEMG_RAMP_TEST=ON` to replace the ADC samples with a synthetic
 * counter while still being paced by real DRDY events.
 *
 * This is the test the +/-1 mV self-test cannot perform. That signal is ~1 Hz
 * sampled at 1 kSPS and identical on every channel, so within one 32-sample
 * frame every sample holds the same value and every channel holds the same
 * data. A clean square wave therefore proves the bit-level path but says
 * NOTHING about ordering: sample reordering inside a block, channel
 * interleaving, and duplicated or dropped samples are all invisible to it.
 *
 * A per-sample counter is visible to all of them. On the host each channel
 * plots as a sawtooth, and:
 *
 *   - any break in the staircase  -> samples reordered, duplicated or lost
 *   - channels not offset as below -> transpose or interleave error
 *   - sawtooth period != 1.000 s   -> the true sample rate is NOT 1000 SPS,
 *                                     so fCLK is not 2.048 MHz and every
 *                                     INFO frame is advertising a false rate
 *
 * That last one is the point. The host derives its entire timebase from the
 * advertised rate, so a clock that is not 2.048 MHz time-scales every trace -
 * which looks exactly like "the waveform is wrong" while the data is intact.
 */
#define EMG_RAMP_PERIOD     1000u    /* samples per sawtooth = 1 s at 1 kSPS */
#define EMG_RAMP_CH_OFFSET  100000   /* codes between channels, decimal-readable */

/* ============== 0d. Measured sample rate ==============
 * Nothing in this firmware previously measured how fast conversions actually
 * arrive - the rate was assumed from the CONFIG1 divider and an assumed
 * 2.048 MHz fCLK, then advertised as fact in every INFO frame. Count DRDY-driven
 * reads and report the real figure once a second. This is independent of
 * framing, the UART and the host, so it isolates the ADC's own timing. */
static uint32_t samples_this_period;

/* ============== 1. Device-tree handles ============== */
#define SPI_OPERATION   (SPI_OP_MODE_1_8BIT | SPI_OP_MODE_MASTER)  /* mode 1: CPOL=0, CPHA=1 */
static const struct spi_dt_spec  ads_spi = SPI_DT_SPEC_GET(DT_NODELABEL(ads129x), SPI_OPERATION);
static const struct gpio_dt_spec ads_cs  = GPIO_DT_SPEC_GET(DT_NODELABEL(ads129x_cs), gpios);
static const struct gpio_dt_spec ads_rdy = GPIO_DT_SPEC_GET(DT_NODELABEL(ads129x_drdy), gpios);
static const struct gpio_dt_spec ads_rs  = GPIO_DT_SPEC_GET(DT_NODELABEL(ads129x_reset), gpios);
static const struct gpio_dt_spec ads_st  = GPIO_DT_SPEC_GET(DT_NODELABEL(ads129x_start), gpios);

/* The console UART doubles as the stream UART - see the file header. */
static const struct device *const stream_uart =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/* ============== 2. Bus + driver device structs ============== */
static struct spi_dev spi_bus = {
	.pSpecSPI  = &ads_spi,
	.pSpecGPIO = &ads_cs,
};
static struct ads129x_dev ads_dev = {
	.pSpi     = &spi_bus,
	.pDRDYpin = &ads_rdy,
};

/* ============== 3. Acquisition configuration ==============
 * High-resolution mode, internal 2.4 V reference, RLD enabled, PGA gain 6,
 * normal electrode input. Configured for ECG; the same settings still work for
 * surface EMG, see the rate note below.
 *
 * data_rate = fmod_div_512 -> 1000 SPS in HR mode.
 *
 * The ADS1298's sinc^3 decimation filter is -3 dB at 0.262 x f_DR, so 1 kSPS
 * gives 262 Hz of usable bandwidth. That clears the 150 Hz diagnostic-ECG band
 * with margin, and it also clears the 20-450 Hz surface-EMG band that ISEK and
 * SENIAM specify >= 1000 Hz for. 500 SPS would NOT do: 131 Hz cuts into
 * diagnostic ECG. 4 kSPS works too but is eight times more than ECG needs and
 * costs 55 % of the 921600 link instead of 14 %.
 *
 * EMG_SAMPLE_RATE_HZ above MUST track this enum - it is what every INFO frame
 * advertises, and the host derives its entire timebase from it.
 *
 * Gain 6 is the standard ECG setting and stays: full scale is +/- VREF/gain =
 * +/- 400 mV, so a 1 mV simulator output is ~21 000 LSB at 0.0477 uV/LSB while
 * still leaving room for the +/- 300 mV electrode half-cell offset you get on
 * skin. Gain 12 would double resolution but halve headroom to +/- 200 mV.
 *
 * RLD and WCT are configured for the ADS1298ECG-FE limb-lead wiring - see the
 * ADS129x_ECG_* presets in ads129x.h. Both register pairs reset to 0x00, and
 * leaving them there is what made CH1/CH4 unusable and cost the common-mode
 * feedback that ECG needs for mains rejection.
 */
static struct ads129x_emg_config emg_conf = {
	.high_resolution = HighResolution,
	.daisy_enable    = multiple_read_back_mode,
	.clock_enable    = osc_clk_output_disable,
	.data_rate       = fmod_div_512,        /* HR mode = 1000 SPS */
	/* CONFIG2 is all zeros for normal acquisition, but must still be written
	 * - it resets to 0x40 with a reserved bit set. */
	.wct_chop        = chopping_frequency_varies,
	.int_test        = EMG_INT_TEST,        /* see EMG_SELFTEST above */
	.test_amp        = x1times,             /* +/- 1 mV                */
	.test_freq       = fclk_div_2pow21,     /* 0.977 Hz square wave    */
	.pd_rebuf        = internal_reference_buffer_enable,
	.vref_4v         = vref_2_4v,           /* VREFP = 2.4 V -> FS = +/- VREF/gain */
	.rlddef_int      = rldref_generated_internal,
	.pd_rld          = rld_buffer_enable,
	.gain            = gain_6,
	.mux             = EMG_MUX_SETTING,     /* see EMG_SELFTEST above */
	/* Right-leg drive from the average of RA, LA and LL. */
	/* Derived ONLY from electrodes that are physically attached - see the
	 * EMG_ELECTRODES block. RLD drives its sum back into the body through RL,
	 * so feeding it a disconnected input injects that pin's pickup into every
	 * channel, including ones whose own electrodes are fine. */
	.rld_sensp       = EMG_RLD_SENSP,
	.rld_sensn       = EMG_RLD_SENSN,
	/* WCT = (RA + LA + LL)/3, all three amplifiers powered on. */
	.wct1            = EMG_WCT1,
	.wct2            = EMG_WCT2,
	/* Lead-off detection, taken from Luiz et al. 2025 (see
	 * paper/REFERENCE_NOTES.md). DC detection, 6 nA source, 95 % threshold.
	 *
	 * Note their published table sets LOFF_SENSP/LOFF_SENSN to 0x00, which
	 * powers the comparators but monitors nothing - a silent no-op. Monitor
	 * exactly the channels being streamed instead. */
	.loff            = EMG_LOFF_REG,
	.config4         = EMG_CONFIG4_REG,
	.loff_sensp      = EMG_LOFF_SENSP,
	.loff_sensn      = EMG_LOFF_SENSN,
};

/* A wrong byte in any of these four is silent - the part accepts it, the link
 * stays healthy, and the only symptom is a trace that looks subtly wrong. Pin
 * the encodings against the datasheet register maps (sections 9.6.1.7-9.6.1.8
 * and 9.6.1.18-9.6.1.19) so a bad edit fails the build instead. */
BUILD_ASSERT(ADS129x_ECG_RLD_SENSP_RA_LA == 0x02, "RLD_SENSP: IN2P(LA) only");
BUILD_ASSERT(ADS129x_ECG_RLD_SENSN_RA_LA == 0x02, "RLD_SENSN: IN2N(RA) only");
BUILD_ASSERT(ADS129x_ECG_RLD_SENSP_3LEAD == 0x06, "RLD_SENSP: IN2P(LA) + IN3P(LL)");
BUILD_ASSERT(ADS129x_ECG_RLD_SENSN_3LEAD == 0x02, "RLD_SENSN: IN2N(RA)");
BUILD_ASSERT(ADS129x_ECG_WCT1_3LEAD == 0x0B, "WCT1: PD_WCTA=1, WCTA=IN2N(RA)");
BUILD_ASSERT(ADS129x_ECG_WCT2_3LEAD == 0xD4,
	     "WCT2: PD_WCTC=PD_WCTB=1, WCTB=IN2P(LA), WCTC=IN3P(LL)");

/* ============== 4. Sample buffer ==============
 * One block, channel-major, which is the layout emg_frame_build_data() wants.
 * Single rather than double buffered: emg_stream_send_data() copies the frame
 * into the TX ring synchronously, so there is no window in which the link is
 * still reading `block` while we refill it. */
#define EMG_BLOCK_SAMPLES  32   /* 32 samples at 1 kSPS = 32 ms per frame */

static int32_t block[EMG_ACTIVE_CHANNELS][EMG_BLOCK_SAMPLES];
static uint16_t block_fill;

/* RDATAC frames rejected by the status-word alignment check. Any non-zero value
 * points at SPI or DRDY timing rather than at the analog front end. */
static uint32_t frame_sync_errors;

/* DRDY behaviour, measured rather than assumed. transitions/s tells us whether
 * the pin actually toggles: if it is roughly twice the conversion rate the pin
 * is behaving and edge detection is safe, and if it is near zero DRDY is stuck
 * asserted and only level polling can work. */
static uint32_t drdy_transitions;
static uint32_t drdy_low_passes;
static uint32_t drdy_loop_passes;

LOG_MODULE_REGISTER(emg_stream_app, LOG_LEVEL_INF);

/* Scale constants, mirrored into every INFO frame so the host never has to
 * hardcode them. Keep in sync with emg_conf above.
 *   V_in = code / 2^23 * (VREF / gain);  gain 6, VREF 2.4 V -> ~0.0477 uV/LSB.
 *
 * NOTE: EMG_GAIN is the numeric gain. In `enum gain` the symbol gain_6 == 0 -
 * that enum is not ordered by gain value, so never send the register field. */
#define EMG_VREF_UV   2400000
#define EMG_GAIN      6

#ifndef EMG_FW_VERSION
#define EMG_FW_VERSION "dev"
#endif

/* Park every channel NOT named by `mask`: PDn=1 turns the channel's PGA off and
 * MUX=input-shorted keeps its front end quiet.
 *
 * This is not just housekeeping for unwired channels. An un-parked channel on a
 * DC-coupled open input rails its PGA, and that shows up on the channels you do
 * care about, because the RLD loop and the reference are shared. Shorting the
 * input holds the front end at mid-supply instead. */
static int park_channels_outside_mask(const struct spi_dev *bus, uint8_t mask)
{
	uint8_t val = (channel_powerdown << ADS129x_CHnSET_PDn_POS) |
		      (input_shorted     << ADS129x_CHnSET_MUXn_POS);   /* 0x81 */

	for (uint8_t ch = 1u; ch <= ADS129x_NUM_CHANNELS; ch++) {
		if (mask & EMG_CH_MASK_BIT(ch)) {
			continue;
		}

		uint8_t addr[2] = { ADS129x_CMD_WREG(ADS129x_CHnSET(ch)), 0x00 };
		struct spi_buf     a    = { .buf = addr, .len = 2 };
		struct spi_buf_set aset = { .buffers = &a, .count = 1 };
		struct spi_buf     d    = { .buf = &val, .len = 1 };
		struct spi_buf_set dset = { .buffers = &d, .count = 1 };

		int ret = spi_write_register(bus, &aset, &dset);
		if (ret < 0) {
			return ret;
		}
	}
	return 0;
}

static void send_info(uint8_t chip_id)
{
	struct emg_info info = {
		.sample_rate_hz = EMG_SAMPLE_RATE_HZ,
		.vref_uv        = EMG_VREF_UV,
		.gain           = EMG_GAIN,
		.chip_id        = chip_id,
		.n_ch_active    = EMG_ACTIVE_CHANNELS,
		.hr_mode        = 1,
		.uptime_s       = (uint32_t)(k_uptime_get() / 1000),
	};

	/* strncpy would pad to the full field, which is what we want here, but
	 * it also warns about truncation on longer hashes; do it by hand. */
	const char *v = EMG_FW_VERSION;
	for (size_t i = 0; i < EMG_FW_VERSION_LEN; i++) {
		info.fw_version[i] = v[i] != '\0' ? v[i] : '\0';
		if (v[i] == '\0') {
			break;
		}
	}

	emg_stream_send_info(&info);
}

int main(void)
{
	int ret;

	/* Bind the link first. The log backend (proto/src/emg_log_backend.c)
	 * queues through emg_stream so text can never land inside a frame; until
	 * this runs it falls back to polling the UART directly, which is safe
	 * only because no frames exist yet. Doing it before the banner keeps the
	 * whole startup log on the fast path. */
	ret = emg_stream_init(stream_uart);
	if (ret < 0) {
		LOG_ERR("emg_stream_init failed: %d", ret);
		return ret;
	}

	LOG_INF("========================================");
	LOG_INF(" ADS1298 stream - %u ch (mask 0x%02X) @ %u SPS",
		EMG_ACTIVE_CHANNELS, EMG_CHANNEL_MASK, EMG_SAMPLE_RATE_HZ);
	LOG_INF("========================================");
	/* Printed rather than assumed: which channel carries which lead is a
	 * property of the eval board's wiring (SBAU171), not of this firmware, and
	 * it is the first thing to check when a trace looks wrong. */
	LOG_INF("ECG leads (ADS1298ECG-FE): CH1=V6-WCT  CH2=LEAD I (LA-RA)");
	LOG_INF("                           CH3=LEAD II (LL-RA)  CH4=V2-WCT");
	LOG_INF("RLD = avg(RA,LA,LL); WCT = (RA+LA+LL)/3");
	LOG_INF("Channels not in the mask are powered down with inputs shorted.");

	/* =====================================================================
	 * STEP 1 - SETUP: peripherals ready, GPIO directions, hardware reset
	 * ===================================================================== */
	if (!spi_is_ready_dt(&ads_spi)) {
		LOG_ERR("SPI bus not ready");
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&ads_cs) || !gpio_is_ready_dt(&ads_rs) ||
	    !gpio_is_ready_dt(&ads_rdy)) {
		LOG_ERR("Control GPIO(s) not ready");
		return -ENODEV;
	}

	gpio_pin_configure_dt(&ads_cs,  GPIO_OUTPUT_INACTIVE); /* CS idle high        */
	gpio_pin_configure_dt(&ads_rs,  GPIO_OUTPUT_ACTIVE);   /* hold RESET asserted */
	gpio_pin_configure_dt(&ads_rdy, GPIO_INPUT);           /* DRDY input          */
	/* START pin must be LOW for the START *command* (sent by start_continuous)
	 * to take effect; if START is left floating/high the command is ignored and
	 * the ADS never converts -> no DRDY. active-high pin, INACTIVE = physical LOW. */
	gpio_pin_configure_dt(&ads_st,  GPIO_OUTPUT_INACTIVE);

	/* Reset pulse. Logical values honor GPIO_ACTIVE_LOW: 1 = asserted (LOW),
	 * 0 = released (HIGH). Datasheet: hold >= 2 tCLK, then wait >= 18 tCLK
	 * (~9 us @ 2.048 MHz) after release before the first SPI command. */
	gpio_pin_set_dt(&ads_rs, 1);   /* assert RESET  */
	k_msleep(10);
	gpio_pin_set_dt(&ads_rs, 0);   /* release RESET */
	k_usleep(20);

	/* =====================================================================
	 * STEP 2 - CONFIG: configure CH1-CH4, park CH5-CH8, verify the chip ID.
	 * ===================================================================== */
	/* Configure all eight with the normal gain/mux settings, then park the
	 * ones outside the mask. Doing it in that order means the parked channels
	 * end up in a known state (PD + input shorted) rather than at their reset
	 * default, which is powered on.
	 *
	 * NULL buffers: we stream raw frames, so the driver allocates none. */
	ret = ads_emg_init(&ads_dev, &emg_conf, NULL, NULL, ADS129x_NUM_CHANNELS);
	if (ret < 0) {
		LOG_ERR("ads_emg_init failed: %d", ret);
		return ret;
	}

	ret = park_channels_outside_mask(&spi_bus, EMG_CHANNEL_MASK);
	if (ret < 0) {
		LOG_ERR("parking unused channels failed: %d", ret);
		return ret;
	}

	/* Registers are not trustworthy just because they were written. */
	ret = ads_emg_verify(&ads_dev, &emg_conf, ADS129x_NUM_CHANNELS);
	if (ret < 0) {
		LOG_ERR("register readback failed: %d", ret);
	} else if (ret > 0) {
		LOG_ERR("%d register(s) did not take - the device is NOT running the "
			"requested configuration; every rate below is wrong", ret);
	} else {
		LOG_INF("all configuration registers verified");
	}

	ret = ads_read_id(&ads_dev);
	if (ret < 0) {
		LOG_ERR("ID read failed: %d", ret);
		return ret;
	}
	const uint8_t chip_id = (uint8_t)ret;
	LOG_INF("Chip ID = 0x%02X", chip_id);
	/* Low 5 bits == 0x12 means 8-channel (0x92 = ADS1298, 0xD2 = ADS1298R). */
	if ((chip_id & 0x1Fu) != 0x12u) {
		LOG_WRN("Unexpected ID - check wiring/power; continuing anyway.");
	}

	/* =====================================================================
	 * STEP 3 - READ + STREAM
	 * ===================================================================== */
	ret = ads_emg_start_continuous(&ads_dev);
	if (ret < 0) {
		LOG_ERR("start_continuous failed: %d", ret);
		return ret;
	}
	LOG_INF("Streaming mask 0x%02X (%u ch), %u samples/frame",
		EMG_CHANNEL_MASK, EMG_ACTIVE_CHANNELS, EMG_BLOCK_SAMPLES);

	send_info(chip_id);

	int64_t last_ms = k_uptime_get();
	int64_t last_info_ms = last_ms;
	uint32_t last_dropped = 0;
	uint32_t last_sync_errors = 0;
	bool drdy_prev = false;
#ifdef EMG_RAMP_TEST
	uint32_t ramp_counter = 0;
#endif

	while (1) {
		/* Data-ready detection by DRDY LEVEL (not edge).
		 *
		 * On this ADS1298R eval-board wiring, DRDY is held low continuously while
		 * a conversion result is pending and does not produce the clean falling
		 * edges a GPIO edge-interrupt needs, so the ISR path never advances past
		 * the first sample. Polling the asserted level is robust: gpio_pin_get_dt
		 * is logical on this active-low pin -> 1 = asserted (data ready), 0 = idle.
		 * (start_continuous() still arms the edge IRQ; if edges do occur it also
		 * sets data_ready, so this loop works either way.) */
		/* Data-ready detection.
		 *
		 * Two failure modes, and they pull in opposite directions:
		 *
		 *   LEVEL polling  re-arms for as long as DRDY stays asserted, so one
		 *                  conversion is read several times. Measured on real
		 *                  data: 4.62 copies per conversion, 1032 SPS emitted
		 *                  carrying 223 SPS of actual samples (B-020).
		 *
		 *   EDGE detection needs DRDY to actually deassert between samples. If
		 *                  it does not - which is what the original comment
		 *                  here reported for this wiring - exactly one sample
		 *                  is ever taken and the stream dies (B-022).
		 *
		 * Level is the default because it produces data; duplicates are a
		 * degradation, silence is not. Which one is correct depends on how
		 * DRDY actually behaves on this board, which the counters below
		 * measure rather than assume. */
		const bool drdy_asserted = (gpio_pin_get_dt(&ads_rdy) == 1);

		if (drdy_asserted != drdy_prev) {
			drdy_transitions++;
		}
		if (drdy_asserted) {
			drdy_low_passes++;
		}
		drdy_loop_passes++;

#ifdef EMG_DRDY_EDGE
		if (drdy_asserted && !drdy_prev) {
			ads_dev.data_ready = true;
		}
#else
		if (drdy_asserted) {
			ads_dev.data_ready = true;
		}
#endif
		drdy_prev = drdy_asserted;

		const int64_t now = k_uptime_get();

		/* INFO once a second, so a host attaching mid-stream learns the
		 * rate, gain and reference within a second. */
		if (now - last_info_ms >= 1000) {
			/* Capture the true interval before resetting: the loop
			 * only reaches here on a pass where a sample or a poll
			 * happened, so it is >= 1000 ms but rarely exactly that.
			 * Dividing by the real elapsed time keeps the measured
			 * rate honest. */
			const int64_t elapsed = now - last_info_ms;

			last_info_ms = now;
			send_info(chip_id);

			/* Measured conversion rate. This is the ADC's real output
			 * rate, counted from DRDY-driven reads - not the rate
			 * derived from the CONFIG1 divider and advertised in the
			 * INFO frame. If the two disagree, fCLK is not 2.048 MHz
			 * and every host timebase is scaled by the ratio. */
			const uint32_t measured =
				elapsed > 0
					? (uint32_t)(((uint64_t)samples_this_period *
						      1000u) / (uint64_t)elapsed)
					: 0u;
			if (measured < (EMG_SAMPLE_RATE_HZ * 95u) / 100u ||
			    measured > (EMG_SAMPLE_RATE_HZ * 105u) / 100u) {
				LOG_WRN("measured %u SPS vs %u nominal - fCLK is not "
					"2.048 MHz, host timebase is wrong",
					measured, EMG_SAMPLE_RATE_HZ);
			} else {
				LOG_INF("measured %u SPS (nominal %u)", measured,
					EMG_SAMPLE_RATE_HZ);
			}
			samples_this_period = 0u;

			/* DRDY health. `transitions` near zero means the pin never
			 * deasserts, so every sample is read repeatedly and edge
			 * detection would stall the stream entirely. */
			LOG_INF("drdy: %u transitions, asserted on %u of %u polls",
				drdy_transitions, drdy_low_passes, drdy_loop_passes);
			drdy_transitions = 0u;
			drdy_low_passes = 0u;
			drdy_loop_passes = 0u;

			/* Electrode integrity, decoded from the RDATAC status word
			 * we already receive every sample. A set bit means that
			 * electrode is OFF - which is the difference between "the
			 * signal is noisy" and "there is no electrode attached",
			 * and is not otherwise distinguishable from the trace. */
#ifdef EMG_LEADOFF
			{
				const uint8_t offp = ADS129x_STATUS_LOFFP(ads_dev.status);
				const uint8_t offn = ADS129x_STATUS_LOFFN(ads_dev.status);
				const uint8_t badp = offp & EMG_CHANNEL_MASK;
				const uint8_t badn = offn & EMG_CHANNEL_MASK;

				if (badp || badn) {
					LOG_WRN("electrode OFF: P=0x%02X N=0x%02X "
						"(bit0=CH1) - check the leads before "
						"trusting any rate", badp, badn);
				}
			}
#endif

			if (frame_sync_errors != last_sync_errors) {
				LOG_WRN("RDATAC status word misaligned on %u frame(s)"
					" - SPI/DRDY timing, not signal quality",
					frame_sync_errors - last_sync_errors);
				last_sync_errors = frame_sync_errors;
			}
		}

		if (!ads_dev.data_ready) {
			/* Watchdog: warn if no sample arrives for 2 s (e.g. START not low,
			 * DRDY unwired, or the board lost power). */
			if (now - last_ms > 2000) {
				LOG_WRN("no data in 2s: drdy=%d (0=idle, 1=asserted)", 
					gpio_pin_get_dt(&ads_rdy));
				last_ms = now;
			}
			k_yield();
			continue;
		}
		last_ms = now;

		/* 3a. Clock the 27-byte frame in and sign-extend the four live
		 *     channels straight into the block. One copy, no buffering. */
		int32_t sample[EMG_ACTIVE_CHANNELS];
		ret = ads_emg_read_frame_masked(&ads_dev, sample, EMG_CHANNEL_MASK);
		if (ret == -EIO) {
			/* Status word not aligned - the frame was discarded rather
			 * than decoded into convincing-looking noise. Counted and
			 * reported once a second below; a non-zero count means the
			 * SPI/DRDY timing is wrong, not that the signal is bad. */
			frame_sync_errors++;
			continue;
		}
		if (ret < 0) {
			continue;      /* -EBUSY simply means no sample this pass */
		}

		samples_this_period++;

#ifdef EMG_RAMP_TEST
		/* Substitute a synthetic counter for the ADC data, but keep the
		 * real DRDY pacing above so the sawtooth period still measures
		 * the true conversion rate. See section 0c. */
		(void)sample;
		for (uint8_t ch = 0; ch < EMG_ACTIVE_CHANNELS; ch++) {
			block[ch][block_fill] =
				(int32_t)((uint32_t)ch * EMG_RAMP_CH_OFFSET) +
				(int32_t)(ramp_counter % EMG_RAMP_PERIOD);
		}
		ramp_counter++;
#else
		for (uint8_t ch = 0; ch < EMG_ACTIVE_CHANNELS; ch++) {
			block[ch][block_fill] = sample[ch];
		}
#endif
		block_fill++;

		/* 3b. Block complete: transmit it. emg_stream_send_data() only copies
		 *     into the TX ring, so this never waits on the UART. */
		if (block_fill >= EMG_BLOCK_SAMPLES) {
			block_fill = 0;

			/* Back-pressure: the TX ring rejected a frame, so the link
			 * could not keep up. Missed *conversions* are not directly
			 * detectable here - the ADS gives no sample counter - but the
			 * host sees them as measured SPS below nominal. */
			uint8_t flags = 0;
			const uint32_t dropped = emg_stream_dropped();
			if (dropped != last_dropped) {
				flags |= EMG_FLAG_OVERFLOW;
				last_dropped = dropped;
			}

			emg_stream_send_data((uint32_t)now, flags, &block[0][0],
					     EMG_BLOCK_SAMPLES, EMG_ACTIVE_CHANNELS,
					     EMG_BLOCK_SAMPLES, EMG_CHANNEL_MASK);
		}
	}

	return 0;
}
