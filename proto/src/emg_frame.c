/*
 * emg_frame.c - FES_Board EMG streaming wire protocol, encoder side.
 *
 * Plain C99, no Zephyr headers: the host unit tests compile this file directly
 * and decode its output with the Qt-side FrameParser, so the two halves of the
 * protocol cannot drift apart unnoticed.
 */
#include "emg_frame.h"

#include <string.h>

/* --------------------------------------------------------------------- CRC */

/*
 * CRC-16/CCITT-FALSE, bitwise. 8 iterations per byte on a 404-byte frame is
 * ~3200 shift/xor pairs, a few microseconds on a 216 MHz M7 against a 32 ms
 * frame period - not worth a 512-byte table in flash.
 */
uint16_t emg_crc16(const uint8_t *data, size_t len)
{
	uint16_t crc = 0xFFFFu;

	if (data == NULL) {
		return crc;
	}

	for (size_t i = 0; i < len; i++) {
		crc ^= (uint16_t)data[i] << 8;
		for (uint8_t bit = 0; bit < 8; bit++) {
			if (crc & 0x8000u) {
				crc = (uint16_t)((crc << 1) ^ 0x1021u);
			} else {
				crc = (uint16_t)(crc << 1);
			}
		}
	}
	return crc;
}

/* ----------------------------------------------------------------- helpers */

static void put_u16le(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v & 0xFFu);
	p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void put_u32le(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v & 0xFFu);
	p[1] = (uint8_t)((v >> 8) & 0xFFu);
	p[2] = (uint8_t)((v >> 16) & 0xFFu);
	p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

/*
 * Write the fixed header and seal the frame with its CRC.
 *
 * The CRC spans bytes [2 .. 6+payload_len-1], i.e. it covers type/ver/len as
 * well as the payload. Covering the length field is the point: a corrupted
 * `len` would otherwise be accepted and resynchronise the parser onto garbage.
 *
 * @return total frame length.
 */
static int seal(uint8_t *out, uint8_t type, size_t payload_len)
{
	out[0] = EMG_FRAME_MAGIC0;
	out[1] = EMG_FRAME_MAGIC1;
	out[2] = type;
	out[3] = EMG_FRAME_VERSION;
	put_u16le(&out[4], (uint16_t)payload_len);

	uint16_t crc = emg_crc16(&out[2], (EMG_FRAME_HEADER_SIZE - 2u) + payload_len);
	put_u16le(&out[EMG_FRAME_HEADER_SIZE + payload_len], crc);

	return (int)(EMG_FRAME_OVERHEAD + payload_len);
}

/* -------------------------------------------------------------- DATA frame */

int emg_frame_build_data(uint8_t *out, size_t out_size,
			 uint32_t seq, uint32_t t_ms, uint8_t flags,
			 const int32_t *block, size_t stride,
			 uint8_t n_ch, uint8_t n_samp, uint8_t ch_mask)
{
	if (out == NULL || block == NULL) {
		return -1;
	}
	if (n_ch == 0u || n_ch > EMG_MAX_CHANNELS ||
	    n_samp == 0u || n_samp > EMG_MAX_SAMPLES) {
		return -1;
	}
	if (stride < (size_t)n_samp) {
		return -1;
	}
	/* A mask that disagrees with n_ch would silently mislabel every trace on
	 * the host, so reject it here rather than emit it. */
	if (ch_mask != EMG_CH_MASK_CONTIGUOUS) {
		uint8_t bits = 0u;

		for (uint8_t b = 0u; b < 8u; b++) {
			if (ch_mask & (1u << b)) {
				bits++;
			}
		}
		if (bits != n_ch) {
			return -1;
		}
	}

	const size_t payload_len =
		EMG_DATA_HDR_SIZE +
		((size_t)n_ch * (size_t)n_samp * EMG_BYTES_PER_SAMPLE);

	if (out_size < EMG_FRAME_OVERHEAD + payload_len) {
		return -1;
	}

	uint8_t *p = &out[EMG_FRAME_HEADER_SIZE];

	put_u32le(&p[0], seq);
	put_u32le(&p[4], t_ms);
	p[8]  = n_ch;
	p[9]  = n_samp;
	p[10] = flags;
	p[11] = ch_mask;                       /* 0 = CH1..CH(n_ch) contiguous */

	/*
	 * Transpose channel-major -> sample-major while packing to 24-bit
	 * big-endian. Only the low 24 bits are emitted; the ADS1298 produces
	 * 24-bit two's complement and the host sign-extends bit 23 back out,
	 * mirroring ads_emg_read() in drivers/src/ads129x.c.
	 */
	uint8_t *s = &p[EMG_DATA_HDR_SIZE];
	for (uint8_t samp = 0u; samp < n_samp; samp++) {
		for (uint8_t ch = 0u; ch < n_ch; ch++) {
			const uint32_t v =
				(uint32_t)block[(size_t)ch * stride + samp];

			*s++ = (uint8_t)((v >> 16) & 0xFFu);
			*s++ = (uint8_t)((v >> 8) & 0xFFu);
			*s++ = (uint8_t)(v & 0xFFu);
		}
	}

	return seal(out, EMG_FRAME_TYPE_DATA, payload_len);
}

/* -------------------------------------------------------------- INFO frame */

int emg_frame_build_info(uint8_t *out, size_t out_size,
			 const struct emg_info *info)
{
	if (out == NULL || info == NULL) {
		return -1;
	}
	if (out_size < EMG_INFO_FRAME_SIZE) {
		return -1;
	}

	uint8_t *p = &out[EMG_FRAME_HEADER_SIZE];

	put_u16le(&p[0], info->sample_rate_hz);
	put_u32le(&p[2], info->vref_uv);
	p[6] = info->gain;
	p[7] = info->chip_id;
	p[8] = info->n_ch_active;
	p[9] = info->hr_mode;
	put_u32le(&p[10], info->uptime_s);
	memcpy(&p[14], info->fw_version, EMG_FW_VERSION_LEN);

	return seal(out, EMG_FRAME_TYPE_INFO, EMG_INFO_PAYLOAD_SIZE);
}

/* -------------------------------------------------------------- TEXT frame */

int emg_frame_build_text(uint8_t *out, size_t out_size,
			 const char *text, size_t len)
{
	if (out == NULL || text == NULL) {
		return -1;
	}
	if (len > EMG_MAX_PAYLOAD) {
		return -1;
	}
	if (out_size < EMG_FRAME_OVERHEAD + len) {
		return -1;
	}

	memcpy(&out[EMG_FRAME_HEADER_SIZE], text, len);

	return seal(out, EMG_FRAME_TYPE_TEXT, len);
}
