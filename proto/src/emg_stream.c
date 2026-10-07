#include "emg_stream.h"

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

#include <errno.h>

/*
 * Ring depth. A DATA frame at 4 ch x 32 samples is 404 bytes and the link
 * drains 92 bytes/ms at 921600 baud, so 2 KB is roughly five frames of slack -
 * plenty to ride out a log burst or a scheduling hiccup without ever making the
 * acquisition loop wait.
 */
#define EMG_TX_RING_SIZE 2048

RING_BUF_DECLARE(emg_tx_ring, EMG_TX_RING_SIZE);

static const struct device *emg_uart;
static uint32_t emg_seq;
static uint32_t emg_dropped;

static void emg_uart_isr(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	if (!uart_irq_update(dev)) {
		return;
	}

	while (uart_irq_tx_ready(dev)) {
		uint8_t *data;
		uint32_t claimed = ring_buf_get_claim(&emg_tx_ring, &data,
						      EMG_TX_RING_SIZE);
		if (claimed == 0u) {
			/* Nothing left: stop asking for TX-empty interrupts,
			 * otherwise this fires continuously and starves the
			 * rest of the system. */
			uart_irq_tx_disable(dev);
			ring_buf_get_finish(&emg_tx_ring, 0);
			break;
		}

		const int sent = uart_fifo_fill(dev, data, (int)claimed);
		ring_buf_get_finish(&emg_tx_ring, sent > 0 ? (uint32_t)sent : 0u);

		if (sent <= 0) {
			break;   /* FIFO full; the next TX-ready IRQ resumes us */
		}
	}
}

int emg_stream_init(const struct device *uart_dev)
{
	if (uart_dev == NULL) {
		return -EINVAL;
	}
	if (!device_is_ready(uart_dev)) {
		return -ENODEV;
	}

	emg_uart = uart_dev;
	emg_seq = 0u;
	emg_dropped = 0u;
	ring_buf_reset(&emg_tx_ring);

	int ret = uart_irq_callback_user_data_set(uart_dev, emg_uart_isr, NULL);
	if (ret < 0) {
		return ret;
	}

	uart_irq_rx_disable(uart_dev);
	uart_irq_tx_disable(uart_dev);
	return 0;
}

/* Copy a built frame into the ring and kick the ISR.
 *
 * All-or-nothing: a partially queued frame would be indistinguishable from
 * corruption on the wire, so if it does not fit entirely we drop it. */
static int emg_stream_queue(const uint8_t *frame, uint32_t len)
{
	if (emg_uart == NULL) {
		return -ENODEV;
	}

	const int key = irq_lock();
	const uint32_t space = ring_buf_space_get(&emg_tx_ring);
	if (space < len) {
		irq_unlock(key);
		emg_dropped++;
		return -ENOMEM;
	}
	const uint32_t put = ring_buf_put(&emg_tx_ring, frame, len);
	irq_unlock(key);

	if (put != len) {
		emg_dropped++;
		return -ENOMEM;
	}

	uart_irq_tx_enable(emg_uart);
	return 0;
}

int emg_stream_send_data(uint32_t t_ms, uint8_t flags, const int32_t *block,
			 size_t stride, uint8_t n_ch, uint8_t n_samp,
			 uint8_t ch_mask)
{
	/* static, not automatic: EMG_FRAME_MAX_SIZE is sized for the worst case
	 * (8 ch x 64 samples = 1556 B) while a typical frame here is 212 B, and
	 * 1556 B of a 2048 B CONFIG_MAIN_STACK_SIZE is 76 % of the acquisition
	 * thread's stack in a single local. That left roughly 230 B of margin for
	 * everything below this call - too thin to rely on.
	 *
	 * Safe because emg_stream_send_data() is called from exactly one thread
	 * (the acquisition loop in main()) and emg_stream_queue() copies into the
	 * ring synchronously before returning. It is NOT reentrant - if a second
	 * caller is ever added, give it its own buffer. */
	static uint8_t frame[EMG_FRAME_MAX_SIZE];

	const int len = emg_frame_build_data(frame, sizeof(frame), emg_seq, t_ms,
					     flags, block, stride, n_ch, n_samp,
					     ch_mask);
	if (len < 0) {
		return -EINVAL;
	}

	/* Advance the sequence number even when the queue rejects the frame:
	 * the resulting gap is exactly how the host learns it lost data. */
	emg_seq++;

	return emg_stream_queue(frame, (uint32_t)len);
}

int emg_stream_send_info(const struct emg_info *info)
{
	uint8_t frame[EMG_INFO_FRAME_SIZE];

	const int len = emg_frame_build_info(frame, sizeof(frame), info);
	if (len < 0) {
		return -EINVAL;
	}
	return emg_stream_queue(frame, (uint32_t)len);
}

int emg_stream_send_raw(const uint8_t *data, size_t len)
{
	if (data == NULL) {
		return -EINVAL;
	}
	if (len == 0u) {
		return 0;
	}
	if (len > EMG_TX_RING_SIZE) {
		return -ENOMEM;
	}
	return emg_stream_queue(data, (uint32_t)len);
}

bool emg_stream_ready(void)
{
	return emg_uart != NULL;
}

uint32_t emg_stream_dropped(void)
{
	return emg_dropped;
}
