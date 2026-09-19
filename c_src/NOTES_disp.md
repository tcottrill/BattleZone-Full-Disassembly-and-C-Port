HANDOVER: compiles /W4 clean, ready for lockstep

NOTES_disp.md - agent "display" (bzone_disp.c/.h), Battlezone C port

After HANDOVER: bzone_disp.c is not edited further by this agent. Findings
in `NOTES_integration.md` about this module get answered in Section 7
below (added at the bottom, checked periodically).

## RESUME (read this first, keep current after every routine)

Written 2026-09-18, continuing a session that died mid-`hiscre()`, /W4 warning
cleanup.  Found on disk: `bzone_disp.c` (~440 lines) with BIGWND/WNSHLD/
BATTLE(+battle_10)/BATINT/CKSCOR/HISCRE fully translated and reading clean;
everything after that was a `#if 0` block of empty stub bodies (signatures
only, no translation).  No notes file existed - this file is new.

| routine | ROM addr | status | notes |
|---|---|---|---|
| BIGWND | $51D9 | done | verified against listing on this resume |
| WNSHLD (+WNSHLD_10/13/15/17/18/20) | $51E9 | done | falls into BATTLE at WNSHLD_20 if CRACK wraps to 0 - literal, commented |
| BATTLE (+BATTLE_5/10/13/15/20/22/90/95) | $5268 | done | TRAP1 checksum translated literally |
| BATINT | $531F | done | |
| CKSCOR (+CKSCOR_10/20/25/30) | $5344 | done | |
| HISCRE (+HISCRE_10/20/30/60/61/65) | $53C5 | done | |
| LETCHK | $5465 | done | signature reverted to `void letchk(void)` - see Section 2 |
| DISTBL (+DISTBL_10/15/20/25/30) | $549B | done | |
| DSPALP | $556A | done | |
| FINISH | $5574 | done | |
| MOUNTS (+MOUNTS_10..90) | $57C8 | done | plus file-static helper `mounts_50` (not a real ROM label) |
| VOLCNO (+VOLCNO_10..40) | $58BA | done | one open carry-in question, see Section 2 |
| DRAW/DTAB dispatch (D_TOP/D_TOP_11) | $5C5C | done | one `switch`, all D_* cases inline |
| D_DOT | $5C92 | done | switch case 0 |
| D_SBRT | $5C9F | done | switch case 1 |
| D_LABS (falls into D_BVCT) | $5CD4 | done | switch case 3, falls to case 2 |
| D_BVCT/D_VCTR (shared D_VCT1 tail) | $5CE1/$5CE5 | done | switch cases 2/4, both call `dovect()` |
| D_SPCL (falls into D_SLIM) | $5CED | done | switch case 5, falls to case 6 |
| D_SLIM | $5D0E | done | switch case 6 (empty) |
| DOVECT | $5D11 | done | standalone function |
| D_NEXT | $5D41 | done | standalone function, called at the bottom of draw()'s loop |
| DRADAR (+DRADAR_10..40) | $6AE9 | done | one asymmetric-sign-extend ROM quirk noted, see Section 2 |
| SHUPDT (+SHUPDT_10/15/20) | $6C66 | done | processes shells in order 2,0 (not a skip) |
| MSGS/LNGTBL dispatch | $6C98 | done | flattened to one `switch`, closed-form language selector (see Section 2) |
| ENGLISH/GERMAN/FRENCH/SPANSH | $6CCC/$6CC1/$6CB6/$6CAB | done | inlined as the 4 switch cases in `msgs()` |
| MSTART (+MSTART_10/20/30) | $6CD4 | done | file-static `mstart(x, hi)` helper |
| INFO (+INFO_10..70, score/lives/msg tail) | $6D2E | done | life-tank loop stores `(VGLIST),Y` directly |
| DIGT2S | $7B9E | done | falls into DIGTYS |
| DIGTYS | $7BA0 | done | falls into DIGITS |
| DIGITS (+DIGITS_10/20/30) | $7BA1 | done | |

All routines in the display assignment are now translated. Compiles /W4
clean as of this point (see Section 5). Remaining before HANDOVER: the
second independent instruction-by-instruction review pass.

Working incrementally through the list above in ROM order.  This table is
updated after every routine - if the session cuts off, check here first.

## Section 1: judgement calls carried over from before this session

(BIGWND/WNSHLD/BATTLE/BATINT/CKSCOR/HISCRE - see file header comment in
bzone_disp.c for the VGVCTR-family "Y always leaves at 3" invariant BIGWND
relies on; that comment predates this session and was left as-is.)

- WNSHLD_20 (`L5262`-`L5266`) falls straight into BATTLE if `CRACK` (after
  `INC CRACK / INC CRACK`) wraps to exactly 0 - a genuine fall-through
  hazard between adjacent ROM labels, translated as a literal call to
  `battle()` at the end of `wnshld()`, commented.
- BATTLE's TRAP1 checksum reads vector RAM through `(TBLPTR),Y` for
  Y=$35..0 then `SBC OBJTBL+$21` - translated with `cpu_rd`/`sbc6502`
  literally per the brief, no shortcuts.

## Section 2: judgement calls this session

### LETCHK ($5465)
- Entry A is read only to feed `STA POTGO` (a strobe write POKEY discards
  the data byte of), then immediately clobbered by `LDA ALLPOT` before it's
  used for anything else - the value could never affect any later store or
  branch in this routine. HISCRE's call site leaves A as whatever its
  string of `MSGS` calls happened to exit with - not a value the ROM's own
  author is relying on. Reverted the header's originally-declared
  `void letchk(void)` back to itself (no parameter) after briefly trying
  `uint8_t a` and hitting exactly this "nothing downstream reads it"
  situation - same shape as CALLING_NOTES.md's SNDON stack-peek precedent
  ("do not port it literally... reproduces the behavior every real call
  site actually exercises"). Writes a fixed placeholder to the POTGO seam
  call, documented in the function body.
- LETCHK_40's `BNE LETCHK_3` is unconditional in practice (STINTL letter
  codes are always in `$16..$4B`, never 0) but falls straight into DISTBL
  (the next ROM label) if it ever were - modelled literally with a real
  `if`, tail-calling `distbl()` on the untaken path.

### DISTBL ($549B)
- The tank-count loop (`DISTBL`'s "blank before tank" + per-tank `TSYMBL`
  pairs) stores directly through `(VGLIST),Y` per the module brief -
  reproduced with `cpu_wr(VGLIST_ADDR + y, ...)`, not routed through any
  VGUT call the ROM didn't use there. `VGMSGA`/`TSYMBL` are vector-ROM
  addresses ($33F0/$3590) read via `cpu_rd` (covers ROM + RAM + vector RAM
  per state.h), never hand-transcribed.
- Decimal-mode bonus-coin add (`L554D`-`L5550`, CALLING_NOTES.md section 0
  hazard) bracketed with a save/restore of `g.dflag` around the one
  `adc6502` call, matching the ROM's `SED`/`CLD` exactly (including the
  `CLC` that precedes `SED`, reproduced as `c = 0` before the flag flip).
- `DISTBL_25`'s `CPX #[HSCNUM-1]*3` translated as `x >= (K_HSCNUM-1)*3`
  (unsigned, matching `BCS`).

### DSPALP ($556A)
- Tail-calls `vgadd2` (`JMP VGADD2`, not `JSR`/`RTS`) exactly as
  CALLING_NOTES.md section 2 describes - DSPALP's own "return" is VGADD2's
  RTS; translated as a direct `vgadd2(...)` call at the end, nothing after
  it.

### FINISH ($5574)
- The TRAP1-to-VECRAM copy writes vector RAM, not flat RAM - state_defs.h
  only generates `A_VECRAM` (no `VECRAM` lvalue macro, since $2000 is
  vector RAM); used `VRAM(A_VECRAM)` from state.h instead. Worth flagging
  since it's an easy mistake (`VECRAM` "looks like" a normal named cell but
  isn't one you can assign directly).

### MOUNTS ($57C8) / MOUNTS_50 (file-static helper, $58A7-$58B9)
- `MOUNTS_50` is called three times in a chain (`L580C/L580F/L5812`) where
  each call's input `A` is the *previous* call's `TEMP1` (a table-index
  byproduct, not the original angle) - modelled as a `static uint8_t
  mounts_50(uint8_t a)` returning the value the ROM leaves in `A` at its
  `RTS` (`LDA TEMP1`), fed straight into the next call, reproducing the
  chaining exactly.
- The two 4x `LSR A / ROR XCOMP`-style shift chains (`$57D4-$57DE` and
  `$5824-$582E`) are hand-unrolled as a 4-iteration loop shifting a bit from
  `A` into `XCOMP`'s top bit each time - same idiom in both places, kept as
  a small local loop rather than a shared helper since the surrounding
  code differs (one masks/ORs the result after, one stores it plain).
- `vgjsrl`'s low byte needs an explicit `& 0xFF` before the `(uint8_t)`
  cast (`A_HORIZN` is `0x3000`) - a bare cast triggered `/W4` `C4310`
  ("cast truncates constant value"); masking first makes the intended
  truncation explicit and silences it cleanly.

### VOLCNO ($58BA)
- **RESOLVED by lockstep (NOTES_integration.md "Fixes made" #5, #11)** -
  the "assumed CLC" below was wrong. The carry is a real, deterministic
  chain: C=1 on entry (MAIN's `LSR SYNC / BCC MAIN` falls through only
  with C set), one `c` persists across all 5 rocks, the second spawn ADC
  chains off the first, and `L5917 CMP #$A2` sets C on both of its exits
  (C=1 into `ADC YPOSH` on the branch, C=0 when the rock lands). All of
  that is now in `volcno()`; VOLCNO passes 100% in both scenarios. The
  original judgement call is kept below for history.
- **(Historical) Open question - uncontrolled carry-in**: the two `ADC #$01`/`ADC #$05`
  adds that randomize a newly-spawned rock's X speed/Y speed (`L58D2`,
  `L58E3`) have no `CLC`/`SEC` anywhere upstream in VOLCNO itself - their
  carry-in is whatever the 6502 flags happened to be when `JMP`/`JSR
  VOLCNO` was reached from `MAIN`, which in turn depends on every other
  rock's arithmetic earlier in the *same* call (the loop runs 5 times) and,
  for the very first rock, on whatever MAIN's own code did last before
  calling VOLCNO. Reproducing this exactly would require carrying 6502
  condition flags across calls into a currently flag-free C architecture -
  out of scope for this pass. **Assumed carry-in = 0 (as if `CLC`)** at
  both sites, documented in the code; this can only ever perturb a newly
  spawned rock's initial speed by 1, and only when the game's real carry
  state would have differed - worth an empirical check once lockstep is
  running (compare `XSPD`/`YSPD` right after a rock spawns across many
  frames; a divergence isolated to that one byte, only on spawn frames,
  would confirm this guess needs revisiting).
- Otherwise a straightforward per-rock state machine; translated with all
  loop-body locals declared once at the top of each iteration (not inside
  the nested blocks the ROM's branches would naturally suggest) specifically
  so the several backward/forward `goto`s between `VOLCNO_12`/`VOLCNO_30`/
  `VOLCNO_33` never jump over a C variable initializer.

### DRAW + DTAB + D_DOT/D_SBRT/D_LABS/D_BVCT/D_VCTR/D_SPCL/D_SLIM/DOVECT/D_NEXT ($5C5C-$5D41)
- DTAB's push-address-then-RTS dispatch on the opcode's low 3 bits ->
  `switch ((uint8_t)(op & 0x07))`, one `case` per DTAB slot (0-6); opcode 7
  has a `default:` no-op since DTAB only has 7 entries and no real OBJTBL
  edge-list byte in this ROM ever encodes it (out-of-table read on real
  hardware - not modelled, see the `default` comment).
- Fall-through chains reproduced as literal C `switch` fall-through, not
  separate calls: `case 3` (D_LABS) has no `break`, so it runs into `case 2`
  (D_BVCT)'s body; `case 5` (D_SPCL) has no `break`, runs into `case 6`
  (D_SLIM, empty). Matches CALLING_NOTES.md section 4 exactly.
- D_SBRT_10's brightness clamp: re-derived the branch directions carefully
  after an initial reversed draft - the ROM clamps brightness UP to a
  *minimum* of `$30` (`t = TEMP1-DQUE`; keep `t` only if the subtract didn't
  borrow AND `t>=0x30`, otherwise use `0x30`), not a maximum. Written as
  `bright = (cc && t >= 0x30) ? t : 0x30;` - double-checked against the
  branch targets (`BCC D_SBRT_12` on borrow, `CMP #$30`/`BCS D_SBRT_15` to
  keep `t`, else fall into `D_SBRT_12`'s `LDA #$30`) before settling on this.
- D_SPCL's scale-byte bit-twiddle (`TAX/AND #$3F/ASL/TAY` for the linear
  factor, `TXA/AND #$C0/ASL/ROL/ROL/ADC #$01` for the power-of-2 factor)
  worked out by hand-tracing carry through each shift (documented inline at
  the call site) rather than emulating each ASL/ROL individually: linear =
  `(sb&0x3F)<<1`; power-of-2 = `((sb>>6)&0x03)+1` (verified against all 4
  combinations of the top two bits by hand).
- `TEMP1 = op & 0xF8` (the point-table index/value D_TOP_11 saves before
  dispatch) is read back by both DOVECT (as `TEMP1>>1`, a `PNTTBL` byte
  offset) and D_SBRT (as a raw depth value) - same cell, different meaning
  per opcode, exactly as the ROM overloads it; no special-casing needed
  since both readers just read the RAM cell.
- DOVECT/D_NEXT are real standalone functions (not folded into draw()'s
  loop body) per bzone_disp.h, even though D_NEXT is only ever reached via
  `JMP` in the ROM (never `JSR`) - draw()'s own `for(;;)` loop plays the
  part of "returning to D_TOP" after `d_next()` returns.

### DRADAR ($6AE9)
- **ROM quirk - asymmetric sign-extend**: the COS/2 computation (`$6B12`-
  `$6B21`) sets `XCOMP+3 = 0xFF` when its shifted result is negative but has
  no corresponding "else clear to 0" - unlike the SIN/2 computation right
  above it (`$6AFF`-`$6B10`) which does the same asymmetric thing but for
  `XCOMP+1`/`CURNTX+1`, consistently, every call. Translated literally
  (only the negative branch writes `XCOMP+3`); flagged rather than "fixed"
  since it's exactly the kind of thing the brief asks to preserve. Whether
  it's observable depends on what a previous frame's `DRADAR` call (or
  earlier code sharing `XCOMP+3`) left behind - worth an empirical check
  once lockstep runs (compare `CURNTY+1` frame to frame around this call).
- The SIN/2 and COS/2 "CMP #$80 / ROR" idiom (arithmetic/sign-preserving
  1-bit right shift of the high byte) is reproduced as
  `(uint8_t)((hi >> 1) | (hi & 0x80))` rather than relying on C's
  implementation-defined behaviour for `(int8_t)hi >> 1` - hand-verified
  against all four bit-7/bit-0 combinations before use (see the derivation
  in this session's working notes; not re-transcribed here since the
  formula is stated at each call site as a comment).
- `MOUTLO`/`MOUTHI`/`ACOS`/`BSIN`/`TPOSXT`/`TPOSYT`/`XMATRX`/`YMATRX`/
  `YSTRT2`/`DXP`/`DYPH`/`DST2` are all Mathbox register-block addresses
  ($1810-$187F) - despite state_defs.h naming several of them like ordinary
  RAM cells (`DXP`'s own comment even says "result"), they are hardware and
  go through `hw_mb_ylow()`/`hw_mb_yhigh()`/`hw_mb_write(offset,...)`
  (`hw.h`), never `cpu_rd`/`cpu_wr` or a RAM-cell macro - `DXP`/`DYPH`/
  `DST2` are genuinely *written* here despite their "result" framing in
  state_defs.h's comments, which was surprising enough to double-check
  against the address ranges in hw.h's own module banner before trusting.
- `sin`/`cos` are called as `sin_`/`cos_` - bzone_math.h (maths agent) had
  to rename them mid-session (`cl.exe` reserves the bare identifiers
  `sin`/`cos` as compiler intrinsics; even a non-conflicting declaration is
  a hard error under `/O2`). Caught by re-reading `bzone_math.h` before use
  rather than trusting the copy read at session start - worth remembering
  that a live multi-agent session means shared headers can change under
  you between reads.
- The dummy `CMP ($00,X)` "WASTE TIME" read at `$6B79` (CALLING_NOTES.md
  section 3) is not modelled, per the brief - no comment-only stub emitted
  besides the code comment noting where it was. Confirmed correct by
  lockstep: its pointer (zero page at whatever X TRACK left) sometimes
  lands on IN0/Mathbox/POKEY, and the harness used to log that read,
  which caused the ~1-2% "C read $1810, ROM read $0800" DRADAR failures.
  Fixed in `tests\lockstep.c`, not here (NOTES_integration.md "Fixes
  made" #12). DRADAR passes 100% in both scenarios.
- The asymmetric `XCOMP+3` sign-extend (first bullet) is translated
  literally and lockstep agrees with it; no divergence was ever traced to
  it.

### SHUPDT ($6C66)
- `LDX #$02` / the `DEX,DEX / BEQ SHUPDT_10` loop actually visits shell
  index 2 *then* shell index 0 (X: 2 -> 0 [loops back] -> -2/0xFE [exits]) -
  not a skip of shell 0 as the earlier placeholder note in this file
  speculated before translation. Corrected once traced through by hand;
  see the RESUME table.

### MSGS + LNGTBL + ENGLISH/GERMAN/FRENCH/SPANSH + MSTART ($6C98-$6D2B)
- MSGS's language selector (`LDA OPTION / ROL A x4 / AND #$06 / TAY`) reads
  OPTION with an uncontrolled carry-in (no `CLC`/`SEC` anywhere upstream) -
  the same shape as VOLCNO's open question, but this time it was possible
  to *prove* the carry-in cannot matter: hand-tracing all 4 `ROL`s shows
  the incoming carry only ever lands in the result's bit 3, and `AND #$06`
  keeps only bits 1-2 - so the masked result depends solely on OPTION's own
  bits 6-7, closed form `y = (OPTION & 0xC0) >> 5` (giving 0/2/4/6, one per
  language). Used the closed form directly (not a hand-rolled 4x ROL loop)
  since it is *exactly* equivalent, not an approximation - no assumption
  needed, unlike VOLCNO where the carry-in genuinely could change the
  result and had to be guessed.
- Per CALLING_NOTES.md section 2's own suggestion, flattened the
  push-address-then-RTS `LNGTBL` dispatch and the four one-line language
  legs (ENGLISH/GERMAN/FRENCH/SPANSH - each just sets `TBLPTR`/`TBLPTR+1`
  from its own message-table base then falls into MSTART) into one
  `switch` inside `msgs()`, each case calling the shared file-static
  `mstart(x, hi)` helper. MSGS never itself returns to its caller on real
  hardware (MSTART's RTS is the real return) - `msgs()` returning right
  after `mstart()` reproduces the same observable effect, per
  CALLING_NOTES.md's own note that this flattening needs no trampoline.
- MSTART's `STY TEMP3` / `LDY TEMP3` (saving/restoring the character-string
  Y position around the VGLIST-offset bookkeeping, which reuses Y for a
  different purpose in the ROM) is reproduced as a real `TEMP3 = y;` RAM
  store for lockstep's RAM comparison, even though the C translation
  itself doesn't need the "restore" half - a plain C local (`y`) already
  survives untouched across that stretch (it's the ROM's single shared Y
  register, not the C code, that needed saving). Translating the *store*
  but not the pointless *reload* is intentional, not an oversight - see
  the inline comment at the `LDY TEMP3` site.
- MSTART's character loop stores directly through `(VGLIST),Y` (per the
  module brief) via `cpu_wr(VGLIST_ADDR + offset, ...)`, exactly like
  DISTBL's tank-count loop and INFO's life-tank loop below.

### INFO ($6D2E)
- The life-tank loop stores directly through `(VGLIST),Y` per the brief -
  same pattern as DISTBL/MSTART, reproduced via `cpu_wr`, never routed
  through a VGUT call the ROM didn't use there.
- `TSYMBL`/`TSYMBL+1` are read once before the loop (they're vector-ROM
  constants, `A_TSYMBL`=$3590, never written) rather than re-read every
  iteration - a harmless strength-reduction versus the ROM's own `LDX
  TSYMBL+1` (once, outside the loop already) / `LDA TSYMBL` (inside the
  loop, but always the same ROM byte) - included here only because the two
  reads are structured slightly differently from the ROM's own code, not
  because the values could ever differ.

### DIGT2S / DIGTYS / DIGITS ($7B9E-$7BCA)
- Straightforward fall-through chain (DIGT2S sets Y=2 then falls into
  DIGTYS; DIGTYS sets carry then falls into DIGITS), each translated as an
  explicit tail call per the VGUT convention used throughout this module.
- DIGITS' two `PHP`/`PLP` brackets exist purely to protect the caller's
  zero-suppression carry from the `LSR x4` sequence in between (LSR is the
  only carry-affecting instruction in this routine's body) - modelled as
  the C `c` parameter simply not being touched by the unrelated `>>4` and
  `adc6502` computations, which use their own local `cc`/no-carry
  arithmetic. No explicit "save/restore" needed in C since nothing in the
  surrounding statements can accidentally clobber a separate C variable.
- `LDA $00,X` (indexed zero-page absolute - X holds a *computed* zero-page
  address, the caller's `a` parameter plus an offset) read via
  `cpu_rd((uint16_t)x)`, consistent with how this module always reads
  computed/indirect addresses.

## Section 6: second independent review (instruction-by-instruction, per TRANSLATING.md)

Read the whole of `bzone_disp.c` back against `../disasm/bzone_program_rom.asm`
end to end after every routine was translated and the file compiled `/W4`
clean, checking specifically: every RAM and vector-RAM store in the order
the ROM makes it, every flag test's direction (`BCC`/`BCS`/`BPL`/`BMI`/
`BEQ`/`BNE`) against what the C `if` actually tests, every indexed/indirect
address, and every ROM table address used.

- BIGWND/WNSHLD/BATTLE/BATINT/CKSCOR/HISCRE (carried over from the previous
  session): re-read in full: internally consistent, branch directions and
  RAM stores match the listing at every line checked. No changes made.
- LETCHK/DISTBL/DSPALP/FINISH: re-verified DISTBL's tank-count loop offsets
  (`(VGLIST),Y` writes land at `y=0,1` then `2,3,4,5...`, matching the two
  `INY`s per pair) and the decimal-mode bracket's exact `CLC`/`SED`/`ADC`/
  `CLD` ordering. No changes.
- MOUNTS/VOLCNO: re-verified the two 4x shift chains bit-by-bit and the
  `mounts_50` chaining (`A` in = previous call's `TEMP1` out). Re-confirmed
  VOLCNO's branch directions for the "landed" check (`BPL VOLCNO_33` /
  `CMP #$A2`/`BCS VOLCNO_33`) match `goto`s exactly. No changes; the
  carry-in judgement call stands, flagged for lockstep.
- DRAW/DTAB family: re-walked the `switch` against `DTAB`'s `.word` list
  entry-by-entry (D_DOT,D_SBRT,D_BVCT,D_LABS,D_VCTR,D_SPCL,D_SLIM = indices
  0-6, matching cases 0-6 exactly) and re-derived D_SBRT_10's clamp
  direction from scratch a second time, independently, to confirm the
  Section 2 write-up (`bright = t` only when no-borrow AND `t>=0x30`) - same
  answer both times. No changes.
- DRADAR: re-verified every `hw_mb_write` offset against `A_*-0x1860` by
  hand (ACOS=0,BSIN=2,TPOSXT=4,TPOSYT=6,XMATRX=8,YMATRX=0xA,YSTRT2=0x11,
  DXP=0x17,DYPH=0x19,DST2=0x1E) and the `sin_`/`cos_` half-formula against
  all 4 sign/magnitude combinations again. No changes; the asymmetric
  sign-extend stands, flagged for lockstep.
- SHUPDT/MSGS/MSTART/INFO/DIGT2S/DIGTYS/DIGITS: re-verified MSGS's
  carry-independence proof by re-deriving it a second time from the raw
  `ROL` semantics (not from the first derivation's notes) - same closed
  form. Re-checked INFO's and MSTART's `(VGLIST),Y` offsets step by step.
  No changes.

No further bugs found on this pass; the file compiles `/W4` clean
(`cl /nologo /W4 /std:c11 /D_CRT_SECURE_NO_WARNINGS /I. /c bzone_disp.c`,
0 warnings/errors, `bzone_disp.obj` produced). Open items for integration
to watch (not bugs to fix here - translated faithfully per the brief):
VOLCNO's two assumed-CLC carry-ins, DRADAR's asymmetric `XCOMP+3`
sign-extend, and the missing `b_end`/`bz_main` in `bzone_game.h`.

## Section 5: /W4 compile checkpoints

- After BIGWND..HISCRE (previous session) + LETCHK/DISTBL/DSPALP/FINISH
  (this session): `cl /nologo /W4 /std:c11 /D_CRT_SECURE_NO_WARNINGS /I.
  /c bzone_disp.c` - clean, 0 warnings/errors (`bzone_disp.obj` produced).
  Two issues caught and fixed by this checkpoint: `OPTION` is a hardware
  DSW read (`hw_option()`), not a RAM cell - state_defs.h has no such
  symbol, it would have been a link-time/other-module surprise if left as
  a bare identifier that happened to compile; and a `g.dflag` save/restore
  through an `int` triggered `C4244` under `/W4` (narrowed to `uint8_t`).
- After MOUNTS/VOLCNO (+ `mounts_50` helper) added: clean, 0 warnings/
  errors. One `C4310` ("cast truncates constant value") caught and fixed
  at the `vgjsrl` HORIZN call site - see Section 2.
- After DRAW/DTAB/D_*/DOVECT/D_NEXT added: one self-inflicted build break
  (accidentally consumed the `#if 0` opening line while replacing the stub
  block, leaving a stray `#endif` - `C1020 unexpected #endif`) caught and
  fixed by re-opening `#if 0` before the still-stubbed routines. Otherwise
  clean, 0 warnings.
- After DRADAR/SHUPDT added: initial compile failed - `bzone_math.h` had
  been renamed `sin`/`cos` to `sin_`/`cos_` by the maths agent since this
  session started (a real cl.exe intrinsic-name conflict, its own
  NOTES_integration.md fix). Re-read the live header, fixed all 4 call
  sites, clean rebuild, 0 warnings.
- After MSGS/MSTART/INFO added: clean, 0 warnings/errors on the first try.
- After DIGT2S/DIGTYS/DIGITS added (the last routines in this module's
  assignment): clean, 0 warnings/errors on the first try. Every routine in
  the RESUME table is now translated.

## Section 3: cross-module calls made by this module (for integration to check)

- `vgcntr/vgvtr/vgvtr1/vgvtr2/vgstat/vgadd2/vgadd/vgscal/vgsca1/vgdot/vgjsrl/
  vghalt/vghex/vghexz/vgvctr` - VGUT (vgut.h), all call sites in ROM order.
- `sndon(a, x)` (bzsoun.h) - CKSCOR (K_SUPBON), DRADAR (K_RBEEP, K_WARNG) -
  all with `x=0` (unread since `a!=0` at every real call site).
- `sin_`/`cos_`/`track`/`wait` (bzone_math.h) - all four in DRADAR only;
  named with trailing underscores per bzone_math.h's own intrinsic-name
  fix (see DRADAR's Section 2 entry).
- `robchk`/`tankck`/`objobj` (bzone_game.h) - WNSHLD; `tankck`/`r2d3ck`
  (bzone_game.h) - DRADAR.
- `b_end`/`bz_main` (proposed by display, see SIGNATURES.md) - WNSHLD,
  CKSCOR, HISCRE tail calls.  **Integration: these two names do not exist in
  bzone_game.h yet** - main_pass() needs to expose B_END's body and MAIN's
  top under these names (or equivalent) for the tail calls to link.
- `hw_mb_write`/`hw_mb_ylow`/`hw_mb_yhigh` (hw.h) - DRADAR, for every
  Mathbox register access (ACOS/BSIN/TPOSXT/TPOSYT/XMATRX/YMATRX/YSTRT2/
  DXP/DYPH/DST2/MOUTLO/MOUTHI) - none of these go through `cpu_rd`/`cpu_wr`
  or a RAM macro despite state_defs.h naming several of them like ordinary
  cells; see DRADAR's Section 2 entry.
- Within this module: `distbl()` calls `info()`; `msgs()` calls its own
  file-static `mstart()`; `letchk()` tail-calls `distbl()` on its (dead)
  fall-through path; `hiscre()`/`distbl()` both call `finish()`; `draw()`
  calls `dovect()` and `d_next()`.

## Section 4: open questions / ROM oddities noted, not "fixed"

- WNSHLD_20's fall-through into BATTLE on CRACK wraparound (see Section 1) -
  translated literally.
- LETCHK_40's fall-through into DISTBL if `STINTL,X` were ever 0 (dead in
  practice - see Section 2's LETCHK entry).
- VOLCNO's carry-ins - resolved: a deterministic chain, not an
  assumption (Section 2's VOLCNO entry; NOTES_integration.md #5/#11).
- DRADAR's asymmetric sign-extend of `XCOMP+3` (COS/2 branch has no "else
  clear to 0", unlike the SIN/2 branch right above it) - translated
  literally; lockstep passes 100% with it (Section 2's DRADAR entry).
- WNSHLD `L5207 CMP CRACK / BCS` was translated inverted (respawn at
  CRACK >= $12 instead of after $2C) - fixed once lockstep covered WNSHLD
  again (NOTES_integration.md #14).
- MSGS's language-selector carry-in is uncontrolled too, but proven not to
  matter (closed form, no assumption needed) - see Section 2's MSGS entry.

## Cycle counting (2026-09-19)

Every routine in bzone_disp.c charges the ROM's cycles with CYC() (TRANSLATING.md "Cycle
counting"). Lockstep, attract 5000 / coin_start 3000 / play_long 4000 with --cycles-strict: every
registered display routine exact on every untainted call (HISCRE/LETCHK are not reached by the
scenarios - audit only; D_NEXT and the tail-chained WNSHLD/CKSCOR/HISCRE/DISTBL close where the
ROM JMPs away, which the C charges as that JMP).
- DRAW's DTAB push/RTS dispatch (L5C71-L5C83) is 35 cycles; D_SBRT's brightness chain is charged
  per object-type exit (12/16/20/28 to D_SBRT_5, 24/29 to D_SBRT_10).
- BATTLE's TRAP1 checksum loop and MAIN's TRAP2 loop are 54 passes each (Y = $35..0).
- BATTLE_95 (L52E7) is itself the BCC BATTLE_22 - both the JMP BATTLE_95 and the fall-through
  paths end on that one branch.
- Hand-summing slips found by lockstep or by tools\cyc_audit.py and fixed: BATINT 46,
  MOUNTS' volcano block L5832-L5846 30, DRADAR L6B23-L6B34 29 and L6B47-L6B57 26, MSTART's
  character loop 55, SHUPDT's in-flight update 58.
