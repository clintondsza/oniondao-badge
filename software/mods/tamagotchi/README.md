# Tamagotchi

A virtual pet for the NULL City Badge. Keep your creature alive through five
evolution stages by feeding, playing, sleeping, and giving medicine.

**Module variant required:** None — base badge only (e-paper display + TCA9534
button expander). The ATECC608B and CC1101 are not used.

## Gameplay

| Stage  | Age (real time) | Notes |
|--------|----------------|-------|
| EGG    | 0 – 1.5 h      | Incubating; no interaction yet |
| BABY   | 1.5 – 24 h     | Basic needs |
| CHILD  | 1 – 3 days     | Arms appear |
| TEEN   | 3 – 7 days     | Legs and more expression |
| ADULT  | 7 days+        | Confident stance, star eyes when happy |

**Stats** (all 0–10, shown as fill bars):

| Bar | Decays when … | Consequence of zero |
|-----|---------------|---------------------|
| HUN (hunger) | Awake, every 30 min | Health −2 / tick |
| HAP (happiness) | Always, every 30 min | Health −1 / tick |
| NRG (energy) | Awake, every 30 min | — (pet just looks tired) |
| HLT (health) | Starvation / sadness / 5 % random sickness | Death at 0 |

**Actions** (navigate with LEFT / RIGHT or UP / DOWN, confirm with SELECT):

- **FEED** — hunger +4, happiness +1
- **PLAY** — happiness +4, hunger −1, energy −2
- **SLEEP** — starts sleep mode; badge deep-sleeps and restores energy over time (NRG +3 / 30 min). Press any button to wake early.
- **MED** — health +4, happiness −1

## Power / time model

The badge deep-sleeps between interactions. A 30-minute hardware timer
wakes it silently to tick stats, then it goes back to sleep — no display
refresh, minimal power draw. Button press wakes it for interaction.

If the pet dies, press SELECT to hatch a new egg.

## Build & flash

```bash
cd software/mods/tamagotchi
pio run -t upload
pio device monitor
```

Requires: PlatformIO, `espressif32` platform, `/dev/ttyUSB0`.

## GPIOs used

| Signal | GPIO | Role |
|--------|------|------|
| PWR | 18 | Peripheral power rail — held HIGH during setup, released on sleep |
| SCL | 9 | I²C clock (TCA9534 button expander @ 0x20) |
| SDA | 10 | I²C data |
| EPD_MOSI | 17 | E-paper SPI data |
| EPD_SCK | 11 | E-paper SPI clock |
| EPD_CS | 12 | E-paper chip select (active LOW) |
| EPD_DC | 13 | E-paper data/command |
| EPD_RST | 14 | E-paper reset (active LOW) |
| EPD_BUSY | 21 | E-paper busy flag |
| BTN_IRQ | 1 | Button interrupt (EXT0 wakeup source) |

Full reference: [`docs/PINOUT.md`](../../../../docs/PINOUT.md).

## Demo

The e-paper display shows an onion-shaped creature (onion bulb head with
sprouts) that evolves visually across five life stages. When actions are
confirmed the creature performs a 2-frame dancing animation using the SSD1680
partial-refresh window (~500 ms per frame, no full-panel flicker). Press the
CANCEL button at any time to advance the stage instantly for testing.
