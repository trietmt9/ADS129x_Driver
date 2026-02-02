#ifndef __ADS129X_H__
#define __ADS129X_H__
#include <spi_bus.h>
#include <zephyr/sys/ring_buffer.h>
/*=================================================================
 *                          ADS1298 GENERAL MACROS
 *=================================================================*/
/*======================= NUMBER OF CHANNELS ======================*/
#define ADS1294_NUM_CHANNELS                                     0x04u
#define ADS1296_NUM_CHANNELS                                     0x06u
#define ADS1298_NUM_CHANNELS                                     0x08u
#ifdef ADS129x_4CHANNELS
#define ADS129x_NUM_CHANNELS                                     ADS1294_NUM_CHANNELS
#elif defined(ADS129x_6CHANNELS)
#define ADS129x_NUM_CHANNELS                                     ADS1296_NUM_CHANNELS
#else
#define ADS129x_NUM_CHANNELS                                     ADS1298_NUM_CHANNELS
#endif 
/*====================== ADS129x BUFFER SIZE ======================*/
#define ADS129x_BUFFER_SIZE                                      256

/*======================= DOUBLE BUFFER DSP =======================*/                                                                         
#define DSP_BLOCK_SIZE                                           64 

/*=================================================================
 *                          ADS1298 COMMANDS
 *=================================================================*/

/*======================== SYSTEM COMMANDS ========================*/
#define ADS129x_CMD_WAKEUP  /* Wakeup from standby mode       */ 0x02u 
#define ADS129x_CMD_STANDBY /* Enter standby mode             */ 0x04u
#define ADS129x_CMD_RESET   /* Reset the device               */ 0x06u
#define ADS129x_CMD_START   /* Start/restart conversions      */ 0x08u 
#define ADS129x_CMD_STOP    /* Stop conversion                */ 0x0Au

/*======================= DATA READ COMMANDS =======================*/
#define ADS129x_CMD_RDATAC  /* Enable Read Data Continuous    */ 0x10u //* This mode is the default mode at power up.
#define ADS129x_CMD_SDATAC  /* Stop Read Data Continuously    */ 0x11u
#define ADS129x_CMD_RDATA   /* Read data by command           */ 0x12u

/*===================== REGISTER READ COMMANDS =====================*/
#define ADS129x_CMD_RREG(r) /* Read registers                 */ (0x20u|(r))
#define ADS129x_CMD_WREG(r) /* Write registers                */ (0x40u|(r))

/*=================================================================
 *                           ADS129x Registers 
 *=================================================================*/
#define ADS129x_ID          /* Device ID                      */ 0x00u 
#define ADS129x_CONFIG1     /* Configuration Register 1       */ 0x01u 
#define ADS129x_CONFIG2     /* Configuration Register 2       */ 0x02u
#define ADS129x_CONFIG3     /* Configuration Register 3       */ 0x03u 
#define ADS129x_LOFF        /* Lead-Off Control               */ 0x04u 
#define ADS129x_CHnSET_BASE /* Channels Setting               */ 0x05u 
#define ADS129x_CHnSET(n)   /* No. Channel to set             */ (ADS129x_CHnSET_BASE + ((n) -1)) // 0x05 - 0x0C
#define ADS129x_RLD_SENSP   /* RLD Positive Signal Derivation */ 0x0Du
#define ADS129x_RLD_SENSN   /* RLD Negative Signal Derivation */ 0x0Eu 
#define ADS129x_LOFF_SENSP  /* Lead-Off Positive Signal       */ 0x0Fu
#define ADS129x_LOFF_SENSN  /* Lead-Off Negative Signal       */ 0x10u
#define ADS129x_FLIP        /* Lead-Off Flip                  */ 0x11u
#define ADS129x_STATP       /* Lead-Off Positive Status       */ 0x12u
#define ADS129x_STATN       /* Lead-Off Negative Status       */ 0x13u
#define ADS129x_GPIO        /* General Purpose I/O            */ 0x14u
#define ADS129x_PACE        /* Pace detect                    */ 0x15u
#define ADS129x_RESP        /* Respiration Control            */ 0x16u
#define ADS129x_CONFIG4     /* Configuration 4                */ 0x17u
#define ADS129x_WCT1        /* Wilson Central Terminal 1      */ 0x18u
#define ADS129x_WCT2        /* Wilson Central Terminal 2      */ 0x19u

/*=================================================================
 *                          ADS1298 Registers bit masked 
 *=================================================================*/
#define ADS129x_CONFIG1_HR_POS                                   0x7u
#define ADS129x_CONFIG1_HR_MASK                                  (1u << ADS129x_CONFIG1_HR_POS)
#define ADS129x_CONFIG1_DAISY_EN_POS                             0x6u
#define ADS129x_CONFIG1_DAISY_EN_MASKED                          (1u << ADS129x_CONFIG1_DAISY_EN_POS)
#define ADS129x_CONFIG1_CLK_EN_POS                               0x5u
#define ADS129x_CONFIG1_CLK_EN_MASKED                            (1u << ADS129x_CONFIG1_CLK_EN_POS)
#define ADS129x_CONFIG1_DR_POS                                   0x0u
#define ADS129x_CONFIG1_DR_MASKED                                (0x07u << ADS129x_CONFIG1_DR_POS)
/* CONFIG2 Register */
#define ADS129x_CONFIG2_WCT_CHOP_POS                             0x5u
#define ADS129x_CONFIG2_WCT_CHOP_MASKED                          (1u << ADS129x_CONFIG2_WCT_CHOP_POS)
#define ADS129x_CONFIG2_INT_TEST_POS                             0x4u 
#define ADS129x_CONFIG2_INT_TEST_MASKED                          (1u << ADS129x_CONFIG2_INT_TEST_POS)
#define ADS129x_CONFIG2_TEST_AMP_POS                             0x2u 
#define ADS129x_CONFIG2_TEST_AMP_MASKED                          (1u << ADS129x_CONFIG2_TEST_AMP_POS)
#define ADS129x_CONFIG2_TEST_FREQ_POS                            0x0u
#define ADS129x_CONFIG2_TEST_FREQ_MASKED                         (0x03u << ADS129x_CONFIG2_TEST_FREQ_POS)
/* CONFIG3 Register */
#define ADS129x_CONFIG3_PD_REFBUF_POS                            0x7u 
#define ADS129x_CONFIG3_PD_REFBUF_MASKED                         (1u << ADS129x_CONFIG3_PD_REFBUF_POS)
#define ADS129x_CONFIG3_VREF_4V_POS                              0x5u
#define ADS129x_CONFIG3_VREF_4V_MASKED                           (1u << ADS129x_CONFIG3_VREF_4V_POS)
#define ADS129x_CONFIG3_RLD_MEAS_POS                             0x4u 
#define ADS129x_CONFIG3_RLD_MEAS_MASKED                          (1u << ADS129x_CONFIG3_RLD_MEAS_POS)
#define ADS129x_CONFIG3_RLDREF_INT_POS                           0x3u 
#define ADS129x_CONFIG3_RLDREF_INT_MASKED                        (1u << ADS129x_CONFIG3_RLDREF_INT_POS)
#define ADS129x_CONFIG3_PD_RLD_POS                               0x2u 
#define ADS129x_CONFIG3_PD_RLD_MASKED                            (1u << ADS129x_CONFIG3_PD_RLD_POS)
#define ADS129x_CONFIG3_RLD_LOFF_SENS_POS                        0x1u 
#define ADS129x_CONFIG3_RLD_LOFF_SENS_MASKED                     (1u << ADS129x_CONFIG3_RLD_LOFF_SENS_POS)
#define ADS129x_CONFIG3_RLD_STAT_POS                             0x0u 
#define ADS129x_CONFIG3_RLD_STAT_MASKED                          (1u << ADS129x_CONFIG3_RLD_STAT_POS)
/* LOFF Register */
#define ADS129x_LOFF_COMP_TH_POS                                 0x5u
#define ADS129x_LOFF_COMP_TH_MASKED                              (0x07u << ADS129x_LOFF_COMP_TH_POS)
#define ADS129x_LOFF_VLEAD_OFF_EN_POS                            0x4u 
#define ADS129x_LOFF_VLEAD_OFF_EN_MASKED                         (1u << ADS129x_LOFF_VLEAD_OFF_EN_POS)
#define ADS129x_LOFF_ILEAD_OFF_POS                               0x2u 
#define ADS129x_LOFF_ILEAD_OFF_MASKED                            (0x03u << ADS129x_LOFF_ILEAD_OFF_POS)
#define ADS129x_LOFF_FLEAD_OFF_POS                               0x0u
#define ADS129x_LOFF_FLEAD_OFF_MASKED                            (0x03u << ADS129x_LOFF_FLEAD_OFF_POS)
/* CHnSET Register */
#define ADS129x_CHnSET_PDn_POS                                   0x7u
#define ADS129x_CHnSET_PDn_MASKED                                (1u << ADS129x_CHnSET_PDn_POS)
#define ADS129x_CHnSET_GAINn_POS                                 0x4u 
#define ADS129x_CHnSET_GAINn_MASKED                              (0x07u << ADS129x_CHnSET_GAINn_POS)
#define ADS129x_CHnSET_MUXn_POS                                  0x0u 
#define ADS129x_CHnSET_MUXn_MASKED                               (0x07u << ADS129x_CHnSET_MUXn_POS)
/* RLD_SENSP Register */
#define ADS129x_RLDnP_POS(n)                                     ((n) - 1)
#define ADS129x_RLDnP_MASKED(n)                                  (1u << ADS129x_RLDnP_POS(n))
/* RLD_SENSN Register */
#define ADS129x_RLDnN_POS(n)                                     ((n) - 1)
#define ADS129x_RLDnN_MASKED(n)                                  (1u << ADS129x_RLDnN_POS(n))

/*=================================================================
 *                         ADS1298 Device Setting Bits 
 *=================================================================*/

/*============== ID Control Register bit ==============*/

enum ads129x_channels
{   
    ADS129x_4Channels = 0,
    ADS129x_6Channels,
    ADS129x_8Channels

} ;

enum ads129x_family
{
    ADS129x_Family = 4,
    ADS129xR_Family = 6
} ;

struct ads129x_id
{
    const enum ads129x_channels channels;
    const enum ads129x_family   family;
};

/*============== Configration Register 1 control bit ==============*/

/**
 * @brief High resolution or Low power mode 
 * @note  This bit determines whether the device runs in low-power or high-resolution mode.
 */
enum hi_res
{
    LowPower = 0,
    HighResolution,
};

/**
 * @brief Daisy chain or multiple feedback mode 
 * @note  This bit determines which mode is enabled.
 */
enum ndaisy_en
{
   daisy_chain_mode = 0,
   multiple_read_back_mode
};

/**
 * @brief CLK connection
 * @note  This bit determines if the internal oscillator signal is connected to the CLK pin when the CLKSEL pin = 1.
 **Additional power is consumed when driving external devices.
 */

enum clk_en
{
    osc_clk_output_disable = 0,
    osc_clk_output_enable
};

/**
 * @brief Output data rate 
 * @note  For High-Resolution mode, fMOD = fCLK / 4. For low power mode, fMOD = fCLK / 8.
 * @note  These bits determine the output data rate of the device.
 */

enum dataRate
{
    fmod_div_16 = 0, /* HR Mode: 32  kSPS, LP Mode: 16  kSPS */
    fmod_div_32,     /* HR Mode: 16  kSPS, LP Mode: 8   kSPS */
    fmod_div_64,     /* HR Mode: 8   kSPS, LP Mode: 4   kSPS */
    fmod_div_128,    /* HR Mode: 4   kSPS, LP Mode: 2   kSPS */
    fmod_div_256,    /* HR Mode: 2   kSPS, LP Mode: 1   kSPS */
    fmod_div_512,    /* HR Mode: 1   kSPS, LP Mode: 500  SPS */
    fmod_div_1024    /* HR Mode: 500  SPS, LP Mode: 500  SPS */ 
};

/*============== Configration Register 2 control bit ==============*/

/**
 * @brief WCT chopping scheme
 * @note  This bit determines whether the chopping frequency of WCT amplifiers is variable or fixed.
 */
enum wct_chop
{ 
    chopping_frequency_varies = 0, 
    chopping_frequency_fixed_fmod_div_16
};

/**
 * @brief TEST source
 * @note  This bit determines the source for the test signal.
 */
enum int_test
{
    test_signal_driven_external = 0,
    test_signal_generated_internal
}; 

/**
 * @brief Test signal Amplitude
 * @note  These bits determine the calibration signal amplitude.
 */
enum test_amp
{
    x1times = 0, /* 1 × –(VREFP – VREFN) / 2400 V */
    x2times      /* 2 × –(VREFP – VREFN) / 2400 V */
};

/**
 * @brief Test signal frequency 
 * @note  These bits determine the calibration signal frequency.
 */
enum test_freq
{
    fclk_div_2pow21 = 0, /* Pulsed at fCLK / 2^21 */
    fclk_div_2pow20,     /* Pulsed at fCLK / 2^20 */
    not_use,             /* Not use               */
    at_dc                /* At DC                 */
};

/*============== Configration Register 3 control bit ==============*/

/**
 * @brief Power-down reference buffer 
 * @note  This bit determines the power-down reference buffer state.
 */
enum pd_refbuf
{
    internal_reference_buffer_disable = 0,
    internal_reference_buffer_enable
};

/**
 * @brief Reference voltage
 * @note  This bit determines the reference voltage, VREFP.
 */
enum vref
{
    vref_2_4v = 0, /* VREFP is set to 2.4 V */
    vref_4v        /* VREFP is set to 4 V   */
};

/**
 * @brief RLD measurement
 * @note  This bit enables RLD measurement. The RLD signal may be measured with any channel.
 */
enum rld_meas
{
    rld_open = 0,
    rld_in_signal_routed /* RLD_IN signal is routed to the channel that has the MUX_Setting 010 (VREF)  */
}; 

/**
 * @brief RLDREF signal
 * @note  This bit determines the RLDREF signal source.
 */
enum rldref_int
{
   rldref_fed_external = 0,  /* RLDREF signal fed externally */
   rldref_generated_internal /* RLDREF signal (AVDD – AVSS) / 2 generated internally */
};

/**
 * @brief RLD buffer power 
 * @note  This bit determines the RLD buffer power state.
 */
enum pd_rld
{
   rld_buffer_disable = 0,
   rld_buffer_enable   
};

/**
 * @brief RLD sense function
 * @note  This bit enables the RLD sense function.
 */
enum rld_loff_sens
{
    rld_sense_disable = 0,
    rld_sens_enable
};

/**
 * @brief RLD lead-off status
 * @note  This bit determines the RLD status.
 */
enum rld_stat
{
    rld_is_connected = 0,
    rld_is_not_connected
};

/*============== Lead-Off Control Register control bit ==============*/

/**
 * @brief Lead-off comparator threshold positive side
 * @note  Comparator positive side
 */
enum comp_th_positive
{
    _95_percent = 0,
    _92_5_percent,
    _90_percent,
    _87_5_percent,
    _85_percent,
    _80_percent,
    _75_percent,
    _70_percent
};

/**
 * @brief Lead-off comparator threshold negative side
 * @note  Comparator negative side
 */
enum comp_th_negative
{
    _5_percent = 0,
    _7_5_percent,
    _10_percent,
    _12_5_percent,
    _15_percent,
    _20_percent,
    _25_percent,
    _30_percent
};

/**
 * @brief Lead-off detection mode
 * @note  This bit determines the lead-off detection mode.
 */
enum vlead_off_en
{
    current_source_mode = 0,
    pullup_or_pulldown_resgistor_mode_lead_off
};

/**
 * @brief Lead-off current magnitude
 * @note  These bits determine the magnitude of current for the current lead-off mode.
 */
enum ilead_off
{
    _6nA = 0,
    _12nA,
    _18nA,
    _24nA
};

/**
 * @brief Lead-off frequency
 * @note  These bits determine the frequency of lead-off detect for each channel.
 */
enum flead_off
{
    flead_off_disable,
    AC_fdr_div_4,
    do_not_use, 
    DC_lead_off_detection_enable
};

/*============== Individual Channel Setting register control bit ==============*/

/**
 * @brief Power-down
 * @note  This bit determines the channel power mode for the corresponding channel.
 */
enum power_down
{
    normal_operation = 0,
    channel_powerdown
};

/**
 * @brief PGA gain
 * @note  These bits determine the PGA gain setting.
 */
enum gain
{
    gain_6 = 0,
    gain_1,
    gain_2,
    gain_3, 
    gain_4,
    gain_8,
    gain_12
};

/**
 * @brief Channel input
 * @note  These bits determine the channel input selection.
 */
enum mux
{
    normal_electrode_input = 0, /* Normal electrode input                                      */
    input_shorted,              /* Input shorted                                               */
    rld_measurement,            /* Used in conjunction with RLD_MEAS bit for RLD measurements. */
    mmd_for_supply_measurement, /* MVDD for supply measurement                                 */
    temperature_sensor,         /* Temperature sensor                                          */
    test_signal,                /* Test signal                                                 */
    rld_drp,                    /* Positive electrode is the driver                            */
    rld_drn                     /* Negative electrode is the driver                            */
};

/*==============  RLD Positive Signal Derivation Register control bit ==============*/
/**
 * @brief INnP to RLD
 * @note  Route channel n positive signal into RLD derivation.
 */
enum rldnp
{
    rldnp_disable = 0,
    rldnp_enable
};

/*==============  RLD Negative Signal Derivation Register control bit ==============*/
/**
 * @brief INnN to RLD
 * @note  Route channel n negative signal into RLD derivation.
 */
enum rldnn
{
    rldnn_disable = 0,
    rldnn_enable
};

/*==============  Positive Signal Lead-Off Detection Register control bit ==============*/
/**
 * @brief INnP lead off
 * @note  Enable lead-off detection on INnP.
 */
enum loffnp
{
    loffnp_disable = 0,
    loffnp_enable
};

/*==============  Negative Signal Lead-Off Detection Register control bit ==============*/
/**
 * @brief INnN lead off
 * @note  Enable lead-off detection on INnN
 */
enum loffnn
{
    loffnn_disable = 0,
    loffnn_enable
};

/*==============  Lead-Off Flip Register control bit ==============*/
/**
 * @brief Channel n LOFF polarity flip
 * @note  Flip the pullup/pulldown polarity of the current source or resistor on channel n for lead-off derivation.
 */
enum loff_flipn
{
    loff_flipn_no_flip = 0,
    loff_flipn_flip
};
 
/*==============  Lead-Off Positive Signal Status Register control bit ==============*/
/**
 * @brief Channel n positive channel lead-off status
 * @note  Status of whether INnP electrode is on or off.
 */
enum innp_off
{
    positive_electrode_on = 0,
    positive_electrode_off
};

/*==============  Lead-Off Negative Signal Status Register control bit ==============*/
/**
 * @brief Channel n negative channel lead-off status
 * @note  Status of whether INnN electrode is on or off.
 */
enum innn_off
{
    negative_electrode_on = 0,
    negative_electrode_off
};

/*==============  General-Purpose I/O Register control bit ==============*/
/**
 * @brief GPIO control (corresponding GPIOD)
 * @note  These bits determine if the corresponding GPIOD pin is an input.
 */
enum gpioc
{
    gpio_output = 0,
    gpio_input
};

/*==============  Pace Detect Register control bit ==============*/
/**
 * @brief Pace even channels
 * @note  These bits control the selection of the even number channels
 * available on TEST_PACE_OUT1. Only one channel may be
 * selected at any time
 */
enum pacee
{
    pace_even_channel2 = 0,
    pace_even_channel4, 
    pace_even_channel6,
    pace_even_channel8
}; 

/**
 * @brief Pace odd channels
 * @note  These bits control the selection of the odd number channels
 * available on TEST_PACE_OUT2. Only one channel may be
 * selected at any time
 */
enum paceo
{
    pace_odd_channel1 = 0,
    pace_odd_channel3,
    pace_odd_channel5,
    pace_odd_channel7
};

/**
 * @brief Pace detect buffer
 * @note  This bit is used to enable/disable the pace detect buffer.
 */
enum pd_pace
{
    pace_detect_buffer_disable = 0,
    pace_detect_buffer_enable, 
};

/*==============  Respiration Control Register control bit ==============*/
/**
 * @brief Enables respiration demodulation circuitry (ADS129xR only; for ADS129x always write 0)
 * @note  This bit enables and disables the demodulation circuitry on channel 1.
 */
enum resp_demod_en1
{
    resp_demodulation_circuitry_disable = 0,
    resp_demodulation_circuitry_enable
};

/**
 * @brief Enables respiration modulation circuitry (ADS129xR only; for ADS129x always write 0)
 * @note  This bit enables and disables the modulation circuitry on channel 1.
 */
enum resp_mod_en1
{
    resp_modulation_circuitry_disable = 0,
    resp_modulation_circuitry_enable
};

/**
 * @brief Respiration phase
 * @note  RESP_PH[2:0] phase control bits only for internal respiration RESP_CTRL[1:0]
 */
enum resp_ph
{
    phase_22_5 = 0,
    phase_45,
    phase_67_5,
    phase_90,
    phase_112_5,
    phase_135,
    phase_157_5
};

/**
 * @brief Respiration control
 * @note  These bits set the mode of the respiration circuitry.
 */
enum resp_ctrl
{
    no_respiration = 0,
    external_respiration,
    internal_respiration_with_internal_signals,
    internal_respiration_with_generated_signals
};

/*==============  Configuration Register 4 control bit ==============*/
/**
 * @brief Respiration modulation frequency
 * @note  These bits control the respiration control frequency when RESP_CTRL[1:0] = 10 or RESP_CTRL[1:0] = 10
 */
enum resp_freq
{
    resp_freq_64kHz = 0,
    resp_freq_32kHz,
 /* Square wave on GPIO3 and GPIO04. 
    Output on GPIO4 is 180 degree out of phase with GPIO3 */
    resp_freq_16kHz, 
    resp_freq_8kHz,  
    resp_freq_4kHz, 
    resp_freq_2kHz,
    resp_freq_1kHz,
    resp_freq_500Hz,
};

/**
 * @brief Single-shot conversion
 * @note  This bit sets the conversion mode.
 */
enum single_shot
{
    continuous_conversion = 0,
    single_shot_mode
};

/**
 * @brief Connects the WCT to the RLD
 * @note  This bit connects WCT to RLD.
 */
enum wct_to_rld
{
    wct_to_rld_disable = 0,
    wct_to_rld_enable
};

/**
 * @brief Lead-off comparator power-down
 * @note  This bit powers down the lead-off comparators.
 */
enum pd_loff_comp
{
    lead_off_comparators_disable = 0,
    lead_off_comparators_enable
}; 

/*==============  Wilson Central Terminal and Augmented Lead Control Register control bit ==============*/
/**
 * @brief Enable (WCTA + WCTB)/2 to the negative input of channel n
 * @note  From channel 4 to channel 7
 */
enum avf_chn
{
    avf_chn_disable = 0,
    avf_chn_enable
}; 

/**
 * @brief Power-down WCTA
 * @note  Control WCTA power
 */
enum pd_wcta
{
    pd_wcta_power_down = 0,
    pd_wcta_power_on
}; 

/**
 * @brief WCT Amplifier A channel selection, typically connected to RA electrode
 * @note  These bits select one of the eight electrode inputs of channels 1 to 4
 */
enum wcta
{
    channel_1_positive_input_connected_to_wcta = 0,
    channel_1_negative_input_connected_to_wcta,
    channel_2_positive_input_connected_to_wcta,
    channel_2_negative_input_connected_to_wcta,
    channel_3_positive_input_connected_to_wcta,
    channel_3_negative_input_connected_to_wcta,
    channel_4_positive_input_connected_to_wcta,
    channel_4_negative_input_connected_to_wcta
};

/*==============  Wilson Central Terminal Control Register control bit ==============*/
/**
 * @brief Power-down WCTC
 * @note  Control WCTC power
 */
enum pd_wctc
{
    pd_wctc_power_down = 0,
    pd_wctc_power_on
};

/**
 * @brief Power-down WCTB
 * @note  Control WCTB power
 */
enum pd_wctb
{
    pd_wctb_power_down = 0,
    pd_wctb_power_on
}; 

/**
 * @brief WCT Amplifier B channel selection, typically connected to LA electrode
 * @note  These bits select one of the eight electrode inputs of channels 1 to 4
 */
 enum wctb
{
    channel_1_positive_input_connected_to_wctb = 0,
    channel_1_negative_input_connected_to_wctb,
    channel_2_positive_input_connected_to_wctb,
    channel_2_negative_input_connected_to_wctb,
    channel_3_positive_input_connected_to_wctb,
    channel_3_negative_input_connected_to_wctb,
    channel_4_positive_input_connected_to_wctb,
    channel_4_negative_input_connected_to_wctb
};

/**
 * @brief WCT Amplifier C channel selection, typically connected to LL electrode
 * @note  These bits select one of the eight electrode inputs of channels 1 to 4
 */
enum wctc
{
    channel_1_positive_input_connected_to_wctc = 0,
    channel_1_negative_input_connected_to_wctc,
    channel_2_positive_input_connected_to_wctc,
    channel_2_negative_input_connected_to_wctc,
    channel_3_positive_input_connected_to_wctc,
    channel_3_negative_input_connected_to_wctc,
    channel_4_positive_input_connected_to_wctc,
    channel_4_negative_input_connected_to_wctc
};

/*=================================================================
 *                         ADS1298 Device Struct and Enum 
 *=================================================================*/
/*========================= Config struct =========================*/
struct ads129x_emg_config
{
    /* Configuration from CONFIG1 register */
    uint8_t high_resolution; 
    uint8_t daisy_enable;
    uint8_t clock_enable;
    uint8_t data_rate; 
    /* Configuration from CONFIG3 register */
    uint8_t pd_rebuf; 
    uint8_t vref_4v;  
    uint8_t rlddef_int; 
    uint8_t pd_rld; 
    /* Configuration from CHnSET register */
    uint8_t gain;
    uint8_t mux; 
};

/*======================= Ring Buffer struct =======================*/
struct emg_channel_buffer
{
    struct ring_buf rb;
    uint8_t __aligned(4) dataBuffer[ADS129x_BUFFER_SIZE];
};

/*======================== EMG buffer struct =======================*/
struct emg_buffer
{
    struct emg_channel_buffer channel[ADS129x_NUM_CHANNELS];
    uint32_t sample_count; 
    bool overflow; 
};

struct ads129x_dev
{
    struct spi_dev*            pSpi;       /* Pointer to SPI and CS */
    const struct gpio_dt_spec* pDRDYpin;   /* DRDY interrupt pin    */
    struct gpio_callback       drdy_cb;    /* Callback struct       */
    bool                       data_ready; /* Device state          */
};
/*=========================== DSP struct ===========================*/
struct dsp_double_buffer{                                                                                                                                                                            
      int32_t buffer_a[DSP_BLOCK_SIZE];                                                                                                                                                       
      int32_t buffer_b[DSP_BLOCK_SIZE];                                                                                                                                                       
      int32_t *active;      /* Being filled from circular buffer */                                                                                                                           
      int32_t *processing;  /* Being processed by DSP */                                                                                                                                      
      uint16_t fill_index;                                                                                                                                        
      bool ready;           /* Processing buffer has new data */                                                                                                                              
}; 

enum ads129x_read_mode
{
    ADS_READ_CONTINUOUS = 0,
    ADS_READ_SINGLE
};

/*=================================================================
 *                         ADS1298 Device APIs
 *=================================================================*/

/**
 * @brief Initialize ADS129x for EMG acquisition
 * @param pAds: Pointer to ADS129x device struct
 * @param pEMG: Pointer to EMG config struct
 * @param pEMGBuffer: Pointer to EMG buffer (will be initialized)
 * @param pDSPBuffer: Pointer to DSP buffer (will be initialized)
 * @param channels: number of channels
 * @return 0 on success, negative on fail
 */
int ads_emg_init(const struct ads129x_dev* pAds,
                 const struct ads129x_emg_config* pEMG,
                 struct emg_buffer* pEMGBuffer,
                 struct dsp_double_buffer* pDSPBuffer,
                 uint8_t channels);

/**
 * @brief Read EMG data from ADS129x
 * @param pAds: Pointer to ADS129x device struct
 * @param pBuffer: Pointer to EMG buffer
 * @return 0 on success, negative on fail
 */
int ads_emg_read(const struct ads129x_dev* pAds,
                 struct emg_buffer* pBuffer);

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
 * @param pAds: Pointer to ADS129x device struct
 * @return 0 on success, negative on fail
 */
int ads_emg_start_continuous(const struct ads129x_dev* pAds);

/**
 * @brief Stop ADS129x conversion
 * @param pAds: Pointer to ADS129x device struct
 * @return 0 on success, negative on fail
 */
int ads_emg_stop(const struct ads129x_dev* pAds);

/**
 * @brief Read ADS129x device ID
 * @param pAds: Pointer to ADS129x device struct
 * @return Device ID on success, negative on fail
 */
int ads_read_id(const struct ads129x_dev* pAds);
#endif
