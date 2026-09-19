# How to translate a Battlezone routine (brief for every translation agent)

You are one of several agents translating Atari Battlezone (1980, 6502) to C11, routine by
routine, 1:1. The disassembly is complete and byte-verified; the C must do exactly what the ROM
does, including every RAM store to scratch cells, in the ROM's order. It will be checked by a
lockstep verifier that runs each C function against the real ROM from the same snapshot and
compares ALL RAM, ALL vector RAM and the hardware I/O sequence - so "equivalent" is not good
enough; "identical effects" is the bar.

## Read first
1. `c_src\PLAN.md` (architecture and the facts the port depends on), `c_src\SIGNATURES.md`
   (the contract between agents - read its rules, register YOUR routines' signatures in your
   section BEFORE writing bodies), `c_src\regs.h`, `c_src\hw.h`, `c_src\state.h`,
   `c_src\state_defs.h` (generated: `A_NAME` addresses, `NAME` lvalues, indexable forms for the
   tank/robot twin cells, `K_NAME` constants), `c_src\progrom.h` / `vecrom.h` (ROM accessors).
2. `..\disasm\CALLING_NOTES.md` (entry/exit contracts and the translation hazards),
   `..\disasm\MAINLINE_NOTES.md`, `..\disasm\MATHBOX_NOTES.md`, `..\disasm\HARDWARE_NOTES.md`
   as needed, `..\disasm\bzone_defines.asm` (what every cell holds).
3. Your routines in `..\disasm\bzone_program_rom.asm` (address label `Lxxxx:` on every line,
   Atari's names and comments) and in Atari's source `..\battlezone-main\*.MAC` (RT-11 text,
   NUL padded: read with python `open(p,'rb').read().replace(b'\0',b'').decode('latin-1')`).
4. Style reference - the finished Tempest port, same conventions:
   `C:\Source2026\Tempest Dissasembly\c_src\alsoun.c`, `alvgut.c`, `alcoin.c`, `alwelg.c`,
   `game.h`, and Space Duel's `C:\Source2026\Space Duel Dissasembly\c_src\CONVENTIONS.md`.
   Where those ports already contain a translation of the SAME Atari routine (COIN65's MOOLAH,
   VGUT), start from that code and adapt it - the user wants maximum reuse.

## Conventions
- One C function per Atari label that is a routine entry (a JSR/JMP target, a dispatch-table
  entry, or a fall-through entry other code jumps to). Header comment `/* LABEL ($ADDR) - what
  it does; ENTRY ...; EXIT ... */`, and a `/* Lxxxx */` address comment on each statement or
  small group so the C can be read beside the listing.
- 8-bit arithmetic is explicit: `uint8_t`, carries computed exactly as ADC/SBC do (write small
  static inline helpers in your own file if you need them: adc/sbc with carry in/out, asl/rol/
  lsr/ror, BCD adc/sbc for the decimal-mode sites). Multi-byte values live in RAM cells byte by
  byte as the ROM keeps them; do not widen them into C variables that outlive the routine's
  own instructions unless the ROM keeps them in registers.
- Flags a caller branches on after the call are part of the contract (return them).
- Indexed twins: `TANGLE,X` with X=2 reaches NTHETA's byte etc. Always index the flat RAM from
  the base cell's address exactly as the ROM does.
- ROM tables are read from the ROM image through the accessor at their ROM address (or a named
  generated pointer), never retyped.
- Hardware only through `hw.h`, same order and same number of accesses as the ROM (the verifier
  compares the I/O sequence). Deliberate dummy reads used as delays (e.g. `CMP ($00,X)` before
  reading the Mathbox result) have no effect: leave a comment and emit only their cycles
  (`CYC(n)`, see "Cycle counting").
- Busy-waits on hardware (Mathbox `WAIT`, `BIT HALT` polls) go through the seam function once;
  do not spin.
- Push-address/RTS dispatches become `switch`; say which table in a comment.
- No dynamic allocation, no platform includes, plain C11, must compile `/W4` clean with
  `cl /W4 /std:c11 /c yourfile.c` (find cl the way `build_all.bat` does; run .bat via `cmd /c`).
- Routines owned by another agent: call them by the signature in SIGNATURES.md and include
  that module's header (`vgut.h`, `bzsoun.h`, `coin65.h`, `bzone_nmi.h`, `bzone_math.h`,
  `bzone_disp.h`, `bzone_game.h`). If the header does not exist yet when you compile, put the
  prototypes you need in a clearly marked temporary block at the top of your .c and remove it
  when the header appears.

## Rules
- Own only your files. Never edit generated files, `..\disasm`, another agent's module,
  `PLAN.md`, `hw.h`, `state.h`, `regs.h`. If the seam lacks something you need, write it up in
  your notes file and use the closest existing function, marked `TODO(seam)`.
- On this Windows machine never write files with bash heredocs; use the Write/Edit tools.
- No git commands that change state. Never write a 6502 core.
- Keep `NOTES_<yourmodule>.md`: routine list with status, every judgement call with the listing
  address, anything in the ROM that looks like a bug (translate it faithfully and note it).
- Final report: routines translated (count and names), compile result, signatures registered,
  calls into other modules (so integration can check them), open questions.

## Cycle counting
Translated code must take the machine time the ROM takes, because the NMI's counters are game
inputs read mid-pass (S_INTCT&7 at ROBOT picks R_EVAD's back-away branch; POKEY RANDOM is a
function of the cycle it is read on).  `hw.h` gives `CYC(n)`: charge n 6502 cycles.
- The model is the one `tests\refrun.c`/`lockstep.c` run the ROM with: NMOS base cycles
  (their `base_cycles[]`), +1 when a conditional branch is taken, no page-cross penalty.
  `python tools\cyc.py 6418-6433 [more ranges]` prints each instruction's cost and a running
  sum; use it, do not count by hand.
- A routine charges every instruction it executes, from its first through its own
  RTS/RTI/tail JMP.  A JSR's 6 cycles are charged by the CALLER, just before the call.  A tail
  `JMP OTHER` is 3 cycles in the jumping routine, then OTHER charges its own body (and its RTS).
  A fall-through into the next routine costs nothing extra: each C function charges only its
  own instructions (see `vgut.c`, the reference: `vgcntr` -> `vgadd2` -> `vgadd`).
- Conditional branch: charge 2 with the straight-line block that ends in it, then `CYC(1)` on
  the path where it is taken (`else CYC(1);` / inside the `if` - whichever side the branch
  jumps to).  Loops: the per-iteration block inside the loop, the backward branch's +1 on
  every iteration that goes round again.
- One `CYC()` per straight-line block (sum it with cyc.py), placed at the top of the block,
  commented with the address range: `CYC(19);  /* L7A34-L7A3C */`.  Where the charge depends
  on data the C already has (sign tests etc.), `CYC(cond ? 8 : 7)` is fine.
- Everything the ROM executes counts, including instructions the C does not need: dummy delay
  reads, PHP/PLP pairs simplified away, flag-only instructions, register shuffles, the
  Mathbox `WAIT` loop's one status read (the model's Mathbox is never busy).
- Hardware accesses through `hw.h` cost nothing extra - the instruction's own cycles include
  its bus access.
- Verification: `tests\lockstep.exe` prints, per routine, `cycles exact / differ` next to the
  RAM columns, and `LOCKSTEP CYCLES ...: C n, ROM m (+d)` lines for the first mismatches (with
  the entry registers, so the path can be traced).  The bar is 0 differ for every routine you
  own, over the three standard runs: `--frames 5000 --script tests\scenarios\attract.txt`,
  `--frames 3000 --script tests\scenarios\coin_start.txt`, `--frames 4000 --script
  tests\scenarios\play_long.txt`.  A tainted call (an NMI arrived during it) is not compared.
- MAIN/B_END (`bz_main_body`, `b_end_body`) are not lockstep-registered: annotate them the same
  way and check them by hand with cyc.py.
