#include <ads129x.h>
#include <string.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ads129x, LOG_LEVEL_INF);

/*=================================================================
 *                        Register access
 *=================================================================*/

/**
 * @brief Write one register: WREG opcode, count-1, value, in one transaction.
 * @param pBus  SPI bus and chip select.
 * @param reg   Register address.
 * @param val   Value to write.
 * @return 0 on success, negative errno on failure.
 */
static int write_reg(const struct spi_dev* pBus, uint8_t reg, uint8_t val)
{
    uint8_t cmd[3] = { ADS129x_CMD_WREG(reg), 0x00u, val };

    return spi_write_bytes(pBus, cmd, sizeof(cmd));
}

/**
 * @brief Read one register: RREG opcode, count-1, value on the third byte.
 * @param pBus  SPI bus and chip select.
 * @param reg   Register address.
 * @return Register value 0..255, or negative errno on failure.
 */
static int read_reg(const struct spi_dev* pBus, uint8_t reg)
{
    uint8_t tx[3] = { ADS129x_CMD_RREG(reg), 0x00u, 0x00u };
    uint8_t rx[3] = { 0 };

    struct spi_buf     tx_buf     = { .buf = tx, .len = 3 };
    struct spi_buf_set tx_buf_set = { .buffers = &tx_buf, .count = 1 };
    struct spi_buf     rx_buf     = { .buf = rx, .len = 3 };
    struct spi_buf_set rx_buf_set = { .buffers = &rx_buf, .count = 1 };

    int ret = spi_read_register(pBus, &tx_buf_set, &rx_buf_set);
    if(ret < 0) return ret;

    return (int)rx[2];
}

/*=================================================================
 *                     Configure and verify
 *=================================================================*/

/**
 * @brief Configure the device: CONFIG1-4, CHnSET, RLD, WCT and lead-off.
 *
 * Sends SDATAC first, since register writes are ignored in continuous mode,
 * and waits for the internal reference to settle before returning.
 *
 * @param pAds        Device handle.
 * @param pEMG        Configuration to apply.
 * @param pEMGBuffer  Optional ring buffers, initialised if non-NULL.
 * @param pDSPBuffer  Optional DSP double buffer, initialised if non-NULL.
 * @param channels    Channels to configure, 1..ADS129x_NUM_CHANNELS.
 * @return 0 on success, negative errno on failure.
 */
int ads_emg_init(const struct ads129x_dev* pAds, const struct ads129x_emg_config* pEMG, struct emg_buffer* pEMGBuffer, struct dsp_double_buffer* pDSPBuffer, uint8_t channels)
{
    int ret;

    /* STEP 1: validate arguments. */
    if(pAds == NULL || pAds->pSpi->pSpecSPI == NULL ||
       pAds->pSpi->pSpecGPIO == NULL || pEMG == NULL ||
       channels == 0 || channels > ADS129x_NUM_CHANNELS)
    {
        return -EINVAL;
    }

    /* STEP 2: initialise the optional buffers. A streaming caller passes NULL. */
    if(pEMGBuffer != NULL)
    {
        memset(pEMGBuffer, 0, sizeof(*pEMGBuffer));
        for(uint8_t chn = 0; chn < ADS129x_NUM_CHANNELS; chn++)
        {
            ring_buf_init(&pEMGBuffer->channel[chn].rb, ADS129x_BUFFER_SIZE,
                          pEMGBuffer->channel[chn].dataBuffer);
        }
    }
    if(pDSPBuffer != NULL)
    {
        memset(pDSPBuffer, 0, sizeof(*pDSPBuffer));
        pDSPBuffer->active     = pDSPBuffer->buffer_a;
        pDSPBuffer->processing = pDSPBuffer->buffer_b;
    }

    /* STEP 3: leave continuous mode - register writes are ignored in it. */
    ret = spi_send_cmd(pAds->pSpi, ADS129x_CMD_SDATAC);
    if(ret < 0) return ret;

    /* STEP 4: CONFIG1 - power mode, readback mode, CLK output, data rate. */
    ret = write_reg(pAds->pSpi, ADS129x_CONFIG1,
                    (pEMG->high_resolution << ADS129x_CONFIG1_HR_POS)       |
                    (pEMG->daisy_enable    << ADS129x_CONFIG1_DAISY_EN_POS) |
                    (pEMG->clock_enable    << ADS129x_CONFIG1_CLK_EN_POS)   |
                    (pEMG->data_rate       << ADS129x_CONFIG1_DR_POS));
    if(ret < 0) return ret;

    /* STEP 5: CONFIG2 - test signal. Written even when all-zero: bits 7:6 are reserved and must be 0. */
    ret = write_reg(pAds->pSpi, ADS129x_CONFIG2,
                    (pEMG->wct_chop  << ADS129x_CONFIG2_WCT_CHOP_POS)  |
                    (pEMG->int_test  << ADS129x_CONFIG2_INT_TEST_POS)  |
                    (pEMG->test_amp  << ADS129x_CONFIG2_TEST_AMP_POS)  |
                    (pEMG->test_freq << ADS129x_CONFIG2_TEST_FREQ_POS));
    if(ret < 0) return ret;

    /* STEP 6: CONFIG3 - reference and RLD buffers. Bit 6 must be written 1. */
    ret = write_reg(pAds->pSpi, ADS129x_CONFIG3,
                    (pEMG->pd_rebuf   << ADS129x_CONFIG3_PD_REFBUF_POS) |
                    (pEMG->vref_4v    << ADS129x_CONFIG3_VREF_4V_POS)   |
                    (pEMG->rlddef_int << ADS129x_CONFIG3_RLDREF_INT_POS)|
                    (pEMG->pd_rld     << ADS129x_CONFIG3_PD_RLD_POS)    |
                    (1u               << 6));
    if(ret < 0) return ret;

    /* STEP 7: wait for the internal reference to settle. */
    k_msleep(150);

    /* STEP 8: CHnSET for every live channel. */
    for(uint8_t ch = 1; ch <= channels; ch++)
    {
        ret = write_reg(pAds->pSpi, ADS129x_CHnSET(ch),
                        (pEMG->mux  << ADS129x_CHnSET_MUXn_POS) |
                        (pEMG->gain << ADS129x_CHnSET_GAINn_POS));
        if(ret < 0) return ret;
    }

    /* STEP 9: RLD derivation, WCT, lead-off. Route only ATTACHED electrodes
     * to RLD. */
    const struct { uint8_t reg; uint8_t val; } rest[] = {
        { ADS129x_RLD_SENSP,  pEMG->rld_sensp  },
        { ADS129x_RLD_SENSN,  pEMG->rld_sensn  },
        { ADS129x_WCT1,       pEMG->wct1       },
        { ADS129x_WCT2,       pEMG->wct2       },
        { ADS129x_LOFF,       pEMG->loff       },
        { ADS129x_CONFIG4,    pEMG->config4    },
        { ADS129x_LOFF_SENSP, pEMG->loff_sensp },
        { ADS129x_LOFF_SENSN, pEMG->loff_sensn },
    };

    for(size_t i = 0; i < ARRAY_SIZE(rest); i++)
    {
        ret = write_reg(pAds->pSpi, rest[i].reg, rest[i].val);
        if(ret < 0) return ret;
    }

    return 0;
}

/**
 * @brief Read every configuration register back and compare against @p pEMG.
 *
 * Call after ads_emg_init(). Each mismatch is logged as
 * "REG: wrote 0xNN, reads 0xNN".
 *
 * @param pAds      Device handle.
 * @param pEMG      Configuration that was applied.
 * @param channels  Channels to check, 1..ADS129x_NUM_CHANNELS.
 * @return Number of registers that disagree (0 = all correct), or negative
 *         errno if the bus failed.
 */
int ads_emg_verify(const struct ads129x_dev* pAds, const struct ads129x_emg_config* pEMG, uint8_t channels)
{
    /* STEP 1: validate arguments and work out what was written. */
    if(pAds == NULL || pEMG == NULL) return -EINVAL;

    const uint8_t want_chn = (pEMG->mux  << ADS129x_CHnSET_MUXn_POS) |
                             (pEMG->gain << ADS129x_CHnSET_GAINn_POS);

    const struct { uint8_t reg; uint8_t want; const char *name; } checks[] = {
        { ADS129x_CONFIG1, (pEMG->high_resolution << ADS129x_CONFIG1_HR_POS)       |
                           (pEMG->daisy_enable    << ADS129x_CONFIG1_DAISY_EN_POS) |
                           (pEMG->clock_enable    << ADS129x_CONFIG1_CLK_EN_POS)   |
                           (pEMG->data_rate       << ADS129x_CONFIG1_DR_POS), "CONFIG1" },
        { ADS129x_CONFIG2, (pEMG->wct_chop  << ADS129x_CONFIG2_WCT_CHOP_POS)  |
                           (pEMG->int_test  << ADS129x_CONFIG2_INT_TEST_POS)  |
                           (pEMG->test_amp  << ADS129x_CONFIG2_TEST_AMP_POS)  |
                           (pEMG->test_freq << ADS129x_CONFIG2_TEST_FREQ_POS), "CONFIG2" },
        { ADS129x_CONFIG3, (pEMG->pd_rebuf   << ADS129x_CONFIG3_PD_REFBUF_POS) |
                           (pEMG->vref_4v    << ADS129x_CONFIG3_VREF_4V_POS)   |
                           (pEMG->rlddef_int << ADS129x_CONFIG3_RLDREF_INT_POS)|
                           (pEMG->pd_rld     << ADS129x_CONFIG3_PD_RLD_POS)    |
                           (1u               << 6), "CONFIG3" },
        { ADS129x_RLD_SENSP, pEMG->rld_sensp, "RLD_SENSP" },
        { ADS129x_RLD_SENSN, pEMG->rld_sensn, "RLD_SENSN" },
        { ADS129x_WCT1,      pEMG->wct1,      "WCT1"      },
        { ADS129x_WCT2,      pEMG->wct2,      "WCT2"      },
        { ADS129x_LOFF,      pEMG->loff,      "LOFF"      },
        { ADS129x_CONFIG4,   pEMG->config4,   "CONFIG4"   },
    };

    int bad = 0;

    /* STEP 2: check the global registers. */
    for(size_t i = 0; i < ARRAY_SIZE(checks); i++)
    {
        int got = read_reg(pAds->pSpi, checks[i].reg);
        if(got < 0) return got;
        if((uint8_t)got != checks[i].want)
        {
            LOG_ERR("%s: wrote 0x%02X, reads 0x%02X", checks[i].name,
                    checks[i].want, (uint8_t)got);
            bad++;
        }
    }

    /* STEP 3: check the per-channel registers. */
    for(uint8_t ch = 1; ch <= channels; ch++)
    {
        int got = read_reg(pAds->pSpi, ADS129x_CHnSET(ch));
        if(got < 0) return got;
        if((uint8_t)got != want_chn)
        {
            LOG_ERR("CH%uSET: wrote 0x%02X, reads 0x%02X", ch, want_chn,
                    (uint8_t)got);
            bad++;
        }
    }

    return bad;
}

/*=================================================================
 *                          Reading data
 *=================================================================*/

/**
 * @brief Check the status word and unpack the selected channels.
 *
 * The status word's leading nibble is always 1100; anything else means the
 * read did not start on a frame boundary.
 *
 * @param pAds     Device handle; its status field is updated.
 * @param raw      27-byte frame: status word then eight channels.
 * @param pOut     Receives one sample per set bit, ascending channel order.
 * @param ch_mask  Channels to extract, bit 0 = CH1.
 * @return 0 on success, -EIO if the status word was misaligned.
 */
static int unpack_frame(struct ads129x_dev* pAds, const uint8_t* raw,
                        int32_t* pOut, uint8_t ch_mask)
{
    /* STEP 1: reject a frame we did not start on a boundary. */
    if((raw[0] & 0xF0u) != 0xC0u) return -EIO;

    /* STEP 2: keep the status word - it carries the lead-off bits. */
    pAds->status[0] = raw[0];
    pAds->status[1] = raw[1];
    pAds->status[2] = raw[2];

    /* STEP 3: extract and sign-extend the selected channels. */
    uint8_t out = 0u;

    for(uint8_t ch = 0; ch < ADS129x_NUM_CHANNELS; ch++)
    {
        if(!(ch_mask & (1u << ch))) continue;

        const uint8_t *p = &raw[3 + ch * 3];

        int32_t sample = ((int32_t)p[0] << 16) |
                         ((int32_t)p[1] << 8)  |
                          (int32_t)p[2];

        if(sample & 0x800000) sample |= (int32_t)0xFF000000;   /* sign-extend */

        pOut[out++] = sample;
    }
    return 0;
}

/** @brief True if @p ch_mask is non-zero and within the channel count. */
static bool mask_valid(uint8_t ch_mask)
{
    return ch_mask != 0u && (ch_mask >> ADS129x_NUM_CHANNELS) == 0u;
}

/**
 * @brief Read one conversion using RDATA. Preferred read.
 *
 * RDATA latches the sample on the command, so the transfer cannot be torn by a
 * conversion completing part-way through. Pair with ads_emg_start_rdata().
 *
 * @param pAds     Device handle.
 * @param pOut     Receives one sample per set bit, ascending channel order.
 * @param ch_mask  Channels to extract, bit 0 = CH1. Must be non-zero.
 * @return 0 on success, -EIO if the status word was misaligned (sample
 *         discarded), negative errno on failure.
 */
int ads_emg_read_rdata_masked(struct ads129x_dev* pAds, int32_t* pOut,
                              uint8_t ch_mask)
{
    /* STEP 1: validate arguments. */
    if(pAds == NULL || pOut == NULL || !mask_valid(ch_mask)) return -EINVAL;

    /* STEP 2: build the transaction - opcode and data together. */
    uint8_t tx[1 + ADS129x_NUM_BYTES] = { ADS129x_CMD_RDATA };
    uint8_t rx[1 + ADS129x_NUM_BYTES] = { 0 };

    struct spi_buf     tx_buf     = { .buf = tx, .len = sizeof(tx) };
    struct spi_buf_set tx_buf_set = { .buffers = &tx_buf, .count = 1 };
    struct spi_buf     rx_buf     = { .buf = rx, .len = sizeof(rx) };
    struct spi_buf_set rx_buf_set = { .buffers = &rx_buf, .count = 1 };

    /* STEP 3: transfer. */
    int ret = spi_read_register(pAds->pSpi, &tx_buf_set, &rx_buf_set);
    if(ret < 0) return ret;

    /* STEP 4: unpack. The data follows the opcode byte. */
    return unpack_frame(pAds, &rx[1], pOut, ch_mask);   /* data follows the opcode */
}

/**
 * @brief Read one frame in continuous mode, extracting selected channels.
 *
 * Requires ads_emg_start_continuous(). Prefer ads_emg_read_rdata_masked():
 * continuous mode does not latch.
 *
 * @param pAds     Device handle.
 * @param pOut     Receives one sample per set bit, ascending channel order.
 * @param ch_mask  Channels to extract, bit 0 = CH1. Must be non-zero.
 * @return 0 on success, -EBUSY if no sample is pending, -EIO if the status
 *         word was misaligned, negative errno on failure.
 */
int ads_emg_read_frame_masked(struct ads129x_dev* pAds, int32_t* pOut,
                              uint8_t ch_mask)
{
    /* STEP 1: validate arguments. */
    if(pAds == NULL || pOut == NULL || !mask_valid(ch_mask)) return -EINVAL;

    /* STEP 2: claim the pending sample. Cleared before the read so a DRDY
     * arriving during it is not lost. */
    if(!pAds->data_ready) return -EBUSY;
    pAds->data_ready = false;

    uint8_t raw[ADS129x_NUM_BYTES] = {0};
    struct spi_buf     rx_buf = { .buf = raw, .len = sizeof(raw) };
    struct spi_buf_set rx     = { .buffers = &rx_buf, .count = 1 };

    /* STEP 3: clock in the frame. */
    int ret = spi_read_stream(pAds->pSpi, &rx);
    if(ret < 0) return ret;

    /* STEP 4: unpack. */
    return unpack_frame(pAds, raw, pOut, ch_mask);
}

/**
 * @brief Read one frame in continuous mode, channels CH1..CH@p channels.
 *
 * Convenience wrapper over ads_emg_read_frame_masked().
 *
 * @param pAds      Device handle.
 * @param pOut      Receives @p channels samples, CH1 first.
 * @param channels  How many channels, 1..ADS129x_NUM_CHANNELS.
 * @return 0 on success, -EBUSY if no sample is pending, -EIO if the status
 *         word was misaligned, negative errno on failure.
 */
int ads_emg_read_frame(struct ads129x_dev* pAds, int32_t* pOut, uint8_t channels)
{
    if(channels == 0 || channels > ADS129x_NUM_CHANNELS) return -EINVAL;

    return ads_emg_read_frame_masked(pAds, pOut,
                                     (uint8_t)((1u << channels) - 1u));
}

/*=================================================================
 *                        Start conversions
 *=================================================================*/

/**
 * @brief Start converting; data is handed over only when RDATA asks for it.
 *
 * The START pin must be held low for the command to take effect.
 *
 * @param pAds  Device handle.
 * @return 0 on success, negative errno on failure.
 */
int ads_emg_start_rdata(struct ads129x_dev* pAds)
{
    if(pAds == NULL) return -EINVAL;

    /* STEP 1: make sure continuous mode is off - it is the power-on default. */
    int ret = spi_send_cmd(pAds->pSpi, ADS129x_CMD_SDATAC);
    if(ret < 0) return ret;

    /* STEP 2: start converting. */
    return spi_send_cmd(pAds->pSpi, ADS129x_CMD_START);
}

/** @brief DRDY interrupt handler: flags that a conversion is available. */
static void drdy_callback(const struct device *port, struct gpio_callback *cb,
                          uint32_t pins)
{
    ARG_UNUSED(port);
    ARG_UNUSED(pins);

    struct ads129x_dev *pAds = CONTAINER_OF(cb, struct ads129x_dev, drdy_cb);
    pAds->data_ready = true;
}

/**
 * @brief Start converting in continuous mode and arm the DRDY interrupt.
 *
 * The START pin must be held low for the command to take effect. Prefer
 * ads_emg_start_rdata().
 *
 * @param pAds  Device handle.
 * @return 0 on success, negative errno on failure.
 */
int ads_emg_start_continuous(struct ads129x_dev* pAds)
{
    if(pAds == NULL) return -EINVAL;

    /* STEP 1: arm DRDY before starting, so the first edge is not missed. */
    gpio_init_callback(&pAds->drdy_cb, drdy_callback, BIT(pAds->pDRDYpin->pin));
    gpio_add_callback(pAds->pDRDYpin->port, &pAds->drdy_cb);

    int ret = gpio_pin_interrupt_configure_dt(pAds->pDRDYpin, GPIO_INT_EDGE_FALLING);
    if(ret < 0) return ret;

    /* STEP 2: enter continuous mode. */
    ret = spi_send_cmd(pAds->pSpi, ADS129x_CMD_RDATAC);
    if(ret < 0) return ret;

    /* STEP 3: start converting. The START pin must be held low for this. */
    return spi_send_cmd(pAds->pSpi, ADS129x_CMD_START);
}

/*=================================================================
 *                           Identity
 *=================================================================*/

/**
 * @brief Read the ID register.
 *
 * Low 5 bits == 0x12 identifies the 8-channel parts: 0x92 is the ADS1298,
 * 0xD2 the ADS1298R.
 *
 * @param pAds  Device handle.
 * @return Register value 0..255, or negative errno on failure.
 */
int ads_read_id(const struct ads129x_dev* pAds)
{
    if(pAds == NULL) return -EINVAL;

    return read_reg(pAds->pSpi, ADS129x_ID);
}
