# Battlezone C port - macOS (Apple Silicon)

Native SDL2 backend: `platform/macos/plat_mac.c` (implements `platform/bz_platform.h`).
Tested on macOS 27.0.1, arm64, Apple clang 21, SDL2 2.32.70 (Homebrew `sdl2`, which is the
`sdl2-compat` shim over SDL3 3.4.12).

## Prerequisites
```sh
xcode-select --install        # Git, clang, make, macOS SDK (skip if installed)
brew install sdl2             # also pulls in sdl3 (sdl2-compat)
```

## Build and run
```sh
git clone https://github.com/tcottrill/BattleZone-Full-Disassembly-and-C-Port.git
cd BattleZone-Full-Disassembly-and-C-Port/c_src
make -f Makefile.mac              # -> build/mac/bz_mac
./build/mac/bz_mac                # run from anywhere; samples are found automatically
make -f Makefile.mac app          # -> build/mac/Battlezone.app (bundles samples + libSDL2/libSDL3)
open build/mac/Battlezone.app
```
Options: `--fullscreen`, `--test` (self-test held from power-on), `--mute`, `--stats` (pacing in the
title bar), `--samples DIR`, `--demo` (scripted coin/start/drive/fire), `--shot FILE.bmp SECS`
(save a frame after SECS and quit). Env: `BZ_SAMPLES` (wav folder), `BZ_DSW_OPTION` /
`BZ_DSW_OPTON2` (hex option switches, defaults 15 / 02).

The `.app` is ad-hoc signed and not notarized; if Gatekeeper complains after copying it between
machines, right-click > Open, or `xattr -dr com.apple.quarantine Battlezone.app`.

## Controls
| Key | Action |
|---|---|
| E / D | left tread forward / reverse |
| I / K | right tread forward / reverse |
| Arrow keys | single-stick driving |
| Space, Left Ctrl | fire |
| 5 / 6 | coin 1 / 2 |
| 1 / 2 | start 1 / 2 |
| F1 | diagnostic step |
| F2 | toggle self-test |
| F3 | reset |
| F4 | slam |
| Cmd+F, Alt+Return | fullscreen |
| Esc, Cmd+Q | quit |

Game controller: left/right stick Y = treads, D-pad = single stick, A or right trigger = fire,
Start = start 1, Back = coin 1. (Ctrl+arrows may be intercepted by Mission Control shortcuts;
use Space or the E/D/I/K keys, or turn those shortcuts off.)

## Resource lookup
Samples (`ENGINE1/2`, `EXPLODE1/2`, `FIRE`, `FIRE2` `.WAV`) are searched in: `--samples`,
`$BZ_SAMPLES`, `<exe dir>/samples`, `<exe dir>/../Resources/samples` (app bundle),
`<exe dir>/../samples`, `<exe dir>/../../samples` (`build/mac/` -> `c_src/samples`), `./samples`.

## Checks
```sh
make -f Makefile.mac check      # headless: scripted play, self-test, PRAND regression
make -f Makefile.mac lockstep   # real ROM vs. translated C, call by call
```

## Portability changes
- `wait()` (Mathbox wait, clashed with POSIX `wait(int*)`) renamed `bz_mb_wait()` in
  `bzone_math.[ch]`, `bzone_disp.c`, `tests/lockstep.c`.
- `<stddef.h>` added to `state.h` and `c012294.h` (NULL).
- `ror6502` in `bzone_robot.c` marked unused (GNU attribute, guarded).
No game logic, cycle accounting or Windows files changed.

## Notes
- Audio: 44.1 kHz mono S16 callback device; POKEY goes through a 16384-frame ring (prefill
  ~2 NMIs, re-prefills on underrun, trims backlog if the device clock lags); samples are mixed
  in the callback with looping, volume and playback-rate control (engine pitch).
- Video: each picture is one `SDL_RenderGeometry` call of additive-blended quads (square end
  caps so zero-length vectors are dots), Liang-Barsky clipped to `BZ_VIEW_*`, letterboxed 4:3,
  Retina-aware.
- Dragging/resizing the window on macOS blocks the event loop; the game pauses and catches up.
