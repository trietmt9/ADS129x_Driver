# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Register this directory as a board root so Zephyr finds the custom
# board definition under boards/fes/fesboard/. Must precede find_package.
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(emg_reader)

# Read the ADS1298 and stream framed binary samples to the host.
#
# No build options: one behaviour, so there is nothing to get into a wrong
# combination of. examples/main_streaming.c keeps the older application.
#
# No custom log backend: main.c writes frames with uart_poll_out() from the
# acquisition thread, and the stock UART log backend uses the same call from
# the same thread, so the two serialise naturally.
#
# There IS a sample ring (Zephyr's ring_buf, declared in main.c). It decouples
# reading the ADS1298 from framing it, so a stall in transmission no longer
# costs conversions. It does NOT protect against a stall in the poll loop
# itself - the chip has no FIFO, so a conversion nobody reads is simply gone.
# That is why the status log is the remaining source of loss.
# emg_frame.c is kept - it builds the frames the host already knows how to
# decode. emg_stream.c and emg_log_backend.c stay on disk for
# examples/main_streaming.c, which needs interrupt-driven TX for 8 channels.
target_sources(app PRIVATE
    src/main.c
    bus/src/spi_bus.c
    drivers/src/ads129x.c
    proto/src/emg_frame.c
    filters/src/iir.c
    filters/src/bio_filter.c
)

# On-board conditioning: 5-point median + 4th-order Butterworth band-pass.
# The median is what removes impulsive artifacts - a low-pass alone turns a
# spike into a rounded bump, which for ECG is close enough to a QRS to be
# counted as a beat.
#
# Always built. Change the band by editing the corner frequencies at the top of
# src/main.c and reflashing; the host receives conditioned samples either way.

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/bus/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/drivers/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/filters/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/inc
)
