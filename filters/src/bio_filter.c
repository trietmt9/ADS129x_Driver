#include <bio_filter.h>

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Butterworth pole-pair Q values for a 4th-order response:
 * 1/(2 cos(pi/8)) and 1/(2 cos(3 pi/8)). */
#define BUTTER_Q1  0.54119610
#define BUTTER_Q2  1.30656296

/*
 * RBJ cookbook biquads, normalised so a0 = 1.
 *
 * Designed in double and stored as float. At 20 Hz against 4.5 kSPS the poles
 * sit very close to z = 1, and computing the coefficients in single precision
 * loses enough of cos(w0) to shift the corner measurably.
 *
 * The IIR_coefficient layout matches iir_bandpass()'s Direct Form II:
 *   w[n] = x[n] - a[0]*w[n-1] - a[1]*w[n-2]
 *   y[n] = b[0]*w[n] + b[1]*w[n-1] + b[2]*w[n-2]
 */
static void design_lowpass(struct IIR_coefficient *pC, double fs, double fc,
                           double q)
{
	const double w0 = 2.0 * M_PI * fc / fs;
	const double cw = cos(w0);
	const double alpha = sin(w0) / (2.0 * q);
	const double a0 = 1.0 + alpha;

	pC->b[0] = (float)(((1.0 - cw) * 0.5) / a0);
	pC->b[1] = (float)((1.0 - cw) / a0);
	pC->b[2] = pC->b[0];
	pC->a[0] = (float)((-2.0 * cw) / a0);
	pC->a[1] = (float)((1.0 - alpha) / a0);
}

/* RBJ cookbook notch (band-stop). Unity gain either side of f0, a null at it. */
static void design_notch(struct IIR_coefficient *pC, double fs, double f0,
                         double q)
{
	const double w0 = 2.0 * M_PI * f0 / fs;
	const double cw = cos(w0);
	const double alpha = sin(w0) / (2.0 * q);
	const double a0 = 1.0 + alpha;

	pC->b[0] = (float)(1.0 / a0);
	pC->b[1] = (float)((-2.0 * cw) / a0);
	pC->b[2] = pC->b[0];
	pC->a[0] = (float)((-2.0 * cw) / a0);
	pC->a[1] = (float)((1.0 - alpha) / a0);
}

static void design_highpass(struct IIR_coefficient *pC, double fs, double fc,
                            double q)
{
	const double w0 = 2.0 * M_PI * fc / fs;
	const double cw = cos(w0);
	const double alpha = sin(w0) / (2.0 * q);
	const double a0 = 1.0 + alpha;

	pC->b[0] = (float)(((1.0 + cw) * 0.5) / a0);
	pC->b[1] = (float)((-(1.0 + cw)) / a0);
	pC->b[2] = pC->b[0];
	pC->a[0] = (float)((-2.0 * cw) / a0);
	pC->a[1] = (float)((1.0 - alpha) / a0);
}

/** @brief Design the filter for a sample rate and band. See bio_filter.h. */
int bio_filter_init(struct bio_filter *pF, float fs_hz, float hp_hz, float lp_hz)
{
	/* STEP 1: validate. Above Nyquist the bilinear transform folds and the
	 * design is meaningless rather than merely inaccurate. */
	if (pF == NULL || fs_hz <= 0.0f || lp_hz <= 0.0f) {
		return -EINVAL;
	}
	if (lp_hz >= fs_hz * 0.5f || hp_hz >= lp_hz) {
		return -EINVAL;
	}

	/* STEP 2: clear the median window. */
	memset(pF->med, 0, sizeof(pF->med));
	pF->med_fill = 0u;
	pF->med_pos = 0u;

	/* STEP 3: design the high-pass, if one was asked for. */
	pF->use_hp = (hp_hz > 0.0f);
	if (pF->use_hp) {
		design_highpass(&pF->hp1_coef, fs_hz, hp_hz, BUTTER_Q1);
		design_highpass(&pF->hp2_coef, fs_hz, hp_hz, BUTTER_Q2);
	}

	/* STEP 4: design the low-pass. */
	design_lowpass(&pF->lp1_coef, fs_hz, lp_hz, BUTTER_Q1);
	design_lowpass(&pF->lp2_coef, fs_hz, lp_hz, BUTTER_Q2);

	/* STEP 5: the notch is opt-in - clear it, so a re-design at the measured
	 * rate cannot silently carry a stale one forward. */
	pF->use_notch = false;

	/* STEP 6: clear the delay lines. */
	iir_init(&pF->hp1_state);
	iir_init(&pF->hp2_state);
	iir_init(&pF->lp1_state);
	iir_init(&pF->lp2_state);

	return 0;
}

/**
 * @brief Median of the window.
 *
 * Five elements, so an insertion sort of a local copy is both the simplest and
 * the fastest option - at most ten comparisons.
 *
 * @param w  Window contents.
 * @param n  How many are valid.
 * @return The median value.
 */
static float median5(const float *w, uint8_t n)
{
	float s[BIO_MEDIAN_N];

	for (uint8_t i = 0; i < n; i++) {
		s[i] = w[i];
	}
	for (uint8_t i = 1; i < n; i++) {
		const float v = s[i];
		int8_t j = (int8_t)i - 1;

		while (j >= 0 && s[j] > v) {
			s[j + 1] = s[j];
			j--;
		}
		s[j + 1] = v;
	}
	return s[n / 2u];
}

/** @brief Filter one sample. See bio_filter.h. */
int bio_filter_set_notch(struct bio_filter *pF, float fs_hz, float f0_hz,
			 float q)
{
	/* STEP 1: reject what cannot be designed. */
	if (pF == NULL || fs_hz <= 0.0f) {
		return -EINVAL;
	}

	/* STEP 2: zero means disable, and is not an error. */
	if (f0_hz <= 0.0f) {
		pF->use_notch = false;
		return 0;
	}
	if (f0_hz >= fs_hz * 0.5f || q <= 0.0f) {
		return -EINVAL;
	}

	/* STEP 3: design it and clear the delay line. */
	design_notch(&pF->notch_coef, fs_hz, f0_hz, q);
	iir_init(&pF->notch_state);
	pF->use_notch = true;
	return 0;
}

int bio_filter_process(struct bio_filter *pF, float input, float *pOutput)
{
	if (pF == NULL || pOutput == NULL) {
		return -EINVAL;
	}

	/* STEP 1: median, ring-buffered so nothing shifts per sample. */
	pF->med[pF->med_pos] = input;
	pF->med_pos = (uint8_t)((pF->med_pos + 1u) % BIO_MEDIAN_N);
	if (pF->med_fill < BIO_MEDIAN_N) {
		pF->med_fill++;
	}

	float y = median5(pF->med, pF->med_fill);
	int ret;

	/* STEP 2: high-pass, if configured. */
	if (pF->use_hp) {
		ret = iir_bandpass(y, &pF->hp1_coef, &pF->hp1_state, &y);
		if (ret < 0) return ret;
		ret = iir_bandpass(y, &pF->hp2_coef, &pF->hp2_state, &y);
		if (ret < 0) return ret;
	}

	/* STEP 3: low-pass. */
	ret = iir_bandpass(y, &pF->lp1_coef, &pF->lp1_state, &y);
	if (ret < 0) return ret;

	ret = iir_bandpass(y, &pF->lp2_coef, &pF->lp2_state, &y);
	if (ret < 0) return ret;

	/* STEP 4: mains notch, if one was asked for. Last, so it acts on a signal
	 * the high-pass has already stripped of DC - a notch rings hard on a step. */
	if (pF->use_notch) {
		ret = iir_bandpass(y, &pF->notch_coef, &pF->notch_state, &y);
		if (ret < 0) return ret;
	}

	*pOutput = y;
	return 0;
}
