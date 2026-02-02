# Zephyr SPI API Structures Reference (DT Style)

> **Header:** `<zephyr/drivers/spi.h>`
>
> This document covers the modern Device Tree (DT) style API, which is the recommended approach for new Zephyr projects.

---

## Table of Contents

1. [Core Structures](#1-core-structures)
2. [Initialization Macros](#2-initialization-macros)
3. [Buffer Structures](#3-buffer-structures)
4. [Operation Flags](#4-operation-flags)
5. [DT-Style API Functions](#5-dt-style-api-functions)
6. [Device Tree Configuration](#6-device-tree-configuration)
7. [Complete Usage Examples](#7-complete-usage-examples)
8. [Structure Relationships Diagram](#8-structure-relationships-diagram)
9. [Manual CS Control with DT Style](#9-manual-cs-control-with-dt-style)
10. [Error Codes](#10-error-codes)
11. [Migration Checklist](#11-migration-checklist-legacy--dt-style)

---

## 1. Core Structures

### `struct spi_dt_spec`

**PRIMARY structure for DT-style SPI** - contains everything needed.

```c
struct spi_dt_spec {
    const struct device *bus;     /* SPI controller device */
    struct spi_config config;     /* Complete SPI configuration */
};
```

This single structure replaces the need for separate device pointer and `spi_config`. It's initialized from device tree using macros.

---

### `struct spi_config`

Embedded in `spi_dt_spec`, holds all SPI settings.

```c
struct spi_config {
    uint32_t frequency;           /* Clock frequency (from DT: spi-max-frequency) */
    uint16_t operation;           /* Operation flags (mode, word size, etc.) */
    uint16_t slave;               /* Slave number (from DT: reg) */
    struct spi_cs_control cs;     /* Chip select (from DT: cs-gpios) */
};
```

---

### `struct spi_cs_control`

Chip select control (auto-populated from device tree).

```c
struct spi_cs_control {
    struct gpio_dt_spec gpio;     /* CS GPIO from device tree */
    uint32_t delay;               /* CS delay in microseconds */
};
```

---

## 2. Initialization Macros

### Macro Comparison

| Macro | Returns | Use Case |
|-------|---------|----------|
| `SPI_DT_SPEC_GET()` | `spi_dt_spec` (bus + config) | Most common, use with `_dt()` APIs |
| `SPI_CONFIG_DT()` | `spi_config` only | When you need config separately |
| `SPI_CONFIG_DT_INST()` | `spi_config` only | For driver instances |

---

### `SPI_DT_SPEC_GET(node_id, operation, delay)`

Get complete `spi_dt_spec` (bus + config) from device tree node.

| Parameter | Description |
|-----------|-------------|
| `node_id` | Device tree node (e.g., `DT_NODELABEL(ads1298)`) |
| `operation` | SPI operation flags (word size, mode, etc.) |
| `delay` | CS delay in microseconds |

**Example:**
```c
static const struct spi_dt_spec my_spi =
    SPI_DT_SPEC_GET(DT_NODELABEL(ads1298),
                    SPI_WORD_SET(8) | SPI_MODE_CPOL | SPI_MODE_CPHA,
                    0);
```

**What it extracts from device tree:**
| Field | Source |
|-------|--------|
| `bus` | Parent SPI controller |
| `frequency` | `spi-max-frequency` property |
| `slave` | `reg` property |
| `cs` | `cs-gpios` from parent controller |

---

### `SPI_CONFIG_DT(node_id, operation, delay)`

Get **only** `spi_config` from device tree node (no bus device).

| Parameter | Description |
|-----------|-------------|
| `node_id` | Device tree node (e.g., `DT_NODELABEL(ads1298)`) |
| `operation` | SPI operation flags (word size, mode, etc.) |
| `delay` | CS delay in microseconds |

**Example:**
```c
/* Get just the config */
static const struct spi_config my_spi_cfg =
    SPI_CONFIG_DT(DT_NODELABEL(ads1298),
                  SPI_WORD_SET(8) | SPI_MODE_CPOL | SPI_MODE_CPHA,
                  0);

/* Get the bus device separately */
static const struct device *spi_bus = DEVICE_DT_GET(DT_BUS(DT_NODELABEL(ads1298)));
```

**What it extracts from device tree:**
| Field | Source |
|-------|--------|
| `frequency` | `spi-max-frequency` property |
| `operation` | From macro parameter |
| `slave` | `reg` property |
| `cs.gpio` | `cs-gpios` from parent controller |
| `cs.delay` | From macro parameter |

**When to use `SPI_CONFIG_DT` instead of `SPI_DT_SPEC_GET`:**

| Scenario | Use |
|----------|-----|
| Standard DT-style code | `SPI_DT_SPEC_GET` |
| Need to modify config at runtime | `SPI_CONFIG_DT` |
| Using legacy `spi_transceive()` API | `SPI_CONFIG_DT` |
| Multiple configs for same bus | `SPI_CONFIG_DT` |
| Sharing bus between modules | `SPI_CONFIG_DT` |

---

### `SPI_CONFIG_DT_INST(inst, operation, delay)`

Same as `SPI_CONFIG_DT` but uses instance number (for driver development).

```c
/* In a Zephyr driver using DT_INST macros */
static const struct spi_config cfg = SPI_CONFIG_DT_INST(0, SPI_WORD_SET(8), 0);
```

---

### `SPI_DT_SPEC_GET_BY_IDX(node_id, idx, operation, delay)`

Get `spi_dt_spec` for specific index (multi-CS setups).

Use when a node has multiple chip selects.

---

### `SPI_DT_SPEC_GET_ON_BUS(node_id, bus, operation, delay)`

Get `spi_dt_spec` with explicit bus specification.

Use when node's parent is not the SPI controller.

---

## 3. Buffer Structures

### `struct spi_buf`

Single buffer descriptor.

```c
struct spi_buf {
    void *buf;                    /* Pointer to data buffer */
    size_t len;                   /* Length in bytes */
};
```

**Notes:**
- For TX: `buf` = data to send
- For RX: `buf` = buffer to receive into
- `buf = NULL` → TX sends zeros, RX discards data

---

### `struct spi_buf_set`

Set of buffers for scatter-gather.

```c
struct spi_buf_set {
    const struct spi_buf *buffers;  /* Array of spi_buf */
    size_t count;                    /* Number of buffers */
};
```

---

## 4. Operation Flags

### Word Size

| Flag | Description |
|------|-------------|
| `SPI_WORD_SET(8)` | 8-bit words (most common) |
| `SPI_WORD_SET(16)` | 16-bit words |

---

### SPI Mode (CPOL/CPHA)

| Flags | Mode | Description |
|-------|------|-------------|
| *(none)* | Mode 0 | CPOL=0, CPHA=0 (idle LOW, sample rising) |
| `SPI_MODE_CPHA` | Mode 1 | CPOL=0, CPHA=1 (idle LOW, sample falling) |
| `SPI_MODE_CPOL` | Mode 2 | CPOL=1, CPHA=0 (idle HIGH, sample falling) |
| `SPI_MODE_CPOL \| SPI_MODE_CPHA` | Mode 3 | CPOL=1, CPHA=1 (idle HIGH, sample rising) |

```
    Mode 0          Mode 1          Mode 2          Mode 3
    ──────          ──────          ──────          ──────
      _____           _____         ─────_         ─────_
CLK _/     \_       _/     \_           \_____         \_____
    ↑sample         ↓sample         ↓sample         ↑sample
```

---

### Bit Order

| Flag | Description |
|------|-------------|
| `SPI_TRANSFER_MSB` | MSB first (default, most common) |
| `SPI_TRANSFER_LSB` | LSB first |

---

### CS Control

| Flag | Description |
|------|-------------|
| `SPI_CS_ACTIVE_HIGH` | CS active HIGH (default is active LOW) |
| `SPI_HOLD_ON_CS` | Keep CS asserted between transactions |
| `SPI_LOCK_ON` | Lock bus for exclusive access |

---

### Line Modes

| Flag | Description |
|------|-------------|
| `SPI_LINES_SINGLE` | Standard SPI (default) |
| `SPI_LINES_DUAL` | Dual SPI |
| `SPI_LINES_QUAD` | Quad SPI |
| `SPI_LINES_OCTAL` | Octal SPI |

---

### Common Operation Combinations

```c
#define SPI_OP_MODE_0  (SPI_WORD_SET(8) | SPI_TRANSFER_MSB)
#define SPI_OP_MODE_1  (SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPHA)
#define SPI_OP_MODE_2  (SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL)
#define SPI_OP_MODE_3  (SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA)

/* ADS1298 uses Mode 1 (CPHA=1) or Mode 3 (CPOL=1, CPHA=1) */
#define ADS1298_SPI_OP (SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA)
```

---

## 5. DT-Style API Functions

### `spi_is_ready_dt(spec)`

Check if SPI device is ready.

```c
static inline bool spi_is_ready_dt(const struct spi_dt_spec *spec);
```

**Returns:** `true` if ready, `false` otherwise

**Example:**
```c
if (!spi_is_ready_dt(&my_spi)) {
    LOG_ERR("SPI not ready");
    return -ENODEV;
}
```

---

### `spi_transceive_dt(spec, tx_bufs, rx_bufs)`

Full-duplex transfer.

```c
static inline int spi_transceive_dt(const struct spi_dt_spec *spec,
                                    const struct spi_buf_set *tx_bufs,
                                    const struct spi_buf_set *rx_bufs);
```

**Returns:** 0 on success, negative error code on failure

---

### `spi_write_dt(spec, tx_bufs)`

Write only (no receive).

```c
static inline int spi_write_dt(const struct spi_dt_spec *spec,
                               const struct spi_buf_set *tx_bufs);
```

---

### `spi_read_dt(spec, rx_bufs)`

Read only (transmits zeros).

```c
static inline int spi_read_dt(const struct spi_dt_spec *spec,
                              const struct spi_buf_set *rx_bufs);
```

---

### `spi_release_dt(spec)`

Release bus lock.

```c
static inline int spi_release_dt(const struct spi_dt_spec *spec);
```

Use after transactions with `SPI_HOLD_ON_CS` or `SPI_LOCK_ON`.

---

## 6. Device Tree Configuration

### Example Device Tree Overlay

```dts
/* boards/nucleo_f767zi.overlay */

&spi1 {
    status = "okay";
    pinctrl-0 = <&spi1_sck_pa5 &spi1_miso_pa6 &spi1_mosi_pa7>;
    pinctrl-names = "default";

    /* CS GPIO for all slaves on this bus */
    cs-gpios = <&gpiod 14 GPIO_ACTIVE_LOW>;

    /* ADS1298 ADC */
    ads1298: ads1298@0 {
        compatible = "ti,ads1298";
        reg = <0>;                          /* Slave 0, uses cs-gpios[0] */
        spi-max-frequency = <2000000>;      /* 2 MHz */
        /* Optional: additional device-specific properties */
        drdy-gpios = <&gpiof 13 GPIO_ACTIVE_LOW>;
    };

    /* Example: Second device on same bus */
    flash: w25q128@1 {
        compatible = "jedec,spi-nor";
        reg = <1>;                          /* Slave 1, would need cs-gpios[1] */
        spi-max-frequency = <10000000>;     /* 10 MHz */
    };
};
```

### Device Tree Properties

| Property | Description |
|----------|-------------|
| `status` | `"okay"` to enable, `"disabled"` to disable |
| `pinctrl-0` | Pin configuration reference |
| `cs-gpios` | Chip select GPIO(s) for slaves |
| `reg` | Slave number (index into cs-gpios) |
| `spi-max-frequency` | Maximum SPI clock frequency in Hz |
| `compatible` | Device driver binding identifier |

---

## 7. Complete Usage Examples

### Example 1: Basic Initialization and Write

```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>

/* Define SPI operation flags */
#define MY_SPI_OP (SPI_WORD_SET(8) | SPI_TRANSFER_MSB)

/* Get SPI spec from device tree */
static const struct spi_dt_spec my_device =
    SPI_DT_SPEC_GET(DT_NODELABEL(ads1298), MY_SPI_OP, 0);

int main(void)
{
    /* Check if ready */
    if (!spi_is_ready_dt(&my_device)) {
        printk("SPI device not ready\n");
        return -ENODEV;
    }

    /* Prepare data */
    uint8_t tx_data[] = {0x11};  /* SDATAC command */

    struct spi_buf tx_buf = {
        .buf = tx_data,
        .len = sizeof(tx_data)
    };
    struct spi_buf_set tx = {
        .buffers = &tx_buf,
        .count = 1
    };

    /* Write */
    int ret = spi_write_dt(&my_device, &tx);
    if (ret < 0) {
        printk("SPI write failed: %d\n", ret);
    }

    return 0;
}
```

---

### Example 2: Read Data

```c
uint8_t rx_data[27];

struct spi_buf rx_buf = {
    .buf = rx_data,
    .len = sizeof(rx_data)
};
struct spi_buf_set rx = {
    .buffers = &rx_buf,
    .count = 1
};

int ret = spi_read_dt(&my_device, &rx);
```

---

### Example 3: Full-Duplex Transfer

```c
uint8_t tx_data[] = {0x20, 0x00};  /* Read register command */
uint8_t rx_data[3];                 /* Command echo + response */

struct spi_buf tx_buf = { .buf = tx_data, .len = sizeof(tx_data) };
struct spi_buf rx_buf = { .buf = rx_data, .len = sizeof(rx_data) };

struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };

int ret = spi_transceive_dt(&my_device, &tx, &rx);
/* rx_data[2] contains the register value */
```

---

### Example 4: Scatter-Gather (Multiple Buffers)

```c
uint8_t cmd = 0x20;           /* Command byte */
uint8_t addr = 0x01;          /* Address byte */
uint8_t dummy[4] = {0};       /* Dummy bytes for read */
uint8_t response[4];

struct spi_buf tx_bufs[] = {
    { .buf = &cmd,  .len = 1 },
    { .buf = &addr, .len = 1 },
    { .buf = dummy, .len = 4 }
};

struct spi_buf rx_bufs[] = {
    { .buf = NULL,     .len = 1 },  /* Discard during cmd */
    { .buf = NULL,     .len = 1 },  /* Discard during addr */
    { .buf = response, .len = 4 }   /* Capture response */
};

struct spi_buf_set tx = { .buffers = tx_bufs, .count = 3 };
struct spi_buf_set rx = { .buffers = rx_bufs, .count = 3 };

spi_transceive_dt(&my_device, &tx, &rx);
```

---

### Example 5: Helper Functions Pattern (using SPI_DT_SPEC_GET)

```c
#define ADS1298_SPI_OP (SPI_WORD_SET(8) | SPI_MODE_CPOL | SPI_MODE_CPHA)

static const struct spi_dt_spec ads1298_spi =
    SPI_DT_SPEC_GET(DT_NODELABEL(ads1298), ADS1298_SPI_OP, 0);

/* Send single command */
int ads1298_send_cmd(uint8_t cmd)
{
    struct spi_buf buf = { .buf = &cmd, .len = 1 };
    struct spi_buf_set set = { .buffers = &buf, .count = 1 };
    return spi_write_dt(&ads1298_spi, &set);
}

/* Read register */
int ads1298_read_reg(uint8_t reg, uint8_t *value)
{
    uint8_t tx[3] = { 0x20 | reg, 0x00, 0x00 };
    uint8_t rx[3];

    struct spi_buf tx_buf = { .buf = tx, .len = 3 };
    struct spi_buf rx_buf = { .buf = rx, .len = 3 };
    struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
    struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

    int ret = spi_transceive_dt(&ads1298_spi, &tx_set, &rx_set);
    if (ret == 0) {
        *value = rx[2];
    }
    return ret;
}

/* Write register */
int ads1298_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t tx[3] = { 0x40 | reg, 0x00, value };
    struct spi_buf buf = { .buf = tx, .len = 3 };
    struct spi_buf_set set = { .buffers = &buf, .count = 1 };
    return spi_write_dt(&ads1298_spi, &set);
}

/* Read conversion data (27 bytes) */
int ads1298_read_data(uint8_t *buffer)
{
    struct spi_buf buf = { .buf = buffer, .len = 27 };
    struct spi_buf_set set = { .buffers = &buf, .count = 1 };
    return spi_read_dt(&ads1298_spi, &set);
}
```

---

### Example 6: Using SPI_CONFIG_DT (Config Only)

Use `SPI_CONFIG_DT` when you need the config separately from the bus device.

```c
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>

#define ADS1298_SPI_OP (SPI_WORD_SET(8) | SPI_MODE_CPOL | SPI_MODE_CPHA)
#define ADS1298_NODE   DT_NODELABEL(ads1298)

/* Get config from device tree */
static const struct spi_config ads1298_cfg =
    SPI_CONFIG_DT(ADS1298_NODE, ADS1298_SPI_OP, 0);

/* Get bus device separately */
static const struct device *spi_bus = DEVICE_DT_GET(DT_BUS(ADS1298_NODE));

int ads1298_init(void)
{
    if (!device_is_ready(spi_bus)) {
        return -ENODEV;
    }
    return 0;
}

/* Use legacy API with DT config */
int ads1298_send_cmd(uint8_t cmd)
{
    struct spi_buf buf = { .buf = &cmd, .len = 1 };
    struct spi_buf_set set = { .buffers = &buf, .count = 1 };

    /* Legacy API: spi_write(device, config, bufs) */
    return spi_write(spi_bus, &ads1298_cfg, &set);
}

int ads1298_transceive(uint8_t *tx, uint8_t *rx, size_t len)
{
    struct spi_buf tx_buf = { .buf = tx, .len = len };
    struct spi_buf rx_buf = { .buf = rx, .len = len };
    struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
    struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

    return spi_transceive(spi_bus, &ads1298_cfg, &tx_set, &rx_set);
}
```

---

### Example 7: Modifying Config at Runtime

`SPI_CONFIG_DT` is useful when you need to change settings at runtime.

```c
#define ADS1298_NODE   DT_NODELABEL(ads1298)

/* Get base config from DT (not const - we'll modify it) */
static struct spi_config ads1298_cfg =
    SPI_CONFIG_DT(ADS1298_NODE, SPI_WORD_SET(8), 0);

static const struct device *spi_bus = DEVICE_DT_GET(DT_BUS(ADS1298_NODE));

/* Change SPI frequency at runtime */
void ads1298_set_speed(uint32_t frequency)
{
    ads1298_cfg.frequency = frequency;
}

/* Change SPI mode at runtime */
void ads1298_set_mode(uint8_t mode)
{
    /* Clear existing mode bits */
    ads1298_cfg.operation &= ~(SPI_MODE_CPOL | SPI_MODE_CPHA);

    /* Set new mode */
    switch (mode) {
    case 1:
        ads1298_cfg.operation |= SPI_MODE_CPHA;
        break;
    case 2:
        ads1298_cfg.operation |= SPI_MODE_CPOL;
        break;
    case 3:
        ads1298_cfg.operation |= SPI_MODE_CPOL | SPI_MODE_CPHA;
        break;
    default:
        /* Mode 0 - no flags needed */
        break;
    }
}

/* Example: Adaptive speed based on cable length */
void ads1298_auto_configure(bool long_cable)
{
    if (long_cable) {
        ads1298_set_speed(1000000);  /* 1 MHz for long cables */
    } else {
        ads1298_set_speed(4000000);  /* 4 MHz for short cables */
    }
}
```

---

### Example 8: Multiple Devices on Same Bus

Using `SPI_CONFIG_DT` for multiple devices sharing one SPI bus.

```c
/* Device tree:
 * &spi1 {
 *     ads1298: ads1298@0 { reg = <0>; spi-max-frequency = <2000000>; };
 *     flash: w25q@1 { reg = <1>; spi-max-frequency = <10000000>; };
 * };
 */

#define ADS1298_NODE  DT_NODELABEL(ads1298)
#define FLASH_NODE    DT_NODELABEL(flash)

/* Shared bus device */
static const struct device *spi_bus = DEVICE_DT_GET(DT_NODELABEL(spi1));

/* Separate configs for each device */
static const struct spi_config ads1298_cfg =
    SPI_CONFIG_DT(ADS1298_NODE, SPI_WORD_SET(8) | SPI_MODE_CPHA, 0);

static const struct spi_config flash_cfg =
    SPI_CONFIG_DT(FLASH_NODE, SPI_WORD_SET(8), 0);

/* Each device uses appropriate config */
int ads1298_read(uint8_t *buf, size_t len)
{
    struct spi_buf rx_buf = { .buf = buf, .len = len };
    struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };
    return spi_read(spi_bus, &ads1298_cfg, &rx);
}

int flash_read(uint8_t *buf, size_t len)
{
    struct spi_buf rx_buf = { .buf = buf, .len = len };
    struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };
    return spi_read(spi_bus, &flash_cfg, &rx);
}
```

---

### Comparison: SPI_DT_SPEC_GET vs SPI_CONFIG_DT

| Aspect | `SPI_DT_SPEC_GET` | `SPI_CONFIG_DT` |
|--------|-------------------|-----------------|
| Returns | `spi_dt_spec` (bus + config) | `spi_config` only |
| API style | `spi_write_dt(&spec, ...)` | `spi_write(dev, &cfg, ...)` |
| Runtime modification | Harder (config embedded) | Easy (standalone struct) |
| Device access | `spec.bus` | Need separate `DEVICE_DT_GET()` |
| Code simplicity | Simpler for single device | Better for multiple devices |
| Recommended for | New code, single device | Multi-device, dynamic config |

**Typical usage patterns:**

```c
/* Pattern 1: SPI_DT_SPEC_GET (recommended for most cases) */
static const struct spi_dt_spec dev = SPI_DT_SPEC_GET(NODE, OP, 0);
spi_write_dt(&dev, &bufs);

/* Pattern 2: SPI_CONFIG_DT (when you need flexibility) */
static const struct device *bus = DEVICE_DT_GET(DT_BUS(NODE));
static struct spi_config cfg = SPI_CONFIG_DT(NODE, OP, 0);
spi_write(bus, &cfg, &bufs);
```

---

## 8. Structure Relationships Diagram

### Using SPI_DT_SPEC_GET (Complete Spec)

```
Device Tree                          C Code
───────────                          ──────

┌─────────────────┐
│ &spi1 {         │
│   cs-gpios=...; ├──────────┐
│                 │          │
│   ads1298@0 {   │          │
│     reg = <0>;  │          │
│     spi-max-    │          ▼
│     frequency;  │   ┌─────────────────────────────────────┐
│   };            │   │ SPI_DT_SPEC_GET(node, op, delay)    │
│ };              │   └──────────────────┬──────────────────┘
└─────────────────┘                      │
                                         ▼
                              ┌─────────────────────┐
                              │  struct spi_dt_spec │
                              ├─────────────────────┤
                              │  *bus ──────────────┼──► SPI controller device
                              │  config:            │
                              │    .frequency ◄─────┼─── spi-max-frequency
                              │    .operation ◄─────┼─── from macro param
                              │    .slave ◄─────────┼─── reg property
                              │    .cs.gpio ◄───────┼─── cs-gpios[slave]
                              │    .cs.delay ◄──────┼─── from macro param
                              └─────────────────────┘
                                         │
                                         ▼
                              ┌─────────────────────┐
                              │  spi_write_dt()     │
                              │  spi_read_dt()      │
                              │  spi_transceive_dt()│
                              └─────────────────────┘
```

### Using SPI_CONFIG_DT (Config Only)

```
Device Tree                          C Code
───────────                          ──────

┌─────────────────┐
│ &spi1 {         │
│   cs-gpios=...; ├──────────┬──────────────────────────────────────┐
│                 │          │                                      │
│   ads1298@0 {   │          │                                      │
│     reg = <0>;  │          │                                      │
│     spi-max-    │          ▼                                      ▼
│     frequency;  │   ┌──────────────────────┐    ┌─────────────────────────────┐
│   };            │   │ SPI_CONFIG_DT(node,  │    │ DEVICE_DT_GET(DT_BUS(node)) │
│ };              │   │            op, delay)│    └──────────────┬──────────────┘
└─────────────────┘   └──────────┬───────────┘                   │
                                 │                               │
                                 ▼                               ▼
                      ┌─────────────────────┐         ┌──────────────────┐
                      │  struct spi_config  │         │  struct device * │
                      ├─────────────────────┤         │    (spi_bus)     │
                      │  .frequency ◄───────┼─── spi-max-frequency       │
                      │  .operation ◄───────┼─── from macro param        │
                      │  .slave ◄───────────┼─── reg property            │
                      │  .cs.gpio ◄─────────┼─── cs-gpios[slave]         │
                      │  .cs.delay ◄────────┼─── from macro param        │
                      └─────────────────────┘         └──────────────────┘
                                 │                               │
                                 └───────────────┬───────────────┘
                                                 ▼
                                      ┌─────────────────────┐
                                      │  spi_write()        │
                                      │  spi_read()         │
                                      │  spi_transceive()   │
                                      │  (legacy API)       │
                                      └─────────────────────┘
```

### Side-by-Side Comparison

```
┌─────────────────────────────────────┬─────────────────────────────────────┐
│         SPI_DT_SPEC_GET             │           SPI_CONFIG_DT             │
├─────────────────────────────────────┼─────────────────────────────────────┤
│                                     │                                     │
│  static const struct spi_dt_spec    │  static const struct device *bus =  │
│      dev = SPI_DT_SPEC_GET(...);    │      DEVICE_DT_GET(DT_BUS(node));   │
│                                     │                                     │
│                                     │  static struct spi_config cfg =     │
│                                     │      SPI_CONFIG_DT(...);            │
│                                     │                                     │
├─────────────────────────────────────┼─────────────────────────────────────┤
│                                     │                                     │
│  spi_write_dt(&dev, &bufs);         │  spi_write(bus, &cfg, &bufs);       │
│                                     │                                     │
│  spi_transceive_dt(&dev, &tx, &rx); │  spi_transceive(bus, &cfg,          │
│                                     │                 &tx, &rx);          │
│                                     │                                     │
├─────────────────────────────────────┼─────────────────────────────────────┤
│  ✓ Simpler, all-in-one             │  ✓ Flexible, modifiable config     │
│  ✓ Recommended for new code        │  ✓ Good for multi-device buses     │
│  ✗ Config embedded, hard to change │  ✗ Need to manage device + config  │
└─────────────────────────────────────┴─────────────────────────────────────┘
```

---

## 9. Manual CS Control with DT Style

Sometimes you need manual CS control (e.g., ADS1298 requires specific timing). You can still use DT style but control CS separately.

### Option 1: Define CS in DT, Control Manually

**Device tree:**
```dts
ads1298: ads1298@0 {
    compatible = "ti,ads1298";
    reg = <0>;
    spi-max-frequency = <2000000>;
    cs-gpios = <&gpiod 14 GPIO_ACTIVE_LOW>;  /* Device-level CS */
};
```

**C code:**
```c
#define ADS1298_SPI_OP (SPI_WORD_SET(8) | SPI_MODE_CPOL | SPI_MODE_CPHA)

/* Get SPI spec WITHOUT automatic CS */
static const struct spi_dt_spec ads1298_spi =
    SPI_DT_SPEC_GET(DT_NODELABEL(ads1298), ADS1298_SPI_OP, 0);

/* Get CS GPIO spec separately */
static const struct gpio_dt_spec ads1298_cs =
    GPIO_DT_SPEC_GET(DT_NODELABEL(ads1298), cs_gpios);

int init(void)
{
    gpio_pin_configure_dt(&ads1298_cs, GPIO_OUTPUT_INACTIVE);
    return 0;
}

int transfer_with_manual_cs(uint8_t *tx, uint8_t *rx, size_t len)
{
    struct spi_buf tx_buf = { .buf = tx, .len = len };
    struct spi_buf rx_buf = { .buf = rx, .len = len };
    struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
    struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

    gpio_pin_set_dt(&ads1298_cs, 1);  /* CS LOW (active) */
    int ret = spi_transceive_dt(&ads1298_spi, &tx_set, &rx_set);
    gpio_pin_set_dt(&ads1298_cs, 0);  /* CS HIGH (inactive) */

    return ret;
}
```

---

### Option 2: Separate SPI Config Without CS

**Device tree:**
```dts
/* Don't include cs-gpios in SPI controller for this slave */
&spi1 {
    status = "okay";
    /* No cs-gpios here - manual control */

    ads1298: ads1298@0 {
        compatible = "ti,ads1298";
        reg = <0>;
        spi-max-frequency = <2000000>;
    };
};

/* Define CS GPIO elsewhere */
/ {
    ads1298_pins {
        compatible = "gpio-keys";
        ads1298_cs: cs_pin {
            gpios = <&gpiod 14 GPIO_ACTIVE_LOW>;
        };
    };
};
```

---

## 10. Error Codes

| Return Value | Meaning |
|--------------|---------|
| `0` | Success |
| `-ENODEV` | Device not found or not ready |
| `-ENOTSUP` | Operation not supported |
| `-EINVAL` | Invalid parameter |
| `-EIO` | I/O error during transfer |
| `-EBUSY` | Bus busy |
| `-ETIMEDOUT` | Transfer timed out |

---

## 11. Migration Checklist (Legacy → DT Style)

### Option A: Full DT Migration (Recommended)

- [ ] Add device node in overlay with `reg`, `spi-max-frequency`
- [ ] Add `cs-gpios` to SPI controller (or device node for manual CS)
- [ ] Replace `struct device *` + `struct spi_config` with `struct spi_dt_spec`
- [ ] Replace `DEVICE_DT_GET()` + manual config with `SPI_DT_SPEC_GET()`
- [ ] Replace `device_is_ready()` with `spi_is_ready_dt()`
- [ ] Replace `spi_write()` with `spi_write_dt()`
- [ ] Replace `spi_read()` with `spi_read_dt()`
- [ ] Replace `spi_transceive()` with `spi_transceive_dt()`
- [ ] Replace `spi_release()` with `spi_release_dt()`
- [ ] Remove manual frequency/operation/slave assignments
- [ ] Test!

### Option B: Config-Only DT Migration (Keep Legacy API)

- [ ] Add device node in overlay with `reg`, `spi-max-frequency`
- [ ] Add `cs-gpios` to SPI controller
- [ ] Replace manual `struct spi_config` with `SPI_CONFIG_DT()`
- [ ] Keep `DEVICE_DT_GET(DT_BUS(node))` for bus device
- [ ] Keep using `spi_write()`, `spi_read()`, `spi_transceive()`
- [ ] Remove manual frequency/slave/cs assignments (now from DT)
- [ ] Test!

### Quick Reference

```c
/* Before (fully manual) */
struct spi_config cfg = {
    .frequency = 2000000,
    .operation = SPI_WORD_SET(8) | SPI_MODE_CPHA,
    .slave = 0,
};

/* After Option A: SPI_DT_SPEC_GET */
static const struct spi_dt_spec dev =
    SPI_DT_SPEC_GET(DT_NODELABEL(mydev), SPI_WORD_SET(8) | SPI_MODE_CPHA, 0);
/* Use: spi_write_dt(&dev, &bufs); */

/* After Option B: SPI_CONFIG_DT */
static const struct spi_config cfg =
    SPI_CONFIG_DT(DT_NODELABEL(mydev), SPI_WORD_SET(8) | SPI_MODE_CPHA, 0);
static const struct device *bus = DEVICE_DT_GET(DT_BUS(DT_NODELABEL(mydev)));
/* Use: spi_write(bus, &cfg, &bufs); */
```
