# Swappable Modules

The NULL City Badge ships with **two flexible module slots** that share a
common set of GPIOs on the side expansion ports. The schematics for each
module live next to the main board:

| Module | Schematic | What it adds |
|--------|-----------|--------------|
| **CC1101 Sub-GHz** | [`pcb/CC1101_MOD.kicad_sch`](../pcb/CC1101_MOD.kicad_sch) | 315 / 433 / 868 / 915 MHz SPI radio |
| **Sound (I²S + PDM)** | [`pcb/SOUND_MOD.kicad_sch`](../pcb/SOUND_MOD.kicad_sch) | NS4168 Class-D amp + SPH0641 PDM mic |
| **SD Card** | [`pcb/SDCARD_MOD.kicad_sch`](../pcb/SDCARD_MOD.kicad_sch) | microSD slot (SPI / SDIO) |
| **LoRa** | [`pcb/LORA_MOD.kicad_sch`](../pcb/LORA_MOD.kicad_sch) | SX1276-style long-range radio |

Per-variant BOMs are in [`pcb/production/`](../pcb/production/) (e.g.
`bom-list_cc1101.xlsx`, `bom-list_sound.xlsx`).

---

## Why mutual exclusion matters

The CC1101 and Sound modules **both** map their primary signals onto the
same physical pins on J8 / J10. The ESP32 cannot drive a single GPIO as
both an I²S BCLK and an SPI SCK simultaneously. So:

- Decide at build time (compile-time `#define`) or at boot (read a
  module-ID resistor, EEPROM tag, or NVS flag) which module is installed.
- Initialise **only** the matching peripheral.
- Leave the unused pins as inputs with no pull, or tri-state them, so
  the absent module doesn't fight the bus.

If you ship a single firmware image that supports both, gate the
initialisation behind a runtime detect — for example, attempt to read the
CC1101 `PARTNUM` register; if it returns `0x00` you're probably on the
Sound variant.

## Shared GPIO map

The "variants" column in [`PINOUT.md`](PINOUT.md) reflects the three
populations the schematic supports — **L1**, **L2**, and **R**. They
differ only in **which side port** the module physically attaches to.
Pick the column that matches the assembled board.

| Function on **CC1101** | Function on **Sound** | L1 GPIO | L2 GPIO | R GPIO |
|------------------------|-----------------------|---------|---------|--------|
| SPI MOSI               | Mic PDM data          | 48 | 40 | 38 |
| SPI CLK                | I²S/PDM bit clock     | 47 | 41 | 39 |
| SPI CS                 | I²S WS / LRCK         | 19 | 42 | 16 |
| SPI MISO               | I²S audio data out    | 42 | 19 | 15 |
| GDO0 IRQ               | I²S CTRL              | 41 | 47 |  7 |

> Most production boards use the **L1** (left side, primary) wiring.
> Check your specific board's silkscreen or assembled module before
> picking a column.

## CC1101 — Sub-GHz SPI Radio

- ISM bands: 315 / 433 / 868 / 915 MHz (depends on installed matching
  network and antenna).
- Standard 4-wire SPI plus one IRQ line (GDO0).
- Power-gated via the main `PWR` rail — assert `GPIO18` HIGH before use.
- `CS` is active LOW. Hold it across whole register reads/writes.

Suggested libraries:
- C / Arduino: [`SmartRC-CC1101-Driver-Lib`](https://github.com/LSatan/SmartRC-CC1101-Driver-Lib)
- Rust: `cc1101`
- MicroPython: community `cc1101.py` ports

Example use cases: SubGHz scanning, ASK/OOK replay (with authorisation
on whatever you're testing — this is your responsibility), wireless
sensor links, low-power telemetry.

## Sound Module — NS4168 + SPH0641

- **NS4168** — I²S Class-D amplifier driving a small speaker pad on
  the board. Inputs: BCLK, LRCK, SD, CTRL (mode/shutdown).
- **SPH0641** — Knowles PDM digital microphone. Outputs PDM data
  clocked by BCLK; SELECT (= LRCK) picks left vs right channel.
- Same `PWR` rail gating via `GPIO18`.

Typical ESP32-S3 I²S configuration:

```cpp
i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,
                                                       I2S_ROLE_MASTER);
i2s_new_channel(&chan_cfg, &tx, &rx);

i2s_std_config_t std_cfg = {
  .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(44100),
  .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                  I2S_SLOT_MODE_STEREO),
  .gpio_cfg = {
    .mclk = I2S_GPIO_UNUSED,
    .bclk = GPIO_NUM_47,   // variant L1 — adjust per board
    .ws   = GPIO_NUM_19,
    .dout = GPIO_NUM_42,
    .din  = GPIO_NUM_48,
  },
};
```

For PDM-mic capture, swap the std_cfg for `i2s_pdm_rx_config_t`.

## SD Card Module

- microSD slot on a separate module — see `SDCARD_MOD.kicad_sch`.
- Wiring is variant-specific; check the schematic for which side-port
  GPIOs it uses on your build. Typically SPI mode (CS, SCK, MOSI, MISO).

## LoRa Module

- SX1276-class long-range radio (sub-GHz). Variant-specific antenna and
  matching network.
- SPI bus + DIO interrupt + reset line — see `LORA_MOD.kicad_sch`.

## Authoring a new module

Want to design your own (NFC reader, OLED, joystick, eInk variant)?

1. The mechanical / pin contract is the **J8 / J10 side port** —
   10 pins, GND/VCC on 1/2/9/10 (left) or 1/2 + GPIO (right). See
   [`HARDWARE.md`](HARDWARE.md) §9.
2. All your IO comes from the GPIO pins on that port. If you need more
   than one side, you'll consume both ports.
3. Avoid driving GPIOs that another (already-assembled) module might
   contend for. Document the conflict matrix in your module's README.
4. Open a PR with:
   - the new `*_MOD.kicad_sch` in `pcb/`,
   - a BOM under `pcb/production/`,
   - and a firmware example or guide under `software/`.
