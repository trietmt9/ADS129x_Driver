/*
 * emg_stream.h - Zephyr transmit path for the EMG wire protocol.
 *
 * Splits cleanly from emg_frame.c: that file is pure C99 (and is compiled by
 * the host test suite to cross-validate the protocol), while everything here
 * depends on Zephyr - the UART device, its TX interrupt, and a ring buffer.
 *
 * The acquisition loop must never block on the link. At 921600 baud a 404-byte
 * frame takes 4.4 ms to clock out, and the sample period at 1000 SPS is 1 ms,
 * so a synchronous write would cost about four samples per frame. Instead
 * emg_stream_send_*() copies the frame into a ring buffer and enables the UART
 * TX interrupt; the ISR drains it. The send call costs a memcpy.
 *
 * Back-pressure is deliberate and visible: if the ring is full the frame is
 * dropped and the sequence number still advances, so the host reports it as a
 * gap rather than silently receiving corrupt or stale data.
 */
#ifndef EMG_STREAM_H_
#define EMG_STREAM_H_

#include "emg_frame.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration rather than including <zephyr/device.h>: this header is
 * pulled in beside emg_frame.h, and keeping the Zephyr include out of it means
 * a caller only pays for what it uses. */
struct device;

/**
 * @brief Bind to the streaming UART and arm the TX path.
 * @param uart_dev Zephyr UART device; typically DEVICE_DT_GET(DT_CHOSEN(zephyr_console)).
 * @return 0 on success, negative errno otherwise.
 */
int emg_stream_init(const struct device *uart_dev);

/**
 * @brief Queue a DATA frame built from a channel-major sample block.
 *
 * Arguments match emg_frame_build_data(). The sequence number is maintained
 * internally and advances even when a frame is dropped, which is what lets the
 * host distinguish loss from a quiet link.
 *
 * @return 0 if queued, -ENOMEM if the ring was full (frame dropped).
 */
int emg_stream_send_data(uint32_t t_ms, uint8_t flags, const int32_t *block,
			 size_t stride, uint8_t n_ch, uint8_t n_samp,
			 uint8_t ch_mask);

/**
 * @brief Queue an INFO frame.
 * @return 0 if queued, -ENOMEM if the ring was full.
 */
int emg_stream_send_info(const struct emg_info *info);

/**
 * @brief Queue raw bytes on the stream UART, ahead of nothing and behind
 *        whatever is already queued.
 *
 * This is how console log text reaches the wire. The frame magic is non-ASCII,
 * so text and binary frames are safe to interleave BETWEEN frames (see the
 * emg_frame.h header) - but only if something serialises them. Two independent
 * writers on one UART do not: Zephyr's console backend busy-writes with
 * uart_poll_out() whenever TXE is set, while emg_uart_isr() writes the same
 * data register from the TX interrupt, so a log line emitted mid-frame lands
 * INSIDE it. The frame then fails CRC on the host and a whole block is lost.
 *
 * Routing log output through this ring puts both on one producer path, so text
 * can only ever land on a frame boundary.
 *
 * All-or-nothing, like the frame path: a half-written log line is worse than a
 * missing one.
 *
 * @return 0 if queued, -ENOMEM if the ring was full, -ENODEV before
 *         emg_stream_init().
 */
int emg_stream_send_raw(const uint8_t *data, size_t len);

/** @brief True once emg_stream_init() has bound a UART. */
bool emg_stream_ready(void);

/** @brief Frames dropped because the TX ring was full. */
uint32_t emg_stream_dropped(void);

#ifdef __cplusplus
}
#endif

#endif /* EMG_STREAM_H_ */
