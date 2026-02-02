# SPI Bus Driver Test Suite Guide

---

## 1. Overview

### Test Without Hardware

| Platform | Command | Use Case |
|----------|---------|----------|
| `native_posix` | `west build -b native_posix` | Unit tests, logic |
| `qemu_cortex_m3` | `west build -b qemu_cortex_m3` | ARM emulation |

### Test Pyramid

```
            /\
           /  \        Hardware Tests (few)
          /────\
         /      \      Integration Tests (some)
        /────────\
       /          \    Unit Tests (many)
      /────────────\
```

---

## 2. Project Structure

```
tests/
└── spi_bus/
    ├── CMakeLists.txt
    ├── prj.conf
    ├── testcase.yaml
    └── src/
        └── main.c
```

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(spi_bus_test)

target_sources(app PRIVATE
    src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../bus/src/spi_bus.c
)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../bus/inc
)
```

### prj.conf

```
CONFIG_ZTEST=y
CONFIG_ZTEST_MOCKING=y
CONFIG_SPI=y
CONFIG_GPIO=y
```

### testcase.yaml

```yaml
tests:
  spi_bus.test:
    tags: spi driver unit
    platform_allow: native_posix qemu_cortex_m3
```

---

## 3. Test File Template

```c
#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include "spi_bus.h"

DEFINE_FFF_GLOBALS;

/*=================================================================
 * MOCKS
 *=================================================================*/

FAKE_VALUE_FUNC(int, spi_write_dt,
                const struct spi_dt_spec *,
                const struct spi_buf_set *);

FAKE_VALUE_FUNC(int, spi_transceive_dt,
                const struct spi_dt_spec *,
                const struct spi_buf_set *,
                const struct spi_buf_set *);

FAKE_VALUE_FUNC(int, gpio_pin_set_dt,
                const struct gpio_dt_spec *,
                int);

/*=================================================================
 * TEST FIXTURES
 *=================================================================*/

static struct spi_dt_spec spi_dev;
static struct gpio_dt_spec gpio_cs;

static void reset_fakes(void *fixture)
{
    RESET_FAKE(spi_write_dt);
    RESET_FAKE(spi_transceive_dt);
    RESET_FAKE(gpio_pin_set_dt);
    FFF_RESET_HISTORY();
}

/*=================================================================
 * NULL POINTER TESTS
 *=================================================================*/

ZTEST(spi_null, test_send_cmd_null)
{
    int ret = spi_send_cmd(NULL, 0x06);
    zassert_equal(ret, -EINVAL);
}

ZTEST(spi_null, test_write_register_null_spi)
{
    struct spi_buf_set dummy = { 0 };
    int ret = spi_write_register(NULL, &gpio_cs, &dummy, &dummy);
    zassert_equal(ret, -EINVAL);
}

ZTEST(spi_null, test_write_register_null_gpio)
{
    struct spi_buf_set dummy = { 0 };
    int ret = spi_write_register(&spi_dev, NULL, &dummy, &dummy);
    zassert_equal(ret, -EINVAL);
}

ZTEST(spi_null, test_read_register_null_spi)
{
    struct spi_buf_set dummy = { 0 };
    int ret = spi_read_register(NULL, &gpio_cs, &dummy, &dummy);
    zassert_equal(ret, -EINVAL);
}

ZTEST(spi_null, test_read_register_null_gpio)
{
    struct spi_buf_set dummy = { 0 };
    int ret = spi_read_register(&spi_dev, NULL, &dummy, &dummy);
    zassert_equal(ret, -EINVAL);
}

ZTEST_SUITE(spi_null, NULL, NULL, reset_fakes, NULL, NULL);

/*=================================================================
 * SUCCESS PATH TESTS
 *=================================================================*/

ZTEST(spi_success, test_send_cmd_returns_spi_result)
{
    spi_write_dt_fake.return_val = 0;

    int ret = spi_send_cmd(&spi_dev, 0x06);

    zassert_equal(ret, 0);
    zassert_equal(spi_write_dt_fake.call_count, 1);
}

ZTEST(spi_success, test_write_register_success)
{
    spi_write_dt_fake.return_val = 0;
    gpio_pin_set_dt_fake.return_val = 0;

    uint8_t addr = 0x01;
    uint8_t data = 0x55;
    struct spi_buf addr_buf = { .buf = &addr, .len = 1 };
    struct spi_buf tx_buf = { .buf = &data, .len = 1 };
    struct spi_buf_set addr_set = { .buffers = &addr_buf, .count = 1 };
    struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };

    int ret = spi_write_register(&spi_dev, &gpio_cs, &addr_set, &tx_set);

    zassert_equal(ret, 0);
    zassert_equal(spi_write_dt_fake.call_count, 2);  /* addr + data */
    zassert_equal(gpio_pin_set_dt_fake.call_count, 2);  /* CS low + high */
}

ZTEST(spi_success, test_read_register_success)
{
    spi_transceive_dt_fake.return_val = 0;
    gpio_pin_set_dt_fake.return_val = 0;

    uint8_t addr = 0x01;
    uint8_t data;
    struct spi_buf addr_buf = { .buf = &addr, .len = 1 };
    struct spi_buf rx_buf = { .buf = &data, .len = 1 };
    struct spi_buf_set addr_set = { .buffers = &addr_buf, .count = 1 };
    struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

    int ret = spi_read_register(&spi_dev, &gpio_cs, &addr_set, &rx_set);

    zassert_equal(ret, 0);
    zassert_equal(spi_transceive_dt_fake.call_count, 1);
    zassert_equal(gpio_pin_set_dt_fake.call_count, 2);  /* CS low + high */
}

ZTEST_SUITE(spi_success, NULL, NULL, reset_fakes, NULL, NULL);

/*=================================================================
 * ERROR HANDLING TESTS
 *=================================================================*/

ZTEST(spi_error, test_send_cmd_propagates_error)
{
    spi_write_dt_fake.return_val = -EIO;

    int ret = spi_send_cmd(&spi_dev, 0x06);

    zassert_equal(ret, -EIO);
}

ZTEST(spi_error, test_write_register_releases_cs_on_addr_error)
{
    spi_write_dt_fake.return_val = -EIO;
    gpio_pin_set_dt_fake.return_val = 0;

    struct spi_buf_set dummy = { 0 };
    int ret = spi_write_register(&spi_dev, &gpio_cs, &dummy, &dummy);

    zassert_equal(ret, -EIO);
    zassert_equal(gpio_pin_set_dt_fake.call_count, 2);  /* CS still released! */
}

ZTEST(spi_error, test_read_register_releases_cs_on_error)
{
    spi_transceive_dt_fake.return_val = -EIO;
    gpio_pin_set_dt_fake.return_val = 0;

    struct spi_buf_set dummy = { 0 };
    int ret = spi_read_register(&spi_dev, &gpio_cs, &dummy, &dummy);

    zassert_equal(ret, -EIO);
    zassert_equal(gpio_pin_set_dt_fake.call_count, 2);  /* CS still released! */
}

ZTEST_SUITE(spi_error, NULL, NULL, reset_fakes, NULL, NULL);

/*=================================================================
 * CS CONTROL TESTS
 *=================================================================*/

ZTEST(spi_cs, test_cs_sequence_write)
{
    spi_write_dt_fake.return_val = 0;
    gpio_pin_set_dt_fake.return_val = 0;

    struct spi_buf_set dummy = { 0 };
    spi_write_register(&spi_dev, &gpio_cs, &dummy, &dummy);

    /* Verify CS sequence: LOW first, HIGH last */
    zassert_equal(gpio_pin_set_dt_fake.arg1_history[0], CHIP_SELECTED);
    zassert_equal(gpio_pin_set_dt_fake.arg1_history[1], CHIP_DESELECTED);
}

ZTEST(spi_cs, test_cs_sequence_read)
{
    spi_transceive_dt_fake.return_val = 0;
    gpio_pin_set_dt_fake.return_val = 0;

    struct spi_buf_set dummy = { 0 };
    spi_read_register(&spi_dev, &gpio_cs, &dummy, &dummy);

    /* Verify CS sequence: LOW first, HIGH last */
    zassert_equal(gpio_pin_set_dt_fake.arg1_history[0], CHIP_SELECTED);
    zassert_equal(gpio_pin_set_dt_fake.arg1_history[1], CHIP_DESELECTED);
}

ZTEST_SUITE(spi_cs, NULL, NULL, reset_fakes, NULL, NULL);
```

---

## 4. Running Tests

### Build and Run

```bash
# Build
west build -b native_posix tests/spi_bus/

# Run
west build -t run
```

### Using Twister

```bash
# Run all tests
west twister -T tests/spi_bus/

# Verbose output
west twister -T tests/spi_bus/ -v

# With coverage
west twister -T tests/spi_bus/ --coverage
```

---

## 5. Test Coverage Checklist

### Functions to Test

| Function | NULL | Success | Error | CS Control |
|----------|------|---------|-------|------------|
| `spi_send_cmd` | ☐ | ☐ | ☐ | N/A |
| `spi_write_register` | ☐ | ☐ | ☐ | ☐ |
| `spi_read_register` | ☐ | ☐ | ☐ | ☐ |

### Test Categories

| Category | What to Test |
|----------|--------------|
| **NULL** | Every pointer parameter |
| **Success** | Normal operation returns 0 |
| **Error** | Error codes propagate correctly |
| **CS** | Always released, correct sequence |

---

## 6. Common Assertions

| Assertion | Usage |
|-----------|-------|
| `zassert_equal(a, b, msg)` | Check values equal |
| `zassert_not_equal(a, b, msg)` | Check values not equal |
| `zassert_true(cond, msg)` | Check condition true |
| `zassert_false(cond, msg)` | Check condition false |
| `zassert_is_null(ptr, msg)` | Check pointer NULL |
| `zassert_not_null(ptr, msg)` | Check pointer not NULL |

---

## 7. Test Naming Convention

```
test_<function>_<scenario>

Examples:
- test_send_cmd_null
- test_send_cmd_success
- test_write_register_null_spi
- test_write_register_releases_cs_on_error
```

---

## 8. Output Files

After running tests:

```
twister-out/
├── twister.json          # Results (JSON)
├── twister.log           # Detailed log
├── twister_report.xml    # JUnit XML (CI/CD)
└── native_posix/
    └── tests/spi_bus/
        ├── handler.log   # Test output
        └── build.log     # Build output
```

---

## 9. Quick Start Commands

```bash
# 1. Create test directory
mkdir -p tests/spi_bus/src

# 2. Copy template files (CMakeLists.txt, prj.conf, main.c)

# 3. Build
west build -b native_posix tests/spi_bus/

# 4. Run
west build -t run

# 5. Check results
cat build/zephyr/zephyr.log
```
