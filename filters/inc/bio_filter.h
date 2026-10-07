#ifndef __BIO_FILTER_H__
#define __BIO_FILTER_H__

#include <iir.h>

/*
 * Biopotential conditioning applied on the board: median, then a Butterworth
 * band-pass.
 *
 * The median runs first because a linear filter cannot remove an impulse, only
 * spread it. A one-sample spike through the low-pass alone comes back as a
 * rounded bump the width of the filter's impulse response, which for ECG is
 * close enough to a QRS to be counted as a beat.
 *
 * Typical bands:
 *   EMG  20-450 Hz   (SENIAM). Needs >= 2 kSPS: the ADS1298's own sinc^3
 *                    decimation is -3 dB at 0.262 x f_DR, so at 1 kSPS the
 *                    device rolls off at ~296 Hz and eats the top of the band.
 *   ECG  0.5-40 Hz   monitoring
 *   ECG  0.05-150 Hz diagnostic
 *
 * Filtering here is destructive - the host receives conditioned samples and
 * cannot recover what was removed, so recordings are no longer raw and the band
 * can only be changed by reflashing.
 */

#define BIO_MEDIAN_N 5

struct bio_filter {
	/* Median window. */
	float med[BIO_MEDIAN_N];
	uint8_t med_fill;
	uint8_t med_pos;

	/* Optional mains notch. Off unless bio_filter_set_notch() is called. */
	bool use_notch;
	struct IIR_coefficient notch_coef;
	struct IIR_state notch_state;

	/* Two biquads a side = 4th-order Butterworth, 24 dB/octave. */
	bool use_hp;
	struct IIR_coefficient hp1_coef, hp2_coef;
	struct IIR_coefficient lp1_coef, lp2_coef;
	struct IIR_state hp1_state, hp2_state;
	struct IIR_state lp1_state, lp2_state;
};

/**
 * @brief Design the filter for a sample rate and band.
 *
 * @param pF      Filter to initialise.
 * @param fs_hz   Sample rate. Use the MEASURED rate: both corners scale with
 *                it, and this board runs about 13 % fast.
 * @param hp_hz   High-pass corner, or 0 for none.
 * @param lp_hz   Low-pass corner. Must be below fs_hz / 2.
 * @return 0 on success, -EINVAL if a corner is out of range.
 */
int bio_filter_init(struct bio_filter *pF, float fs_hz, float hp_hz, float lp_hz);

/**
 * @brief Add a mains notch. Opt-in, and OFF after bio_filter_init().
 *
 * @warning For sEMG this costs real signal. The dominant energy of surface EMG
 * lies in the 50-150 Hz range, so a 50/60 Hz notch sits in the middle of the
 * band and removes muscle activity along with the interference. The literature
 * advises against it wherever the interference can be attacked at source.
 *
 * Attack it at source first:
 *   - Enable the RLD loop (RLD_SENSP/RLD_SENSN). With no channel routed to the
 *     RLD amplifier the common-mode feedback loop is OPEN and mains rejection
 *     comes only from the INA's own CMRR. This is worth far more than a notch.
 *   - Skin preparation, short and equal-length electrode leads, twisted pair.
 *
 * Reach for this only when the interference survives all of that, and treat the
 * result as unsuitable for EMG spectral analysis (median/mean frequency), which
 * a notch biases directly.
 *
 * Must be called AFTER bio_filter_init(), which clears it - including the
 * re-design that happens when the measured sample rate locks.
 *
 * @param pF     Filter to modify.
 * @param fs_hz  Sample rate, the same one passed to bio_filter_init().
 * @param f0_hz  Mains frequency (50 or 60), or 0 to disable.
 * @param q      Quality factor. Higher is narrower and takes less signal with
 *               it; 30 gives roughly a 2 Hz notch at 60 Hz.
 * @return 0 on success, -EINVAL if f0_hz is not below fs_hz / 2.
 */
int bio_filter_set_notch(struct bio_filter *pF, float fs_hz, float f0_hz,
			 float q);

/**
 * @brief Filter one sample: median, then high-pass, then low-pass.
 *
 * @param pF       Filter state.
 * @param input    Raw sample, in ADC codes or microvolts.
 * @param pOutput  Filtered sample.
 * @return 0 on success, negative errno on failure.
 */
int bio_filter_process(struct bio_filter *pF, float input, float *pOutput);

#endif
