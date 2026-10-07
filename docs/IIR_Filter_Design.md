# IIR Butterworth Bandpass Filter Design
## Step-by-Step with Mathematics

---

## 1. What is an IIR Filter?

An **Infinite Impulse Response (IIR)** filter is a recursive digital filter whose output depends on:
- Current and past **input** samples
- Past **output** samples (feedback)

General difference equation:

$$y[n] = \frac{1}{a_0} \left( \sum_{k=0}^{M} b_k x[n-k] - \sum_{k=1}^{N} a_k y[n-k] \right)$$

Where:
- `x[n]` = current input sample
- `y[n]` = current output sample
- `b_k`  = feedforward (numerator) coefficients
- `a_k`  = feedback (denominator) coefficients
- `N`    = filter order

**Key difference from FIR:** The feedback term `a_k y[n-k]` creates an infinite impulse response using fewer coefficients — computationally efficient for embedded systems.

---

## 2. Why Butterworth?

The Butterworth filter is **maximally flat** in the passband — no ripple.

| Filter Type | Passband | Stopband | Phase |
|-------------|----------|----------|-------|
| Butterworth | Flat (no ripple) | Moderate rolloff | Moderate |
| Chebyshev I | Ripple | Steep rolloff | Poor |
| Chebyshev II | Flat | Ripple | Poor |
| Elliptic | Ripple | Steepest rolloff | Worst |

Butterworth magnitude response:

$$|H(j\Omega)|^2 = \frac{1}{1 + \left(\frac{\Omega}{\Omega_c}\right)^{2N}}$$

Where:
- `Ω_c` = cutoff frequency (rad/s)
- `N`   = filter order
- At `Ω = Ω_c`: magnitude is always `-3 dB` regardless of order

Higher `N` → steeper rolloff, but more phase distortion and computation cost.

---

## 3. Bandpass from Lowpass Prototype

### Step 3.1 — Start with Analog Lowpass Prototype

A Butterworth lowpass prototype of order `N` has poles equally spaced on a unit circle in the left-half s-plane:

$$s_k = e^{j\pi \frac{2k + N - 1}{2N}}, \quad k = 1, 2, ..., N$$

Transfer function:

$$H_{LP}(s) = \frac{1}{\prod_{k=1}^{N}(s - s_k)}$$

### Step 3.2 — Lowpass to Bandpass Transformation

Apply the **s-domain bandpass transformation**:

$$s \rightarrow \frac{s^2 + \omega_0^2}{B \cdot s}$$

Where:
- `ω_0 = √(ω_low × ω_high)` = geometric center frequency (rad/s)
- `B = ω_high - ω_low`       = bandwidth (rad/s)

This transforms each lowpass pole into **two bandpass poles**, doubling the order.
A lowpass prototype of order `N` → bandpass of order `2N`.

For our case:
- Lowpass order `N = 2` → Bandpass order `4` → **2 biquad stages**

### Step 3.3 — Analog to Digital (Bilinear Transform)

Convert the analog bandpass filter `H(s)` to digital `H(z)` using the **bilinear transform**:

$$s = \frac{2}{T_s} \cdot \frac{z - 1}{z + 1}$$

Where `T_s = 1/f_s` is the sampling period.

This maps:
- `s`-plane left half → inside unit circle in `z`-plane
- Entire `jΩ` axis → unit circle in `z`-plane

**Frequency warping correction** (pre-warping) must be applied to compensate for the nonlinear frequency compression of the bilinear transform:

$$\Omega_{analog} = \frac{2}{T_s} \tan\left(\frac{\omega_{digital}}{2}\right)$$

where `ω_digital = 2π f / f_s`

scipy's `butter()` handles pre-warping automatically.

---

## 4. Second-Order Sections (Biquad)

### Why Biquad?

A high-order filter implemented as a single transfer function suffers from:
- Numerical precision issues (coefficient rounding)
- Instability for order > 4

**Solution:** Factor the transfer function into cascaded **Second-Order Sections (SOS)**, each a 2nd-order filter:

$$H(z) = \prod_{k=1}^{K} H_k(z)$$

Each biquad section:

$$H_k(z) = \frac{b_{0k} + b_{1k}z^{-1} + b_{2k}z^{-2}}{1 + a_{1k}z^{-1} + a_{2k}z^{-2}}$$

For our 4th-order bandpass (N=2 prototype):
- `K = 2` stages (2 biquad sections)
- Each stage is 2nd order

### Difference Equation per Biquad

$$y[n] = b_0 x[n] + b_1 x[n-1] + b_2 x[n-2] - a_1 y[n-1] - a_2 y[n-2]$$

---

## 5. Direct Form II Implementation

### Why Direct Form II?

Direct Form I requires **4 delay elements** per biquad (2 for input, 2 for output).
Direct Form II requires only **2 delay elements** by sharing the delay line.

### Signal Flow

```
x[n] ──►──(+)──► w[n] ──► b0 ──►──(+)──► y[n]
           ▲                        ▲
          (-a1)◄──[z⁻¹]──► w[n-1] ──► b1
           ▲                        ▲
          (-a2)◄──[z⁻¹]──► w[n-2] ──► b2
```

### Equations

**Step 1** — compute intermediate state:

$$w[n] = x[n] - a_1 \cdot w[n-1] - a_2 \cdot w[n-2]$$

**Step 2** — compute output:

$$y[n] = b_0 \cdot w[n] + b_1 \cdot w[n-1] + b_2 \cdot w[n-2]$$

**Step 3** — shift state (update delay line):

$$w[n-2] \leftarrow w[n-1]$$
$$w[n-1] \leftarrow w[n]$$

### In C

```c
float w  = x - a1*w1 - a2*w2;
float y  = b0*w + b1*w1 + b2*w2;
w2 = w1;
w1 = w;
```

Only **2 state variables** (`w1`, `w2`) per biquad per channel.

---

## 6. Cascading for 4th Order Bandpass

Output of stage 0 feeds into stage 1:

```
x[n] ──► [ Biquad Stage 0 ] ──► [ Biquad Stage 1 ] ──► y[n]
              (H0(z))                  (H1(z))

H(z) = H0(z) × H1(z)
```

Each stage has **independent state** (`w1`, `w2`) but **shared coefficients** across all EMG channels (same filter applied to each channel independently).

---

## 7. Design with Python (scipy)

```python
from scipy.signal import butter, sosfreqz
import numpy as np
import matplotlib.pyplot as plt

# ── Parameters ──────────────────────────────────────────────────
fs     = 4000    # Sampling rate (Hz) — match ADS1298 config
f_low  = 20      # Low  cutoff (Hz) — remove motion artifact / DC
f_high = 500     # High cutoff (Hz) — remove HF noise
order  = 2       # Prototype order → 4th order bandpass → 2 stages

# ── Design ──────────────────────────────────────────────────────
sos = butter(order, [f_low, f_high], btype='bandpass', fs=fs, output='sos')

# ── Print C coefficients ─────────────────────────────────────────
print(f"Number of biquad stages: {len(sos)}\n")
for i, s in enumerate(sos):
    print(f"/* Stage {i} */")
    print(f"{{ .b0={s[0]:.8f}f, .b1={s[1]:.8f}f, .b2={s[2]:.8f}f,")
    print(f"  .a1={s[4]:.8f}f, .a2={s[5]:.8f}f }},\n")

# ── Verify frequency response ────────────────────────────────────
w, h = sosfreqz(sos, worN=4096, fs=fs)
plt.figure(figsize=(10, 4))
plt.plot(w, 20 * np.log10(np.abs(h) + 1e-12))
plt.axvline(f_low,  color='r', linestyle='--', label=f'Low  cutoff {f_low} Hz')
plt.axvline(f_high, color='g', linestyle='--', label=f'High cutoff {f_high} Hz')
plt.axhline(-3, color='gray', linestyle=':', label='-3 dB')
plt.xlabel('Frequency (Hz)')
plt.ylabel('Magnitude (dB)')
plt.title(f'Butterworth Bandpass {f_low}–{f_high} Hz, Order {order*2}, fs={fs} Hz')
plt.ylim(-80, 5)
plt.xlim(0, fs / 2)
plt.legend()
plt.grid(True)
plt.tight_layout()
plt.show()
```

---

## 8. Sample Rate vs ADS1298 Config

The coefficients **depend entirely on `fs`** — wrong `fs` gives wrong cutoff frequencies.

| ADS1298 Config              | Actual `fs`  | Use in Python   |
|-----------------------------|-------------|-----------------|
| HighResolution + fmod_div_16  | 32000 Hz   | `fs = 32000`   |
| HighResolution + fmod_div_32  | 16000 Hz   | `fs = 16000`   |
| HighResolution + fmod_div_64  |  8000 Hz   | `fs = 8000`    |
| HighResolution + fmod_div_128 |  4000 Hz   | `fs = 4000`    |
| LowPower + fmod_div_16        | 16000 Hz   | `fs = 16000`   |
| LowPower + fmod_div_128       |  2000 Hz   | `fs = 2000`    |

> **Current project config:** `HighResolution + fmod_div_16` → use `fs = 32000`

---

## 9. Coefficient Example (fs=4000 Hz, 20–500 Hz)

```c
static const struct iir_biquad_coeff bp_coeff[IIR_BP_STAGES] = {
    {   /* Stage 0 */
        .b0 =  0.05431812f,
        .b1 =  0.00000000f,
        .b2 = -0.05431812f,
        .a1 = -1.79595967f,
        .a2 =  0.89136376f
    },
    {   /* Stage 1 */
        .b0 =  0.05431812f,
        .b1 =  0.00000000f,
        .b2 = -0.05431812f,
        .a1 = -1.68062872f,
        .a2 =  0.76968676f
    }
};
```

> **Note:** `b1 = 0` always for a pure bandpass biquad — this is expected and correct.

---

## 10. Filter Characteristics Summary

| Parameter         | Value              |
|-------------------|--------------------|
| Filter type       | Butterworth        |
| Response          | Maximally flat passband |
| Topology          | Cascaded biquad (SOS) |
| Implementation    | Direct Form II     |
| Prototype order   | 2                  |
| Bandpass order    | 4 (2N)             |
| Biquad stages     | 2                  |
| State per channel | 4 floats (2 × 2)   |
| Passband          | 20 – 500 Hz        |
| Rolloff           | -40 dB/decade per stage |
| Ripple            | None               |

---

## 11. Stability Check

A biquad is stable if all poles are inside the unit circle. For Direct Form II, stability is guaranteed when:

$$|a_2| < 1$$
$$|a_1| < 1 + a_2$$

Always verify after computing coefficients:

```python
for i, s in enumerate(sos):
    a1, a2 = s[4], s[5]
    stable = (abs(a2) < 1) and (abs(a1) < 1 + a2)
    print(f"Stage {i}: a1={a1:.6f}, a2={a2:.6f} → {'STABLE' if stable else 'UNSTABLE'}")
```
