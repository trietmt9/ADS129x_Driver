# SPDX-License-Identifier: Apache-2.0
#
# Flash/debug runners for the STM32F767ZI via the on-board SWD headers
# (J1 = STM32 target, TC2030-NL). Keep the first line first.

board_runner_args(stm32cubeprogrammer "--port=swd" "--reset-mode=hw")
board_runner_args(jlink "--device=STM32F767ZI" "--speed=4000")
board_runner_args(openocd "--tcl-port=6666")

include(${ZEPHYR_BASE}/boards/common/stm32cubeprogrammer.board.cmake)
include(${ZEPHYR_BASE}/boards/common/openocd-stm32.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
