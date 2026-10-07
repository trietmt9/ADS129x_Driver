#ifndef __SPI_BUS_H__
#define __SPI_BUS_H__

#include "zephyr/drivers/spi.h"
#include "zephyr/drivers/gpio.h"
#include "zephyr/logging/log.h"

/*=================================================================
 * SPI OPERATION MACROS 
 *=================================================================*/
#define CHIP_SELECTED                 1
#define CHIP_DESELECTED               0
/* 8-bits word */
#define SPI_OP_MODE_0_8BIT            SPI_WORD_SET(8)                                       /* CPHA = 0, CPOL = 0 */
#define SPI_OP_MODE_1_8BIT           (SPI_WORD_SET(8) | SPI_MODE_CPHA)                      /* CPHA = 1, CPOL = 0 */
#define SPI_OP_MODE_2_8BIT           (SPI_WORD_SET(8) | SPI_MODE_CPOL)                      /* CPHA = 0, CPOL = 1 */
#define SPI_OP_MODE_3_8BIT           (SPI_WORD_SET(8) | SPI_MODE_CPHA | SPI_MODE_CPOL)      /* CPHA = 1, CPOL = 1 */

/* 16-bits word */
#define SPI_OP_MODE_0_16BIT            SPI_WORD_SET(16)                                     /* CPHA = 0, CPOL = 0 */
#define SPI_OP_MODE_1_16BIT           (SPI_WORD_SET(16) | SPI_MODE_CPHA)                    /* CPHA = 1, CPOL = 0 */
#define SPI_OP_MODE_2_16BIT           (SPI_WORD_SET(16) | SPI_MODE_CPOL)                    /* CPHA = 0, CPOL = 1 */
#define SPI_OP_MODE_3_16BIT           (SPI_WORD_SET(16) | SPI_MODE_CPHA | SPI_MODE_CPOL)    /* CPHA = 1, CPOL = 1 */

/*=================================================================
 * SPI STRUCT CONTROL
 *=================================================================*/
struct spi_dev
{
   const struct spi_dt_spec*  pSpecSPI;
   const struct gpio_dt_spec* pSpecGPIO;
};

/*=================================================================
 * SPI BASIC FUNCTION
 *=================================================================*/

/**
 * @brief SPI send command
 * @param pDev: Pointer to device struct
 * @param cmd: Command to send
 * @return 0 on success, negative on fail
 */
int spi_send_cmd(const struct spi_dev* pDev, uint8_t cmd);

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
                       const struct spi_buf_set* pTx_buf);

/**
 * @brief Write a byte sequence as one transaction inside a single CS assertion.
 *
 * Register writes must use this rather than spi_write_register(): that function
 * splits opcode and data into two spi_write_dt() calls, while the working read
 * path sends everything in one. See the comment in spi_bus.c.
 *
 * @param pDev: Pointer to device struct
 * @param data: bytes to send
 * @param len:  how many
 * @return 0 on success, negative on fail
 */
int spi_write_bytes(const struct spi_dev* pDev, const uint8_t* data, size_t len);

/**
 * @brief SPI read register function
 * @param pDev: Pointer to device struct
 * @param pAddr_buf: Pointer to register address buffer
 * @param pRx_buf: Pointer to receive buffer
 * @return 0 on success, negative on fail
 */
int spi_read_register(const struct spi_dev* pDev,
                      const struct spi_buf_set* pAddr_buf,
                      struct spi_buf_set* pRx_buf);

/**
 * @brief SPI read data stream function
 * @param pDev: Pointer to device struct
 * @param pRx_buf: Pointer to receive buffer
 * @return 0 on success, negative on fail
 */
int spi_read_stream(const struct spi_dev* pDev,
                      struct spi_buf_set* pRx_buf);

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
                 uint8_t mask);

/**
 * @brief SPI clear a single bit in register function
 * @param pDev: Pointer to device struct
 * @param pAddr_buf: Pointer to register address buffer
 * @param mask: BIT mask to modify
 * @return 0 on success, negative on fail
 */
int spi_clear_bits(const struct spi_dev* pDev,
                   const struct spi_buf_set* pAddr_buf,
                   uint8_t mask);

/*=================================================================
 * SPI DEBUG FUNCTIONS
 *=================================================================*/

/**
 * @brief SPI dumb config
 * @param pDev: Pointer to device struct
 */
void spi_bus_dumb_config(const struct spi_dev* pDev);

#endif