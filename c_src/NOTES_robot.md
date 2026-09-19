HANDOVER: compiles /W4 clean, ready for lockstep

# NOTES_robot.md - agent "robot" (bzone_robot.c)

## RESUME (read this first if picking this up mid-session)

**Status: DONE.** Every routine in this agent's assignment (ROBOT $6418 and
its whole family through FIREIT $6595-$6623) is translated, written to
`bzone_robot.c`, `bzone_game.h`'s ROBOT-family rows fixed up (see "fireit()
signature correction" below), and the file compiles
`cl /nologo /W4 /std:c11 /D_CRT_SECURE_NO_WARNINGS /I. /c bzone_robot.c`
clean - 0 warnings, 0 errors. `bzone_game.c` was re-compiled too (same
flags) after the `bzone_game.h` edit, to confirm the shared header change
doesn't break the "game" module - still 0 warnings, 0 errors there either
(that module never calls `fireit()` itself, so the signature fix is not
observed by it). A second, independent instruction-by-instruction review
against `../disasm/bzone_program_rom.asm` (re-reading the raw listing text
fresh, not relying on the first pass's notes) was done after the compile
went clean - see "Second-pass review" below. Per the brief: **no further
edits to `bzone_robot.c` after this handover.**

### Per-routine status

| Routine | ROM address | Status | Notes |
|---|---|---|---|
| `robot` | $6418-$64ED | **done** | see "ROBOT structure" below |
| `r_37s_` | $64EE-$6531 | **done** | see "R_37S_ structure" below - corrects the brief's own paraphrase |
| `r_40s_` | $6534-$6537 | **done** | trivial: ATRACT dispatch, falls into `r_50s_` |
| `r_50s_` | $6538-$6551 | **done** | plain decision chain, no hazards |
| `r_evad` | $6553-$6570 | **done** | plain, no carry hazards |
| `r_rand` | $6571-$6586 | **done** | see "R_RAND entry-carry proof" below |
| `r_atck` | $6589-$6592 | **done** | trivial |
| `fireit` | $6595-$6623 | **done** | signature corrected from the pre-existing draft - see below |

All eight registered in `bzone_game.h` (this module's routines only - see
"Signatures registered" below) and in `SIGNATURES.md`'s "BZONE game logic"
section (appended, not overwriting anyone else's rows).

## Which labels got their own C function vs. a `goto` inside `robot()`

Grepped the whole disassembly (`grep -n "JSR R_\|JMP R_\|ROBOT_"`, restricted
to outside lines 3516-3785, the ROBOT block itself) for every label in this
assignment: **none** of `R_37S_`/`R_40S_`/`R_50S_`/`R_EVAD`/`R_RAND`/
`R_ATCK` is ever `JSR`'d/`JMP`'d from outside `ROBOT`'s own control flow -
same as MOTION's internal `M_*` legs (`NOTES_game.md` "MOTION structure").
The one hit outside the block (line 4633) is a plain English comment in the
R2D3CK area ("the same 'attacking' flag R_ATCK sets"), not a real reference.

Despite that, all six get real (non-`static`) functions here, not `goto`
labels inside `robot()`, for two reasons, matching the brief's own explicit
naming of them by address:
1. Each has its own listing header comment block with a `[note]`
   description (`routine_docs.py`'s own routine-boundary detection treats
   them as distinct routines), unlike the plain numbered `ROBOT_nn` locals
   (`ROBOT_1`, `ROBOT_2`, ... `ROBOT_32`), which really are just internal
   control-flow labels and stayed as `goto` targets inside `robot()`.
2. `FIREIT` is unambiguously a real routine (`JSR`'d 8 times from within
   `ROBOT` alone) that never `RTS`s back to a "top" - every one of its exit
   points is a plain `RTS`, and `ROBOT` itself repeatedly calls it and
   keeps going, the same "callable, but only from one family" shape as
   `R_37S_`/`R_40S_`/etc (which typically end with `JMP BAKOUT` / `JMP
   REACT`, i.e. a tail call, never returning to `ROBOT` at all). Modelling
   all seven the same way (real functions, `void name(void)`) keeps the
   file consistent and gives lockstep per-routine coverage of each - the
   same trade-off `bzone_math.h` made for `T_0`..`T_3` ("exposed non-static
   ... for direct lockstep coverage" even though only reached internally).

`ROBOT_1`..`ROBOT_32` are NOT exposed (no header entries) - they are pure
internal control flow inside `robot()`, `goto`-labelled with their ROM name
in a comment for readability, matching `bz_main()`'s/`react()`'s own style
for `MAIN_nn`/`REACT_nn`.

## ROBOT structure (for whoever reads it fresh)

- **Register contract**: no inputs/outputs. `x=2` (the robot) is hardwired
  throughout - `LDX #$02` once at L6424 (right before `JSR SAVEIT`), never
  reloaded anywhere else in `ROBOT`'s own body (confirmed by reading every
  line 3516-3646) - every callee below takes `x=2` as its own C parameter
  instead of relying on a live 6502 X register, so there is nothing to
  thread through this function.
- **ROBOT_2's `PHP`/`PLP` (L6436/L643A) is a REAL, necessary carry save**,
  translated literally with an explicit `saved_c` local scoped to that one
  block - same category as EXPLDE's own PHP/PLP (`NOTES_game.md` "EXPLDE
  structure"), NOT MOTION's `M_NEX0`-style "simplify away" (`NOTES_game.md`
  "MOTION structure"). Traced by hand: the carry being saved is the
  *second* `LSR STATE`'s carry-out (original `STATE` bit 1, since the
  first `LSR` already consumed bit 0 into its own carry and shifted the
  copy in `A`), and `JSR M_REV` between the `PHP` and `PLP` genuinely does
  several `ADC`/`SBC`s internally (via `m_set`/`m_for1`-shaped code) that
  would otherwise destroy it before the `BCC ROBOT_7` that follows the
  `PLP` reads it. No possible simplification here (unlike `M_NEX0`, where
  the thing being protected against - an intervening `LDX` reload - simply
  doesn't exist in this C model): `m_rev()` is an opaque call as far as
  this function is concerned, so the save/restore must be literal.
- **ROBOT_11's abs-value computation** (`SBC RGOAL`/`TAY`/`BPL`/`EOR #$FF`/
  `CLC`/`ADC #$01`) is the same idiom already established in
  `SHRTCK_30`/`C_21S_` (`NOTES_game.md`) - translated the same way, with
  the pre-negation value kept in `y` (a plain C local, not the CPU's own Y)
  for the later turn-direction tests at `ROBOT_12`/`ROBOT_20` (`TYA`).
- **`ROBOT_30`'s Mathbox distance check reads `TEMP1`/`TEMP2` as a hidden
  RAM input, NOT freshly computed here** - `TRACK`'s own header
  ("TEMP1=ABS VALUE(Y), TEMP2=ABS VALUE(X)", `bzone_math.h`) is the only
  thing in this ROM that sets them, and `ROBOT_30` itself never calls
  `TRACK` - it relies on whatever the last `TRACK` call (inside `FIREIT_5`,
  earlier THIS SAME call to `fireit()` two lines up, or a stale value left
  over from an EARLIER FRAME's `R_EVAD`/`R_ATCK` decision if `fireit()`
  returned early via one of its own guard clauses) happened to leave there.
  This needs nothing special in the C model - `TEMP1`/`TEMP2` are ordinary
  `state_defs.h` RAM cells, so simply not touching them on `fireit()`'s
  early-return paths reproduces the exact same "possibly stale" behaviour
  the ROM has - but it is worth flagging for lockstep as a genuine
  cross-frame RAM dependency (same category as `NEWLIF`'s `TEMP1`,
  `NOTES_game.md`/`SIGNATURES.md`'s "CORRECTION" entry).
- `STA DST2` at L64D9 stores the SAME accumulator value just used for
  `STA TPOSYT+1` (both loaded from one `LDA TEMP2+1`) - translated by
  literally reusing the same C `a` for both `hw_mb_write` calls, not
  reloading anything.
- `JSR TR7CHK` immediately followed by `LDA MOUTHI` (L64DC/L64DF) needs no
  dummy-read comment (unlike SHRTCK's `CMP ($00,X)`/`LDA ($00),Y` pair) -
  the `JSR TR7CHK` call itself is the "wait a few cycles for the Mathbox"
  delay, and its own return carry is read immediately after (the
  intervening `LDA` doesn't touch C) - same idiom, different delay
  instruction.

## R_37S_ structure (for whoever reads it fresh)

- **Correction to the brief's own paraphrase**: the brief describes the
  BAKOUT tail as "sets a STATE bit and ACTION=$30 unless an attract-mode
  hit". Reading the actual bytes (L651A-L6531, quoted in the file's own
  header comment) shows the real gate is the OPPOSITE of what that phrase
  suggests taken literally: **attract mode ALWAYS sets STATE/ACTION**
  (`BIT ATRACT`/`BPL R_37S__42` jumps straight past the check when
  ATRACT bit 7 is CLEAR = attract mode); it is a REAL GAME collision
  **with the player tank specifically** (OBJOBJ's own `A` return negative,
  i.e. `$FF`) that skips the STATE/ACTION escalation and goes straight to
  `BAKOUT` - matching the ROM's own inline comment at L651E, `;D IF
  COLLIDED WITH PLAYER`, verbatim. Non-tank obstacle hits (A=$00) during a
  real game DO get the escalation, with a PRAND-derived bit1 instead of
  the raw OBJOBJ return. The brief's phrasing was presumably a compressed/
  imprecise gloss of the `[note]` header, not the code itself - translated
  from the actual bytes, not the gloss, per TRANSLATING.md's own
  instruction to check `[note]` headers against the code (they're
  "reliable" per the brief, but still secondary to the bytes).
- `OBJOBJ`'s own `regs.a` return (`0` non-tank / `$FF` tank, per
  `bzone_game.h`'s existing contract) flows unchanged from either of
  `R_37S_`'s two `JSR OBJOBJ` call sites all the way to the `R_37S__39`
  block - `BCS` (the collision branch) doesn't touch `A`, and nothing
  between the second `OBJOBJ` call and `R_37S__39` does either. Modelled
  by re-reading `r.a` from the last `objobj()` call's `regs` result at
  `R_37S__39`'s own first line, which is correct on both incoming paths
  since both assign the same local `r` right before branching there.
- The `ASL`/`ROL` doubling of `Y1`/`X1` (TR7-class robots move faster) has
  no residual-carry hazard: `ASL` never reads carry-in (only produces
  carry-out from the shifted bit), so `ASL Y1`/`ROL Y1+1` and the following
  independent `ASL X1`/`ROL X1+1` pair are each self-contained even though
  nothing resets `c` between the two pairs.

## R_RAND entry-carry proof (the hazard TRANSLATING.md specifically warns about)

`R_RAND`'s own `SBC RGOAL` (L657A) and the fall-through `ADC RGOAL`
(L657E) have no `CLC`/`SEC` immediately before them - both depend on
whatever carry is already set when `R_RAND` is entered. Traced by hand,
instruction by instruction, back from BOTH of `R_RAND`'s real call sites
to the last thing that could have touched `C`:

1. **Via `R_40S_`'s `BPL R_RAND`** (L6536): `BIT ATRACT` (L6534, the only
   instruction between `R_40S_`'s entry and this branch) does not touch
   `C`. `R_40S_` itself is entered only via `ROBOT_10`'s `JMP R_40S_`
   (L6460), which is reached only when `ROBOT_10`'s `LDA ACTION`/`BEQ`
   (L645C/L645E, `LDA` is N/Z-only) falls through - and `ROBOT_10` itself
   is entered only from `ROBOT_2`'s `LDA STATE`/`LSR`/`BCC ROBOT_10`
   (L6430-L6433): the `BCC` is taken (landing at `ROBOT_10`) exactly when
   that `LSR`'s own carry-out is 0. Since branching does not itself alter
   `C`, and every instruction between that `BCC` and `R_RAND`'s first real
   `SBC`/`ADC` (`LDA ACTION`, `BIT ATRACT` - both N/Z/V-only) leaves `C`
   untouched, carry entering `R_RAND` via this path is **provably 0**.
2. **Via `R_50S_`'s `BCC R_RAND`** (L654F): this one is even more direct -
   `BCC` branches exactly when `C=0`, and `R_RAND`'s own first instruction
   runs immediately after that same branch with no intervening code, so
   carry is **trivially 0** on this path too (it's the very carry that
   caused the branch).

**Both real entries arrive with carry clear.** This is a genuine but
CONSTANT residual (not runtime-ambiguous), so `r_rand()` models it as a
plain local `unsigned c = 0;`, not a parameter - documented at length in
the function's own header comment too, per the brief's explicit warning to
"find where the carry comes from and thread it" rather than guess.

This also settles, as a side effect, that the `ADC RGOAL` at `R_RAND_10`
genuinely has TWO different meanings depending on how it's reached: carry
in = 0 (the outer residual) when reached directly via the `BMI` from
`S_INTCT`'s sign bit test, or carry in = the real carry-OUT of the
immediately preceding `SBC RGOAL` when reached by falling through from a
zero result. Both are modelled correctly by simply not resetting the
shared `c` between the two - see the code's own comment on `r_rand()`.

## fireit() signature correction

The pre-existing `bzone_game.h` draft had `void fireit(uint8_t x);`
("X = 0 tank, 2 robot", presumably guessed by analogy with `SAVEIT`/
`ITANGL`/etc before anyone had read `FIREIT`'s own body). Reading the
actual bytes (L6595-L6623) shows every single cell `FIREIT` touches uses a
literal `+2`/`+3` offset (`FIRECT+2`, `TANGLE+2`, `SINCX+2`/`+3`,
`SINCY+2`/`+3`, `TPOSX+2`/`+3`, `SHELLX+2`/`+3`, `TPOSY+2`/`+3`,
`SHELLY+2`/`+3`) - never a `,X`-indexed form anywhere - and it is `JSR`'d/
`JMP`'d only from inside `ROBOT`, always with `X` already `2`. **Fixed to
`void fireit(void);`** in `bzone_game.h` (the only existing call site,
`b_end()` calling `robot()`, is unaffected - nothing outside this module
called the old `fireit(x)` signature either, confirmed by grep before the
edit). `bzone_game.c` re-compiled clean after this header change to
confirm it doesn't break the "game" module (it doesn't call `fireit()` at
all). The `LDX #$02` mid-routine (L65B9) and the final `LDX #$02` before
`RTS` (L6621) are both pure 6502-register bookkeeping with zero effect in
this C model (every access here is by literal offset, never by register
index) - noted as comments, nothing emitted, same treatment TRANSLATING.md
gives deliberate dummy reads.

## Possible ROM quirk, translated faithfully (flagged for lockstep, not "fixed")

`FIREIT`'s own "fire!" block (L65D9-L65DD) does `LDA SNDWRD`/`AND
#SOFTSH`/`STA SNDWRD` - the TANK's own (unindexed, index-0) `SNDWRD`, not
`SNDWRD+2` (the robot's). This clears the loud-shell-sound bit on the
PLAYER's sound word as a side effect of the ROBOT firing a shell. This is
the same shape of asymmetry `NOTES_game.md`/`SIGNATURES.md` already
flagged for `C_21S_`'s unindexed `EXPCNT`/`SNDWRD` stores vs. `SAUCHK`'s/
`SHRTCK`'s indexed ones - could be intentional (maybe the "loud shell" flag
is meant to describe only the player's own audio channel regardless of who
fired) or a genuine ROM bug. Translated exactly as the listing has it, not
"fixed" - `SOFTSH` ($F7) confirmed to equal `K_SOFTSH` in `state_defs.h`
before relying on it (`../disasm/bzone_defines.asm` line 383).

## REACT entry-A resolved (closes NOTES_game.md's open flag)

`NOTES_game.md`'s `react()` entry states R_RAND/R_ATCK "tail-call
`react()`... **when translating those, make sure they call `react(a)`
with whatever `a` they hold at their own tail-jump site... and update this
note**" (the Atari header for `REACT` says "REGISTERS: NONE", flagged
there as possibly stale). Confirmed by hand-tracing both tail sites:
- `R_RAND` (`R_RAND_20`): `LDA #$00`/`STA STATE`/`JMP REACT` - `STA`
  doesn't touch `A`, so `REACT` is entered with `A=0x00`. Modelled as
  `react(0x00);`.
- `R_ATCK`: `LDA #$80`/`STA STATE`/`JMP REACT` - same shape, `A=0x80`.
  Modelled as `react(0x80);`.

Both confirm `bzone_game.h`'s already-corrected `void react(uint8_t a);`
signature is right, and that `a` really is live on entry from both of this
module's tail-jump sites - nothing further to fix on either side. (This
note lives here, in this module's own file, per TRANSLATING.md's "own only
your files" - `NOTES_game.md` itself is the "game" agent's file and is not
edited here; flagging the resolution for whoever next touches that file or
does integration.)

## Second-pass review

After the file compiled `/W4` clean, re-read the raw listing text fresh
(not this file's own derived notes) for every branch/carry decision point
and cross-checked against the written C, specifically:
- `ROBOT`'s full L6418-L64ED range, instruction by instruction, including
  re-verifying the `BEQ`/`BNE`/`BPL`/`BMI`/`BCC`/`BCS` sense at every branch
  (easy to get backwards - `BPL`/`BCC` both branch on a flag being *clear*).
- `R_37S_`'s L64EE-L6531 range, specifically re-deriving the `BIT ATRACT`/
  `BPL`/`ORA #$00`/`BMI` gate from the raw bytes a second time independent
  of the first pass's reasoning (see "R_37S_ structure" above - this is
  where the brief's own paraphrase and the actual code diverge).
- `R_40S_`/`R_50S_`/`R_EVAD`/`R_RAND`/`R_ATCK`'s full L6534-L6592 range.
- `FIREIT`'s full L6595-L6623 range, including re-confirming the `SINCX`/
  `SINCY` triple-byte-doubling idiom against `bzone_game.c`'s own
  `m_nex0()` (identical shape, gave high confidence quickly).

No discrepancies found between the second pass and the code as written -
no further changes made as a result (matches "no further edits" for the
handover, since the review didn't call for any).

## Signatures registered

`SIGNATURES.md`'s "BZONE game logic" section: appended a `### ROBOT family
(owner: agent "robot", bzone_robot.c)` block with all eight routines'
final signatures/behaviour, right after the existing `tr7chk()` entry (did
not touch any other agent's rows). `bzone_game.h`: fixed the ROBOT-family
comment block and `fireit()`'s signature (see above); left every other
prototype in that header (all the "game"/"enemy" owned rows) untouched.

## Calls into other modules (for integration to check)

- `bzone_math.h`: `track()` (R_EVAD, R_ATCK, FIREIT), `cos_()`/`sin_()`
  (FIREIT).
- `bzone_game.h` (this module's own header, but calling INTO routines owned
  by "game"/"enemy", not this agent): `saveit`/`m_rev`/`itangl`/`dtangl`/
  `m_set`/`m_for1`/`bakout`/`react`/`tr7chk` (all "game"-owned, already real
  headers, already translated); `buzbom`/`objobj` ("enemy"-owned - `buzbom`
  still only a draft `void buzbom(void);` prototype, not yet defined
  anywhere, fine for a `/c` compile-only build; `objobj` likewise a draft
  `regs objobj(uint8_t x);` - both consumed exactly as documented in
  `bzone_game.h`, no signature changes needed on either).
- `hw.h`: `hw_pokey_read(0x0A)` (PRAND, in `R_50S_`/`R_RAND`/`R_37S_`),
  `hw_mb_write()`/`hw_mb_yhigh()` (`ROBOT_30`'s distance check).
- No `hw_pokey_write`, no `hw_sound`/`hw_emcnt`/`hw_goadd`/etc anywhere in
  this assignment - `FIREIT`/`ROBOT` are pure RAM+Mathbox+POKEY-read logic.

## Open questions / flags for lockstep and integration

1. **`ROBOT_30`'s stale `TEMP1`/`TEMP2` Mathbox inputs** (see "ROBOT
   structure" above) - a genuine cross-frame RAM dependency, not a bug,
   but worth a lockstep scenario that exercises `ROBOT` on a frame where
   `FIREIT` returns early via one of its guard clauses, to confirm the
   distance check still uses whatever `TRACK` last left there rather than
   garbage.
2. **`FIREIT`'s unindexed `SNDWRD` write** (see "Possible ROM quirk" above)
   - flagged, not fixed; if lockstep ever disagrees here, check this first.
3. **`R_37S_`'s STATE/ACTION gate** (see "R_37S_ structure" above) is the
   one place this session's own reasoning diverged from the brief's own
   paraphrase - re-verify against a real trace if lockstep flags `R_37S_`,
   though the code was written from the raw bytes, re-verified twice, and
   both readings of the raw text agreed.
4. **Buzz-bomb / OBJOBJ dependencies**: `robot()`'s tail call to `buzbom()`
   and `r_37s_()`'s call to `objobj()` cannot be exercised standalone until
   the "enemy" agent hands those over - this module is otherwise complete
   and self-contained (`tr7chk`, `saveit`, `m_rev`, `itangl`, `dtangl`,
   `m_set`, `m_for1`, `bakout`, `react`, `track`, `cos_`, `sin_` are ALL
   already real, translated code from "game"/"maths").

## Cycle counting (2026-09-19)

ROBOT, R_37S_, R_40S_, R_50S_, R_EVAD, R_RAND, R_ATCK, FIREIT all charge the ROM's cycles with
CYC(). FIREIT is exact in all three standard lockstep runs. ROBOT/R_40S_ (and the R_* legs they
reach by branch, which lockstep never opens as separate checks) include bzone_game.c callees
(SAVEIT, M_REV, M_SET, M_FOR1, ITANGL, DTANGL, BAKOUT, REACT, TR7CHK) and bzone_enemy.c's OBJOBJ,
so they can only match once those modules are counted.
- ROBOT's branches into R_37S_ (L64E6/L64EB BCS) are +1 each, no JSR.
- R_50S_'s final BCS (L6551) is always taken: 3 cycles.
- FIREIT's sign-extends (L65E9/L6602): negative BMI(3)+LDA(2)=5, positive BMI(2)+LDA(2)+BEQ(3)=7.
