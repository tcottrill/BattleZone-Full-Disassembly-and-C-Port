HANDOVER: compiles /W4 clean, ready for lockstep

# NOTES_enemy.md - agent "enemy" (bzone_enemy.c/.h contributions)

## RESUME (read this first if picking this up mid-session)

**Status: all seven assigned routines translated, written to
`bzone_enemy.c`, `SIGNATURES.md` updated (new "BZONE enemy AI" section),
`bzone_game.h` updated (ROB1's stale draft prototype removed - see below),
file compiles `cl /nologo /W4 /std:c11 /D_CRT_SECURE_NO_WARNINGS /I. /c
bzone_enemy.c` CLEAN (0 warnings, 0 errors), and a second independent
instruction-by-instruction re-read against `../disasm/bzone_program_rom.asm`
was done after that clean compile (see "Second-pass review" below). Nothing
left to do in this file - per TRANSLATING.md, do not edit `bzone_enemy.c`
again after this HANDOVER line.**

### Per-routine status

| Routine | ROM address | Status | Notes |
|---|---|---|---|
| `buzbom` | $6624-$677B | **done** | see "BUZBOM structure" below |
| `saucmv` | $677C-$680F | **done** | see "SAUCMV structure" below |
| `objobj` | $68D6-$6994 | **done** | see "OBJOBJ structure" below |
| `robchk` | $69BE-$69E7 | **done** | see "ROBCHK/TANKCK/R2D3CK/ROB1 structure" below |
| `tankck` | $69E8-$6A21 | **done** | ditto |
| `r2d3ck` | $6A22-$6A44 | **done** | ditto |
| `rob1` (static) | $6A45-$6AE8 | **done** | ditto - one real PHP/PLP kept literal |

### Compile status

**Passes.** `cl /nologo /W4 /std:c11 /D_CRT_SECURE_NO_WARNINGS /I. /c
bzone_enemy.c` (via VsDevCmd.bat, invoked through `cmd /c "...VsDevCmd.bat
-arch=amd64 -no_logo && cl ..."` in one shell so the environment survives -
a plain PowerShell `&`/two separate calls does NOT work, the batch file's
env vars don't propagate back to the parent shell) is clean: 0 warnings, 0
errors. Fixed two issues during the first compile attempt:
1. `bzone_game.h` still had the pre-existing draft `void rob1(void);`
   prototype, conflicting with this file's `static void rob1(uint8_t a)`
   (C4028/C4211) - removed the prototype from `bzone_game.h` (ROB1 is
   `static` here; see below).
2. An unused label (`OBJOBJ_10:`, C4102) - the loop-back edge is a plain C
   `continue`, so the label was never actually jumped to; removed it (kept
   a comment in its place).

### Second-pass review (instruction-by-instruction, after the clean compile)

Re-read `bzone_enemy.c` next to `../disasm/bzone_program_rom.asm` lines
3883-4763 a second time, address by address, specifically re-checking: (a)
every ROR/ROL/ASL/LSR ordering against the actual listing sequence (easy to
transpose - re-verified BUZBOM_19's SIN/COS x4-scale block, OBJOBJ_20's 3/4
tank-radius block, and all of ROB1's X/Y-component blocks instruction by
instruction a second time), (b) every BPL/BMI/BEQ/BNE/BCC/BCS sense (branch
taken vs. not-taken target), (c) every place carry/flags are claimed to
survive intervening instructions (BUZBOM_40's FRAME-bit3 residual, OBJOBJ's
L6921 always-taken branch, ROB1's genuine PHP/PLP) against the 6502's own
flag-effect table (LDA/STA/TXA/TAX/BPL/BMI/BEQ/BNE/BIT-target-only touch
N/Z, never C; STA never touches any flag). No corrections were needed on
this pass - the first-draft translation (written with the listing open
side-by-side, per TRANSLATING.md) already matched. Cross-checked signatures
once more against `bzone_game.h`'s pre-existing drafts and confirmed all
six public ones (`buzbom`/`saucmv`/`objobj`/`robchk`/`tankck`/`r2d3ck`) were
already right; only ROB1's needed correcting (see below).

### BUZBOM structure

- No inputs/outputs (confirmed against Atari's own header, "REGISTERS:
  A,X,Y" = clobbered only). Calls `track()`/`sin_()`/`cos_()`
  (bzone_math.h), `m_for1()`/`qwikck()`/`exinit()` (bzone_game.h, agent
  "game"'s already-done module), `objobj()` (this file, below).
- **BUZBOM_1** (buzz bomb above the TOP height threshold AND already
  flagged colliding?): a 16-bit `CMP`/`SBC` of the constant `TOP` ($0200,
  `K_TOP` in state_defs.h) against `EXPOSZ+$C`/`+$D` - confirmed (again)
  these two cells are the buzz bomb's own Z depth (R2D3CK's "STARTZ"), not
  an explosion-piece slot, per bzone_game.c's own EXPLDE loop-bound finding
  (NOTES_game.md) - consistent third confirmation (EXPLDE, SHRTCK, now
  BUZBOM all agree).
- **BUZBOM_40's residual carry** (`L66B7 ADC TEMP1` / `L66BC SBC TEMP1`,
  neither preceded by a fresh `CLC`/`SEC`): the carry-in is literally bit 3
  of `FRAME` (the last bit shifted out by the four `LSR`s at L66A9-L66AC) -
  `LDA FRAME` at L66AD (reloading for the `AND #$1F`) does not touch C, and
  `AND` only touches N/Z, so the bit survives untouched to the `BCS
  BUZBOM_18` test at L66B5 and then into the ADC/SBC themselves. Modelled
  by simply not resetting the shared `c` between the LSR chain and the
  ADC/SBC - same style as bzone_game.c's own residual-carry hazards
  (EXPLDE's GRAVTY add, SHRTCK's `ADC #$38`).
- **SIN/COS x4-scale, low byte discarded**: BUZBOM_19's block computes a
  16-bit value by sign-extending SIN/COS's HIGH byte alone into Y1/Y1+1 (or
  X1/X1+1), then shifts that pair left twice, feeding the two shifts'
  *carries* from the ASL of the (separately pushed) LOW byte - but the low
  byte's own final shifted value is never stored anywhere. This is the
  exact same deliberate precision-loss shape bzone_game.c's MOTION
  documents for SINCX/SINCY (NOTES_game.md "MOTION structure") - not a bug,
  translated literally with the discarded `lo` variable explicitly marked
  `(void)lo;` at the end of the COS block (the SIN block's `lo` is
  implicitly discarded the same way, just not marked - both are equally
  intentional, only one needed the cast to silence an unused-value concern
  during drafting).
- **BUZBOM_50/60 unindexed `SNDWRD`/`EXPCNT`**: BUZBOM_60's "blow'm both up"
  path stores to plain (unindexed) `EXPCNT`/`SNDWRD`, the same asymmetry
  bzone_game.c's `C_21S_` already flags against `SAUCHK`/`SHRTCK`'s own
  X-indexed stores (NOTES_game.md "COLCHK/SAUCHK/C_21S_ structure") -
  translated exactly as the listing has it, not "fixed".
- `HITS+2` (unindexed) gets the BCD `+1` add here ("ONLY POINTS FOR ENEMY"
  per the ROM's own comment) - this is the SAME twin-cell scheme
  `shrtck()`/`sauchk()` use via `HITS_AT(x)/HITS_AT(x+1)` with x=2 for
  "the enemy's own counter" (confirmed by SHRTCK_90's `ADC HITS,X`); BUZBOM
  just writes the x=2 half directly since it's unconditionally the enemy's
  own death being tallied here (a mutual-destruction collision, not a
  shell hit), never the player's. No `newlif()` call on this path (the ROM
  genuinely doesn't call it here, unlike SHRTCK_19/SAUCHK_40's x==0 paths) -
  translated as-is.
- `objobj()`'s own `regs` return value's `a` field (0/`$FF`) has to survive
  the `GARBAG`-based branch (`LDX GARBAG`/`BNE BUZBOM_28`) untouched before
  BUZBOM_28's own `ORA #$00`/`BMI` reads it - modelled with a local `orr`
  (of type `regs`) kept alive across that branch rather than re-deriving it,
  since `LDX`/`BNE` genuinely don't touch A on real hardware either.

### SAUCMV structure

- No real register inputs/outputs (Atari's header lists only RAM cells).
- One place worth flagging precisely because it is NOT a "provably always
  taken" branch (unlike several others in this file): the very last `INC
  SAUCER`/`BNE SAUCMV_30` (L680C/L680E) - `SAUCER` really can wrap from
  `$FF` to `$00` (only when it was already `$FF`, a real though rare case),
  in which case the routine falls straight through into the next routine
  in ROM order, TRACK ($6810), instead of looping into `SAUCMV_30` (there
  is no RTS at the end of SAUCMV - corrected 2026-09-19, see "Cycle
  counting" below). Translated as a genuine `if (SAUCER != 0) goto
  SAUCMV_30;`, not simplified to an unconditional jump - double-checked
  this one specifically during the second-pass review since every other
  "LDA-then-BNE-clobbers-the-branch" pattern in this file (OBJOBJ's L6921,
  the two sign-extend BEQs in SAUCMV_30/32) turned out to be always-taken,
  and it would have been easy to over-generalize that pattern here by
  mistake.
- The two `ORA #$00`/`BMI`/`LDA #$00`/`BEQ`/`LDA #$FF` sign-extend
  sequences (SAINCX+1, SAINCY+1) both collapse cleanly to a C ternary - the
  `BEQ` after `LDA #$00` is a provably always-taken forward branch (A is
  always exactly 0 there), same idiom as OBJOBJ's L6921 below.

### OBJOBJ structure

- Signature (`regs objobj(uint8_t x)`, `r.c`=carry, `r.a`=0/`$FF`) is
  unchanged from the pre-existing `bzone_game.h` draft - confirmed correct
  against the actual code and Atari's own header, no correction needed
  (unlike SAUCHK/EXINIT/REACT/FIREIT, which all needed signature fixes in
  earlier sessions per NOTES_game.md/the ROBOT family section of
  SIGNATURES.md).
- **CALLING_NOTES.md section 3 lists OBJOBJ as one of WAIT's 7 explicit
  callers** ("callers (7 explicit - ROTPNT x2, PNTPUT x2, TRACK
  (TRACK_40), OBJOBJ... - every Mathbox read in the game funnels through
  here)"). Grepped OBJOBJ's actual address range ($68D6-$6994) for `JSR
  WAIT`: there is none. OBJOBJ reads `hw_mb_ylow()`/`hw_mb_yhigh()` directly
  after a dummy `CMP ($00,X)` delay read, the exact same idiom
  bzone_game.c's `shrtck()`/`sauchk()`/`c21s_()` already use WITHOUT calling
  `wait()` either (their own headers in SIGNATURES.md document this the
  same way). Treated CALLING_NOTES.md's claim as stale/imprecise (same
  spirit as its own documented "Atari header cites subroutines that don't
  exist" pattern, just found in the disassembly's own supporting doc this
  time instead of Atari's) - not translated as a `wait()` call.
- **L6921's `BNE OBJOBJ_30` is a provably always-taken branch**: `LDA #$04`
  at L691F unconditionally sets Z=0 (4≠0), clobbering the Z flag the very
  `CMP TEMP3` two instructions earlier had set - so the branch always
  fires, regardless of that CMP's actual result. Only the CMP's CARRY
  (untouched by the LDA) matters, feeding OBJOBJ_30's `SBC TEMP3+1`. Same
  shape as CALLING_NOTES.md section 1's own VGADD tail-branch writeup
  ("not a bug, but would read as an 'if not X then Y' branch to someone
  porting line-by-line without checking what's actually tested"). Modelled
  as an unconditional `goto`.
- **Genuine residual-carry/register hazard at OBJOBJ_50** (the "either tank
  already flagged as colliding" early exit, `L695B LDA COLFLG / L695D ORA
  COLFLG+2 / L695F BNE OBJOBJ_60`): this reaches the shared RTS with `A` =
  `COLFLG | COLFLG+2` (a nonzero value that is generally NEITHER `0` nor
  `$FF` - e.g. `COLFLG=$20` alone gives A=$20), NOT the header-documented
  "0=nontank/-1=tank" contract, and with carry left over from whatever the
  LAST obstacle-table iteration's own `CMP`/`SBC` happened to leave (varies
  run to run with the playfield/table state - not a single provable value
  the way BUZBOM_40's FRAME-bit3 residual is). Translated faithfully
  (`r.a`/`r.c` simply keep whatever the preceding code left, matching the
  ROM exactly) and flagged here for lockstep to specifically exercise: any
  caller of `objobj()` that happens to hit this exact path while BOTH tanks
  are already mid-collision gets an off-contract return value straight
  from the real ROM's own behaviour, not a translation bug.
- `PTBLX1` ($7681)/`PTBLY1` ($76AB) confirmed (grep of the raw listing for
  their own label definitions, `PTBLX1:`/`PTBLY1:`) to be the exact SAME
  ROM table bzone_game.c's `c21s_()` already reads via the same two literal
  addresses - OBJOBJ and COLCHK's obstacle scan share one physical table,
  as their headers' shared mention of "PTBLO1,PTBLX1,PTBLY1" implies.
  `PROXTB` ($6995, OBJOBJ's own proximity-threshold table, immediately
  following OBJOBJ in the listing) is a SEPARATE table from `c21s_()`'s own
  `PRXTBL` ($6139) - two distinct proximity tables, no relation beyond the
  name similarity; double-checked so as not to accidentally conflate them.

### ROBCHK/TANKCK/R2D3CK/ROB1 structure

- All four form one connected control-flow chain (ROBCHK falls/tail-calls
  into TANKCK or R2D3CK; TANKCK falls/tail-calls into ROB1; R2D3CK falls
  straight into ROB1 with no branch at all) - same shape as bzone_game.c's
  own COLCHK/SAUCHK/C_21S_ cluster, and treated the same way: each public
  function (`robchk`/`tankck`/`r2d3ck`) just tail-calls the next one and
  returns, rather than trying to thread a shared "what to do next" value
  back up a call stack that the ROM itself never uses (every hand-off here
  is `JMP`/fall-through/`BNE`-to-a-label, never `JSR`/`RTS`).
- **ROB1's signature needed correcting** (same pattern as several routines
  in bzone_game.c before it - SAUCHK, EXINIT, REACT): the pre-existing
  `bzone_game.h` draft was `void rob1(void);`, but ROB1's own first
  instruction (`STA TEMP1`) proves it genuinely reads an input, matching
  the value TANKCK's own ROL-loop computes (`a = 0x0F` shifted left N times
  with 1-bits fed in) or R2D3CK's `PRAND & $0F`. Also, grepping the whole
  disassembly for `JSR ROB1`/`JMP ROB1` finds only TANKCK's own two
  internal references (`L6A1B BEQ ROB1`, `L6A1F JMP ROB1`) plus R2D3CK's
  fall-through - never called from outside this file's own cluster. Fixed
  to `static void rob1(uint8_t a)` in `bzone_enemy.c`; the old public
  prototype is removed from `bzone_game.h` entirely (see that file's own
  updated comment at the old ROB1 entry) - same convention bzone_game.c
  uses for `c21s_`/`m_top`/etc (internal-only fall-through targets get no
  header entry, TRANSLATING.md/SIGNATURES.md's own rule).
- **ROB1's PHP/PLP (L6A81/L6AB3) is a real, necessary carry save** - NOT
  the kind bzone_game.c's MOTION simplifies away (NOTES_game.md "MOTION
  structure", where the intervening code provably never sets the tested
  flag differently in this port's own register model). Here, between the
  PHP and the PLP, the code calls `SIN` (a real subroutine that clobbers
  flags) and does several `CMP #$80`/`ROR` pairs (Y-component computation) -
  all of which really do overwrite the live carry well before the PLP. The
  PHP'd value (bit 0 of a fresh `PRAND` read, via one `LSR`) is reused for
  BOTH the X-component decision (immediately, via the still-live carry -
  the `PHP` itself is not even needed for that first test) AND the
  Y-component decision much later (via the actual PLP). Modelled with an
  explicit `saved_c` local, exactly like EXPLDE's own genuine PHP/PLP case
  in bzone_game.c (NOTES_game.md "EXPLDE structure", case 2) - not the
  MOTION-style simplification. Verified the PHP and the matching PLP are
  gated by the IDENTICAL `BIT R2D3FL`/`BMI` condition at both L6A79/L6A7B
  (before the PHP) and L6AAF/L6AB1 (before the PLP), and `R2D3FL` is never
  written anywhere between those two points in ROB1's own body - so
  whenever the PLP path is taken, the PHP path was necessarily taken too
  moments earlier in the SAME call, meaning `saved_c` is always assigned
  before it's read (no uninitialized-read hazard, and no real 6502
  stack-imbalance risk either - the two pushes/pops are always paired).
- R2D3CK's `EXPOSZ+$C`/`+$D` writes use `A_STARTZ` (state_defs.h) directly
  as a 16-bit literal ($1800 = `STARTZ`) - confirmed against state_defs.h's
  own comment on that constant ("used by R2D3CK only as a 16-bit constant...
  not as an address") that this is NOT the Mathbox/MSTAT register it
  numerically collides with; no `hw_mb_write`/similar involved here, it's a
  plain two-byte RAM store of a compile-time constant.
- `GOVER`/`TANKCK` tail-call at ROBCHK's very first branch (`LDA GOVER/BNE
  TANKCK`) - confirmed this is a genuine data-dependent branch (not
  provably always one way), unlike several other "BNE"s in this file.

## Signatures registered

See `SIGNATURES.md`'s new "BZONE enemy AI" section: `buzbom`/`saucmv`/
`objobj`/`robchk`/`tankck`/`r2d3ck` all registered with their final (not
draft) signatures - all six matched the pre-existing `bzone_game.h` drafts
exactly, no changes needed on either side. `rob1`'s old draft prototype
(`void rob1(void);`) is removed from `bzone_game.h` - it is `static` in
`bzone_enemy.c` with a real `uint8_t a` parameter (see above).

## Calls into other modules (for integration to check)

- `bzone_math.h`: `track()` (buzbom, rob1), `sin_()`/`cos_()` (buzbom,
  rob1).
- `bzone_game.h` (agent "game", already done): `m_for1()`, `qwikck()`,
  `exinit()` (buzbom).
- `hw.h`: `hw_option()` (buzbom, robchk), `hw_pokey_read(0x0A)` (PRAND -
  buzbom does not use it directly but saucmv/robchk/tankck/r2d3ck/rob1
  all do), `hw_pokey_write()` (CHAN3V/CHAN4V in buzbom, CHAN3F/CHAN4F in
  r2d3ck), `hw_mb_write()`/`hw_mb_ylow()`/`hw_mb_yhigh()` (objobj).
- `state.h`: `cpu_rd()` for `PTBLO1`/`PTBLX1`/`PTBLY1`/`PROXTB`/`MISLVL`
  (objobj, buzbom, robchk).
- Within this file: `buzbom()` calls `objobj()`; `robchk()` calls
  `tankck()`/`r2d3ck()`; `tankck()`/`r2d3ck()` both call the `static
  rob1()`.
- Called BY other modules (confirmed against SIGNATURES.md's "ROBOT
  family" section, already handed over): agent "robot"'s `robot()`/
  `r_37s_()`/`fireit()` call `objobj()`/`buzbom()` (indirectly, per that
  section's own text); agent "game"'s `b_end()` calls `saucmv()`; MAIN
  (`bz_main`)/`dradar()` (display) call `tankck()`/`r2d3ck()`/`robchk()`.

## Open questions for lockstep/integration

1. OBJOBJ_50's residual-`a`/residual-carry exit (see "OBJOBJ structure"
   above) - worth a dedicated lockstep scenario that forces both
   `COLFLG`/`COLFLG+2` nonzero simultaneously while calling `objobj()`, to
   confirm the real ROM's own off-contract return value is what this port
   also produces (it should be, since nothing here "fixes" it).
2. CALLING_NOTES.md's WAIT-callers list including OBJOBJ appears to be a
   stale/imprecise claim in that document (see "OBJOBJ structure" above) -
   flagged in case a future re-read of CALLING_NOTES.md needs correcting,
   though this file does not own that document.
3. SAUCMV's `SAUCER` wraparound (`$FF`->`$00` skipping `SAUCMV_30`) is a
   1-in-256 real-hardware case - worth a targeted lockstep check (force
   `SAUCER=$FF` before a `saucmv()` call) rather than relying on random
   attract-mode coverage to happen to hit it.

## Cycle counting (2026-09-19)

Every routine (BUZBOM, SAUCMV, OBJOBJ, ROBCHK, TANKCK, R2D3CK, ROB1) charges the ROM's cycles with
CYC(). Lockstep, attract 5000 / coin_start 3000 / play_long 4000: OBJOBJ, SAUCMV, ROBCHK, TANKCK
exact on every untainted call; BUZBOM/R2D3CK are not reached by these scenarios (the buzz bomb
needs a higher score) - checked with tools\cyc_audit.py only.
- ROB1: three block sums were short when added by hand (L6A5A-L6A7B 56, L6A91-L6AB1 54,
  L6ABB-L6AD9 48) - found by lockstep (TANKCK/ROBCHK -4), cyc_audit.py written after.
- OBJOBJ's R2D3 radius block L692C-L6941 is 34 (was 32; not exercised by the scenarios).
- SAUCMV's move block L67B2-L67CA is 40.
- SAUCMV's last BNE (L680E) falls through into TRACK ($6810) when SAUCER wraps $FF->$00 (1 in
  256). The C used to return there; since 2026-09-19 it calls track() and discards the angle, as
  the ROM does (TRACK's RTS returns to SAUCMV's caller, B_END, which ignores A). Not reached by
  the lockstep scenarios.
