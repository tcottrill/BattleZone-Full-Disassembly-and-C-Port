# Battlezone — calling conventions

Entry and exit contracts for the routines a C port has to get right first: the
ones called from more than one place, or across module boundaries — the VGUT
vector-list utilities, message/number display, the maths helpers, the
object/point-table pipeline, sound, the coin routine, and the NMI handler's
callees. Read from `bzone_program_rom.asm` (line numbers as `L####`, in that
file — not to be confused with the listing's own `Lxxxx:` *address* labels)
and cross-checked against Atari's source comments the listing already carries
over verbatim. This is preparation only; **no C code is written here**.

Every ENTRY/EXIT/USES block quoted from Atari's own source is marked
"Atari's header" and was checked against the code, not trusted — several
turned out to be stale (see the "Hazards summary" table at the end).
Everything else is derived from reading the instructions. Addresses and
header text can be cross-checked directly against `bzone_program_rom.asm`
and `FUNCTIONS.md`.

---

## 0. Conventions the whole program assumes

**The vector-list cursor.** `VGLIST`/`VGLIST+1` (zero page) is a running
pointer into the AVG display list being built in VGRAM. Almost every VGUT
routine's contract is "entry/exit: `(VGLIST,VGLIST+1)` = vector list address"
— i.e. it is a stateful output iterator, not a pure function: call order
matters, and every routine that appends to the list also advances the
pointer as a side effect.

**The Mathbox (2901 bit-slice ALU).** A separate co-processor reached through
hardware registers `MSTAT` ($1800, D7 = busy), the argument registers at
`ACOS`/`BSIN`/`TPOSXT`/`TPOSYT`/... ($1860+, "MBOXIN"), and the result
registers `MOUTLO`/`MOUTHI` ($1810/$1818). `WAIT` (§3) is the universal
"start a Mathbox op, then read the result" wrapper; SIN/COS/ARCTAN and the
X/Y-distance-and-divide sequences all go through it. A C port replaces the
Mathbox with the equivalent floating-point/fixed-point math directly — there
is no way to "call" real Mathbox hardware, so every `WAIT` call site is a
seam.

**The push-address-then-RTS computed-goto.** This source uses the same
6502 trick three separate times for three unrelated dispatch tables — worth
naming once instead of three times: a table holds `.word TARGET-1` entries;
the dispatcher indexes it, does
```
LDA TABLE+1,X   ; high byte
PHA
LDA TABLE,X     ; low byte
PHA
RTS
```
and the `RTS` pops low-then-high, adds 1, and "returns" into `TARGET`. Seen
at `DTAB` (DRAW's per-opcode dispatch, §4), `TTAB` (TRACK's quadrant fix-up,
§3) and `LNGTBL` (MSGS's per-language dispatch, §2). A C port models each as
an ordinary array of function pointers or a `switch`; the `-1`/`+1` bookkeeping
is 6502-only and should not appear in the port.

**Object tables.** `ROTATE` (§3/§4) selects on-screen objects from the ROM
playfield tables into three parallel RAM arrays indexed by `Y` in twos —
`PTBLX2`/`PTBLY2` (position) and `PTBLO2` (object #/orientation byte),
56 bytes each. `PNTPUT` (§4) then projects one such object's 3-D point table
into 2-D screen deltas in `PNTTBL`, and `DRAW` (§4) walks its edge list
(opcode stream from `OBJTBL`) to emit the actual AVG vectors. `SAVE0` is the
loop index into `PTBLO2`/`PTBLX2`/`PTBLY2` across a whole frame's worth of
objects (`MAIN_20`, `L50AA`-`L50BE`).

**Decimal mode.** Used only for two things, both narrowly bracketed
`SED`...`CLD`: the two-limb BCD add into `HITS`/`HITS+1` (robot/player hit
counters — four call sites: `SHRTCK` `L6011`-`L601A`, `SAUCHK_30`
`L6127`-`L6130`, `BUZBOM` `L668E`-`L6691`, `COLIDE`-area `L675A`-`L675D`) and
one bonus-coin lookup (`DISTBL` `L554D`-`L5550`). `NMI` (§7) explicitly
`CLD`s on entry with the comment "INSURE WE DON'T SCREW UP COINS IN BINARY"
— i.e. it assumes decimal mode could be left set by mainline code interrupted
mid-`SED`/`CLD` bracket, and defensively clears it before `MOOLAH` (the coin
routine, which is pure binary) runs. A C port has no decimal mode; these
four `HITS` sites need an explicit BCD-add helper (or, more simply, switch
`HITS` to plain binary and drop the `SED`/`CLD` pairs entirely, since nothing
else depends on `HITS` being BCD-encoded on-disk — check the display code
that reads it before doing this).

---

## 1. VGUT — vector generator utilities (`VGUT.MAC`, $7A19-$7AC9)

VGUT.MAC carries genuine Atari `ENTRY`/`EXIT`/`USES` header comments for
every routine (quoted below, block-copied from the shared library — the
module's own top-of-file header is the generic VGUT.MAC boilerplate, not
Battlezone-specific). All of it checks out against the code **except** that
three of the documented routines are never actually called by this game (see
end of section).

The whole module is one long fall-through chain: `VGRTSL` → `VGHAL1`,
`VGHALT` → `VGHAL1`, `VGHEXZ` → `VGHEX` → `VGHEX1`, `VGJMPL` → `VGJMP1`,
`VGJSRL` → `VGJMP1`, `VGSTA1` → `VGSTAT`, `VGCNTR` → `VGADD2` → `VGADD`,
`VGSCA1` → `VGSCAL`, `VGDOT` → `VGVTR` → `VGVTR1` → `VGVTR2` → `VGVCTR` →
`VGWAI1` → (tail-`BNE`) `VGADD`. A C port that gives each of these its own
function needs to either duplicate the shared tail code or replicate the
fall-through as an explicit call — the ROM shares bytes, C functions don't.

### VGRTSL ($7A19) — L6762 — **not called anywhere in this ROM**
- Atari's header: "ENTRY `(VGLIST,VGLIST+1)` = VECTOR LIST ADDRESS. EXIT
  `(VGLIST,VGLIST+1)` = NEW VECTOR LIST ADDRESS. USES A,Y,`(VGLIST,VGLIST+1)`."
- Adds an `RTSL` (opcode `$C0`) to the list, then falls into `VGHAL1`.
- **Dead code**: grep of the whole listing finds zero `JSR`/`JMP`/branch to
  `VGRTSL`. Every `RTSL` this game's AVG lists actually use is baked
  statically into the vector ROM (e.g. `V3004: RTSL` closing `HORIZN`), never
  emitted dynamically at runtime. Do not port unless something else needs it.

### VGHALT ($7A1D) — L6773 — callers (2): `$5574` FINISH · `$7F27` (BZSTST)
- Atari's header: "ENTRY `(VGLIST,VGLIST+1)` = VECTOR LIST ADDRESS. EXIT
  `(VGLIST,VGLIST+1)` = NEW VECTOR LIST ADDRESS. USES A,X,Y,`(VGLIST,VGLIST+1)`."
- `JSR VGCNTR` first (re-centres the beam), then falls into `VGHAL1` to
  append a `HALT` opcode (`$20`). `VGCNTR` itself clobbers A/X/Y per its own
  contract (§ below) — so "USES A,X,Y" already accounts for that call.

### VGHEXZ ($7A29) — L6792 — callers (2): `$7BB4`, `$7BC0` (BZSTST bookkeeping display)
- Atari's header: "ENTRY (A) = LOWER 4 BITS TO BE DISPLAYED. (C) = CARRY
  CLEAR IF NO ZERO SUPPRESSION. EXIT (C) = CARRY CLEARED IF NON-ZERO DIGIT
  DISPLAYED. USES A,X,Y,`(VGLIST,VGLIST+1)`."
- If carry is set on entry (suppression wanted) and the digit is zero,
  branches straight to `VGHEX1` **with carry still set** and does *not* draw
  anything — the caller's carry-clear-means-"drew something" contract is how
  a chain of `VGHEXZ` calls (score digits, MSB→LSB) knows when to stop
  suppressing leading zeros. Falls into `VGHEX` otherwise.

### VGHEX ($7A2F) — L6808 — callers (6): `$7A29` VGHEXZ (fall-through target, no `JSR`) · `$7E9E`,`$7EB6`,`$7EC4`,`$7ED2`,`$7EF5` (BZSTST)
- Atari's header: "ENTRY `(VGLIST,VGLIST+1)` = VECTOR LIST ADDRESS. (A) =
  LOWER 4 BITS TO BE DISPLAYED. EXIT `(VGLIST,VGLIST+1)` = NEW VECTOR LIST
  ADDRESS. (C) = CARRY IS CLEAR. USES A,X,Y,`(VGLIST,VGLIST+1)`."
- Looks up a `JSRL`-to-the-character-glyph pair in `VGMSGA` (the vector-ROM
  character-JSRL table VGAN.MAC provides — see Atari's own EXTERNAL ENTRY
  POINTS note in the module banner, §2), writes it via `VGADD`, `PLP` restores
  the caller's carry flag (pushed at `VGHEX1`) — note the header's own "(C) =
  carry is clear" EXIT claim is only exactly true when entered via `VGHEX`
  itself (which explicitly clears C before falling into `VGHEX1`); entered
  from `VGHEXZ` the exit carry is whatever `VGHEXZ` pushed, verified above.

### VGJMPL ($7A49) — L6836 — **not called anywhere in this ROM**
- Atari's header: "ENTRY `(VGLIST,VGLIST+1)` = VECTOR LIST ADDRESS. (A) = MSB
  OF ADDRESS. (X) = LSB OF ADDRESS. EXIT `(VGLIST,VGLIST+1)` = NEW VECTOR
  LIST ADDRESS. USES A,Y,`(VGLIST,VGLIST+1)`."
- Dead code, same situation as `VGRTSL`: every `JMPL` in this game's vector
  lists is static vector-ROM data, never built at runtime.

### VGJSRL ($7A5A) — L6860 — callers (6): `$50FF` MAIN_48 · `$5806` (mountain rendering) · `$5D06` D_SPCL · `$6AED` (radar) · `$7E6D`,`$7E7E` (BZSTST)
- Atari's header: "ENTRY `(VGLIST,VGLIST+1)` = VECTOR LIST ADDRESS. (A) = MSB
  OF ADDRESS. (X) = LSB OF ADDRESS. EXIT `(VGLIST,VGLIST+1)` = NEW VECTOR
  LIST ADDRESS. USES A,Y,`(VGLIST,VGLIST+1)`."
- Shares `VGJMP1` with the (unused) `VGJMPL`; `ORA #$A0` (JSRL's opcode
  nibble) guarantees the byte is never zero, so `BNE VGJMP1` always falls
  through as intended — not data-dependent, safe.
- Both `X` and `A` are register-passed, unlike `VGSTAT`/`VGSCAL`'s
  `A`-plus-caller's-`Y`-in-`VGBRIT` convention below — check the exact
  register pairing when porting call sites (`LXL`/`LAH` macro pairs load
  `X`=low, `A`=high immediately before the call, e.g. `L5D02`-`L5D06` in
  D_SPCL, §4).

### VGSTA1 / VGSTAT ($7A61 / $7A63) — L6868/L6881 — VGSTA1 **never called**; VGSTAT callers (6): `$5023` MAIN · `$5082` MAIN_16 · `$51E6` WNSHLD · `$5CCE` D_SBRT · `$7E40`,`$7E50` (BZSTST)
- Atari's header (on `VGSTA1`, inline comment not a boxed block): "VGSTAT -
  SET VECTOR GENERATOR STATUS. ENTRY (A)=HI/LOW AND IN/OUT FLAGS AND ENABLE
  (0,1,2,3,OR 4). (Y)=VECTOR BRIGHTNESS (0,10,20,...,F0).
  `(VGLIST,VGLIST+1)`=VECTOR LIST ADDRESS. EXIT `(VGLIST,VGLIST+1)`=NEW
  VECTOR LIST ADDRESS. USES A,X,Y,`(VGLIST,VGLIST+1)`."
- `VGSTA1` is the entry that reads brightness from the global `VGBRIT`
  (`LDY VGBRIT`) before falling into `VGSTAT`; **every actual caller in this
  ROM already has the brightness in `Y`** (from a prior `VGVTR`/`VGDOT`/
  literal `LDY`) and calls `VGSTAT` directly, so `VGSTA1` — despite being
  documented as the "normal" entry in Atari's header — is unreachable dead
  code here. `VGSTAT`'s own `[note]` in the listing ("second entry point,
  for callers that already have the brightness in Y") is this disassembly's
  own correct read of the situation.

### VGCNTR ($7A6A) — L6893 — callers (19): `$5016`,`$5074` MAIN/MAIN_15 · `$51D9` BIGWND · `$541D`,`$54A3` (hi-score) · `$5527` DISTBL_25 · `$57FC`,`$584A` (mountains) · `$5CDE` D_LABS · `$6CEA`,`$6D2E` (message positioning) · `$7A1D` VGHALT (fall-through source, no `JSR`) · `$7E33`,`$7E43`,`$7E70`,`$7E81`,`$7EAA`,`$7EE3`,`$7F0B` (BZSTST)
- Atari's header: "ENTRY `(VGLIST,VGLIST+1)`=VECTOR LIST ADDRESS. USES
  A,X,Y,`(VGLIST,VGLIST+1)`." (No stated inputs beyond the list pointer —
  verified: `LDA #$40; LDX #$80` are hardwired constants, not parameters.)
- Falls into `VGADD2` to write the `CNTR` opcode (`$80`) with the fixed
  "timer + scale" byte `$40`.

### VGADD2 ($7A6E) — L6906 — callers (6): `$51F6` WNSHLD · `$5571` DSPALP (tail-`JMP`, so DSPALP's caller gets VGADD2's exit state) · `$58B4` (mountains) · `$7A67` VGSTAT (fall-through) · `$7A87` VGSCAL (fall-through) · `$7F24` (BZSTST)
- Atari's header: "ENTRY (A)=FIRST BYTE. (X)=SECOND BYTE. EXIT
  `(VGLIST,VGLIST+1)`=NEW VECTOR LIST POINTER. USES A,Y,`(VGLIST,VGLIST+1)`."
- Writes `A` then `X` as the two-byte word at `(VGLIST),Y=0/1`, then falls
  into `VGADD` with `Y=1` already set — `VGADD`'s "add Y+1" contract (below)
  is what actually advances the pointer by 2.

### VGADD ($7A76) — L6922 — callers (6): `$5517` (message pointer advance) · `$6D26`,`$6D56` (message loop) · `$7A44` VGHEX1 · `$7A58` VGJMP1 (tail-`BNE`, i.e. VGJSRL's own exit) · `$7ACA` VGWAI1 (tail-`BNE`, VGVCTR's own exit)
- Atari's header: "ENTRY `(VGLIST,VGLIST+1)` = VECTOR LIST ADDRESS. (Y) =
  VALUE+1 TO BE ADDED TO VECTOR LIST. EXIT `(VGLIST,VGLIST+1)` = NEW VECTOR
  LIST ADDRESS. USES A,`(VGLIST,VGLIST+1)`."
- Plain 16-bit add with carry into `VGLIST`/`VGLIST+1`; `X`/`Y` genuinely
  untouched (header's "USES A" is accurate — many other VGUT routines'
  headers over-claim `X,Y` because *they* clobber those before falling in).
- **Hazard**: two of its six callers reach it via a *tail* `BNE`/`JMP`
  instead of `JSR`/`RTS` — `VGJMP1`'s `BNE VGADD` at the end of
  `VGJSRL`/`VGJMPL`, and `VGWAI1`'s `BNE VGADD` at the end of `VGVCTR`. In
  both cases the branch is guaranteed taken (see next paragraph), and
  **`VGADD`'s own `RTS` is what returns to the original caller of
  `VGJSRL`/`VGVCTR`**, not to `VGJMP1`/`VGWAI1`. A routine-by-routine C
  translation must model `VGADD` either as a genuinely shared static
  function (fine) or must realise `VGJSRL`/`VGVCTR` "call" `VGADD` as their
  literal last action and return whatever it returns — there is no code to
  run after that in the 6502 original.
- The `VGWAI1` tail-branch is **not** a test of the just-stored intensity
  byte, despite `STA (VGLIST),Y` sitting right before it (`STA` never
  affects flags on 6502) — the `BNE` actually tests **`Y`** after the
  preceding `INY` (`Y` is a fixed small loop-position counter, 3 at that
  point, never 0), i.e. it is an unconditional branch disguised as a
  conditional one. Same shape as `VGJMP1`'s `BNE` (`ORA #$A0`/`#$E0`
  guarantees a non-zero, hence never-taken-false, accumulator). Neither is a
  bug, but both would read as "if not X then Y" branches to someone porting
  line-by-line without checking what's actually being tested.

### VGSCA1 / VGSCAL ($7A81 / $7A83) — L6942/L6948 — VGSCA1 callers (6): `$5013` MAIN · `$5D0B` D_SPCL · `$6CFA`,`$6D2B`,`$6D73`,`$6D90` (message scaling); VGSCAL callers (1): `$5CFF` D_SPCL
- Atari's header (on VGSCA1): "ENTRY (Y)=LINEAR SCALE FACTOR (0=FULL SIZE,
  FF=1/256 SIZE). (A)=POWER OF 2 SCALE FACTOR (0=FULL SIZE,1=1/2 SIZE,...).
  `(VGLIST,VGLIST+1)`=VECTOR LIST ADDRESS. EXIT
  `(VGLIST,VGLIST+1)`=NEW VECTOR LIST ADDRESS. USES A,X,Y`(VGLIST,VGLIST+1)`."
- `VGSCA1` just forces `Y=0` (full linear size) then falls into `VGSCAL`,
  which takes the caller's own `Y` as the linear factor. `D_SPCL` (§4) is
  the only caller that uses the `VGSCAL` entry directly, with a computed `Y`
  from the object's own scale byte.

### VGDOT ($7A8A) — L6963 — callers (4): `$589E` (mountains) · `$5C99` D_DOT · `$6C33`,`$6C38` (radar blips)
- Atari's header: "ENTRY `(VGLIST,VGLIST+1)` = VECTOR LIST ADDRESS. (A) =
  INTENSITY (0 = OFF, EQ = MAX, 20 = INCREMENT). EXIT
  `(VGLIST,VGLIST+1)` = NEW VECTOR LIST ADDRESS. (VGBRIT) = NEW INTENSITY.
  USES A,Y,`(VGLIST,VGLIST+1)`."
- `TAY; LDA #$00; TAX` (zero delta X/Y) then falls into `VGVTR`, which
  `STY VGBRIT` — confirms the header's "(VGBRIT) = NEW INTENSITY" exit
  claim. Draws a zero-length vector (a dot) at the given brightness.

### VGVTR ($7A8E) — L6980 — callers (11): `$501E` MAIN · `$507D` MAIN_15 · `$51E1` WNSHLD · `$5428`,`$54B2` (hi-score) · `$6CF5` MSTART (message positioning) · `$6D39`,`$6D64`,`$6D7C` (message digit spacing) · `$7E3B`,`$7E4B` (BZSTST)
- Atari's header: "ENTRY `(VGLIST,VGLIST+1)` = VECTOR LIST ADDRESS. (A) =
  CHANGE IN X/4 (-80 TO +7F). (X) = CHANGE IN Y/4 (-80 TO +7F). (Y) = VECTOR
  BRIGHTNESS (0,10,20,...,F0). EXIT `(VGLIST,VGLIST+1)` = NEW VECTOR LIST
  ADDRESS. (VGBRIT)=NEW VECTOR BRIGHTNESS. USES A,X,Y,`(VGLIST,VGLIST+1)`,
  `(XCOMP,XCOMP+3)`."
- "Short form" of `VGVCTR`: sign-extends the 8-bit `A`/`X` deltas into the
  16-bit `XCOMP` block `VGVCTR` expects, ×4 scaled (two left shifts each),
  then falls through `VGVTR1`→`VGVTR2`→`VGVCTR`.

### VGVTR1 / VGVTR2 ($7A90 / $7AAB) — L6986/L7013 — VGVTR1 callers (6 direct `JSR`, all BZSTST diagnostic-display positioning: `$7E77`,`$7E88`,`$7EB1`,`$7EF0`,`$7EFC`,`$7F12`) plus `VGVTR`'s own fall-through; VGVTR2 callers (6): `$57FF`,`$584D`,`$5891` (mountains) · `$5D3D` DOVECT · `$6B34` (radar) · `$6C2B` (tank-visible blip), plus `VGVTR1`'s own fall-through
- No boxed Atari header on `VGVTR1`; `VGVTR2`'s own header is just "POSITION
  BEAM (comment at the call)". Both are fall-through legs of `VGVTR`'s
  sign-extend-and-scale sequence *and* independently `JSR`-callable entry
  points. BZSTST's diagnostic screens call `VGVTR1` directly with the same
  `A`/`X` = change-in-X/4, change-in-Y/4 format `VGVTR` itself expects
  (verified: `$7E73`-`$7E77` loads `A=$82`/`X=$A7` with the source comment
  "`(src: LDA I,-504./4)`"/"`(src: LDX I,-358./4)`", the same `/4` convention)
  — `VGVTR1` still does the ×4 sign-extend-and-scale itself; the only thing
  it skips versus `VGVTR` is `STY VGBRIT` (so these callers rely on
  brightness already being set from an earlier call). `VGVTR2` is reachable
  directly too, by callers that already have `XCOMP` populated in full
  16-bit form, setting `X=<XCOMP` themselves before falling into `VGVCTR`.
  Three usable entry points into what looks like one routine.

### VGVCTR ($7AAD) — L7018 — **not called anywhere in this ROM directly** (reached only via `VGVTR`/`VGVTR1`/`VGVTR2` fall-through)
- Atari's header: "NOTE: IF THE NUMBERS GIVEN ARE MORE THAN 15 BITS IN
  SIGNIFICANCE THEN AN INCORRECT VECTOR WILL BE GENERATED. ENTRY (X) = 4
  ZERO PAGE LOCN. CONTAINING (X LSB,X MSB,Y MSB,Y LSB). `(VGLIST,VGLIST+1)`
  = VECTOR LIST ADDRESS. (VGBRIT)=VECTOR BRIGHTNESS (ONLY THE UPPER 3 BITS
  ARE USED). EXIT `(VGLIST,VGLIST+1)` = VECTOR LIST ADDRESS. USES
  A,X,Y,`(VGLIST,VGLIST+1)`."
- This is the actual `VCTR` opcode emitter: reads 4 bytes at `X`
  (`XCOMP` in every real caller) as (X-lo, X-hi, Y-hi, Y-lo — note the
  header's own ordering, verified against the `LDA $02,X`/`$03,X`/`$00,X`/
  `$01,X` sequence), masks each high byte to 5 significant bits (`AND #$1F`,
  matching the header's 15-bit-total warning: 2×(1 sign+4 magnitude) high
  bits + 2×8 low bits = the VCTR word format), and `EOR`s `VGBRIT`'s upper 3
  bits into the Y-MSB byte for the intensity field. Falls into `VGWAI1` to
  finish (§ VGADD above for the tail-branch hazard).
- 15-bit range check is **not enforced by the code** — it is a documented
  precondition only (the header's own "NOTE"), not something a C port gets
  for free; callers are responsible for keeping deltas in range.

---

## 2. Message and number display

Builds on VGUT's `VGHEX`/`VGHEXZ`/`VGADD2` (§1). The character-glyph lookup
table `VGMSGA` is provided by `VGAN.MAC` (included partway through BZONE.MAC
— see the vector-ROM listing's own module banner) and is common to both
number display (`VGHEX`) and free-text display (`DSPALP` below): both index
it by the character code ×2 to get a `JSRL`-into-the-glyph pair.

### DSPALP ($556A) — L852 (BZONE) — callers (4): `$5432` (hi-score digit) · `$54D7`,`$54E0`,`$54E9` (initials-entry alphabet display)
- No Atari header (`[note]`, written for this disassembly).
- Entry: `A` = character code. Looks up `VGMSGA,Y`/`VGMSGA+1,Y` (`Y=A`) and
  `JMP VGADD2` — a tail call, so **DSPALP's own "return" is VGADD2's `RTS`**,
  and DSPALP's exit state is exactly VGADD2's (§1: `(VGLIST,VGLIST+1)`
  advanced by 2, A/Y clobbered). Used to spell out player initials
  character-by-character, as opposed to `MSGS` below which plays back a
  whole pre-built message.

### MSGS ($6C98) — L5010 (BZONE) — callers (19, one instruction spelled out twice per site — `LDX #msgnum` then `JSR MSGS`, so 19 distinct message displays across the game)
- Header: "C OUTPUT BONUS MESSAGE (comment at the call)" — not a full
  Atari ENTRY/EXIT block; this is call-site-comment provenance.
- Entry: `X` = message number × 2 (an index, not a byte count — see `MSTART`
  below). Reads `OPTION`'s language-select bits (rotated into `Y=0,2,4,6`)
  and uses the push-address-then-RTS trick (§0) on `LNGTBL` to "return" into
  one of `ENGLISH`/`GERMAN`/`FRENCH`/`SPANSH` with `X` still holding the
  message index.
- **`MSGS` never itself returns to its caller** — it always ends by falling
  through the language dispatch into one of `ENGLISH`/`FRENCH`/`GERMAN`/
  `SPANSH` → `MSTART`, whose own `RTS` is what returns to `MSGS`'s caller.
  A C port can flatten this to one function (`msgs(x)` that switches on
  language internally) without needing the trampoline at all.

### ENGLISH / FRENCH / GERMAN / SPANSH ($6CCC/$6CB6/$6CC1/$6CAB) — L5056/L5037/L5046/L5028 — reached only from `MSGS`'s `LNGTBL` dispatch (not `JSR`-called)
- No Atari headers (`[note]`s). Each: `LDA LANGMSG,X; STA TBLPTR; LDA
  LANGMSG+1,X` (its own message-table base, e.g. `ENGMSG`/`FRNMSG`), then
  falls (`SPANSH`/`FRENCH`/`GERMAN`) or jumps (`ENGLISH`, since it's first
  in file order but last in `LNGTBL`, so needs an explicit `JMP`) into
  `MSTART`.

### MSTART ($6CD4) — L5061 — callers (3, all tail-falls from the four language legs above, no explicit `JSR`)
- No Atari header. Entry: `TBLPTR`/`TBLPTR+1` = the specific message's
  packed-text address (set by the caller leg above), `X` = the same message
  index the caller passed to `MSGS` (still live in `X`, untouched by the
  language dispatch). Positions the beam if the message record's first word
  is non-zero, sets scale from a table-driven size byte, then walks the
  packed character stream (7 bits/char + high bit = "last char" flag,
  matching `ASCVH`/`ASCVG`'s packing) emitting one `DSPALP`-style glyph per
  character via `VGMSGA`.
- Exit: `RTS` — this is the real return point for every `MSGS` call.

---

## 3. Maths helpers

### WAIT ($5B80) — L1907 (BZONE) — callers (7 explicit — `ROTPNT` ×2, `PNTPUT` ×2, `TRACK` (`TRACK_40`), `OBJOBJ`... — every Mathbox read in the game funnels through here)
- Atari's header: "WAIT: WAIT FOR 2901 MATH BOX & RETURN. LOW BYTE IN A."
- `BIT MSTAT; BMI WAIT` spins on the busy bit (D7/N flag of `MSTAT`), then
  `LDA MOUTLO; RTS`. **Undocumented but essential**: the caller must also
  read `MOUTHI` separately afterward for the high byte (every real call site
  does — e.g. `ROTPNT` `L5B25: LDA MOUTHI` right after `JSR WAIT`); `WAIT`
  itself only hands back the low byte in `A`, despite being the routine that
  "waits for the result." A C port's Mathbox stub does not need the busy
  loop, but any code copied line-by-line from a `WAIT` call site must not
  forget the follow-up `MOUTHI` read.
- In `PNTPUT` (`L5DC7`) and `SHRTCK`/`OBJOBJ`, an extra dummy
  `CMP ($00,X)` / `LDA ($00),Y` indirect instruction (commented "WASTE TIME"
  at `SHRTCK` `L5F8C`-`L5F8E`) is executed purely to burn cycles while the
  Mathbox finishes — not a real memory access (the operand address is
  whatever zero page happens to hold at that point; the *value* read is
  discarded). A C port replaces these with nothing (no Mathbox latency to
  hide) rather than translating the indirect load literally.

### SIN / SIN1 / COS ($5E4E/$5E63/$5E4B) — L2448/L2466/L2439 — SIN callers (11): `ROTATE` · `PNTPUT` (×2) · `SINXTR` (×2, fall-through + `XTRAP`-shared) · `EXPLDE` · `TR7CHK`-area (×2) · `ROTAT`-adjacent (`$6A8E`,`$6AFF`,`$6BCF`); COS callers (11), same shape
- Atari's header (on `COS`, covers both): "SIN & COS WILL ACCEPT 8 BITS OF
  ANGLE & RETURN 16 BITS OF SIN OR COS IN REGISTERS A & X (A IS LOW ORDER).
  ENTRY (A)=ANGLE (0 TO FF REPRESENTS 0 TO 360 DEGREES). (CC)=MINUS/PLUS
  FLAG SET CORRECTLY. EXIT (A)=LOW ORDER BYTE OF SIN/COS. (X)=HIGH \" \".
  REGISTERS USED: A,X."
- `COS(A) = SIN(A+$40)` (quarter-turn phase shift), a one-instruction
  `ADC #$40` then fall into `SIN`. `SIN` handles the sign (`BPL`/negate) and
  quadrant-folds (`SIN1`) into a 65-entry table.
- **Self-relative table lookup, not a plain symbol reference**: `SIN1`'s
  table read is `LDA $5E77,X` / `LDA $5E78,X` — the listing's own comment
  says `(src: LDA AX,99$)` / `(src: LDA AX,99$+1)`, meaning Atari's source
  indexed the table through a local numeric label (`99$`) relative to the
  routine, not by the table's own name (`SIN1_99` in this disassembly's
  naming). It is the *only* place in the whole program ROM this pattern is
  used (verified: no other `(src: LDA AX,...)` comment anywhere in the
  listing). A C port must use the table by its actual address/name
  (`SIN1_99`) — there is no equivalent "PC-relative" addressing to preserve.

### COSXTR / SINXTR / XTRAP ($5EF9/$5F17/$5F32) — L2513/L2535/L2552 — COSXTR callers (2): `$5979` ROTATE, `$62C4` (BUZBOM-area); SINXTR callers (2): `$5963` ROTATE, `$62E4`
- Atari's header (`COSXTR`): "WILL EXTRAPOLATE SIN & COS TO AN EXTRA 2
  DIVISIONS BETWEEN EACH ANGLE. ENTRY (X)=ANGLE. (A)=DIVISION OF ANGLE (0 TO
  1). EXIT (A)=LOW ORDER BYTE SIN/COS. (X)=HIGH \" \". GARBAG. REGS: A,X."
  (`GARBAG` — a scratch cell, not a typo; also separately reused as a
  "cheating" flag elsewhere in `ROTATE`, see `bzone_defines.asm`.)
- `SINXTR` has no separate boxed header (`[note]`, cross-referenced to
  `COSXTR`'s). Both compute `SIN`/`COS` of the angle and angle+1, then share
  `XTRAP`'s linear-interpolate-by-division-fraction tail. `XTRAP` itself has
  no header and is unreachable except by falling out of `COSXTR`/`SINXTR` —
  not a `JSR` target.
- Exit register convention differs subtly from `SIN`/`COS`: on the
  division-0 fast path (`AND #$01` result 0) it's a tail `JMP` into
  `SIN`/`COS` directly (exit state = theirs exactly); on the interpolated
  path it's `XTRAP`'s own `RTS` with `PLA`/`TAX` reassembling A/X from the
  stack — same nominal contract, different code path, worth knowing before
  assuming a single exit site to hook a C wrapper onto.

### TRACK ($6810) — L4241 (BZONE) — callers (7): `$50CF` MAIN_35 · `$6565`,`$6589`,`$65B6` (robot AI) · `$6656` (BUZBOM) · `$6ADB`,`$6B44` (radar-adjacent)
- Atari's header: "THIS ROUTINE COMPUTE THE VECTOR BETWEEN THE ROBOT TANK
  AND THE PLAYER CONTROLLED TANK, AND USING THE ARCTANGENT FUNCTION, RETURNS
  THE ANGLE TO DIRECT THE ROBOT TANK IN. INPUT PARAMS: TPOSX,TPOSY FOR BOTH
  TANKS. OUTPUT PARAMS: (A)=NEW ANGLE TO DIRECT ROBOT. TEMP1 =ABS VALUE(Y).
  TEMP2 =ABS VALUE(X). REGISTERS: A,X,Y. SUBROUTINES: NONE."
- "SUBROUTINES: NONE" undersells it: `TRACK` does call `ABSTMP` (twice,
  `L6821`/`L683E`) and `WAIT` (`TRACK_40`, via the Mathbox divide) — this is
  a genuine discrepancy between the header and the code, not a subtlety;
  it's simply wrong (or refers to an earlier draft with the math inlined).
- Computes `|Δx|`/`|Δy|` (via `ABSTMP`, §4), picks X/Y or Y/X division
  depending on which is larger (`TRACK_25`/`TRACK_28`/`TRACK_30`/`TRACK_40`),
  divides through the Mathbox, indexes `ARCTAN` (the vector-ROM lookup table
  at $3785, 128 entries — see `build/vector_tables.json`'s `misc_tables`)
  with the quotient, then uses `TTAB` (push-address-then-RTS, §0) to map the
  raw arctangent into the correct quadrant via `T_0`/`T_1`/`T_2`/`T_3`.

### T_0 / T_1 / T_2 / T_3 ($68D5/$68C6/$68CB/$68D0) — L4380/L4355/L4364/L4372 — reached only via `TTAB`'s push-address-then-RTS dispatch (§0), not `JSR`-called
- No Atari headers (`[note]`s). `T_1` negates (two's complement) then falls
  into `T_2`, which flips 180° (`EOR #$80`) then `JMP T_0`. `T_3` negates
  then falls into `T_0`. `T_0` is the shared exit: bare `RTS`, quadrant-0's
  own body (nothing to do) *and* `T_2`/`T_3`'s tail. Entry/exit convention:
  `A` = raw arctangent in, corrected angle out; `X`/`Y` untouched.

### OBJOBJ ($68D6) — L4397 (BZONE) — callers (5): `$5240` MAIN_51 · `$6271`,`$6503`,`$6514`,`$6714` (collision-adjacent)
- Atari's header: "OBJECT-OBJECT COLLISION DETECT. ... BLOCK NAME: OBJOBJ.
  ... INPUT: (X)=0 FOR PLAYER TANK, (X)=2 FOR ROBOT TANK. TPOSX,TPOSX
  [sic — repeated, presumably meant TPOSY],PTBLO1,PTBLX1,PTBLY1,PROXTB.
  OUTPUT PARMS: CARRY SET COLLISION, 0=NONTANK COLLISION, -1=TANK COLLISION
  IN(A). REGISTERS: DSTNCE. USES: TEMP1,TEMP2,TEMP3."
- Scans the (ROM) playfield-object proximity table `PTBLO1`/`PTBLX1`/
  `PTBLY1` (terminated by a negative `PTBLO1` byte), reads the Mathbox
  distance into `DSTNCE`, compares against a per-object-type proximity
  threshold `PROXTB`, and reports the first hit (or none). "REGISTERS:
  DSTNCE" in the header is odd phrasing (`DSTNCE` is a RAM cell, not a
  register) but matches the code: it's genuinely live output state, just
  not one of A/X/Y.
- Exit convention verified: carry set + `A=0` for a non-tank playfield
  object, carry set + `A=$FF`(-1) for a tank; carry clear = no collision.

### ROTPNT ($5B1F) — L1846 (BZONE) — callers (5): `ROTATE` ×1 (`$59C2`) · `M_10S_`-area ×3 (`$5AAF`,`$5AD8`,`$5B14`) · `$5C3A` (radar-object placement)
- Header: "CHECK VALUES PASSED IN X1 & Y1 USING PNTROT. PUT IN TABLE IF IN
  VIEW Y=INDEX TO RAM TABLE" (comment-derived, not a full ENTRY/EXIT/USES
  block — and it cites "PNTROT", a routine that doesn't exist anywhere in
  this ROM; almost certainly this disassembly's own `ROTPNT` under an
  even-older working name, not a missing subroutine).
- Two Mathbox rotations via `WAIT` (X then Y), then a view-frustum test
  (too far / too close / |Y|≥|X| i.e. outside the ~90° field of view) that
  discards the point (`BMI`/`BEQ`/`BCS` to `ROTPNT_20`) instead of writing
  it. Entry: `X1`(`$19`)/`Y1` implicitly via the Mathbox argument registers
  set by the caller before entry (not by `ROTPNT` itself — verified: no
  write to `ACOS`/`BSIN` inside `ROTPNT`, only reads of `MOUTHI`/`MOUTLO`
  after `WAIT`), `Y` = destination index into `PTBLX2`/`PTBLY2`. Exit: `Y`
  advanced by 2 if the point was kept, unchanged if discarded — the
  **caller must track "was a point written" itself** by comparing `Y`
  before/after, since there's no other kept/discarded flag.

---

## 4. Object / point-table routines

`ROTATE` (§0/§3, `$5946`) selects and transforms visible playfield objects
into `PTBLX2`/`PTBLY2`/`PTBLO2` via repeated `ROTPNT` calls; for each object
`MAIN_20`'s loop (`L50AA`-`L50BE`) then calls `PNTPUT` then `DRAW` in that
fixed order — `DRAW`'s opcode stream (below) reads positions `PNTPUT` just
computed. `ROTATE` itself has an Atari header ("SUBROUTINES: ROTPNT,EXPLDE",
both of which do exist and are called — unlike `TRACK`'s and `PNTPUT`'s
headers below, this one checks out).

### PNTPUT ($5D4A) — L2271 — caller (1): `$50B1` MAIN_20 (but this is the whole game's per-object projection step, run once per visible object per frame)
- Atari's header: "THIS ROUTINE WILL GENERATE THE POINTS USED TO DRAW AN
  OBJECT AND MOVE THEM TO THE POINT TABLE. INPUT PARAMS: THETA=ANGLE OF
  VIEW. X=POINTER TO CENTER POINT OF OBJECT. OUTPUT PARAMS: POINT TABLE
  (PNTTBL),TBLPTR,NOPNTS. SUBROUTINES: PNTROT,PROJPT,DIVIDE,KLUDGE.
  REGISTERS: A,X,Y."
- **Three of the four named "SUBROUTINES" don't exist in this ROM**:
  `PNTROT`, `PROJPT` and `DIVIDE` are not routine names anywhere in the
  listing (only `KLUDGE` is real and actually called, `L5DE8`). The actual
  implementation does the rotation/projection inline via `SIN`/`COS` and the
  Mathbox (`WAIT`), not through separate subroutines — this header describes
  an earlier design, not the assembled code. Same pattern as `TRACK`'s
  header above and `NMI`'s below (§7) — treat any Atari "SUBROUTINES:" list
  as a hint to verify, not a fact.
- Entry: `X` = object index into `PTBLO2`/`PTBLX2`/`PTBLY2` (the caller's
  `SAVE0`). Computes the relative bearing (`TANGLE` − the object's own
  orientation byte), `SIN`/`COS` of it into `BSIN`/`ACOS`, then walks the
  object's 3-D point table (found via `OBJPNT,Y` — the vector-ROM pointer
  array, see `build/vector_tables.json`) applying the rotation and a
  perspective divide (via `WAIT`) per point, calling `KLUDGE` for each
  point's Z-axis adjustment. Output: `PNTTBL` filled with 2-D deltas,
  `NOPNTS`-worth for this object.

### KLUDGE ($5E05) — L2373 — caller (1): `$5DE8` PNTPUT
- Atari's header: "WILL ADD APPROPRIATE Z OFFSET FOR EXPLOSION OBJECTS.
  INPUT PARAMS: DDEND,PTBLO2,EXPOSZ. OUTPUT PARAMS: DDEND. REGISTERS: A,X."
- Only touches `DDEND` if the object's `PTBLO2` byte has bit 4 set
  (explosion-piece flag); otherwise a no-op fall to `KLUDGE_20` (not shown
  above, but confirmed by the `BEQ KLUDGE_20` at entry). Named "kludge" by
  Atari themselves, not this disassembly — it is exactly what it sounds
  like: a special-case patch bolted onto the general point-projection path
  for one object type.

### DRAW ($5C5C) — L2068 — caller (1): `$50B9` MAIN_20 (the same per-object frame loop as `PNTPUT`)
- Atari's header: "DRAW WILL BUILD VECTOR LIST FOR ANY OBJECT FROM VALUES IN
  TABLES PNTTBL AND OBJTBL. INPUT PARAMS: A=OBJECT #, SAVE0=OBJECT #
  POINTER. OUTPUT PARAMS: NONE. SUBROUTINES: VGHALT,VGWAIT,VGLABS,VGVCTR,
  NEWCUR. REGISTERS: A,X,Y." — **also stale**: `VGWAIT`, `VGLABS` and
  `NEWCUR` are not routine names in this ROM either (the real callees below
  DRAW's dispatch are `VGDOT`/`VGSTAT`/`VGCNTR`/`VGVTR2`/`VGSCAL`/`VGSCA1`/
  `VGJSRL`, none of which match those three names — most likely
  `VGVTR2`≈"VGVCTR" already listed correctly, and `VGWAIT`/`VGLABS`/
  `NEWCUR` are older names for what became `DOVECT`'s `VGVTR2` call and
  `D_LABS`'s `VGCNTR` re-centre).
- Entry: `A` = object #. Looks up the object's edge-list address in
  `OBJTBL,Y` (`Y=A*2`) into `TBLPTR`, then loops (`D_TOP`) fetching one
  opcode byte at a time until a `$FF` terminator, dispatching through
  **`DTAB`** — the push-address-then-RTS trick (§0) — on the opcode's low 3
  bits (bits 3-7 are saved to `TEMP1` as a point-table index/value first).
  `D_TOP_11` (`$5C71`-`$5C83`) *is* the dispatcher: `TAX`/`AND`/`STA`/
  `TXA`/`AND`/`ASL`/`TAX` then the `LDA DTAB+1,X` / `PHA` / `LDA DTAB,X` /
  `PHA` / `RTS` sequence.

### DTAB opcodes — D_DOT/D_SBRT/D_LABS/D_BVCT/D_VCTR/D_SPCL/D_SLIM ($5C92/$5C9F/$5CD4/$5CE1/$5CE5/$5CED/$5D0E) — L2105/L2117/L2156/L2168/L2176/L2188/L2211 — all reached only via `DTAB`'s dispatch, never `JSR`-called directly
- No Atari headers (all `[note]`s — this disassembly's own reading of an
  undocumented computed-goto table). All seven share the loop-continuation
  label `D_NEXT` (below) as their common "return".
- **Fall-through chain, not independent handlers**: `D_LABS` (opcode 3,
  reset beam) falls straight into `D_BVCT` (opcode 2, blank vector) after
  re-centring — so opcode 3 always does opcode 2's work too, unconditionally.
  `D_BVCT`/`D_VCTR` (opcodes 2/4) share a tail `D_VCT1` via `BEQ`/fall-in,
  differing only in the brightness constant loaded first (`$00` blank vs.
  `$20` visible). `D_SPCL` (opcode 5, explosion picture via `VGJSRL EXPIC`)
  falls straight into `D_SLIM` (opcode 6, a no-op placeholder that just
  `JMP D_NEXT`) — meaning opcode 5's *last* action is always opcode 6's
  single instruction, harmless here but a trap if a future editor inserts a
  new opcode 6 body expecting it to run standalone.
- `D_DOT` (dot) and `D_SBRT` (opcode 1, special brightness — logo/saucer use
  `SINT` directly, everything else uses depth-faded `TEMP1-DQUE` clamped to
  `$30`) both end with an explicit `JMP D_NEXT` rather than falling through,
  since they're not adjacent to `D_NEXT` in the table's declared order.

### DOVECT ($5D11) — L2219 — callers (2): `$5C94` D_DOT · `$5CE7` D_VCT1 (shared by D_BVCT/D_VCTR)
- No Atari header (`[note]`). Entry: `A` = brightness (stored to `VGBRIT`).
  Computes the delta from `CURNTX`/`CURNTY` (the beam's last drawn position)
  to the next `PNTTBL` point (indexed by `TEMP1`, set by `D_TOP_11`'s
  dispatcher before the opcode handler ran), updates `CURNTX`/`CURNTY` to
  the new position, and calls `VGVTR2` (§1) to emit the actual vector.
  Exit: plain `RTS` (not shared with `D_NEXT` itself — each `D_xxx` opcode
  handler does its own `JMP D_NEXT` after calling `DOVECT`).

### D_NEXT ($5D41) — L2252 — reached (4 explicit `JMP`s, from D_DOT/D_SBRT/D_BVCT-or-D_VCTR/D_SLIM-or-D_SPCL) — the shared loop-continuation for every DTAB opcode
- No Atari header. 16-bit increment of `TBLPTR` (`INC`/`BNE`/`INC` — the
  classic zero-page pointer bump with carry check) then `JMP D_TOP` to fetch
  and dispatch the next opcode byte. This is `DRAW`'s actual loop back-edge;
  the `$FF` terminator test lives in `D_TOP` itself, not here.

### QWIKCK / SHRTCK ($5F4E / $5F5B) — L2584/L2599 — QWIKCK callers (2): `$51B0` B_END_30 · `$670D` (BUZBOM-area); SHRTCK callers: 1 explicit `JSR` (`$6059`) **plus** QWIKCK's own fall-through (`QWIKCK_10`, `L5F57`-`L5F59` sets `X=0,Y=2` then falls in with an explicit "FALL THROUGH" comment) — so SHRTCK effectively has 3 call paths, only 1 of them a real `JSR`
- Atari's header (spans both): "QWIKCK DOES A QUICK COLLISION CHECK FOR
  PLAYER'S SHELL VS. ROBOT ONLY. SHRTCK DOES THE SAME CHECK, BUT FOR EITHER
  PLAYER'S SHELL VS. ROBOT, OR FOR ROBOT'S SHELL VS. PLAYER. SHRTCK REQUIRES
  X AND Y TO BE SET UP PROPERLY. CC - NO COLLISION. CS - CLLISION [sic] --
  APPROPRIATE FLAGS & COUNTERS ARE SET."
- `QWIKCK`: if there's no player shell in flight (`FIRECT` zero/negative)
  jumps straight to the shared "no collision" exit `Q_20S_`; otherwise
  hardwires `X=0` (player), `Y=2` (robot's shell-related index) and falls
  into `SHRTCK` — i.e. `QWIKCK` is really just "`SHRTCK` with fixed
  arguments," matching the header's own framing exactly.
- `SHRTCK`: entry `X`/`Y` per the header (whose-shell / whose-target index
  pair — verified against `SHELLX,X`/`SHELLY,X` and `TPOSX,Y`/`TPOSY,Y`
  reads). Uses several **forced-absolute** reads of sub-`$100` cells
  (`COLFLG,Y`, `TPOSX,Y`, `TPOSX+1,Y`, `TPOSY,Y`, `TPOSY+1,Y` — all emitted
  as raw `.byte $B9,lo,hi` because `Y`-indexed absolute addressing of a
  page-0 address isn't zero-page-eligible in this assembler's own encoding
  choice, see the listing's own convention note) rather than the more usual
  zero-page,Y or symbolic form. A Mathbox distance/divide via `WAIT`
  (including a "WASTE TIME" dummy indirect read, §3) computes proximity,
  compared against a radius derived from `TANGLE`; on collision, sets
  `COLFLG,Y` (again forced-absolute), and does the two-limb **decimal-mode**
  `HITS`/`HITS+1` add (§0) crediting whoever's `TEMP1` held the attacker
  index. Exit: `CC`/`CS` exactly as documented, verified — both paths reach
  either `Q_20S_` (`CLC; RTS`, shared with `QWIKCK`'s early-out) or fall out
  the bottom into the caller with `CS` set from the final `CMP`.

---

## 5. Sound

### SNDON ($7985) — L6571 (BZSOUN) — callers (8): `$5373` MAIN_53 (super-bonus tune) · `$55E1` NMI_11 · `$561E` NMI_60 (slam alarm) · `$6108` SAUCHK · `$6182` NEWLIF_10 · `$627F` (object-collision "boing") · `$6BCA` DRADAR_36 (radar beep) · `$6C57` (enemy-in-range warning)
- Header: "[note] Start a sound effect: A = the effect's SNDWRD bit; a
  bit-scan loop (ASL/DEX) finds its 0-7 index, ... sets that channel's
  POINT/FRAMES/COUNT so MODSND starts playing it next frame." Written for
  this disassembly (no boxed Atari header on `SNDON` itself, unlike VGUT).
- Entry: `A` = the sound's bit within `SNDWRD` (a bitmask constant per
  effect, e.g. `SUPBON`/`SAUSND`/`DISINT`/`BOING`/`RBEEP`/`WARNG`). Pushes
  `A`/`X`/`Y`, then does `TSX; LDA $0103,X` — reading 3 bytes above the
  post-push stack pointer, i.e. the byte that was on the stack at the
  instant `SNDON` was entered (before its own 3 pushes), which is normally
  whatever the *previous* subroutine's stack frame left there. **This is not
  a reliable caller-supplied parameter**: only one of the eight call sites
  (`$561B`, just before `NMI_60`'s call) does an explicit `PHA` right before
  `JSR SNDON` and a matching `PLA` right after — the other seven call it
  with no stack setup of their own. If that peeked byte is zero, `SNDON`
  **skips the bit-scan and uses `X` (the caller's own, untouched `X`
  register) directly as the sound index** instead of decoding it from `A`.
  This disassembly could not establish a controlled purpose for this
  branch — treat it as unreliable/coincidental behaviour rather than a
  documented fast path, and **do not port it literally**; a C `sndon(int
  bit)` that always does the bit-scan reproduces the behaviour every real
  call site actually exercises.
- Exit: no meaningful return value; `A`/`X`/`Y` restored to entry values via
  the matching `PLA`/`TAY`/`PLA`/`TAX`/`PLA` (so, contrary to the bit-scan
  possibly clobbering them internally, the *caller* sees no register
  changes at all).

### MODSND ($79B9) — L6613 (BZSOUN) — caller (1): `$55BF` NMI (once per NMI, i.e. 4×/frame — see §7)
- Atari's header: "SOUND ROUTINE. CONTINUES A PREVIOUSLY STARTED SOUND.
  WHEN CHANNEL 1 GOES IDLE, ALL SOUND ENDS."
- Entry: none (operates on the fixed 4-channel `POINT`/`FRAMES`/`COUNT`/
  `CURRENT` state `SNDON` set up). Per channel (`X=3..0`), counts down
  `FRAMES`; at zero, advances to the next envelope step from the
  `SOUND_7886`-based sequence table (offset by `STVAL`/`FRCNT` macros, see
  `BZSOUN.MAC`'s own "EACH TABLE ENTRY IS 4 BYTES..." header comment quoted
  in the module banner) and pokes `AUDF1,X`/etc. Channel 0 going idle
  (`FRAMES=0` and `POINT=0`) is specifically what shuts off *all* sound
  (`MODSND_15`'s "cheap way to test if this is ch 1" comment, `L79D9`) —
  not documented in the header beyond its one-line summary, but verified
  against the code (`TXA; BNE MODSND_30` — only channel index 0 takes the
  idle-value path that also resets `POINT`).
- Exit: no register contract; called for effect only, once per NMI.

### The `$1840` discrete-sound latch (`SOUND`)
- `SOUND` ($1840, write-only, `[note]` in `bzone_defines.asm`: "explosion/
  shell/engine-rumble trigger bits, decoded outside the 6502 program" — i.e.
  a hardware bit-latch feeding discrete audio circuitry, not a POKEY/table
  effect like `SNDON`/`MODSND` drive).
- Written from five sites, three of them inside `NMI` itself
  (`NMI_56`/`NMI_70`/`NMI`'s coin-start block — `L5639`,`L565C`,`L5694`) and
  two in BZSTST's self-test/diagnostic display (`L7B2B`,`L7F90`,`L7FA4`
  — three, not two; see the listing). The NMI writes are built up bit by
  bit across `NMI_18`-`NMI_56` (rumble on attract-mode transitions, slam
  alarm, license/LICON display flicker, engine-idle flash) into a single
  accumulator value that's stored to `SOUND` exactly once per NMI at
  `NMI_56`/`NMI_70` — **not** written incrementally per bit, so a C port
  should accumulate the same way and do one final register write per frame
  tick, matching hardware's expectation of a coherent latch value rather
  than a stream of partial ones.

---

## 6. Coin routine (`MOOLAH`, from `TC65.MAC`/`COIN65`)

### MOOLAH ($56C3) — L1101 (BZONE, included from TC65.MAC) — caller (1): `$55AC` NMI, once per NMI (§7)
- Header (module-level, `TC65`/`COIN65`'s own boilerplate, not
  Battlezone-specific): "MOOLAH - DETECT COIN. IF YOU USE CMZP=0, DEFINE
  EQUIVALENT MACROS ... The coin routine assumes the presence of the
  following .GLOBL variables ... INSTRUCTIONS IN BRACKETS ARE FOR
  ILLUSTRATION ONLY, AND ARE NOT ACTUALLY ASSEMBLED." (Generic COIN65
  library documentation; the bracketed illustrative fragments genuinely
  don't appear in the assembled ROM, confirmed against `build/srclines.json`
  — this is *expected*, not a discrepancy, since the header says so itself.)
- Entry: none (reads coin-mech switches `S_COINA`/`S_LAM` and self-test
  timing state directly). `LDX #$02` then falls into `S_DETCT`, which
  processes one coin mech per call... except it doesn't loop externally —
  `S_DETCT`'s own internal logic (not shown in full here) advances `X`
  itself across all three mechs (left/center/right) in one `MOOLAH` call,
  debouncing each coin switch over several NMI ticks via the down-counter
  in `S_CNSTT`.
- Runs entirely in **binary mode** — confirmed no `SED` anywhere in
  `MOOLAH`/`S_DETCT`'s ~120-instruction body — which is exactly why `NMI`
  (§7) explicitly `CLD`s before calling it (§0): a stray decimal-mode
  carry-into-BCD from earlier code could otherwise corrupt the coin
  debounce counters' arithmetic.
- Exit: no register contract documented or apparent; writes `S_S_CRDT`
  (credit count), `S_LMTIM`/`S_PSTSL` (slam timers) and similar coin-state
  cells as side effects only.

---

## 7. The NMI handler and its callees

### NMI ($558F) — L886 (BZONE) — the 6502 NMI vector (`.VCTRS` at $7FFA); not `JSR`-called by anything, fires on the AVG's own interrupt line
- Atari's header: "BLOCK NAME: NMI. DESCRIPTION: INCREMENTS INTERRUPT
  COUNT, MAINTAINS SYNC, HANDLES COINS, KEEPS VECTOR GENERATOR GOING.
  SUBROUTINES: MOOLAH,MODSND,SNDON,RDBCHK."
- **`RDBCHK` does not exist anywhere in this ROM** — no label, no
  reference, nothing. The only routine `NMI` actually calls that has a
  similar name is `ROBCHK` ($69BE, "ROBOT RESET CHECK" — called from `NMI`
  at `L567E`, and independently from mainline at `$525C`/`$5C55`, so it is
  genuinely shared between NMI and mainline, see below). Given how close
  the letters are, this is almost certainly Atari's header citing an old
  name for `ROBCHK` that was never updated, not a missing routine — but
  this disassembly can't prove that from the source alone, so it's flagged
  rather than silently "corrected."
- Entry: none (interrupt). First action: `PHA; TXA; TSX; BMI NMI_2` — a
  **stack-corruption guard**: if the stack pointer (via `TSX`) has gone
  negative (i.e. `SP < $80`, meaning less than half the page remains, N set)
  the handler skips the interrupt-count/sync bookkeeping entirely and loops
  at `NMI_2` (`BMI NMI_2`) — **spinning forever if the stack is genuinely
  blown**, rather than trying to recover. A C port has no equivalent
  failure mode to model (no shared 256-byte hardware stack to overflow) but
  should note that the original hardware relied on this NMI-reentrancy
  guard as its last line of defence against runaway recursion/stack leaks
  elsewhere in the game.
- `CLD` immediately after (comment "INSURE WE DON'T SCREW UP COINS IN
  BINARY", §0) — defends `MOOLAH`'s binary-mode assumption against any
  mainline code interrupted between its own `SED` and `CLD` (the four
  `HITS`-update sites in §0 are exactly the risk this guards against, since
  none of them disable interrupts around their `SED`/`CLD` bracket).
- Calls, in order: `MOOLAH` (§6) unconditionally every NMI tick; `MODSND`
  (§5) unconditionally; `SNDON` (§5) conditionally, picking one of several
  sound-effect constants by priority (super-bonus lockout → object-collision
  → saucer-present); on a coin-start detected (`ALLPOT AND #START`),
  `ROBCHK` — **code genuinely shared between the interrupt handler and
  mainline**: `ROBCHK` has no special "called from NMI" mode, so whatever
  RAM it touches (robot position/state reset) must already be safe to touch
  from either context, or must not itself be interrupted by another NMI
  while running (it isn't re-entrant-guarded itself — relies on NMI's own
  stack-blitzed check above as the only protection). Finally maintains the
  double-buffered vector-list swap (`VECRAM+1` bit toggle) and re-arms the
  AVG (`GOADD`/`VTIMER`) once every ~24 ms.
- Exit: `PLA/TAY; PLA/TAX; PLA; RTI` — standard register restore. No
  meaningful "return value"; runs purely for side effects on shared state.

### ROBCHK ($69BE) — L4550 (BZONE) — callers (3): `$525C` MAIN_1 (mainline) · `$567E` NMI_65 (interrupt) · `$5C55` MAIN_20-adjacent (mainline)
- Header: "ROBCHK - ROBOT RESET CHECK" (comment-only, not a full Atari
  ENTRY/EXIT/USES block).
- The one routine in this document genuinely called from **both** the NMI
  handler and ordinary mainline code (as opposed to `MOOLAH`/`MODSND`/
  `SNDON`, which are NMI-only, or everything in §1-§4, which are
  mainline-only). A C port that puts NMI-handler logic on a separate
  "thread"/callback from the main frame loop must make sure `ROBCHK`'s own
  state (and everything it touches) is safe to call from both, or must
  serialize the two call paths — the original relies on the 6502 hardware
  interrupt actually stopping mainline execution while it runs, which a
  ported architecture may not get for free.

---

## Hazards summary (with addresses)

For quick reference when translating routine-by-routine:

| hazard | where |
|---|---|
| Fall-through chains across labelled "routines" | VGUT §1 (whole module, $7A19-$7AC9); TTAB's T_1→T_2, T_3→T_0 ($68C6-$68D5); DTAB's D_LABS→D_BVCT ($5CD4-$5CE1), D_BVCT/D_VCTR shared tail ($5CE1-$5CEA), D_SPCL→D_SLIM ($5CED-$5D0E) |
| Push-address(-1)-then-RTS computed dispatch | `DTAB` $5C84 (DRAW, §4); `TTAB` $68BE (TRACK, §3); `LNGTBL` $6C90 (MSGS, §2) |
| Tail `JMP`/`BNE` = "callee's RTS returns to my caller" | `VGADD` reached from `VGJMP1`/`VGWAI1` tails ($7A58,$7ACA, §1); `DSPALP`→`VGADD2` ($5571, §2); `SPLIT`-style pattern not present here but same shape at `MSGS`→language-leg→`MSTART` ($6C98→…→$6CD4, §2) |
| Branch tests stale/unrelated flags, not the value it looks like it tests | `VGWAI1`'s `BNE VGADD` tests `Y` (a fixed counter) not the just-stored intensity byte ($7AC7-$7ACA, §1); `VGJMP1`'s `BNE` is unconditional by construction (`ORA #$A0`/`#$E0` never zero) |
| Self-relative/local-label table addressing, not a plain symbol | `SIN1`'s table read, `(src: LDA AX,99$)` at $5E6D/$5E71 (§3) — the only such case in the program ROM |
| Decimal mode (`SED`/`CLD` bracketed) | `HITS`/`HITS+1` BCD add: $6011-$601A (SHRTCK), $6127-$6130 (SAUCHK_30), $668E-$6691 (BUZBOM), $675A-$675D; bonus lookup $554D-$5550 (DISTBL); `NMI`'s defensive `CLD` at $55A3 (§0/§7) |
| Forced-absolute (raw `.byte`) addressing of sub-$100 RAM via `,Y` | `SHRTCK` $5F6F,$5F74,$5F7A,$5F80,$5F86,$5FDE (§4) |
| Dummy/"waste time" instructions for hardware timing, not real memory ops | `SHRTCK` $5F8C-$5F8E (§3/§4); similar pattern in `PNTPUT`/`OBJOBJ`'s own Mathbox waits |
| Stack-peeked "parameter" not supplied by most callers | `SNDON`'s `LDA $0103,X` at $798B (§5) — only 1 of 8 call sites sets it up |
| Code shared between the NMI handler and mainline | `ROBCHK` ($69BE), called from both $567E (NMI) and $525C/$5C55 (mainline) (§7) |
| NMI's own re-entrancy/corruption guard | stack-pointer sign check at $5591-$559F (`TSX; BMI`) (§7) |
| Atari header cites subroutines that don't exist in this ROM | `NMI`'s "RDBCHK" (header at L880, almost certainly `ROBCHK`, §7); `PNTPUT`'s "PNTROT,PROJPT,DIVIDE" (header at L2261, §4); `DRAW`'s "VGWAIT,VGLABS,NEWCUR" (header at L2059, §4); `TRACK`'s "SUBROUTINES: NONE" despite calling `ABSTMP`/`WAIT` (header at L4227, §3) |
| Documented routine never actually called (dead code in this game) | `VGRTSL` $7A19, `VGJMPL` $7A49, `VGSTA1` $7A61 (all VGUT.MAC library routines Battlezone doesn't exercise, §1) |
| Comment on a hardware equate names the wrong bit | `HALT`/`$COINA`/`$LAM` ($0800): equate comment says "D0=1 FOR HALTED", code tests bits 6/7 via `BIT` everywhere (`bzone_defines.asm`'s own `[note]`, cross-referenced here since `BIT HALT` gates BZSTST's diagnostic-display polling loops, e.g. $7C1E,$7C23,$7CBC,$7CC1,$7CD4,$7CD9,$7E12,$7E17) |
