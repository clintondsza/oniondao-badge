# Complete Pinout Reference

This is a markdown mirror of the interactive table in
[`pcb/null badge.html`](../pcb/null%20badge.html). Open the HTML in a
browser for filterable / searchable views — this document is here so PR
reviewers and search tools can read the pin map without rendering HTML.

**28 GPIO used** · **2 swappable modules** (see [`MODULES.md`](MODULES.md))

Legend: **IN** = input, **OUT** = output, **I/O** = bidirectional.

---

## Core · UART · Boot

CH340C USB-UART bridge, BOOT button, peripheral power gate.

| GPIO | Net | Dir | Interface | Peripheral | Notes |
|------|-----|-----|-----------|------------|-------|
| `GPIO0`  | `IO0` | IN  | BOOT       | BOOT button (SW_Push) + auto-reset Q1/Q2 | **STRAPPING**. LOW @ power-on → download mode. HIGH → normal boot. |
| `GPIO43` | `TX0` | OUT | UART0 TX   | U15 CH340C RXD (pin 6)                    | Debug / programming TX. Series R1 = 220 Ω. |
| `GPIO44` | `RX0` | IN  | UART0 RX   | U15 CH340C TXD (pin 7)                    | Debug / programming RX. |
| `GPIO18` | `PWR` | OUT | GPIO       | Q5 SS8050 (R22 1 kΩ) → peripheral VCC      | HIGH = enable PWR rail. Assert before talking to gated peripherals. |

## I²C Bus — Security Chip + IO Expander

Single shared bus. Fast Mode (400 kHz) max.

| GPIO | Net | Dir | Interface | Peripheral | Notes |
|------|-----|-----|-----------|------------|-------|
| `GPIO9`  | `SCL`   | OUT | I²C SCL    | U10 TCA9534 pin 14 + U16 ATECC608B pin 6 | Clock. External pull-ups on board. |
| `GPIO10` | `SDA`   | I/O | I²C SDA    | U10 TCA9534 pin 13 + U16 ATECC608B pin 5 | Data. TCA9534 @ `0x20`. |
| `GPIO1`  | `PBINT` | IN  | GPIO IRQ   | U10 TCA9534 INT → PB1..PB6 buttons         | Active LOW. `attachInterrupt(FALLING)` then read TCA9534 reg `0x00`. |
| `GPIO8`  | `SE_EN` | OUT | GPIO       | U16 ATECC608B-SSHDA enable                 | HIGH = active. Power-gate when idle for low-power modes. |

## Display SPI — J4 Socket (24-pin)

E-Ink or TFT display socket.

| GPIO | Net | Dir | Interface | J4 Pin | Notes |
|------|-----|-----|-----------|--------|-------|
| `GPIO14` | `RS`   | OUT | DISP RST  | 3  | Hardware reset. Pulse LOW ≥ 10 ms during init. |
| `GPIO13` | `DC`   | OUT | DISP D/C  | 11 | HIGH = Data, LOW = Command. Toggle per transfer. |
| `GPIO12` | `CS`   | OUT | DISP CS   | 12 | SPI Chip Select. Active LOW. |
| `GPIO11` | `SCK`  | OUT | DISP SCK  | 13 | SPI Clock. |
| `GPIO21` | `BUSY` | IN  | DISP BUSY | 9  | HIGH while refresh in flight. Poll or interrupt. |
| `GPIO17` | `SDI`  | OUT | DISP MOSI | 23 | SPI MOSI — data to display. |

J4 also carries VCC, GND, and module-specific lines (GDR booster
enable, RESE, etc.) — see the schematic for the full 24-pin breakout.

## Sound Module — PDM / I²S

`NS4168` Class-D amplifier + `SPH0641` PDM microphone. The Sound
Module shares its pins with the CC1101 Module — only one can be
active at a time. The three columns below are the alternative GPIO
selections depending on **which side port** the module is plugged
into / configured for.

| Signal | IO L1 | IO L2 | IO R | Dir | Interface | Notes |
|--------|-------|-------|------|-----|-----------|-------|
| Mic data       | `G48` | `G40` | `G38` | IN  | PDM       | SPH0641 DATA. |
| Bit clock      | `G47` | `G41` | `G39` | OUT | I²S / PDM | BCLK to NS4168 + SPH0641. |
| Word select    | `G19` | `G42` | `G16` | OUT | I²S WS/LR | LRCK to NS4168 + SPH0641 SELECT. |
| Audio data out | `G42` | `G19` | `G15` | OUT | I²S SDO   | I²S data to NS4168 SD pin. |
| Control        | `G41` | `G47` | `G07` | OUT | I²S CTRL  | NS4168 CTRL pin (mode / shutdown). |

## CC1101 Module — Sub-GHz SPI Radio

TI CC1101 (315 / 433 / 868 / 915 MHz ISM). Shares the same GPIOs as the
Sound Module above — pick one.

| Signal     | IO L1 | IO L2 | IO R | Dir | Interface | Notes |
|------------|-------|-------|------|-----|-----------|-------|
| SPI MOSI   | `G48` | `G40` | `G38` | I/O | SPI MOSI  | Data to CC1101. |
| SPI CLK    | `G47` | `G41` | `G39` | OUT | SPI CLK   | SPI clock. |
| SPI CS     | `G19` | `G42` | `G16` | OUT | SPI CS    | Active LOW. |
| SPI MISO   | `G42` | `G19` | `G15` | OUT | SPI MISO  | CC1101 SO. |
| GDO0 IRQ   | `G41` | `G47` | `G07` | IN  | GPIO IRQ  | CC1101 GDO0; behaviour set via register. |

## Expansion Ports

### Left Port — J8 (10-pin)

| Pin | Net  | GPIO |
|-----|------|------|
| 1   | GND  | — |
| 2   | VCC  | — |
| 3   | `G48` | `GPIO48` |
| 4   | `G47` | `GPIO47` |
| 5   | `G19` | `GPIO19` |
| 6   | `G42` | `GPIO42` |
| 7   | `G41` | `GPIO41` |
| 8   | `G40` | `GPIO40` |
| 9   | VCC  | — |
| 10  | GND  | — |

### Right Port — J10 (10-pin)

| Pin | Net  | GPIO |
|-----|------|------|
| 1   | GND  | — |
| 2   | VCC  | — |
| 3   | `G38` | `GPIO38` |
| 4   | `G39` | `GPIO39` |
| 5   | `G16` | `GPIO16` |
| 6   | `G15` | `GPIO15` |
| 7   | `G07` | `GPIO7`  |
| 8   | `G06` | `GPIO6`  |
| 9   | `G05` | `GPIO5`  |
| 10  | `G04` | `GPIO4`  |

---

## Cross-reference: GPIO → Function

Sorted by GPIO number. `*` = module-shared pin.

| GPIO | Primary use |
|------|-------------|
| 0    | BOOT / strapping / user button |
| 1    | TCA9534 INT (`PBINT`) |
| 4    | J10 right port |
| 5    | J10 right port |
| 6    | J10 right port |
| 7    | J10 right port (CC1101 GDO0 / I²S CTRL — variant R)* |
| 8    | ATECC608B enable (`SE_EN`) |
| 9    | I²C SCL |
| 10   | I²C SDA |
| 11   | Display SPI SCK |
| 12   | Display SPI CS |
| 13   | Display DC |
| 14   | Display RST |
| 15   | J10 right port (CC1101 MISO / I²S SDO — variant R)* |
| 16   | J10 right port (CC1101 CS / I²S WS — variant R)* |
| 17   | Display SPI MOSI (SDI) |
| 18   | Peripheral power-gate (`PWR` / Q5) |
| 19   | J8 left port (variant L1)* |
| 21   | Display BUSY |
| 38   | J10 right port (variant R)* |
| 39   | J10 right port (variant R)* |
| 40   | J8 left port (variant L2)* |
| 41   | J8 left port* |
| 42   | J8 left port* |
| 43   | UART0 TX (CH340C) |
| 44   | UART0 RX (CH340C) |
| 47   | J8 left port* |
| 48   | J8 left port* |
