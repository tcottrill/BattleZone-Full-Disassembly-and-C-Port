# NOTES_integration.md - the integration agent's log

Owner: integration agent (folds translated modules into `tests\lockstep.c`,
one module at a time, per its own HANDOVER signal in `NOTES_<module>.md`).
Never edit `..\disasm`, generated files, or a module that has not handed
over. Read with `STATUS.md`, `PLAN.md`, `TRANSLATING.md`, `SIGNATURES.md`.

## RESUME HERE (checkpoint - refresh after every significant step)

**Status as of this checkpoint (2026-09-18, later session): EVERY
module of BZONE.MAC/BZSOUN/COIN65/VGUT is translated, handed over, and
folded into `tests\lockstep.c` - nmi-sound-coin, maths, display, game
(first half), robot, enemy. Only BZSTST remains untranslated (planned
last, port order item 7); no ROM thunks are needed for anything else any
more (`lk_rom_call()` etc. stay as generic infrastructure for BZSTST).**

**Current numbers (2026-09-18, after "Fixes made" #10-#16; full rebuild
`ALL BUILDS OK`, `/W4` clean, 0 warnings):**
- `lockstep --frames 5000` (attract): **PASS - 3,140,249 compared, 0
  failed, 87,048 tainted.** VOLCNO 4960 passed / 39 tainted, DRADAR 2136 /
  1711, `$6B79` dummy read excluded 753 times.
- `lockstep --script tests\scenarios\coin_start.txt --frames 3000`:
  **PASS - 1,759,516 compared, 0 failed, 46,077 tainted.** No crash.
  MOTION 2556/187, WNSHLD 62 passed / 7 tainted (covered again), VOLCNO
  2953/46, DRADAR 1774/900.
- `refrun --frames 600`: PASS.
- Every open item from the previous checkpoint is resolved (see below).
  Remaining coverage limits (not failures): CKSCOR (1 check) and DISTBL
  (256 coin_start, 1152 attract) are always NMI-tainted - both run long
  enough that an NMI always lands inside them; HISCRE is never reached by
  either scenario (needs a scripted high-score entry). The pass-level
  probe still fails by design until NMI placement exists (below).

### CRITICAL fix this session: a real crash, not just a mismatch

**`coin_start --frames 500`+ crashed hard** (process terminates, zero
output - a stack overflow gives stdio no chance to flush) as soon as the
scripted play actually got the player's tank hit (`CRACK` becomes
nonzero for the first time, around frame 454 in that scenario - bisected
frame-by-frame: 453 clean, 454+ crashes, 100% reproducible). Root cause:
`WNSHLD`/`CKSCOR`/`HISCRE`/`DISTBL` (`bzone_disp.c`) each end their own C
body with a real, unconditional call into `b_end()` or `bz_main()`
(matching the ROM's own "JMP B_END"/"JMP MAIN" tail) - and `b_end()`/
`bz_main()` call each other forever by design, matching the ROM's own
endless frame loop (`NOTES_game.md`'s own "flagged for integration"
caveat). `bz_main()`/`b_end()` themselves were already excluded from
`addr_routine[]` (so the ROM's OWN "JMP MAIN"/"JMP B_END" never auto-opens
a check for them - the fix from earlier in this session, for the display
module's tail calls) - but that did nothing to stop a check that DOES
open for WNSHLD/CKSCOR/HISCRE/DISTBL from crashing the moment ITS OWN
replay runs: `run_compare()` calls the real `wnshld()`/etc., and THEIR
body then makes an ordinary, unbounded C call into `b_end()`/`bz_main()`
- recursing through the rest of the game's execution, transitively,
forever. A temporary `fprintf`-per-open-check diagnostic (added, used,
removed) confirmed the last check opened before the crash was WNSHLD.
**Fixed** by excluding all four from `addr_routine[]` too (same mechanism,
`tests\lockstep.c`'s `main()`) - they stay listed in `routines[]`/
`--list-untranslated` but are never auto-opened, so no lockstep coverage
exists for these four routines specifically until "game" restructures
`bz_main()`/`b_end()` to return after one pass instead of recursing
(exactly the fix `NOTES_game.md` already asked integration to consider -
out of scope to do unilaterally here, since it means rewriting the shared
game-loop contract these four AND the pass-level probe both depend on).
**This is the most important finding of this session** - a memory-safety
bug that only manifests when a real game is actually played long enough,
invisible to any attract-only testing (attract mode never sets `CRACK`).

**Since resolved properly**: `bz_main()`/`b_end()`/WNSHLD/CKSCOR/HISCRE/
DISTBL now use the pass_signal trampoline (`bzone_game.h`: each routine's
`_body()` returns a signal at its ROM `JMP`, `main_pass()` is the one
bounded loop). The four are auto-opened again; each check's window ends
at the ROM's JMP into MAIN/B_END (existing close) or, for WNSHLD's
`L521F JMP CKSCOR`, at the JMP into the next trampoline routine
(`is_tail_chained()` in `tests\lockstep.c`, "Fixes made" #15).

### Previous checkpoint's open items - all resolved

1. **WNSHLD/CKSCOR/HISCRE/DISTBL coverage** - lifted (above). Coverage
   immediately found a real bug in WNSHLD ("Fixes made" #14).
2. **BATTLE logo-animation bug** - inverted `BPL` at `$52D3` ("Fixes made"
   #10). BATTLE 0 failed in both scenarios.
3. **VOLCNO residual carry path** - `L5917 CMP #$A2` sets carry on both
   exits and the C ignored it ("Fixes made" #11). VOLCNO 0 failed.
4. **DRADAR rare mismatch** - the harness logged DRADAR's `$6B79 CMP
   ($00,X)` dummy read whenever its pointer landed on I/O; the C
   translation was already right ("Fixes made" #12). DRADAR 0 failed.
5. **MOTION `SAVE1`** - missing `L62C0 STX SAVE1` in `m_nex0()` (player
   fires a shell; "Fixes made" #13). MOTION 0 failed.

**Pass-level probe: disabled by default** (`--pass-probe` to opt in). Two
stale-contract bugs fixed ("Fixes made" #16): it now runs `main_pass()`
(not `bz_main()`, which is one segment since the trampoline) and keeps its
whole-pass I/O slice (the depth-0 log recycle used to discard it). It
still fails every pass, now for the one real remaining reason: the ROM
pass includes the NMIs that landed inside it (watchdog kick, POKEY/sound
writes, SYNC/BUFRDY/coin/sound RAM), and the C pass never runs `nmi()` -
first divergence is typically `C read $0A00, ROM wrote $1400`. Next step
for Gates 1/N is an NMI-placement device (Tempest M3+'s IRQ placement:
record where each NMI landed, e.g. by I/O-log index, and run `nmi()` at
that point of the C pass; `bz_main_body()`'s `BUFRDY` spin at MAIN_5
needs the same injection or it spins). Do not read the probe's FAIL as a
regression signal until then.

**Status as of an EARLIER checkpoint in this same session (kept for
history, all numbers below have since moved - see the top of this file
for the current ones):**

### M1 (VGUT + lockstep verifier): CLOSED OUT this session

All 7 "Not yet done" items in `NOTES_m1.md` are done. One real bug found
and fixed along the way (not just the textual surgery the item asked for)
- see `NOTES_m1.md` section 8 for the full account:

- `tests\refrun.c` switched from its local AVG-timing stand-in to
  `avg_walk()` (`avg.c`/`avg.h`). The switch surfaced a unit bug:
  `avg_result.cycles` is in AVG **master** cycles (`AVG_CYC_PER_CPU`=8/CPU
  cycle), but was being added straight into the CPU-cycle `vg_busy_until`
  with no `/8` - draw time and the VTIMER-busy stat came out ~8x too high.
  Fixed in **both** `tests\refrun.c` and `tests\lockstep.c` (same latent
  bug, same shape, same fix): `(ar.cycles + AVG_CYC_PER_CPU - 1u) /
  AVG_CYC_PER_CPU`. After the fix, `refrun --frames 600` is byte-for-byte
  identical to `NOTES_m0.md` section 8's recorded numbers.
- `$6B79` dummy read now reported separately in `refrun`'s summary line
  too (matching `lockstep.c`'s existing format).
- `tests\lockstep.exe` added to `build_all.bat` (also had to add `avg.c`
  to the shared `HW` object list - `refrun.exe`'s link was already broken
  before this session for the same missing-object reason, just never
  re-run since the avg.c switch was made).
- Mutation-test executables/scratch dirs: already absent on disk, nothing
  to clean up.
- `PLAN.md`'s M1 row and `NOTES_m1.md`'s RESUME section updated with final
  numbers.

**Final numbers** (all confirmed by this session's own rebuild + run, not
just carried over from notes):
- `refrun --frames 600`: PASS, byte-identical to `NOTES_m0.md`. Two-run
  determinism into the same `--outdir`: PASS, 71/71 files MD5-identical.
- `refrun --frames 5000`: PASS (13,819 GOADDs, avg draw 15.29 ms, 10/13820
  VTIMER-busy).
- `lockstep --frames 5000` (attract): **PASS, 1,119,992 compared, 0
  failed, 14,160 tainted.**
- `lockstep --script tests\scenarios\coin_start.txt --frames 3000`:
  **PASS, 581,064 compared, 0 failed, 6,262 tainted.**
- `build_all.bat`: `ALL BUILDS OK`, `/W4` clean, 0 warnings.
- Registered in lockstep's adapter table so far: all 17 VGUT routines
  (VGRTSL/VGHALT/VGHEXZ/VGHEX/VGJMPL/VGJSRL/VGSTA1/VGSTAT/VGCNTR/VGADD2/
  VGADD/VGSCA1/VGSCAL/VGDOT/VGVTR/VGVTR1/VGVTR2/VGVCTR) - see `NOTES_m1.md`
  section 5 for the per-routine checked/passed/failed/tainted breakdown.

### Correction from the coordinator (mid-session)

The coordinator had already relaunched the four module-owner agents
directly; the four I started via the `Agent` tool were duplicates and were
stopped. **Never spawn sub-agents for module work in this project** - only
the coordinator does that. My own job is: prepare lockstep's machinery,
fold in each module as its own `HANDOVER:` line appears (the coordinator
tells me directly when), and report numbers/fixes back here.

### M2 (nmi-sound-coin) and part of M3-M5 (maths): HANDED OVER and FOLDED IN

Both modules' `NOTES_*.md` now start with `HANDOVER: compiles /W4 clean,
ready for lockstep`. Folded both into `tests\lockstep.c` in the same pass
(maths depends on nmi-sound-coin's machinery being in place first: the
hardware seam and ROM-thunk infrastructure both modules need).

**(a) Lockstep machinery built** (generic, verified to change nothing for
VGUT - see "Regression check" below):
- **`hw.h` implemented** in `tests\lockstep.c` (`hw_in0/hw_option/hw_opton2/
  hw_pokey_read/hw_pokey_write/hw_mb_status/hw_mb_ylow/hw_mb_yhigh/
  hw_mb_write/hw_emcnt/hw_goadd/hw_wtchdg/hw_vstop/hw_sound`) - it had no
  implementation anywhere in the tree before now (M1's VGUT never called
  it). Each one replays against the currently-completing check's own slice
  of the shared I/O log via new `replay_read()`/`replay_write()` (address,
  read/write kind, and value except for the two true "any value" strobes
  `is_strobe()` flags - WTCHDG $1400, VSTOP $1600; GOADD's value is real
  per PLAN.md and IS checked).
- **Adapter-table shape extended for `regs`**: added `ret_z`/`ret_n`/
  `ret_v` alongside the existing `ret_c`/`ret_a`/`ret_x`/`ret_y`, and
  `run_compare()`'s flag comparison now covers all four P-register bits a
  `regs`-returning routine might document. (No currently-registered
  routine sets z/n/v yet - only `.c` is documented anywhere so far - but
  the plumbing is there for the next one that does.)
- **ROM thunks**: `lk_rom_call()` runs a not-yet-translated callee's own
  ROM bytes on a second, throwaway `cpu` instance directly over the shared
  `g` (RAM/vector RAM via `cpu_rd`/`cpu_wr`, hardware via the SAME
  `replay_read`/`replay_write` every `hw_*` uses) until a fake return
  address pops it to PC=$FFFF, then restores every stack byte it touched.
  Three thunks currently defined this way (all "game"-owned, not handed
  over): `robchk()` ($69BE, called from `nmi()`), `explde()` ($5B89) and
  `tr7chk()` ($69B5) (both called from `rotate()`). None of the three are
  registered in `routines[]` (no real translation to compare against yet -
  they trivially "pass" by construction; same as Tempest's `thunk_addr[]`
  being disjoint from its checked-routine table).
- **NMI as its own checked unit**: `routines[]` has an `"NMI"` ($558F)
  entry, opened NOT via the JSR/JMP address table (excluded from
  `addr_routine[]` by name, since the ROM never JSRs there) but explicitly
  in `main()`'s loop right after `cpu_nmi(&c)` (so `entry_s` already
  reflects the NMI's own 3-byte push). Closing/nesting/tainting all fall
  out of the *existing* generic machinery with zero special-casing (an
  RTI raising SP above `entry_s` closes it exactly like an RTS would; a
  JSR to another registered routine - e.g. MOOLAH - while NMI's own check
  is open nests exactly like any other case; another NMI landing while
  checks are open, including NMI's own, taints all of them via the
  unmodified existing loop). This matches Tempest's M1/M2-era bar (IRQ
  checked natively, taint-counted) - not its later M3+ IRQ-placement/
  IRQ-moved machinery, out of scope per PLAN.md's own M2 exit criterion.
- **`--list-untranslated`**: a small hand-rolled scanner over
  `../disasm/build/program_refs.json`'s `"routines"` object, cross-
  referenced against `routines[]` by address (falling back to a name match
  for `NMI`, the one entry deliberately absent from `addr_routine[]`).
  Prints checked/passed/failed/tainted per ROM routine, `NO` for anything
  not yet registered. Diagnostic only.

**Regression check (task requirement)**: re-ran `lockstep --frames 5000`
right after building the machinery (before registering any new routine) -
VGUT's own per-routine numbers were byte-identical to the M1 baseline in
`NOTES_m1.md` (VGHALT 4999/4900/0/99, VGHEXZ 88376/86180/0/2196, ... every
row unchanged). Confirms the new machinery is additive, not disruptive.

**A real infrastructure bug found while testing (not a module bug)**: the
shared I/O log (`iolog[]`/`io_n`, `IOLOG_MAX` = 2^20) never shrank - fine
for M1 (VGUT never touches `hw.h`, so the log was always empty), but once
Mathbox-heavy routines (PNTPUT/KLUDGE, ~10 hw events per call, thousands
of calls per attract run) started logging for real, `io_n` blew through
`IOLOG_MAX` within a few hundred frames; every later I/O event was then
silently dropped (`io_overflow`), corrupting every check's replay window
from that point on (4,394,736 overflow events, ~97% of all checks failing
on the first attempt). Fixed: whenever the check stack fully unwinds
(`depth == 0`), every check that ever referenced an `iolog[]` index has
already been replayed and compared (checks close LIFO, and a check's own
replay fully consumes its slice before returning) - so it is always safe
to reset `io_n = 0` there. `io log overflow` is 0 in every run since.

**(b) Registered in `routines[]`** (per SIGNATURES.md's now-final
prototypes): `SNDON` $7985, `MODSND` $79B9, `MOOLAH` $56C3, `NMI` $558F
(nmi-sound-coin); `WAIT` $5B80, `ROTATE` $5946, `ROTPNT` $5B1F, `PNTPUT`
$5D4A, `KLUDGE` $5E05, `BOUND` $5E44, `COS` $5E4B, `SIN` $5E4E, `SIN1`
$5E63, `COSXTR` $5EF9, `SINXTR` $5F17, `ABSTMP` $6149, `TRACK` $6810,
`T_0`/`T_1`/`T_2`/`T_3` $68D5/$68C6/$68CB/$68D0 (maths).
`build_all.bat`'s `tests\lockstep.exe` line now links `bzsoun.c coin65.c
bzone_nmi.c bzone_math.c` alongside `vgut.c` (new `set MODULES=...` var).

**(c) Fixes made - see the dedicated section below** (four total: one
build-portability issue in the maths module's naming, one missing RAM
store, one wrong shift operand, one missing hardware write).

**Final numbers, both scenarios, after all fixes**:
- `lockstep --frames 5000` (attract): **PASS, 2,511,283 compared, 0
  failed, 33,441 tainted.**
- `lockstep --script tests\scenarios\coin_start.txt --frames 3000`:
  **PASS, 1,400,061 compared, 0 failed, 18,065 tainted.**
- `build_all.bat`: `ALL BUILDS OK`, `/W4` clean, 0 warnings.
- `--list-untranslated`: **36/120 ROM routines now registered** (verified
  by running it directly; up from 18 at the end of M1 - VGUT's 18 table
  rows - plus this session's 4 nmi-sound-coin + 14 maths-with-real-ROM-
  addresses rows; `T_1`/`T_2`/`T_3`/`T_0` show `checked 0` since the ROM
  never `JSR`s them directly, per bzone_math.h's own note - same
  "registered but never directly called" treatment VGUT's VGRTSL etc. get).

### M3-M5 (display, game): display HANDED OVER, queued next; game not yet

`NOTES_disp.md` now starts with `HANDOVER:` too (26 routines,
`bzone_disp.c/.h`) - coordinator's brief for it queued after nmi-sound-coin
and maths, with four specific empirical questions to answer via lockstep
(VOLCNO's uncontrolled carry-in, DRADAR's sign-extend, the WNSHLD/CKSCOR/
HISCRE tail-jump-into-MAIN handling since "game" is not handed over, and
the `sin_`/`cos_` rename this session already made). Not started yet -
next step after this checkpoint.

`game` module: not handed over. `robchk`/`explde`/`tr7chk` stay ROM thunks
in `tests\lockstep.c` until it is.

**Next step**: read `NOTES_disp.md` sections 2/4/6 + `bzone_disp.h` +
SIGNATURES.md's display section in full, then register `bzone_disp.c`'s
26 routines the same way, deciding a documented approach for the tail-
jump-into-MAIN cases (treat as end-of-checked-region vs. a ROM thunk) and
answering the VOLCNO/DRADAR empirical questions from real lockstep runs.

## Fixes made during integration (routine, address, what was wrong, how found)

Items 1-4 found by running `lockstep --frames 5000` / `--script
coin_start.txt --frames 3000` after registering nmi-sound-coin + maths,
each isolated with `--mutation-check <NAME>` to quiet the other routines'
output while narrowing down the cause; items 5-9 the same way after
folding in display and game's first half (item 9's crash needed a
frame-by-frame bisection of `coin_start` instead, since it produced no
output at all to isolate with `--mutation-check`). Every module touched
below had already handed over (`HANDOVER:` line present) before any fix
was made, per the rule.

1. **Build portability - `cos`/`sin` reserved by cl.exe (bzone_math.h/.c,
   "maths")**. `regs cos(uint8_t)`/`regs sin(uint8_t)` compiled clean under
   a bare `cl /W4 /std:c11 /c` (what the module's own NOTES said it last
   checked with) but hard-errored under `build_all.bat`'s actual flags
   (`/O2` added): `C2169: 'cos': intrinsic function, cannot be defined` -
   MSVC reserves those two identifiers unconditionally, and a `#pragma
   function(cos, sin)` workaround (tried first) only stops the compiler
   inlining an ALREADY-`double`-typed definition, it cannot repurpose the
   name for a different signature (`C2371: redefinition; different basic
   types` on the next attempt). Renamed to `cos_`/`sin_` (bzone_math.h,
   bzone_math.c, `SIGNATURES.md`, and this file's own lockstep adapters) -
   this was in fact already anticipated: `NOTES_game.md`'s own TODO(seam)
   block used `sin_`/`cos_` for these two routines before bzone_math.h
   settled on the plain names.
2. **Missing RAM store - `SAVE3` ($0018), PNTPUT ($5D4A), bzone_math.c**.
   `SAVE3` is a real RAM cell the ROM writes (`STX`/`STY SAVE3`) and leaves
   changed after PNTPUT returns; the C translation kept it as a local
   variable (`uint8_t save3;`) instead of the `state_defs.h` macro, so
   `g.ram[$18]` never actually changed. Every lockstep check whose window
   included a PNTPUT call failed the RAM compare at `$0018` (attract:
   14,850/24,135 PNTPUT checks failing before the fix). Fixed by using the
   real cell (`SAVE3`) throughout, matching `SAVE1`/`SAVE2`'s own treatment
   three lines up in the same function.
3. **Wrong shift operand - ROTATE ($5946), the radar-antenna Y coordinate,
   bzone_math.c**. `L5A54 TXA / L5A55 ASL` loads A from X (SIN's HIGH byte,
   `r.x`) and shifts THAT; the C code shifted `r.a` (the low byte) instead
   (`a = asl8(r.a, &cs);` - even the file's own inline comment said "ASL(on
   r.a)", contradicting its own "TXA(r.x kept)" half). The X-part just
   above (PTBLX2, via COS) already shifts `r.x` correctly, which is what
   exposed the asymmetry once lockstep ran enough frames to reach a
   non-tainted ROTATE check with the radar-antenna code live: 9/10 non-
   tainted checks failed with small-magnitude diffs at `PTBLY2_AT(4)`/`+1`
   (`$023C`/`$023D`). Also found and fixed en route: a genuinely INVERTED
   branch a few lines earlier - `BIT LOGO / BPL ROTATE_33` branches to
   ROTATE_33 when LOGO's bit 7 is CLEAR, with the `LDX #$00/JMP R10S_` that
   follows being the fall-through (bit 7 SET); the C code had `if
   ((int8_t)LOGO >= 0) { ...goto r10s_...}` - backwards. This was the
   *first* symptom lockstep caught (10/10 non-tainted ROTATE checks failing
   with a Mathbox write mismatch at $1868/XMATRX, cascading into dozens of
   RAM diffs once the shared I/O log desynced) - fixed before the shift bug
   above, which only became visible once the branch was corrected.
4. **Missing hardware write - NMI ($558F), the game-start path,
   bzone_nmi.c**. `L5652 LDA #$00 / L5654 STA SKCTL` (POKEY register $0F,
   value 0) was missing entirely from the translation - the adjacent
   comment ("L5652/57") jumped straight from the `LDA` to `S_BCCNT`'s own
   store, skipping the hardware write in between. Only reachable when a
   coin is inserted and START is actually pressed, so only the
   `coin_start` scenario ever exercised it: 1/48,832 NMI checks failed
   (the very first real game-start NMI), with an I/O mismatch at exactly
   this point ("C wrote $00 to $1840 [SOUND], ROM wrote $00 at $182F
   [SKCTL]" - both writing the SAME value 0, only the address differed,
   confirming a missing write rather than a wrong one) plus a cascade of
   RAM diffs in Mathbox-adjacent scratch cells (TEMP1/X1/Y1 - all touched
   by ROB1, which ROBCHK's own thunk falls into) downstream of the desynced
   I/O log. Fixed by adding the missing `hw_pokey_write(0x0F, 0x00);` call
   in its correct program-order position, before `S_BCCNT`/`LOGO` are
   cleared.

**Folding in "display" (`bzone_disp.c`) and "game" first half
(`bzone_game.c`)** turned up four more real bugs (found the same way -
`lockstep --frames N` after registering the module's routines, isolated
with `--mutation-check`):

5. **VOLCNO's ($58BA) two "uncontrolled" carry-ins, bzone_disp.c** - the
   owner's own NOTES_disp.md flagged these as an open question and asked
   integration to check empirically. Traced fully: VOLCNO is `JSR`'d from
   exactly one site, `bz_main()`'s own $5004 ("JSR VOLCNO", right after
   "L5000 LSR SYNC / L5002 BCC MAIN" falls through) - a `BCC` falls
   through only when carry is SET, so carry is proven = 1 on entry, not
   merely assumed as 0. The second `ADC` (`YSPD`) chains off the first
   ADC's own carry-out (nothing in between touches C). Went further than
   the owner's own question: the SAME `c` also has to persist ACROSS
   loop iterations (VOLCNO processes 5 rocks per call; `DEC`/`BNE`/`AND`/
   the loop's own `DEX`/`BPL` never touch C, so a LATER rock's own
   "start a new rock" ADC can inherit carry from an EARLIER rock's own
   `VOLCNO_31`/`VOLCNO_33` chain, which has its own genuine `CLC`s).
   Found in two passes: first fix (carry=1 for the first ADC, chained
   into the second, but re-declared `c` fresh inside the loop) still left
   6/99 VOLCNO checks failing on later rocks in the same call; moving `c`
   outside the loop (initialised once to 1, never force-reset before the
   first ADC) fixed all of them.
6. **Missing RAM store - `TEMP2` ($000A), MSGS' character loop
   (`mstart()`, part of `msgs()`), bzone_disp.c**. "`L6D05 STA TEMP2 /
   L6D07 AND #$7F`" was documented in the code's own inline comment but
   only the `AND` half was implemented (`code = ch & 0x7F`) - the raw,
   unmasked character byte was never actually stored into `TEMP2`. The
   later "`BIT TEMP2`" test was already correctly modelled as `ch & 0x80`
   (behaviourally equivalent for that one branch), which is exactly why
   this one was easy to miss - the branch worked, the RAM store just
   silently didn't happen. Found by lockstep: every non-tainted MSGS
   check failed at $000A. Fixed by adding the `TEMP2 = ch;` store.
7. **Missing RAM store - `TEMP1` ($0008), M_SET ($6310), bzone_game.c**.
   "`L6312 STX TEMP1 ;SAVE X`" (saving the entry X because the upcoming
   `JSR SIN` clobbers the real 6502 X register) was never translated -
   harmless for the C code's OWN correctness (a plain C parameter `x`
   survives the `sin_()` call just fine, no register-clobbering issue in
   C), but the RAM SIDE EFFECT the ROM performs was simply missing, so
   `g.ram[$08]` never changed while the ROM's own copy did. Found by
   lockstep: every M_SET/M_FOR/MOTION check failed at $0008 (M_FOR/MOTION
   both call M_SET, so the same missing store surfaced under three
   different routine names). Fixed by adding `TEMP1 = x;` right after
   entry, matching the `STX TEMP1`.
8. **Check-scoping hazard - D_NEXT ($5D41), bzone_disp.c/tests\lockstep.c**
   (an integration-side design issue this time, not a module bug - see
   "Lockstep machinery built" above for the general pattern, same family
   as bz_main()/b_end()). D_NEXT is DRAW's own loop back-edge, reached
   only by "`JMP D_NEXT`" from inside `draw()`'s own dispatch; `draw()`
   already calls `d_next()` correctly as a plain nested C call within its
   OWN check. Because D_NEXT was ALSO registered by address (the display
   module exposed it "for lockstep coverage" per its own header comment),
   the ROM's "JMP D_NEXT" auto-opened a SECOND, wrongly-scoped check at
   the same stack depth as `draw()`'s own - one that closes at the SAME
   RTS `draw()`'s own check does, but whose adapter only runs the TBLPTR
   increment, not the rest of `draw()`'s remaining loop iterations. Every
   D_NEXT check failed (sometimes 30+ bytes) purely from this scoping
   mismatch, not a real translation bug. Fixed the same way bz_main()/
   b_end() are handled: excluded D_NEXT from `addr_routine[]` (still
   listed in `routines[]`/`--list-untranslated` by address, just never
   auto-opened by the JSR/JMP mechanism).
9. **CRITICAL - crash: WNSHLD/CKSCOR/HISCRE/DISTBL replay recurses
   forever, bzone_disp.c/tests\lockstep.c.** See the RESUME section at
   the top of this file ("CRITICAL fix this session") for the full
   account - short version: these four routines' own C bodies each end
   with a real, unconditional call into `b_end()`/`bz_main()`, which
   call each other forever by design (the ROM's own endless frame loop).
   Once ANY of these four opened its own registered check and that
   check's replay actually ran the real function, the process stack-
   overflowed - only reachable once a real game gets far enough for the
   player's tank to take a hit (`CRACK != 0`), never in attract-only
   testing (`--frames 5000` attract never hit this; `coin_start --frames
   500`+ crashed 100% reproducibly, bisected to exactly frame 454).
   Fixed by excluding all four from `addr_routine[]` (same mechanism as
   bz_main()/b_end()/D_NEXT/NMI) - they keep their `routines[]` entry
   (so `--list-untranslated` still lists them by address) but are never
   auto-opened, so lockstep has NO coverage of these four routines until
   "game" restructures `bz_main()`/`b_end()` to return after one pass
   (open item #1, RESUME section).
10. **Inverted branch - BATTLE ($5268), "THE ULTIMATE KLUDGE" at
   L52D0-L52E7 (`battle_10()`), bzone_disp.c** (open item #2 in the
   RESUME section, `task_1665c325`). "`L52D0 LDA LOGOZ+1 / L52D3 BPL
   BATTLE_90`" branches to BATTLE_90 (`CPY #$05` - draw all of LOGOBJ,
   "BATTLE" plus "ZONE") when LOGOZ+1 is POSITIVE, the `CMP #$FD`/`LDA
   LOGOZ`/`CMP #$B0`/`CPY #$03` chain ("DON'T DRAW "ZONE" UNTIL WE HAVE
   ENOUGH TIME") being the fall-through, reached only while LOGOZ+1 is
   NEGATIVE; the C code had `if (!(a & 0x80)) { ...chain... }` -
   backwards (same family as item 3's `BIT LOGO / BPL ROTATE_33`). With
   the sense flipped the nested `a < 0xFD` was always true on the C side,
   so the two loop limits simply swapped: early in the sequence (BATINT's
   own LOGOZ = $FC00 up to $FCAF) the ROM stops at Y=4 and writes its $FF
   terminator into the third slot while the C went round once more and
   wrote a whole extra pseudo-object there; later (LOGOZ >= $FCB0, then
   $FDxx up and positive) the ROM writes the third slot and the C stopped
   short. Found by lockstep (`--frames 5000`, attract): 536/3847 BATTLE
   checks failing, in exactly those two shapes - first "7 byte(s)" at
   `$020E/$020F`, `$0246/$0247`, `$027E/$027F` (+ the C's stray
   terminator one slot further on), C=`LOGOX`/`LOGOX+1`/`$00`/`$00`/
   `LOGOBJ+4`/`TANGLE` against the ROM's stale bytes and `$FF`; then the
   mirror image, "`$020E C=00 ROM=40 $027E C=FF ROM=1F`". Both sides
   start from the same RAM snapshot, so the slot search (`BATTLE_15`) and
   every stored value were already right - only the COUNT of slots
   differed, which pointed straight at the loop's exit test; read against
   the listing instruction by instruction, L52D3 was the only branch
   whose direction disagreed (L5290's `BMI BATTLE_13`, the other sign
   test on the same cell, is correct). Fixed by testing `a & 0x80`:
   BATTLE 0/3847 failing in attract, run total 573 -> 37 (the outstanding
   VOLCNO 18 / DRADAR 19 only); `coin_start --frames 3000` BATTLE 132 ->
   0/2743, run total 224 -> 92 (MOTION 2, VOLCNO 10, DRADAR 80).
11. **Missed carry source - VOLCNO ($58BA), bzone_disp.c** (the residual
   after #5). For a falling rock with `YPOSH` negative, `L5914 LDA YPOSL,X
   / L5917 CMP #$A2 / L5919 BCS VOLCNO_33` - the CMP overwrites C on both
   exits: C=1 into `L5923 ADC YPOSH,X` when it branches (the C code
   carried the `ADC YPOSL` carry-out through instead), and C=0 when the
   rock lands and goes back through VOLCNO_12 (the C left the stale
   carry, which #5's cross-iteration persistence then fed into a later
   rock's spawn `ADC #$01`). Symptom: `$0354`-area speed/position bytes
   off by 1, 18/4999 attract, 10/2999 coin_start. Fixed with `c =
   (YPOSL_AT(x) >= 0xA2)` before the branch. VOLCNO 0 failed.
12. **Harness: `$6B79` dummy read logged when it hit I/O, tests\lockstep.c**
   (DRADAR open item; `dradar()` itself was already correct). `CMP
   ($00,X)` is a "waste time" read whose pointer comes from zero page at
   whatever X TRACK left. `mem_read_io()` excluded it only AFTER the
   mapped-I/O decode, so when the pointer landed on IN0/Mathbox/POKEY the
   read was logged: one extra ROM-only event right before `L6B7B LDA
   MOUTLO`, desyncing the replay ("C read $1810, ROM read $0800 (I/O
   #13)") and cascading into `$02E8`/vector-RAM diffs. The check now runs
   first. None of those reads has a side effect. DRADAR 0 failed.
13. **Missing RAM store - `SAVE1` ($0013), M_NEX0 ($6294, part of MOTION),
   bzone_game.c**. `L62C0 STX SAVE1` (saving the tank index across `JSR
   COSXTR`) was folded into a comment; the C keeps `x` in a parameter so
   its own logic was fine, but the RAM effect was missing. Reached only
   when the player fires a shell, hence coin_start only. Fixed with
   `SAVE1 = x;`. MOTION 0 failed.
14. **Inverted compare - WNSHLD ($51E9), bzone_disp.c** (found the moment
   WNSHLD was covered again). `L5205 LDA #$2C / L5207 CMP CRACK / L5209
   BCS WNSHLD_20` keeps growing the crack while `$2C >= CRACK`; the C had
   `if (a <= CRACK) break;`, so once the picture was full (CRACK >= $12)
   it took the respawn/game-over path immediately instead of letting the
   crack grow to $2E first. Fixed to `a >= CRACK`. Proven by mutation:
   putting the old test back makes WNSHLD fail at CRACK = $12, $14, ...
15. **Harness: tail-chained checks and replay runaway, tests\lockstep.c**.
   (a) With the four trampoline routines auto-opened, `L521F JMP CKSCOR`
   would have left WNSHLD's check open across all of CKSCOR while
   `wnshld_body()` stops at `return PASS_SIG_CKSCOR`; a JMP from one
   `is_tail_chained()` routine into another at the same SP now closes the
   first check. (b) The #14 mutation hung lockstep instead of failing:
   after a desync `replay_read()` returns 0 forever and WNSHLD_15's
   PRAND/OBJOBJ retry loop never exits. `runaway_tick()` now longjmps out
   of the C call 100,000 hw.h calls after the first I/O error, and the
   check is reported as the FAIL it is ("C call abandoned").
17. **Missed branch target - MAIN ($5000), MAIN_50, bzone_game.c** (found by
   hand review against the listing - MAIN/B_END themselves have no
   lockstep coverage until the pass probe works). `L5102 LDA COLFLG+2 /
   L5104 BNE MAIN_53` skips MAIN_51 (the flashing "ENEMY TO LEFT/RIGHT/
   REAR" message) while the enemy is exploding; the C skipped only the
   TRAP2 block and then fell into MAIN_51 anyway. Fixed with `goto
   MAIN_53`. The rest of `bz_main_body()`/`b_end_body()` was re-checked
   line by line against `$5000-$51D7` in the same pass; no other
   difference.
16. **Harness: pass probe contract, tests\lockstep.c** - see the RESUME
   section: `main_pass()` instead of `bz_main()`, and the whole-pass I/O
   slice is no longer recycled mid-pass under `--pass-probe`. Still fails
   pending NMI placement.

**Integration-side bug also found and fixed** (not a module bug - see
"Lockstep machinery built" above for the full account): `thunk_read()`'s
first cut mis-classified program ROM ($5000-$7FFF) as a hardware range,
so a thunk's own opcode fetches inside that range (e.g. `tr7chk()`
fetching its own first byte at $69B5) were routed through `replay_read()`
- visible as `"ROM thunk $69B5 did not return"` (the 5,000,000-iteration
runaway guard tripping, since the thunk was consuming real I/O events and
never reaching its own real code) and a bogus `"C read $69B5"` in the
failure report. Fixed by matching the ROM-side oracle's own
`mem_read_io()`/`mem_write()` address list exactly (`is_hw_addr()`)
instead of a broad range guess.

## NMI-as-its-own-check design (implemented; kept as the design record)

**Status: implemented and in production** (see "Lockstep machinery built"
above) - this section is kept as the design rationale, not a TODO.

Studied `C:\Source2026\Tempest Dissasembly\c_src\tests\lockstep.c` (the
M9/final version - much bigger than what Battlezone needs right now, but
the base mechanism for "the interrupt handler is itself a checked
routine" is unchanged since Tempest's M1/M2, per its PLAN.md section 4's
M2 row: "IRQ fully native (no thunks)", checked the same way as any other
routine, well before the M3+ "IRQ placement"/"IRQ moved" machinery this
milestone's brief does NOT ask for yet). The piece Battlezone's NMI needs
(matching Tempest's early-milestone bar, not its final one):

- A routine table entry keyed by address (Tempest: `{"IRQ", 0xD704,
  run_irq, ...}` - ALEXEC's IRQ entry). Battlezone's equivalent address is
  NMI $558F (`nmi()`'s entry, per `NOTES_nmi_sound_coin.md`).
- Unlike a JSR/JMP-opened check, this one opens when the HARNESS's own
  oracle side takes the NMI (`cpu_nmi(&c)` in the ROM-side machine, not a
  JSR/JMP landing on the registered address) - `tests\lockstep.c`'s
  current `open_check`/main loop only watches JSR/JMP; NMI entry needs its
  own explicit open call right where `cyc += 7; n_nmi_taken++;` already
  happens.
- Closes at RTI (opcode `$40`), not RTS - Tempest: `if (last_op == 0x40 &&
  c->s == irq_s) in_irq = 0;` - matching SP, not "SP rose above entry".
- Compared the same way as any other routine check (RAM/vector-RAM diff,
  I/O log replay) - no taint logic needed for the NMI's own check (it IS
  the interrupt), but every OTHER check still open when an NMI lands stays
  tainted exactly as `tests\lockstep.c` already does.
- Battlezone's own milestone brief (PLAN.md M2: "NMI fully native;
  coin_start scenario 0 failed") matches Tempest's M2 bar exactly, not
  M3's IRQ-placement device or M4's "IRQ moved" comparison - those are not
  needed for M2 here, and NOTES_m1.md already recorded that the fuller
  device is deferred pending a module that always runs under an NMI
  (BZSOUN/COIN65, i.e. exactly this module).
- **Implemented** in `tests\lockstep.c` once nmi-sound-coin handed over -
  see "Lockstep machinery built" above for the exact mechanism and the
  final numbers (NMI: 83,168 checked / 83,168 passed / 0 failed / 0
  tainted in attract; 48,832 / 48,832 / 0 / 0 in coin_start - NMI's own
  check is never itself tainted, by construction, since it's the
  interrupting event, not an interrupted one).
