# ADS1298 ↔ STM32F767 Pinout — FES Board

Signal mapping extracted from `FESboard.kicad_sch` netlist on 2026-07-27.

- **U2** = ADS1298 (TQFP-64)
- **U6** = STM32F767ZITx (LQFP-144)

## SPI + Control Signal Table

| Function | Net name | STM32 pin | STM32 AF | ADS1298 pin | Active |
|---|---|---|---|---|---|
| SPI1 SCK | `/STM32_SCLK` | **PA5** (pin 41) | AF5 (SPI1_SCK) | 40 (SCLK) | — |
| SPI1 MISO | `/STM32_MISO` | **PA6** (pin 42) | AF5 (SPI1_MISO) | 43 (DOUT) | — |
| SPI1 MOSI | `/STM32_MOSI` | **PA7** (pin 43) | AF5 (SPI1_MOSI) | 34 (DIN) | — |
| Chip select | `/~{STM32_ADS_CS}` | **PA4** (pin 40) | GPIO output | 39 (CS) | active-low |
| Reset | `/~{STM32_ADS_RESET}` | **PB0** (pin 46) | GPIO output | 36 (RESET) | active-low |
| Power-down | `/~{STM32_ADS_PWDWN}` | **PC5** (pin 45) | GPIO output | 35 (PWDN) | active-low |
| Start convert | `/STM32_ADS_START` | **PB1** (pin 47) | GPIO output | 38 (START) | active-high |
| Data ready | `/~{STM32_ADS_DRDY}` | **PC4** (pin 44) | GPIO input + EXTI | 47 (DRDY) | active-low |

SPI peripheral: **SPI1**. All SPI pins are on port A, alternate function 5.

## Notes for Firmware

- CS is driven manually as a GPIO — do **not** use SPI1_NSS hardware CS, because the ADS1298 needs CS held low across multiple bytes per transaction (register reads/writes and continuous data reads span >1 byte).
- DRDY is active-low: falling edge = new sample available. Configure EXTI4 (PC4) on the falling edge.
- On boot: pull PWDN and RESET **high** first, then release CS high, then start SPI clocking. Datasheet requires ≥18 t_CLK after RESET release before first SPI command.
- SPI mode: **CPOL=0, CPHA=1** (SPI mode 1). Datasheet: DIN latched on SCLK rising edge, DOUT changed on SCLK falling edge.
- Max SCLK: 20 MHz per ADS1298 datasheet. Start at 1–4 MHz during bring-up.

## Zephyr Device Tree Skeleton

Place this in your board overlay or `.dts` file (adjust the parent node to match your Zephyr STM32F767 board file):

```dts
&pinctrl {
    spi1_sck_pa5: spi1_sck_pa5 {
        pinmux = <STM32_PINMUX('A', 5, AF5)>;
        bias-pull-down;
    };
    spi1_miso_pa6: spi1_miso_pa6 {
        pinmux = <STM32_PINMUX('A', 6, AF5)>;
    };
    spi1_mosi_pa7: spi1_mosi_pa7 {
        pinmux = <STM32_PINMUX('A', 7, AF5)>;
        bias-pull-down;
    };
};

&spi1 {
    status = "okay";
    pinctrl-0 = <&spi1_sck_pa5 &spi1_miso_pa6 &spi1_mosi_pa7>;
    pinctrl-names = "default";
    cs-gpios = <&gpioa 4 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>;

    ads1298: ads1298@0 {
        compatible = "ti,ads1298";
        reg = <0>;
        spi-max-frequency = <4000000>;           /* 4 MHz for bring-up; up to 20 MHz per datasheet */
        spi-cpol;                                /* CPOL = 0 in Zephyr is default; set only if node uses mode 2/3 */
        spi-cpha;                                /* CPHA = 1 → SPI mode 1 */

        /* Control signals (all active-low except START) */
        reset-gpios  = <&gpiob 0 GPIO_ACTIVE_LOW>;
        powerdown-gpios = <&gpioc 5 GPIO_ACTIVE_LOW>;
        start-gpios  = <&gpiob 1 GPIO_ACTIVE_HIGH>;
        drdy-gpios   = <&gpioc 4 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>;
    };
};
```

### Notes on the snippet above

- `spi-cpol` and `spi-cpha` together select **SPI mode 3** in Zephyr terms, not mode 1. For **mode 1** (which the ADS1298 needs), specify `spi-cpha` only, and omit `spi-cpol`. Correct line:
  ```dts
  spi-cpha;   /* mode 1: CPOL=0, CPHA=1 */
  ```
- The `compatible = "ti,ads1298"` string assumes you're using or writing a Zephyr driver with that binding. If none exists yet, use `compatible = "vnd,spi-device"` as a placeholder and access it via the generic SPI API.
- `drdy-gpios` combined with `GPIO_PULL_UP` gives you a stable idle level; the Zephyr GPIO interrupt API will handle the falling-edge trigger for you.
- For a custom driver, you'll want to configure the DRDY line as `GPIO_INT_EDGE_TO_INACTIVE` (falling edge, since active-low means "inactive" is high, and the *transition to active* is the falling edge). Or use `GPIO_INT_EDGE_FALLING` directly.

## Bring-up Sequence Reference

Once the device tree is built, the firmware bring-up order:

1. Configure all GPIOs: PWDN=1, RESET=1, START=0, CS=1 (all deasserted).
2. Wait for supplies to settle (~10 ms).
3. Pulse RESET low for ≥ 1 µs, then high. Wait ≥ 18 t_CLK ≈ 9 µs @ 2.048 MHz.
4. Send `SDATAC` (0x11) to exit continuous data mode.
5. Read register 0x00 (ID). Expect **0x92** for ADS1298.
6. Write CONFIG1/2/3, CHnSET registers per your channel setup.
7. Send `START` (0x08) opcode, or pulse the START GPIO high.
8. Send `RDATAC` (0x10) for continuous data mode.
9. On each DRDY falling edge, read 27 bytes (3 status + 8 × 3 channel bytes).

## ADS1298 ECG-FE Eval Board — Input Channel Map (for EMG bench testing)

> This section is about the **SBAU171 ECG-FE eval board (REVB)**, *not* the custom
> FES board above. It maps the board's `ECG_*` input labels to ADS1298 channels.

The eval board's inputs arrive on the **DB15 connector (J1)**, pass through a
single-pole RC filter, and are routed to the ADS1298 channels by jumpers
**JP26–JP33**. `ECG_V1`–`ECG_V6` are the six **precordial (chest) ECG lead
inputs**; `ECG_RA/LA/LL` are the limb electrodes. In the **factory ECG config**
each `ECG_Vn` goes to a channel's **+** input while that channel's **−** input is
tied to **WCT** (Wilson Central Terminal, internal = (RA+LA+LL)/3).

| ADS ch | + input (INxP) | − input (INxM) | Good for standalone EMG? |
|---|---|---|---|
| CH1 | ECG_V6 | WCT | no (− on WCT) |
| **CH2** | **ECG_LA** | **ECG_RA** | **yes — bipolar LA−RA** |
| **CH3** | **ECG_LL** | **ECG_RA** | **yes — bipolar LL−RA** |
| CH4 | ECG_V2 | WCT | no (− on WCT) |
| CH5 | ECG_V3 | WCT | no (− on WCT) |
| CH6 | ECG_V4 | WCT | no (− on WCT) |
| CH7 | ECG_V5 | WCT | no (− on WCT) |
| CH8 | ECG_V1 | WCT | no (− on WCT) |

**EMG/ECG testing:** use **CH2** or **CH3** — the only channels with *both* inputs
on real electrode terminals. The `Vn` channels reference WCT, which has no stable
value without limb electrodes attached, so they need a jumper change to do a clean
bipolar measurement. Connect the two electrodes to **LA (+)** and **RA (−)** on J1
for CH2, or **LL (+)** and **RA (−)** for CH3, and a bias/reference electrode to
**RL (RLD)**.

**RL is not optional.** The firmware enables right-leg drive from avg(RA, LA, LL),
but the loop only closes if RLDOUT actually reaches the subject or simulator. With
RL unconnected there is no common-mode feedback, and mains hum couples straight
through the amplifier's finite CMRR — which is the usual reason a trace looks noisy
next to a commercial instrument that drives its own reference.

**The firmware streams CH2 and CH3 only** (`EMG_CHANNEL_MASK = 0x06` in
`firmware/src/main.c`); the rest are powered down with their inputs shorted. This
is deliberate: with a 3-lead source, V6 and V2 are open circuits, and on this
DC-coupled front end an open PGA input rails and streams full-scale mash — which
also disturbs the good channels through the shared RLD loop. Override with
`-DEMG_CHANNEL_MASK=0x0F` for the custom FES board. The mask travels in the DATA
frame, so the viewer labels the traces LEAD I and LEAD II rather than counting up
from CH1.

Source: SBAU171D §2.1 (jumper table JP26–JP33) and §4.6 (lead configuration).

## Source

Netlist regenerated with:

```bash
kicad-cli sch export netlist --format kicadsexpr \
    -o scratch_netlist_full.net FESboard/FESboard.kicad_sch
```

If the schematic changes (new refdes, moved signals), rerun and regenerate this file.
