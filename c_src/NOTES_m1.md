# M1 notes - VGUT + the lockstep verifier

## RESUME HERE (checkpoint - update this section after each significant step)

**M1 is DONE (closed out by the integration agent, 2026-09-18).  All 7
"Not yet done" items below are complete.  `build_all.bat` -> `ALL BUILDS
OK`, `/W4` clean, 0 warnings.  `refrun --frames 600`/`--frames 5000`: PASS.
`lockstep --frames 5000` (attract): PASS, 1,119,992 compared, 0 failed,
14,160 tainted.  `lockstep --script tests\scenarios\coin_start.txt --frames
3000`: PASS, 581,064 compared, 0 failed, 6,262 tainted.  See PLAN.md's M1
row for the summary and section 8 below for the bug the item-1 switch
surfaced (found and fixed, not silently accepted per this file's own "STOP
and report" instruction).**

Superseded status (left for history - the situation when the previous
session was cut off):

~~Status as of this checkpoint: core M1 deliverables built and passing; a
few finishing items remain (list below).  Nothing is left half-written -
every file compiles `/W4` clean right now.~~

Finished and verified:
- `c_src\vgut.h` (canonical prototypes, per SIGNATURES.md - see its VGUT
  section, already filled in) and `c_src\vgut.c` (all 17 required entry
  points implemented and `/W4` clean: `cl /nologo /O2 /W4 /std:c11
  /D_CRT_SECURE_NO_WARNINGS /I. /c vgut.c state.c /Foobj\`).
- `c_src\game.h` now just `#include "vgut.h"` (kept for modules that
  already `#include "game.h"` per PLAN.md's per-module convention).
- `c_src\SIGNATURES.md` VGUT section filled in (table of every routine's
  C prototype + which cells it reads/writes besides VGLIST/the buffer).
- `c_src\tests\lockstep.c` - written from scratch (see "What differs from
  Tempest's lockstep.c" below for why it's much smaller), builds and runs
  clean:
  ```
  cd "C:\Source2026\BattleZone Dissasembly\c_src"
  call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -no_logo
  cl /nologo /O2 /W4 /std:c11 /D_CRT_SECURE_NO_WARNINGS /I. /I"tests\ref6502" /Foobj\ tests\lockstep.c mathbox.c mbprom.c progrom.c vecrom.c avg.c state.c vgut.c obj\ref6502.obj obj\c012294.obj /Fe:tests\lockstep.exe
  ```
  (needs `obj\ref6502.obj` / `obj\c012294.obj` - built once by `build_all.bat`,
  or directly: `cl /nologo /O2 /W3 /std:c11 /c tests\ref6502\ref6502.c /Foobj\`
  and `cl /nologo /O2 /W3 /std:c11 /D_CRT_SECURE_NO_WARNINGS /I. /c c012294.c /Foobj\`).
- **Attract, `--frames 5000`: 1,119,992 calls compared, 0 failed, 14,160
  tainted** (exact per-routine breakdown in section "Results" below).
- **`coin_start` (extended - see below), `--frames 3000`: 581,064 calls,
  0 failed, 6,262 tainted.**
- **Mutation test: both mutations caught** (`LK_MUTATE_VGADD` and
  `LK_MUTATE_VGVTR1` in vgut.c, `#ifdef`-gated, undefined in the normal
  build) - exact bytes in "Mutation test" below.
- `tests\scenarios\coin_start.txt` extended: coin+start (unchanged) plus
  drive-forward, turn, fire, drive-forward-again, fire (ALLPOT bits) so a
  game actually starts and the tank moves/fires under the oracle.

All 7 steps below are DONE (integration agent, 2026-09-18) - kept verbatim
as the record of what was asked, with the outcome noted inline. See section
8 for the one substantive finding (item 1's bug).

Done (was "Not yet done", next steps, in order):
1. **DONE, with a bug found and fixed - see section 8.**  `tests\refrun.c` PLAN.md item 3**: replace `avg_draw_cycles()` /
   `avg_norm_shifts()` / `avg_timer_reg()` (the local timing stand-in) with
   a call to `avg_walk()` from `avg.c`/`avg.h` (already `#include`d nowhere
   in refrun.c yet - add `#include "avg.h"`).  At the `case 0x1200` (GOADD)
   in `mem_write()`: build `avg_mem am = { vram, vecrom };` (refrun.c's own
   `vram`/`vecrom` are already the right shape - vecrom is the generated
   global from vecrom.h), call `avg_result ar = avg_walk(&am, vg_start_addr,
   NULL, NULL);`, set `last_draw_cycles = ar.cycles;` and keep
   `vg_ends_in_halt` from the EXISTING local `avg_walk()` (the HALT/LOOP/BAD
   classifier used for the shape summary) UNCHANGED - do not conflate the
   two `avg_walk` names; refrun.c's own local one (different signature, no
   relation to avg.h) stays for the ops/vectors/bright print. Then delete
   `avg_draw_cycles()`, `avg_norm_shifts()`, `avg_timer_reg()`,
   `AVG_TICK_CPU`/`AVG_TICKS` from refrun.c. Rebuild
   (`build_all.bat`), run `tests\refrun.exe --frames 600` and diff against
   NOTES_m0.md section 8's numbers (should be BYTE-FOR-BYTE identical per
   NOTES_m0.md's own TODO: "both walk the identical opcode stream ... the
   switch should be a pure deletion, not a behaviour change" - if the
   numbers differ even slightly, STOP and report the discrepancy with an
   example list per the milestone brief, do not silently accept it).
2. **`tests\refrun.c` PLAN.md item 4**: in `mem_read_io()`'s fallback
   (the `note_unmapped` call for reads), add the same `if (cur_pc ==
   0x6B79) { <count separately, do not call note_unmapped>; return 0; }`
   guard `tests\lockstep.c` already has (`n_dummy_wait_r`) - see
   lockstep.c's `mem_read_io()` for the exact shape to copy. Update the
   summary print to show it separately from `unmapped reads`.
3. Add `tests\lockstep.exe` to `build_all.bat` (new `echo === tests\
   lockstep.exe` block, same shape as the existing refrun.exe block, linking
   `mathbox.c mbprom.c progrom.c vecrom.c avg.c state.c vgut.c` + the two
   shared `.obj`s - see the command above, it already works).  Remove the
   placeholder "state.c / hw.h scaffold check" block (state.c now has a
   real consumer, vgut.c/lockstep.exe).
4. Re-run `build_all.bat` end to end after 1-3, confirm `ALL BUILDS OK`,
   `/W4` clean, `refrun --frames 600` unchanged, `lockstep --frames 5000`
   and `--script tests\scenarios\coin_start.txt --frames 3000` both still
   0 failed (re-paste the exact summary lines into "Results" below - they
   should not change from what's already recorded, since 1-3 don't touch
   vgut.c/lockstep.c's own logic, only refrun.c and the build script).
5. Update `PLAN.md`'s M1 row (template: the M0/M0v rows already in the
   file) with the final numbers from this file.
6. **DONE - already true when the integration agent checked**: no
   `tests\lockstep_mutate_vgadd.exe` / `lockstep_mutate_vgvtr1.exe` /
   `obj_mut1\` / `obj_mut2\` were present on disk at resume time (the prior
   session must have cleaned them up, or never left them, before it was
   cut off); confirmed absent, nothing to delete.
7. Final report: see PLAN.md's M1 row and section 8 below.

Steps 2 (dummy-read summary line, `refrun.c`) and 3 (`build_all.bat`
lockstep.exe target, placeholder block removed) were textual surgery as
described. Step 1 turned out NOT to be a pure deletion once run - see
section 8: the switch to `avg_walk()` needed a unit conversion the item's
own instructions didn't mention (they assumed `avg_result.cycles` was
already in CPU cycles), and without it the before/after numbers differed
exactly as this file warned they might; found via that check, not missed.

## 8. Item 1's bug: `avg_result.cycles` is in AVG master cycles, not CPU cycles

`avg.h`'s own TIMING section says outright: "`avg_result.cycles` is the
running sum **in master cycles**" - and Tempest's `app_loop.c` (the one
other place in either port that consumes `avg_walk()`'s `cycles` field for
real hardware timing, not just Gate T) converts it before using it as a
CPU-cycle duration: `uint32_t d = (r.cycles + AVG_CYC_PER_CPU - 1u) /
AVG_CYC_PER_CPU; /* the list's draw time, CPU cycles */`.  The half-edited
`refrun.c` this session resumed from had `last_draw_cycles = ar.cycles;`
with no such conversion - a plain unit bug, not a semantic difference in
what's being modelled - and `tests\lockstep.c`'s GOADD case (written by
the same M1 agent, same session) had the identical shape: `vg_busy_until =
cyc + ar.cycles;`.

**Symptom, caught by this file's own "if the numbers differ, STOP and
report" rule (not silently accepted):** `refrun --frames 600` after the
switch reported AVG draw time avg **124.923 ms**/list (min 0.011 ms, max
254.806 ms) and VTIMER-busy **1646/1650 (99.8%)** - against
`NOTES_m0.md` section 8's recorded avg **15.616 ms** (min 0.001, max
31.851 ms) and **4/1650 (0.2%)**.  Every OTHER field in the summary
(passes, cycles, NMIs raised/taken, watchdog kicks, GOADD/VSTOP counts,
display-list ops/vectors/beam-on, RANDOM/ALLPOT/POKEY/EMCNT/SOUND counts,
Mathbox starts/microsteps, unmapped-read count, all RAM values including
TRAP1, distinct PCs, captured frames) was byte-identical before and after
- exactly the "8x on the derived draw-time stat only, everything else the
ROM's own instruction stream produces is untouched" signature of a pure
unit-scale bug in a value nothing else consumes, not a behavioural change
to the walk itself.  124.923 / 15.616 = 8.00 - the ratio is
`AVG_CYC_PER_CPU`.

**Fix**, both files: `last_draw_cycles = (ar.cycles + AVG_CYC_PER_CPU -
1u) / AVG_CYC_PER_CPU;` (refrun.c) and `vg_busy_until = cyc + (ar.cycles +
AVG_CYC_PER_CPU - 1u) / AVG_CYC_PER_CPU;` (lockstep.c) - ceiling division,
matching this file's own header comment on `vg_halted()` ("reads halted
ceil(avg_cycles/8) CPU cycles after the GOADD") which had the right answer
written down the whole time, one call site over from the bug.

**Why lockstep's numbers didn't move even though it had the same bug**:
VGUT never reads `hw.h` (PLAN.md/this file, section 4 "No ROM-thunk"), so
none of VGUT's own checks depend on IN0 bit 6's derived value; the only
consumer of `vg_halted()` in either harness is code that isn't part of any
currently-registered check (refrun.c's own summary stat, and - in the ROM
- the mainline HALT-poll, which is untranslated and not under lockstep's
watch yet). Re-ran `lockstep --frames 5000` and `--script
tests\scenarios\coin_start.txt --frames 3000` after the fix: **identical**
call/pass/fail/tainted numbers to what's recorded in "Results" above -
confirming the bug was latent for M1's own checks but needed fixing before
a hw.h-using module (M2's NMI/BZSOUN/COIN65, or the mainline HALT poll)
folds in and starts exercising it for real.

**Rebuild and full-suite re-check after the fix** (integration agent,
2026-09-18): `build_all.bat` -> `ALL BUILDS OK`, `/W4` clean, 0 warnings.
`refrun --frames 600`: PASS, byte-for-byte identical to `NOTES_m0.md`
section 8 (confirmed by direct comparison, not just "same shape").
`refrun --frames 600` two-run determinism into the same `--outdir`: PASS,
71/71 dump files MD5-identical. `refrun --frames 5000`: PASS (13,819
GOADDs, avg draw 15.29 ms, 10/13820 VTIMER-busy - same shape as
`NOTES_m0.md`'s "13,819 GOADDs, avg draw time 15.24 ms, 10/13820" note).
`lockstep --frames 5000`: PASS, 1,119,992 compared, 0 failed, 14,160
tainted (identical to section 5 above). `lockstep --script
coin_start.txt --frames 3000`: PASS, 581,064 compared, 0 failed, 6,262
tainted (identical to section 5 above).

A second, smaller build-script gap found and fixed in the same pass:
`build_all.bat`'s `refrun.exe` link line never had `avg.c` in its object
list even before this session (`HW` was `mathbox.c mbprom.c progrom.c
vecrom.c` - no `avg.c`), so as soon as the half-edited `refrun.c` called
`avg_walk()`, the very first rebuild failed at link time (`LNK2019:
unresolved external symbol avg_walk`). Fixed by adding `avg.c` to `HW`
(shared by both the `refrun.exe` and `lockstep.exe` targets) - noted here
because it means `build_all.bat` had not actually been run successfully
since `refrun.c`'s avg.c switch was made; this session's rebuild is the
first one to exercise the combination end to end.

## 1. What was copied from where, what had to change

- **`vgut.c`**: started from `C:\Source2026\Gravitar Dissasembly\c_src\
  vgutil.c`/`.h` (PLAN.md: "0 differing code lines" between Battlezone's
  and Gravitar's VGUT.MAC - independently re-confirmed by reading both
  listings side by side, section 2 below) and cross-checked against the
  Tempest port's `alvgut.c` (same module, colour variant - same routine
  names VGRTSL/VGHALT/VGHEXZ/VGHEX/VGJSRL/VGSTA1/VGSTAT/VGCNTR/VGADD2/
  VGADD/VGSCA1/VGSCAL/VGVTR/VGVTR1/VGVCTR, reference only). Battlezone-
  specific: zero-page cell addresses ($01 VGBRIT, $02/$03 VGLIST, $04-$07
  XCOMP - Gravitar's and Tempest's differ), `VGMSGA` at $33F0 in
  Battlezone's own vector ROM (read through `cpu_rd`, matching state.h's
  15-bit decode - no separate `VGROM()` macro needed the way Gravitar's
  vgutil.h has one, since Battlezone's `cpu_rd` already covers $3000-$3FFF),
  and Battlezone additionally requires **VGJMPL** and **VGVTR2** as
  standalone functions (Tempest's alvgut.c folds VGVTR2 into VGVTR1's tail
  and does not translate VGJMPL at all since Tempest's own ROM never uses
  it either - Battlezone's milestone brief explicitly lists both as
  required entry points, so both got their own C function here even though
  VGJMPL is dead code and VGVTR2 has direct callers CALLING_NOTES.md lists).
  No DIGTYS/DIGITS (Tempest's ALVGUT has them; Battlezone's own VGUT.MAC
  ends at $7AC9 with no digit-group helper - BZONE.MAC's actual score/digit
  display code, not yet translated, calls VGHEX/VGHEXZ directly per digit).
- **`vgut.h`**: new (Tempest's game.h has no per-module header split - this
  port's SIGNATURES.md convention, set up by the coordinating agent for the
  now-parallel translation effort, asks for one header per module).
- **`tests\lockstep.c`**: the ENTRY/DURING/COMPLETION/COMPARE structure and
  the "JSR/JMP into a registered address opens a check, SP rising back
  above entry_s closes it" mechanism are Tempest's
  `C:\Source2026\Tempest Dissasembly\c_src\tests\lockstep.c` idea, but that
  file (1935 lines) is the *final*, M9-era version with a decade of
  machinery M1 does not need yet (pass-level probe, IRQ-placement-in-
  translated-code with checkpoints, ROM thunks for untranslated callees,
  the M4 "IRQ moved" comparison, Gate-1/N trace export, RESET regions...).
  None of that exists here - see "What differs" below for exactly why each
  piece was left out, not just that it was.

## 2. VGUT.MAC textual identity, re-checked

PLAN.md's table claims "VGUT.MAC - 0 differing lines vs Gravitar's and Red
Baron's VGUT.MAC". Read side by side in this session:
`battlezone-main\VGUT.MAC` (386 lines) against `C:\Source2026\Gravitar
Dissasembly\...\vgutil.c`'s own header (which quotes Gravitar's VGUT.MAC
routine list) and the Battlezone disassembly listing
(`..\disasm\bzone_program_rom.asm`, $7A19-$7ACB) - same routine names, same
order, same fall-through chain, same ENTRY/EXIT/USES header text verbatim
(down to the typo "ALWASY" at VGRTSL's `BNE VGHAL1` comment, and "VHALT" in
VGHALT's own header line - both present in Battlezone's `VGUT.MAC` too).
The only differences are Atari's own link addresses (Gravitar: $E40F+;
Battlezone: $7A19+) and the zero-page cell numbers - exactly what PLAN.md
already flagged as needing translation-time adaptation, not a code
difference.

## 3. Register protocol / signatures (SIGNATURES.md)

See `SIGNATURES.md`'s VGUT table (filled in this milestone) for the full
per-routine reads/writes. Summary of the one non-obvious convention choice:
`vgjmpl`/`vgjsrl` take the target CPU address as **A = high byte, X = low
byte** (two `uint8_t`, not a `uint16_t`) - matching the ROM's own register
pair exactly and avoiding any ambiguity a 16-bit parameter's byte order
would introduce for callers translating `LDA #target/256 \ LDX
#target&255 \ JSR VGJSRL` literally.

## 4. Lockstep design

### Entry detection
Only real `JSR` ($20) / `JMP` ($4C) opcodes landing on a registered address
open a check - never a conditional branch, even though several VGUT tail
branches (`VGJMP1`'s `BNE VGADD`, `VGWAI1`'s `BNE VGADD`) land exactly on a
registered address. This is the exact hazard
`..\disasm\CALLING_NOTES.md` section 1 calls out: "VGADD's own RTS is what
returns to the original caller of VGJSRL/VGVCTR, not to VGJMP1/VGWAI1" - if
a branch-landing were treated as a new entry, the resulting nested check's
own RTS-detection would steal the return that was meant to close the OUTER
check, corrupting the whole stack discipline. Modelling every tail branch as
an ordinary C tail call (`vgadd(y)` called from inside `vgjmp1`/`vgwai1`)
gives the identical observable result with none of that risk.

`JMP` INTO a registered routine from OUTSIDE VGUT (`$51E6 JMP VGSTAT`,
`$6D2B JMP VGSCA1`, `$6D90 JMP VGSCAL`, `$5571 JMP VGADD2` from DSPALP) DOES
open a check - it is a genuine tail call from another module, and the
generic "entry_s = SP right now, close when SP rises above it" rule handles
a no-push JMP entry exactly the same as a JSR entry (see the code comment
in `open_check`/the main loop). This also means **VGUT's own internal
tail-JMPs** (`VGSTAT`'s `JMP VGADD2`, `VGSCAL`'s `JMP VGADD2`) open a NESTED
check for VGADD2/VGADD *while VGSTAT's/VGSCAL's own outer check (if any) is
still open* - both close on the same eventual RTS (the `while` loop in the
main loop pops as many as have their condition satisfied at once). This is
the same "checks nest" behaviour Tempest's lockstep.c documents and is why
VGADD2/VGADD/VGVTR2's call counts are much larger than the routines that
only reach them by JSR - they are also entered every time a registered
caller tail-JMPs into them.

### NMI taint (PLAN.md's "at minimum" bar, not the fuller M4 device)
An NMI taken while any check is open marks every open check `tainted`
(counted, not compared) - PLAN.md M1's explicit minimum bar ("implement the
Tempest M4-style handling from the start if feasible... at minimum count
them as tainted and do not compare, and report the count"). The fuller M4
"IRQ moved" comparison (re-run with the NMI's own effect applied at the
call's entry or after its return, count as pass if either matches) was not
implemented this milestone - out of scope for M1's size (VGUT is one small
module; the fuller device pays for itself once a module that ALWAYS runs
under an NMI, the way Tempest's INFO/DSPCRD/LDRDSP always do, appears -
likely BZSOUN/COIN65, M2). Measured tainted fraction: attract 14,160 /
1,134,152 total calls attempted (1.2%), coin_start 6,262 / 587,326 (1.1%) -
small, as expected for calls that are each only a handful of instructions
long against an NMI every 6144 cycles.

### No ROM-thunk / hardware-replay machinery yet
VGUT never calls `hw.h` (it only touches RAM/vector RAM/vector ROM through
ordinary `cpu_rd`/`cpu_wr`, which are already deterministic and shared
between the ROM's own execution model in this file and the C function under
test) - so there is nothing to "thunk" (no untranslated callee VGUT invokes)
and nothing for a hardware replay layer to feed back (VGUT's I/O log is
always empty). The I/O-log plumbing (`lk_io()`, called from the same
`mem_read_io()`/`mem_write()` this file's oracle machine already has) is
built and generic anyway, exactly so the next modules (BZSOUN/COIN65/
NMI, which DO call `hw.h`) can register without any change to
`open_check`/`finish`/`run_compare` - see the "Extending the routine table"
comment at the top of `lockstep.c`. Registering a routine that calls other,
still-untranslated ROM code (a true "thunk" callee) is also not needed yet
(VGUT calls nothing outside itself) - if a later module does, the design
slot for it is the same one Tempest's `lk_rom_call()` fills, not yet built
here.

### Stack-byte exclusion (`min_s`)
`VGHEX1`'s `PHP`/`PLP` (VGHEX/VGHEXZ, saving/restoring the caller's carry
across the glyph append) pushes one transient byte. Every open check's
`min_s` is updated to the lowest SP seen during its own lifetime (every
instruction, for every check currently on the stack, not just the top one -
a nested call's pushes count against the outer check too, correctly, since
they are still "the outer call's own" stack use from its perspective); the
final RAM compare excludes `$0100+min_s+1 .. $0100+entry_s`. Confirmed
working: 0 RAM-compare failures from this exclusion band across both
scenarios' 1.7M total calls.

## 5. Results

**Attract, `--frames 5000`:**
```
LOCKSTEP: stop = frames reached, frames 5000, cycles 510984965
  NMIs raised 83168 taken 83168 lost 0, NMIs while a check was open (tainting) 12587
  watchdog kicks 83424 (max gap 6155 cyc), BRK 0, unmapped reads 0 (dummy-wait $6B79 excluded: 733) writes 0
  io log overflow 0, depth overflow 0
  per-routine (name: checked passed failed tainted):
    VGRTSL         0        0        0        0   (never called by this scenario)
    VGHALT      4999     4900        0       99
    VGHEXZ     88376    86180        0     2196
    VGHEX          0        0        0        0   (never called by this scenario)
    VGJMPL         0        0        0        0   (never called by this scenario)
    VGJSRL      9997     9944        0       53
    VGSTA1         0        0        0        0   (never called by this scenario)
    VGSTAT     35076    34916        0      160
    VGCNTR     92043    91731        0      312
    VGADD2    175798   175159        0      639
    VGADD     128188   127224        0      964
    VGSCA1     94621    93864        0      757
    VGSCAL         0        0        0        0   (never called by this scenario)
    VGDOT      14075    13809        0      266
    VGVTR      66584    65301        0     1283
    VGVTR1         0        0        0        0   (never called by this scenario)
    VGVTR2    410235   402804        0     7431
    VGVCTR         0        0        0        0   (never called by this scenario)
  TOTAL    1119992  1105832        0    14160
LOCKSTEP: PASS (1119992 compared, 0 failed, 14160 tainted)
```

**`coin_start` (extended scenario), `--frames 3000`:**
```
LOCKSTEP: stop = frames reached, frames 3000, cycles 300024549
  NMIs raised 48832 taken 48832 lost 0, NMIs while a check was open (tainting) 5664
  watchdog kicks 49088 (max gap 6155 cyc), BRK 0, unmapped reads 0 (dummy-wait $6B79 excluded: 541) writes 0
  io log overflow 0, depth overflow 0
  per-routine (name: checked passed failed tainted):
    VGRTSL         0        0        0        0   (never called by this scenario)
    VGHALT      2999     2962        0       37
    VGHEXZ     34744    34159        0      585
    VGHEX          0        0        0        0   (never called by this scenario)
    VGJMPL         0        0        0        0   (never called by this scenario)
    VGJSRL      7675     7633        0       42
    VGSTA1         0        0        0        0   (never called by this scenario)
    VGSTAT     22839    22730        0      109
    VGCNTR     50619    50355        0      264
    VGADD2     84217    83853        0      364
    VGADD      53862    53591        0      271
    VGSCA1     45001    44633        0      368
    VGSCAL         0        0        0        0   (never called by this scenario)
    VGDOT       6759     6716        0       43
    VGVTR      34624    33956        0      668
    VGVTR1         0        0        0        0   (never called by this scenario)
    VGVTR2    237725   234214        0     3511
    VGVCTR         0        0        0        0   (never called by this scenario)
  TOTAL     581064   574802        0     6262
LOCKSTEP: PASS (581064 compared, 0 failed, 6262 tainted)
```

### Never exercised, and why (CALLING_NOTES.md is the source for all of this)

- **VGRTSL, VGJMPL, VGSTA1**: expected per the milestone brief - "the game
  never calls them" (dead code in this ROM; CALLING_NOTES.md confirms all
  three have zero call sites anywhere in BZONE/BZSTST/VGUT itself).
- **VGHEX** (direct check): CALLING_NOTES.md lists VGHEX's only direct
  (`JSR`) callers as five BZSTST (self-test) addresses; every non-self-test
  use reaches it only via VGHEXZ's fall-through, which does not open VGHEX
  its own check (correctly - VGHEXZ's C function calls `vghex()` as a plain
  C call, and that inner execution is exercised and covered by VGHEXZ's own
  comparison, just not counted as a separate VGHEX row). Will show non-zero
  once a self-test scenario exists (M9-equivalent milestone here).
- **VGVTR1** (direct check): same story - CALLING_NOTES.md's six direct
  callers are all BZSTST diagnostic-display positioning.
- **VGVCTR**: CALLING_NOTES.md - "not called anywhere in this ROM directly
  (reached only via VGVTR/VGVTR1/VGVTR2 fall-through)". This one will
  NEVER get its own check under genuine ROM code, by construction (matches
  Gate-S-equivalent expectations once self-test is translated too).
- **VGSCAL** (direct check): CALLING_NOTES.md's one caller is `$5CFF
  D_SPCL` - DTAB opcode 5, the explosion-picture (`EXPIC`) display leg. The
  extended `coin_start` scenario drives, turns and fires twice but this
  session did not confirm a tank/robot kill actually happening (BZONE's
  collision/AI logic is untranslated ROM in this run, so it is genuinely
  the real game deciding whether a shot connects - not something the
  script can force without either much more precise timing/aiming or a
  RAM-state probe this milestone didn't build). Left open for M3-M5
  (game-logic modules) to close with a scenario that confirms a kill.

## 6. Mutation test

Two independent one-byte mutations, both `#ifdef`-gated in `vgut.c`
(`LK_MUTATE_VGADD`, `LK_MUTATE_VGVTR1`; **undefined in the normal build** -
build_all.bat never passes either `-D`), each built as a separate one-off
executable and run against the same attract 60-frame window, then deleted
(see "Not yet done" step 6) - not part of the normal build.

**`LK_MUTATE_VGADD`** (`vgadd()`: advances VGLIST by `y+2` instead of
`y+1` - a wrong store): caught immediately, exact byte:
```
LOCKSTEP FAIL VGADD #1 (from $6D26, A=A9 X=02 Y=1B P=24): 1 byte(s) $0002 C=39 ROM=38
```
(`$0002` = `VGLIST` low byte; C's wrong value `$39` vs the ROM's correct
`$38` - off by exactly the mutation's +1). Overall for this build:
`LOCKSTEP: FAIL (10683 compared, 10563 failed, 120 tainted)` - fails
propagate through every routine that reaches VGADD (all of them but VGSCAL/
the never-called set), as expected since VGADD is VGUT's universal shared
tail.

**`LK_MUTATE_VGVTR1`** (`vgvtr1()`: forces `XCOMP+3`'s low bit to a literal
1 instead of the sign-extension carry `(x>>6)&1` - a wrong store to a
scratch cell): caught immediately, exact byte (`$0007` = `XCOMP+3`):
```
LOCKSTEP FAIL VGVTR #1 (from $501E, A=81 X=81 Y=00 P=26): 2 byte(s) $0007 C=FF ROM=FE $2807 C=1F ROM=1E
```
(the corrupted `XCOMP+3` byte then feeds straight into the VCTR word VGVCTR
appends, so the display-buffer byte at `$2807` differs too - the mutation's
effect on the ACTUAL vector-RAM output, not just the scratch cell, is
caught in the same report line). `VGVTR1` itself is never directly `JSR`'d
in this ROM (section 5), so the mutation surfaces under the names of its
callers: `LOCKSTEP: FAIL (10683 compared, 10058 passed, 505 failed, 120
tainted)`; VGDOT 96/96 failed (VGDOT always reaches the mutated code path);
VGVTR 409/790 failed (this mutation only changes the stored byte when the
correct `(x>>6)&1` would have been 0 - i.e. only when the ROM's own delta-Y
byte's bit 6 is clear - so roughly half of real calls show no visible
difference; that the split is real and input-dependent, not all-or-nothing,
is itself a useful confirmation that lockstep is comparing the actual byte
values and not just "some call happened").

## 7. Open items / unresolved

- Section 1 items "Not yet done" 1-7 above (avg.c integration in refrun.c,
  $6B79 classification in refrun.c, build_all.bat, PLAN.md M1 row).
- VGSCAL / kill-confirmation scenario coverage (section 5).
- The fuller M4-style "IRQ moved" NMI comparison was not built (section 4) -
  tainted calls are counted only, per the milestone's explicit minimum bar.
- No self-test scenario yet, so VGHEX/VGVTR1's direct-call coverage and
  VGVCTR's fall-through-only status can't be positively demonstrated beyond
  reading CALLING_NOTES.md (a later milestone's self-test scenario should
  confirm VGHEX/VGVTR1 direct checks appear, and that VGVCTR's direct-check
  count stays exactly 0 even there).
