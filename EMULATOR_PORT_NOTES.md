# Battlezone: what the C port taught us - notes for an emulator port

Written 2026-09-18 at the end of the C-port work (c_src/). For an emulator port (a real 6502
running the ROM, e.g. an AAE driver or a standalone emulator) built from `disasm/`, the Tempest
port's code and `c_src/`. Everything below was found or proven the hard way; file:line pointers
are to this tree.

## 1. The board, as the ROM actually uses it

| thing | fact | source / proof |
|---|---|---|
| CPU | 6502 @ 1.512 MHz (12.096 MHz / 8); POKEY on the same clock | HARDWARE_NOTES.md |
| NMI | every 6144 cycles = 3 kHz / 12 = **246.09 Hz**, non-maskable, runs in the self test too | refrun, lockstep |
| frame | MAIN waits `LSR SYNC / BCC MAIN`; the NMI bumps SYNC once per **16** NMIs -> ~15.4 game passes/s | c_src/bzone_nmi.c |
| video refresh | the NMI re-arms the AVG every **6** NMIs (VTIMER) -> ~41 pictures/s; double-buffered via BUFRDY + a JMPL at VECRAM | bzone_nmi.c NMI_40 |
| first picture | PWRON clears VTIMER, so the first GOADD is 256 NMIs (~1 s) after reset - real, not a bug | |
| IN0 $0800 | b0/b1 coins, b3 slam, b4 self-test, b5 diag step (all active LOW); **b6 VG HALT; b7 3 kHz = (cycle >> 8) & 1** | refrun.c in0_byte |
| DIPs | OPTION $0A00 default $15, OPTON2 $0C00 default $02 | bzone.cpp |
| controls | POKEY ALLPOT, active HIGH: b0 right-rev, b1 right-fwd, b2 left-rev, b3 left-fwd, b4 fire, b5 start1, b6 start2 (derived from MTAB's case table; matches refrun's script ports) | NOTES_m7.md |
| SOUND $1840 | D0 explosion, D1 which (set=explode1), D2 shell, D3 loud(player, M_NEX0 ORA #LOUDSH)/soft(enemy, FIREIT AND #SOFTSH), D4 engine fast, D5 master enable (gates POKEY too), D6 start lamp, D7 engine on | bz_sounds.c |
| Mathbox | identical to Tempest's (microcode + PROMs); reuse mathbox.c | memory bzone-mathbox-same-as-tempest |
| AVG | Tempest's state PROM (036408-01 = 136002-125) -> MAME cycle-true timing applies unchanged; monochrome, Z field = intensity directly; clip window (STAT HI/IN) | c_src/avg.h |
| watchdog | ~0.75 s (1,134,000 cycles) without a WTCHDG strobe resets the board. **The self test relies on it** (STEST4_10: "IF NO HALT, WTCHDGDOG WILL RESET") | bzstst.c stest4 |
| no EAROM | high scores are volatile RAM | HARDWARE_NOTES.md |

## 2. AVG / video - the traps

1. **The AVG always starts at $2000.** GOADD's data byte is the buffer page, but the hardware ignores
   it; in the game VECRAM's first word is a JMPL to $2002/$2802 (the NMI flips it). A "start at
   page<<8+2" shortcut (refrun's) works for the game and **breaks the self test**, which builds its
   picture at $2000 (lost the HATCH JSRL -> "just ones and zeros").
2. **The scale register persists across GOADDs.** Game lists set it first (VGSCA1); the self test's
   DIATST patterns set none. Resetting scale per walk drew them 2x too big.
3. **The tube is exactly +-256 x +-192** in post-scale AVG units: DIATST's corner-to-corner diagonal
   (-512,-384 -> +511,+383 raw) lands exactly there. Explosion debris going below -192 is off-tube on
   the cabinet too - clip, don't enlarge the view.
4. **A list that never HALTs keeps the AVG busy** until VSTOP (then IN0 b6 never goes high and the
   watchdog resets the board). Don't turn a runaway walk into "halted after N cycles".
5. **Clip window**: BIGWND arms a window with STAT HI/IN; clip segments to it (inside mode; OUT is
   never used by this ROM).
6. **Overlay gel**: red above AVG y ~110 (radar, score, messages), green below. Split segments that
   cross the edge - colouring whole segments by midpoint leaves most of a crossing line the wrong colour.
7. **$6B79 `CMP ($00,X)`** is a deliberate time-waster in DRADAR; its pointer comes from whatever X
   TRACK left, so it can read IN0/Mathbox/POKEY. Harmless on hardware (no side effects) - but any
   I/O logger/tracer must not count it as a real access.

## 3. Timing model that works (c_src/app_loop.c; Tempest's NOTES_m8.md design)

- One thread, one cycle timeline. NMIs on a fixed 6144-cycle grid; the NMI is serviced inside MAIN's
  frame wait when the wall clock reaches the grid point, plus any whose point the pass already passed.
- Charge a few cycles per hardware access (read 4, write 8). **Consecutive PRAND reads must see a
  moving POKEY** - WNSHLD_15 loops "pick a random spot until it is free" and spins forever otherwise.
- MAIN_5 also spins on BUFRDY (an NMI-owned RAM cell) - a real 6502 in an emulator just runs it; a
  translated port must fold it into the frame wait.
- The self test is busy loops on IN0 b7 (3 kHz) with no frame wait: machine time advances through
  the IN0 reads, NMIs still fire, pacing is to machine time.
- The watchdog is **hardware**: a counter on the machine clock, checked where time advances, below
  all game code; a bite = board reset (CPU to PWRON, Mathbox and AVG reset). Not a check inside a
  register read.

## 4. Game-logic facts worth knowing (all lockstep-proven)

- Carry flags matter across routine boundaries: VOLCNO's spawn ADCs chain one carry across all 5
  rocks, starting at C=1 (MAIN's `BCC MAIN` falls through only with carry set), and `CMP #$A2` sets
  it on both exits. MAIN's `L5042 BCC` uses VGSTAT's residual carry - provably clear (VGLIST low byte
  only reaches $0E there). An emulator gets these for free; a port or HLE does not.
- MAIN / B_END / WNSHLD / CKSCOR / HISCRE / DISTBL never RTS - they JMP into each other forever
  (the frame loop). WNSHLD -> CKSCOR is the one routine-to-routine tail jump.
- The self-test switch is read ONLY at power-on (PWRON `AND #ST_BIT`) and inside the self test.
  Flipping it during play does nothing on the board until reset -> an emulator's F2 should toggle
  AND reset (MAME style).
- Self-test flow: plain test (TEMP3=$FF) shows HATCH + alphabet + switch digits + ROM checksum
  errors; each diag press steps TEMP3 0..3 (DIATST patterns), then STEST7_70's own loop, then the
  next press JMPs PWRON. ROM checksums: 8 x 2 KB EOR (seeded $FF) over $3000-$3FFF then **$5000**-
  $7FFF (the operand is `LDA #$50`, not $40); a good board gives all $00.
- TRAP1 (BATTLE, attract) and TRAP2 (MAIN) are anti-tamper checksums over the display list; TRAP1
  must come out 1 or credits misbehave. Both are verified by the port (TRAP1 = 1).

## 5. Sounds

- Samples: MAME .SAM = 16-byte header ("MAME", size, rate, bits) + signed 8-bit PCM; the shipped WAVs
  in c_src/samples are those, converted (XOR $80 to unsigned 8-bit). AAE's bzonesamples[] order: fire1, fire2, engine1, engine2, explode1,
  explode2; channels 2 shell / 3 engine / 7 explosion.
- Improve on AAE: start one-shots on the **rising edge** of D0/D2 (AAE restarts them on any latch
  change, e.g. the blinking start lamp D6); **ramp** the engine loop's rate 1.0 <-> 1.66 on D4 with an
  RC-like time constant (~0.35 s by ear; AAE jumps, and its set_freq(get_freq*1.66) compounds);
  gate the engine with D7 (attract is silent). c_src/bz_sounds.c is written in AAE's API shape
  (`plat_sample_*` = `sample_*`) to drop straight into an AAE driver.

## 6. How the verification was done (reuse this)

- **refrun** (c_src/tests/refrun.c): the real ROM on the shared ref6502 core with the board model
  above - the oracle. Scenario scripts drive inputs per pass (tests/scenarios/*.txt).
- **lockstep** (c_src/tests/lockstep.c): at every JSR/JMP to a translated routine, snapshot RAM/VRAM,
  replay the ROM's own I/O log into the C routine, compare. NMIs landing inside a check taint it.
  Final: attract 5000 frames and coin_start 3000 frames, ~4.9 M calls, 0 failed.
- Bugs it caught in hand translation were almost all one of: an **inverted branch** (BPL/BMI, BCS/BCC
  sense - BATTLE, ROTATE, WNSHLD, STEST7), a **missing RAM side effect** of a register save
  (`STX TEMP1`, `STA TEMP2`, `STX SAVE1`, SAVE3 kept in a local), a **carry** chained or overwritten by
  a CMP, or a **harness** artefact. An emulator port avoids the first three entirely - but these
  are exactly the places to look if an HLE shortcut is ever added.
- Gaps still open: the pass-level probe needs NMI placement (Gates 1/N); MAIN/B_END bodies and BZSTST
  are hand-checked, not lockstepped; DISTBL/CKSCOR checks are always NMI-tainted.

## 7. What to reuse for an emulator port

- `disasm/`: the verified listing (Atari names, every routine documented), `HARDWARE_NOTES.md`,
  `MATHBOX_NOTES.md`, `CALLING_NOTES.md`, the defines file.
- `c_src/avg.c/.h` (monochrome AVG, MAME timing, clip window) + section 2's fixes (start at $2000,
  persistent scale, HALT-less lists stay busy); `mathbox.c`; `c012294.c` (POKEY); `tests/ref6502`
  (or the shared 6502 cores - never write a new one).
- `c_src/app_loop.c`'s board model (IN0 bits, costs, NMI grid, watchdog, overlay, audio tick) and
  `platform/windows/plat_win.c` (renderer, input bindings, ini) - an emulator swaps the translated
  modules for a CPU core and keeps the rest.
- `c_src/bz_sounds.c` for the discrete sounds; `c_src/samples/` (MAME set) or `SAMPLES2/` (80s Classics
  set - map files in bz_win.ini `[samples]`).
- Keys (same as Tempest / Space Duel / Asteroids Deluxe ports): E/D I/K treads, arrows single-stick,
  Space/Ctrl fire, 1/2 start, 5/6 coin, F2 test toggle (+reset), F1 diag step, F3 slam, Esc quit.
