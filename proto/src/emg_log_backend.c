/*
 * emg_log_backend.c - Zephyr log backend that shares the stream UART safely.
 *
 * THE PROBLEM THIS SOLVES. The console and the binary sample stream deliberately
 * share one UART: the frame magic 0xAA 0x55 is outside ASCII, so a host can
 * demultiplex text from frames with no escaping and no second port (emg_frame.h,
 * ARCHITECTURE.md 4.2). That reasoning is sound about the *bytes* and silent
 * about the *writers*. Zephyr's stock UART backend calls uart_poll_out(), which
 * spins on TXE and writes the data register directly; emg_stream's TX interrupt
 * writes the same register from emg_uart_isr(). Nothing serialises the two, so a
 * log line emitted while the ISR is draining a frame is interleaved INTO that
 * frame. The host then fails its CRC and drops the whole 32 ms block.
 *
 * Fixing it does not need a protocol change - only one producer. This backend
 * formats exactly what the UART backend would have, then hands the bytes to
 * emg_stream_send_raw() so they queue behind whatever frame is in flight and can
 * only ever land on a frame boundary. Plain terminals still see readable text,
 * the host's unframed-ASCII passthrough still works, and no host-side code
 * changes.
 *
 * Before emg_stream_init() binds the UART there is no frame traffic to corrupt,
 * so early boot output falls back to uart_poll_out() and nothing is lost.
 */
#include <emg_stream.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_core.h>
#include <zephyr/logging/log_output.h>

static const struct device *const emg_log_uart =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/*
 * Chunk size handed to the ring per call. log_output calls out() repeatedly for
 * one message, so this only bounds the granularity of the all-or-nothing queue
 * attempt, not the length of a log line.
 */
#define EMG_LOG_CHUNK 64

static uint8_t emg_log_buf[EMG_LOG_CHUNK];

/* Log bytes discarded because the TX ring was full. Deliberately separate from
 * emg_stream_dropped(): losing a log line and losing a block of samples are not
 * the same event and should not share a counter. */
static uint32_t emg_log_dropped;

static int emg_log_out(uint8_t *data, size_t length, void *ctx)
{
	ARG_UNUSED(ctx);

	if (!emg_stream_ready()) {
		/* Pre-init: no frames exist yet, so writing directly is safe and
		 * is the only way not to lose the boot banner. */
		for (size_t i = 0; i < length; i++) {
			uart_poll_out(emg_log_uart, data[i]);
		}
		return (int)length;
	}

	if (emg_stream_send_raw(data, length) < 0) {
		emg_log_dropped++;
	}

	/* Report the bytes as consumed either way. Returning short makes
	 * log_output retry the same bytes forever, which would stall the log
	 * thread on a full ring rather than dropping one line. */
	return (int)length;
}

LOG_OUTPUT_DEFINE(emg_log_output, emg_log_out, emg_log_buf, sizeof(emg_log_buf));

static void emg_log_process(const struct log_backend *const backend,
			    union log_msg_generic *msg)
{
	ARG_UNUSED(backend);

	/* Same decoration the stock UART backend applies, so switching to this
	 * one does not change what a log line looks like. */
	uint32_t flags = LOG_OUTPUT_FLAG_LEVEL | LOG_OUTPUT_FLAG_TIMESTAMP |
			 LOG_OUTPUT_FLAG_FORMAT_TIMESTAMP;

	if (IS_ENABLED(CONFIG_LOG_BACKEND_SHOW_COLOR)) {
		flags |= LOG_OUTPUT_FLAG_COLORS;
	}

	log_output_msg_process(&emg_log_output, &msg->log, flags);
}

static void emg_log_init(const struct log_backend *const backend)
{
	ARG_UNUSED(backend);
	emg_log_dropped = 0u;
}

static void emg_log_panic(const struct log_backend *const backend)
{
	ARG_UNUSED(backend);

	/* On panic the ring will never be drained - the ISR is not going to run
	 * again - so bypass it entirely and write the final message out by
	 * polling. Corrupting a frame no longer matters at this point. */
	log_output_flush(&emg_log_output);
}

static void emg_log_dropped_cb(const struct log_backend *const backend,
			       uint32_t cnt)
{
	ARG_UNUSED(backend);
	log_output_dropped_process(&emg_log_output, cnt);
}

static const struct log_backend_api emg_log_backend_api = {
	.process    = emg_log_process,
	.panic      = emg_log_panic,
	.init       = emg_log_init,
	.dropped    = emg_log_dropped_cb,
	.format_set = NULL,
};

LOG_BACKEND_DEFINE(emg_log_backend, emg_log_backend_api, true);

uint32_t emg_log_backend_dropped(void)
{
	return emg_log_dropped;
}
