<!-- Thanks for contributing to the NULL City Badge!
     See docs/CONTRIBUTING.md for the full guidelines. -->

## What is this?

<!-- One paragraph. What does this PR add or change? -->

## Bucket

<!-- Tick one. -->

- [ ] `software/examples/` — minimal, single-feature
- [ ] `software/guides/` — narrative tutorial
- [ ] `software/mods/` — full firmware project / fork
- [ ] `pcb/` — hardware module / schematic
- [ ] `docs/` — documentation fix or addition

## Hardware requirements

- **Module variant needed:** <!-- CC1101 / Sound / none -->
- **GPIOs touched:** <!-- list using the names from docs/PINOUT.md -->
- **Conflicts with other modules?** <!-- yes/no, explain -->

## Build & test

<!-- Toolchain (Arduino / ESP-IDF / PlatformIO / MicroPython / Rust / …),
     version, and the exact command(s) you ran. -->

```
# e.g.
idf.py set-target esp32s3
idf.py build flash monitor
```

## Demo

<!-- Screenshot, GIF, or short video if your change has a visible result. -->

## Checklist

- [ ] My folder has a `README.md` (answers: what, which module, which GPIOs, how to build).
- [ ] My code builds with the toolchain I claim it does.
- [ ] I cross-checked `docs/PINOUT.md` for GPIO conflicts.
- [ ] My folder name is `kebab-case` and short.
- [ ] I am OK releasing this under MIT (software) / CERN-OHL-S v2 (hardware), unless I include my own `LICENSE`.
