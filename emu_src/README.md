# bz_emu - the emulator twin of the Battlezone C port

*A bonus beside the port: not a translation, but the ROM itself, run on a
6502 core inside everything the port built.*

`bz_emu.exe` runs the **real Battlezone program ROM** on a 6502 core, behind
the same window, beam renderer, audio path, samples, controls and ini as
[`../c_src/bz_win.exe`](../c_src). You can run the emulated ROM and the
translated C side by side and compare them. The design is in
[`DESIGN.md`](DESIGN.md), and the board facts it uses come from
[`../EMULATOR_PORT_NOTES.md`](../EMULATOR_PORT_NOTES.md).

**`emu_src\` is self-contained.** It builds and runs from this folder
alone: its sources and its sample WAVs (`samples\`, MAME's `bzone` set) are
all here, and it loads its ROM zip from `roms\`. The ROMs are not
distributed — put a MAME `bzone` (or `bzonea`) zip in `roms\`. It was
checked by building a copy with nothing beside it. The C port's
parts were copied in on 2026-09-18 and now belong to this project, so a
change in `..\c_src` does not reach the emulator unless you copy it over.

| piece | file here | copied from |
|---|---|---|
| Windows backend (window, GL beam renderer, XAudio2, sample mixer, raw input, joystick, ini, log) | `platform\windows\*`, `platform\bz_platform.h` | `c_src\platform\` |
| AVG walker with MAME's cycle-true timing and the clip window | `avg.c/.h` | `c_src\avg.c` |
| Mathbox (runs the microcode PROMs) | `mathbox.c/.h`, `mbprom.h` | `c_src\mathbox.c` |
| POKEY | `c012294.c/.h` | `c_src\c012294.c` |
| discrete sounds (SOUND latch -> samples) | `bz_sounds.c/.h` | `c_src\bz_sounds.c` |
| machine state `g` (RAM, vector RAM), Atari's names | `state.c/.h`, `state_defs.h`, `progrom.h`, `vecrom.h` | `c_src\` |
| headless smoke driver | `tests\bz_smoke.c` | `c_src\tests\` |
| the C port's ROM arrays, for `romcheck` only | `tests\cport_ref\*.c` | `c_src\` (generated) |
| ROM zips `bzone`, `bzonea`, `bzone2` (yours, not distributed) | `roms\` | |
| MAME sample WAVs | `samples\` | `..\c_src\samples\` |
| board model (memory map, IN0 bits, NMI gate, watchdog, AVG traps, overlay, audio tick, pacing) | `app_loop.c` / `refrun.c` / EMULATOR_PORT_NOTES.md, re-implemented in `emu_main.cpp` |
| **6502 CPU** | **AAE's `cpu_6502` core** (`cpu\`, vendored). This is the only part taken from AAE. |

## Files

| file | |
|---|---|
| `emu_main.cpp` | the seam: memory map, NMI, watchdog, GOADD/VSTOP, POKEY catch-up, audio, pacing, and the `bz_app_*` hooks that `plat_win.c` calls |
| `emu_roms.c`, `emu_roms.h` | loads a MAME Battlezone zip and defines the storage behind `progrom`, `vecrom`, `mb_map` and `mb_ucode` |
| `cpu\` | the vendored core (`cpu_6502.cpp/.h`) with its support headers and log shim |
| `build_emu.bat` | builds `bz_emu.exe` and `tests\bz_emu_smoke.exe`; objects go in `obj\` |
| `tests\build_tests.bat`, `tests\romcheck.c` | the Klaus functional test on the core, and the ROM loader's byte-identity check |
| `bz_win.ini` | settings. The file name is fixed by the backend; this copy belongs to `emu_src\` |
| `shots\` | headless pictures from the verification runs |

## Build

    cmd /c ".\build_emu.bat"            (from emu_src; VS2022 x64)
    cmd /c ".\tests\build_tests.bat"    (obj\klaus_test.exe, obj\romcheck.exe)

Our files and the machine files compile at `/W4` with no warnings. The vendored framework, the POKEY and the CPU core compile at `/W3`.

## Run

`bz_emu.exe` uses the same keys as `bz_win.exe`:

- **Treads:** E/D for the left tread, I/K for the right tread. The arrows or d-pad give single-stick control.
- **Fire and start:** Space or Ctrl fires; 1 and 2 start.
- **Coins:** 5 and 6.
- **Test and diagnostics:** F2 toggles the self-test switch (turning it ON resets the board), F1 steps the diag, F3 resets, F4 is slam.
- **Window:** Alt+Enter toggles fullscreen; Esc quits.

Command line:

- `--test` holds the self-test switch on from power-on.
- `--roms FILE` picks the ROM zip. This option exists only in the emulator.

### ROMs

By default the emulator loads `roms\bzone.zip` beside the exe (the smoke test
in `tests\` finds the same folder as `..\roms\`). To use a different zip,
pass `--roms FILE` or set `[main] roms=FILE` in the ini; `--roms` wins. The
set is recognised from the file names inside the zip:

| set | program ROM $5000 | note |
|---|---|---|
| `bzone` | 036414-02.e1 (13de36d5) | MAME parent |
| `bzonea` | 036414-01.e1 (efbc3fa0) | Atari's source assembles to exactly this image, and the C port runs it |
| `bzone2 (old names)` | 036414a.01 | old MAME file names; the same program as `bzone` |
| `bzone (old names)` | 036414.01 | old MAME file names; the same program as `bzonea` |

Every set also needs:

- 036413 .. 036409 at `$5800-$7FFF`
- the vector ROMs 036422 at `$3000` and 036421 at `$3800`
- the Mathbox PROMs 036174 (mapping) and 036175-036180 (microcode). They are assembled low nibble first from 036180, exactly as `gen_roms.py` does.

036408 (the AVG state PROM) is not needed, because `avg.c` counts its cycles
itself.

If a file's CRC doesn't match, the log gets a warning and the ROM loads
anyway. If a file or the whole zip is missing, you get an error message box
and the program exits with code 3.

## Verification (2026-09-18, all headless)

1. **CPU core.** `obj\klaus_test.exe …\6502_functional_test.bin` prints
   `SUCCESS! Reached target $3469 in 96241364 cycles.`
2. **ROMs.** `obj\romcheck.exe` loads `roms\bzonea.zip`, and `progrom`,
   `vecrom`, `mb_map` and `mb_ucode` are all IDENTICAL to the C port's generated
   arrays. `bzone.zip` differs in exactly 149 bytes, starting at `$5340`,
   which matches the disassembly's record of the two sets. `bzone2.zip` loads
   as the old-names layout.
3. **Attract** (`tests\bz_emu_smoke.exe 300`): 246.1 NMI/s, 15.4 MAIN passes/s
   and about 41 pictures/s. TRAP1 = 01, with 0 watchdog bites, 0
   never-halting lists and 0 Mathbox runaways. Everything drawn stays within
   x ±253, y -120..191.
4. **A game** (`--play`: coin, start, drive and fire): ATRACT=FF, LIVES=03,
   with the landscape, radar, score and tanks drawn (`shots\play.png`).
5. **Self test** (`--test`): the NMI is gated off (0 delivered), and the
   HATCH + alphabet + switch + checksum screen matches the C port's
   `bz_smoke --test` picture exactly (`shots\selftest.png` against
   `shots\cport_selftest.png`). `--test --diag 40` steps through the
   diagnostic patterns with no watchdog bites.

Not verified: anything that needs a visible window (live keys, joystick, F2
and F3 at runtime, audio by ear, fullscreen).

## Differences from `bz_win.exe`

- The ROM runs; nothing is translated. There is no pass-cost model: CPU time
  is real, and the POKEY is caught up to the exact bus cycle, so RANDOM
  sequences and RAM differ from `refrun` / `bz_win` from pass to pass, as
  expected.
- The exit log has extra `emu:` lines:
  - NMIs delivered and gated
  - GOADD/VSTOP counts and never-halting lists
  - watchdog kicks and bites
  - RESETs
  - Mathbox starts and runaways
  - unmapped reads. These come from DRADAR's `L6B79 CMP ($00,X)` time-waster, whose pointer comes from whatever X TRACK left behind. EMULATOR_PORT_NOTES.md 2.7 says this is harmless.
  - NMOS dummy read cycles into the write-only Mathbox range, from the self test's indexed stores
- `--test --diag` runs show many Mathbox "runaway" starts in the diagnostic
  steps. The Mathbox model is the same `mathbox.c` that `bz_win.exe` uses, so
  this is its behaviour on those diag writes, not something the emulator adds.
  It has not been investigated further.
