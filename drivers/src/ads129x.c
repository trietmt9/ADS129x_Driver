#include <ads129x.h>
#include <string.h>
/*=================================================================
 *                         ADS1298 Device APIs
 *=================================================================*/

/**
 * @brief Initialize ADS129x for EMG acquisition
 * @param pDev: Pointer to device struct
 * @param pEMG: Pointer to EMG config struct
 * @param pEMGBuffer: Pointer to EMG buffer (will be initialized)
 * @param pDSPBuffer: Pointer to DSP buffer (will be initialized)
 * @param channels: number of channels 
 * @return 0 on success, negative on fail
 */
int ads_emg_init(const struct spi_dev* pDev, const struct ads129x_emg_config* pEMG, struct emg_buffer* pEMGBuffer, struct dsp_double_buffer* pDSPBuffer, uint8_t channels)
{
    int ret;
    /* Check pointer */
    if(pDev != NULL && pDev->pSpecSPI != NULL && pDev->pSpecGPIO != NULL 
    && pEMG != NULL && (channels <= ADS129x_NUM_CHANNELS && channels != 0))
    {
       /* Check Buffer */
       if(pEMGBuffer != NULL && pDSPBuffer != NULL)
       {
            /* Initialize EMG Buffers */
            memset(pEMGBuffer, 0, sizeof(*pEMGBuffer));

            /* Initialize ring buffer for each channel */
            for(uint8_t chn = 0; chn < ADS129x_NUM_CHANNELS; chn++)
            {
                ring_buf_init(&pEMGBuffer->channel[chn].rb, 
                              ADS129x_BUFFER_SIZE, 
                              pEMGBuffer->channel[chn].dataBuffer);
            }
            pEMGBuffer->overflow = false;
            pEMGBuffer->sample_count = 0;

            /* Initialize DSP Buffers */
            memset(pDSPBuffer, 0, sizeof(*pDSPBuffer));
            pDSPBuffer->active = pDSPBuffer->buffer_a;
            pDSPBuffer->processing = pDSPBuffer->buffer_b;
            pDSPBuffer->fill_index = 0; 
            pDSPBuffer->ready = false;

            /* Send Stop reading data command */
            ret = spi_send_cmd(pDev, ADS129x_CMD_SDATAC);
            if(ret < 0) return ret;
            /* Add configuration to CONFIG1 */
            /* Address buffer */
            uint8_t address[2] = {ADS129x_CMD_WREG(ADS129x_CONFIG1), 0x00};
            struct spi_buf addr_buf = { .buf = address, .len = 2};
            struct spi_buf_set addr_buf_set = { .buffers = &addr_buf, .count = 1};
            
            /* Config buffer */
            uint8_t config = (pEMG->high_resolution << ADS129x_CONFIG1_HR_POS) | 
                             (pEMG->daisy_enable << ADS129x_CONFIG1_DAISY_EN_POS) | 
                             (pEMG->clock_enable << ADS129x_CONFIG1_CLK_EN_POS) | 
                             (pEMG->data_rate << ADS129x_CONFIG1_DR_POS);
            struct spi_buf emgConfig_buf = { .buf = &config , .len = 1};
            struct spi_buf_set emgConfig_buf_set = {.buffers = & emgConfig_buf, .count = 1};
            
            /* Write to the device */
            ret = spi_write_register(pDev, &addr_buf_set, &emgConfig_buf_set);                                                                                                                                             
            if(ret < 0) return ret;
            /* Add configuration to CONFIG3 */
            /* Address buffer */
            address[0] = ADS129x_CMD_WREG(ADS129x_CONFIG3);

            /* Config buffer */
            config = (pEMG->pd_rebuf   << ADS129x_CONFIG3_PD_REFBUF_POS) | 
                     (pEMG->vref_4v    << ADS129x_CONFIG3_VREF_4V_POS)   | 
                     (pEMG->rlddef_int << ADS129x_CONFIG3_RLDREF_INT_POS)| 
                     (pEMG->pd_rld     << ADS129x_CONFIG3_PD_RLD_POS)    |
                     (1                << 6); /* Reserve bit required write to 1 in datasheet*/
            /* Write to the device */
            ret = spi_write_register(pDev, &addr_buf_set, &emgConfig_buf_set);
            if(ret < 0) return ret;
            k_msleep(150);  // Wait for reference to settle

            /* Add configuration to CHnSET */  
            for (uint8_t count = 1; count <= channels; ++count)
            {
                /* Address buffer */
                address[0] = ADS129x_CMD_WREG(ADS129x_CHnSET(count));               
                /* Config buffer */
                config = (pEMG->mux  << ADS129x_CHnSET_MUXn_POS) | 
                         (pEMG->gain << ADS129x_CHnSET_GAINn_POS); 
                /* Write to the device */
                ret = spi_write_register(pDev, &addr_buf_set, &emgConfig_buf_set);
                if(ret < 0) return ret;
            }
            return 0;
       }
    }
    return -EINVAL;
}

/**
 * @brief Read EMG data from ADS129x
 * @param pDev: Pointer to device struct
 * @param pBuffer: Pointer to EMG buffer
 * @param mode: Read mode (ADS_READ_CONTINUOUS or ADS_READ_SINGLE)
 * @return 0 on success, negative on fail
 */
int ads_emg_read(const struct spi_dev* pDev,
                 struct emg_buffer* pBuffer,
                 enum ads129x_read_mode mode);

/**
 * @brief Process EMG data with DSP double buffer
 * @param pEMGBuffer: Pointer to EMG buffer (source)
 * @param pDSPBuffer: Pointer to DSP buffer (destination)
 * @return 0 on success, negative on fail
 */
int ads_emg_dsp(struct emg_buffer* pEMGBuffer,
                struct dsp_double_buffer* pDSPBuffer);

/**
 * @brief Start ADS129x conversion
 * @param pDev: Pointer to device struct
 * @return 0 on success, negative on fail
 */
int ads_emg_start(const struct spi_dev* pDev);

/**
 * @brief Stop ADS129x conversion
 * @param pDev: Pointer to device struct
 * @return 0 on success, negative on fail
 */
int ads_emg_stop(const struct spi_dev* pDev);

/**
 * @brief Read ADS129x device ID
 * @param pDev: Pointer to device struct
 * @return Device ID on success, negative on fail
 */
int ads_read_id(const struct spi_dev* pDev);