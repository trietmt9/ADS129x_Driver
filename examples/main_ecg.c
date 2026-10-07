/*
 * ECG acquisition example: one channel, median + 40 Hz low-pass on the board,
 * framed binary samples to the host.
 *
 * Snapshot of src/main.c. To use it, copy over src/main.c and rebuild - the
 * build compiles src/main.c only, so editing this file changes nothing.
 *
 * Start-up order and DRDY/RDATA rules: ../../WORKFLOW.md sections 4d-4e.
 */
#include <main.h>
#include <emg_frame.h>
#include <zephyr/drivers/uart.h>
#include <bio_filter.h>
#include <zephyr/sys/ring_buffer.h>

LOG_MODULE_REGISTER(ads_read, LOG_LEVEL_INF);

/* ============ config ============ */

#define ADS_CHANNEL         2          /* 1-based; flat trace => try another  */
#define EMG_SAMPLE_RATE_HZ  2000       /* nominal; real rate is measured      */
#define EMG_VREF_UV         2400000
#define EMG_GAIN            6          /* +/- 400 mV FS, 0.0477 uV/LSB        */
#define BLOCK_SAMPLES       32         /* 32 ms per frame at 1 kSPS           */

/* Conversions held between acquisition and transmission.
 *
 * The ADS1298 has no FIFO, so a conversion not read before the next one is
 * gone. Anything that stalls the loop - a frame write, a log line - therefore
 * costs samples unless the read itself is decoupled from the send. This ring
 * is that decoupling: service_adc() only has to get the sample OUT of the
 * chip, and framing drains whenever it next gets the chance.
 *
 * 256 sample-sets is ~226 ms at 1130 SPS, far more than any stall observed
 * (worst measured gap is reported as gap_worst_us). Costs 4 bytes per channel
 * per slot: 1 KB at one channel, 8 KB at eight.
 */
#define RING_SAMPLES        256

/* Shortest gap between two reads of the chip.
 *
 * DRDY is polled by LEVEL because edge detection stalls on this wiring
 * (B-022), and the pin stays asserted rather than pulsing, so the loop can
 * read the SAME conversion repeatedly (B-020). This floor suppresses that.
 *
 * Size it to outlast a RE-READ, never a conversion. A re-read happens as fast
 * as the loop can go round - the masked SPI read is ~27 us and the rest of the
 * iteration tens of us - whereas a genuine conversion is hundreds of us away.
 * Sizing this from the NOMINAL period was wrong (B-033 fix, first attempt): at
 * 700 us it capped reads at 1429 SPS while the part was converting at ~1527,
 * so it silently dropped 6.4 % of conversions.
 *
 * 250 us gives a 4000 SPS ceiling - above anything this CONFIG1 data rate can
 * produce - so it can never gate a real conversion.
 *
 * Note what this is NOT for: the timebase is correct because conversions++
 * counts DELIVERED samples, so advertised_rate always equals the rate the host
 * receives. Residual duplication costs bandwidth and stair-steps the trace, but
 * it cannot skew BPM. Do not tighten this to chase duplicates.
 */
#define MIN_READ_INTERVAL_US 250u
#define RATE_SETTLE_MS      3000
#define ECG_HIGHPASS_HZ     2.0f
#define ECG_LOWPASS_HZ      40.0f

#ifdef ADS_STREAM_ALL
#define N_STREAM  ADS129x_NUM_CHANNELS
#define CH_MASK   EMG_CH_MASK_CONTIGUOUS
#else
#define N_STREAM  1
#define CH_MASK   EMG_CH_MASK_BIT(ADS_CHANNEL)
#endif

/* ============ hardware ============ */

#define SPI_OPERATION  (SPI_OP_MODE_1_8BIT | SPI_OP_MODE_MASTER)  /* mode 1 */

static const struct spi_dt_spec  ads_spi = SPI_DT_SPEC_GET(DT_NODELABEL(ads129x), SPI_OPERATION);
static const struct gpio_dt_spec ads_cs  = GPIO_DT_SPEC_GET(DT_NODELABEL(ads129x_cs), gpios);
static const struct gpio_dt_spec ads_rdy = GPIO_DT_SPEC_GET(DT_NODELABEL(ads129x_drdy), gpios);
static const struct gpio_dt_spec ads_rs  = GPIO_DT_SPEC_GET(DT_NODELABEL(ads129x_reset), gpios);
static const struct gpio_dt_spec ads_st  = GPIO_DT_SPEC_GET(DT_NODELABEL(ads129x_start), gpios);

static const struct device *const uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static struct spi_dev     spi_bus = { .pSpecSPI = &ads_spi, .pSpecGPIO = &ads_cs };
static struct ads129x_dev ads     = { .pSpi = &spi_bus, .pDRDYpin = &ads_rdy };

/* RLD holds the body at mid-supply; no derivation, no WCT, no lead-off. */
static struct ads129x_emg_config conf = {
	.high_resolution = HighResolution,
	.daisy_enable    = multiple_read_back_mode,
	.clock_enable    = osc_clk_output_disable,
	.data_rate       = fmod_div_512,

	.wct_chop = chopping_frequency_varies,
	.int_test = test_signal_driven_external,
	.test_amp = x1times,
	.test_freq = fclk_div_2pow21,

	.pd_rebuf   = internal_reference_buffer_enable,
	.vref_4v    = vref_2_4v,
	.rlddef_int = rldref_generated_internal,
	.pd_rld     = rld_buffer_enable,

	.gain = gain_6,
	.mux  = normal_electrode_input,

	.rld_sensp  = 0x00, .rld_sensn  = 0x00,
	.wct1       = 0x00, .wct2       = 0x00,
	.loff       = 0x00, .config4    = 0x00,
	.loff_sensp = 0x00, .loff_sensn = 0x00,
};

/* ============ state ============ */

/* One conversion's worth of channels, as stored in the ring. */
#define SAMPLE_BYTES (N_STREAM * sizeof(int32_t))

RING_BUF_DECLARE(sample_ring, RING_SAMPLES * N_STREAM * sizeof(int32_t));

/* Drain target. Channel-major with stride BLOCK_SAMPLES, which is the layout
 * emg_frame_build_data() expects; the ring is sample-major, so drain_block()
 * transposes.
 */
static int32_t  block[N_STREAM][BLOCK_SAMPLES];
static uint8_t  frame[EMG_FRAME_MAX_SIZE];
static uint32_t seq;

/* Measured over the first few seconds, then fixed for the session. */
static uint16_t advertised_rate = EMG_SAMPLE_RATE_HZ;
static bool     rate_locked;
static uint32_t settle_conversions;
static int64_t  settle_start;

static uint32_t conversions;
static uint32_t gap_overruns, gap_lost, gap_worst_us;
static uint32_t ring_overflows;   /* read from the ADS, no room to keep */

static struct bio_filter filt;

/* ============ acquisition ============ */

/* Runs from the loop AND from send_frame(). */
static bool service_adc(void)
{
	static uint32_t prev_cyc;

	/* STEP 1: is a conversion ready, and has enough time passed that it can
	 * be a NEW one? DRDY stays asserted on this wiring, so the interval floor
	 * is what stops the same conversion being read twice (B-020). */
	if (gpio_pin_get_dt(&ads_rdy) != 1) {
		return false;
	}
	const uint32_t now_cyc = k_cycle_get_32();
	if (prev_cyc != 0u &&
	    k_cyc_to_us_floor32(now_cyc - prev_cyc) < MIN_READ_INTERVAL_US) {
		return false;
	}
	ads.data_ready = true;

	/* RDATA latches the sample, so the read cannot be torn. */
	/* STEP 2: read it. RDATA latches, so the transfer cannot be torn. */
	int32_t ch[N_STREAM];
	if (ads_emg_read_rdata_masked(&ads, ch, CH_MASK) < 0) {
		return false;
	}
	/* STEP 3: time the interval, to catch conversions we were too slow for. */
	const uint32_t cyc = now_cyc;
	if (prev_cyc != 0u) {
		const uint32_t us = k_cyc_to_us_floor32(cyc - prev_cyc);
		const uint32_t nominal_us = 1000000u / advertised_rate;

		gap_worst_us = MAX(gap_worst_us, us);
		if (us > (nominal_us * 3u) / 2u) {
			gap_overruns++;
			gap_lost += (us / nominal_us) - 1u;
		}
	}
	prev_cyc = cyc;

	/* STEP 4: condition, then queue for transmission.
	 *
	 * Space is checked before writing rather than trusting the return of
	 * ring_buf_put(): a short write would put half a sample-set in the ring
	 * and desynchronise every frame after it. Dropping the whole set keeps
	 * the ring aligned, and the count says it happened.
	 */
	int32_t out[N_STREAM];
	for (uint8_t c = 0; c < N_STREAM; c++) {
		float y = 0.0f;
		out[c] = bio_filter_process(&filt, (float)ch[c], &y) == 0
				 ? (int32_t)y
				 : ch[c];
	}

	if (ring_buf_space_get(&sample_ring) < SAMPLE_BYTES) {
		ring_overflows++;
		return true;
	}
	ring_buf_put(&sample_ring, (uint8_t *)out, SAMPLE_BYTES);

	/* Counted here, not at the read: advertised_rate is derived from this,
	 * and the host's whole timebase is derived from advertised_rate. It must
	 * therefore count samples DELIVERED, never reads attempted - a read that
	 * is dropped at the ring would otherwise inflate every BPM by the ratio. */
	conversions++;
	return true;
}

/* Move one frame's worth out of the ring into the channel-major block.
 * Returns false if a whole block is not yet available. */
static bool drain_block(void)
{
	if (ring_buf_size_get(&sample_ring) < BLOCK_SAMPLES * SAMPLE_BYTES) {
		return false;
	}

	for (uint16_t i = 0; i < BLOCK_SAMPLES; i++) {
		int32_t s[N_STREAM];
		ring_buf_get(&sample_ring, (uint8_t *)s, SAMPLE_BYTES);
		for (uint8_t c = 0; c < N_STREAM; c++) {
			block[c][i] = s[c];
		}
	}
	return true;
}

/* Takes samples between bytes - the write outlasts a sample period. */
static void send_frame(int len)
{
	for (int i = 0; i < len; i++) {
		uart_poll_out(uart, frame[i]);
		(void)service_adc();
	}
}

static void send_info(uint8_t chip_id, int64_t now)
{
	struct emg_info info = {
		.sample_rate_hz = advertised_rate,
		.vref_uv        = EMG_VREF_UV,
		.gain           = EMG_GAIN,
		.chip_id        = chip_id,
		.n_ch_active    = N_STREAM,
		.hr_mode        = 1,
		.uptime_s       = (uint32_t)(now / 1000),
	};
	send_frame(emg_frame_build_info(frame, sizeof(frame), &info));
}

static void report(int64_t now, int64_t elapsed)
{
	/* STEP 1: what rate did we actually achieve? */
	const uint32_t sps =
		(uint32_t)(((uint64_t)conversions * 1000u) / (uint64_t)elapsed);

	/* STEP 2: average across the settling window, then fix it for good. */
	if (!rate_locked) {
		settle_conversions += conversions;
		const int64_t since = now - settle_start;

		if (since >= RATE_SETTLE_MS) {
			const uint32_t avg = (uint32_t)(
				((uint64_t)settle_conversions * 1000u) / (uint64_t)since);

			/* ALWAYS advertise the measured rate. It counts samples
			 * delivered, so it equals the rate the host actually receives,
			 * and a self-consistent rate times every beat correctly however
			 * odd the number looks. Substituting nominal here breaks that:
			 * it was tried on 2026-09-07 and reported a 30 BPM source as
			 * 7.5 when duplicates pushed the real figure to ~4x nominal.
			 *
			 * A figure far from nominal still means something is wrong -
			 * duplicate reads - so say so loudly, but do not act on it. */
			if (avg > 100u && avg < 40000u) {
				advertised_rate = (uint16_t)avg;
			}
			if (avg > EMG_SAMPLE_RATE_HZ * 3u / 2u) {
				LOG_WRN("measured %u SPS is %u%% of nominal %u - duplicate "
					"reads likely; timebase still correct",
					avg, avg * 100u / EMG_SAMPLE_RATE_HZ,
					EMG_SAMPLE_RATE_HZ);
			}
			rate_locked = true;

			bio_filter_init(&filt, (float)advertised_rate, ECG_HIGHPASS_HZ, ECG_LOWPASS_HZ);
			LOG_INF("rate locked at %u SPS (nominal %u)",
				advertised_rate, EMG_SAMPLE_RATE_HZ);
		}
	}

	/* STEP 3: report and reset the counters. */
	LOG_INF("%u SPS | gaps %u (~%u lost, worst %u us) | ring %u/%u ovf %u",
		sps, gap_overruns, gap_lost, gap_worst_us,
		(uint32_t)(ring_buf_size_get(&sample_ring) / SAMPLE_BYTES),
		(uint32_t)RING_SAMPLES, ring_overflows);

	conversions = gap_overruns = gap_lost = gap_worst_us = 0;
	ring_overflows = 0;
}

/* ============ bring-up ============ */

static int ads_bring_up(void)
{
	int ret;

	/* STEP 1: are the peripherals up? */
	if (!spi_is_ready_dt(&ads_spi) || !device_is_ready(uart)) {
		LOG_ERR("SPI or UART not ready");
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&ads_cs) || !gpio_is_ready_dt(&ads_rs) ||
	    !gpio_is_ready_dt(&ads_rdy) || !gpio_is_ready_dt(&ads_st)) {
		LOG_ERR("GPIO not ready");
		return -ENODEV;
	}

	/* STEP 2: GPIO directions and idle states. */
	gpio_pin_configure_dt(&ads_cs,  GPIO_OUTPUT_INACTIVE);  /* CS idle high      */
	gpio_pin_configure_dt(&ads_rs,  GPIO_OUTPUT_ACTIVE);    /* RESET asserted    */
	gpio_pin_configure_dt(&ads_rdy, GPIO_INPUT);
	gpio_pin_configure_dt(&ads_st,  GPIO_OUTPUT_INACTIVE);  /* START must be LOW */

	/* STEP 3: hardware reset pulse. */
	gpio_pin_set_dt(&ads_rs, 1);
	k_msleep(10);
	gpio_pin_set_dt(&ads_rs, 0);
	k_usleep(20);                            /* >= 18 tCLK before first command */

	/* STEP 4: write the configuration. */
	ret = ads_emg_init(&ads, &conf, NULL, NULL, ADS129x_NUM_CHANNELS);
	if (ret < 0) {
		LOG_ERR("init failed: %d", ret);
		return ret;
	}

	/* Confirm the registers hold what we wrote. */
	/* STEP 5: confirm the registers hold what we wrote. */
	ret = ads_emg_verify(&ads, &conf, ADS129x_NUM_CHANNELS);
	if (ret < 0) {
		LOG_ERR("register readback failed: %d", ret);
	} else if (ret > 0) {
		LOG_ERR("%d register(s) did NOT take", ret);
	} else {
		LOG_INF("all registers verified");
	}

	/* STEP 6: identify the part. */
	ret = ads_read_id(&ads);
	if (ret < 0) {
		LOG_ERR("ID read failed: %d", ret);
		return ret;
	}
	LOG_INF("Chip ID = 0x%02X", (uint8_t)ret);
	return ret;
}

int main(void)
{
	LOG_INF("ADS1298 stream - CH%u @ %u SPS", ADS_CHANNEL, EMG_SAMPLE_RATE_HZ);

	/* STEP 1: reset, configure and verify the ADC. */
	const int chip_id = ads_bring_up();
	if (chip_id < 0) {
		return chip_id;    /* Both buffers are optional - a streaming caller passes NULL and keeps the samples itself. */

	}

	/* Median removes spikes, then the low-pass removes HF noise. */
	/* STEP 2: build the filter. */
	if (bio_filter_init(&filt, (float)EMG_SAMPLE_RATE_HZ, ECG_HIGHPASS_HZ, ECG_LOWPASS_HZ) < 0) {
		LOG_ERR("bio_filter_init failed");
		return -EINVAL;
	}
	LOG_INF("filter: median + 4th-order band-pass %d.%d-%d Hz", 0, 5,
		(int)ECG_LOWPASS_HZ);

	/* STEP 3: start converting. */
	int ret = ads_emg_start_rdata(&ads);   /* converts, stays out of RDATAC */
	if (ret < 0) {
		LOG_ERR("start failed: %d", ret);
		return ret;
	}
	/* STEP 3b: is DRDY actually wired?
	 *
	 * Everything downstream trusts this pin. A pin that never changes state
	 * cannot be distinguished from a slow one by the acquisition loop, and the
	 * failure is silent and expensive: stuck ASSERTED makes the loop free-run,
	 * reading one stale conversion over and over (B-020, B-033); stuck IDLE
	 * makes it read nothing at all; INTERMITTENT gives erratic drops and jitter
	 * that no amount of firmware tuning will fix.
	 *
	 * B-022 concluded from "edge detection takes one sample then stalls
	 * forever" that this wiring does not produce clean edges. A pin stuck
	 * asserted produces exactly that symptom, so that conclusion needs
	 * re-testing on a known-good connection - see B-034.
	 *
	 * Count transitions over a window instead of sampling once. At the nominal
	 * rate the pin should toggle about twice per conversion.
	 */
	{
		const uint32_t win_ms = 50u;
		const int64_t  t_end  = k_uptime_get() + win_ms;
		int last = gpio_pin_get_dt(&ads_rdy);
		uint32_t edges = 0;

		while (k_uptime_get() < t_end) {
			const int now = gpio_pin_get_dt(&ads_rdy);
			if (now != last) {
				edges++;
				last = now;
			}
		}

		/* One edge per conversion is the floor; expect roughly two. */
		const uint32_t expect = (EMG_SAMPLE_RATE_HZ * win_ms) / 1000u;

		if (edges == 0u) {
			LOG_ERR("DRDY never changed state in %u ms (stuck %s).",
				win_ms, last == 1 ? "ASSERTED" : "IDLE");
			LOG_ERR("  CHECK THE WIRE. Every sample time below is invalid.");
		} else if (edges < expect / 2u) {
			LOG_WRN("DRDY toggled %u times in %u ms, expected >= %u - "
				"intermittent connection? Drops and jitter will follow.",
				edges, win_ms, expect);
		} else {
			LOG_INF("DRDY alive: %u edges in %u ms", edges, win_ms);
		}
	}

	LOG_INF("streaming");

	/* STEP 4: acquire, block, send. */
	int64_t last_report = k_uptime_get();
	settle_start = last_report;

	while (1) {
		const bool got = service_adc();
		const int64_t now = k_uptime_get();

		/* One frame per pass, not a drain-everything loop: send_frame()
		 * services the ADC between bytes, so catching up a backlog this
		 * way still keeps the chip read on time. */
		if (drain_block()) {
			send_frame(emg_frame_build_data(
				frame, sizeof(frame), seq++, (uint32_t)now, 0,
				&block[0][0], BLOCK_SAMPLES, N_STREAM,
				BLOCK_SAMPLES, CH_MASK));
		}

		if (now - last_report >= 1000) {
			send_info((uint8_t)chip_id, now);
			report(now, now - last_report);
			last_report = now;
		}

		if (!got) {
			k_yield();
		}
	}
	return 0;
}
