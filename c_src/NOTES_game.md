# NOTES_game.md - agent "game" (bzone_game.c/.h)

## RESUME (read this first if picking this up mid-session)

**Status as of this checkpoint (session 2): MAIN (`main_init`/`bz_main`/
`b_end`, with `main_pass` now a thin wrapper), `explde`, `qwikck`/
`shrtck`, `colchk`/`sauchk`(+static `c21s_`), `newlif`/`exinit`, the WHOLE
MOTION cluster (`motion`, `m_set`/`m_for`/`m_for1`/`m_rev`/`itangl`/
`dtangl`/`saveit`/`bakout` public + ~14 `static` M_* legs), `react`, and
`tr7chk` (translated early/out of order once its trivial body was read
while chasing its carry convention) are ALL translated, written to
`bzone_game.c`/`bzone_game.h`, SIGNATURES.md updated, and the whole file
compiles `cl /W4 /std:c11 /c bzone_game.c` CLEAN (verified after every
routine group this session - all the other agents' real headers now
exist, see "Compile status" below). Roughly HALF the module's routine
list is done. Remaining: ROBOT family (the next single biggest chunk),
FIREIT, BUZBOM, SAUCMV, OBJOBJ, ROBCHK, TANKCK, R2D3CK, ROB1. Exact next
step: ROBOT ($6418) and its family - budget real time, do NOT rush it,
same as MOTION.**

### Per-routine status

| Routine | ROM address | Status | Notes |
|---|---|---|---|
| `main_init` | $5000 (BZSTST one-time part, none owned here) | **done** | trivial, matches Tempest's `mainln()` split |
| `main_pass` | trampoline | **done** | superseded: now the pass_signal trampoline loop (bzone_game.h) - one bounded pass per call; `bz_main()`/`b_end()` run one segment each. Later fix: `m_nex0()` was missing `L62C0 STX SAVE1` (NOTES_integration.md #13) |
| `bz_main` | $5000-$5158 | **done** | was `main_pass`'s body; renamed/split this session - see "bz_main/b_end split" below |
| `b_end` | $5159-$51D8 | **done** | split out of `bz_main` this session - see "bz_main/b_end split" below |
| `explde` | $5B89-$5C5B | **done** | see "EXPLDE structure" below for the loop-bound finding and the carry/PHP hazards |
| `qwikck`/`shrtck` | $5F4E/$5F5B | **done** | see "QWIKCK/SHRTCK structure" below |
| `colchk` | $6045 | **done** | see "COLCHK/SAUCHK/C_21S_ structure" below |
| `sauchk` (+ static `c21s_`) | $60C2 (C_21S_ $6061) | **done** | signature changed from the old draft `void sauchk(void)` to `void sauchk(uint8_t x, uint8_t y)` - it genuinely reads both; see structure notes below |
| `newlif`/`exinit` | $615F/$6199 | **done** | `exinit` signature corrected to take `x` (see structure notes below); NEWLIF's `TEMP1` read also corrected the earlier "dead store" misreading of SHRTCK/SAUCHK |
| `motion` + `M_*` | $61D0 | **done** | see "MOTION structure" below; ~14 internal `M_*` legs are `static` (never entered from outside - confirmed by grep), `m_set`/`m_for`/`m_for1`/`m_rev`/`itangl`/`dtangl`/`saveit`/`bakout` are public (ARE called from outside, by ROBOT/BUZBOM - confirmed by grep) |
| `react` | $63CB | **done** | signature corrected from the draft's `uint8_t x` to `uint8_t a` (REACT doesn't touch X at all; A genuinely flows in from its `JMP`-tail callers on one path) - see "REACT structure" below; entered via `JMP` from R_RAND/R_ATCK (ROBOT family, not yet translated) - **when translating those, make sure they call `react(a)` with whatever `a` they hold at their own tail-jump site, and update this note** |
| `robot` + family (`ROBOT_nn`, `R_37S_`, `R_40S_`, `R_50S_`, `R_EVAD`, `R_RAND`, `R_ATCK`) | $6418 | not started | **exact next step** - asm lines ~3516-3785; `R_RAND`/`R_ATCK` tail-call `react()` (now done, see above) |
| `fireit` | $6595 | not started | |
| `buzbom` | $6624 | not started | asm lines ~3896-4133 |
| `saucmv` | $677C | not started | stop before `TRACK` (maths agent's), asm lines ~4133-4249 |
| `objobj` | $68D6 | not started | |
| `tr7chk` | $69B5 | **done** | translated early (out of the brief's order) - its body turned out to be the same 4 lines already fully understood while resolving its carry convention for EXPLDE/SHRTCK; see per-routine table entry history in earlier sessions if curious, nothing more to add |
| `robchk` | $69BE | not started | shared with NMI - flag for nmi-sound-coin agent once written; called (forward reference, fine) from `explde()` |
| `tankck` | $69E8 | not started | called from `bz_main` MAIN_1 |
| `r2d3ck` | $6A22 | not started | called from `bz_main` MAIN_3 |
| `rob1` | (before DRADAR $6AE9) | not started | |

### bz_main/b_end split (session 2 finding)

While translating EXPLDE this session, the display agent's module
(`bzone_disp.c`/`.h`) had already landed with WNSHLD/CKSCOR/HISCRE/DISTBL
calling two names this module never exposed: `bz_main()` (MAIN's own re-entry
point, `JMP MAIN`) and `b_end()` (MAIN's mid-routine `B_END` label,
`JMP B_END`) - see SIGNATURES.md's display section, "tail-calls
`bz_main()`/`b_end()`". Rather than ask display to change already-written,
presumably-reviewed code, this module adopted those names as the real
contract (SIGNATURES.md's own rule: "the owner adopts it or fixes both
sides" - I own MAIN):
- The old `main_pass()` body is now `bz_main()` (MAIN $5000-$5158).
- The old inline `B_END:` label section (from `info()` to the tail) is now
  its own function `b_end()` (B_END $5159-$51D8), called by `bz_main()`
  where it used to fall through to the label, and reachable directly by
  WNSHLD/CKSCOR per their own `JMP B_END`.
- `main_pass()` is kept as a one-line wrapper (`{ bz_main(); }`) for the
  PLAN.md/TRANSLATING.md-documented harness-driver name, in case a future
  milestone's driver code looks for it by that name.
- **Consequence, flagged for integration**: every one of MAIN's own
  `JMP MAIN` sites (`bz_main()`'s two tail calls into `hiscre()`/`distbl()`/
  `wnshld()`, already fine since those routines themselves now call
  `bz_main()`/`b_end()`; and `b_end()`'s own two `B_END_20` sites) now calls
  `bz_main()` directly and recurses, rather than doing a plain `return` back
  to an external per-frame driver loop. This is faithful to the ROM (MAIN
  really is one continuous `JMP`-chained loop, never `RTS`s anywhere), and
  it's what display's already-written tail calls require to work at all -
  but it means a real run (or a long lockstep test) grows the C call stack
  by one frame's worth of locals per video frame, unbounded, unless the
  compiler tail-call-optimizes every one of these `f(); return;` sites (not
  guaranteed by the C standard). **Integration should check `tests/lockstep.c`
  either drives frames by calling individual routines directly through its
  own adapter table (bypassing `bz_main`'s free-running recursion entirely -
  the likely intent, given "lockstep... call by call"), or add a trampoline/
  loop-based rewrite of this recursion before any real multi-thousand-frame
  run.** Not fixable unilaterally from this module without breaking the
  display module's already-written contract.

### EXPLDE structure (for whoever reads it fresh)

- **Loop-bound finding (resolves the old "6 vs 8 slots" open question)**:
  EXPLDE's own per-frame physics loop only visits X = $0A,$08,...,$00 (SIX
  of the EXNOBJ=8 slots, byte offsets 0/2/4/6/8/10). This is NOT a bug or a
  mismatch to "fix" - cross-referencing R2D3CK ($6A22, still unwritten but
  read ahead this session to settle this) shows the OTHER two slots' Z-cells
  (`EXPOSZ+$C`/`EXPOSZ+$D`, i.e. offsets 12/14) are deliberately reused by
  R2D3CK as the buzz-bomb's own Z-depth ("STARTZ"), not as an explosion
  piece - `STA EXPOSZ+$D` / `STA EXPOSZ+$C` right there in R2D3CK. EXPLDE's
  loop starting at X=$0A (not $0E) is exactly right: it must never touch
  the two cells the buzz-bomb is using for something else while it's alive.
  Translated as a literal `x = 0x0A` start, not derived from `K_EXNOBJ`.
- **Register contract**: only `y` is a real input/output (the running
  PTBLX2/PTBLY2/PTBLO2 write index) - confirmed by checking EXPLDE's only
  call site (ROTATE, `L5946 LDY #$00` ... `L5990 JSR EXPLDE`, no intervening
  Y-register instruction, so Y is always 0 at entry in this ROM, but the
  routine is still modelled generically per the Atari header's own "(Y)"
  output and SIGNATURES.md's register rule). X is NOT an input - `LDX #$0A`
  at EXPLDE's very first instruction discards whatever X held.
- **Two real (non-accidental-looking, but still worth flagging) residual-
  carry hazards**, both translated by simply not touching the shared local
  `c` on the paths that rely on the residual:
  1. `EXPLDE_8`'s `ADC #<GRAVTY` (L5BF1): reached two ways. Via the `CMP
     #$85`/`BCC EXPLDE_10`-not-taken path, carry-in is always 1 (falling
     through requires the CMP's carry to have been set). Via the direct
     `BPL EXPLDE_8` path (ZVELOC already non-negative), carry-in is
     whatever the earlier 16-bit Z-position `ADC EXPOSZ+1,X` (L5BE2) left -
     a genuine residual, since `LDA`/`BPL` between them don't touch C.
  2. The orientation calc's `PHP`/`PLP` (L5BF9/L5BFF) is a DELIBERATE (not
     accidental) carry save: it snapshots the carry right after the second
     `LSR` (bit 1 of the piece's X-offset) and restores it after an
     unrelated `TXA/ASL/ASL/ADC #$03` sequence that would otherwise have
     clobbered it. Modelled with an explicit `saved_c` local, scoped to
     that one block only (not the shared per-instruction `c`), matching a
     real `PHP`/`PLP` pair rather than the file's other "just don't touch
     the variable" residual-carry style.
- Bit-level helpers added this session (reusable by every later routine in
  this file): `asl6502`/`rol6502`/`lsr6502` (carry in/out, same shape as
  `state.h`'s `adc6502`/`sbc6502`) and `cmp6502` (binary-only, sets carry +
  optional Z, never touches A) - see the "Bit/carry helpers" block near the
  top of `bzone_game.c`. Expect to need `ror6502` too once MOTION/ROBOT are
  translated; add it there the same way (only add helpers actually used, to
  keep `/W4` from flagging unused statics - `static inline` mostly avoids
  the warning anyway, but no need to find out the hard way).
- `TR7CHK`'s carry convention is now confirmed (see the per-routine table
  above) - EXPLDE's own `L5C28 JSR TR7CHK`/`L5C2B BCC EXPLDE_27` matches
  `if (!tr7chk()) goto EXPLDE_27;`.

### QWIKCK/SHRTCK structure (for whoever reads it fresh)

- Register contract confirmed against the Atari header: QWIKCK takes no
  inputs (hardwires X=0/Y=2, "player's shell vs. robot only"); SHRTCK reads
  only X (whose shell: SHELLX/SHELLY/FIRECT/HITS/SNDWRD/EXPCNT) and Y
  (whose position is the target: COLFLG/TPOSX/TPOSY/EXPCNT/SNDWRD) - the
  existing draft signatures in `bzone_game.h` (`int qwikck(void);
  int shrtck(uint8_t x, uint8_t y);`) were already right, no change needed.
- `L5F8C CMP ($00,X)` / `L5F8E LDA ($00),Y`: the SAME deliberate dummy-read-
  before-Mathbox-result idiom TRANSLATING.md calls out by name ("Deliberate
  dummy reads used as delays (e.g. `CMP ($00,X)` before reading the Mathbox
  result) have no effect: leave a comment, emit nothing") - translated as
  a comment only, exactly per that instruction.
- `SHRTCK_16`'s `BNE SHRTCK_22` (L5F9E) is reached two different ways with
  two different meanings for the shared accumulator `a`: falling all the
  way through from the Mathbox-result shifts (a = MOUTHI>>2), or jumping
  straight there from `L5F72`'s `COLFLG,Y != 0` check (a still holds
  COLFLG,Y's own nonzero value, untouched by the jump). Both are correct
  under a literal "one shared `a` local, `goto` for control flow" model -
  no special-casing needed, unlike EXPLDE's PHP/PLP which genuinely needed
  a separate saved-carry variable.
- The R2D3/TOP height check (`SHRTCK_17`'s lead-in) reads `EXPOSZ+$C`/
  `EXPOSZ+$D` - confirms the EXPLDE loop-bound finding from last session:
  these two cells really are the buzz-bomb's own Z-depth, read back here by
  a completely different routine, not explosion-piece slots.
- `L5FD1`'s `ADC #$38` has NO `CLC` before it (unlike its neighbour two
  lines up) - deliberate residual carry from the immediately preceding
  `ADC TEMP1`, translated by simply not resetting the shared `c`.
- Added `ror6502` to the "Bit/carry helpers" block this routine (needed for
  the `ROR TEMP3` pair at L5F9B/L5FA0) - same shape as `asl6502`/`rol6502`/
  `lsr6502`.
- `TR7CHK`'s call site here (`L6007 JSR TR7CHK`/`L600C BCS SHRTCK_90`) is a
  second independent confirmation of the carry convention already nailed
  down while translating EXPLDE: `if (trc) goto SHRTCK_90;` where
  `trc = tr7chk()`.

### COLCHK/SAUCHK/C_21S_ structure (for whoever reads it fresh)

- COLCHK, despite its own Atari header only mentioning
  "SUBROUTINES: ABSTMP, EXINIT" (ABSTMP is never actually called in this
  cluster - stale/misleading Atari doc comment, not a translation bug),
  turned out to also unconditionally call `SHRTCK` and, when that finds no
  collision, `SAUCHK`, which itself always falls through to `C_21S_`
  (playfield-obstacle scan) regardless of whether the saucer check hit or
  missed. All FOUR labels (`COLCHK`, `SAUCHK`, `C_21S_`, and the shared
  loop-bottom `C_60S_`) form ONE connected control-flow graph over a single
  `X = 2, 0` loop (`C_10S_`) - `C_60S_`'s own `JMP C_10S_` loops back into
  COLCHK's own top. Modelled as: `colchk()` owns the `X` loop and its own
  early-exit checks inline; `sauchk(x, y)` and the `static c21s_(x)` helper
  each do their own thing and simply `return` on every exit (never needing
  to know whether they're "supposed to" continue scanning or stop) because
  EVERY exit from either one reaches `C_60S_` regardless of outcome -
  so `colchk()` just unconditionally continues to its own loop-bottom
  after calling `sauchk()`, no shared value needs to flow back.
  `C_21S_` is `static` (never `JSR`'d/`JMP`'d from outside this cluster,
  confirmed by grepping the whole disassembly) - same convention
  `coin65.c`/`bzsoun.c` already use for their own internal-only labels.
- **`sauchk()`'s signature changed** from the pre-existing draft
  `void sauchk(void)` to `void sauchk(uint8_t x, uint8_t y)` - reading the
  actual body shows it genuinely uses both (X for FIRECT/EXPCNT/the HITS
  score-add gate, Y for `SNDWRD,Y |= LOX` on a hit) - the old draft was
  written before anyone had read SAUCHK's code. `colchk()`'s own call site
  already uses the corrected shape (nothing else called the old one).
- **Possible ROM quirk, translated faithfully, not "fixed"**: `C_21S_`'s
  own obstacle-hit path (`L60AA`-`L60B2`) stores to PLAIN (unindexed)
  `EXPCNT`/`SNDWRD`, while `SAUCHK`'s saucer-hit path (`L610F`-`L6116`) and
  `SHRTCK`'s tank-hit path both index them by X. Could be intentional
  (obstacle explosions always sound on channel/slot 0) or a genuine ROM
  asymmetry - flagged for lockstep, translated exactly as each listing has
  it.
- `PTBLX1`/`PTBLY1` ($7681/$76AB) and `PRXTBL` ($6139) are disassembler-
  local labels with NO `state_defs.h`/`bzone_defines.asm` alias (unlike
  their sibling `PTBLO1`, which IS exported as `A_PTBLO1`) - referenced in
  `c21s_()` as literal ROM hex addresses via `cpu_rd`, matching how
  `bzone_math.h`'s notes handle `SIN1`'s own real-address-only table.
- The `CMP #$80`/`ROR` pairs in `C_21S_`/`SAUCHK` (vs. `SHRTCK`'s plain
  `LSR` for a similar-looking shift) are the standard 6502 "no ASR
  instruction" idiom: `CMP #$80` sets carry = the old sign bit, then `ROR`
  replicates it into the new top bit while shifting right - a sign-
  preserving arithmetic shift, not the zero-filling logical shift `LSR`
  gives. Translated with the existing `cmp6502`/`ror6502` helpers (no new
  helper needed - `cmp6502(a, 0x80, &c, NULL)` is exactly `a & 0x80 != 0`).
- **CORRECTION** (superseded by NEWLIF's own header): the `TEMP1 =
  HITS[...]` save right before `SAUCHK`'s/`SHRTCK_18`'s decimal HITS add
  is NOT a dead store - it is `NEWLIF`'s own documented hidden RAM input
  ("TEMP1 MUST HAVE SCORE PRIOR TO CURRENT INCREMENT"), read back by
  `NEWLIF`'s `CMP TEMP1`. See "NEWLIF/EXINIT structure" below.

### NEWLIF/EXINIT structure (for whoever reads it fresh)

- `NEWLIF`'s header ("USES (A) - TEMP1 MUST HAVE SCORE PRIOR TO CURRENT
  INCREMENT") together with its own first instruction (`LDA OPTION`,
  clobbering A immediately) settles that "(A)" in this ROM's header
  convention sometimes just means "clobbers the accumulator", not "reads
  it as an input" - A is NOT a real parameter here. `TEMP1`, on the other
  hand, genuinely is read (`CMP TEMP1`) - this is the RAM cell
  `SHRTCK_18`/`SAUCHK` each set to the pre-increment score right before
  their own decimal `HITS` add, and it directly resolves last session's/
  this session's earlier "dead store?" question about that save - it was
  never dead, `NEWLIF` reads it. NOTES/SIGNATURES corrected above.
- `NEWLIF`'s tail (`LDX #$00` before every `RTS`) never actually changes
  anything a caller observes - not modelled as a return value, matches the
  pre-existing draft signature `void newlif(void)`.
- **`EXINIT`'s signature needed correcting** (same pattern as `sauchk()`
  last step): the pre-existing draft was `void exinit(void)`, but the
  routine's own header ("INPUT PARAMS: (X)=2 MEANS PLAYER EXPLODED,
  (X)=0 MEANS ROBOT EXPLODED") and body (`TXA/BNE EXINIT_20` - does
  nothing at all unless x==0) show x is a real, load-bearing input. Fixed
  to `void exinit(uint8_t x)`; the one call site (`SHRTCK_19`) updated to
  `exinit(x)`. Confirmed x is never actually changed by EXINIT from a
  caller's point of view either way (the early-return x!=0 path never
  touches X; the x==0 path's own `LDX #$00` tail is a no-op since the loop
  that precedes it already leaves X at exactly 0), so no return value is
  needed - `SHRTCK_19` reads `EXPCNT_AT(x)`/etc right after the call and
  gets the correct value either way.
- EXINIT's loop runs X=5..0 (6 iterations, byte-granularity into the ROM
  table `IZVEL`) while Y runs 10..0 step -2 (6 iterations, the SAME 6
  explosion-piece slots EXPLDE's own per-frame loop covers) - independent
  confirmation of last session's EXPLDE loop-bound finding: there really
  are only 6 "real" explosion-piece slots, not 8, and this is where they
  get their initial position (from the ROBOT's own `TPOSX+2`/`TPOSY+2` -
  EXINIT is only ever meaningfully entered on ROBOT deaths, since player
  deaths use the separate windshield-crack sequence instead - see
  `SHRTCK_18`'s own `CRACK`/`BOUNCE` set when Y==0/player is the target).

### MOTION structure (for whoever reads it fresh)

- Register/exposure audit (done BEFORE writing code, per the brief's own
  instruction): grepped the whole disassembly for `JSR M_\*`/`JMP M_\*`.
  `M_SET`/`M_FOR`/`M_FOR1`/`M_REV` are called from OUTSIDE MOTION (ROBOT/
  BUZBOM-area code at asm lines ~3538/3657/3666/3678/4053, not yet
  translated) - these, plus `ITANGL`/`DTANGL`/`SAVEIT`/`BAKOUT` (whose own
  header already says "ON ENTRY X=0 MEANS TANK, X=2 MEANS ROBOT"), are
  public with an `x` parameter. Every other `M_*` label (`M_TOP`, `M_PR`,
  `M_PL`, `M_LTF`, `M_LTR`, `M_RTF`, `M_RTR`, `M_FF`, `M_FR`, `M_F`, `M_R`,
  `M_NEXT`, `M_6S_`, `M_10S_`, `M_STOP`, `M_NEX0`) is reached ONLY from
  MOTION's own internal dispatch chain - `static`, same convention as
  `c21s_`. They all still take an `x` parameter for symmetry even though
  MOTION's own dispatch only ever calls them with x=0 (`M_TOP` is only ever
  reached from `MOTION_10` with X hardwired to 0 right before).
- **MTAB decoded by hand** (push-address/RTS -> `switch`, per
  TRANSLATING.md): the inverted-ALLPOT nibble `a = (ALLPOT ^ $0F) & $0F`
  (range 0-15) maps to `M_STOP` for 0-4 (checked before the table via
  `CMP #$05`/`BCC`) and, from the table itself (`MTAB` $621E, 11 words for
  a=5..15, indexed via the classic pushed-`addr-1`-then-`RTS` trick):
  5->M_FF, 6->M_PR, 7->M_RTF, 8->M_STOP, 9->M_PL, 10->M_FR, 11->M_LTR,
  12->M_STOP, 13->M_LTF, 14->M_RTR, 15->M_STOP. Reconstructed by manually
  computing each `.word` table's byte offsets against `LDA MTAB-9,Y`/
  `LDA MTAB-$A,Y` with Y=2*a - double-check this table if anything in
  lockstep flags MOTION, it was hand-derived, not extracted mechanically.
- **Two `PHP`/`PLP` pairs in `M_NEX0` simplified away, not translated
  literally** (a real judgement call, flagged clearly in the code comment
  too): the ROM's `TXA/PHP/LDX SAVE1/STA SINCX,X/PLP/BMI` sequence exists
  purely to protect the `TXA`'s N flag against the intervening `LDX SAVE1`
  (X is needed both as the outer tank index AND, moments earlier, as
  COSXTR's/SINXTR's own angle parameter - real 6502 register pressure).
  Since this C translation keeps the outer index in its own `x` parameter
  throughout (never reloading it via a flag-setting `LDX`), that `LDX`
  never happens here and the `PHP`/`PLP` has nothing to protect against -
  simplified to testing the freshly-computed value's sign directly.
  Verified by hand-checking every instruction between the `TXA` and the
  `BMI` in both occurrences (L62C9-L62D0 and L62E9-L62F0) - `LDX`/`STA` are
  the only ones there, and `STA` never touches flags. This is DIFFERENT
  from EXPLDE's own PHP/PLP (which protected against a real, unavoidable
  intervening ASL/ADC sequence, and so was kept literal there) - don't
  conflate the two "PHP/PLP" writeups when reviewing.
- `SINCX`/`SINCY`'s low byte is computed into `TEMP1` (scratch) and then
  DISCARDED - only the doubled mid+high bytes actually get stored
  per-tank into `SINCX,X`/`SINCX+1,X` (a deliberate precision-loss
  simplification in the original game, not a translation bug - confirmed
  by re-reading the instruction sequence twice).
- `MOTION`'s own top-level ATRACT/LOGO/CRACK gating and the attract-mode
  auto-drive (`M_FOR`/`M_REV` + a pivot based on `TPOSX+3`'s bits) always
  operates on x=0 (the player tank) - the robot's own movement must be
  driven by ROBOT/BUZBOM calling `m_for`/`m_rev`/`m_set`/`itangl`/`dtangl`
  etc. directly with x=2 (not yet translated, but the public primitives
  above are ready for it).

### REACT structure (for whoever reads it fresh)

- **Signature corrected**: the pre-existing draft was `void react(uint8_t
  x)` (probably a generic guess before the code was read, assuming the
  usual tank/robot X=0/2 convention) - REACT never touches X at all. It
  DOES read the entry accumulator on the `BMI REACT_10` (ATRACT bit7 set)
  path, since `PHA` at entry never gets undone before that branch. Fixed
  to `void react(uint8_t a)`.
- **REACT is entered via `JMP` (tail call), never `JSR`** - only from
  `R_RAND`/`R_ATCK` inside the not-yet-translated ROBOT family (asm lines
  3768/3778, `JMP REACT`). Its header says "REGISTERS: NONE", which
  appears stale/incomplete given the above - **when R_RAND/R_ATCK are
  translated, they must call `react(a)` with whatever `a` they hold right
  at their own tail-jump site (matching the ROM's register flow exactly),
  and this note should be updated/closed out once confirmed.**
- **Proven dead code, translated as an unconditional jump, not a runtime
  branch**: on the ATRACT-bit7-CLEAR path, `LDA #$30/STA ACTION/BNE
  REACT_35` - `STA` never affects flags on a 6502 (checked against every
  6502 flag-effect reference), so the `BNE` tests whatever the `LDA #$30`
  left (Z=0, always, since $30≠0) - the branch is ALWAYS taken. This makes
  the entire HITS-based `SKILL`/`ACTION` block below it (`L63D6`-`L63EB`)
  unreachable on this path. Modelled with a plain `goto REACT_35;` right
  after `ACTION = 0x30;`, per the same reasoning `bz_main`'s residual-carry
  hazards used (a provable, not guessed, control-flow fact) - see the
  in-code comment on `react()` for the one-paragraph version.

### Exact next step

Translate `ROBOT` ($6418) and its whole family - `bzone_program_rom.asm`
lines ~3516-3785 per the "Where things are" table below (re-verify with
`grep -n "^ROBOT:\|^FIREIT:"` first, same caveat as always). This is now
the single biggest remaining chunk. Read the WHOLE thing (including
Atari's own `battlezone-main/BZONE.MAC` ROBOT block and
`../disasm/routine_docs.py`/the listing's `[note]` headers per the brief -
"they were reviewed against the code") before writing anything. The brief
names `ROBOT_nn` locals, `R_37S_`, `R_40S_`, `R_50S_`, `R_EVAD`, `R_RAND`,
`R_ATCK` as things other code enters - audit with grep (`JSR R_\|JMP R_`)
the SAME way MOTION's M_* labels were audited this session, to decide
which need real public signatures (probably `R_RAND`/`R_ATCK` at least,
since they tail-call the now-public `react()`) vs. which are `static`
internal-only. Expect more `PHP`/`PLP`/residual-carry hazards (per the
brief's own warning) - hunt for them the same way, and use the SAME
judgement-call standard as `M_NEX0`'s simplification above: only simplify
away a `PHP`/`PLP` if you can hand-verify (and document) that nothing
between the push and pop actually depends on a register this C
translation handles differently (e.g. a parameter instead of a reloaded
X) - when in doubt, translate literally with an explicit saved-flag local,
like EXPLDE did. Budget real time - do not rush this one either; if cut
off partway, stop at a clean sub-boundary and update this section
precisely with which ROBOT_nn/R_* pieces are done vs. not.

Then continue down the per-routine table in order: FIREIT, BUZBOM, SAUCMV,
then OBJOBJ/ROBCHK/TANKCK/R2D3CK/ROB1 (`TR7CHK` is already done).

### Compile status

**Passes.** `cl /nologo /W4 /std:c11 /D_CRT_SECURE_NO_WARNINGS /I. /c
bzone_game.c` (via VsDevCmd.bat per TRANSLATING.md) is clean - 0 warnings,
0 errors - for everything translated so far (`main_init`, `main_pass`,
`bz_main`, `b_end`, `explde`, `qwikck`, `shrtck`, `colchk`, `sauchk`,
`newlif`, `exinit`, `motion` + its whole M_* cluster, `saveit`, `bakout`,
`itangl`, `dtangl`, `m_set`, `m_for`, `m_for1`, `m_rev`, `react`,
`tr7chk`). All the other agents' real headers now exist (`vgut.h`,
`bzone_math.h`, `bzone_disp.h`, `bzone_nmi.h`, `bzsoun.h`, `coin65.h`) and
are `#include`d for real - the only remaining TODO(seam) entry is
`hw_wait_frame()` (still not in `hw.h`, which this agent cannot edit).
Note `bzone_math.h`'s `SIN`/`COS` are named `sin_`/`cos_` (trailing
underscore) - the maths agent hit exactly the "cl.exe reserves `sin`/`cos`
as compiler intrinsics" problem this session's own `m_set()`/`m_nex0()`
also ran into (confirmed independently: `cl2440`/`C4013` when using the
bare names) and fixed it in their header; this file's calls already use
`sin_`/`cos_` - if a stray bare `sin(`/`cos(` ever creeps back in during a
future edit, that's the fix. `tankck`/`r2d3ck`/`colchk`/`robot`/`saucmv`/
`robchk` are declared in `bzone_game.h` but not yet defined - fine for a
`/c` (compile-only) build; only the eventual link step would need them.
Re-run this exact compile after every future routine group, per
TRANSLATING.md's "work incrementally" instruction - do not let several
routines pile up unverified.

### MAIN structure (for whoever reads bz_main/b_end fresh)

- **Superseded by the "bz_main/b_end split" section above** - the
  `main_init()`/`main_pass()` split described here originally (matching
  Tempest's `alexec.c` `mainln()`/`mainln_pass()`, with a plain `return` at
  every `JMP MAIN` site for an external per-frame driver loop to re-call)
  was the design for session 1. Session 2 found the display module had
  already written code calling `bz_main()`/`b_end()` as direct tail calls
  (not expecting a return-to-driver), so the body moved under those names
  and every `JMP MAIN`/`JMP B_END` site now calls the target function
  directly and recurses, per the ROM's own never-`RTS`s control flow. See
  "bz_main/b_end split" above for the full reasoning and the resulting
  stack-growth caveat flagged for integration. `main_pass()` still exists
  as a one-line wrapper for the originally-documented harness-driver name,
  but no longer describes the real structure - the rest of THIS section
  (residual-carry hazards, etc.) is still accurate, just mentally
  substitute `bz_main`/`b_end` for `main_pass` throughout.
- **Two residual-carry hazards resolved by careful hand-tracing** (see the
  file's own header comment for the short version): MAIN's carry at
  `L5042`/`L506B` is a coincidental reuse of a once-per-256-frames `TIMOUT>=4`
  test (I called this `periodic_ok` in the code) that gates BOTH the
  attract-mode `TANKCK`/`DSPLAY`-toggle block AND the `R2D3CK` call,
  depending on which branch is live that frame; and MAIN_42's `BCC MAIN_45`
  is a real (not accidental) `PTURN>=2` test surviving two N/Z-only
  instructions. Both are explained in depth in the "Hazards found" section
  below - **do not "simplify" these back to a literal carry-flag global**
  without re-reading that reasoning; a global-carry model would also work
  but is more error-prone across the rest of this module (many more such
  cases are likely still ahead in MOTION/ROBOT/BUZBOM, which are far bigger
  and less thoroughly hand-verified than MAIN was this session - **budget
  extra time to hunt for the same pattern (a CMP whose flags survive several
  N/Z-only instructions to a later branch) in every remaining routine**).
- Tail calls that never return to `main_pass` (`hiscre()`, `distbl()`,
  `wnshld()`) are modelled as "call it, then `return`" - matches the ROM's
  `JMP` (not `JSR`) to those routines; **this assumes those routines
  themselves eventually loop back into `main_pass` from the outside** (the
  harness's job), not from inside `main_pass` - flag this to whoever wires
  up the driver loop if it turns out `hiscre`/`distbl`/`wnshld` expect to be
  called *as if* they `RTS` back into MAIN's own body instead.
- The `BUFRDY` wait (`MAIN_5`) is translated as a literal spin
  (`while (BUFRDY==0) {}` via `goto`) - **this is a real infinite-loop risk
  in a single-threaded port** unless the NMI handler (nmi-sound-coin
  agent's `bzone_nmi.c`, not yet written) runs interleaved with `hw_wait_frame()`
  so `BUFRDY` actually becomes nonzero before `main_pass` spins on it.
  Flagged for integration; not fixable from this module alone.

### Helpers / conventions established this session (reuse them going forward)

- **Twin-cell indexing**: use the generated `_AT(i)` macros from
  `state_defs.h` (e.g. `PTBLO2_AT(x)`, `COLFLG_AT(2)`) rather than hand
  computing `g.ram[A_NAME + i]` - confirmed present and correctly generated
  (`COLFLG_AT(2)`, `VGLIST_AT(1)` both used already in `main_pass`).
- **BCD add/sub**: `state.h`'s `adc6502(a, v, &c, &z)` / `sbc6502(a, v, &c, &z)`
  - not yet exercised in this module (no `HITS` sites translated yet; they
    live in `SHRTCK`/`SAUCHK`/`BUZBOM`, all still TODO) but confirmed present
    and matching the Tempest template's contract (`*c` is carry in/out, `*z`
    optional).
- **Mathbox writes that are NOT RAM**: cells whose `state_defs.h` entry has
  only an `A_NAME` (no bare `NAME`/`NAME_AT` lvalue macro) are hardware
  Mathbox registers ($1860-$187F) - write them with
  `hw_mb_write((uint8_t)(A_NAME - 0x1860), value)`, not a `g.ram[]` store.
  Hit this for `DIVCYC` in `main_pass`; **`EXPLDE` will hit it again for
  `XMATRX`/`YMATRX`** (see "Exact next step" above) - same pattern.
- **Seam gaps** (routines/headers this module needs but that don't exist
  yet) go in the `TODO(seam)` block at the top of `bzone_game.c`, one
  `extern`-style prototype per routine, with a comment saying which real
  module header it belongs under. Current list: `hw_wait_frame` (hw.h),
  `volcno/bigwnd/hiscre/distbl/mounts/battle/wnshld/dradar/draw/pntput/
  shupdt/msgs/info/finish` (bzone_disp.h), `rotate/track/absnval/sin_/cos_`
  (bzone_math.h), `sndon/bound` (bzone_nmi.h/bzsoun.h). **Add to this list,
  do not create ad hoc externs elsewhere in the file** - keeps the eventual
  cleanup (deleting the block once real headers land) a single diff.

### Hazards found (full detail, referenced from the summary above)

1. **MAIN's `periodic_ok` gate** (`L5026`-`L511E` region touches this):
   `INC FRAME` / `BNE MAIN_6` skips `INC TIMOUT; LDA TIMOUT; CMP #$04`
   entirely on 255 of every 256 frames. The `CMP #$04`'s carry (when it DOES
   run) survives `BIT GOVER/BPL/BIT ATRACT/BMI/LDA CRACK/BNE` (none of which
   touch C) all the way down to `L5042 BCC MAIN_10` (attract path) or, via
   the `BMI MAIN_3`/`BNE MAIN_3` exits earlier, to `L506B BCC MAIN_15`
   (game-active or crack-active path) - **the same carry value, whichever
   of those two tests is actually reached, because they're mutually
   exclusive paths through the same preceding carry-setting code**. On the
   254/255 frames where `CMP #$04` did NOT run, the carry those two tests
   see is instead whatever `JSR VGSTAT` (at `L5023`, well before `MAIN_6`)
   left in C internally, via its own `VGADD` pointer-increment add - traced
   by hand: `VGLIST`'s low byte is only `$0A` before that add (2 SCAL + 2
   CNTR + 4 VCTR bytes into a list that started at offset `$02`), so
   `+2` never crosses a page, C is always clear. Net effect: the
   `TANKCK`/`DSPLAY`-toggle block and the `R2D3CK` call each only actually
   run once every 256 frames, and only if `TIMOUT` (which is reset to 3
   every time either block fires) has climbed back to >=4 by then - i.e.
   almost always true in practice (4 frame-wraps = 1024 frames apart would
   be needed to NOT re-fire, but it's reset to 3 each firing, so it re-fires
   every single 256-frame wrap, forever, once primed). **This produces the
   documented "alternates roughly every several seconds" attract behaviour**
   (`MAINLINE_NOTES.md` item 4) - consistent, gives confidence the reading
   is right, but this is exactly the kind of thing lockstep needs to
   double-check against a real trace once the harness exists.
2. **MAIN_42's `pturn_ge2`**: `LDA #$81... ` no wait, this is
   `L50E7 CMP #$02` (against `PTURN`, loaded a few instructions earlier)
   surviving `LDA FIRECT`/`AND S_INTCT` (both N/Z-only) to `L50F3 BCC
   MAIN_45`. Verified by checking every instruction between the `CMP` and
   the `BCC` against the 6502's flag-effect table; only `LDA`/`AND`/`BEQ`
   appear, none of which touch C. Confident in this one (shorter chain,
   easier to verify than the 256-frame one above).
3. **Tail calls / dead-end `JMP`s**: `MAIN_6`'s `JMP HISCRE`, `MAIN_10`'s
   `JMP DISTBL`, `MAIN_30`'s `JMP WNSHLD` all end `main_pass` early (mapped
   to `return;` after calling the display-owned routine) - see "MAIN
   structure" above for the caveat about who's expected to loop back to
   `MAIN`.
4. **`DIVCYC` is a Mathbox register, not RAM** - see "Helpers" above.
   `EXPLDE`'s `XMATRX`/`YMATRX` writes will need the same treatment; watch
   for more of these ($1860-$187F range) in `MOTION`/`ROBOT`/`FIREIT`
   (anything that starts a Mathbox rotate/divide - `ACOS`/`BSIN`/`TPOSXT`/
   `TPOSYT`/`XMATRX`/`YMATRX`/`YSTRT1`/`YSTRT2`/`ZLOW`/`ZHI`/`DIVOUT`/`DST2`
   per `bzone_defines.asm`'s Mathbox register block, lines ~68-90 of that
   file).

## Where things are (so re-finding this doesn't cost another full pass)

- Routine line ranges in `../disasm/bzone_program_rom.asm` (from this
  session's own `grep -n` of the label list; re-run if the file changes):
  MAIN 39-313, EXPLDE 1925-2057, QWIKCK/SHRTCK 2587-2759, COLCHK 2759-2846,
  SAUCHK 2846-2926 (ABSTMP starts at 2926, not mine), NEWLIF/EXINIT
  2947-3032, MOTION 3032-3447, REACT 3447-3516, ROBOT family 3516-3785,
  FIREIT 3785-3896, BUZBOM 3896-4133, SAUCMV 4133-4249 (TRACK starts at
  4249, not mine), OBJOBJ 4405-4538, TR7CHK 4538-4558, ROBCHK 4558-4589,
  TANKCK 4589-4637, R2D3CK 4637-4654, ROB1 4654-4763 (DRADAR starts at 4763,
  not mine).
- `../disasm/MAINLINE_NOTES.md` and `../disasm/CALLING_NOTES.md` both read
  in full this session - do not re-read section 0-4/7 of `CALLING_NOTES.md`
  or the whole of `MAINLINE_NOTES.md` again unless checking a specific
  claim; their content is already reflected in this file's decisions.
- `../disasm/bzone_defines.asm` and `state_defs.h` - not read in full
  (both large); look up specific cell names with `grep` as each routine
  needs them, same as this session did for MAIN's cells.
- Style/template references already consulted:
  `C:\Source2026\Tempest Dissasembly\c_src\alexec.c` (the `mainln`/
  `mainln_pass` split, `xy6502`-style register threading via a local
  `regs`-shaped return where needed - this module uses the project's own
  `regs` from `regs.h`, not Tempest's `xy6502`, since that's what
  `SIGNATURES.md` specifies here) and this project's own `vgut.c` (fall-
  through-chain style, per-statement `Lxxxx` comments, `A_NAME`/`_AT`
  macro usage) - both good enough as style references, no need to re-read
  unless a new pattern (not yet seen in either) comes up.

## Signatures registered

See `SIGNATURES.md`, "BZONE game logic" section: `main_init`, `main_pass`
registered so far. Every other routine's planned signature is already
declared in `bzone_game.h` (derived from each routine's Atari header/call
sites per SIGNATURES.md's rules) but not yet copied into `SIGNATURES.md`
itself - do that as each routine is actually implemented and verified
against its real call sites, not before (a header guess is not yet a
contract other agents should rely on).

## Cycle counting (2026-09-19)

All routines except MAIN/B_END charge the ROM's cycles with CYC(): EXPLDE, QWIKCK, SHRTCK, C_21S_,
SAUCHK, COLCHK, NEWLIF, EXINIT, SAVEIT, BAKOUT, ITANGL, DTANGL, M_SET, M_FOR1, M_FOR, M_REV,
M_NEX0 and the MTAB legs, M_NEXT, M_TOP, MOTION, REACT, TR7CHK. Lockstep: all exact where reached.
- SAUCHK's lockstep row never matches by construction: it ends with JMP C_60S_ (COLCHK's loop
  bottom), so its check closes at COLCHK's RTS and the ROM count includes the rest of COLCHK.
  COLCHK itself is exact, which covers SAUCHK/C_21S_.
- M_NEX0's two PHP/PLP pairs (simplified away in the C) are charged where they sit.
- The MTAB push/RTS dispatch in M_TOP is 24 cycles.
