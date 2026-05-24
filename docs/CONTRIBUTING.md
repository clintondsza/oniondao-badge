# Contributing

Thanks for wanting to add something to the NULL City Badge! This repo is
intentionally light on rules — the goal is a low-friction place to share
hardware mods, firmware, guides, and CTF-style challenges that other badge
owners can clone and flash in an afternoon.

---

## What lives where

| You want to share… | Put it in… |
|--------------------|------------|
| A minimal "this is how peripheral X works" sketch | `software/examples/<your-feature>/` |
| A step-by-step tutorial (markdown + code) | `software/guides/<your-guide>/` |
| A full firmware project / fork / variant | `software/mods/<your-mod>/` |
| A new hardware module (schematic + BOM) | `pcb/<NAME>_MOD.kicad_sch` + BOM in `pcb/production/` |
| A fix or addition to the docs | edit files under `docs/` directly |

## Folder shape for software PRs

Each contribution should be **one self-contained folder** with at minimum:

```
software/<bucket>/<your-thing>/
├── README.md          ← what it does, GPIOs used, build/flash steps
├── LICENSE            ← optional; defaults to MIT if absent
└── <source files>     ← .ino / src/ / main/ / *.py / platformio.ini / etc.
```

Your `README.md` should answer, at the top:

1. **What does this do?** (one paragraph)
2. **Which module variant does it need?** (CC1101 / Sound / none)
3. **Which GPIOs does it touch?** Reference the names in
   [`docs/PINOUT.md`](PINOUT.md) — don't re-invent numbering.
4. **How do I build & flash?** Toolchain + command lines.
5. **Demo / screenshot / video** if it has a visible result.

## Conventions

- **Toolchain-agnostic.** ESP-IDF, Arduino-ESP32, PlatformIO, MicroPython,
  CircuitPython, Rust-`esp-hal`, NuttX — all welcome. Just specify which
  in your README.
- **Use the documented pin names.** If the pinout document calls it
  `PBINT`, your code should too — makes diffs and grep work.
- **Don't break the BOOT pin.** Anything that drives `GPIO0` as an output
  must release it before the next reset, or document the manual-BOOT
  recovery in your README.
- **Power-gate awareness.** If your mod needs `PWR`, assert `GPIO18`
  HIGH during setup and (ideally) drop it on deep sleep.
- **Authorisation matters for RF.** CC1101 replay or jamming
  firmware is fine to share for research / testing on your own equipment.
  Don't ship anything that targets other people's systems without their
  consent. Your contribution, your responsibility.

## PR checklist

- [ ] My folder has a `README.md` answering the five questions above.
- [ ] My code builds with the toolchain I claim it does.
- [ ] If I added new GPIO usage, I cross-checked
      [`docs/PINOUT.md`](PINOUT.md) for conflicts.
- [ ] I picked the right bucket (`examples` / `guides` / `mods`).
- [ ] My folder name is `kebab-case` and short.

## Reporting bugs / asking questions

Open a GitHub issue. Include:

- Badge revision (silkscreen on the back, e.g. `r3`).
- Which module variant is populated.
- The toolchain + version you're using.
- The smallest failing example you can reproduce.

## License

By contributing under `software/`, you agree that your work is released
under the **MIT License** unless you ship a different `LICENSE` file in
your folder. Hardware contributions under `pcb/` follow **CERN-OHL-S v2**
unless you state otherwise in the schematic's title block.
