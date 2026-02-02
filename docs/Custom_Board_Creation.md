# Creating a Custom Zephyr Board for STM32F767ZI

> A step-by-step guide to create a custom board definition in Zephyr RTOS based on the STM32F767ZI microcontroller.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Prerequisites](#2-prerequisites)
3. [Board Directory Structure](#3-board-directory-structure)
4. [Step 1: Create Board Directory](#step-1-create-board-directory)
5. [Step 2: Create Board YAML File](#step-2-create-board-yaml-file)
6. [Step 3: Create Board Kconfig Files](#step-3-create-board-kconfig-files)
7. [Step 4: Create Board Defconfig](#step-4-create-board-defconfig)
8. [Step 5: Create Device Tree Source](#step-5-create-device-tree-source)
9. [Step 6: Create Pin Control Configuration](#step-6-create-pin-control-configuration)
10. [Step 7: Create Board Documentation](#step-7-create-board-documentation)
11. [Step 8: Build and Test](#step-8-build-and-test)
12. [Common Customizations](#common-customizations)
13. [Troubleshooting](#troubleshooting)
14. [Reference: STM32F767ZI Specifications](#reference-stm32f767zi-specifications)

---

## 1. Overview

### What is a Board Definition?

In Zephyr, a "board" is a combination of:
- **Hardware description** (Device Tree) - What peripherals exist and how they're connected
- **Default configuration** (Kconfig/defconfig) - What features are enabled by default
- **Pin mappings** (pinctrl) - How MCU pins are configured

### Why Create a Custom Board?

| Scenario | Solution |
|----------|----------|
| Using Nucleo-F767ZI as-is | Use existing `nucleo_f767zi` board |
| Custom PCB with STM32F767ZI | Create custom board (this guide) |
| Minor pin changes from Nucleo | Use overlay on existing board |
| Major differences from any existing board | Create custom board |

### Approach

We'll create a board by:
1. Referencing the STM32F767ZI SoC definition (already in Zephyr)
2. Defining our board-specific peripherals and pins
3. Setting up default configurations

---

## 2. Prerequisites

### Required Knowledge
- Basic understanding of Device Tree syntax
- Familiarity with your board's schematic
- Understanding of STM32F767ZI pinout

### Required Tools
```bash
# Zephyr environment activated
source ~/.zehpyrvenv/bin/activate

# Verify Zephyr is available
echo $ZEPHYR_BASE
```

### Reference Materials
- Your board schematic (PDF or source)
- [STM32F767ZI Datasheet](https://www.st.com/resource/en/datasheet/stm32f767zi.pdf)
- [STM32F767ZI Reference Manual](https://www.st.com/resource/en/reference_manual/rm0410-stm32f76xxx-and-stm32f77xxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- Existing Nucleo board as reference: `$ZEPHYR_BASE/boards/st/nucleo_f767zi/`

---

## 3. Board Directory Structure

### Standard Zephyr Board Layout

```
boards/
└── <vendor>/                    # Your company/organization name
    └── <board_name>/            # Your board name
        ├── board.yml            # Board metadata (Zephyr 3.x+)
        ├── Kconfig.board        # Board Kconfig entry
        ├── Kconfig.defconfig    # Default Kconfig values
        ├── <board_name>_defconfig    # Default configuration
        ├── <board_name>.dts     # Main device tree source
        ├── <board_name>-pinctrl.dtsi # Pin control definitions
        ├── doc/                 # Documentation (optional)
        │   ├── index.rst
        │   └── img/
        └── support/             # Additional support files (optional)
```

### Where to Place Your Board

**Option A: In Zephyr tree** (not recommended for custom boards)
```
$ZEPHYR_BASE/boards/<vendor>/<board_name>/
```

**Option B: In your application** (recommended)
```
your_project/
├── boards/
│   └── <vendor>/
│       └── <board_name>/
├── src/
├── CMakeLists.txt
└── prj.conf
```

**Option C: Separate repository**
```
my_boards_repo/
└── boards/
    └── <vendor>/
        └── <board_name>/
```

For this guide, we'll use **Option B** (in your application).

---

## Step 1: Create Board Directory

### 1.1 Define Your Board Name

Choose a descriptive name:
```
<company>_<product>_<variant>
```

**Example:** `mycompany_emg_board`

### 1.2 Create Directory Structure

```bash
# Navigate to your project
cd /home/cgu/Documents/EMG_Read_Test

# Create board directory
mkdir -p boards/mycompany/mycompany_emg_board

# Create subdirectories
mkdir -p boards/mycompany/mycompany_emg_board/doc
```

### 1.3 Verify Structure

```bash
tree boards/
# Expected:
# boards/
# └── mycompany/
#     └── mycompany_emg_board/
#         └── doc/
```

---

## Step 2: Create Board YAML File

> **Note:** `board.yml` is required for Zephyr 3.x and later.

### 2.1 Create `board.yml`

```yaml
# boards/mycompany/mycompany_emg_board/board.yml

board:
  name: mycompany_emg_board
  vendor: mycompany
  socs:
    - name: stm32f767xx
  variants:
    - name: default
```

### 2.2 Understanding `board.yml`

| Field | Description |
|-------|-------------|
| `name` | Board identifier (used in west build -b) |
| `vendor` | Your company/organization |
| `socs` | List of supported SoCs |
| `variants` | Board variants (e.g., with/without external flash) |

### 2.3 Multiple Variants Example (Optional)

```yaml
board:
  name: mycompany_emg_board
  vendor: mycompany
  socs:
    - name: stm32f767xx
  variants:
    - name: default
      qualifier: default
    - name: ext_flash
      qualifier: ext_flash
```

---

## Step 3: Create Board Kconfig Files

### 3.1 Create `Kconfig.board`

This file registers your board with Zephyr's build system.

```kconfig
# boards/mycompany/mycompany_emg_board/Kconfig.board

config BOARD_MYCOMPANY_EMG_BOARD
	bool "MyCompany EMG Board"
	depends on SOC_STM32F767XX
	select SOC_STM32F767XX
```

### 3.2 Create `Kconfig.defconfig`

This file sets default Kconfig values when your board is selected.

```kconfig
# boards/mycompany/mycompany_emg_board/Kconfig.defconfig

if BOARD_MYCOMPANY_EMG_BOARD

config BOARD
	default "mycompany_emg_board"

# Default console on USART3 (commonly used for ST-Link VCP)
config UART_CONSOLE
	default y if CONSOLE

# Enable GPIO by default
config GPIO
	default y

# Set default clock configuration
config SYS_CLOCK_HW_CYCLES_PER_SEC
	default 216000000  # 216 MHz (max for STM32F767)

# Flash configuration
config FLASH_SIZE
	default 2048  # 2MB flash

config FLASH_BASE_ADDRESS
	default 0x08000000

endif # BOARD_MYCOMPANY_EMG_BOARD
```

---

## Step 4: Create Board Defconfig

### 4.1 Create `mycompany_emg_board_defconfig`

This file contains the default Kconfig options for your board.

```kconfig
# boards/mycompany/mycompany_emg_board/mycompany_emg_board_defconfig

# Core configuration
CONFIG_SOC_SERIES_STM32F7X=y
CONFIG_SOC_STM32F767XX=y
CONFIG_BOARD_MYCOMPANY_EMG_BOARD=y

# ARM Cortex-M7 settings
CONFIG_CPU_CORTEX_M7=y
CONFIG_CPU_CORTEX_M_HAS_DWT=y
CONFIG_CPU_HAS_FPU=y
CONFIG_CPU_HAS_ARM_MPU=y

# Enable MPU
CONFIG_ARM_MPU=y

# Clock configuration (216 MHz using HSE + PLL)
CONFIG_CLOCK_CONTROL=y
CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC=216000000

# Enable GPIO
CONFIG_GPIO=y

# Console configuration
CONFIG_SERIAL=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y

# Use USART3 for console (ST-Link Virtual COM Port on Nucleo)
# Change this based on your board's debug UART
CONFIG_UART_3=y

# Enable pinctrl
CONFIG_PINCTRL=y

# Flash driver
CONFIG_FLASH=y
CONFIG_SOC_FLASH_STM32=y

# Optionally enable SPI (for ADS1298)
CONFIG_SPI=y
```

---

## Step 5: Create Device Tree Source

### 5.1 Create Main DTS File

```dts
/* boards/mycompany/mycompany_emg_board/mycompany_emg_board.dts */

/dts-v1/;
#include <st/f7/stm32f767Xi.dtsi>
#include <st/f7/stm32f767zitx-pinctrl.dtsi>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include "mycompany_emg_board-pinctrl.dtsi"

/ {
	model = "MyCompany EMG Board";
	compatible = "mycompany,emg-board";

	/* Define memory regions */
	chosen {
		zephyr,console = &usart3;
		zephyr,shell-uart = &usart3;
		zephyr,sram = &sram0;
		zephyr,flash = &flash0;
		zephyr,code-partition = &slot0_partition;
		zephyr,canbus = &can1;
	};

	/* On-board LEDs */
	leds {
		compatible = "gpio-leds";

		/* User LED - adjust GPIO based on your schematic */
		led_user: led_user {
			gpios = <&gpiob 7 GPIO_ACTIVE_HIGH>;
			label = "User LED";
		};

		/* Status LED */
		led_status: led_status {
			gpios = <&gpiob 14 GPIO_ACTIVE_HIGH>;
			label = "Status LED";
		};
	};

	/* On-board buttons */
	gpio_keys {
		compatible = "gpio-keys";

		/* User button - adjust GPIO based on your schematic */
		user_button: user_button {
			gpios = <&gpioc 13 (GPIO_PULL_DOWN | GPIO_ACTIVE_HIGH)>;
			label = "User Button";
			zephyr,code = <INPUT_KEY_0>;
		};
	};

	/* Define aliases for easy access */
	aliases {
		led0 = &led_user;
		led1 = &led_status;
		sw0 = &user_button;
		watchdog0 = &iwdg;
		spi-flash0 = &spi1;
	};

	/* SRAM configuration */
	sram0: memory@20000000 {
		compatible = "mmio-sram";
		reg = <0x20000000 DT_SIZE_K(512)>;  /* 512KB SRAM */
	};

	/* ADS1298 GPIO definitions */
	ads1298_gpio {
		compatible = "gpio-leds";

		ads1298_drdy: ads1298_drdy {
			gpios = <&gpiof 13 GPIO_ACTIVE_LOW>;
			label = "ADS1298 DRDY";
		};

		ads1298_reset: ads1298_reset {
			gpios = <&gpiof 14 GPIO_ACTIVE_LOW>;
			label = "ADS1298 RESET";
		};

		ads1298_start: ads1298_start {
			gpios = <&gpioe 9 GPIO_ACTIVE_HIGH>;
			label = "ADS1298 START";
		};
	};
};

/* Clock configuration */
&clk_hse {
	clock-frequency = <DT_FREQ_M(8)>;  /* 8 MHz external crystal */
	status = "okay";
};

&clk_lse {
	clock-frequency = <32768>;  /* 32.768 kHz for RTC */
	status = "okay";
};

&pll {
	div-m = <8>;
	mul-n = <432>;
	div-p = <2>;
	div-q = <9>;
	clocks = <&clk_hse>;
	status = "okay";
};

&rcc {
	clocks = <&pll>;
	clock-frequency = <DT_FREQ_M(216)>;  /* 216 MHz system clock */
	ahb-prescaler = <1>;
	apb1-prescaler = <4>;   /* APB1 = 54 MHz */
	apb2-prescaler = <2>;   /* APB2 = 108 MHz */
};

/* Flash configuration */
&flash0 {
	partitions {
		compatible = "fixed-partitions";
		#address-cells = <1>;
		#size-cells = <1>;

		/* Bootloader partition (optional) */
		boot_partition: partition@0 {
			label = "bootloader";
			reg = <0x00000000 DT_SIZE_K(64)>;
			read-only;
		};

		/* Main application partition */
		slot0_partition: partition@10000 {
			label = "image-0";
			reg = <0x00010000 DT_SIZE_K(896)>;
		};

		/* Secondary slot for OTA (optional) */
		slot1_partition: partition@f0000 {
			label = "image-1";
			reg = <0x000f0000 DT_SIZE_K(896)>;
		};

		/* Storage partition */
		storage_partition: partition@1d0000 {
			label = "storage";
			reg = <0x001d0000 DT_SIZE_K(192)>;
		};
	};
};

/* Console UART (USART3 - commonly used with ST-Link VCP) */
&usart3 {
	pinctrl-0 = <&usart3_tx_pd8 &usart3_rx_pd9>;
	pinctrl-names = "default";
	current-speed = <115200>;
	status = "okay";
};

/* Additional UART for external communication (optional) */
&usart2 {
	pinctrl-0 = <&usart2_tx_pd5 &usart2_rx_pd6>;
	pinctrl-names = "default";
	current-speed = <115200>;
	status = "disabled";  /* Enable when needed */
};

/* SPI1 for ADS1298 */
&spi1 {
	pinctrl-0 = <&spi1_sck_pa5 &spi1_miso_pa6 &spi1_mosi_pa7>;
	pinctrl-names = "default";
	cs-gpios = <&gpiod 14 GPIO_ACTIVE_LOW>;
	status = "okay";

	/* ADS1298 ADC */
	ads1298: ads1298@0 {
		compatible = "ti,ads1298";
		reg = <0>;
		spi-max-frequency = <2000000>;
		drdy-gpios = <&gpiof 13 GPIO_ACTIVE_LOW>;
		reset-gpios = <&gpiof 14 GPIO_ACTIVE_LOW>;
		start-gpios = <&gpioe 9 GPIO_ACTIVE_HIGH>;
	};
};

/* SPI2 for external flash or other devices (optional) */
&spi2 {
	pinctrl-0 = <&spi2_sck_pb13 &spi2_miso_pb14 &spi2_mosi_pb15>;
	pinctrl-names = "default";
	status = "disabled";
};

/* I2C1 for sensors (optional) */
&i2c1 {
	pinctrl-0 = <&i2c1_scl_pb8 &i2c1_sda_pb9>;
	pinctrl-names = "default";
	clock-frequency = <I2C_BITRATE_FAST>;
	status = "disabled";
};

/* CAN1 for external communication (optional) */
&can1 {
	pinctrl-0 = <&can1_rx_pd0 &can1_tx_pd1>;
	pinctrl-names = "default";
	status = "disabled";
};

/* USB OTG FS (optional) */
&usbotg_fs {
	pinctrl-0 = <&usb_otg_fs_dm_pa11 &usb_otg_fs_dp_pa12>;
	pinctrl-names = "default";
	status = "disabled";
};

/* ADC1 for analog inputs (optional) */
&adc1 {
	pinctrl-0 = <&adc1_in0_pa0 &adc1_in1_pa1>;
	pinctrl-names = "default";
	st,adc-clock-source = <SYNC>;
	st,adc-prescaler = <4>;
	status = "disabled";
};

/* Timers */
&timers2 {
	status = "okay";

	pwm2: pwm {
		pinctrl-0 = <&tim2_ch1_pa0>;
		pinctrl-names = "default";
		status = "disabled";
	};
};

/* Watchdog */
&iwdg {
	status = "okay";
};

/* RTC */
&rtc {
	clocks = <&rcc STM32_CLOCK_BUS_APB1 0x10000000>,
		 <&rcc STM32_SRC_LSE RTC_SEL(1)>;
	status = "okay";
};

/* Backup SRAM */
&backup_sram {
	status = "okay";
};

/* Random number generator */
&rng {
	status = "okay";
};

/* Enable all required GPIO ports */
&gpioa {
	status = "okay";
};

&gpiob {
	status = "okay";
};

&gpioc {
	status = "okay";
};

&gpiod {
	status = "okay";
};

&gpioe {
	status = "okay";
};

&gpiof {
	status = "okay";
};

&gpiog {
	status = "okay";
};

&gpioh {
	status = "okay";
};
```

### 5.2 Key DTS Sections Explained

| Section | Purpose |
|---------|---------|
| `model` / `compatible` | Board identification |
| `chosen` | Default devices for console, flash, etc. |
| `leds` | On-board LED definitions |
| `gpio_keys` | Button definitions |
| `aliases` | Shortcut names |
| `&clk_*` / `&pll` / `&rcc` | Clock configuration |
| `&flash0` | Flash partitioning |
| `&usart3` | UART configuration |
| `&spi1` | SPI configuration |

---

## Step 6: Create Pin Control Configuration

### 6.1 Create `mycompany_emg_board-pinctrl.dtsi`

```dts
/* boards/mycompany/mycompany_emg_board/mycompany_emg_board-pinctrl.dtsi */

/*
 * Pin Control Configuration for MyCompany EMG Board
 *
 * This file defines custom pin mappings that differ from the default
 * STM32F767ZI pinctrl. For standard pins, we include the ST-provided
 * pinctrl file and only override what's needed.
 *
 * Reference: STM32F767ZI Datasheet - Alternate Function Mapping
 */

#include <st/f7/stm32f767zitx-pinctrl.dtsi>

&pinctrl {
	/*
	 * Custom pin definitions go here.
	 * Only define pins that differ from the default pinctrl.
	 *
	 * Format:
	 * <peripheral>_<function>_<port><pin>: <peripheral>_<function>_<port><pin> {
	 *     pinmux = <STM32_PINMUX('P', pin, AF_number)>;
	 *     bias-pull-up;       // or bias-pull-down, bias-disable
	 *     drive-push-pull;    // or drive-open-drain
	 *     slew-rate = "...";  // low-speed, medium-speed, high-speed, very-high-speed
	 * };
	 */

	/* Example: Custom UART pins if different from default */
	/*
	usart1_tx_custom: usart1_tx_custom {
		pinmux = <STM32_PINMUX('A', 9, AF7)>;
		bias-pull-up;
		drive-push-pull;
	};
	*/

	/* Example: Custom SPI pins */
	/*
	spi1_sck_custom: spi1_sck_custom {
		pinmux = <STM32_PINMUX('A', 5, AF5)>;
		bias-pull-down;
		slew-rate = "very-high-speed";
	};
	*/
};
```

### 6.2 Understanding Pin Control

**STM32 Pinmux Format:**
```
STM32_PINMUX(port, pin, alternate_function)
```

**Example:**
```dts
/* PA5 as SPI1_SCK (AF5) */
pinmux = <STM32_PINMUX('A', 5, AF5)>;
```

### 6.3 Common Alternate Functions for STM32F767

| Peripheral | Common Pins | AF |
|------------|-------------|-----|
| USART1 TX/RX | PA9/PA10 | AF7 |
| USART2 TX/RX | PD5/PD6 | AF7 |
| USART3 TX/RX | PD8/PD9 | AF7 |
| SPI1 SCK/MISO/MOSI | PA5/PA6/PA7 | AF5 |
| SPI2 SCK/MISO/MOSI | PB13/PB14/PB15 | AF5 |
| I2C1 SCL/SDA | PB8/PB9 | AF4 |
| CAN1 RX/TX | PD0/PD1 | AF9 |
| TIM2 CH1 | PA0 | AF1 |

### 6.4 Pin Configuration Options

```dts
/* Input configurations */
bias-disable;        /* No pull-up/down (floating) */
bias-pull-up;        /* Internal pull-up */
bias-pull-down;      /* Internal pull-down */

/* Output configurations */
drive-push-pull;     /* Standard push-pull output */
drive-open-drain;    /* Open-drain output */

/* Speed configurations */
slew-rate = "low-speed";         /* Up to 8 MHz */
slew-rate = "medium-speed";      /* Up to 50 MHz */
slew-rate = "high-speed";        /* Up to 100 MHz */
slew-rate = "very-high-speed";   /* Up to 180 MHz */
```

---

## Step 7: Create Board Documentation

### 7.1 Create `doc/index.rst` (Optional but Recommended)

```rst
.. _mycompany_emg_board:

MyCompany EMG Board
###################

Overview
********

The MyCompany EMG Board is a custom board designed for EMG signal acquisition,
based on the STM32F767ZI microcontroller.

Hardware
********

- **MCU**: STM32F767ZIT6
  - ARM Cortex-M7 @ 216 MHz
  - 2 MB Flash, 512 KB SRAM
  - FPU, DSP instructions

- **ADC**: ADS1298 (8-channel, 24-bit)
- **Debug**: SWD via ST-Link or J-Link
- **Communication**: USB, CAN, UART

Supported Features
******************

+-----------+------------+-------------------------------------+
| Interface | Controller | Driver/Component                    |
+===========+============+=====================================+
| NVIC      | on-chip    | nested vector interrupt controller  |
+-----------+------------+-------------------------------------+
| UART      | on-chip    | serial port-polling;                |
|           |            | serial port-interrupt               |
+-----------+------------+-------------------------------------+
| GPIO      | on-chip    | gpio                                |
+-----------+------------+-------------------------------------+
| SPI       | on-chip    | spi                                 |
+-----------+------------+-------------------------------------+
| I2C       | on-chip    | i2c                                 |
+-----------+------------+-------------------------------------+
| PWM       | on-chip    | pwm                                 |
+-----------+------------+-------------------------------------+
| ADC       | on-chip    | adc                                 |
+-----------+------------+-------------------------------------+
| FLASH     | on-chip    | flash                               |
+-----------+------------+-------------------------------------+
| RNG       | on-chip    | random number generator             |
+-----------+------------+-------------------------------------+
| WATCHDOG  | on-chip    | independent watchdog                |
+-----------+------------+-------------------------------------+
| USB       | on-chip    | usb_device                          |
+-----------+------------+-------------------------------------+
| CAN       | on-chip    | can                                 |
+-----------+------------+-------------------------------------+

Connections and IOs
*******************

LED
===

- LED_USER (Green): PB7
- LED_STATUS (Red): PB14

Button
======

- USER_BUTTON: PC13

SPI1 (ADS1298)
==============

- SCK: PA5
- MISO: PA6
- MOSI: PA7
- CS: PD14
- DRDY: PF13
- RESET: PF14
- START: PE9

Console UART
============

- USART3 TX: PD8
- USART3 RX: PD9
- Baud rate: 115200

Programming and Debugging
*************************

Build and flash:

.. code-block:: console

   west build -b mycompany_emg_board
   west flash

Debugging:

.. code-block:: console

   west debug
```

---

## Step 8: Build and Test

### 8.1 Update CMakeLists.txt

Add board root to your project's `CMakeLists.txt`:

```cmake
# CMakeLists.txt

cmake_minimum_required(VERSION 3.20.0)

# Point to custom boards BEFORE find_package(Zephyr)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(emg_reader)

target_sources(app PRIVATE
    src/main.c
    bus/src/spi_bus.c
    drivers/src/ads1298.c
)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/bus/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/drivers/inc
)
```

### 8.2 Build the Project

```bash
# Clean previous build
rm -rf build

# Build for your custom board
west build -b mycompany_emg_board

# Or with pristine build
west build -b mycompany_emg_board --pristine
```

### 8.3 Verify Device Tree

```bash
# Check generated device tree
cat build/zephyr/zephyr.dts

# Check device tree bindings
west build -t menuconfig  # Look for your peripherals
```

### 8.4 Flash and Test

```bash
# Flash to board
west flash

# Open serial console (115200 baud)
minicom -D /dev/ttyACM0 -b 115200

# Or use screen
screen /dev/ttyACM0 115200
```

### 8.5 Debug

```bash
# Start debugger
west debug

# Or attach to running target
west attach
```

---

## Common Customizations

### Change Console UART

In your DTS file:
```dts
/ {
    chosen {
        zephyr,console = &usart1;  /* Change from usart3 */
        zephyr,shell-uart = &usart1;
    };
};

&usart1 {
    pinctrl-0 = <&usart1_tx_pa9 &usart1_rx_pa10>;
    pinctrl-names = "default";
    current-speed = <115200>;
    status = "okay";
};
```

### Add External Flash

```dts
&spi2 {
    pinctrl-0 = <&spi2_sck_pb13 &spi2_miso_pb14 &spi2_mosi_pb15>;
    pinctrl-names = "default";
    cs-gpios = <&gpiob 12 GPIO_ACTIVE_LOW>;
    status = "okay";

    w25q128: w25q128@0 {
        compatible = "jedec,spi-nor";
        reg = <0>;
        spi-max-frequency = <40000000>;
        jedec-id = [ef 40 18];  /* W25Q128 */
        size = <DT_SIZE_M(128)>;  /* 128 Mbit = 16 MB */
        has-dpd;
        t-enter-dpd = <3000>;
        t-exit-dpd = <30000>;
    };
};
```

### Change Clock Speed

```dts
&pll {
    div-m = <8>;
    mul-n = <360>;   /* Reduce for lower speed */
    div-p = <2>;
    div-q = <8>;
    clocks = <&clk_hse>;
    status = "okay";
};

&rcc {
    clocks = <&pll>;
    clock-frequency = <DT_FREQ_M(180)>;  /* 180 MHz instead of 216 */
    ahb-prescaler = <1>;
    apb1-prescaler = <4>;
    apb2-prescaler = <2>;
};
```

### Add More LEDs

```dts
leds {
    compatible = "gpio-leds";

    led_red: led_red {
        gpios = <&gpiob 14 GPIO_ACTIVE_HIGH>;
        label = "Red LED";
    };

    led_green: led_green {
        gpios = <&gpiob 0 GPIO_ACTIVE_HIGH>;
        label = "Green LED";
    };

    led_blue: led_blue {
        gpios = <&gpiob 7 GPIO_ACTIVE_HIGH>;
        label = "Blue LED";
    };
};

aliases {
    led0 = &led_red;
    led1 = &led_green;
    led2 = &led_blue;
};
```

---

## Troubleshooting

### Build Errors

| Error | Solution |
|-------|----------|
| `Board not found` | Check `BOARD_ROOT` in CMakeLists.txt |
| `SOC not found` | Verify `CONFIG_SOC_STM32F767XX=y` in defconfig |
| `Pinctrl not found` | Check #include in DTS file |
| `DTS syntax error` | Validate DTS with `dtc` compiler |

### Runtime Issues

| Issue | Solution |
|-------|----------|
| No console output | Check `chosen { zephyr,console = ... }` |
| Wrong baud rate | Verify `current-speed` in UART node |
| Peripheral not working | Check `status = "okay"` |
| Wrong pins | Verify pinctrl configuration |

### Debug Commands

```bash
# Check device tree compilation
west build -t dts_check

# View preprocessed device tree
cat build/zephyr/zephyr.dts.pre

# Check Kconfig
west build -t menuconfig

# Verbose build
west build -v
```

---

## Reference: STM32F767ZI Specifications

### Memory Map

| Region | Start Address | Size |
|--------|---------------|------|
| Flash | 0x08000000 | 2 MB |
| SRAM1 | 0x20000000 | 368 KB |
| SRAM2 | 0x2005C000 | 16 KB |
| DTCM RAM | 0x20000000 | 128 KB |

### Clock Tree (Max Configuration)

```
HSE (8 MHz) → PLL → SYSCLK (216 MHz)
                  ├── AHB (216 MHz)
                  ├── APB1 (54 MHz max)
                  └── APB2 (108 MHz max)
```

### Key Peripheral Base Addresses

| Peripheral | Base Address |
|------------|--------------|
| GPIOA | 0x40020000 |
| GPIOB | 0x40020400 |
| SPI1 | 0x40013000 |
| USART3 | 0x40004800 |
| I2C1 | 0x40005400 |

---

## Final Checklist

- [ ] Created `board.yml`
- [ ] Created `Kconfig.board`
- [ ] Created `Kconfig.defconfig`
- [ ] Created `<board>_defconfig`
- [ ] Created `<board>.dts`
- [ ] Created `<board>-pinctrl.dtsi`
- [ ] Added `BOARD_ROOT` to CMakeLists.txt
- [ ] Successfully built with `west build -b <board>`
- [ ] Verified console output
- [ ] Tested all required peripherals
