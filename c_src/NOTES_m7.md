# M7 - the playable Windows build (core side)

Status 2026-09-18. Platform backend: `NOTES_platform.md` (platform\windows\plat_win.c, build_win.bat).

## Files

| file | what |
|---|---|
| `platform\bz_platform.h` | the platform contract (adapted from Tempest's `tempest_platform.h`): monochrome segment sink, tread/fire/start/coin inputs, 2 DIP banks, mono s16 audio, time, `bz_app_*` hooks, `BZ_VIEW_*` (the visible tube in AVG units) |
| `app_loop.c` | the game seam: every `hw.h` function, machine time + NMI grid, wall-clock pacing, video walk per GOADD, POKEY + discrete-sound audio, `bz_app_init/step/exit` |
| `bzstst.c/.h` | PWRON ($7ACC-$7B7C), the power-on game path, translated (the self test, RAMTST onwards, is M8) |
| `tests\bz_smoke.c` | headless smoke test: a tiny backend (simulated clock, scripted inputs, SVG of the last picture) on the real core. Built by `build_all.bat` |
| `tools\svg2png.py` | rasterize bz_smoke's SVG (Pillow) |

## Run

    build_all.bat                                   (also builds tests\bz_smoke.exe)
    tests\bz_smoke.exe 300 --svg attract.svg        (attract; ~20 s machine time)
    tests\bz_smoke.exe 900 --play --svg play.svg    (coin at pass 20, start at 40, then drive/turn/fire)
    python tools\svg2png.py play.svg play.png 1
    build_win.bat  ->  bz_win.exe                   (see NOTES_platform.md for keys)

## Seam design (`app_loop.c`)

- **One thread, one cycle timeline** (`mach_cyc`, 1.512 MHz; the POKEY is clocked exactly to it). The NMI
  ($558F) sits on a 6144-cycle grid (3 kHz / 12 = 246.09 Hz). It is serviced only inside MAIN's frame wait,
  when the wall clock (`plat_now_ms`) reaches the grid point - after first servicing any NMI whose grid point
  the pass's own time already passed (on the board those interrupted the pass). Stall clamp 100 ms.
- **Access costs** (Tempest's): read 4, write 8 cycles, one SYNC spin iteration 8. They are what makes
  consecutive PRAND reads differ (WNSHLD_15's "random spot until free" loop depends on it). The translated
  code has no cycle annotations beyond that; a pass otherwise takes no machine time. That is harmless here:
  MAIN is gated by SYNC (one bump per 16 NMIs, ~15.4 passes/s), the real pass fits inside that window
  (refrun: ~102k cycles/pass ~ 16.6 NMIs), and nothing in the game logic reads time except PRAND.
- **MAIN's two spins** are both in `hw_wait_frame()`: `L5000 LSR SYNC / BCC MAIN` literally (shift, and on
  no carry wait for the next NMI), then MAIN_5's `LDX BUFRDY / BEQ` (a RAM spin on an NMI-owned cell that a
  single thread cannot do inside `bz_main_body()`): it keeps servicing NMIs until BUFRDY != 0. The only
  thing between the two in the ROM is `JSR VOLCNO`, which shares nothing with the NMI but PRAND.
- **Video**: `hw_goadd(page)` (the NMI, every 6 NMIs = ~41 Hz) walks the list at `page<<8 + 2` (VECRAM's
  first word JMPLs there - refrun's model) with avg.c, clips each lit segment to the AVG clip window when
  one is armed (IN mode; OUT is never used), colours it by the cabinet overlay (red above AVG y 110 - the
  radar, score, lives and messages; green below) and presents. IN0 b6 (VG HALT) reads busy until GOADD +
  the list's authentic AVG time (avg.c, MAME state machine); b7 = (cycle >> 8) & 1.
- **Inputs**: polled once per pass; ALLPOT = b0 right reverse, b1 right forward, b2 left reverse, b3 left
  forward, b4 fire, b5 start1, b6 start2, active high (derived from MTAB's case table and matching
  refrun.c's `rt_down/rt_up/lt_down/lt_up`); IN0 b0/b1 coins, b3 slam, b4 test, active low.
- **Audio**: per NMI, ~179 POKEY samples at 44.1 kHz (c012294.c, measured DAC) pushed to the stream,
  muted while the SOUND latch's D5 (master enable) is clear. The latch's discrete sounds are recorded
  samples on the platform's mixer (AAE's mixer.c): `bz_sounds.c` is the BzoneSounds equivalent, written
  against AAE's sample API shape (`plat_sample_*` = AAE's `sample_*`) so it ports back to AAE. Mapping
  as AAE's driver (channels 2 shell / 3 engine / 7 explosion; D1 picks explode1/2; D3 picks fire1 =
  the player's shot (M_NEX0 ORA #LOUDSH) / fire2 = the enemy's (FIREIT AND #SOFTSH)), except: one-shots
  on the rising edge of D0/D2; the engine loop's rate ramps 1.0 <-> 1.66 on D4 with an RC-like time
  constant (ENGINE_TAU_S 0.35 s, by ear) via sample_set_freq per NMI; D7 fades the engine.
  Files: bz_win.ini `[samples] dir` (default samples = c_src\samples; was ..\SAMPLES until 2026-09-19) + one key per name (fire1..explode2);
  `[sound] samples_volume`. Headless backends stub the sample calls.
- **Boot**: `bz_app_init()` runs `pwron()` then `main_init()`; `bz_app_step()` = poll inputs + one
  `main_pass()`. The first picture appears ~1 s after power-on: PWRON clears VTIMER, so the NMI's first
  DEC wraps to $FF and the first GOADD is 256 NMIs later - as on the board.

## Measured (bz_smoke)

- attract 300 passes: 4992 NMIs (16.6/pass), 790 pictures, TRAP1 = 1 (the anti-tamper checksum is right).
- `--play` 900 passes: coin + start accepted (ATRACT = $FF), the scripted tank drives, fires, gets hit.
- Lit segments span x -253..253, y -253..191 -> `BZ_VIEW_*` = x -270..270, y -205..200 (4:3).
- The gunsight blinking while a shell is in flight is the ROM's (MAIN L50E9-L50F1), not a bug.

## Verification status / gaps

- Every routine except MAIN/B_END's own bodies is lockstep-verified (NOTES_integration.md). MAIN/B_END were
  re-checked by hand against the listing (found and fixed #17); the pass-level probe that would verify them
  mechanically needs NMI placement (NOTES_integration.md RESUME).
- MAIN's `L5042 BCC MAIN_10` residual carry (when FRAME does not wrap) is proven clear: VGSTAT exits via
  VGADD, whose carry is VGLIST's low-byte add, and VGLIST's low byte is only $0C -> $0E there.
- Not yet: the self test (BZSTST, M8), the discrete sound circuits, the Bradley/alternate ROM.
