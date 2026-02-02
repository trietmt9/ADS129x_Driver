# SPI Driver Function Reference

---

## 1. Zephyr DT APIs (Use Directly)

### Synchronous

| Function | Purpose |
|----------|---------|
| `spi_is_ready_dt()` | Check if SPI device is ready |
| `spi_write_dt()` | Write data |
| `spi_read_dt()` | Read data |
| `spi_transceive_dt()` | Write then read (single transaction) |

```c
struct spi_dt_spec spi_dev = SPI_DT_SPEC_GET(DT_NODELABEL(mydevice),
                                              SPI_WORD_SET(8), 0);
spi_is_ready_dt(&spi_dev);
spi_write_dt(&spi_dev, &tx_bufs);
spi_read_dt(&spi_dev, &rx_bufs);
spi_transceive_dt(&spi_dev, &tx_bufs, &rx_bufs);
```

### Asynchronous

| Function | Purpose |
|----------|---------|
| `spi_transceive_cb_dt()` | Async transfer with callback |
| `spi_transceive_signal_dt()` | Async transfer with signal/polling |
| `spi_release_dt()` | Release SPI / cancel operation |

```c
/* Callback function */
void spi_callback(const struct device *dev,
                  int result,
                  void *userdata) {
    /* Handle completion */
}

/* Async transfer with callback */
spi_transceive_cb_dt(&spi_dev, &tx_bufs, &rx_bufs, spi_callback, NULL);

/* Release / cancel */
spi_release_dt(&spi_dev);
```

---

## 2. Basic Functions

### spi_bus_send_cmd

```c
int spi_bus_send_cmd(const struct spi_dt_spec *pSpec,
                     uint8_t cmd);
```

| Parameter | Description |
|-----------|-------------|
| `pSpec` | Pointer to SPI device tree spec |
| `cmd` | Command byte to send |
| **Return** | 0 on success, negative on error |

---

## 3. Register Interface

### Read / Write Register

One function handles both single and multiple registers - buffer `.len` controls how many.

```c
int spi_write_register(const struct spi_dt_spec   *pSpecSPI,
                       const struct gpio_dt_spec  *pSpecGPIO,
                       const struct spi_buf_set   *pAddr,
                       const struct spi_buf_set   *pTxData);

int spi_read_register(const struct spi_dt_spec   *pSpecSPI,
                      const struct gpio_dt_spec  *pSpecGPIO,
                      const struct spi_buf_set   *pAddr,
                      struct spi_buf_set         *pRxData);
```

| Parameter | Description |
|-----------|-------------|
| `pSpecSPI` | Pointer to SPI device tree spec |
| `pSpecGPIO` | Pointer to GPIO device tree spec (for CS control) |
| `pAddr` | Pointer to register address buffer set |
| `pTxData` | Pointer to transmit data buffer set |
| `pRxData` | Pointer to receive data buffer set |
| **Return** | 0 on success, negative on error |

### Usage Example: Single Register

```c
uint8_t reg_addr = 0x01;
uint8_t tx_data  = 0x55;
uint8_t rx_data;

struct spi_buf addr_buf = { .buf = &reg_addr, .len = 1 };
struct spi_buf tx_buf   = { .buf = &tx_data,  .len = 1 };  /* len = 1 for single */
struct spi_buf rx_buf   = { .buf = &rx_data,  .len = 1 };

struct spi_buf_set addr_set = { .buffers = &addr_buf, .count = 1 };
struct spi_buf_set tx_set   = { .buffers = &tx_buf,   .count = 1 };
struct spi_buf_set rx_set   = { .buffers = &rx_buf,   .count = 1 };

/* Write single register */
spi_write_register(&spi_dev, &gpio_cs, &addr_set, &tx_set);

/* Read single register */
spi_read_register(&spi_dev, &gpio_cs, &addr_set, &rx_set);
```

### Usage Example: Multiple Registers

```c
uint8_t start_addr = 0x01;
uint8_t tx_data[4] = { 0x11, 0x22, 0x33, 0x44 };
uint8_t rx_data[4];

struct spi_buf addr_buf = { .buf = &start_addr, .len = 1 };
struct spi_buf tx_buf   = { .buf = tx_data,     .len = 4 };  /* len = 4 for multiple */
struct spi_buf rx_buf   = { .buf = rx_data,     .len = 4 };

struct spi_buf_set addr_set = { .buffers = &addr_buf, .count = 1 };
struct spi_buf_set tx_set   = { .buffers = &tx_buf,   .count = 1 };
struct spi_buf_set rx_set   = { .buffers = &rx_buf,   .count = 1 };

/* Write 4 consecutive registers starting from 0x01 */
spi_write_register(&spi_dev, &gpio_cs, &addr_set, &tx_set);

/* Read 4 consecutive registers starting from 0x01 */
spi_read_register(&spi_dev, &gpio_cs, &addr_set, &rx_set);
```

### Key Point

```
Same function, different buffer length:

Single:   .len = 1  →  spi_write_register()
Multiple: .len = N  →  spi_write_register()  (same function!)
```

### Bit Manipulation

```c
int spi_bus_update_reg(const struct spi_dt_spec  *pSpec,
                       const struct spi_buf_set  *pAddr,
                       uint8_t mask,
                       uint8_t val);

int spi_bus_set_bits(const struct spi_dt_spec  *pSpec,
                     const struct spi_buf_set  *pAddr,
                     uint8_t mask);

int spi_bus_clear_bits(const struct spi_dt_spec  *pSpec,
                       const struct spi_buf_set  *pAddr,
                       uint8_t mask);
```

| Function | Description |
|----------|-------------|
| `update_reg` | Replace masked bits with new value |
| `set_bits` | Set bits to 1: `old \| mask` |
| `clear_bits` | Clear bits to 0: `old & ~mask` |

**Parameters for update_reg:**

| Parameter | Description |
|-----------|-------------|
| `mask` | Bits to modify (1=change, 0=keep) |
| `val` | New values for masked bits |

---

## 4. Debug Functions (Optional)

### Statistics

```c
struct spi_bus_stats {
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t transactions;
    uint32_t errors;
};

int spi_bus_get_stats(const struct spi_dt_spec *pSpec,
                      struct spi_bus_stats *stats);

void spi_bus_reset_stats(const struct spi_dt_spec *pSpec);
```

### Logging

```c
void spi_bus_dump_config(const struct spi_dt_spec *pSpec);

void spi_bus_hexdump(const char *prefix, const uint8_t *data, size_t len);
```

| Function | Description |
|----------|-------------|
| `dump_config` | Print SPI configuration to log |
| `hexdump` | Print data in hex format |

---

## 5. Quick Reference

### Priority Levels

| Priority | Functions |
|----------|-----------|
| **Essential** | `send_cmd` |
| **Common** | `read_register`, `write_register` |
| **Optional** | `update_reg`, `set_bits`, `clear_bits` |
| **Debug** | `get_stats`, `dump_config`, `hexdump` |

### SPI Mode Macros

```c
#define SPI_OP_MODE_0_8BIT   SPI_WORD_SET(8)                                    // CPOL=0 CPHA=0
#define SPI_OP_MODE_1_8BIT  (SPI_WORD_SET(8) | SPI_MODE_CPHA)                   // CPOL=0 CPHA=1
#define SPI_OP_MODE_2_8BIT  (SPI_WORD_SET(8) | SPI_MODE_CPOL)                   // CPOL=1 CPHA=0
#define SPI_OP_MODE_3_8BIT  (SPI_WORD_SET(8) | SPI_MODE_CPHA | SPI_MODE_CPOL)   // CPOL=1 CPHA=1
```

### SPI_HOLD_ON_CS Flag

```
Normal:           CS LOW → transfer → CS HIGH
With HOLD_ON_CS:  CS LOW → transfer → CS stays LOW (until next transfer without flag)
```

Usually not needed when using `spi_transceive_dt()`.
