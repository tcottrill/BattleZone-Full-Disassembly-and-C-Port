# AVG notes (avg.c / avg.h) - M0v

Battlezone's Analog Vector Generator semantics as implemented, with the
evidence for every decision, and what differs from the Tempest port's
`avg.c`/`avg.h` (`C:\Source2026\Tempest Dissasembly\c_src\avg.c`), which this
file was copied from and adapted, per the milestone brief ("reuse existing
C, do not rewrite from scratch").

## Result

- **Gate V** (`python tools/avg_gate_v.py`): **PASS** - 66 pictures/characters
  (29 `avg_pictures` + 37 VGAN characters, from
  `../disasm/build/vector_tables.json`), 899 segments, 66/66 match
  `../disasm/vrender.py` exactly (identical `x0,y0,x1,y1,intensity` in Q15,
  same segment count, picture by picture).
- **Gate T** (`python tools/avg_prom_sim.py`): **PASS** - 71 lists timed (the
  same 66 pictures/characters + 5 synthetic lists), 0 mismatches against an
  independent Python transcription of MAME's AVG state machine run on the
  real state PROM (`../roms/bzonea.zip` member `036408-01.k7`).
- `/W4` clean (`build_avg.bat`, VS2022 `cl /W4 /std:c11`): `avg.c`,
  `tests\avgshapes.c`, `tests\avgtime.c` - no warnings.

## Memory map

Flat 8K AVG address space, CPU `$2000-$3FFF`: `$2000-$2FFF` vector RAM (the
live display list), `$3000-$3FFF` vector ROM (pictures/VGAN characters).
JSRL/JMPL target = `$2000 + 2*(word & $1FFF)`.

Evidence: `../bzone.cpp`'s `AAE_DRIVER_VECTORRAM(0x2000, 0x1000)` (7
occurrences, one per driver variant); `../disasm/bzone_defines.asm`:
`VECRAM $2000`, `VECROM $3000`, `VGROM $3000`; VGMC.MAC's JSRL macro comment
("THE LOWER THREE NYBBLES SPECIFY AN ADDRESS RELATIVE TO THE BEGINNING OF THE
VECTOR GENERATOR ADDRESSABLE INSTRUCTION SPACE DIVIDED BY 2. THE TOTAL
ADDRESS AREA IS 8K BYTES"). Identical shape to Tempest's map (4K+4K, no
mirror) - `avg.c`'s `fetch()`/`target()` are unchanged from Tempest's.

## Opcode encodings, delta convention, scale arithmetic

Confirmed three ways, all in agreement - `../disasm/avg.py` (already
byte-verified against the ROM by the disassembly project), `VGMC.MAC`'s own
macros (Ed Logg's source, `battlezone-main/VGMC.MAC`), and the actual ROM
bytes (`../disasm/bzone_vector_rom.asm`):

| op | word(s) | VGMC.MAC evidence |
|---|---|---|
| VCTR (long) | `w1=DY&$1FFF`, `w2=Z<<13\|DX&$1FFF` | `.WORD DY&^H1FFF,<ZZ*^H2000>+<DX&^H1FFF>` - **13-bit two's complement**, never sign-magnitude; confirmed by the disasm listing's own raw hex (e.g. `VCTR -1536,0,3 ;0000 7A00`: `w2=$7A00=0111 1010...`, `DX field=$1A00=6656=-1536 mod 8192` - exactly two's complement) |
| SVEC (short, auto-picked) | `$4000\|Z<<5\|(DX/2)&$1F\|((DY/2)&$1F)<<8` | VCTR macro's own short-form branch, taken when `(|DX|\|DY|) & $0FFE1 == 0` (both even, both <=30) |
| STAT | `$6000 + one*$400 + hi*$200 + in*$100 + Z*$10` | STAT macro: `Z,HI,IN` - see "Clip window" below |
| SCAL | `$7000 \| b<<8 \| l` | SCAL macro: `S`=binary power of 2 (0=full,...,7=1/128), `LS`=linear (0=full, $80=1/2, $FF=1/256) |
| CNTR | `$8040` | macro takes no operand - the ROM only ever holds this one value |
| JSRL/JMPL | `$A000\|op/2`, `$E000\|op/2` | as the memory-map section |
| RTSL | `$C000` | macro takes no operand |
| HALT | `$2000` | macro takes no operand; not used by any ROM picture (every one ends RTSL) - kept in the decoder for completeness/robustness |

SCAL factor: `(255-l)/256 / 2^b`, Q15 form `(255-l) << (7-b)` -
`avg_scal_q15()` is byte-identical to Tempest's and to
`../disasm/avg.py`'s `scal_factor()`.

## Intensity: Z is used directly (no Tempest-style substitution)

**Decision**: VCTR/SVEC's 3-bit Z field is the drawn segment's intensity
*directly*. There is no "Z==1 selects the STAT intensity latch" rule (the
rule Tempest's colour AVG board, and the DVG/AVG family generally, use).

**Evidence for**:
1. VGMC.MAC's VCTR macro comment: "ZZ SPECIFIES THE BEAM INTENSITY (0
   THROUGH 7., 0 IS NO INTENSITY, 7. IS BRIGHTEST INTENSITY)" - no exception
   noted for the value 1.
2. The HATCH self-test picture's own brightness-bar sequence
   (`$3768-$3780`, 7 SVECs): `Z = 7,6,5,4,3,2,1` in strict descending order -
   the ROM byte at `$3780` is `SVEC 6,0,1 ;4023`. If Z==1 meant "substitute
   the STAT intensity latch" (whatever unrelated value that held - the last
   STAT Z parameter set anywhere earlier), the dimmest bar of this
   self-test brightness demonstration would not show level 1, contradicting
   its evident purpose (a literal 7-down-to-1 staircase). No VCTR in the ROM
   ever uses Z=1 (checked by grepping every `VCTR ... , 1` line), so only
   the SVEC case is directly exercised.
3. `../disasm/avg.py`/`vrender.py`'s own header comments state this
   explicitly, citing VGMC.MAC and "the AAE/MAME AVG core... used
   unmodified", and this is the byte-verified reference Gate V passes
   against (66/66).

**Evidence against, and why it doesn't change the decision**: AAE's
`bzone_strobe3` (`mame_late_avgdvg.cpp`) contains
`(((vg->int_latch >> 1) == 1) ? vg->intensity : vg->int_latch & 0xe) << 4` -
textually the same "Z==1" idiom the generic/Tempest boards use. A nibble-
level trace of where `int_latch` comes from settles this in favour of
"direct", not against it: `int_latch` is written *only* by `avg_latch3`
(`vg->int_latch = vg->data >> 4`), which the state machine invokes only
while fetching a VCTR's *second* word (the state PROM only visits latch3 for
a two-word instruction). For **SVEC** (one word), `int_latch` is never
refreshed for that instruction at all - it holds whatever was left over from
a previous, unrelated VCTR (typically stale/0 for a list built entirely of
SVECs, as HATCH's brightness bar is). A stale `int_latch` feeding this
substitution would make the brightness-bar SVECs render essentially blank
(`0 & 0xE == 0`) for a "==1" test that can never legitimately fire from
fresh data, not the demonstrably-intentional descending bar the ROM draws -
so even taken at face value, `bzone_strobe3`'s copy-pasted idiom cannot be
what makes this picture correct; VGMC.MAC's plain documentation (point 1)
and the observed ROM content (point 2) are decisive. `avg.c` therefore uses
Z directly for both VCTR and SVEC (`s.intensity = (uint8_t)z;`), matching
`vrender.py`. STAT's own `Z` parameter is still tracked
(`avg_state.intensity`) for completeness/future use, but is never applied to
a segment.

## Clip window (STAT's HI/LOW, IN/OUT parameters)

**What it is**, from VGMC.MAC's STAT macro comment (quoted in full, it is
the primary source):

> STAT - SET VECTOR GENERATOR STATUS. THE FIRST PARAMETER IS AN INTENSITY TO
> SET THE INTENSITY REGISTER. THE SECOND PARAMETER IS CALLED HI/LOW. THE
> CURRENT BEAM POSITION IS USED TO DEFINE THE CORNERS OF A RECTANGULAR AREA.
> IF HI/LOW=1, THEN THIS DEFINES THE UPPER RIGHT CORNER; OTHERWISE, IT IS THE
> LOWER LEFT CORNER. THE THIRD PARAMETER IS CALLED IN/OUT. IF IT IS 0, THEN
> LINES ARE DISPLAYED ON THE INSIDE ELSE THE OUTSIDE.

`STAT Z` alone (HI/IN both blank) sets word bit `$0400` ("`...1=1`" in the
macro) and only loads the intensity latch. `STAT Z,HI,IN` clears that bit
and instead arms one corner of a window rectangle **at the current beam
position** - HI=1 -> `(xmax,ymax)`, HI=0 -> `(xmin,ymin)`; IN=0 draws inside
the window, IN=1 outside.

**Hardware confirmation**: AAE's `bzone_strobe2`/`bzone_latch1`
(`mame_late_avgdvg.cpp`) implement exactly this - `bzone_strobe2` sets
`vg->intensity` from the Z field always, and when the "not a window" bit is
clear also sets two analog switches `lst`/`hst` from the HI bit (`lst =
dvy&0x200`, `hst = lst^0x200`) and stashes the IN bit as `izblank` (the
macro's IN/OUT bit; AAE's own comment: "Battle Zone doesn't seem to invert
zblank so it's no issue" - i.e. the ROM only ever uses IN=0, drawing
inside). `bzone_latch1` then - on the *next* word fetch - checks which
switch is off and records the beam position as that corner: `hst==0` (last
STAT had HI=1) -> `clipx_max=xpos, clipy_min=ypos`; `lst==0` (HI=0) ->
`clipx_min=xpos, clipy_max=ypos`; either way it queues a clip-rectangle
update and resets both switches to "on" (armed). AAE's version has an
apparent Y-axis flip relative to VGMC.MAC's plain-English "upper right /
lower left" (its internal `ypos` is a down-positive raster coordinate, while
`avg.c`/`vrender.py`/`avg.py` all use up-positive "AVG math space") - but the
two conventions cancel exactly: `hst==0` (HI=1) pairs `clipx_max` with
`clipy_min` in AAE's raster space, which is `(xmax, ymax)` in the up-positive
space, i.e. genuinely the upper-right corner, matching VGMC.MAC's comment
literally. `avg.c`'s `stat_window()` implements the plain-English reading
directly (`hi ? (xmax,ymax) : (xmin,ymin)` at the current beam position),
which is both simpler and already reconciled with AAE's internal sign
convention.

**Pipelining simplification**: AAE captures the corner one word-fetch after
the arming STAT (a serial-nibble-latch artifact of the real hardware). Since
neither the beam position nor the window registers can change between a
STAT and the very next fetch (the position only moves on the *next*
VCTR/SVEC/CNTR, which by definition is a later instruction than "the very
next fetch"), the observed corner is always exactly the beam position at the
STAT itself; `avg.c` captures it immediately in `stat_window()`, which is
behaviourally identical and avoids modelling the one-fetch delay.

**Not clipped in this milestone**: `avg_walk()` tracks the window rectangle
and an "armed" flag in `avg_state` and tags every `avg_seg` with
`AVG_CLIP_ARMED`/`AVG_CLIP_OUT` (avg.h), but does **not** truncate or blank
segment geometry - and neither does `../disasm/vrender.py` (its `walk()`
advances past STAT with no side effect at all beyond the instruction
pointer). Gate V therefore compares unclipped geometry on both sides by
construction; a future renderer (M7) is the natural place to apply the
window to the beam.

**Open item - the "horizon window" the milestone brief mentions**: the only
picture in the vector ROM that arms a window at all is `HATCH`
(`$3712 STAT 1,1,0` then `$371A STAT 2,0,0`, both around the self-test grid
test pattern) - checked by grepping every `STAT` with three operands in
`../disasm/bzone_vector_rom.asm` (2 occurrences, both in HATCH). No
attract/play picture in $3000-$3FFF arms one. The horizon/mountain-blanking
window the task brief describes must therefore be built at runtime by
BZONE.MAC's own display-list code writing STAT HI/IN words into vector RAM
(`DRAW`/`BIGWND`/`WNSHLD` per PLAN.md's port-order table) - out of reach of
a ROM-only Gate V walk. Unresolved until the M3 (BZONE display) pass can
walk an actual runtime-built list; `avg.c`'s clip-window tracking is already
general enough to consume that once it exists.

## Stack depth: 4 physical slots, not VGMC.MAC's "five levels"

`avg.c` uses the real hardware's 4-slot / 4-bit up-down counter (`stack[sp &
3]`, `sp = (sp +/- 1) & $F`) - identical to Tempest's `avg.c`. Evidence: AAE's
`vgdata` struct declares `UINT16 stack[4]`, and `avg_strobe0`/
`avg_common_strobe1` address it with `sp & 3` while `sp` itself is a 4-bit
(`&0xF`) counter - the same generic avgdvg-family hardware every AVG board
(Tempest, Space Duel, Star Wars, Major Havoc, Battlezone) shares; `avg_bzone`
does not override latch0/latch2/strobe0/strobe1, so its stack behaves
identically to Tempest's. This **contradicts** VGMC.MAC's own RTSL comment -
"THE VG IS CAPABLE OF FIVE LEVELS OF SUBROUTINE CALLS (JSRL)" - which is
Atari's assembler-level documentation, not the transistor-level truth: 4
physical return-address slots support 4 *nested* JSRLs (a 5th silently
overwrites slot 0); "five levels" plausibly counts the outermost,
non-nested call as a "level" too, an assembler-doc simplification rather
than a hardware fact. No ROM picture nests anywhere near this depth (deepest
observed: `MTNS`/`CRACKS`, one level, calling `MTNn`/`CRACKn`), so the
distinction cannot be tested against the ROM; `avg_walk()` follows the real
hardware (4 slots + wraparound, `AVG_FLAG_STACK_OVER` diagnostic) as the
milestone brief asks ("stack depth as the hardware's").

**Also noted (no effect on Gate V, explained rather than "fixed")**: `MTNS`
and `CRACKS` are drawn with no `RTSL` of their own - `MTNS` is 8 `JSRL
MTNn` words that fall straight through into `MTN0`'s own body (`$3006-$3015`
= 8 JSRL words; `MTN0` starts at exactly `$3016`, the address right after the
last one). When walked from `MTNS`, execution runs `MTN0..MTN7` via their
own JSRL/RTSL pairs, and after the *last* `RTSL` (from `MTN7`) returns to
`$3016` at depth 0, it falls straight into **`MTN0`'s body again**, running
it a second time as inline code, until `MTN0`'s own `RTSL` (at depth 0, no
caller) ends the walk. This is a deliberate Atari code-reuse trick (borrow
MTN0's trailing RTSL as MTNS's own return) rather than a decoder bug: since
`avg_walk()` and `vrender.py` both implement the literal hardware semantics
with no picture-specific special-casing, both reproduce this fall-through
identically, and Gate V passes for `MTNS` (54 segments) exactly as it does
for every other picture - the duplication is real and expected, not a
mismatch.

## Timing (Gate T)

The milestone brief requires the *authentic* MAME AVG cycle model, not an
approximation - copied unchanged from Tempest's `avg.c`/`avg.h`
(`norm_shifts`/`timer_reg`/`TICKS`, the per-op tick table, and the
`$8000-timer` / `$100-(timer&$FF)` beam-draw-time formulas). This is valid
for Battlezone because:

1. **Same state PROM.** Battlezone's state PROM, `../roms/bzonea.zip` member
   `036408-01.k7` (256 bytes), is **byte-identical** to Tempest's
   `136002-125` - both CRC32 `5903AF03` (verified independently by
   `tools/avg_prom_sim.py --verify-prom`, which reads the real zip member
   and the Tempest project's own extracted copy and diffs the CRCs; also
   printed on every Gate T run).
2. **Same handlers for everything timing depends on.** AAE's `avg_bzone`
   config (`mame_late_avgdvg.cpp`, the `vgconf avg_bzone` table) uses
   `avg_latch0`, `avg_latch2`, `avg_strobe0`, `avg_strobe1`, `avg_data`,
   `avg_vggo`, `avg_vgrst` **unchanged** - byte-identical function pointers
   to `avg_tempest`'s config. Its own `bzone_latch1` ends by calling
   `avg_latch1` (so the op/dvy assignment driving the state machine is
   identical - it only *also* records a clip-window corner first, a
   side-effect with no PROM-tick cost), and `bzone_strobe2`/`bzone_strobe3`
   both tail-call the shared `avg_common_strobe2`/`avg_common_strobe3` (the
   JSRL/RTSL/JMPL dispatch and the `timer -> cycles` arithmetic) after their
   own monochrome-intensity/clip bookkeeping. Nothing in the Battlezone
   variant changes a state transition or a cycle count - only which segment
   colour/clip-state gets recorded, which the timing model does not touch.
   Checked directly by reading the source (`mame_late_avgdvg.cpp` lines
   ~605-1025, ~1549-1565) - documented in `tools/avg_prom_sim.py`'s header
   alongside the CRC evidence.

Per-op state-PROM ticks (1 tick = 8 master cycles of the 12.096 MHz clock =
one 6502 cycle), plus the vector timer's beam-draw time where noted:

| op | ticks | + beam time |
|---|---|---|
| VCTR | 8 | `$8000 - timer` |
| SVEC | 6 | `$100 - (timer & $FF)` |
| STAT / SCAL | 7 | - |
| CNTR | 5 | `$8000 - timer` |
| JSRL | 5 | - |
| RTSL | 4 | - |
| JMPL | 3 | - |
| HALT | 2 | - |

plus one idle tick after the GO strobe (`AVG_VGGO_LEADIN`), which the
*caller* adds once per fresh pass - `avg_walk()` itself does not, so a
JSRL'd picture walk (no GO strobe of its own) doesn't double-count it.
`avg_result.cycles` is the running sum in master cycles; `avg_cycles_ms()`
converts to milliseconds - both macros copied unchanged from Tempest's
avg.h.

**HALT and the 3 kHz bit (why this matters for the oracle/playable build,
and what is and isn't this file's job)**: unlike Tempest (whose master lists
loop forever via `JMPL` to address 0), Battlezone's real display lists (built
fresh into vector RAM at `$2000` every frame by VGUT/BZONE) end in a genuine
`HALT` instruction. The 6502 polls this at `$0800` (IN0): bit 6 is "VG HALT"
(`../disasm/bzone_defines.asm`: "V (bit 6) = VG HALT (the AVG has finished
the display list)"; `../bzone.cpp`'s `bzone_IN0_r()`: `if (avgdvg_done())
res |= IN0_VG_HALT;`) - the bit that reads true exactly when a walk from
`$2000` reaches `AVG_STOP_HALT`, after `avg_result.cycles` master cycles
have elapsed since the GO strobe. `avg_result` exposes both `stop` and
`cycles` for exactly this reason, cleanly, with no change needed once the
oracle/hw.h seam (owned by the concurrent scaffold milestone) wires `GOADD`/
`IN0` to a live `avg_walk()`. IN0 bit 7 is a *separate*, unrelated 3 kHz
square-wave oscillator readback (`../bzone.cpp`: `(eterna_ticks + 6502
ticks) & 0x100`, i.e. a free-running divider of the board clock, polled by
`BZSTST`'s RAM test and the watchdog-clear loop) - it has nothing to do with
the AVG and is not modelled by `avg.c`; noted here only because the task
brief asked what the bit means.

**Gate T harness**: since Battlezone's pictures are subroutines (no
"depth-zero" hardware concept - a bare RTSL at depth 0 just pops whatever
garbage the 4-slot ring holds), both `tests\avgtime.c` and
`tools/avg_prom_sim.py` time each picture/synthetic list the way the real
display list actually invokes one: write `JSRL <target>; HALT` at CPU
`$2000/$2002` (into a copy of the ROM's own vram region for pictures; an
all-synthetic buffer for the 5 synthetic lists) and walk from `$2000`. Both
sides therefore reach a genuine, hardware-real `HALT` and compare exactly
the same quantity - no depth-zero special case, no adjustment arithmetic.
The 5 synthetic lists (`SYN_A..SYN_E`, byte-identical word tables in both
`tests\avgtime.c` and `tools/avg_prom_sim.py`, each documented with the
`../disasm/avg.py` `avg.encode()` call that produced its words) exercise
SCAL binary+linear together, CNTR, JSRL nesting to depth 3, a lone SVEC, and
a zero-delta VCTR (the normalizer's 16-shift cutoff path). `tests\avgtime.c`
also has a `frames DIR` mode and `tools/avg_prom_sim.py --frames DIR` its
Python counterpart, both **written but unexercised** - Battlezone has no
oracle yet (M1+), so no `frame_NNNN.vram` captures exist; kept ready so the
oracle milestone's harness doesn't need another pass over these files.

## What differs from Tempest's avg.c/avg.h (summary)

- Monochrome: no colour STAT, no colour RAM, no `avg_colram_rgb()`; VCTR/SVEC
  Z is the segment intensity directly (Tempest: `z==1` substitutes the STAT
  intensity latch).
- STAT carries the clip-window HI/LOW, IN/OUT parameters (Tempest's STAT has
  no window mode); `avg_seg`/`avg_state` gained `clip`/`clip_xmin..clip_out`
  fields, unclipped by design this milestone (see "Clip window").
- `avg_state_init()` starts `scale_q15` at Q15 1.0 (`AVG_Q15_ONE`), not 0 -
  matches `../disasm/vrender.py`'s picture-preview convention (most ROM
  pictures carry no SCAL of their own); Tempest's "start at 0" is a
  Tempest/AAE-specific quirk that does not apply here.
- No `AVG_OPT_PREVIEW` / vram-skip mode: Gate V loads the *same* 64K ROM
  image `../disasm/avg.py` does and slices vram/vrom from it directly, so
  both sides already see byte-identical memory; Tempest needed the preview
  mode because its shape test zeroes vram instead.
- No `avg_mem_g()`/`avg_run_frame()` - no `state.h`/`vecrom.c` exist yet
  (owned by the concurrent scaffold milestone); `avg_walk()` takes `avg_mem`
  (two raw pointers) directly, matching the brief's "take the vector memory
  through an accessor" requirement.
- Timing model, per-op tick table, `norm_shifts`/`timer_reg`/`TICKS`,
  `AVG_MASTER_HZ`/`AVG_CYC_PER_CPU`/`AVG_VGGO_LEADIN`/`avg_cycles_ms` are
  **unchanged** from Tempest (see "Timing" - proven identical hardware).
  Battlezone additionally stops at `AVG_STOP_HALT` for its real display
  lists (Tempest's loop endlessly instead) - `avg_result.stop`/`cycles`
  already expose exactly what a HALT-polling 6502 needs.
- Stack: same 4-slot hardware model as Tempest (`AVG_STACK_SLOTS 4`); see
  "Stack depth" for why this is right despite VGMC.MAC's own "five levels"
  comment.

## Files

- `avg.h`, `avg.c` - the walker (deliverable 1).
- `tests\avgshapes.c`, `build_avg.bat` - Gate V harness, C side (deliverable
  2): walks every picture/character from `tests\avg_shapes_index.txt` (see
  below) loaded straight from `../disasm/_survey/roms_extracted/
  bzone_cpu64k.bin`, writes `tests\avg_out\avg_shapes.txt`.
- `tests\avg_shapes_index.txt` - generated by `tools/avg_gate_v.py` from
  `../disasm/build/vector_tables.json` (never hand-transcribed).
- `tools\avg_gate_v.py` - Gate V (deliverable 3): drives the index
  generation + `avgshapes.exe` + the `../disasm/vrender.py` comparison.
- `tests\avgtime.c`, `tools\avg_prom_sim.py` - Gate T (added mid-milestone,
  see below): cycle-true timing vs the real AVG state PROM.
- `NOTES_avg.md` - this file (deliverable 4).

## Addendum - Gate T added mid-milestone

The initial M0v pass (Gate V only) was reviewed and the coordinator required
the authentic MAME cycle-timing model as a hard requirement, plus Gate T,
before sign-off. That work is folded into this file and into `avg.c`/`avg.h`
directly (there is no separate "part 2" - the TIMING section above and the
per-op tick table are the final state of the timing model, and both gates
are reported together per the updated instructions). See "Timing" above for
the full evidence trail (PROM CRC match, AAE handler-table diff, Gate T
methodology and results).
