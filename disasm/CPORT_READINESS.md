# C port readiness

A checklist of what a 1:1 C translation needs, and where each piece comes
from. This pulls together findings from `HARDWARE_NOTES.md`,
`MATHBOX_NOTES.md`, and `MAINLINE_NOTES.md` — read those first for the
detail behind each line item here.

## Source modules

| Module | Lines | Assembled size | Role | In main game link? |
|---|---|---|---|---|
| `BZMTNS.MAC` | 847 | part of the $7864-byte `.ABS.` section (`BZONE.MAP:16`) | vector ROM data: horizon, mountains, font, logo, cracks, radar ring, playfield object table (`PTBLO1` etc.) | yes (`BZONE.MAP:4`) |
| `BZONE.MAC` | 4946 | part of the same `.ABS.` section | main program: reset-adjacent init, NMI, `MAIN` frame loop, 3D pipeline glue, AI, HUD/messages, data tables | yes |
| `COIN65.MAC` (included as `TC65`, see note below) | 644 | inline, part of `BZONE.MAC`'s object code | generic Atari coin-mech handler (`MOOLAH`) | yes, via `.INCLUDE TC65` at `BZONE.MAC:1236` |
| `BZSOUN.MAC` | 350 | $01B5 = 437 bytes at `$7864` (`BZONE.MAP:19`) | POKEY sound sequencer (`SNDON`/`MODSND`) | yes |
| `VGUT.MAC` | 385 | $00B3 = 179 bytes at `$7A19` (`BZONE.MAP:20`) | vector-list-building primitives (`VGCNTR`, `VGVTR`, `VGJSRL`, ...) | yes |
| `BZSTST.MAC` | 649 | $052E = 1326 bytes at `$7ACC` (`BZONE.MAP:21`) | power-on (`PWRON`), self test | yes |
| `MBDIAG.MAC` (included by `BZSTST.MAC`) | 157 | inline in `BZSTST.MAC`'s object code | 6502-side Mathbox diagnostic, with a self-contained input/expected-output test table | yes, via `.INCLUDE MBDIAG` at `BZSTST.MAC:380` |
| `VGMC.MAC` | 145 | inline in `BZSTST.MAC`'s object code | additional self-test vector-generator macros | yes, via `.INCLUDE VGMC` at `BZSTST.MAC:46` |
| `MBUCOD.V05` | 715 | separate ROM set, not part of the 6502 program | Mathbox's own 2901 microcode — not 6502 code at all, drives the Mathbox hardware model instead | no (runs on the aux board, not the 6502) |
| `BZSADG.MAC` | 301 | separate link (`BZSADG.COM`, `BZONE.DOC:71-81`) | standalone vector-generator diagnostic cartridge/board test | **no** — different link entirely, not part of `BZONE.SAV` |

Total 6502 program + vector ROM footprint: **`$3000`-`$7FFA`**, ~20 KB
(`BZONE.MAP:50`, "Low limit = 3000 High limit = 7FFA"). Zero page is
nearly full: `BZONE.MAC`'s own `.BSECT` uses `$00-$EE` (238 bytes,
`BZONE.MAC:202-284`) and `BZSOUN.MAC` claims 17 more bytes on top
(`BZONE.MAP:18`) — a C port's "zero page" analog (whatever fast/global
scratch area it uses) inherits essentially no slack for extra state
without also relaying it through the object-table pages at `$0200`+.

**Open naming discrepancy**: `BZONE.MAC:1236` includes a file named
`TC65`, but the only coin-routine source shipped in `battlezone-main\` is
`COIN65.MAC`. The assembled symbol table (`disasm\build\symbols.json`)
records `MOOLAH` and its internals under file `TC65.MAC`. Resolved: the
disassembly tooling maps the include name `TC65` to `COIN65.MAC`
(`macsrc.py` `ALIASES`), `BZONE.DOC` itself lists the include as `COIN65`,
and the result assembles to the ROM byte for byte - so `TC65` is simply
the name the file had on the programmer's disk.

## Constructs that bite a 1:1 translation

### Stack-based computed jumps (twice)

Two places build a jump-table target address and "return" into it with a
bare `RTS`, instead of an indexed `JMP`:

- `DRAW`'s dispatch on point-table opcodes, `D.TOP` (`BZONE.MAC:1875-1886`):
  ```
          LDA AX,DTAB+1
          PHA
          LDA AX,DTAB
          PHA
          RTS
  DTAB:   .WORD D.DOT-1,D.SBRT-1,D.BVCT-1,D.LABS-1
          .WORD D.VCTR-1,D.SPCL-1,D.SLIM-1
  ```
- The tread-lever motion dispatch, `M.TOP` (`BZONE.MAC:2640-2648`):
  ```
          LDA AY,MTAB-9
          PHA
          LDA AY,MTAB-0A
          PHA
          RTS
  MTAB:   .WORD M.FF-1,M.PR-1,M.RTF-1,M.STOP-1,M.PL-1,M.FR-1
          .WORD M.LTR-1,M.STOP-1,M.LTF-1,M.RTR-1,M.STOP-1
  ```

Both push `target-1` (high byte then low byte) and let `RTS` add 1 and
jump — the standard 6502 idiom for an indexed computed jump (the plain
6502 has no indexed-absolute-indirect `JMP`). **This is not a subroutine
call** — nothing was `JSR`'d to get here in the way the label placement
suggests, and whichever handler runs (`D.DOT`, `M.FF`, etc.) is expected
to eventually fall through to a shared continuation, not `RTS` back to
whoever set up the jump. A literal 1:1 C translation needs an explicit
`switch`/function-pointer-table here, not an attempt to preserve the
stack trick.

### Fall-through routines (no `RTS` between them)

`VGUT.MAC` chains several "routines" with no return instruction between
them — the next label is just the continuation of the previous one, and
callers are expected to know which entry point gives which behavior:

- `VGRTSL` falls into `VGHALT`'s continuation label `VGHAL1`
  (`VGUT.MAC:127-142`).
- `VGHEXZ` falls into `VGHEX1` inside `VGHEX` (`VGUT.MAC:156-176`).
- `VGCNTR` falls into `VGADD2` (`VGUT.MAC:250-263`, the `JMP VGADD2` is
  commented out — it's not needed because execution just continues).
- `VGDOT` falls into `VGVTR`/`VGVTR1` (`VGUT.MAC:313-331`).
- `VGVTR1` falls into `VGVCTR` via the `VGVTR2` label
  (`VGUT.MAC:348-352`).
- `VGSCA1` falls into `VGSCAL` (`VGUT.MAC:297-298`).

None of these are bugs — it's deliberate code-size sharing, extremely
common in 6502 assembly of this era. A C port has two honest choices:
either give each fall-through group one function with an internal entry
parameter, or duplicate the shared tail into each public entry point —
either is fine, but a literal "translate every label to a function that
returns at its end" pass will silently break every one of these unless
each is checked by hand first.

### Self-relative vector-generator address tables

`VGJMPL`/`VGJSRL` (`VGUT.MAC:190-224`) don't take a normal 6502 address —
they take the target's address **relative to vector-instruction-space
origin, divided by 2** (`LSR; AND I,0F` before encoding). Every
`.WORD label-1` table meant for `PHA/PHA/RTS` dispatch above is a normal
6502 address, but every `LAH`/`LXL` pair feeding `VGJSRL`/`VGJMPL` (the
macros at `BZONE.MAC:404-417`) is *not* — it's loading a literal absolute
address that then gets bit-shifted by the callee into VG-space-relative
form. A C port's vector-RAM/AVG model needs its own address space
(matching the AVG's own addressing, not the 6502's) for this to come out
right; treating VG jump targets as ordinary host pointers will not work.

### Decimal (BCD) mode

Score arithmetic runs in decimal mode, bracketed carefully with explicit
`SED`/`CLD`: e.g. `SED` before `ADC HITS` (`BZONE.MAC:2498-2500`), and
similarly at `BZONE.MAC:1039, 2353, 3256, 3362`. `HITS` (4 bytes,
`BZONE.MAC:246`) is therefore packed BCD, not binary. Coin arithmetic is
explicitly forced *out* of decimal mode instead — `CLD` at the very top
of the NMI handler with the comment *"INSURE WE DON'T SCREW UP COINS IN
BINARY"* (`BZONE.MAC:1090-1091`), and again in `PWRON` (`BZSTST.MAC:51`).
**A C port must implement real BCD add/subtract for the `HITS` score
path** (not just format a binary integer as decimal for display) —
6502 decimal-mode addition has its own carry/half-carry rules that don't
match binary arithmetic, and this codebase relies on them.

### Code shared by NMI and mainline

`SNDON` (`$7985`) is called both from inside the NMI handler (crash/
saucer/explosion sound-priority logic, `BZONE.MAC:1106-1122`) **and**
directly from mainline frame code (e.g. the object-collision "boing",
`BZONE.MAC:2675-2676`). On real hardware the NMI can preempt mainline at
any instruction boundary, including mid-way through a mainline call into
`SNDON`. Nothing in `BZSOUN.MAC` was found explicitly guarding against
reentrancy (no interrupt-disable bracket around `SNDON`'s internal
table writes) — either the original code relies on `SNDON`'s specific
instruction sequence being safe to interrupt at any point (worth
verifying by hand before assuming so), or it simply never mattered in
practice. **A straight single-threaded C translation sidesteps this
automatically** as long as the port keeps the same call structure (NMI
handling as a function called at defined points between frame-logic
steps, not a true asynchronous interrupt) — call this out as a place
where the emulation's cooperative scheduling is *safer* than the
original hardware's true preemption, not a place needing extra work,
provided the port doesn't try to run NMI logic on a real separate thread.

### A vector-RAM checksum used as copy protection *and* a gameplay gate

Documented in full in `MAINLINE_NOTES.md` ("A trap worth knowing about").
`BZONE.MAC:539-550` XOR-checksums $36 = 54 bytes (offsets $02-$37) of the vector list the current
frame just built, and `BZONE.MAC:622-626` only allows the Super Bonus
Tank bonus to trigger if that checksum matches what the original code
expects. **This means the C port's vector-RAM buffer must be byte-
identical, byte-for-byte, to the original in that specific 54-byte
window**, or the Super Bonus Tank silently stops working. Any
restructuring of how the display list gets built (different helper
function boundaries are fine; a different byte layout in the buffer
itself is not) risks breaking this one gameplay feature invisibly — worth
a dedicated regression test once the vector-RAM model exists.

### One giant frame routine, not a per-system update loop

`MAIN` (`$5000`) is not decomposed into "update AI", "update physics",
"build display list" as separate top-level calls the way a modern engine
might expect — it's one long routine that does clip-window setup, 3D
projection, HUD drawing, *then* AI/collision for next frame, all inline,
documented call-by-call in `MAINLINE_NOTES.md`. A C port can absolutely
split this into named functions (and should, for readability) — just
make sure the **order** is preserved exactly, since later steps depend on
state the earlier steps in the same pass left behind (e.g. `ROTATE`
before `BATTLE` before the per-object `PNTPUT`/`DRAW` loop before
`FINISH` before the AI tail).

### Flag-passing conventions

Ordinary 6502-idiom flag passing throughout — carry set/clear as a
boolean return (e.g. `OBJOBJ`'s collision result consumed via `BCS`,
`BZONE.MAC:681-682`; `TR7CHK`'s carry-clear-means-regular-tank result,
`BZONE.MAC:3699-3703`), and `BIT`-tested high bits of state flags used as
booleans (`ATRACT`, `GOVER`, `LOGO`, `SAUCER`, `R2D3FL` are all tested via
`BIT`+`BMI`/`BPL`, meaning their "true" value is specifically `$FF`/
negative, not just nonzero — a C port representing these as `bool` needs
to make sure nothing downstream ever relies on the exact bit pattern
rather than truthiness; a spot check of the flags above didn't find one,
but this wasn't exhaustively verified for every flag in the module).

## Hardware models required

All detailed in `HARDWARE_NOTES.md`/`MATHBOX_NOTES.md`; summarized here
as a checklist:

- [ ] **6502 core** — reuse per the user's own standing instruction:
  `C:\Source2026\shared\ref6502\` or
  `C:\Source2026\6502_Klaus_tests_c6502\cpu_6502.cpp`. Not evaluated
  further here since this project translates 6502 code to C rather than
  interpreting it, but decimal-mode correctness (above) matters if any
  intermediate/validation step runs the real ROM through an interpreter
  for comparison.
- [ ] **AVG (vector generator), monochrome, Battlezone clip-window
  variant** — confirmed as `avg_start_bzone()` in the reference driver
  (`bzone.cpp:377, 387`), distinct from a generic AVG. Needs: the
  `$2000-$2FFF` vector-RAM double-buffer scheme (`MAINLINE_NOTES.md`),
  the `VGUT.MAC` instruction encodings (`CNTR`/`HALT`/`JSRL`/`JMPL`/
  `STAT`/`SCAL`/`RTSL`/`VCTR`, documented in the `BZMTNS.MAC:9` header
  block and implemented in `VGUT.MAC`), and the GO/reset strobes plus
  halt-status readback bit documented in `HARDWARE_NOTES.md`.
- [ ] **Mathbox** — **reuse the Tempest port's microcode interpreter**
  (`C:\Source2026\Tempest Dissasembly\c_src\mathbox.c`/`.h` + `mbprom.c`):
  the microcode source, the mapping PROM and all six microcode PROMs are
  byte-identical between the two games (see the first section of
  `MATHBOX_NOTES.md`); only the bus addresses change (`$1800`, `$1810`,
  `$1818`, `$1860+n`). What follows describes the behaviour that model
  already produces, for reference: the
  32-register command interface, sign-magnitude multiply/divide with the
  specific rounding/truncation behavior documented there, the 3/8
  fast-distance approximation (not a real hypotenuse), and the binary-
  bisection clip command. **`MBDIAG.MAC`'s test table
  (`MBDIAG.MAC:115-147`) is a ready-made set of input/expected-output
  vectors** — turn it directly into C unit tests for the Mathbox model
  before wiring it into the rest of the port.
- [ ] **POKEY** (1 chip) — for `BZSOUN.MAC`'s `SNDON`/`MODSND` tonal
  sounds and for reading the tank tread levers/fire button through
  `ALLPOT`'s packed switch nibble (not through individual analog pot
  channels — see `HARDWARE_NOTES.md`).
- [ ] **Discrete sound board** ($1840 latch) — engine rumble, shell fire
  (loud/soft), explosion (big/small), start-button LED, master enable;
  full bit table in `HARDWARE_NOTES.md`. The reference driver implements
  this with WAV samples rather than analog circuit simulation
  (`bzone.cpp:219-267`) — reasonable prior art for a first pass, though a
  "real" discrete-audio model would need schematics not present in any
  source file read for this task.
- [ ] **Watchdog** — trivial; a write-resets-timer counter, standard
  Atari pattern (`HARDWARE_NOTES.md`).
- [ ] **EAROM — not needed.** Confirmed absent from Battlezone's own
  memory map (`HARDWARE_NOTES.md`); high scores are volatile RAM, reset
  every power cycle by `PWRON`. Don't build this model.
- [ ] **Coin-counter solenoids** — optional/stub. The game maintains
  software pulse timers (`$CCTIM`) for them, but no corresponding
  hardware output address was found in the reference driver, and they
  have no visible effect on gameplay (`HARDWARE_NOTES.md`).

## Proposed translation order

1. **Memory-mapped I/O scaffolding + RAM layout.** Get the address
   decode (`HARDWARE_NOTES.md`'s table) and the exact `.BSECT`/`.ASECT`
   RAM layout from `BZONE.MAC:202-328` in place first, byte-address-exact
   — the Super Bonus checksum trap (above) means "close enough" RAM
   layout for the vector buffer specifically isn't good enough later.
2. **AVG vector-RAM model + `VGUT.MAC` primitives.** Nearly everything
   downstream calls into `VGCNTR`/`VGVTR`/`VGJSRL`/`VGDOT`/`VGSTAT`/
   `VGSCAL` — get these right (including the fall-through groups noted
   above) before anything that emits vectors.
3. **Mathbox model**, validated standalone against `MBDIAG.MAC`'s test
   vectors before it's wired to anything else — it's the highest-risk
   "quiet wrongness" component (a slightly-off distance approximation or
   rounding step won't crash anything, it'll just make AI/aiming feel
   subtly off).
4. **POKEY + discrete sound latch models**, independent of the Mathbox
   work, needed before `BZSOUN.MAC` can be ported meaningfully.
5. **`BZMTNS.MAC` as static data**, not code — it's overwhelmingly vector-
   ROM shape/point-table data with a handful of `JSRL` dispatch points;
   translate it into a C data table + the small set of VG-space entry
   labels other code jumps to (`HORIZN`, `MTNS`, `OBJPNT`, `PTBLO1`,
   `LOGOBJ`, `CRACKS`, `ARCTAN`, `BONTBL`, `MISLVL`, etc.).
6. **`COIN65.MAC`/`MOOLAH`.** Self-contained, generic, well-commented by
   Atari for exactly this kind of reuse (`COIN65.MAC:148-291`
   documents its own variable contract) — a good early "porting warm-up"
   module independent of the rest of the game.
7. **`BZSOUN.MAC` (`SNDON`/`MODSND`).** Depends on the POKEY model from
   step 4.
8. **`BZSTST.MAC` (`PWRON` + self test).** `PWRON`'s reset sequence is
   needed for correct initial state before `MAIN` can run at all; the
   rest of self test can be deferred behind a stub that always reports
   "test switch not held" until the C port wants a self-test screen.
9. **`BZONE.MAC`'s `NMI`.** Small, everything else hangs off `SYNC`/
   `BUFRDY`/the double-buffer flip it owns — port it in full before
   tackling `MAIN`.
10. **`BZONE.MAC`'s `MAIN`**, function by function, in the exact call
    order documented in `MAINLINE_NOTES.md` — `MOUNTS`, then `ROTATE`/
    `BATTLE`, then the per-object `PNTPUT`/`DRAW` loop, then the HUD/
    message routines, then `FINISH`, then the AI/collision tail
    (`COLCHK`/`SHUPDT`/`QWIKCK`/`ROBOT`/`SAUCMV`/`BOUND`/`MOTION`) last,
    since it feeds *next* frame rather than the one just finished.
11. **Validation**: run `MBDIAG.MAC`'s vectors as unit tests (step 3);
    diff the C port's assembled/derived state against the two known ROM
    CRCs (`efbc3fa0` source-matching, `13de36d5` MAME's other revision —
    both documented in `HARDWARE_NOTES.md`) to confirm which behavior a
    given build target should match; and specifically exercise the Super
    Bonus checksum path as an integration test that the vector-RAM byte
    layout is faithful, not just visually equivalent.

## Reading further

- `HARDWARE_NOTES.md` — full memory map and every I/O register bit this
  checklist references.
- `MATHBOX_NOTES.md` — the Mathbox model in the detail needed to actually
  build it, plus the `MBDIAG.MAC` test-vector table.
- `MAINLINE_NOTES.md` — the frame-by-frame call order this checklist's
  translation order is based on, and the full Super Bonus checksum-trap
  writeup.
