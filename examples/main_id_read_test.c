/*
 * EXAMPLE: ADS1298 SPI communication bring-up test (chip ID read).
 *
 * Confirms the STM32 <-> ADS1298 SPI link works by reading the device ID
 * register (0x00) and printing chip availability. Two valid 8-channel IDs:
 *   0x92 = ADS1298   (100 10 010)
 *   0xD2 = ADS1298R  (110 10 010)
 * Both share low 5 bits 0x12 (reserved 10b + channel 010b = 8-channel), so
 * the availability check masks with 0x1F and compares to 0x12.
 *
 * This file is a reference example. To run it, either point CMakeLists at
 * `examples/main_id_read_test.c` instead of `src/main.c`, or copy it over
 * src/main.c. It uses the same SPI/GPIO device-tree nodes as the app
 * (ads129x, ads129x_cs, ads129x_drdy, ads129x_reset).
 */
#include <main.h>

/* ============== SPI device tree structs ============== */
#define SPI_OPERATION   (SPI_OP_MODE_1_8BIT | SPI_OP_MODE_MASTER)
static const struct spi_dt_spec  ads_spi = SPI_DT_SPEC_GET(DT_NODELABEL(ads129x), SPI_OPERATION);
static const struct gpio_dt_spec ads_cs  = GPIO_DT_SPEC_GET(DT_NODELABEL(ads129x_cs), gpios);
static const struct gpio_dt_spec ads_rdy = GPIO_DT_SPEC_GET(DT_NODELABEL(ads129x_drdy), gpios);
static const struct gpio_dt_spec ads_rs  = GPIO_DT_SPEC_GET(DT_NODELABEL(ads129x_reset), gpios);

/* ============== Bus + driver structs ============== */
static struct spi_dev spi_bus = {
	.pSpecSPI  = &ads_spi,
	.pSpecGPIO = &ads_cs,
};
static struct ads129x_dev ads_dev = {
	.pSpi     = &spi_bus,
	.pDRDYpin = &ads_rdy,
};

LOG_MODULE_REGISTER(ads_id_test, LOG_LEVEL_INF);

/* Valid 8-channel ADS1298/ADS1298R share low 5 bits == 0x12 (reserved 10b +
 * channel 010b). Family bits [7:5] distinguish 0x92 (ADS1298) vs 0xD2 (R). */
#define ADS1298_ID_MASK   0x1Fu
#define ADS1298_ID_VALUE  0x12u

static void decode_id(uint8_t id)
{
	/* Datasheet Table 17: [7:5]=family, [4:3]=reserved(10b), [2:0]=channels. */
	uint8_t family = (id >> 5) & 0x07u;   /* bits [7:5] */
	uint8_t nu_ch  = id & 0x07u;          /* bits [2:0] */
	const char *ch = (nu_ch == 0) ? "4-channel" :
			 (nu_ch == 1) ? "6-channel" :
			 (nu_ch == 2) ? "8-channel" : "unknown-channel";
	const char *fam = (family == 0x4) ? "ADS129x" :
			  (family == 0x6) ? "ADS129xR" : "unknown-family";
	LOG_INF("  decode: %s, %s (family bits 0x%X)", ch, fam, family);
}

int main(void)
{
	int ret;

	LOG_INF("========================================");
	LOG_INF(" ADS1298 SPI communication test");
	LOG_INF("========================================");

	/* --- 1. Peripherals ready? --- */
	if (!spi_is_ready_dt(&ads_spi)) {
		LOG_ERR("SPI bus not ready");
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&ads_cs) || !gpio_is_ready_dt(&ads_rs) ||
	    !gpio_is_ready_dt(&ads_rdy)) {
		LOG_ERR("Control GPIO(s) not ready");
		return -ENODEV;
	}
	spi_bus_dumb_config(&spi_bus);

	/* --- 2. Configure control lines --- */
	gpio_pin_configure_dt(&ads_cs, GPIO_OUTPUT_INACTIVE); /* CS deasserted (HIGH) */
	gpio_pin_configure_dt(&ads_rs, GPIO_OUTPUT_ACTIVE);   /* RESET asserted  (LOW) */
	gpio_pin_configure_dt(&ads_rdy, GPIO_INPUT);

	/* --- 3. Reset pulse ---
	 * Logical values respect GPIO_ACTIVE_LOW: 1=asserted(LOW), 0=released(HIGH).
	 * Datasheet: hold >=2 tCLK, then wait >=18 tCLK (~9 us @ 2.048 MHz).
	 */
	k_msleep(1);
	gpio_pin_set_dt(&ads_rs, 0);   /* release RESET */
	k_usleep(20);

	/* --- 4. Exit continuous-read mode ---
	 * The ADS1298 boots in RDATAC; register reads (RREG) are ignored until
	 * SDATAC is sent. Skipping this is the #1 reason an ID read returns junk.
	 */
	ret = spi_send_cmd(&spi_bus, ADS129x_CMD_SDATAC);
	if (ret < 0) {
		LOG_ERR("SDATAC command failed: %d", ret);
		return ret;
	}
	k_msleep(1);

	/* --- 5. Read the ID register --- */
	ret = ads_read_id(&ads_dev);
	if (ret < 0) {
		LOG_ERR("SPI transfer failed: %d", ret);
		return ret;
	}

	uint8_t id = (uint8_t)ret;
	LOG_INF("ID register (0x00) = 0x%02X", id);
	decode_id(id);

	/* --- 6. Verdict --- */
	if ((id & ADS1298_ID_MASK) == ADS1298_ID_VALUE) {
		LOG_INF("RESULT: PASS - ADS1298 detected, SPI link OK");
	} else if (id == 0x00 || id == 0xFF) {
		LOG_ERR("RESULT: FAIL - got 0x%02X (no response).", id);
		LOG_ERR("  Check: SCLK on J3.3, MISO/MOSI not swapped, common GND,");
		LOG_ERR("  board power (+5V/+3.3V on J4), JP21=1-2, JP5 open (PWDN).");
		return -EIO;
	} else {
		LOG_WRN("RESULT: link responds but ID 0x%02X is not an ADS1298.", id);
		LOG_WRN("  Possible: wrong reset timing, bit slip, or a different ADS129x.");
	}

	/* --- 7. Stability re-reads (catch flaky wiring / marginal SCLK) --- */
	LOG_INF("Re-reading ID 10x to check stability...");
	int ok = 0;
	for (int i = 0; i < 10; i++) {
		ret = ads_read_id(&ads_dev);
		uint8_t v = (ret < 0) ? 0 : (uint8_t)ret;
		if (ret >= 0 && (v & ADS1298_ID_MASK) == ADS1298_ID_VALUE) {
			ok++;
		}
		LOG_INF("  read %2d: 0x%02X%s", i, v,
			((v & ADS1298_ID_MASK) == ADS1298_ID_VALUE) ? "" : "  <-- mismatch");
		k_msleep(50);
	}
	LOG_INF("Stable reads: %d/10", ok);
	LOG_INF("========================================");
	LOG_INF(" test complete");
	LOG_INF("========================================");

	/* --- 8. Continuous availability monitor ---
	 * Keep polling the ID register so the chip's presence can be watched live
	 * (e.g. unplug/replug the eval board). Prints the ID and a PRESENT/ABSENT
	 * verdict once a second, forever.
	 */
	LOG_INF("Entering availability monitor (ID poll @ 1 Hz)...");
	while (1) {
		ret = ads_read_id(&ads_dev);
		if (ret < 0) {
			LOG_ERR("chip ID: ABSENT (SPI transfer failed: %d)", ret);
		} else {
			uint8_t v = (uint8_t)ret;
			bool present = (v != 0x00) && (v != 0xFF) &&
				       ((v & ADS1298_ID_MASK) == ADS1298_ID_VALUE);
			LOG_INF("chip ID = 0x%02X -> %s", v,
				present ? "PRESENT (ADS1298)" : "ABSENT / not an ADS1298");
		}
		k_msleep(1000);
	}

	return 0;
}
