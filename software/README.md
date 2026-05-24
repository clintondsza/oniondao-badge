# Software

Community-driven firmware, examples, guides, and mods for the NULL City
Badge. **PRs welcome** — see [`../docs/CONTRIBUTING.md`](../docs/CONTRIBUTING.md).

## Folders

```
software/
├── examples/   ← minimal, single-feature sketches
├── guides/     ← end-to-end tutorials (markdown + code)
└── mods/       ← full firmware projects / forks / CTF challenges
```

### `examples/`

Drop-in single-file or single-folder demos. Each one should focus on
**one** subsystem: I²C buttons, e-paper hello-world, CC1101 RX, audio
playback, secure-element signing, etc. The reader should be able to flash
it and see the feature work in under five minutes.

### `guides/`

Long-form, narrative documentation. "Build a 433 MHz spectrum analyser",
"Wire up a CTF challenge using the ATECC608B", "Port MicroPython to the
badge". Markdown lives alongside any reference code.

### `mods/`

Full firmware projects. Forks, variants, conference-specific builds, your
personal daily-driver firmware. One folder per project.

## Choosing a toolchain

The badge is plain ESP32-S3 + CH340C, so **any** ESP32-S3 toolchain works.
Pick what you already know:

- **Arduino-ESP32** — easiest entry point.
- **ESP-IDF** — full control, recommended for non-trivial mods.
- **PlatformIO** — best DX if you're shipping multi-target projects.
- **MicroPython / CircuitPython** — quick scripting, REPL over the
  CH340C serial port.
- **Rust** (`esp-hal`, `esp-idf-svc`) — production-grade ergonomics.

## Quick reference for code

| Constant | Value |
|----------|-------|
| `PIN_BOOT_BTN` | 0  |
| `PIN_PBINT`    | 1  |
| `PIN_SE_EN`    | 8  |
| `PIN_I2C_SCL`  | 9  |
| `PIN_I2C_SDA`  | 10 |
| `PIN_DISP_SCK` | 11 |
| `PIN_DISP_CS`  | 12 |
| `PIN_DISP_DC`  | 13 |
| `PIN_DISP_RST` | 14 |
| `PIN_DISP_SDI` | 17 |
| `PIN_PWR_EN`   | 18 |
| `PIN_DISP_BUSY`| 21 |
| `PIN_UART0_TX` | 43 |
| `PIN_UART0_RX` | 44 |
| `I2C_ADDR_TCA9534` | `0x20` |

Copy-paste the constants you need; full table in
[`../docs/PINOUT.md`](../docs/PINOUT.md).
