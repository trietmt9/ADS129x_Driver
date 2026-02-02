# ADS1298 Interaction Workflow

## Overview

This document describes the step-by-step workflow to interact with the ADS1298 8-channel, 24-bit ADC for EMG signal acquisition.

---

## Hardware Setup

### Pin Connections (Nucleo F767ZI - SPI1)

```
ADS1298          Nucleo F767ZI      Description
─────────────────────────────────────────────────
DVDD             3.3V               Digital supply
AVDD             5V or 3.3V         Analog supply
DGND             GND                Digital ground
AGND             GND                Analog ground
SCLK             PA5 (D13)          SPI clock
DIN              PA7 (D11)          SPI MOSI (data to ADS)
DOUT             PA6 (D12)          SPI MISO (data from ADS)
CS               PD14 (D10)         Chip select (active LOW)
DRDY             PF13 (D7)          Data ready (active LOW)
START            PE9 (D6)           Start conversion (optional)
RESET            PF14 (D4)          Hardware reset (optional)
CLKSEL           DVDD               Use internal clock
```

### SPI Configuration

- **Mode**: SPI Mode 1 (CPOL=0, CPHA=1) or Mode 3 (CPOL=1, CPHA=1)
- **Speed**: 2 MHz (can go up to 20 MHz)
- **Word Size**: 8 bits
- **Bit Order**: MSB first

---

## Workflow Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                        POWER ON                                  │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│  STEP 1: Wait for Power-On Reset (~150ms)                       │
│  - Device performs internal reset                               │
│  - Device enters RDATAC mode by default                         │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│  STEP 2: Reset Device                                           │
│  - Send RESET command (0x06) OR pulse RESET pin LOW             │
│  - Wait 18 tCLK (~1ms)                                          │
│  - Send SDATAC command (0x11) to exit continuous mode           │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│  STEP 3: Verify Device ID                                       │
│  - Read ID register (0x00)                                      │
│  - Expected value: 0x92 (ADS1298)                               │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│  STEP 4: Configure Registers                                    │
│  - CONFIG1: Sample rate, high-resolution mode                   │
│  - CONFIG2: Test signals (usually 0x00)                         │
│  - CONFIG3: Internal reference, RLD settings                    │
│  - CHnSET: Channel gain and input selection                     │
│  - Wait 150ms for internal reference to settle                  │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│  STEP 5: Start Conversion                                       │
│  - Send START command (0x08) OR set START pin HIGH              │
│  - Send RDATAC command (0x10) for continuous read mode          │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│  STEP 6: Read Data Loop                                         │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  Wait for DRDY pin to go LOW                            │    │
│  │              │                                          │    │
│  │              ▼                                          │    │
│  │  Assert CS (LOW)                                        │    │
│  │              │                                          │    │
│  │              ▼                                          │    │
│  │  Read 27 bytes via SPI                                  │    │
│  │  (3 status + 8 channels × 3 bytes)                      │    │
│  │              │                                          │    │
│  │              ▼                                          │    │
│  │  Deassert CS (HIGH)                                     │    │
│  │              │                                          │    │
│  │              ▼                                          │    │
│  │  Parse and process data                                 │    │
│  │              │                                          │    │
│  │              ▼                                          │    │
│  │  Repeat ◄────┘                                          │    │
│  └─────────────────────────────────────────────────────────┘    │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│  STEP 7: Stop Conversion                                        │
│  - Send SDATAC command (0x11) to exit continuous mode           │
│  - Send STOP command (0x0A) OR set START pin LOW                │
└─────────────────────────────────────────────────────────────────┘
```

---

## Detailed Steps

### Step 1: Power-On Wait

```c
/* Wait for power-on reset (tPOR) */
k_msleep(150);
```

The ADS1298 requires approximately 2^18 clock cycles after power-on before it's ready. With the internal 2.048 MHz clock, this is about 128ms. We wait 150ms to be safe.

---

### Step 2: Reset Device

```c
/* Option A: Software Reset */
ads1298_send_command(&ads1298, ADS1298_CMD_RESET);  /* 0x06 */
k_busy_wait(1000);  /* Wait 18 tCLK */

/* Option B: Hardware Reset (if RESET pin connected) */
gpio_pin_set(reset_port, reset_pin, 0);  /* RESET LOW */
k_busy_wait(10);
gpio_pin_set(reset_port, reset_pin, 1);  /* RESET HIGH */
k_busy_wait(1000);

/* Exit RDATAC mode (device starts in this mode after reset) */
ads1298_send_command(&ads1298, ADS1298_CMD_SDATAC);  /* 0x11 */
```

**Important**: After reset, the device enters RDATAC (Read Data Continuous) mode. You MUST send SDATAC before you can read/write registers.

---

### Step 3: Verify Device ID

```c
uint8_t device_id;
ads1298_read_register(&ads1298, ADS1298_REG_ID, &device_id);

/* ADS1298 ID format: [REV_ID:3][DEV_ID:5] */
/* DEV_ID for ADS1298 = 0x12, typical full ID = 0x92 */
if ((device_id & 0x1F) == 0x12) {
    /* ADS1298 detected */
}
```

---

### Step 4: Configure Registers

#### 4.1 CONFIG1 - Sample Rate

```c
/* CONFIG1 Register (0x01)
 * Bit 7: HR = 1 (High-resolution mode)
 * Bit 6: DAISY_EN = 0 (Multiple readback mode)
 * Bit 5: CLK_EN = 0 (Clock output disabled)
 * Bits [2:0]: DR - Data rate
 */

/* Example: High-resolution, 500 SPS */
uint8_t config1 = 0x86;  /* HR=1, DR=110 (500 SPS) */
ads1298_write_register(&ads1298, ADS1298_REG_CONFIG1, config1);
```

| DR[2:0] | Sample Rate (HR mode) |
|---------|----------------------|
| 000     | 32 kSPS              |
| 001     | 16 kSPS              |
| 010     | 8 kSPS               |
| 011     | 4 kSPS               |
| 100     | 2 kSPS               |
| 101     | 1 kSPS               |
| 110     | 500 SPS              |

#### 4.2 CONFIG2 - Test Signals

```c
/* CONFIG2 Register (0x02)
 * For normal operation, set to 0x00
 * To enable internal test signal, set bit 5
 */
uint8_t config2 = 0x00;  /* Normal operation */
ads1298_write_register(&ads1298, ADS1298_REG_CONFIG2, config2);
```

#### 4.3 CONFIG3 - Reference and RLD

```c
/* CONFIG3 Register (0x03)
 * Bit 7: PD_REFBUF = 1 (Enable internal reference buffer)
 * Bit 6: Reserved = 1 (Must be 1)
 * Bit 5: VREF_4V = 0 (2.4V reference)
 * Bit 3: RLDREF_INT = 1 (Internal RLD reference)
 * Bit 2: PD_RLD = 1 (Enable RLD buffer) - optional for EMG
 */

/* With internal reference, no RLD */
uint8_t config3 = 0xC0;  /* PD_REFBUF=1, Reserved=1 */

/* With internal reference and RLD enabled */
/* uint8_t config3 = 0xDC; */

ads1298_write_register(&ads1298, ADS1298_REG_CONFIG3, config3);

/* Wait for internal reference to settle */
k_msleep(150);
```

#### 4.4 Channel Configuration

```c
/* CHnSET Register (0x05-0x0C)
 * Bit 7: PD = 0 (Normal operation) or 1 (Power down)
 * Bits [6:4]: GAIN
 * Bits [2:0]: MUX (input selection)
 */

/* Channel settings for EMG */
/* Enable CH1 and CH2 with Gain 12, normal input */
uint8_t ch_enabled = ADS1298_CHSET_GAIN_12 | ADS1298_CHSET_MUX_NORMAL;  /* 0x60 */
ads1298_write_register(&ads1298, ADS1298_REG_CH1SET, ch_enabled);
ads1298_write_register(&ads1298, ADS1298_REG_CH2SET, ch_enabled);

/* Power down unused channels (CH3-CH8) */
uint8_t ch_disabled = ADS1298_CHSET_PD | ADS1298_CHSET_MUX_SHORTED;  /* 0x81 */
for (int i = 2; i < 8; i++) {
    ads1298_write_register(&ads1298, ADS1298_REG_CH1SET + i, ch_disabled);
}
```

| GAIN[2:0] | PGA Gain |
|-----------|----------|
| 000       | 6        |
| 001       | 1        |
| 010       | 2        |
| 011       | 3        |
| 100       | 4        |
| 101       | 8        |
| 110       | 12       |

| MUX[2:0] | Input Selection      |
|----------|---------------------|
| 000      | Normal electrode    |
| 001      | Input shorted       |
| 010      | RLD measurement     |
| 011      | MVDD supply         |
| 100      | Temperature sensor  |
| 101      | Test signal         |

---

### Step 5: Start Conversion

```c
/* Option A: Software Start */
ads1298_send_command(&ads1298, ADS1298_CMD_START);   /* 0x08 */

/* Option B: Hardware Start (if START pin connected) */
gpio_pin_set(start_port, start_pin, 1);  /* START HIGH */

/* Enter continuous read mode */
ads1298_send_command(&ads1298, ADS1298_CMD_RDATAC);  /* 0x10 */
```

---

### Step 6: Read Data

#### 6.1 Data Format

Each conversion produces 27 bytes:

```
Byte 0-2:   Status (24 bits)
Byte 3-5:   Channel 1 data (24-bit signed)
Byte 6-8:   Channel 2 data (24-bit signed)
Byte 9-11:  Channel 3 data (24-bit signed)
Byte 12-14: Channel 4 data (24-bit signed)
Byte 15-17: Channel 5 data (24-bit signed)
Byte 18-20: Channel 6 data (24-bit signed)
Byte 21-23: Channel 7 data (24-bit signed)
Byte 24-26: Channel 8 data (24-bit signed)
```

Status format: `1100 + LOFF_STATP[7:0] + LOFF_STATN[7:0] + GPIO[4:1]`

#### 6.2 Reading Code

```c
/* Wait for DRDY to go LOW */
while (gpio_pin_get(drdy_port, drdy_pin) != 0) {
    k_yield();
}

/* Read 27 bytes */
uint8_t buffer[27];
spi_bus_cs_control(&spi, true);   /* CS LOW */
spi_bus_read(&spi, buffer, 27);
spi_bus_cs_control(&spi, false);  /* CS HIGH */

/* Parse channel data */
int32_t ch1 = ads1298_convert_sample(buffer[3], buffer[4], buffer[5]);
int32_t ch2 = ads1298_convert_sample(buffer[6], buffer[7], buffer[8]);
```

#### 6.3 Convert 24-bit to Signed 32-bit

```c
int32_t ads1298_convert_sample(uint8_t msb, uint8_t mid, uint8_t lsb)
{
    int32_t value = ((int32_t)msb << 16) | ((int32_t)mid << 8) | lsb;

    /* Sign extend from 24-bit to 32-bit */
    if (value & 0x800000) {
        value |= 0xFF000000;
    }
    return value;
}
```

#### 6.4 Convert to Voltage

```c
/* Vref = 2.4V (internal reference)
 * Full scale = +/- Vref / Gain
 * Resolution = 24 bits (2^23 for positive range)
 *
 * Voltage = (code / 2^23) × (Vref / Gain)
 */

float vref = 2.4f;
float gain = 12.0f;
float voltage = (float)ch1 / 8388608.0f * (vref / gain);
float voltage_mv = voltage * 1000.0f;  /* Convert to mV */
```

---

### Step 7: Stop Conversion

```c
/* Exit continuous read mode first */
ads1298_send_command(&ads1298, ADS1298_CMD_SDATAC);  /* 0x11 */

/* Stop conversion */
ads1298_send_command(&ads1298, ADS1298_CMD_STOP);    /* 0x0A */

/* Or if using hardware START pin */
gpio_pin_set(start_port, start_pin, 0);  /* START LOW */
```

---

## SPI Command Reference

### Sending a Command

```c
void send_command(uint8_t cmd)
{
    spi_bus_cs_control(&spi, true);   /* CS LOW */
    spi_bus_write_byte(&spi, cmd);
    k_busy_wait(4);                    /* 4 tCLK decode time */
    spi_bus_cs_control(&spi, false);  /* CS HIGH */
}
```

### Reading a Register

```c
uint8_t read_register(uint8_t reg)
{
    uint8_t cmd[2] = {
        0x20 | (reg & 0x1F),  /* RREG + address */
        0x00                   /* Read 1 register (n-1) */
    };
    uint8_t value;

    spi_bus_cs_control(&spi, true);
    spi_bus_write(&spi, cmd, 2);
    k_busy_wait(4);
    spi_bus_read_byte(&spi, &value);
    spi_bus_cs_control(&spi, false);

    return value;
}
```

### Writing a Register

```c
void write_register(uint8_t reg, uint8_t value)
{
    uint8_t cmd[3] = {
        0x40 | (reg & 0x1F),  /* WREG + address */
        0x00,                  /* Write 1 register (n-1) */
        value
    };

    spi_bus_cs_control(&spi, true);
    spi_bus_write(&spi, cmd, 3);
    k_busy_wait(4);
    spi_bus_cs_control(&spi, false);
}
```

---

## Complete Initialization Sequence

```c
int ads1298_init_sequence(struct ads1298_dev *dev)
{
    int ret;
    uint8_t id;

    /* 1. Wait for power-on reset */
    k_msleep(150);

    /* 2. Reset device */
    ret = ads1298_reset(dev);
    if (ret < 0) return ret;

    /* 3. Verify device ID */
    ret = ads1298_read_id(dev, &id);
    if (ret < 0) return ret;
    if ((id & 0x1F) != 0x12) {
        return -ENODEV;
    }

    /* 4. Configure registers */
    /* CONFIG1: HR mode, 500 SPS */
    ret = ads1298_write_register(dev, ADS1298_REG_CONFIG1, 0x86);
    if (ret < 0) return ret;

    /* CONFIG2: Normal operation */
    ret = ads1298_write_register(dev, ADS1298_REG_CONFIG2, 0x00);
    if (ret < 0) return ret;

    /* CONFIG3: Internal reference enabled */
    ret = ads1298_write_register(dev, ADS1298_REG_CONFIG3, 0xC0);
    if (ret < 0) return ret;

    /* Wait for reference to settle */
    k_msleep(150);

    /* CH1 & CH2: Gain 12, normal input */
    ret = ads1298_write_register(dev, ADS1298_REG_CH1SET, 0x60);
    if (ret < 0) return ret;
    ret = ads1298_write_register(dev, ADS1298_REG_CH2SET, 0x60);
    if (ret < 0) return ret;

    /* CH3-CH8: Power down */
    for (int i = 2; i < 8; i++) {
        ret = ads1298_write_register(dev, ADS1298_REG_CH1SET + i, 0x81);
        if (ret < 0) return ret;
    }

    return 0;
}
```

---

## Troubleshooting

| Issue | Possible Cause | Solution |
|-------|---------------|----------|
| Device ID reads 0x00 or 0xFF | SPI not working | Check wiring, SPI mode, clock speed |
| Register reads fail | Still in RDATAC mode | Send SDATAC before register access |
| DRDY never goes LOW | Conversion not started | Send START command |
| Data reads all zeros | CS timing issue | Ensure CS is LOW during entire read |
| Noisy readings | Poor grounding | Improve analog ground connection |
| Clipped readings | Gain too high | Reduce PGA gain |

---

## Recommended EMG Settings

| Parameter | Value | Register |
|-----------|-------|----------|
| Sample Rate | 500-2000 SPS | CONFIG1 |
| PGA Gain | 6 or 12 | CHnSET |
| Reference | Internal 2.4V | CONFIG3 |
| Input | Normal electrode | CHnSET |
| RLD | Optional | CONFIG3 |
