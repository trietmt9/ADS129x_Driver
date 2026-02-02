# ADS1298 Key Registers for EMG Acquisition

## Overview

The ADS1298 is an 8-channel, 24-bit analog front-end designed for biopotential measurements including EMG. This document lists the essential registers needed for EMG data acquisition.

---

## Register Summary Table

| Address | Register    | Reset | Description                          |
|---------|-------------|-------|--------------------------------------|
| 0x00    | ID          | xx    | Device ID (read-only)                |
| 0x01    | CONFIG1     | 0x06  | Data rate, HR/LP mode                |
| 0x02    | CONFIG2     | 0x40  | Test signal configuration            |
| 0x03    | CONFIG3     | 0x40  | Reference buffer, RLD settings       |
| 0x04    | LOFF        | 0x00  | Lead-off detection settings          |
| 0x05-0x0C | CH1SET-CH8SET | 0x00 | Per-channel gain & input mux       |
| 0x17    | CONFIG4     | 0x00  | Conversion mode, lead-off comparator |

---

## Essential Registers for EMG

### 1. CONFIG1 (0x01) - Data Rate & Mode Selection

```
Bit 7   : HR        - High-resolution mode (1=HR, 0=LP)
Bit 6   : DAISY_EN  - Daisy-chain mode
Bit 5   : CLK_EN    - Clock output enable
Bit 4-3 : Reserved  - Always write 0
Bit 2-0 : DR[2:0]   - Output data rate
```

**Data Rate Settings (HR mode, fCLK = 2.048MHz):**
| DR[2:0] | Data Rate |
|---------|-----------|
| 000     | 32 kSPS   |
| 001     | 16 kSPS   |
| 010     | 8 kSPS    |
| 011     | 4 kSPS    |
| 100     | 2 kSPS    |
| 101     | 1 kSPS    |
| 110     | 500 SPS   |

**EMG Recommendation:** Use `0x86` for HR mode at 500 SPS, or `0x82` for 8 kSPS (better for muscle activity capture).

---

### 2. CONFIG2 (0x02) - Test Signal Configuration

```
Bit 7-6 : Reserved     - Always write 0
Bit 5   : WCT_CHOP     - WCT chopping scheme
Bit 4   : INT_TEST     - Internal test signal (1=internal, 0=external)
Bit 3   : Reserved     - Always write 0
Bit 2   : TEST_AMP     - Test signal amplitude
Bit 1-0 : TEST_FREQ[1:0] - Test signal frequency
```

**EMG Recommendation:** Use `0x00` for normal operation (no test signal).

---

### 3. CONFIG3 (0x03) - Reference & RLD Configuration

```
Bit 7   : PD_REFBUF     - Internal reference buffer (1=enabled)
Bit 6   : Reserved      - Always write 1
Bit 5   : VREF_4V       - Reference voltage (0=2.4V, 1=4V)
Bit 4   : RLD_MEAS      - RLD measurement enable
Bit 3   : RLDREF_INT    - Internal RLD reference (1=internal)
Bit 2   : PD_RLD        - RLD buffer power (1=enabled)
Bit 1   : RLD_LOFF_SENS - RLD sense function
Bit 0   : RLD_STAT      - RLD lead-off status (read-only)
```

**EMG Recommendation:** Use `0xC0` to enable internal reference buffer with 2.4V reference.

---

### 4. CHnSET (0x05-0x0C) - Channel Settings

Each channel (1-8) has its own register with identical bit fields:

```
Bit 7   : PDn         - Power-down (0=normal, 1=power-down)
Bit 6-4 : GAINn[2:0]  - PGA gain setting
Bit 3   : Reserved    - Always write 0
Bit 2-0 : MUXn[2:0]   - Input multiplexer setting
```

**PGA Gain Settings:**
| GAINn[2:0] | Gain |
|------------|------|
| 000        | 6    |
| 001        | 1    |
| 010        | 2    |
| 011        | 3    |
| 100        | 4    |
| 101        | 8    |
| 110        | 12   |

**Input Multiplexer Settings:**
| MUXn[2:0] | Input Selection                        |
|-----------|----------------------------------------|
| 000       | Normal electrode input                 |
| 001       | Input shorted (for offset/noise test)  |
| 010       | RLD measurement                        |
| 011       | MVDD supply measurement                |
| 100       | Temperature sensor                     |
| 101       | Test signal                            |
| 110       | RLD_DRP (positive electrode driver)    |
| 111       | RLD_DRN (negative electrode driver)    |

**EMG Recommendation:** 
- Use gain of 6 or 12 for typical EMG signals (e.g., `0x60` for gain=6, normal input)
- For noise testing: `0x01` (shorted inputs)

---

### 5. CONFIG4 (0x17) - Conversion Mode & Lead-Off

```
Bit 7-5 : RESP_FREQ[2:0]  - Respiration frequency (not used for EMG)
Bit 4   : Reserved        - Always write 0
Bit 3   : SINGLE_SHOT     - Conversion mode (0=continuous, 1=single-shot)
Bit 2   : WCT_TO_RLD      - WCT to RLD connection
Bit 1   : PD_LOFF_COMP    - Lead-off comparator (1=enabled)
Bit 0   : Reserved        - Always write 0
```

**EMG Recommendation:** Use `0x00` for continuous conversion mode.

---

### 6. LOFF (0x04) - Lead-Off Detection (Optional)

```
Bit 7-5 : COMP_TH[2:0]    - Comparator threshold
Bit 4   : VLEAD_OFF_EN    - Lead-off mode (0=current, 1=resistor)
Bit 3-2 : ILEAD_OFF[1:0]  - Lead-off current (6nA to 24nA)
Bit 1-0 : FLEAD_OFF[1:0]  - Lead-off frequency
```

**Lead-Off Current Options:**
| ILEAD_OFF[1:0] | Current |
|----------------|---------|
| 00             | 6 nA    |
| 01             | 12 nA   |
| 10             | 18 nA   |
| 11             | 24 nA   |

---

## EMG Configuration Example

### Basic EMG Setup (C Code)

```c
// Register addresses
#define ID_REG          0x00
#define CONFIG1_REG     0x01
#define CONFIG2_REG     0x02
#define CONFIG3_REG     0x03
#define LOFF_REG        0x04
#define CH1SET_REG      0x05
#define CH2SET_REG      0x06
#define CH3SET_REG      0x07
#define CH4SET_REG      0x08
#define CH5SET_REG      0x09
#define CH6SET_REG      0x0A
#define CH7SET_REG      0x0B
#define CH8SET_REG      0x0C
#define CONFIG4_REG     0x17

// Recommended EMG configuration values
#define CONFIG1_EMG     0x86    // HR mode, 500 SPS
#define CONFIG2_EMG     0x00    // No test signal
#define CONFIG3_EMG     0xC0    // Internal reference enabled
#define CONFIG4_EMG     0x00    // Continuous conversion
#define CHSET_EMG       0x60    // Gain=6, normal input

void ads1298_init_emg(void)
{
    // 1. Send SDATAC command to stop continuous read mode
    ads1298_send_command(SDATAC);
    
    // 2. Enable internal reference
    ads1298_write_reg(CONFIG3_REG, CONFIG3_EMG);
    delay_ms(150);  // Wait for reference to settle
    
    // 3. Configure data rate (HR mode, 500 SPS for typical EMG)
    ads1298_write_reg(CONFIG1_REG, CONFIG1_EMG);
    
    // 4. Disable test signals
    ads1298_write_reg(CONFIG2_REG, CONFIG2_EMG);
    
    // 5. Configure channels with appropriate gain
    // EMG typically needs gain of 6-12
    for (int ch = CH1SET_REG; ch <= CH8SET_REG; ch++) {
        ads1298_write_reg(ch, CHSET_EMG);
    }
    
    // 6. Enable continuous conversion
    ads1298_write_reg(CONFIG4_REG, CONFIG4_EMG);
    
    // 7. Start conversion
    ads1298_send_command(START);
    
    // 8. Enable continuous data read mode
    ads1298_send_command(RDATAC);
}
```

### High Sample Rate EMG (for fine motor control analysis)

```c
// For faster EMG capture (8 kSPS)
#define CONFIG1_EMG_FAST    0x82    // HR mode, 8 kSPS
#define CHSET_EMG_HIGH_GAIN 0xA0    // Gain=12, normal input
```

---

## Data Format

When reading data in continuous mode (RDATAC), the output format is:

```
| 24-bit Status | 24-bit CH1 | 24-bit CH2 | ... | 24-bit CH8 |
|   3 bytes     |  3 bytes   |  3 bytes   | ... |  3 bytes   |
```

**Status Word (24 bits):**
- Bit 23-20: 1100 (fixed pattern, indicates new data)
- Bit 19-15: LOFF_STATP (lead-off status, positive)
- Bit 14-8:  LOFF_STATN (lead-off status, negative) 
- Bit 7-4:   GPIO data
- Bit 3-0:   Reserved

**Channel Data (24 bits, two's complement):**
- Full-scale range: ±VREF/Gain
- Resolution: 24 bits

---

## Key SPI Commands

| Command   | Opcode | Description                    |
|-----------|--------|--------------------------------|
| WAKEUP    | 0x02   | Wake from standby              |
| STANDBY   | 0x04   | Enter standby mode             |
| RESET     | 0x06   | Reset device                   |
| START     | 0x08   | Start conversion               |
| STOP      | 0x0A   | Stop conversion                |
| RDATAC    | 0x10   | Read data continuous mode      |
| SDATAC    | 0x11   | Stop read data continuous mode |
| RDATA     | 0x12   | Read single data               |
| RREG      | 0x20+r | Read register (r = address)    |
| WREG      | 0x40+r | Write register (r = address)   |

---

## EMG-Specific Considerations

1. **Sample Rate**: EMG signals typically have frequency content up to 500Hz, so minimum 1kSPS is recommended. For high-fidelity capture, use 2-8 kSPS.

2. **Gain Selection**: EMG amplitudes range from ~50µV to 5mV. With 2.4V reference:
   - Gain=6: Full-scale input = ±400mV
   - Gain=12: Full-scale input = ±200mV
   
3. **Noise Performance**: At 500 SPS with gain=6, input-referred noise is ~3.5µVpp - suitable for EMG.

4. **Input Impedance**: The ADS1298 has very high input impedance (>1GΩ without lead-off), making it suitable for dry electrode EMG.

5. **RLD (Right Leg Drive)**: For EMG, this can be used as a driven ground reference to improve CMRR. Configure via RLD_SENSP/RLD_SENSN registers if needed.