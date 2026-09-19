HANDOVER: compiles /W4 clean, ready for lockstep

# BZONE maths / 3D pipeline notes (bzone_math.c/.h) - agent "maths"

## RESUME (read this first if picking this up mid-session)

**Status: DONE.** All 15 routines translated and verified against the
listing, `bzone_math.c` compiles `cl /nologo /W4 /std:c11
/D_CRT_SECURE_NO_WARNINGS /I. /c bzone_math.c` clean (0 warnings, exit code
0, re-confirmed from a clean `.obj` on a fresh build in this session), and
`SIGNATURES.md`'s maths section is registered with final prototypes. Ready
for the integration agent to fold into the lockstep verifier.

**Important - two real fixes were needed to reach that clean state; see
"Fixes made this session" below before assuming the file needed nothing.**
Both are already applied to `bzone_math.c` on disk (verified by deleting
the `.obj` and rebuilding from scratch after each fix): (1) a compile-time
linkage bug (`t_2` declared non-static in the header but forward-declared
`static` in the .c - warning C4211, not `/W4`-clean), and (2) a genuine
bit-inexactness bug in `rotate()`'s BSIN negate, found only by the
instruction-by-instruction cross-check against the listing (the compiler
can't see it) - it would have produced a wrong Mathbox BSIN write on a
large fraction of real frames, not a rare edge case. Read that section
before touching anything sin/cos/xtrap-related in review.

Status table (ROM address, C name, state):

| routine | addr | C name | status |
|---|---|---|---|
| ROTATE | $5946 | `rotate` | done, compiles clean |
| ROTPNT | $5B1F | `rotpnt` | done, compiles clean |
| WAIT | $5B80 | `wait` | done, compiles clean |
| PNTPUT | $5D4A | `pntput` | done, compiles clean |
| KLUDGE | $5E05 | `kludge` | done, compiles clean |
| BOUND | $5E44 | `bound` | done, compiles clean |
| COS | $5E4B | `cos` | done, compiles clean |
| SIN | $5E4E | `sin` | done, compiles clean |
| SIN1 | $5E63 | `sin1` | done, compiles clean |
| COSXTR | $5EF9 | `cosxtr` | done, compiles clean |
| SINXTR | $5F17 | `sinxtr` | done, compiles clean |
| XTRAP | $5F32 | `xtrap` (static, internal) | done, compiles clean |
| ABSTMP | $6149 | `abstmp` | done, compiles clean |
| TRACK | $6810 | `track` | done, compiles clean |
| T_0/T_1/T_2/T_3 | $68D5/$68C6/$68CB/$68D0 | `t_0`/`t_1`/`t_2`/`t_3` | done, compiles clean |

`bzone_game.h` appeared since the previous session and carries real
prototypes for `explde`/`tr7chk` that match this module's own temporary
TODO(seam) prototypes exactly (`uint8_t explde(uint8_t y);` / `int
tr7chk(void);`) - switched `bzone_math.c` to `#include "bzone_game.h"` and
removed the temporary block, per TRANSLATING.md's seam rule.

## Fixes made this session (second, independent review)

The brief's step 2 asked for an instruction-by-instruction re-check of every
routine against `../disasm/bzone_program_rom.asm`, independent of whatever
verification (if any) produced the "done" status this file already
recorded. That re-check found two real problems, both now fixed on disk:

1. **Compile error - `t_2` static/extern linkage conflict (warning
   C4211).** `bzone_math.h` declares `t_0`/`t_1`/`t_2`/`t_3` all
   non-static, on purpose ("exposed here... for direct lockstep coverage"
   even though only `track()`'s own internal `switch` reaches `t_2`
   directly). `bzone_math.c` had a leftover forward declaration
   `static uint8_t t_2(uint8_t a);` plus a `static` definition, which
   conflicts with the header's non-static declaration already in scope by
   the time the compiler reaches it (`#include "bzone_math.h"` is line 17,
   before the stray forward declaration). MSVC accepted it only as
   nonstandard-extension warning C4211 ("redefined extern to static"),
   which fails the `/W4`-clean bar. Fix: dropped the stray forward
   declaration and the `static` qualifier on `t_2`'s definition, matching
   `t_0`/`t_1`/`t_3`. `xtrap` is correctly still `static` (genuinely
   internal-only, not in the header).

2. **BSIN carry-chain bug - real bit-inexactness, not a compiler
   diagnostic.** In `rotate()`, the ROM's BSIN negate is:
   ```
   L5963  JSR  SINXTR
   L5966  EOR  #$FF
   L5968  ADC  #$01      <- NO CLC here
   L596A  STA  BSIN
   L596D  TXA
   L596E  EOR  #$FF
   L5970  ADC  #$00
   L5972  STA  BSIN+1
   ```
   Every *other* 16-bit negate in this module explicitly `CLC`s right
   before its low-byte `ADC` (PNTPUT's ACOS negate at L5D64, ABSTMP at
   L614F, T_1/T_3 at L68C6/L68D0) - this one doesn't. The carry-in to
   L5968 is therefore whatever `SINXTR`'s call chain actually left on
   exit, not a guaranteed 0. Traced every path to find out what that is:
   - `SIN1`'s own exit carry (from the final `ASL` used to build its table
     index) is *provably* always 0 - the folded index never exceeds `$40`,
     so bit 7 going into that `ASL` is never set, in either branch.
   - `SIN`'s direct-return path (angle `>=0`, falls straight into `SIN1`)
     therefore also always exits with carry 0 - the module's original
     hardcoded `cn=0` assumption happened to be right here.
   - `SIN`'s negative-angle path (angle `<0`) does its own negate, and
     *its* exit carry (from the final hi-byte `ADC`) is 1 iff the
     pre-negate 16-bit sine value was exactly 0 (i.e. angle exactly `$00`
     or `$80`) - data-dependent, but wrong at only one boundary angle out
     of 256.
   - **`XTRAP`'s exit carry (the interpolation path - i.e. whenever the
     division bit is set, which happens for roughly half of all calls via
     `LANGLE`) is fully data-dependent on the actual interpolated sine
     value - not reliably 0 at all.** `ROTATE` calls `SINXTR` with
     `x = TANGLE` (a full 0-255 angle, so the negative-angle path is
     common) and `a` derived from `LANGLE` (so the interpolation path is
     also common) - so the old hardcoded-0 assumption would have produced
     a wrong BSIN low/high byte pair whenever both conditions lined up.
     This is a real, unconditionally-reachable divergence from the ROM,
     not a corner case a lockstep run would likely miss.
   - Fix: added the exact exit-carry value to `regs.c` (see `regs.h`) in
     `sin1()`, `sin()` and `xtrap()` - `cos()`/`cosxtr()`/`sinxtr()` just
     return one of those three's `regs` unchanged, so they inherit the fix
     for free. `rotate()`'s BSIN negate now seeds its carry chain with
     `r.c` from `sinxtr()`'s return instead of a hardcoded 0. Checked this
     doesn't disturb any other caller of `sin`/`cos`/`sinxtr`/`cosxtr`:
     PNTPUT's ACOS negate (the only other 16-bit negate anywhere in this
     module that immediately follows a `cos()`/`sin()` call) already has
     its own explicit `CLC`, so it never read the previously-uninitialized
     `.c` field and is unaffected by it now being populated for real.

Both fixes are already in `bzone_math.c` on disk; the compile result below
reflects the file *after* both fixes, from a clean rebuild.

## What the next session needs to know (verified against the listing)

### Mathbox register choreography (offsets from $1860, confirmed against
`../disasm/bzone_program_rom.asm` and `../disasm/MATHBOX_NOTES.md`)

These 6502 addresses are **hardware**, not RAM - `state_defs.h` only defines
their `A_*` address constant, no lvalue macro (unlike RAM cells). Every
`STA <name>` / `LDA <name>` on one of these must go through `hw_mb_write`/
`hw_mb_status`/`hw_mb_ylow`/`hw_mb_yhigh` (`hw.h`), never `g.ram[]`/`cpu_wr`.

| name | addr | offset | function |
|---|---|---|---|
| ACOS / ACOS+1 | $1860/61 | $00/$01 | A register (cosine input) |
| BSIN / BSIN+1 | $1862/63 | $02/$03 | B register (sine input) |
| TPOSXT / +1 | $1864/65 | $04/$05 | E register (distance/rotate-translate X) |
| TPOSYT / +1 | $1866/67 | $06/$07 | F register (distance/rotate-translate Y) |
| XMATRX / +1 | $1868/69 | $08/$09 | X register (rotate input) |
| YMATRX | $186A | $0A | Y register low (rotate input) |
| YSTRT1 | $186B | $0B | Y register high; write **starts YHSM** (rotate multiply: `(x-e)a-(y-f)b`) |
| DIVCYC | $186C | $0C | N (divide/clip iteration count) - **not written by any routine in this module** (MAIN sets it once/frame) |
| ZLOW / ZHI | $186D/6E | $0D/$0E | Z dividend, low WORD (bytes 0,1) |
| DIVOUT / +1 | $186F/70 | $0F/$10 | Z dividend, high WORD (bytes 2,3) - **not a general "divide output latch" despite the name**; see finding below |
| YSTRT2 | $1871 | $11 | Y register high (2nd); write **starts YHSP** ("whole mess": `(xb+ya+f)/(xa-yb+e)`) |
| RSTRT1 | $1872 | $12 | any write **starts SYM** (Y' multiply: `(x-e)b+(y-f)a`) - ROTPNT deliberately reuses the just-read MOUTHI byte as the trigger value (pipelining, see below) |
| ZDIV | $1874 | $14 | any write **starts SZXD** (Z/X' divide) |
| XPL / XPH | $1875/76 | $15/$16 | X' register (divide dividend numerator... actually divisor - see TRACK) |

**Finding (resolves a flagged item in `../disasm/MATHBOX_NOTES.md`'s
"Perspective divide" section):** that file flags DIVOUT/DIVOUT+1 ($186F/70)
as loading only the Z dividend's *high* word, leaving open whether the low
word is "assumed zero" or leftover. Confirmed from the listing: **PNTPUT
explicitly zeroes ZLOW/ZHI (offsets $0D/$0E, the low word) once per object**
(`L5DA3 STX ZLOW` / `L5DA6 STX ZHI`, X=0, right before the per-vertex loop),
and **KLUDGE/PNTPUT then write only DIVOUT/DIVOUT+1 (the high word) per
point** before each SZXD divide. So the low word genuinely is zero, by an
explicit store, not an assumption - worth folding back into
MATHBOX_NOTES.md if that file gets a revision pass (not done here - it's
owned by whoever wrote it, this note just records the resolution).

**ROTPNT's RSTRT1 pipelining trick** (matches MATHBOX_NOTES.md's own
description, verified against the code): after `JSR WAIT` for the X'
result, `LDA MOUTHI` loads the high byte, and `STA RSTRT1` both (a) stores
it as scratch that ROTPNT can still test with `BMI`/`ROL`/`BMI` for the
view-frustum clip, and (b) - because RSTRT1 is offset $12 (SYM, "any write
starts Y' multiply") - simultaneously kicks off the Y' computation while the
6502 is still range-checking X'. Translated as `hw_mb_write(0x12, a)`
immediately after `hw_mb_yhigh()`, with the clip tests running on the same
C variable afterward - same order, same side effect.

### Self-relative table addressing (SIN1)

`SIN1`'s table read is `LDA $5E77,X` / `LDA $5E78,X` (the listing's own
comment: `(src: LDA AX,99$)` / `(src: LDA AX,99$+1)`) - Atari's source
indexed the table via a local numeric label relative to the routine, not by
a symbol. There is no C equivalent; `bzone_math.c` reads it via
`cpu_rd(0x5E77 + idx)` / `cpu_rd(0x5E78 + idx)` - the real ROM address, 65
16-bit entries (130 bytes, $5E77-$5EF8), one quarter-sine table (0..$7FFF
representing 0..1.0).

### ROM tables read (all via `cpu_rd`, none hand-transcribed)

- `SIN1_99` table: $5E77 (65 words) - see above.
- `OBJPNT`: `A_OBJPNT` = $388E (word per object -> its 3-D point-table
  address). Read as `cpu_rd(A_OBJPNT + y)` / `+1`.
- `ARCTAN`: `A_ARCTAN` = $3785, 128 entries, indexed by TRACK's Mathbox
  quotient (shifted right 2).
- `PTBLO1`: `A_PTBLO1` = $3FCC (has a `state_defs.h` alias). `PTBLX1`/
  `PTBLY1` do **not** have `state_defs.h` aliases (not in
  `bzone_defines.asm`) - their real addresses, read directly off the
  listing, are `PTBLX1 = $7681`, `PTBLY1 = $76AB` (both in program ROM,
  words per obstacle, parallel to `PTBLO1`). Defined as local `#define`s
  in `bzone_math.c` with a comment citing the listing, same treatment as
  SIN1_99.

### Object-index scratch cells (RAM, not hardware)

`SAVE1`/`SAVE2`/`SAVE3`, `TEMP1`/`TEMP2`/`TEMP3`, `X2`/`Y1`/`Y2`, `DDEND`,
`DQUE`, `NTHETA`, `GARBAG`, `TBLPTR`, `PNTTBL` are all ordinary RAM
(`state_defs.h` gives them both an `A_*` address and an lvalue macro) -
straightforward `g.ram[]`-backed stores, no seam involved.

### Helper conventions used throughout `bzone_math.c`

- `adc6502`/`sbc6502` (from `state.h`) for every ADC/SBC with carry-in/out
  that crosses more than one instruction (16-bit negates, the TRACK/XTRAP
  compare-and-subtract chains) - decimal-mode-aware for free, though none
  of these routines run in decimal mode (no `SED` anywhere in this module).
- Local `static inline` `asl8`/`rol8`/`ror8`/`lsr8` (carry in/out via
  `unsigned *c`) for the shift-heavy bits (ROTPNT's clip-test shifts,
  TRACK's 2-bit quotient shift, XTRAP's signed halving) - `state.h` only
  supplies ADC/SBC, not shifts, so these are this module's own (matches
  TRANSLATING.md's "write small static inline helpers in your own file").
- A `MB_*` offset macro block (private to `bzone_math.c`, offsets from
  $1860) for readability at every `hw_mb_write` call site - cited against
  MBUDOC.DOC's names in comments.

### Dead code / no-ops observed (translated faithfully, flagged not "fixed")

- `TRACK` `L682D: LDA TEMP1+1` - loads a value that is immediately
  discarded (the next instruction, `SEC`, doesn't consume A, and the
  following `LDA TPOSY` overwrites it). No flags from this load are tested
  either. Emitted as nothing (a comment only), same treatment
  CALLING_NOTES.md gives genuine "waste time" dummy reads - except this one
  isn't documented as deliberate delay, it's just dead code in the original
  ROM.
- `SHRTCK`'s `CMP ($00,X)` / `LDA ($00),Y` "WASTE TIME" dummy reads
  (CALLING_NOTES.md section 3) are **not in this module's routines** -
  `SHRTCK`/`OBJOBJ` belong to the game-logic agent. Nothing in ROTATE/
  ROTPNT/WAIT/PNTPUT/KLUDGE/BOUND/SIN/COS/SIN1/COSXTR/SINXTR/XTRAP/ABSTMP/
  TRACK/T_0-T_3 has a dummy-read delay - checked every JSR WAIT site in this
  module; none has one.

### DSTNCE/distance helpers - explicitly checked, none found in range

The brief asked to check for separate `DSTNCE`/distance-helper *labels*
between TRACK ($6810) and T_0 ($68D5). Read the whole listing across that
range (and PNTPUT/KLUDGE/BOUND/SIN-family/ABSTMP too, since "this range"
was ambiguous): no such label exists. `DSTNCE`/`DST2` ($187D/$187E) are
Mathbox hardware registers only (MBUDOC offsets $1D/$1E), never written by
any routine this module owns - the distance function is used by `SHRTCK`/
`OBJOBJ` (game-logic agent's `SHRTCK`, `OBJOBJ` collision routines), not by
anything here. Recorded so nobody re-checks this.

## Signatures registered (see SIGNATURES.md)

`wait`, `rotate`, `rotpnt`, `pntput`, `kludge`, `bound`, `cos`, `sin`,
`sin1`, `cosxtr`, `sinxtr`, `abstmp`, `track`, `t_0`, `t_1`, `t_2`, `t_3` -
all in `bzone_math.h`. `xtrap` is `static` (internal only, matching VGUT's
own treatment of fall-through-only helpers like `vgwai1` - CALLING_NOTES.md
notes XTRAP is reached only by JMP-tail from COSXTR/SINXTR, never JSR'd).

## Calls into other modules (for integration to check)

- `explde(uint8_t y) -> uint8_t` (EXPLDE $5B89, **game** agent's routine) -
  called from `rotate()` at the ROTATE_33 branch (tank in collision, still
  flashing). Entry/exit both carry the running PTBLX2/PTBLY2/PTBLO2 index in
  Y exactly like `rotpnt` (EXPLDE's own header lists `(Y)` as an output;
  ROTATE's own code relies on Y surviving the call into the shell/saucer
  block right after). Now a real, frozen prototype in `bzone_game.h` -
  matches exactly (see the RESUME section above); `bzone_math.c` includes
  that header directly, no more temporary block.
- `tr7chk(void) -> int` (TR7CHK $69B5, **game** agent's routine) - called
  from `rotate()` twice (tank orientation picture selection, tread-picture
  front/back test). Returns carry only (1 = TR7 present). Same real-header
  treatment as `explde` above.

## Open questions / judgement calls

- `rotpnt`/`explde`'s "Y survives the call" contract is unusual for a C
  signature (most routines here don't need `regs` at all since only one
  register matters) - resolved by making Y both a parameter and the return
  value (`uint8_t rotpnt(uint8_t y)`), letting call sites read exactly like
  the 6502 register does (`y = rotpnt(y);`). Flagging in case the group
  prefers a different shape (e.g. `regs`) for consistency with other
  modules once they exist.
- `ROTATE_11`/`ROTATE_15` etc. are pure fall-through labels (nothing jumps
  to them from outside their own routine) - kept as comments/labels inside
  `rotate()`'s body, not separate functions, matching TRANSLATING.md's rule
  that only genuine JSR/JMP/dispatch targets get their own C function.
- `SAVE1` on the ROTPNT/EXPLDE Y-survival question: `PNTPUT` (`STX SAVE1`)
  and `KLUDGE` (`LDX SAVE1`) share `SAVE1` as an object-index handoff, no
  ambiguity there - noted only because it's easy to confuse with
  `ROTATE`/`EXPLDE`'s Y-index handoff, which is a different mechanism
  (register, not RAM).

## Build

`cl /nologo /W4 /std:c11 /D_CRT_SECURE_NO_WARNINGS /I. /c bzone_math.c` from
`c_src`, run from a real cmd/PowerShell session with
`"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64`
loaded first (Git Bash can't run vcvars). **Result: clean, 0 warnings, exit
code 0** - re-verified by deleting `bzone_math.obj` and rebuilding from
scratch after every source change described in "Fixes made this session"
above (most recently: fresh `.obj`, no diagnostic output at all beyond
`cl`'s own echo of the source filename). The file did **not** compile clean
on the first try: the initial `.obj` build hit warning C4211 on `t_2`
(fix #1 above); after that fix it compiled clean, and the second
(behavioural, non-compiler-visible) fix - the BSIN carry chain - was found
by the instruction-level re-check afterward and re-verified with another
clean from-scratch rebuild. Also re-verified clean after `bzone_game.h`
appeared and `bzone_math.c` switched to including it for real.

## Cycle counting (2026-09-19)

Every routine charges the ROM's cycles with CYC() (TRANSLATING.md "Cycle counting"). Lockstep,
attract 5000 / coin_start 3000 / play_long 4000: WAIT, ROTPNT, PNTPUT, KLUDGE, BOUND, COS, SIN,
SIN1, COSXTR, SINXTR, ABSTMP, TRACK, T_0 exact on every untainted call (T_1..T_3 are only reached
through TRACK's push/RTS dispatch, so lockstep never opens a check on them). ROTATE is annotated but
its totals include bzone_game.c's TR7CHK/EXPLDE, not counted yet.
- TRACK_20 (L6849-L6856) has three cost paths: low bytes differ 9; lows equal/highs differ 21
  (both compares + SEC/BCS); fully equal 22 (both compares + TRACK_22's LDA/BNE). First cut had 13
  and 14 for the last two.
- TRACK's TTAB dispatch (L68B0-L68BD, push/RTS) is 30 cycles; T_n charge their own tails.
- Branch targets that land on a shared RTS (ROTPNT_20, PNTPUT_20) are charged as +1 (taken) +6.
