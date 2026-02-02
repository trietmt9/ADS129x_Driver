# Bootloader Development Guide for STM32F767ZI

> A comprehensive guide to understanding and writing a bootloader for your custom board.

---

## Table of Contents

1. [What is a Bootloader?](#1-what-is-a-bootloader)
2. [Bootloader Options](#2-bootloader-options)
3. [Memory Layout Planning](#3-memory-layout-planning)
4. [Option A: Using MCUboot (Recommended)](#4-option-a-using-mcuboot-recommended)
5. [Option B: Custom Bootloader](#5-option-b-custom-bootloader)
6. [Bootloader Core Components](#6-bootloader-core-components)
7. [Implementation: Minimal Custom Bootloader](#7-implementation-minimal-custom-bootloader)
8. [Implementation: Full-Featured Bootloader](#8-implementation-full-featured-bootloader)
9. [Firmware Update Mechanisms](#9-firmware-update-mechanisms)
10. [Security Considerations](#10-security-considerations)
11. [Testing and Debugging](#11-testing-and-debugging)
12. [Production Checklist](#12-production-checklist)

---

## 1. What is a Bootloader?

### Definition

A bootloader is a small program that runs before your main application. It:

1. **Initializes** minimal hardware (clocks, memory)
2. **Validates** the application firmware
3. **Jumps** to the application or enters update mode

### Boot Sequence

```
┌─────────────────────────────────────────────────────────────────────┐
│                         POWER ON / RESET                            │
└─────────────────────────────────┬───────────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    Hardware Reset Vector                            │
│                    (0x08000000 for STM32)                           │
└─────────────────────────────────┬───────────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         BOOTLOADER                                  │
│  1. Initialize clocks (minimal)                                     │
│  2. Check for update trigger (button, flag, etc.)                   │
│  3. Validate application image                                      │
│  4. Jump to application OR enter update mode                        │
└─────────────────────────────────┬───────────────────────────────────┘
                                  │
                    ┌─────────────┴─────────────┐
                    │                           │
                    ▼                           ▼
┌───────────────────────────┐   ┌───────────────────────────────────┐
│     UPDATE MODE           │   │         JUMP TO APP               │
│  - UART/USB/CAN update    │   │  1. Set MSP (Main Stack Pointer)  │
│  - Receive new firmware   │   │  2. Jump to app reset vector      │
│  - Write to flash         │   │  3. Application starts            │
│  - Verify & reset         │   │                                   │
└───────────────────────────┘   └───────────────────────────────────┘
```

### Why Write a Bootloader?

| Use Case | Benefit |
|----------|---------|
| Field updates | Update firmware without physical access |
| Fail-safe recovery | Recover from bad firmware |
| Secure boot | Verify firmware authenticity |
| Multi-image | Support A/B partitions |
| Factory reset | Return to known-good state |

---

## 2. Bootloader Options

### Option Comparison

| Option | Complexity | Features | Best For |
|--------|------------|----------|----------|
| STM32 ROM Bootloader | None (built-in) | UART/USB/CAN | Development only |
| MCUboot | Medium | Full OTA, secure boot | Production with Zephyr |
| Custom Minimal | Low | Basic update | Simple products |
| Custom Full | High | Custom features | Special requirements |

### Decision Tree

```
Need firmware updates in the field?
├── No → No bootloader needed (direct flash)
└── Yes →
    ├── Using Zephyr? → MCUboot (recommended)
    └── Custom requirements?
        ├── Secure boot required? → MCUboot or Custom with crypto
        ├── Minimal footprint? → Custom minimal bootloader
        └── Special protocol? → Custom bootloader
```

---

## 3. Memory Layout Planning

### STM32F767ZI Flash Layout

Total Flash: **2 MB** (0x08000000 - 0x081FFFFF)

### Recommended Layout (with MCUboot)

```
┌──────────────────────────────────────────────────────────────────┐
│ Address       │ Size    │ Region                                │
├──────────────────────────────────────────────────────────────────┤
│ 0x08000000    │ 64 KB   │ Bootloader                            │
│ 0x08010000    │ 64 KB   │ Bootloader Scratch (for swap)         │
│ 0x08020000    │ 896 KB  │ Slot 0 - Primary Application          │
│ 0x08100000    │ 896 KB  │ Slot 1 - Secondary (Update) Image     │
│ 0x081E0000    │ 128 KB  │ Storage / Configuration               │
└──────────────────────────────────────────────────────────────────┘
```

### Minimal Layout (Single Image)

```
┌──────────────────────────────────────────────────────────────────┐
│ Address       │ Size    │ Region                                │
├──────────────────────────────────────────────────────────────────┤
│ 0x08000000    │ 32 KB   │ Bootloader                            │
│ 0x08008000    │ 1984 KB │ Application                           │
│ 0x081F0000    │ 64 KB   │ Configuration / NVS                   │
└──────────────────────────────────────────────────────────────────┘
```

### A/B Layout (Fail-safe Updates)

```
┌──────────────────────────────────────────────────────────────────┐
│ Address       │ Size    │ Region                                │
├──────────────────────────────────────────────────────────────────┤
│ 0x08000000    │ 64 KB   │ Bootloader                            │
│ 0x08010000    │ 960 KB  │ Slot A - Application                  │
│ 0x08100000    │ 960 KB  │ Slot B - Application                  │
│ 0x081F0000    │ 64 KB   │ Boot Info / Configuration             │
└──────────────────────────────────────────────────────────────────┘
```

---

## 4. Option A: Using MCUboot (Recommended)

### What is MCUboot?

MCUboot is a secure bootloader for 32-bit MCUs. It's the default bootloader for Zephyr and supports:

- Secure boot (signature verification)
- Encrypted images
- Rollback protection
- Multiple upgrade strategies (swap, overwrite, direct-xip)

### MCUboot Setup for Your Board

#### 4.1 Update Board DTS for MCUboot

```dts
/* In your board DTS file */
/ {
    chosen {
        zephyr,code-partition = &slot0_partition;
    };
};

&flash0 {
    partitions {
        compatible = "fixed-partitions";
        #address-cells = <1>;
        #size-cells = <1>;

        /* MCUboot bootloader */
        boot_partition: partition@0 {
            label = "mcuboot";
            reg = <0x00000000 DT_SIZE_K(64)>;
            read-only;
        };

        /* Primary slot */
        slot0_partition: partition@10000 {
            label = "image-0";
            reg = <0x00010000 DT_SIZE_K(896)>;
        };

        /* Secondary slot */
        slot1_partition: partition@f0000 {
            label = "image-1";
            reg = <0x000f0000 DT_SIZE_K(896)>;
        };

        /* Scratch area for swap */
        scratch_partition: partition@1d0000 {
            label = "image-scratch";
            reg = <0x001d0000 DT_SIZE_K(128)>;
        };

        /* Storage */
        storage_partition: partition@1f0000 {
            label = "storage";
            reg = <0x001f0000 DT_SIZE_K(64)>;
        };
    };
};
```

#### 4.2 Build MCUboot

```bash
# Build MCUboot for your board
west build -b mycompany_emg_board -d build-mcuboot \
    $ZEPHYR_BASE/../bootloader/mcuboot/boot/zephyr \
    -- -DCONFIG_BOOT_SIGNATURE_TYPE_RSA=y

# Flash MCUboot
west flash -d build-mcuboot
```

#### 4.3 Build Signed Application

```bash
# Build your application
west build -b mycompany_emg_board

# Sign the application
west sign -t imgtool -- --key $ZEPHYR_BASE/../bootloader/mcuboot/root-rsa-2048.pem

# Flash signed application
west flash --hex-file build/zephyr/zephyr.signed.hex
```

#### 4.4 MCUboot Configuration (prj.conf)

```kconfig
# prj.conf for MCUboot

CONFIG_BOOTLOADER_MCUBOOT=y
CONFIG_MCUBOOT_SIGNATURE_KEY_FILE="bootloader/mcuboot/root-rsa-2048.pem"

# Optional: Enable firmware update via UART
CONFIG_MCUBOOT_SERIAL=y
CONFIG_BOOT_SERIAL_UART=y
```

---

## 5. Option B: Custom Bootloader

### When to Use Custom Bootloader

- Minimal flash footprint required (< 16 KB)
- Custom update protocol needed
- Specific hardware initialization
- Learning purposes

### Custom Bootloader Project Structure

```
bootloader/
├── CMakeLists.txt
├── prj.conf
├── src/
│   ├── main.c              # Bootloader entry point
│   ├── flash.c             # Flash operations
│   ├── flash.h
│   ├── update.c            # Update mechanism
│   ├── update.h
│   ├── image.c             # Image validation
│   ├── image.h
│   ├── jump.c              # Jump to application
│   └── jump.h
└── include/
    └── bootloader.h        # Common definitions
```

---

## 6. Bootloader Core Components

### Component Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                        BOOTLOADER COMPONENTS                        │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌───────────┐  │
│  │   Startup   │  │   Flash     │  │   Image     │  │   Jump    │  │
│  │   & Init    │  │   Driver    │  │   Validator │  │   Logic   │  │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └─────┬─────┘  │
│         │                │                │               │         │
│         ▼                ▼                ▼               ▼         │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                    Update Mechanism                          │   │
│  │         (UART / USB / CAN / Wireless / SD Card)             │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### Component Details

| Component | Responsibility |
|-----------|----------------|
| **Startup** | Clock init, GPIO init, minimal peripherals |
| **Flash Driver** | Erase, write, read flash memory |
| **Image Validator** | Check CRC/signature, validate header |
| **Jump Logic** | Set stack pointer, jump to app |
| **Update Mechanism** | Receive new firmware via interface |

---

## 7. Implementation: Minimal Custom Bootloader

### 7.1 Project Configuration

**CMakeLists.txt:**
```cmake
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bootloader)

target_sources(app PRIVATE
    src/main.c
    src/flash.c
    src/jump.c
)
```

**prj.conf:**
```kconfig
# Minimal bootloader configuration
CONFIG_BOOTLOADER=y
CONFIG_MAIN_STACK_SIZE=2048
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=1024

# Disable unnecessary features
CONFIG_BOOT_BANNER=n
CONFIG_PRINTK=y
CONFIG_CONSOLE=y
CONFIG_SERIAL=y
CONFIG_UART_CONSOLE=y

# Flash support
CONFIG_FLASH=y
CONFIG_SOC_FLASH_STM32=y

# GPIO for button check
CONFIG_GPIO=y

# No kernel features needed
CONFIG_MULTITHREADING=n
```

### 7.2 Common Header

**include/bootloader.h:**
```c
#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <stdint.h>
#include <stdbool.h>

/*============================================================================
 * Memory Layout Configuration
 *============================================================================*/

#define BOOTLOADER_START_ADDR   0x08000000
#define BOOTLOADER_SIZE         0x8000      /* 32 KB */

#define APP_START_ADDR          0x08008000
#define APP_MAX_SIZE            0x1F0000    /* ~1984 KB */

#define CONFIG_START_ADDR       0x081F8000
#define CONFIG_SIZE             0x8000      /* 32 KB */

/*============================================================================
 * Image Header Structure
 *============================================================================*/

#define IMAGE_MAGIC             0x96F3B83D
#define IMAGE_HEADER_SIZE       256

struct image_header {
    uint32_t magic;             /* IMAGE_MAGIC */
    uint32_t version;           /* Firmware version */
    uint32_t size;              /* Image size (excluding header) */
    uint32_t crc32;             /* CRC32 of image data */
    uint32_t entry_point;       /* Entry point address */
    uint32_t reserved[59];      /* Reserved for future use */
};

/*============================================================================
 * Boot Configuration (stored in flash)
 *============================================================================*/

#define BOOT_CONFIG_MAGIC       0xB007C0F6

struct boot_config {
    uint32_t magic;
    uint32_t boot_count;
    uint32_t update_pending;
    uint32_t last_error;
    uint32_t reserved[12];
};

/*============================================================================
 * Error Codes
 *============================================================================*/

#define BOOT_OK                 0
#define BOOT_ERR_NO_IMAGE       -1
#define BOOT_ERR_BAD_MAGIC      -2
#define BOOT_ERR_BAD_CRC        -3
#define BOOT_ERR_FLASH_WRITE    -4
#define BOOT_ERR_FLASH_ERASE    -5

/*============================================================================
 * Function Prototypes
 *============================================================================*/

/* Flash operations */
int flash_erase(uint32_t addr, size_t size);
int flash_write(uint32_t addr, const void *data, size_t size);
int flash_read(uint32_t addr, void *data, size_t size);

/* Image validation */
bool image_validate(uint32_t addr);
uint32_t image_crc32(const uint8_t *data, size_t size);

/* Jump to application */
void jump_to_app(uint32_t addr);

/* Update mechanism */
int update_check_trigger(void);
int update_receive_image(void);

#endif /* BOOTLOADER_H */
```

### 7.3 Main Bootloader

**src/main.c:**
```c
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include "bootloader.h"

/* Button for forcing update mode */
#define UPDATE_BUTTON_NODE  DT_ALIAS(sw0)
#if DT_NODE_EXISTS(UPDATE_BUTTON_NODE)
static const struct gpio_dt_spec update_btn = GPIO_DT_SPEC_GET(UPDATE_BUTTON_NODE, gpios);
#endif

/* LED for status indication */
#define STATUS_LED_NODE     DT_ALIAS(led0)
#if DT_NODE_EXISTS(STATUS_LED_NODE)
static const struct gpio_dt_spec status_led = GPIO_DT_SPEC_GET(STATUS_LED_NODE, gpios);
#endif

static void led_init(void)
{
#if DT_NODE_EXISTS(STATUS_LED_NODE)
    if (device_is_ready(status_led.port)) {
        gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE);
    }
#endif
}

static void led_set(bool on)
{
#if DT_NODE_EXISTS(STATUS_LED_NODE)
    gpio_pin_set_dt(&status_led, on);
#endif
}

static bool check_update_button(void)
{
#if DT_NODE_EXISTS(UPDATE_BUTTON_NODE)
    if (!device_is_ready(update_btn.port)) {
        return false;
    }
    gpio_pin_configure_dt(&update_btn, GPIO_INPUT);
    k_msleep(10);  /* Debounce */
    return gpio_pin_get_dt(&update_btn) != 0;
#else
    return false;
#endif
}

static bool check_update_flag(void)
{
    struct boot_config config;
    flash_read(CONFIG_START_ADDR, &config, sizeof(config));

    if (config.magic != BOOT_CONFIG_MAGIC) {
        return false;
    }

    return config.update_pending != 0;
}

static void clear_update_flag(void)
{
    struct boot_config config;
    flash_read(CONFIG_START_ADDR, &config, sizeof(config));

    if (config.magic == BOOT_CONFIG_MAGIC && config.update_pending) {
        config.update_pending = 0;
        flash_erase(CONFIG_START_ADDR, CONFIG_SIZE);
        flash_write(CONFIG_START_ADDR, &config, sizeof(config));
    }
}

int main(void)
{
    bool enter_update_mode = false;

    printk("\n");
    printk("=====================================\n");
    printk("  Custom Bootloader v1.0.0\n");
    printk("  Board: MyCompany EMG Board\n");
    printk("=====================================\n");

    led_init();

    /* Check for update triggers */
    if (check_update_button()) {
        printk("[BOOT] Update button pressed\n");
        enter_update_mode = true;
    }

    if (check_update_flag()) {
        printk("[BOOT] Update flag set\n");
        enter_update_mode = true;
    }

    /* Enter update mode if triggered */
    if (enter_update_mode) {
        printk("[BOOT] Entering update mode...\n");
        led_set(true);

        int ret = update_receive_image();
        if (ret == BOOT_OK) {
            printk("[BOOT] Update successful!\n");
            clear_update_flag();
        } else {
            printk("[BOOT] Update failed: %d\n", ret);
        }

        /* Reset after update attempt */
        printk("[BOOT] Resetting...\n");
        k_msleep(100);
        NVIC_SystemReset();
    }

    /* Validate application image */
    printk("[BOOT] Validating application at 0x%08X...\n", APP_START_ADDR);

    if (!image_validate(APP_START_ADDR)) {
        printk("[BOOT] ERROR: No valid application found!\n");
        printk("[BOOT] Entering update mode...\n");

        /* Blink LED to indicate error */
        while (1) {
            led_set(true);
            k_msleep(100);
            led_set(false);
            k_msleep(100);

            /* Try to receive update */
            if (update_receive_image() == BOOT_OK) {
                NVIC_SystemReset();
            }
        }
    }

    /* Jump to application */
    printk("[BOOT] Jumping to application...\n");
    printk("=====================================\n\n");
    k_msleep(10);  /* Allow UART to flush */

    jump_to_app(APP_START_ADDR);

    /* Should never reach here */
    printk("[BOOT] ERROR: Failed to jump to application!\n");
    while (1) {
        k_msleep(1000);
    }

    return 0;
}
```

### 7.4 Flash Operations

**src/flash.c:**
```c
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <string.h>
#include "bootloader.h"

static const struct device *flash_dev;

static int flash_init(void)
{
    if (flash_dev != NULL) {
        return 0;
    }

    flash_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));
    if (!device_is_ready(flash_dev)) {
        return -ENODEV;
    }

    return 0;
}

int flash_erase(uint32_t addr, size_t size)
{
    if (flash_init() != 0) {
        return BOOT_ERR_FLASH_ERASE;
    }

    int ret = flash_erase(flash_dev, addr, size);
    if (ret != 0) {
        return BOOT_ERR_FLASH_ERASE;
    }

    return BOOT_OK;
}

int flash_write(uint32_t addr, const void *data, size_t size)
{
    if (flash_init() != 0) {
        return BOOT_ERR_FLASH_WRITE;
    }

    int ret = flash_write(flash_dev, addr, data, size);
    if (ret != 0) {
        return BOOT_ERR_FLASH_WRITE;
    }

    return BOOT_OK;
}

int flash_read(uint32_t addr, void *data, size_t size)
{
    if (flash_init() != 0) {
        return -EIO;
    }

    int ret = flash_read(flash_dev, addr, data, size);
    if (ret != 0) {
        return -EIO;
    }

    return BOOT_OK;
}

/* CRC32 calculation (standard polynomial) */
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
    0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    /* ... full table omitted for brevity ... */
    /* Use standard CRC32 table or Zephyr's crc library */
};

uint32_t image_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < size; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}

bool image_validate(uint32_t addr)
{
    struct image_header header;

    /* Read header */
    if (flash_read(addr, &header, sizeof(header)) != BOOT_OK) {
        return false;
    }

    /* Check magic */
    if (header.magic != IMAGE_MAGIC) {
        printk("[BOOT] Invalid magic: 0x%08X\n", header.magic);
        return false;
    }

    /* Check size */
    if (header.size == 0 || header.size > APP_MAX_SIZE) {
        printk("[BOOT] Invalid size: %u\n", header.size);
        return false;
    }

    /* Verify CRC (optional, can be slow) */
    /*
    uint32_t calc_crc = image_crc32((uint8_t *)(addr + IMAGE_HEADER_SIZE), header.size);
    if (calc_crc != header.crc32) {
        printk("[BOOT] CRC mismatch: expected 0x%08X, got 0x%08X\n",
               header.crc32, calc_crc);
        return false;
    }
    */

    /* Quick validation: check stack pointer is valid */
    uint32_t *app_vector = (uint32_t *)(addr + IMAGE_HEADER_SIZE);
    uint32_t sp = app_vector[0];

    /* Stack pointer should be in SRAM range */
    if (sp < 0x20000000 || sp > 0x20080000) {
        printk("[BOOT] Invalid stack pointer: 0x%08X\n", sp);
        return false;
    }

    printk("[BOOT] Image valid: v%u.%u.%u, size=%u\n",
           (header.version >> 16) & 0xFF,
           (header.version >> 8) & 0xFF,
           header.version & 0xFF,
           header.size);

    return true;
}
```

### 7.5 Jump to Application

**src/jump.c:**
```c
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "bootloader.h"

/* Vector table offset register */
#define SCB_VTOR    (*(volatile uint32_t *)0xE000ED08)

typedef void (*app_entry_t)(void);

void jump_to_app(uint32_t addr)
{
    /* Get application vector table (after image header) */
    uint32_t *app_vector = (uint32_t *)(addr + IMAGE_HEADER_SIZE);

    /* First entry is initial stack pointer */
    uint32_t app_sp = app_vector[0];

    /* Second entry is reset handler (entry point) */
    uint32_t app_entry = app_vector[1];

    /* Validate addresses */
    if (app_sp < 0x20000000 || app_sp > 0x20080000) {
        printk("[BOOT] Invalid SP: 0x%08X\n", app_sp);
        return;
    }

    if (app_entry < 0x08000000 || app_entry > 0x08200000) {
        printk("[BOOT] Invalid entry: 0x%08X\n", app_entry);
        return;
    }

    /* Disable interrupts */
    __disable_irq();

    /* Disable SysTick */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;

    /* Clear pending interrupts */
    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;  /* Disable all interrupts */
        NVIC->ICPR[i] = 0xFFFFFFFF;  /* Clear pending interrupts */
    }

    /* Set vector table offset to application */
    SCB_VTOR = (uint32_t)(addr + IMAGE_HEADER_SIZE);

    /* Memory barrier */
    __DSB();
    __ISB();

    /* Set stack pointer */
    __set_MSP(app_sp);

    /* Jump to application */
    app_entry_t app_reset = (app_entry_t)app_entry;
    app_reset();

    /* Should never reach here */
    while (1);
}

/* Alternative: Jump without header (if app has no header) */
void jump_to_app_direct(uint32_t addr)
{
    uint32_t *app_vector = (uint32_t *)addr;
    uint32_t app_sp = app_vector[0];
    uint32_t app_entry = app_vector[1];

    __disable_irq();

    SysTick->CTRL = 0;

    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    SCB_VTOR = addr;

    __DSB();
    __ISB();

    __set_MSP(app_sp);

    ((app_entry_t)app_entry)();

    while (1);
}
```

---

## 8. Implementation: Full-Featured Bootloader

### 8.1 UART Update Protocol

**src/update.c:**
```c
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include "bootloader.h"

#define UPDATE_UART_NODE    DT_CHOSEN(zephyr_console)
#define UPDATE_TIMEOUT_MS   30000
#define PACKET_SIZE         256
#define MAX_RETRIES         3

/* Simple protocol:
 * Host sends: [CMD][LEN_H][LEN_L][DATA...][CRC_H][CRC_L]
 * Device responds: [ACK] or [NAK]
 */

#define CMD_START       0x01    /* Start update, expects image size */
#define CMD_DATA        0x02    /* Data packet */
#define CMD_END         0x03    /* End update, verify */
#define CMD_RESET       0x04    /* Reset device */
#define CMD_INFO        0x05    /* Get bootloader info */

#define RSP_ACK         0x06
#define RSP_NAK         0x15
#define RSP_READY       0x07

static const struct device *uart_dev;
static uint32_t rx_timeout;

static int uart_init(void)
{
    uart_dev = DEVICE_DT_GET(UPDATE_UART_NODE);
    if (!device_is_ready(uart_dev)) {
        return -ENODEV;
    }
    return 0;
}

static int uart_getc_timeout(uint8_t *c, uint32_t timeout_ms)
{
    uint32_t start = k_uptime_get_32();

    while ((k_uptime_get_32() - start) < timeout_ms) {
        if (uart_poll_in(uart_dev, c) == 0) {
            return 0;
        }
        k_yield();
    }

    return -ETIMEDOUT;
}

static void uart_putc(uint8_t c)
{
    uart_poll_out(uart_dev, c);
}

static void send_response(uint8_t rsp)
{
    uart_putc(rsp);
}

static int receive_packet(uint8_t *cmd, uint8_t *data, uint16_t *len)
{
    uint8_t c;
    uint16_t recv_len;
    uint16_t crc_recv, crc_calc;

    /* Receive command */
    if (uart_getc_timeout(&c, rx_timeout) != 0) {
        return -ETIMEDOUT;
    }
    *cmd = c;

    /* Receive length (big-endian) */
    if (uart_getc_timeout(&c, 1000) != 0) return -ETIMEDOUT;
    recv_len = c << 8;
    if (uart_getc_timeout(&c, 1000) != 0) return -ETIMEDOUT;
    recv_len |= c;

    if (recv_len > PACKET_SIZE) {
        return -EINVAL;
    }
    *len = recv_len;

    /* Receive data */
    for (uint16_t i = 0; i < recv_len; i++) {
        if (uart_getc_timeout(&data[i], 1000) != 0) {
            return -ETIMEDOUT;
        }
    }

    /* Receive CRC (big-endian) */
    if (uart_getc_timeout(&c, 1000) != 0) return -ETIMEDOUT;
    crc_recv = c << 8;
    if (uart_getc_timeout(&c, 1000) != 0) return -ETIMEDOUT;
    crc_recv |= c;

    /* Verify CRC */
    crc_calc = image_crc32(data, recv_len) & 0xFFFF;
    if (crc_calc != crc_recv) {
        return -EILSEQ;
    }

    return 0;
}

int update_check_trigger(void)
{
    if (uart_init() != 0) {
        return 0;
    }

    /* Check for sync byte */
    uint8_t c;
    if (uart_getc_timeout(&c, 100) == 0 && c == 0x7F) {
        return 1;
    }

    return 0;
}

int update_receive_image(void)
{
    int ret;
    uint8_t cmd;
    uint8_t data[PACKET_SIZE];
    uint16_t len;
    uint32_t image_size = 0;
    uint32_t bytes_received = 0;
    uint32_t write_addr = APP_START_ADDR;

    if (uart_init() != 0) {
        return BOOT_ERR_NO_IMAGE;
    }

    printk("[UPDATE] Waiting for firmware...\n");
    printk("[UPDATE] Send image within %d seconds\n", UPDATE_TIMEOUT_MS / 1000);

    /* Send ready signal */
    send_response(RSP_READY);
    rx_timeout = UPDATE_TIMEOUT_MS;

    while (1) {
        ret = receive_packet(&cmd, data, &len);

        if (ret == -ETIMEDOUT) {
            printk("[UPDATE] Timeout\n");
            return BOOT_ERR_NO_IMAGE;
        }

        if (ret != 0) {
            printk("[UPDATE] Packet error: %d\n", ret);
            send_response(RSP_NAK);
            continue;
        }

        switch (cmd) {
        case CMD_START:
            if (len >= 4) {
                image_size = (data[0] << 24) | (data[1] << 16) |
                             (data[2] << 8) | data[3];
                printk("[UPDATE] Image size: %u bytes\n", image_size);

                if (image_size > APP_MAX_SIZE) {
                    printk("[UPDATE] Image too large!\n");
                    send_response(RSP_NAK);
                    return BOOT_ERR_BAD_CRC;
                }

                /* Erase application area */
                printk("[UPDATE] Erasing flash...\n");
                ret = flash_erase(APP_START_ADDR, APP_MAX_SIZE);
                if (ret != BOOT_OK) {
                    send_response(RSP_NAK);
                    return ret;
                }

                bytes_received = 0;
                write_addr = APP_START_ADDR;
                rx_timeout = 5000;  /* Shorter timeout for data */
                send_response(RSP_ACK);
            }
            break;

        case CMD_DATA:
            if (image_size == 0) {
                send_response(RSP_NAK);
                break;
            }

            /* Write data to flash */
            ret = flash_write(write_addr, data, len);
            if (ret != BOOT_OK) {
                printk("[UPDATE] Flash write failed at 0x%08X\n", write_addr);
                send_response(RSP_NAK);
                return ret;
            }

            write_addr += len;
            bytes_received += len;

            /* Progress indication */
            if (bytes_received % 4096 == 0) {
                printk("[UPDATE] %u / %u bytes\n", bytes_received, image_size);
            }

            send_response(RSP_ACK);
            break;

        case CMD_END:
            printk("[UPDATE] Received %u bytes\n", bytes_received);

            /* Validate received image */
            if (image_validate(APP_START_ADDR)) {
                printk("[UPDATE] Image validated successfully!\n");
                send_response(RSP_ACK);
                return BOOT_OK;
            } else {
                printk("[UPDATE] Image validation failed!\n");
                send_response(RSP_NAK);
                return BOOT_ERR_BAD_CRC;
            }

        case CMD_INFO:
            printk("[UPDATE] Bootloader v1.0.0\n");
            send_response(RSP_ACK);
            break;

        case CMD_RESET:
            send_response(RSP_ACK);
            k_msleep(100);
            NVIC_SystemReset();
            break;

        default:
            send_response(RSP_NAK);
            break;
        }
    }

    return BOOT_ERR_NO_IMAGE;
}
```

---

## 9. Firmware Update Mechanisms

### Comparison

| Method | Pros | Cons |
|--------|------|------|
| **UART** | Simple, widely available | Slow, requires connection |
| **USB DFU** | Fast, standard protocol | More complex |
| **CAN** | Industrial standard | Requires CAN hardware |
| **Ethernet** | Very fast, remote | Complex, security concerns |
| **Wireless (BLE/WiFi)** | No cables | Complex, security critical |
| **SD Card** | Simple, no connection | Manual intervention |

### Request Update from Application

**In your main application:**
```c
/* Set update flag and reset to enter bootloader */
void request_firmware_update(void)
{
    struct boot_config config;

    /* Read current config */
    flash_read(CONFIG_START_ADDR, &config, sizeof(config));

    /* Initialize if needed */
    if (config.magic != BOOT_CONFIG_MAGIC) {
        memset(&config, 0, sizeof(config));
        config.magic = BOOT_CONFIG_MAGIC;
    }

    /* Set update flag */
    config.update_pending = 1;

    /* Write config */
    flash_erase(CONFIG_START_ADDR, CONFIG_SIZE);
    flash_write(CONFIG_START_ADDR, &config, sizeof(config));

    /* Reset to bootloader */
    NVIC_SystemReset();
}
```

---

## 10. Security Considerations

### Security Levels

| Level | Features | Use Case |
|-------|----------|----------|
| **None** | No verification | Development |
| **Basic** | CRC check | Consumer products |
| **Medium** | Digital signature | Industrial |
| **High** | Encryption + signature | Medical, financial |

### Implementing Signature Verification

```c
#include <mbedtls/sha256.h>
#include <mbedtls/rsa.h>

/* Public key (embed in bootloader) */
static const uint8_t public_key[] = {
    /* Your RSA public key */
};

bool verify_image_signature(uint32_t addr, size_t size,
                            const uint8_t *signature)
{
    uint8_t hash[32];
    mbedtls_sha256_context ctx;
    mbedtls_rsa_context rsa;

    /* Calculate SHA256 hash of image */
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, (uint8_t *)addr, size);
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);

    /* Verify signature */
    mbedtls_rsa_init(&rsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);
    /* ... setup RSA context with public key ... */

    int ret = mbedtls_rsa_pkcs1_verify(&rsa, NULL, NULL,
                                       MBEDTLS_RSA_PUBLIC,
                                       MBEDTLS_MD_SHA256,
                                       32, hash, signature);

    mbedtls_rsa_free(&rsa);

    return ret == 0;
}
```

### Security Best Practices

1. **Always verify** firmware before jumping
2. **Use signatures** in production
3. **Protect bootloader** from accidental erase
4. **Implement rollback protection**
5. **Secure the update channel** (encryption)
6. **Use hardware security** if available (STM32 secure boot)

---

## 11. Testing and Debugging

### Test Cases

| Test | Description |
|------|-------------|
| Valid image jump | Boot with valid application |
| Invalid image | Enter update mode |
| Partial update | Handle interrupted transfer |
| CRC error | Reject corrupted image |
| Update button | Manual update trigger |
| Reset during update | Recovery mechanism |

### Debugging Tips

```bash
# View bootloader memory map
arm-none-eabi-objdump -h build-boot/zephyr/zephyr.elf

# Check section sizes
arm-none-eabi-size build-boot/zephyr/zephyr.elf

# Debug bootloader
west debug -d build-boot

# In GDB:
# (gdb) break main
# (gdb) break jump_to_app
# (gdb) continue
```

### Serial Debug Output

```
=====================================
  Custom Bootloader v1.0.0
  Board: MyCompany EMG Board
=====================================
[BOOT] Validating application at 0x08008000...
[BOOT] Image valid: v1.0.0, size=45678
[BOOT] Jumping to application...
=====================================
```

---

## 12. Production Checklist

### Before Release

- [ ] Bootloader tested with all update methods
- [ ] Image signature verification enabled
- [ ] Rollback protection implemented
- [ ] Bootloader write-protected
- [ ] Update timeout configured
- [ ] Error handling tested
- [ ] Recovery mechanism tested
- [ ] Documentation complete

### Bootloader Versioning

```c
#define BOOTLOADER_VERSION_MAJOR    1
#define BOOTLOADER_VERSION_MINOR    0
#define BOOTLOADER_VERSION_PATCH    0

#define BOOTLOADER_VERSION  ((BOOTLOADER_VERSION_MAJOR << 16) | \
                             (BOOTLOADER_VERSION_MINOR << 8) | \
                             BOOTLOADER_VERSION_PATCH)
```

### Application Linker Script Adjustment

**For application to work with bootloader, adjust start address:**

```cmake
# In application CMakeLists.txt
set(CONFIG_FLASH_LOAD_OFFSET 0x8000)  # Match bootloader size
```

Or in DTS:
```dts
/ {
    chosen {
        zephyr,code-partition = &slot0_partition;
    };
};
```

---

## Quick Start Summary

### Minimal Custom Bootloader

1. Create bootloader project with files above
2. Build: `west build -b mycompany_emg_board -d build-boot bootloader/`
3. Flash bootloader: `west flash -d build-boot`
4. Build application with offset
5. Flash application to APP_START_ADDR

### Using MCUboot

1. Update board DTS with partitions
2. Build MCUboot: `west build -b mycompany_emg_board -d build-mcuboot $ZEPHYR_BASE/../bootloader/mcuboot/boot/zephyr`
3. Flash MCUboot
4. Build and sign application
5. Flash signed application

---

## References

- [MCUboot Documentation](https://docs.mcuboot.com/)
- [Zephyr Flash API](https://docs.zephyrproject.org/latest/reference/peripherals/flash.html)
- [STM32F7 Reference Manual](https://www.st.com/resource/en/reference_manual/rm0410.pdf)
- [ARM Cortex-M Programming](https://developer.arm.com/documentation)
