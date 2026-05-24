# Hardware Overview

Deep dive on every subsystem on the NULL City Badge. Pair this with
[`PINOUT.md`](PINOUT.md) (where every GPIO lives) and
[`MODULES.md`](MODULES.md) (what the swappable RF / audio modules do).

---

## 1. Main MCU — ESP32-S3-WROOM-1

- Espressif ESP32-S3 dual-core Xtensa LX7 @ up to 240 MHz
- 2.4 GHz Wi-Fi b/g/n + Bluetooth LE 5.0
- Native USB OTG (not exposed — USB-C goes through CH340C)
- Module integrates the antenna, PSRAM (variant-dependent), and flash

### Strapping pins to be careful with

| GPIO | Function | Notes |
|------|----------|-------|
| `GPIO0`  | BOOT mode select | LOW at power-on → download mode. Wired to the BOOT button + auto-reset transistor pair. |
| `GPIO3`  | JTAG signal source | not used on this board |
| `GPIO45` | VDD_SPI voltage | factory-set, do not drive |
| `GPIO46` | Boot ROM messages | factory-set, do not drive |

If you write firmware that reconfigures `GPIO0` as an output, **release it
to input before the next reset** or you'll brick the auto-flash workflow
until someone holds BOOT manually.

## 2. USB & Programming

The USB-C jack feeds a **WCH CH340C** USB ↔ UART bridge:

- `CH340C.RXD` ← `GPIO43` (`TX0`) via series resistor R1 = 220 Ω
- `CH340C.TXD` → `GPIO44` (`RX0`)
- `DTR` / `RTS` drive transistors **Q1** and **Q2** to toggle `EN` and
  `GPIO0` — this is the classic NodeMCU auto-reset circuit. esptool and
  Arduino-IDE handle this transparently.

If the host doesn't toggle DTR/RTS the way the chip expects (some USB
hubs, some serial monitors), the manual recipe is:

1. Hold **BOOT** (`GPIO0`)
2. Tap **RESET**
3. Release **BOOT**
4. Flash

## 3. Power

The board has two rails:

- **VCC** — always-on rail powering the ESP32 module and the CH340C.
- **PWR** — switched rail for peripherals (display, RF mod, audio mod,
  etc.) gated by **Q5 (SS8050)** driven through R22 = 1 kΩ from `GPIO18`.

`GPIO18` HIGH = peripherals on. Drop it LOW to deep-sleep-friendly states.
Always assert `GPIO18` HIGH **before** talking to power-gated peripherals,
and wait long enough for them to come up (most chips: a few milliseconds
plus their own POR delay).

## 4. I²C Bus

Single shared I²C bus at `GPIO9` (SCL) / `GPIO10` (SDA). External pull-ups
are on the board. Maximum advertised speed: **400 kHz** (Fast Mode).

Devices on the bus:

| Address | Chip | Purpose |
|---------|------|---------|
| `0x20`  | **TCA9534** | 8-bit IO expander — drives PB1..PB6 user buttons and exposes `INT` on `GPIO1` (`PBINT`). |
| (factory) | **ATECC608B-SSHDA** | Hardware secure element — ECC signing / key storage. Power-gated via `SE_EN` on `GPIO8`. The ATECC608B's I²C address is set in its config zone; default boards ship with `0xC0` (7-bit `0x60`). |

### TCA9534 register quick-ref

| Reg | Name | Notes |
|-----|------|-------|
| `0x00` | Input port    | Read to see current pin states. |
| `0x01` | Output port   | Write to drive outputs. |
| `0x02` | Polarity inv. | Leave at `0x00` unless inverting reads. |
| `0x03` | Configuration | `1` = input, `0` = output. Buttons are inputs. |

### Recommended I²C pattern

```
Wire.begin(SDA=10, SCL=9, 400000);
pinMode(PBINT=1, INPUT_PULLUP);
attachInterrupt(PBINT, isr, FALLING);

// in isr() — read register 0x00, decode bits, clear by reading.
```

## 5. Boot / Reset Circuit

- **BOOT button** → `GPIO0` (strapping)
- **Auto-reset** Q1/Q2 → `EN` + `GPIO0`, driven by CH340C `DTR`/`RTS`

The same `GPIO0` button is reused as a **user button** once the firmware
is running. Don't drive `GPIO0` low from firmware at startup or you'll
trap yourself in bootloader mode after the next reset.

## 6. Display Socket — J4 (24-pin)

A 24-pin **Conn_01x24** socket for E-Ink or TFT display modules. Pinout
of the GPIO side:

| GPIO | Net | J4 Pin | Function |
|------|-----|--------|----------|
| `GPIO14` | `RS`   | 3  | Display RESET (active LOW; pulse ≥ 10 ms during init) |
| `GPIO21` | `BUSY` | 9  | Display busy — HIGH while refreshing |
| `GPIO13` | `DC`   | 11 | Data / Command (HIGH = data, LOW = command) |
| `GPIO12` | `CS`   | 12 | SPI Chip Select (active LOW) |
| `GPIO11` | `SCK`  | 13 | SPI Clock |
| `GPIO17` | `SDI`  | 23 | SPI MOSI — data to display |

The remaining J4 pins carry VCC, GND, and module-specific signals (GDR,
RESE for e-paper booster, etc.) — see the schematic for the connector
breakout.

> For E-Ink modules, the typical flow is: pulse `RST` low → wait `BUSY`
> goes low → send command (DC=0) → send data bytes (DC=1) → trigger
> refresh → poll `BUSY` until it returns LOW.

## 7. Button Matrix — PB1..PB6

The six tactile buttons are **not** wired directly to the ESP32. They
sit on the TCA9534 IO expander's `P0..P5` and pull a shared `INT` line
that becomes `PBINT` on `GPIO1`.

Firmware pattern:

1. Configure TCA9534 reg `0x03` (config) so `P0..P5` are inputs.
2. Read reg `0x00` (input port) once at boot to clear any stale INT.
3. `attachInterrupt(PBINT, isr, FALLING)`.
4. Inside the ISR (or a deferred task), re-read reg `0x00` and decode.

## 8. Secure Element — ATECC608B

Microchip ECC-P256 chip. Useful for:

- Per-badge unique key pairs
- Signed-message attestations (CTF flags, identity proofs)
- Hardware RNG

The chip is **power-gated** by `GPIO8` (`SE_EN`). Drive HIGH, wait
≥ 1 ms (Tpu = power-up delay), then talk to it. Pull LOW when idle to
save current.

Use Microchip's CryptoAuthLib or the lightweight Arduino libraries.

## 9. Expansion Ports — J8 (Left), J10 (Right)

Two 10-pin headers along the sides of the board. Pin 1 of each is GND,
pin 2 is VCC, then GPIOs follow. These are how external modules,
lanyards, and the swappable RF / audio mods attach.

| Pin | J8 (Left) | J10 (Right) |
|-----|-----------|-------------|
| 1   | GND       | GND |
| 2   | VCC       | VCC |
| 3   | `GPIO48`  | `GPIO38` |
| 4   | `GPIO47`  | `GPIO39` |
| 5   | `GPIO19`  | `GPIO16` |
| 6   | `GPIO42`  | `GPIO15` |
| 7   | `GPIO41`  | `GPIO7`  |
| 8   | `GPIO40`  | `GPIO6`  |
| 9   | VCC       | `GPIO5`  |
| 10  | GND       | `GPIO4`  |

The **Sound Module** and the **CC1101 Module** both target this set of
pins. See [`MODULES.md`](MODULES.md) for the mapping and the rules about
mutual exclusion.

## 10. Mechanical / 3D

- Full board STEP: [`pcb/null-city-badge.step`](../pcb/null-city-badge.step)
- Lanyard / additional 3D assets: [`pcb/3d/`](../pcb/3d/)

## 11. Manufacturing Outputs

Already-generated fabrication outputs are in
[`pcb/production/`](../pcb/production/):

- `null-city-badge.zip` — gerbers + drills + P&P
- `netlist.ipc` — IPC-D-356 netlist
- `bom-list_*.xlsx` — per-variant BOMs

To re-generate: open the `.kicad_pcb` in KiCad 9.0.3 → File → Fabrication
Outputs.
