/*
 * emg_frame.h - FES_Board EMG streaming wire protocol.
 *
 * Normative spec: software/ARCHITECTURE.md section 4. This header and its .c
 * are the firmware half; software/emg-viewer/src/core/FrameParser is the host
 * half. CHANGE BOTH TOGETHER - the host test suite links this file directly
 * so a divergence shows up as a failing test rather than a silent field bug.
 *
 * Deliberately free of Zephyr dependencies (plain C99, stdint/string only) so
 * it builds unmodified in the host unit tests. The transmit path - k_msgq,
 * uart_tx, threads - lives in emg_stream.c, which does depend on Zephyr.
 *
 * Frame layout, all multi-byte header fields little-endian:
 *
 *   off  sz  field
 *   0    2   magic  0xAA 0x55
 *   2    1   type   0x01 DATA | 0x02 INFO | 0x03 TEXT
 *   3    1   ver    0x01
 *   4    2   len    payload length, uint16 LE
 *   6    N   payload
 *   6+N  2   crc16  CRC-16/CCITT-FALSE over bytes [2 .. 6+N-1], uint16 LE
 *
 * The magic bytes are both outside the ASCII range, so Zephyr LOG_* text can
 * never contain the sync pattern. That is what lets binary frames and the
 * ordinary console log share one UART with no escaping and no second port.
 */
#ifndef EMG_FRAME_H_
#define EMG_FRAME_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- framing */

#define EMG_FRAME_MAGIC0        0xAAu
#define EMG_FRAME_MAGIC1        0x55u
#define EMG_FRAME_VERSION       0x01u

#define EMG_FRAME_TYPE_DATA     0x01u
#define EMG_FRAME_TYPE_INFO     0x02u
#define EMG_FRAME_TYPE_TEXT     0x03u

#define EMG_FRAME_HEADER_SIZE   6u   /* magic(2) type(1) ver(1) len(2)       */
#define EMG_FRAME_CRC_SIZE      2u
#define EMG_FRAME_OVERHEAD      (EMG_FRAME_HEADER_SIZE + EMG_FRAME_CRC_SIZE)

/* Sanity bounds the parser uses to reject a false sync before checking CRC. */
#define EMG_MAX_CHANNELS        8u
#define EMG_MAX_SAMPLES         64u
#define EMG_MAX_PAYLOAD         1024u

/* ------------------------------------------------------------- DATA frame */

#define EMG_DATA_HDR_SIZE       12u  /* seq, t_ms, n_ch, n_samp, flags, ch_mask */
#define EMG_BYTES_PER_SAMPLE    3u   /* 24-bit, big-endian, two's complement */

#define EMG_FLAG_OVERFLOW       (1u << 0)  /* acquisition ring overflowed    */
#define EMG_FLAG_LEADOFF        (1u << 1)  /* lead-off detected (reserved)   */

/*
 * ch_mask - which ADS129x channels the n_ch streamed channels actually are.
 * Bit 0 = CH1 ... bit 7 = CH8, and the payload carries the set bits in
 * ascending channel order.
 *
 * This byte was reserved-zero through protocol version 1, so zero keeps its
 * old meaning: the stream is CH1..CH(n_ch), contiguous from one. That is what
 * lets a firmware that streams a non-contiguous subset (CH2 and CH3, the only
 * two bipolar leads on the ADS1298ECG-FE) travel on the same version-1 wire
 * while the host still labels the traces correctly. Without it the host counts
 * from CH1 and calls Lead I "V6".
 */
#define EMG_CH_MASK_CONTIGUOUS  0x00u
#define EMG_CH_MASK_BIT(ch)     (1u << ((ch) - 1u))   /* ch is 1-based */

/* Worst-case frame, for sizing static transmit buffers. */
#define EMG_FRAME_MAX_SIZE                                                     \
	(EMG_FRAME_OVERHEAD + EMG_DATA_HDR_SIZE +                              \
	 (EMG_MAX_CHANNELS * EMG_MAX_SAMPLES * EMG_BYTES_PER_SAMPLE))

/* Exact size of a DATA frame carrying n_ch x n_samp samples. */
#define EMG_DATA_FRAME_SIZE(n_ch, n_samp)                                      \
	(EMG_FRAME_OVERHEAD + EMG_DATA_HDR_SIZE +                              \
	 ((size_t)(n_ch) * (size_t)(n_samp) * EMG_BYTES_PER_SAMPLE))

/* ------------------------------------------------------------- INFO frame */

#define EMG_INFO_PAYLOAD_SIZE   22u
#define EMG_INFO_FRAME_SIZE     (EMG_FRAME_OVERHEAD + EMG_INFO_PAYLOAD_SIZE)
#define EMG_FW_VERSION_LEN      8u

/*
 * Sent at boot and once per second so a host that attaches mid-stream
 * configures itself within a second, and so the microvolt scale always travels
 * with the data instead of being hardcoded on the host. That matters here:
 * firmware/docs/WORKFLOW.md documents gain 12 while src/main.c uses gain 6.
 */
struct emg_info {
	uint16_t sample_rate_hz;              /* 1000                          */
	uint32_t vref_uv;                     /* 2400000                       */
	uint8_t  gain;                        /* NUMERIC gain (6), not the enum */
	uint8_t  chip_id;                     /* 0x92 ADS1298, 0xD2 ADS1298R   */
	uint8_t  n_ch_active;                 /* 4 - the PCB only wires 4      */
	uint8_t  hr_mode;                     /* 1 = high-resolution           */
	uint32_t uptime_s;
	char     fw_version[EMG_FW_VERSION_LEN]; /* git short hash, NUL-padded */
};

/* ------------------------------------------------------------------- API */

/**
 * @brief CRC-16/CCITT-FALSE.
 *
 * poly 0x1021, init 0xFFFF, no input/output reflection, final XOR 0x0000.
 * Check value for the ASCII string "123456789" is 0x29B1.
 */
uint16_t emg_crc16(const uint8_t *data, size_t len);

/**
 * @brief Build a DATA frame from a channel-major sample block.
 *
 * @param out      destination, at least EMG_DATA_FRAME_SIZE(n_ch, n_samp)
 * @param out_size capacity of @p out
 * @param seq      frame counter; the host detects loss from gaps in it
 * @param t_ms     device uptime in ms at block close
 * @param flags    EMG_FLAG_*
 * @param block    samples as block[ch * stride + sample], sign-extended int32
 * @param stride   element stride between channels (DSP_BLOCK_SIZE)
 * @param n_ch     channels to emit, 1..EMG_MAX_CHANNELS
 * @param n_samp   samples per channel, 1..EMG_MAX_SAMPLES
 * @param ch_mask  EMG_CH_MASK_BIT() of each ADS channel present, in ascending
 *                 order, or EMG_CH_MASK_CONTIGUOUS (0) for plain CH1..CH(n_ch).
 *                 A non-zero mask must have exactly n_ch bits set.
 *
 * The block is channel-major (that is how struct dsp_double_buffer stores it)
 * but the wire is sample-major, so a truncated frame still yields whole
 * samples and the order matches what the ADS produces. This function does that
 * transpose while packing to 24-bit big-endian.
 *
 * @return frame length in bytes, or negative on bad arguments.
 */
int emg_frame_build_data(uint8_t *out, size_t out_size,
			 uint32_t seq, uint32_t t_ms, uint8_t flags,
			 const int32_t *block, size_t stride,
			 uint8_t n_ch, uint8_t n_samp, uint8_t ch_mask);

/**
 * @brief Build an INFO frame.
 * @return frame length (EMG_INFO_FRAME_SIZE), or negative on bad arguments.
 */
int emg_frame_build_info(uint8_t *out, size_t out_size,
			 const struct emg_info *info);

/**
 * @brief Build a TEXT frame wrapping @p len bytes of log output.
 * @return frame length, or negative on bad arguments.
 */
int emg_frame_build_text(uint8_t *out, size_t out_size,
			 const char *text, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* EMG_FRAME_H_ */
