# bz_emu - a Battlezone emulator beside the C port

Design, 2026-09-18.  Adapted from the Tempest project's `tempest_emu`
(same folder layout, same vendored CPU, same "twin of the C port" idea),
with everything Tempest-specific replaced by what the Battlezone C port
proved (`..\EMULATOR_PORT_NOTES.md`, `..\c_src\app_loop.c`,
`..\c_src\tests\refrun.c`).

## Purpose

`bz_emu.exe` runs the real Battlezone program ROM on AAE's `cpu_6502` core
behind the SAME window, beam renderer, audio path, samples, controls and ini
as `..\c_src\bz_win.exe`, so the emulated ROM and the translated C can be run
side by side and compared.  One seam file, one ROM loader, a vendored CPU
core, a build script.  The ONLY thing taken from AAE is the 6502 core.

### Order of authority

1. The disassembly (`..\disasm\bzone_program_rom.asm`, Atari's names) and
   `..\c_src` (app_loop.c, refrun.c, EMULATOR_PORT_NOTES.md).
2. MAME's `bzone.cpp` to settle doubts.
3. The AAE driver (`..\bzone.cpp`): a cross-reference only.

## Copied from `..\c_src` (2026-09-18)

`emu_src\` is self-contained: it builds and runs with nothing beside it.
These files were copied unchanged from the C port and are now this
project's own copies, so later changes on either side do not propagate. The
ROM zips (`roms\`, from `..\roms\`) and the MAME sample WAVs (`samples\`, from
`..\c_src\samples\`) were copied the same way. `tests\cport_ref\` holds the C port's
generated `progrom.c` / `vecrom.c` / `mbprom.c`, used only by `romcheck`.

| piece | files |
|---|---|
| Windows backend (window, GL beam renderer, XAudio2, AAE's sample mixer, raw input, joystick, ini, log) | `platform\windows\*` incl. `plat_win.c` |
| platform contract | `platform\bz_platform.h` (`bz_app_*` hooks) |
| AVG walker, MAME cycle-true timing, clip window | `avg.c/.h` |
| Mathbox (executes the microcode PROMs) | `mathbox.c/.h` |
| POKEY | `c012294.c/.h` |
| SOUND latch as recorded samples (rising-edge one-shots, engine ramp) | `bz_sounds.c/.h` |
| machine state `g` (RAM $0000-$03FF, vector RAM) | `state.c/.h`, `state_defs.h` |
| headless driver for the smoke test | `tests\bz_smoke.c` |

The ROM images themselves come from the zip at run time (`emu_roms.c`), so
`progrom.h`, `vecrom.h` and `mbprom.h` are declarations only.

## New, in `emu_src\`

| file | |
|---|---|
| `emu_main.cpp` | the seam: memory map, NMI, watchdog, AVG/GOADD, POKEY catch-up, audio tick, pacing, `bz_app_*` |
| `emu_roms.c/.h` | loads a MAME Battlezone zip; defines `progrom`, `vecrom`, `mb_map`, `mb_ucode` |
| `cpu\` | the vendored core and its log shim (unchanged from tempest_emu) |
| `build_emu.bat` | `bz_emu.exe` + `tests\bz_emu_smoke.exe` |
| `tests\build_tests.bat`, `tests\romcheck.c` | Klaus test on the core; ROM loader byte-identity check |
| `bz_win.ini` | settings (the backend's fixed file name) |

## The board, as built

Everything below is EMULATOR_PORT_NOTES.md sections 1-3, applied to a real
CPU instead of translated code.

- **Address decode**: 15 bits.  The core runs with address mask `$7FFF`, so
  `$8000-$FFFF` mirrors `$0000-$7FFF` (RESET/NMI vectors from `$7FFA-$7FFD`).
- **Map**: RAM `$0000-$03FF`; IN0 `$0800`; OPTION `$0A00`; OPTON2 `$0C00`;
  EMCNT `$1000`; GOADD `$1200`; WTCHDG `$1400`; VSTOP `$1600`; Mathbox status
  `$1800`, result `$1810`/`$1818`, writes `$1860-$187F`; POKEY `$1820-$182F`;
  SOUND `$1840`; vector RAM `$2000-$2FFF`; vector ROM `$3000-$3FFF`; program
  ROM `$5000-$7FFF`.  Anything else reads `$00` and is counted.
- **Time**: one timeline in CPU bus cycles (the core's cycle callback).  The
  POKEY is caught up to the exact cycle of every access, so RANDOM is
  cycle-true (WNSHLD_15's "pick a free spot" loop needs a moving POKEY).
- **NMI**: an edge every 6144 cycles (246.09 Hz), only while the self-test
  switch is off (MAME's gate: IN0 b4 high).
- **IN0**: b0/b1 coins, b3 slam, b4 test, b5 diag (active low), b6 VG HALT,
  b7 = `(cycle >> 8) & 1`.
- **Watchdog**: 1,134,000 cycles without a WTCHDG strobe -> RESET (CPU,
  Mathbox, AVG).  Checked at every instruction boundary.
- **AVG**: GOADD walks the list from `$2000` (the data byte is ignored), with
  the scale register carried over from the previous walk; the picture is
  presented at the GOADD; HALT reads busy until GOADD + (draw cycles + one
  lead-in tick) / 8; a list with no HALT stays busy until VSTOP.  Segments are
  clipped to the AVG clip window and coloured by the overlay (red above
  y = 110, split at the edge).
- **Audio**: per NMI tick, one tick of POKEY samples, zeroed while SOUND D5
  (master enable) is clear; `bz_sounds_w` on every SOUND write,
  `bz_sounds_tick` per NMI.
- **Host keys**: F2 switching the self-test switch ON resets the board
  (MAME style - the ROM reads it only at PWRON); switching it OFF is left to
  the ROM (STEST7 JMPs PWRON).  F3 = RESET.

## Step granularity and pacing

`bz_app_step` = one MAIN pass: from one fall-through of MAIN's frame wait
(`L5004 JSR VOLCNO`) to the next (~15.4/s), capped at 32 NMI periods when no
MAIN runs (the self test).  Inside a step the machine is paced to the wall
clock once per NMI period with app_loop.c's anchor and 100 ms stall clamp.
This makes `tests\bz_smoke.c`'s pass-numbered scripts (coin at pass
20, start at 40, drive from 60) work unchanged on the emulator.
