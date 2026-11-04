# How to maintain the Battlezone C port

The C port is plain C that does what Atari Battlezone (1980) does: the same RAM
effects, the same hardware accesses in the same order, and the same number of
machine cycles on every path. It is not a 6502 translated instruction by
instruction. The separate emulator (`..\emu_src`) and the reference 6502 CPU
under `tests\` exist to check the port against the real ROM; neither is part of
the game.

## Read first
1. `c_src\PLAN.md` (architecture and the facts the port depends on), `c_src\hw.h`,
   `c_src\state.h`, `c_src\state_defs.h` (generated: `A_NAME` addresses, `NAME` lvalues,
   indexable forms for the tank/robot twin cells, `K_NAME` constants), `c_src\progrom.h` /
   `vecrom.h` (ROM accessors).
2. `..\disasm\CALLING_NOTES.md`, `..\disasm\MAINLINE_NOTES.md`, `..\disasm\MATHBOX_NOTES.md`,
   `..\disasm\HARDWARE_NOTES.md` as needed, `..\disasm\bzone_defines.asm` (what every cell
   holds).
3. The routine in `..\disasm\bzone_program_rom.asm` and in Atari's source
   `..\battlezone-main\*.MAC` (RT-11 text, NUL padded: read with python
   `open(p,'rb').read().replace(b'\0',b'').decode('latin-1')`).

## Writing the C
- Write game logic: coordinates and velocities as 16-bit values, headings as wrapping 8-bit
  angles, collision tests as comparisons, scores as decimal numbers, counters as counters.
  Split a routine into helpers wherever that reads better; a helper the ROM reaches by falling
  through costs nothing to call.
- No CPU modelling: no A/X/Y or flag variables, no carry chains, no ADC/SBC/shift/rotate
  helpers, no register save/restore, no stack. Name values after what they are (`rock`,
  `channel`, `offset`), not after the register that held them.
- Results a caller uses are return values with domain types (`trig_sample`,
  `object_contact`). When the ROM's output depends on an arithmetic side effect (the trig
  routines' wrap bit that ROTATE adds back), keep it as a named field and say why.
- RAM cells the ROM writes are part of the observable behaviour: keep every store, including
  scratch cells, in an order the lockstep verifier accepts. Byte packing for RAM and hardware
  interfaces is fine.
- ROM tables are read through `cpu_rd()` at their ROM address (or a named generated pointer),
  never retyped.
- Hardware only through `hw.h`, in the ROM's order and number of accesses (the verifier
  compares the I/O sequence). Busy-waits on hardware go through the seam once; do not spin.
- Comments explain the game. Atari's own source comments (`;C ...`) are welcome; lists of
  6502 instructions are not. Label names and `$ADDR` ranges are fine for orientation.
- Plain C11, no dynamic allocation, no platform includes, `/W4` clean.
- On this Windows machine never write files with bash heredocs; use the Write/Edit tools.

## Cycle counting
Code must take the machine time the ROM takes, because the NMI's counters are game inputs
read mid-pass (S_INTCT&7 at ROBOT picks R_EVAD's back-away branch; POKEY RANDOM is a function
of the cycle it is read on). `hw.h` gives `CYC(n)`: charge n machine cycles.
- The model is the one `tests\refrun.c`/`lockstep.c` run the ROM with: NMOS base cycles,
  +1 when a conditional branch is taken, no page-cross penalty. `python tools\cyc.py
  6418-6433 [more ranges]` prints each instruction's cost and a running sum.
- Charge one total per path through a block, before the block's effects: `CYC(19);`, or
  `CYC(cond ? 8 : 7)` where the cost depends on data the C already has. A routine charges
  everything from its entry through its own return; a call's 6 cycles are charged by the
  caller, just before the call.
- Everything the ROM executes counts, including work the C does not need (delay reads,
  register saves, flag-only instructions, the Mathbox status read).
- Hardware accesses through `hw.h` cost nothing extra.
- Verification: `tests\lockstep.exe` prints, per routine, `cycles exact / differ` next to the
  RAM columns. The bar is 0 differ for every routine, over the three standard runs:
  `--frames 5000`, `--script tests\scenarios\coin_start.txt --frames 3000`, `--script
  tests\scenarios\play_long.txt --frames 4000`, all with `--cycles-strict`. A tainted call (an
  NMI arrived during it) is not compared. Paths the scenarios never reach belong in
  `tests\native_game_boundaries.c`.
- MAIN/B_END (`bz_main_body`, `b_end_body`) are not lockstep-registered: check them by hand
  with cyc.py.
