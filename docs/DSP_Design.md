# DSP Library Design Document

## Overview

A general-purpose, embedded-friendly Digital Signal Processing (DSP) library designed for real-time biopotential signal processing (EMG, ECG, EEG) on ARM Cortex-M microcontrollers.

## Design Goals

| Goal | Description |
|------|-------------|
| Portability | No hardware dependencies, pure C |
| Efficiency | Fixed-point math (Q15/Q31), no malloc |
| Modularity | Each filter/function is independent |
| Reusability | Generic API for any signal type |
| Real-time | Deterministic execution time |

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        Application Layer                        │
│                   (emg_dsp.c, ecg_dsp.c, etc.)                  │
└─────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                         DSP Library                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐             │
│  │ dsp_filter  │  │dsp_transform│  │ dsp_buffer  │             │
│  │             │  │             │  │             │             │
│  │ - Biquad    │  │ - RMS       │  │ - Ring buf  │             │
│  │ - FIR       │  │ - Mean      │  │ - Double buf│             │
│  │ - Notch     │  │ - Envelope  │  │             │             │
│  │ - DC Block  │  │ - Stats     │  │             │             │
│  └─────────────┘  └─────────────┘  └─────────────┘             │
│                         │                                       │
│                         ▼                                       │
│               ┌─────────────────┐                               │
│               │   dsp_common    │                               │
│               │                 │                               │
│               │ - Q15/Q31 types │                               │
│               │ - Fixed-point   │                               │
│               │ - Error codes   │                               │
│               └─────────────────┘                               │
└─────────────────────────────────────────────────────────────────┘
```

---

## File Structure

```
dsp/
├── inc/
│   ├── dsp_common.h       # Types, Q-format macros, error codes
│   ├── dsp_filter.h       # Filter structures and API
│   ├── dsp_transform.h    # Math operations (RMS, stats, etc.)
│   └── dsp_buffer.h       # Buffer management
└── src/
    ├── dsp_filter.c       # Filter implementations
    ├── dsp_transform.c    # Transform implementations
    └── dsp_buffer.c       # Buffer implementations
```

---

## Fixed-Point Arithmetic

### Why Fixed-Point?

| Aspect | Float | Fixed-Point (Q31) |
|--------|-------|-------------------|
| Cortex-M4F multiply | 1 cycle (FPU) | 1 cycle |
| Cortex-M0/M3 multiply | 50+ cycles (soft) | 1 cycle |
| Memory | 4 bytes | 4 bytes |
| Determinism | Variable | Constant |
| Precision | ~7 digits | ~9 digits |

### Q Format

**Q15**: 1 sign bit + 15 fractional bits
- Range: [-1.0, 0.999969]
- Resolution: 2^-15 = 0.0000305

**Q31**: 1 sign bit + 31 fractional bits
- Range: [-1.0, 0.9999999995]
- Resolution: 2^-31 = 4.66e-10

### Conversion Macros

```c
/* Float to Q-format */
#define FLOAT_TO_Q15(x) ((q15_t)((x) * 32768.0f))
#define FLOAT_TO_Q31(x) ((q31_t)((x) * 2147483648.0f))

/* Q-format to Float */
#define Q15_TO_FLOAT(x) ((float)(x) / 32768.0f)
#define Q31_TO_FLOAT(x) ((float)(x) / 2147483648.0f)
```

### Multiplication

```c
/* Q15 × Q15 = Q30, shift right 15 to get Q15 */
static inline q15_t q15_mul(q15_t a, q15_t b)
{
    return (q15_t)(((int32_t)a * b) >> 15);
}

/* Q31 × Q31 = Q62, shift right 31 to get Q31 */
static inline q31_t q31_mul(q31_t a, q31_t b)
{
    return (q31_t)(((int64_t)a * b) >> 31);
}
```

---

## Filter Implementations

### Biquad (IIR Second Order Section)

The building block for most IIR filters (Butterworth, Chebyshev, Notch, etc.)

**Transfer Function:**
```
        b0 + b1·z⁻¹ + b2·z⁻²
H(z) = ────────────────────────
        1 + a1·z⁻¹ + a2·z⁻²
```

**Difference Equation (Direct Form I):**
```
y[n] = b0·x[n] + b1·x[n-1] + b2·x[n-2] - a1·y[n-1] - a2·y[n-2]
```

**Structure:**
```c
struct dsp_biquad_coeff {
    q31_t b0, b1, b2;   /* Numerator */
    q31_t a1, a2;       /* Denominator (a0 = 1, normalized) */
};

struct dsp_biquad_state {
    q31_t x[2];         /* x[n-1], x[n-2] */
    q31_t y[2];         /* y[n-1], y[n-2] */
};

struct dsp_biquad {
    struct dsp_biquad_coeff coeff;
    struct dsp_biquad_state state;
};
```

**API:**
```c
void dsp_biquad_init(struct dsp_biquad *f, const struct dsp_biquad_coeff *coeff);
void dsp_biquad_reset(struct dsp_biquad *f);
q31_t dsp_biquad_process(struct dsp_biquad *f, q31_t input);
void dsp_biquad_process_block(struct dsp_biquad *f, q31_t *data, uint16_t len);
```

### Cascaded IIR (Higher Order)

For filters > 2nd order, cascade multiple biquad sections:

```
Input → [Biquad 1] → [Biquad 2] → ... → [Biquad N] → Output

4th order Butterworth = 2 biquad sections
6th order Butterworth = 3 biquad sections
```

**Structure:**
```c
#define DSP_BIQUAD_MAX_STAGES 4

struct dsp_iir_cascade {
    struct dsp_biquad stages[DSP_BIQUAD_MAX_STAGES];
    uint8_t num_stages;
};
```

### DC Blocker (High-Pass, Single Pole)

Removes DC offset with minimal computation.

**Difference Equation:**
```
y[n] = x[n] - x[n-1] + α·y[n-1]

α = 0.995 typical (corner freq ≈ 0.8 Hz @ 1kHz Fs)
```

**Structure:**
```c
struct dsp_dc_block {
    q31_t prev_input;
    q31_t prev_output;
    q31_t alpha;
};
```

### Notch Filter

Removes specific frequency (e.g., 50/60 Hz power line interference).

**Design Parameters:**
- f0: Center frequency to remove
- Q: Quality factor (higher = narrower notch)
- Fs: Sample rate

**Coefficient Calculation:**
```
ω0 = 2π × f0 / Fs
α = sin(ω0) / (2 × Q)

b0 = 1
b1 = -2 × cos(ω0)
b2 = 1
a0 = 1 + α
a1 = -2 × cos(ω0)
a2 = 1 - α

Normalize by dividing all coefficients by a0
```

### FIR Filter

Finite Impulse Response - always stable, linear phase.

**Difference Equation:**
```
y[n] = Σ(k=0 to N-1) h[k] · x[n-k]
```

**Structure:**
```c
struct dsp_fir {
    const q31_t *coeffs;    /* Filter coefficients h[k] */
    q31_t *state;           /* Circular delay line */
    uint16_t num_taps;      /* Number of taps (N) */
    uint16_t state_index;   /* Current position in circular buffer */
};
```

---

## Common Filter Coefficients

### EMG Bandpass (20-450 Hz @ 1kHz Fs)

```c
/* High-pass 20Hz, 2nd order Butterworth */
const struct dsp_biquad_coeff HPF_20HZ = {
    .b0 = FLOAT_TO_Q31(0.9565436765f),
    .b1 = FLOAT_TO_Q31(-1.9130873530f),
    .b2 = FLOAT_TO_Q31(0.9565436765f),
    .a1 = FLOAT_TO_Q31(-1.9111970674f),
    .a2 = FLOAT_TO_Q31(0.9149758348f)
};

/* Low-pass 450Hz, 2nd order Butterworth */
const struct dsp_biquad_coeff LPF_450HZ = {
    .b0 = FLOAT_TO_Q31(0.5765670062f),
    .b1 = FLOAT_TO_Q31(1.1531340124f),
    .b2 = FLOAT_TO_Q31(0.5765670062f),
    .a1 = FLOAT_TO_Q31(0.9428090416f),
    .a2 = FLOAT_TO_Q31(0.3634589832f)
};
```

### Notch Filters (50/60 Hz)

```c
/* Notch 50Hz @ 1kHz Fs, Q=30 */
const struct dsp_biquad_coeff NOTCH_50HZ = {
    .b0 = FLOAT_TO_Q31(0.9695312529f),
    .b1 = FLOAT_TO_Q31(-1.4095955f),
    .b2 = FLOAT_TO_Q31(0.9695312529f),
    .a1 = FLOAT_TO_Q31(-1.4095955f),
    .a2 = FLOAT_TO_Q31(0.9390625058f)
};

/* Notch 60Hz @ 1kHz Fs, Q=30 */
const struct dsp_biquad_coeff NOTCH_60HZ = {
    .b0 = FLOAT_TO_Q31(0.9695312529f),
    .b1 = FLOAT_TO_Q31(-1.2896975f),
    .b2 = FLOAT_TO_Q31(0.9695312529f),
    .a1 = FLOAT_TO_Q31(-1.2896975f),
    .a2 = FLOAT_TO_Q31(0.9390625058f)
};
```

---

## Transform Functions

### RMS (Root Mean Square)

```c
q31_t dsp_rms(const q31_t *data, uint16_t len)
{
    int64_t sum_sq = 0;

    for (uint16_t i = 0; i < len; i++) {
        sum_sq += ((int64_t)data[i] * data[i]) >> 31;
    }

    /* sqrt approximation or lookup table */
    return fast_sqrt_q31((q31_t)(sum_sq / len));
}
```

### Envelope Detection

```c
void dsp_envelope(q31_t *data, uint16_t len, q31_t attack, q31_t release)
{
    static q31_t envelope = 0;

    for (uint16_t i = 0; i < len; i++) {
        q31_t abs_val = (data[i] < 0) ? -data[i] : data[i];

        if (abs_val > envelope) {
            /* Attack: fast rise */
            envelope += q31_mul(attack, abs_val - envelope);
        } else {
            /* Release: slow decay */
            envelope += q31_mul(release, abs_val - envelope);
        }

        data[i] = envelope;
    }
}
```

### Statistics

```c
struct dsp_stats {
    q31_t min;
    q31_t max;
    q31_t mean;
    q31_t rms;
};

void dsp_stats(const q31_t *data, uint16_t len, struct dsp_stats *result);
```

---

## Buffer Management

### Double Buffer (Ping-Pong)

For real-time processing: fill one buffer while processing another.

```
Time ──────────────────────────────────────────────▶

Buffer A: [FILLING────────] [PROCESSING────] [FILLING────────]
Buffer B: [PROCESSING────] [FILLING────────] [PROCESSING────]
                          ↑                 ↑
                        Swap              Swap
```

**Structure:**
```c
struct dsp_double_buffer {
    q31_t buffer_a[DSP_BLOCK_SIZE];
    q31_t buffer_b[DSP_BLOCK_SIZE];
    q31_t *active;       /* Currently being filled */
    q31_t *processing;   /* Ready for DSP */
    uint16_t fill_index;
    bool ready;          /* Processing buffer has new data */
};
```

**API:**
```c
void dsp_dbuf_init(struct dsp_double_buffer *buf);
void dsp_dbuf_put(struct dsp_double_buffer *buf, q31_t sample);
bool dsp_dbuf_swap(struct dsp_double_buffer *buf);  /* Returns true if swap occurred */
```

---

## EMG Processing Pipeline

### Block Diagram

```
                    EMG Signal Processing Pipeline

┌─────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐
│ Raw ADC │──▶│ DC Block │──▶│ HPF 20Hz │──▶│ Notch    │──▶│ LPF 450Hz│
│ 24-bit  │   │          │   │          │   │ 50/60Hz  │   │          │
└─────────┘   └──────────┘   └──────────┘   └──────────┘   └──────────┘
                                                                 │
                                                                 ▼
┌─────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐
│ Output  │◀──│ RMS/Peak │◀──│ Smooth   │◀──│ Rectify  │◀──│ Filtered │
│ Metrics │   │          │   │ (LPF)    │   │ |x|      │   │ Signal   │
└─────────┘   └──────────┘   └──────────┘   └──────────┘   └──────────┘
```

### Implementation Example

```c
/* Filter instances */
static struct dsp_dc_block   dc_blocker;
static struct dsp_biquad     hpf_20hz;
static struct dsp_biquad     notch_60hz;
static struct dsp_biquad     lpf_450hz;
static struct dsp_biquad     envelope_lpf;

void emg_dsp_init(void)
{
    dsp_dc_block_init(&dc_blocker, 0.995f);
    dsp_biquad_init(&hpf_20hz, &HPF_20HZ);
    dsp_biquad_init(&notch_60hz, &NOTCH_60HZ);
    dsp_biquad_init(&lpf_450hz, &LPF_450HZ);
    dsp_biquad_init(&envelope_lpf, &LPF_10HZ);  /* Smoothing */
}

void emg_dsp_process(q31_t *data, uint16_t len, struct dsp_stats *result)
{
    /* 1. Remove DC offset */
    dsp_dc_block_process(&dc_blocker, data, len);

    /* 2. Bandpass filter (20-450 Hz) */
    dsp_biquad_process_block(&hpf_20hz, data, len);
    dsp_biquad_process_block(&lpf_450hz, data, len);

    /* 3. Remove power line interference */
    dsp_biquad_process_block(&notch_60hz, data, len);

    /* 4. Rectify (full-wave) */
    dsp_abs(data, len);

    /* 5. Smooth (envelope) */
    dsp_biquad_process_block(&envelope_lpf, data, len);

    /* 6. Calculate metrics */
    dsp_stats(data, len, result);
}
```

---

## Performance Considerations

### Execution Time (Cortex-M4 @ 168MHz)

| Operation | Cycles/Sample | Time @ 168MHz |
|-----------|---------------|---------------|
| Biquad (Q31) | ~20 | 0.12 μs |
| DC Block | ~10 | 0.06 μs |
| FIR (32 taps) | ~70 | 0.42 μs |
| RMS (64 samples) | ~200 | 1.2 μs |

### Memory Usage

| Structure | Size (bytes) |
|-----------|--------------|
| dsp_biquad | 36 |
| dsp_dc_block | 12 |
| dsp_fir (32 taps) | 136 + coeffs |
| dsp_double_buffer (64 samples) | 520 |

### Optimization Tips

1. **Use block processing** - process multiple samples per function call
2. **Cascade order matters** - put HPF before notch to reduce signal amplitude
3. **Avoid division** - use shifts or pre-computed reciprocals
4. **Loop unrolling** - process 4 samples per iteration
5. **CMSIS-DSP** - use ARM's optimized library when available

---

## Testing

### Unit Test Strategy

```c
/* Test with known input/output pairs */
void test_biquad_impulse_response(void)
{
    struct dsp_biquad filter;
    dsp_biquad_init(&filter, &LPF_450HZ);

    /* Impulse: [1, 0, 0, 0, ...] */
    q31_t impulse[64] = { Q31_MAX };
    dsp_biquad_process_block(&filter, impulse, 64);

    /* Verify impulse response matches expected */
    assert(impulse[0] == expected[0]);
    assert(impulse[1] == expected[1]);
    /* ... */
}

/* Test with sine wave */
void test_notch_attenuation(void)
{
    /* Generate 60Hz sine wave */
    /* Process through notch filter */
    /* Verify > 40dB attenuation at 60Hz */
}
```

### Frequency Response Verification

Use Python/MATLAB to verify filter coefficients:

```python
import numpy as np
from scipy import signal
import matplotlib.pyplot as plt

# Biquad coefficients (from C code)
b = [0.9695312529, -1.2896975, 0.9695312529]
a = [1.0, -1.2896975, 0.9390625058]

# Frequency response
w, h = signal.freqz(b, a, fs=1000)
plt.plot(w, 20*np.log10(np.abs(h)))
plt.title('Notch Filter 60Hz')
plt.xlabel('Frequency (Hz)')
plt.ylabel('Magnitude (dB)')
plt.grid()
plt.show()
```

---

## References

1. **Digital Signal Processing** - Oppenheim & Schafer
2. **The Scientist and Engineer's Guide to DSP** - Steven W. Smith (free online)
3. **ARM CMSIS-DSP Library** - https://arm-software.github.io/CMSIS-DSP/
4. **Filter Design Tool** - https://www.micromodeler.com/dsp/

---

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-02-03 | - | Initial design document |
