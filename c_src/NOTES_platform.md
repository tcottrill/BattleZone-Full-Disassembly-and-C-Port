# NOTES_platform.md - Windows platform backend (M7 "playable Windows build")

Scope: `c_src\platform\` (windows + headless backends), `c_src\build_win.bat`,
`c_src\bz_win.ini`, `c_src\tests\plat_stub.c` / `build_plat_stub.bat`. Never
touched `app_loop.c`, `bzstst.c`, `bzone_*`, `vgut.*`, `bzsoun.*`, `coin65.*`,
`state*`, `hw.h`, `avg.*`, `mathbox.*`, `c012294.*`, `tests\lockstep.c`,
`tests\refrun.c`, `build_all.bat`, `platform\bz_platform.h`.

## What was copied, from where

Vendored framework files (unchanged, verbatim) from
`C:\Source2026\Tempest Dissasembly\c_src\platform\windows\` into
`c_src\platform\windows\`: `colordefs.h`, `fileio.c/h`, `framework.h`,
`glew.c/h`, `ini.c/h`, `joystick.c/h`, `log.c/h`, `mat4.c/h`, `miniz.c/h`,
`mixer.c/h`, `rawinput.c/h`, `shader_util.h`, `sys_gl.c/h`, `vector_draw.c/h`,
`wglew.h`. `win_probe.c/h` was **not** copied - it queries Tempest's own
state symbols (QSTATE etc.) and isn't part of the reuse list; not needed here.

`platform\windows\plat_win.c` and `platform\headless\plat_headless.c/h` are
adapted (not copied verbatim) from Tempest's equivalents, trimmed to
`bz_platform.h`'s smaller contract: no colour RAM / STAT intensity
substitution (the core already colours each segment and gives 1..7
intensity), no rotation/flip (the monitor is upright), no EAROM/NVRAM
(Battlezone's self test isn't ported yet - `test` is accepted and ignored),
no cocktail, no picture-row/watchdog/diag stats, no `--shot`/`--autoplay`/
`--pass-log` verification tooling (that's what `tests\plat_stub.c` is for -
M7 has no oracle to script against yet). Kept: window + DPI awareness +
ALT+Enter borderless fullscreen, the OpenGL 3.3 shader beam renderer
(`vector_draw.c`, with the 8-frame phosphor/afterglow buffer, off by
default), XAudio2 stream audio, ini config, log file, the
`present_idle`/`plat_sleep_ms` pacing shape (backend never blocks longer
than `bz_app_idle_ms()` reports safe).

`tests\build_plat_stub.bat` and `build_win.bat` are modelled on Tempest's
`build_win.bat` (objects in their own `obj\` subdirectory so they never
collide with `build_all.bat`'s `obj\` or each other).

## Video mapping

AVG space (`BZ_VIEW_XMIN/XMAX/YMIN/YMAX` in `bz_platform.h` - always read
from there, never hard-coded: `DESIGN_W`/`DESIGN_H` in `plat_win.c` are
computed from the macros in `plat_init`) maps straight onto the window with
no rotation and no flip, letterboxed to keep the tube's aspect ratio
(`ViewOrthoScaled`). Segments are clipped to the tube with Liang-Barsky
(`clip_seg`, generalized for the current asymmetric Y range) before being
stored; `plat_shutdown` logs how many segments were dropped/cut, useful for
sanity-checking `BZ_VIEW_*` if it's ever re-measured. Beam width
(`[vector] linewidth`) is authored in pixels at a `DESIGN_W`-wide window and
scales proportionally with the picture as the window resizes; AA feather
(`line_smoothing`) stays a constant number of physical pixels regardless of
zoom. `beam_set_color_mode(1)` (additive) for the glow look.

## Key bindings (hard-coded, MAME's Battlezone defaults - see bz_win.ini)

- Left tread: **E** forward, **D** reverse. Right tread: **I** forward, **K** reverse.
- Single-stick mode on the **arrow keys**, ORed with the direct tread keys:
  Up = both forward, Down = both reverse, Left = left reverse + right
  forward (pivot left), Right = left forward + right reverse (pivot right),
  Up+Left = right forward only, Up+Right = left forward only, Down+Left =
  left reverse only, Down+Right = right reverse only.
- Fire: **Space** or **Left Ctrl**. Start 1/2: **1**/**2**. Coin 1/2: **5**/**6**.
  Slam: unmapped. Test: **F2** (accepted, ignored by the core for now).
- **Alt+Enter**: fullscreen. **Esc** or the window close box: quit.
- Joystick/gamepad (`joystick.c`, DirectInput): left stick Y = left tread,
  right stick Y (the "Ry" extra axis - the usual DirectInput slot for a
  second analog stick on an XInput-style pad; **not verified on real
  hardware**, see Unfinished) = right tread, built-in ~0.5 digital
  threshold; d-pad hat = single-stick mode (same folding as the arrow
  keys); button 1 = fire, Start (button 8) = start1, Back (button 7) = coin1.

Not rebindable via the ini (Tempest's ini doesn't rebind individual keys
either - only tuning knobs like mouse sensitivity - so hard-coded matches
the brief's fallback).

## ini keys (`bz_win.ini`, next to the exe; log is `bz_win.log`)

- `[dips]` - one word per option-switch group, as the Tempest / Space Duel /
  Gravitar ports do it (`read_dips()`): `lives` `missile_at` `bonus_tank`
  `language` (OPTION) and `coinage` `right_coin` `left_coin` `bonus_coins`
  (OPTON2). Defaults are AAE's bzone.cpp: 3 lives, 10000, 15k_and_100k,
  english, 1_coin_1_play = `0x15` / `0x02`. Unknown words fall back to the
  default and are rewritten. (Replaced the raw `[dsw] option`/`opton2` hex
  keys 2026-09-19; an old `[dsw]` section left in an ini is ignored.)
- `[main] vsync` - 0 (default, tested) / 1 / 2. `refresh_hz` - phosphor
  redraw cadence, 0 = ask Windows.
- `[vector] phosphor_ms` (default 0 = off), `linewidth`, `gain`,
  `line_smoothing`, `corner_strength` - read directly by `vector_draw.c`.
- `[sound] pokey_volume` (0 = no audio device opened), `latency_blocks`.
- `[joystick] deadzone` - read directly by `joystick.c`.

## Build / run

```
cmd /c ""C:\Source2026\BattleZone Dissasembly\c_src\build_win.bat""
cmd /c ""C:\Source2026\BattleZone Dissasembly\c_src\tests\build_plat_stub.bat""
```
(Run from PowerShell with the doubled quotes shown - a bare `cmd /c build_win.bat`
or Git Bash `cmd //c` both fail to find the file on this machine.) Both build
`/W4` clean (0 warnings) for `plat_win.c` / `plat_headless.c` / `plat_stub.c`;
the vendored framework and `c012294.c` build at `/W3` as harvested, unchanged.

Outputs: `bz_win.exe` (real core) and `tests\plat_stub.exe` (own stub core,
for platform-only verification without app_loop.c).

## What was verified by running

- `tests\plat_stub.exe`: launched, ran ~10 s at a steady 41.0-41.3 fps
  (matches the stub's intended GOADD-like cadence), drew the rotating green
  hexagon (varying intensity), the red overlay strip and the centre dot
  (confirming a zero-length segment renders as a dot) correctly inside the
  tube rectangle; window title showed live fps/seg-count stats; audio stream
  ran with 0 starved/flushed pushes. Screenshotted, then closed.
- `bz_win.exe` (the real core, once `app_loop.c`/`bzstst.c` existed): built
  clean, launched, ran the actual attract-mode display list - tank, terrain,
  moon, red gunsight/score overlay, "GAME OVER" / "1 COIN 1 PLAY" text all
  rendered correctly at ~41.3 fps; the core's own `plat_status_text` pacing
  line (`passes/NMIs/pictures/lit/bbox`) showed in the title bar and log,
  climbing steadily with 0 segments dropped/cut against the current
  `BZ_VIEW_*`; audio queue depth stayed flat (5..8, avg ~6.4, no drift);
  closed cleanly via WM_CLOSE, exit log confirmed a graceful shutdown
  (stream closed, mixer shut down, 113243 lit segments total, 0 outside the
  tube). Screenshotted both runs (see the session's scratchpad PNGs).

## Unfinished / worth knowing

- Joystick right-tread axis mapping ("Ry") is a best-effort guess at a
  typical DirectInput axis order for a second analog stick - I have no real
  gamepad on this machine to confirm it. If it's wrong on real hardware,
  `plat_input_poll`'s stick-name search (`platform\windows\plat_win.c`) is
  the only place to change.
- `vsync=1`/`2` just lets `SetSwapMode`'s own blocking swap happen from
  wherever `present_idle`/`present_now` is called (no DWM vblank-phase
  scheduling like Tempest's, to keep this simpler for M7) - it may add a
  little jitter to the NMI-driven pacing. `vsync=0` (default) is the tested
  path and never blocks.
- No EAROM/NVRAM and no self-test UI - matches `bz_platform.h` (`test` is
  accepted and ignored "until BZSTST is ported"; `bzstst.c` exists now but
  the contract doc hasn't been updated to wire `test` through - nothing for
  this backend to change until it is).
- `tests\plat_stub.c`'s own audio pacing (fixed nominal ms per picture
  rather than measured wall-clock elapsed time) let its XAudio2 queue depth
  drift slowly upward over a long run (~10 -> ~11.5 over 25 s) - harmless
  for a stub, but not a model for real pacing; `bz_win.exe`'s real core
  paces audio off machine time and showed no such drift over the same
  window.
