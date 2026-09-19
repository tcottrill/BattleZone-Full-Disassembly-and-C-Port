HANDOVER: compiles /W4 clean, ready for lockstep

# NOTES - agent "nmi-sound-coin" (bzsoun.c/.h, coin65.c/.h, bzone_nmi.c/.h)

## RESUME (read this first if picking this up cold)

| routine | ROM addr | status |
|---|---|---|
| `sndon` | SNDON $7985 | **done** (bzsoun.c) - 2nd review clean, no changes |
| `modsnd` | MODSND $79B9 | **done** (bzsoun.c) - 2nd review clean, no changes |
| `moolah` (+ static `detct1`/`detct6`/`s_bonus`/`s_extb`/`s_ext`) | MOOLAH $56C3 | **done** (coin65.c) - 2nd review clean, no changes |
| `nmi` | NMI $558F | **done** (bzone_nmi.c) - 2nd review found and fixed a real bug, see "Fixes" below |

**Compiles `/W4 /std:c11` clean** (verified from `c_src`):
```
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -no_logo
cl /nologo /W4 /std:c11 /D_CRT_SECURE_NO_WARNINGS /I. /c coin65.c bzsoun.c bzone_nmi.c
```
No warnings, no errors, on the first attempt after fixing one `undeclared identifier` (see Fixes #1
below). The `VsDevCmd.bat` run prints a harmless `'vswhere.exe' is not recognized...` line to stderr
(pre-existing on this machine, unrelated to this module) but still sets up `cl` correctly - ignore it.

All four public entries have now been through TWO independent instruction-by-instruction passes
against `../disasm/bzone_program_rom.asm` (SNDON $7985, MODSND $79B9, MOOLAH family $56C3-$57C7, NMI
$558F-$56C2): the original write-up, and this session's re-check line-by-line against the listing.
Two real defects were found and fixed (below); everything else checked out exactly, including every
RAM store's order, every hardware access's order/count through `hw.h`, the carry/flag chains
(ADC/SBC/ROL), indexed twin-cell addressing, the `S_MODLO`/`PNTRS`/`SOUND_7886` ROM table reads, the
START handler's `SBC TRAP1` at $564D, the 6-tick VTIMER VG buffer-swap, and SNDON's entry-A stack
peek.

`SIGNATURES.md`'s "BZSOUN, COIN65, NMI" section is now filled in (it had only the header line before
- the previous session died mid-update). This file and that section are both current as of this
handover.

## Fixes made in this session's 2nd review

1. **`OPTION` used as a RAM cell instead of the hardware seam** (`bzone_nmi.c`, START-handler LIVES
   computation, listing $5681-5689: `CLC/LDA OPTION/AND #3/ADC #2/STA LIVES`). `OPTION` at $0A00 is a
   *hardware register* (DSW0, read through `hw_option()` per `hw.h`/`state_defs.h`'s `A_OPTION`
   comment), not a RAM lvalue - `state_defs.h` deliberately has no `OPTION`/`OPTION_AT` macro for this
   reason. The code as originally written referenced a bare `OPTION` identifier that doesn't exist,
   which is why `/W4` failed with `error C2065: 'OPTION': undeclared identifier` on the very first
   compile attempt. Fixed by calling `hw_option()` instead. This is the trouble the RESUME note from
   the previous session anticipated in spirit (it flagged `VRAM()`/`A_VECRAM` address-vs-index mix-ups
   as the likely spot) but the actual first compile error was this RAM-cell/hardware-register mix-up
   instead - same family of mistake, different address.

2. **`nmi()`'s START-not-held path double-wrote `SOUND`** (`bzone_nmi.c`, listing $563C-$5641/$5694).
   The ROM does `LDA ALLPOT; AND #START; BEQ NMI_40` - when the start button is *not* held (i.e. on
   almost every NMI), this jumps straight to `NMI_40` (the VTIMER/buffer-swap tail), skipping `NMI_56`
   (`STA SOUND` at $5694) entirely; only the earlier `STA SOUND` at $5639 (inside `NMI_70`) has run.
   The C had this as `if (hw_pokey_read(0x08) & K_START) { <game-start block> }` with **nothing after
   the closing brace but the `nmi_56:`-labelled `hw_sound(snd);` line** - so when the `if` was false
   (the common case), control fell straight out of the `if` and into that `hw_sound(snd);` anyway,
   calling it a *second* time every single NMI where START isn't held. `hw.h`'s hardware seam is
   compared by the lockstep verifier in exact order/count, so this would have failed lockstep
   immediately and constantly (attract mode alone fires this every ~4ms). Fixed by flattening the `if`
   into an early `if (!(hw_pokey_read(0x08) & K_START)) goto nmi_40;` followed by the un-nested
   game-start block, matching the ROM's own straight-line `BEQ`-then-fallthrough shape (the same style
   already used for the `ATRACT`/`GOVER` bit-7 checks earlier in the same function) - see the `FIX (2nd
   review)` comment left in `bzone_nmi.c` at that line. Verified against
   `../disasm/CALLING_NOTES.md`'s §5 "`SOUND` latch" note, which independently confirms there are
   exactly 3 write *sites* in `NMI` ($5639/$565C/$5694) but that they are NOT all hit on every call -
   this fix restores that.

3. **`robchk()` seam swapped to the real header.** `bzone_game.h` (the "game" agent's header) now
   exists and declares `void robchk(void);` with the exact same signature this module's temporary
   local prototype used - swapped `bzone_nmi.c`'s `TODO(seam)` prototype for `#include
   "bzone_game.h"` per TRANSLATING.md's seam rule. Recompiled clean after the swap. Not a
   ROM-behaviour bug, just finishing the deferred seam wiring the previous session flagged.

Nothing else needed changing - the rest of `bzsoun.c`/`coin65.c`/`bzone_nmi.c` matched the listing
exactly on line-by-line re-derivation (see "2nd review findings" below for the detailed walk-through
of what was checked and confirmed correct, in case a disagreement ever needs re-litigating).

## 2nd review findings (detailed, for the record)

Re-derived by hand against `../disasm/bzone_program_rom.asm`, instruction by instruction, independent
of the original write-up:

- **SNDON** ($7985-$7B8): bit-scan (`idx*4+3` via the `ASL/ASL/ADC #$03` sequence) re-checked for a
  hidden carry-in from the two `ASL`s feeding the `ADC #$03` - there is no explicit `CLC` before it in
  the ROM, but `idx` is always 0-7 (proven both by the bit-scan's own range and by every real SNDWRD
  bit constant being a single bit 0-7), so `idx*4` never sets the carry out of either `ASL` and the
  extra `+carry` term is always 0. The C's plain `idx*4+3` is therefore exact, not just close. Also
  re-verified the **entry-A stack peek** by hand SP arithmetic (independent of the previous session's
  derivation): after `JSR SNDON` (pushes return address, `SP -= 2`) then `PHA/TXA/PHA/TYA/PHA`
  (`SP -= 3` more), `TSX` gives `X = SP_entry - 5`; `LDA $0103,X` reads absolute address
  `$0103 + (SP_entry-5) = $0100 + (SP_entry-2)` - which is *exactly* the byte SNDON's own first `PHA`
  (of entry `A`) had just written, regardless of what the caller's stack held before the `JSR`. Matches
  the previous session's conclusion and `../disasm/CALLING_NOTES.md` section 5's raw description of
  the mechanism (that note flags it as "unreliable, don't port literally" without doing this arithmetic
  - this module's `sndon(uint8_t a, uint8_t x)` using `a` directly is actually *more* faithful than
  that note's suggested workaround, not a deviation from it).
- **MODSND** ($79B9-$7A18): every branch target (`MODSND_15`/`17`/`20`/`30`) and the `SOUND_7886+FRCNT-4,Y`/
  `+CHANGE-4,Y` offset arithmetic re-checked against the listing; the "no AUDCTL write on the
  channel-0-idle path" asymmetry between `MODSND_15` and `MODSND_30` is real (confirmed by listing
  $79DC-$79E9 vs. $7A0A-$7A16) and the C's `continue;` (skipping the AUDCTL write) vs. fallthrough (with
  it) correctly reproduces it.
- **MOOLAH/S_DETCT** ($56C3-$5759): the per-mech LSR-count-by-X mapping (1/2/3 LSRs for X=2/1/0) and the
  carry-survives-through-LDA/AND/CPX-but-not-through-branches chain into `BCS S_DETCT_5` re-traced
  instruction by instruction; the `detct1`/`detct6` split and which entry carry state each is called
  with (always carry=1 for the "too long" `S_DETCT_6`->`S_DETCT_1` path, both possible for the
  wrap-around path) checked and correct. The mech-multiplier tail (`S_DETCT_85`, $5748-$5754) and its
  `PHA`/`ADC S_BCCNT`/`PLA`/`ADC S_CNCT` double-use of the pushed `units` value re-verified.
- **S_BONUS/S_EXTB/S_EXT** ($575C-$57C7): re-verified against Tempest's known-different later revision
  per the file's own header commentary (still accurate); the `~price+1+S_CNCT` two's-complement-negate
  idiom re-checked bit for bit against `EOR #$FF/SEC/ADC S_CNCT`; the phase-1-vs-phase-2 `+0xF0` vs.
  `+0xEF` distinction in `s_ext()` (implicit carry-in from a preceding `CMP` in phase 1, explicit `CLC`
  in phase 2) re-verified - these are genuinely different additions, not a typo, and the C's separate
  `t + 0xF0` / `t + 0xEF` expressions are correct as written.
- **NMI** ($558F-$56C2): re-traced every branch. The `EMCNT` 5-ROL/carry chain re-simulated by hand
  against $55AF-$55BC and matches the C exactly, including the `PHA`/`PLA` splice ordering. The sound-
  priority cascade (SUPER->SCOLFG->OBJCOL->SAUCER, $55C2-$55E1) and the explosion/shell-sound OR-in
  ($55E4-$5600) re-checked; the `ATRACT`/`GOVER` early-exit `goto nmi_56` paths re-checked (note:
  `ATRACT`'s *name* is inverted from its bit-7 meaning here - bit7 SET means a game is in progress, not
  attract mode; translated faithfully, not a bug, just a confusing variable name inherited from Atari).
  The slam-alarm nested `SNDON` call and its `PHA`/`PLA`-as-real-dataflow around it (`saved_snd`)
  re-checked. The credit-check/flash-bit `NMI_65`/`NMI_68` stack dataflow (one `PHA` at `NMI_65`,
  consumed by exactly one of `NMI_55`'s or `NMI_68`'s own `PLA`) re-checked - only one `PLA` per call
  ever executes, matching the single `saved_snd` local. Found the two real bugs listed above in the
  START-handler section ($5681-$5689 `OPTION`, $563C-$5694 the `SOUND` double-write) - everything
  else in `NMI` matched. The buffer-swap/VTIMER tail ($5697-$56BB) re-checked against `VRAM(A_VECRAM+1)`
  arithmetic (the previous session's own flagged risk spot) and is correct: `VRAM()` takes a CPU
  address ($2000-$2FFF) and the code passes `A_VECRAM+1` = $2001, matching `LDA/STA VECRAM+1` exactly;
  no address/index confusion was actually present.

## What the next session needs to know

**Battlezone's COIN65 assembly options** (from `battlezone-main/BZONE.MAC`, right before
`.INCLUDE TC65`, ~line 6700): `MECHS=3`, `EMCTRS=3`, `$LMBIT=8`, `BONADD=1`, `COIN=0`, `COIN01=1`,
`CNTINT=0`, `SLAM=0`, `INCLUDE=1`. All already exposed as `K_MECHS`/`K_EMCTRS`/`K_S_LMBIT` etc in
`state_defs.h` - no need to re-derive.

**What came from Tempest's `alcoin.c` vs. what had to change.** This game's COIN65
(`../disasm/bzone_program_rom.asm` $56C3-$57C7) is a genuinely EARLIER revision than Tempest's/Space
Duel's (TRANSLATING.md brief, confirmed by reading the listing): **no `$BC` bonus-coin-earned cell**.
`S_BONUS` (label `S_DETCT_120`, $575C) increments `S_CNCT` ($21) **directly** (`INC S_CNCT` at
$576E/$5774) instead of a separate cell Tempest's `alcoin.c` calls `S_BC`. Knock-on effect: Tempest's
`s_extb()` has a "cover the shortfall from S_BC" step (`a = a + S_BC; if (a & 0x80) { ... }`) that THIS
ROM's `S_EXTB`/`S_CNVRT` ($5780) simply does not have - on a shortfall (`BCC S_EXT` at $578F) it just
leaves `S_CNCT` untouched and falls straight into `S_EXT`. This makes sense once you notice the bonus
coin is already folded into `S_CNCT` by `S_BONUS`, so there's nothing left to "cover" separately. The
mech-debounce state machine (`S_DETCT` proper, $56C5-$5759) and the electro-mechanical counter pulse
timer (`S_EXT`, $579B-$57C7) are, byte for byte, the SAME code as Tempest's - `alcoin.c`'s `moolah()`
loop body and `s_ext()` translate over almost unchanged (only the RAM-cell/constant names differ, which
`state_defs.h` already provides). Wrote two small `static` helpers, `detct1()`/`detct6()`, for the two
different "S_DETCT_1-ish" tails `S_DETCT_5`'s branches land on (see the big comment in `coin65.c` - this
was the one place I initially wrote something that doesn't compile, an invalid `goto label(arg)`, before
catching it and refactoring into these two named helpers; if you're re-deriving this, watch for that trap
- there really are two DIFFERENT tails, `S_DETCT_1` proper (with the slam re-check) and `S_DETCT_6`'s own
`STA $CNSTT,X`-then-`PSTSL` tail that skips it).

**`S_MODLO` ($5778)**: 8-byte ROM table right after `S_BONUS`'s code, not a named RAM cell and not in
`state_defs.h` (it's inline in program ROM, not a `.alias`). Defined locally in `coin65.c` as
`#define A_S_MODLO 0x5778` and read through `cpu_rd()`, same pattern Tempest's `alcoin.c` used for its
own copy of this table.

**SNDON's stack peek** (`LDA $0103,X` at $798B, after `PHA/TXA/PHA/TYA/PHA/TSX`): worked the SP
arithmetic by hand (see the long comment in `bzsoun.c`/`bzsoun.h`, and this session's independent
re-derivation above) - it deterministically reads back SNDON's OWN entry `A`, on every call, regardless
of what the caller's stack held before the `JSR`. This resolves `../disasm/CALLING_NOTES.md` section
5's "unreliable, don't port literally" flag: it's not unreliable, it's just not what that note assumed.
Translated per the module brief ("translate as the entry A") - `sndon(uint8_t a, uint8_t x)` just uses
its own `a` parameter where the ROM peeks the stack. The `x`-fallback path (used only when the
peeked/entry `a == 0`) is kept for fidelity but is provably dead in this ROM: all 8 real call sites
(`../disasm/CALLING_NOTES.md` section 5 lists them) pass a nonzero `SNDWRD` bit constant.

**NMI structure** (`bzone_nmi.c`): the tricky parts, in case they need re-deriving:
- `EMCNT` ($1000) is built from a 5-ROL chain across `S_CCTIM`/`S_CCTIM+1`(twice)/`S_CCTIM+2`/(restored
  `S_CCTIM+1`) with a `PHA`/`PLA` splicing the two halves - simulated literally with an explicit `carry`
  variable rather than trusting a hand-derived closed-form bit formula (I derived one as a cross-check -
  final byte = `(CCTIM[1]&0x1F)<<3 | (CCTIM[0]>>7)<<2 | (CCTIM[1]>>7)<<1 | (CCTIM[2]>>7)` - but the
  literal simulation in the code is the thing to trust if the two ever disagree).
- The `SOUND` ($1840) latch is written 1, 2, or (in one theoretically-unreachable fallthrough) 3 times
  per NMI depending on path - traced every path by hand (see the `nmi_56`/`nmi_40` labels and the long
  comments above them in `bzone_nmi.c`); don't "simplify" this to a single write without re-checking.
  **This session found and fixed a real bug here** (Fix #2 above) where the common "START not held"
  path was getting an extra, ROM-incorrect write - re-check this spot especially hard if you ever touch
  this function again.
- The `NMI_65`/`NMI_68` stack push/pop is real dataflow (the accumulated sound word), not a register
  save - modelled as the `saved_snd` local, consumed by exactly one of the two paths (`NMI_55` or
  `NMI_68`'s own `PLA`).
- `ROBCHK` is called from `nmi()` and belongs to the "game" agent - now wired to the real
  `bzone_game.h` (Fix #3 above; the temporary local prototype is gone).
- The `TSX/BMI` stack-pointer-corruption re-entrancy guard at the very top of `NMI` has no C
  equivalent (no 6502 hardware stack to blow) and is elided entirely - `nmi()` always takes the path the
  real hardware takes when the guard doesn't trip.

## Judgement calls (for the record)

1. `sndon`'s stack-peek-as-entry-A and its dead `x`-fallback path - see above. Listing: $798B/$798E.
2. `nmi()`'s stack-blown guard elided (no HW stack to model). Listing: $5591-$559F/$56BD-$56C2 (register
   save/restore also elided throughout, same reasoning).
3. `EMCNT`'s ROL chain translated as literal carry-flag simulation rather than the closed-form bit
   formula, to minimize the chance of a hand-derivation mistake surviving into a "clean" C expression.
   Listing: $55AF-$55BC.
4. Two different `S_DETCT_1`-ish tails in `MOOLAH` (`detct1`/`detct6` in `coin65.c`) - see above.
   Listing: $56E9 vs. $571F.
5. `S_MODLO` treated as a ROM table read via `cpu_rd()`, address hardcoded locally (`0x5778`) since it
   has no `state_defs.h` alias (it's unlabelled inline data, not a `.alias`). Listing: $5778.
6. Nothing in this module's ROM range is an actual ROM bug (unlike some hazards
   `../disasm/CALLING_NOTES.md` flags elsewhere) - the "unreachable" spots found (`NMI_54`'s fallthrough
   after game-start's `LIVES` computation, `VGADD`-style always-taken branches) are provably unreachable
   given the actual value ranges involved, not logic errors, and are translated faithfully anyway (dead
   `if`s kept, not deleted) per TRANSLATING.md's "translate it faithfully and note it" rule. The two
   defects found this session (Fixes #1/#2 above) were both **translation** bugs (mistakes introduced
   while porting), not ROM bugs - the ROM itself is correct at both spots.
7. Flattened the START-handler's `if (START) { block }` into `if (!START) goto nmi_40;` followed by an
   un-nested block (Fix #2) - a deliberate style choice to make the "common case skips the rest"
   shape visible at the top of the block, matching how the `ATRACT`/`GOVER` checks earlier in the same
   function are already written, rather than nesting the whole ~40-line game-start block one level
   deeper just to keep an `if` brace pair.

## Signatures registered

See `SIGNATURES.md`'s "BZSOUN, COIN65, NMI" section (now filled in with the full table - it had only
the header line before this session):
- `void sndon(uint8_t a, uint8_t x);` (bzsoun.h)
- `void modsnd(void);` (bzsoun.h)
- `void moolah(void);` (coin65.h)
- `void nmi(void);` (bzone_nmi.h)

## Open questions for integration

- ~~`robchk()` needs `bzone_game.h`~~ **RESOLVED**: `bzone_game.h` now exists and declares
  `void robchk(void);` with the exact signature this module expected; `bzone_nmi.c` now `#include`s it
  (Fix #3 above).
- `bzone_nmi.c`'s calls to `moolah()`/`modsnd()`/`sndon()` are the only cross-module calls this module
  makes into ITS OWN other files (all within this agent); `robchk()` is the only call into another
  agent's module.
- ~~Not yet compiled~~ **RESOLVED**: compiles `/W4` clean (see RESUME table above).
- **Still open, for whoever builds lockstep.exe** (not resolvable by re-reading the listing - it's a
  question about test-harness design, not ROM behaviour): `S_DETCT`/`S_BONUS`/`S_EXTB`/`S_EXT` and
  `MODSND_15`/`MODSND_30` etc are internal labels only reached by `MOOLAH`'s/`MODSND`'s own
  fall-through and loop, never `JSR`-called elsewhere in this ROM (checked against
  `../disasm/CALLING_NOTES.md`) - modelled as `static` helpers/loop bodies, not exported, matching the
  convention `vgut.c` already established for VGUT.MAC's own fall-through chain (`vghal1`/`vghex1`/
  `vgjmp1`/`vgwai1`). If the lockstep verifier turns out to need per-internal-label hooks for coverage,
  these will need to stop being `static` and get real entries.

## For the integration agent (post-handover)

Per the brief: this module's `.c`/`.h` files are now frozen from this agent's side - fixes from here on
are integration's. If lockstep turns up a discrepancy in `sndon`/`modsnd`/`moolah`/`nmi`, this file's
"2nd review findings" section above documents exactly what was checked and why it was believed correct,
which should make it faster to spot whether a NEW issue is a genuine 3rd bug or a lockstep-harness/seam
issue on the other side (e.g. an `hw_*` stub not yet matching real hardware timing). Post findings about
this module in `NOTES_integration.md`; this file's RESUME section will keep being updated (read-only from
this agent's .c files, but the notes can still record answers) if asked to help interpret something.

## Cycle counting (2026-09-18)

Every routine in bzone_nmi.c, coin65.c and bzsoun.c charges the ROM's own cycles with CYC()
(TRANSLATING.md "Cycle counting"). Lockstep, all three standard runs (attract 5000,
coin_start 3000, play_long 4000): MOOLAH, MODSND, SNDON exact on every untainted call; NMI exact
except the one game-start NMI per run, whose JSR ROBCHK (bzone_enemy.c) is not cycle-counted
yet.

Judgement calls and fixes:
- The elided register saves/restores (PHA/TXA/TSX/BMI guards, PLA/TAY/.../RTI) are charged
  where they sit, though nothing else is modelled for them.
- NMI: the BMI NMI_2 at L559F runs only on the SYNC path (INC SYNC, 1 NMI in 16); L559B's BNE
  goes to NMI_10 ($55A1), past it. First cut charged it always (+2 on 15/16 NMIs).
- NMI's sound-priority cascade (L55C2-L55DF) is one path-dependent ternary CYC() keyed on the
  same flags the C tests, so the C's if/else shape did not have to change.
- SNDON: its entry saves (L7985-L798A, 15 cycles) were missing, and the taken BEQ at L79A5
  (channel pointer 0) was charged 2 instead of 3.
