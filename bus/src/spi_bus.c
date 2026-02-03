#include "spi_bus.h"

LOG_MODULE_REGISTER(spi_bus, CONFIG_SPI_LOG_LEVEL);

/*=================================================================
 * SPI BASIC FUNCTION
 *=================================================================*/

/**
 * @brief SPI Chip select control
 * @param pSpecGPIO: Pointer to GPIO device tree spec
 * @param state: State to send
 * @return 0 on success, negative on fail
 */
static inline int spi_cs_control(const struct gpio_dt_spec* pSpecGPIO, uint8_t state)
{
    if (pSpecGPIO == NULL) {
        return -EINVAL;
    }
    return gpio_pin_set_dt(pSpecGPIO, state);
}

/**
 * @brief SPI send command
 * @param pDev: Pointer to device struct
 * @param cmd: Command to send
 * @return 0 on success, negative on fail
 */
int spi_send_cmd(const struct spi_dev* pDev, uint8_t cmd)
{
    int ret = 0;
    if (pDev == NULL || pDev->pSpecSPI == NULL || pDev->pSpecGPIO == NULL) {
        return -EINVAL;
    }
    /* Create add buffer */
    struct spi_buf buf = {.buf = &cmd, .len = sizeof(cmd)};
    struct spi_buf_set cmd_buffer = {.buffers = &buf, .count = 1};
    
    /* Send command */
    ret = spi_cs_control(pDev->pSpecGPIO, CHIP_SELECTED);
    if(ret < 0) return ret;
    ret = spi_write_dt(pDev->pSpecSPI, &cmd_buffer);
    if(ret < 0) return ret;
    ret = spi_cs_control(pDev->pSpecGPIO, CHIP_DESELECTED);
    if(ret < 0) return ret;
    return ret;
}
/*=================================================================
 * SPI REGISTER ACCESS FUNCTIONS
 *=================================================================*/

/**
 * @brief SPI write register function
 * @param pDev: Pointer to device struct
 * @param pAddr_buf: Pointer to register address buffer
 * @param pTx_buf: Transmit buffer
 * @return 0 on success, negative on fail
 */
int spi_write_register(const struct spi_dev* pDev,
                       const struct spi_buf_set* pAddr_buf,
                       const struct spi_buf_set* pTx_buf)
{
    int ret;
    /* Check NULL pointer */
    if (pDev == NULL || pDev->pSpecSPI == NULL || pDev->pSpecGPIO == NULL) {
        return -EINVAL;
    }
    /* Check NULL buffer */
    if (pAddr_buf == NULL) {
        return -EINVAL;
    }

    spi_cs_control(pDev->pSpecGPIO, CHIP_SELECTED);

    ret = spi_write_dt(pDev->pSpecSPI, pAddr_buf);
    if (ret < 0) {
        spi_cs_control(pDev->pSpecGPIO, CHIP_DESELECTED);
        return ret;
    }

    ret = spi_write_dt(pDev->pSpecSPI, pTx_buf);
    spi_cs_control(pDev->pSpecGPIO, CHIP_DESELECTED);

    return ret;
}

/**
 * @brief SPI read register function
 * @param pDev: Pointer to device struct
 * @param pAddr_buf: Pointer to register address buffer
 * @param pRx_buf: Pointer to receive buffer
 * @return 0 on success, negative on fail
 */
int spi_read_register(const struct spi_dev* pDev,
                      const struct spi_buf_set* pAddr_buf,
                      struct spi_buf_set* pRx_buf)
{
    /* Check NULL pointer */
    if (pDev == NULL || pDev->pSpecSPI == NULL || pDev->pSpecGPIO == NULL) {
        return -EINVAL;
    }
    /* Check NULL buffer */
    if (pAddr_buf == NULL) {
        return -EINVAL;
    }

    int ret;

    spi_cs_control(pDev->pSpecGPIO, CHIP_SELECTED);
    ret = spi_transceive_dt(pDev->pSpecSPI, pAddr_buf, pRx_buf);
    spi_cs_control(pDev->pSpecGPIO, CHIP_DESELECTED);

    return ret;
}

/*=================================================================
 * SPI BIT MANIPULATION FUNCTIONS
 *=================================================================*/

/**
 * @brief SPI set a single bit in register function
 * @param pDev: Pointer to device struct
 * @param pAddr_buf: Pointer to register address buffer
 * @param mask: BIT mask to modify
 * @return 0 on success, negative on fail
 */
int spi_set_bits(const struct spi_dev* pDev,
                 const struct spi_buf_set* pAddr_buf,
                 uint8_t mask)
{
    /* Check NULL pointer */
    if (pDev == NULL || pDev->pSpecSPI == NULL || pDev->pSpecGPIO == NULL) {
        return -EINVAL;
    }
    /* Check NULL buffer */
    if (pAddr_buf == NULL) {
        return -EINVAL;
    }

    int ret;
    uint8_t current_bit = 0;

    struct spi_buf buffer = {
        .buf = &current_bit,
        .len = 1
    };
    struct spi_buf_set buffer_set = {
        .buffers = &buffer,
        .count = 1
    };

    ret = spi_read_register(pDev, pAddr_buf, &buffer_set);
    if (ret < 0) {
        return ret;
    }

    /* Modify register's bit */
    current_bit |= mask;

    return spi_write_register(pDev, pAddr_buf, &buffer_set);
}

/**
 * @brief SPI clear a single bit in register function
 * @param pDev: Pointer to device struct
 * @param pAddr_buf: Pointer to register address buffer
 * @param mask: BIT mask to modify
 * @return 0 on success, negative on fail
 */
int spi_clear_bits(const struct spi_dev* pDev,
                   const struct spi_buf_set* pAddr_buf,
                   uint8_t mask)
{
    /* Check NULL pointer */
    if (pDev == NULL || pDev->pSpecSPI == NULL || pDev->pSpecGPIO == NULL) {
        return -EINVAL;
    }
    /* Check NULL buffer */
    if (pAddr_buf == NULL) {
        return -EINVAL;
    }

    int ret;
    uint8_t current_bit = 0;

    struct spi_buf buffer = {
        .buf = &current_bit,
        .len = 1
    };
    struct spi_buf_set buffer_set = {
        .buffers = &buffer,
        .count = 1
    };

    /* Read register's current bit value */
    ret = spi_read_register(pDev, pAddr_buf, &buffer_set);
    if (ret < 0) {
        return ret;
    }

    /* Modify register's bit */
    current_bit &= ~mask;

    return spi_write_register(pDev, pAddr_buf, &buffer_set);
}

/*=================================================================
 * SPI DEBUG FUNCTIONS
 *=================================================================*/

/**
 * @brief SPI dumb config
 * @param pDev: Pointer to device struct
 */
void spi_bus_dumb_config(const struct spi_dev* pDev)
{
    /* Check NULL pointer */
    if (pDev == NULL || pDev->pSpecSPI == NULL) {
        LOG_ERR("SPI Config: NULL pointer");
        return;
    }

    LOG_INF("SPI bus: %s", spi_is_ready_dt(pDev->pSpecSPI) ? "yes" : "no");
    LOG_INF("Frequency: %u Hz", pDev->pSpecSPI->config.frequency);

    uint16_t spi_operation = pDev->pSpecSPI->config.operation;
    LOG_INF("Mode: CPOL = %u, CPHA = %u",
            (spi_operation & SPI_MODE_CPOL) ? 1 : 0,
            (spi_operation & SPI_MODE_CPHA) ? 1 : 0);
    LOG_INF("Word size: %u bits", SPI_WORD_SIZE_GET(spi_operation));
}
