# The Mathbox

Battlezone's 3D math — rotation, projection, distance, the view-window
clip — runs on a separate coprocessor board: four AMD 2901 bit-slice ALU
chips driven by their own 256-word x 24-bit microcode ROM, addressed by
the 6502 as 32 I/O registers at `$1860-$187F`. This is what `MATHBOX_NOTES`
exists to document: what the game asks it to do, and what a C model has
to reproduce bit-for-bit to keep the game's math trustworthy.

Sources: `battlezone-main\MBUDOC.DOC` (Atari's own Mathbox documentation),
`battlezone-main\MBUCOD.V05` (the actual 2901 microcode source),
`battlezone-main\MBDIAG.MAC` (the 6502-side diagnostic, linked into the
game's self test), and `battlezone-main\BZONE.MAC` (how the game actually
uses it). RT-11 NUL padding was stripped before reading. All three
`.DOC`/`.V05`/`.MAC` files were read in full.

## It is the same Mathbox as Tempest's (verified byte for byte)

The board is not merely similar to Tempest's - it is the same microcode
in the same PROMs, under different part numbers:

| check | result |
|---|---|
| `MBUCOD.V05` (microcode source) | byte-identical in `battlezone-main`, `red-baron-main` and the Tempest archive (`tempest-main`) |
| `MBUDOC.DOC` | byte-identical in the Battlezone and Tempest archives |
| mapping PROM (32 bytes) | BZ `036174-01.b1` = Tempest `136002-126.a1`, CRC32 `8B04F921` |
| microcode PROMs (6 x 256x4) | BZ `036175..036180` = Tempest `136002-132..127` (reverse order), CRC32 `2AF82E87 B31F6E24 8119B847 09F5A4D5 823B61AE 276EADD5` |
| AVG state PROM, for reference | BZ `036408-01.k7` = Tempest `136002-125.d7`, CRC32 `5903AF03` |

Consequence for the C port: the Tempest port's
`C:\Source2026\Tempest Dissasembly\c_src\mathbox.c` / `mathbox.h` (with its
`mbprom.c` PROM data) can be reused unchanged. It does not transcribe the
functions; it interprets Albaugh's real microcode on a model of the four
2901 slices, so whatever the board computes - the truncations, the 3/8
distance, the clip bisection - comes out bit for bit without having to be
re-derived here. Its two flagged documentation gaps (C4/OVR after
EXOR/EXNOR; the Q0 latch when Q0 is not driven) are listed in that file's
header. Only the bus addresses differ:

| | Tempest | Battlezone |
|---|---|---|
| status read (`mb_status`) | `$6040` | `$1800` (`MSTAT`/`MBSTAT`) |
| result low / high (`mb_ylow`/`mb_yhigh`) | `$6060` / `$6070` | `$1810` / `$1818` (`MOUTLO`/`MOUTHI`) |
| command/data write (`mb_write(offset)`) | `$6080+n` | `$1860+n` |

The model runs each write's microcode to the STALL inside the write, so
the busy bit always reads "done"; Battlezone's `WAIT` loops then fall
through at once. The sections below remain useful as the description of
WHAT the game asks for, and `MBDIAG.MAC`'s table remains the unit test.

## The interface as the 6502 sees it

Three registers outside the command block:

| Address | Direction | Name in `BZONE.MAC` | What |
|---|---|---|---|
| `$1800` | R | `MSTAT`/`MBSTAT` | Status. Bit 7 = busy (see below) |
| `$1810` | R | `MOUTLO`/`YLOW` | Result, low byte |
| `$1818` | R | `MOUTHI`/`YHIGH` | Result, high byte |

And 32 write-only registers at `$1860-$187F` (`MBOXIN` base,
`BZONE.MAC:341`), documented as "MATH-BOX is assigned 32 addresses on the
aux board of an X-Y game" (`MBUDOC.DOC:23`). The low 5 bits of the address
select one of the 32; a write there both loads an operand byte into the
2901 register file *and* dispatches a microcode routine, because those
same 5 address bits (6502 A0-A4) directly address a 32 x 8 mapping PROM
(`036174-01`, confirmed in `HARDWARE_NOTES.md`'s ROM table) whose output
is loaded into the Mathbox's own microprogram counter on every write:

> During this write, the contents of the MAPPING ROM, addressed by 6502
> A0-A4, are loaded into the microPC. Thus, a write both presents data to
> the 2901's and initiates the execution of a uRoutine.
> — `MBUDOC.DOC:200-202`

That mapping PROM is assembled directly in `MBUCOD.V05` at `.=8400`
(`MBUCOD.V05:683`), so its 32-byte table is source-verifiable, not just
documented prose (see the register map below, cross-checked entry by
entry against that table).

### Register/command map (offsets from `$1860`)

| Offset | Addr | Name (doc) | Function | Cycles | Cite |
|---|---|---|---|---|---|
| $00 | $1860 | AL* | write A low | — | `MBUDOC.DOC:28` |
| $01 | $1861 | AH* | write A high | — | `MBUDOC.DOC:29` |
| $02 | $1862 | BL* | write B low | — | `MBUDOC.DOC:31` |
| $03 | $1863 | BH* | write B high | — | `MBUDOC.DOC:33` |
| $04 | $1864 | EL* | write E low | — | `MBUDOC.DOC:34` |
| $05 | $1865 | EH* | write E high | — | `MBUDOC.DOC:35` |
| $06 | $1866 | FL* | write F low | — | `MBUDOC.DOC:36` |
| $07 | $1867 | FH* | write F high | — | `MBUDOC.DOC:37` |
| $08 | $1868 | XL* | write X low | — | `MBUDOC.DOC:38` |
| $09 | $1869 | XH* | write X high | — | `MBUDOC.DOC:39` |
| $0A | $186A | YL* | write Y low | — | `MBUDOC.DOC:40` |
| $0B | $186B | YHSM | write Y high, **start multiply**: `out = (x-e)*a - (y-f)*b` | 54-59 | `MBUDOC.DOC:41-43` |
| $0C | $186C | NL | write N (divide/clip iteration count) | — | `MBUDOC.DOC:44` |
| $0D | $186D | ZLL* | write dividend Z, byte 0 (lowest) | — | `MBUDOC.DOC:45` |
| $0E | $186E | ZLH* | write dividend Z, byte 1 | — | `MBUDOC.DOC:46` |
| $0F | $186F | ZHL* | write dividend Z, byte 2 | — | `MBUDOC.DOC:47` |
| $10 | $1870 | ZHH* | write dividend Z, byte 3 (highest) | — | `MBUDOC.DOC:48` |
| $11 | $1871 | YHSP | write Y high, **start "whole mess"**: `out = (xb+ya+f)/(xa-yb+e)` | 119-131+5N | `MBUDOC.DOC:49-51` |
| $12 | $1872 | SYM | any write, **start Y' multiply**: `out = (x-e)b + (y-f)a` | 50-55 | `MBUDOC.DOC:52-54` |
| $13 | $1873 | SYXD | any write, **start Y'/X' divide** | 11-13+5N | `MBUDOC.DOC:55` |
| $14 | $1874 | SZXD | any write, **start Z/X' divide** | 10-12+5N | `MBUDOC.DOC:56` |
| $15 | $1875 | XPL* | write X' low | — | `MBUDOC.DOC:57` |
| $16 | $1876 | XPH* | write X' high | — | `MBUDOC.DOC:58` |
| $17 | $1877 | DXP | any write, **X' -> output** | — | `MBUDOC.DOC:59` |
| $18 | $1878 | DYPL | any write, **Y' low 16 -> output** | — | `MBUDOC.DOC:60-61` |
| $19 | $1879 | DYPH | any write, **Y' high 16 -> output** | — | `MBUDOC.DOC:62-63` |
| $1A | $187A | YPL | write Y' low | — | `MBUDOC.DOC:64` |
| $1B | $187B | YPH | write Y' high | — | `MBUDOC.DOC:65` |
| $1C | $187C | YHSC | write Y high, **start clip** (undocumented in prose; see below) | — | `MBUDOC.DOC:66` |
| $1D | $187D | DSTNCE | **start distance** (absent from `MBUDOC.DOC`'s table entirely — see below) | — | inferred, `MBUCOD.V05:712` |
| $1E | $187E | DST2 | second distance entry point (also absent from the doc) | — | inferred, `MBUCOD.V05:713` |
| $1F | $187F | — | unused | — | `MBUDOC.DOC:67` |

`*`-marked registers echo their own just-written 16-bit value back through
`YLOW`/`YHIGH` on the *next* status-clear read — Atari's own words: *"WRITEing
to these addresses will cause all 16 bits of the just-modified register to
be present at the output, where they can be read (at YLOW and YHIGH) to
verify correct operation. This sort of paranoia is probably useful only in
self-test."* (`MBUDOC.DOC:70-73`). `MBDIAG.MAC`'s test table exercises this
directly, e.g. writing `$7F` to AH and checking both low(`$00`)/high(`$7F`)
readback (`MBDIAG.MAC:116-117`).

**Offsets `$1D`/`$1E` are not in `MBUDOC.DOC`'s table at all** — the doc
calls `$1D-$1F` "currenty [sic] unused" (`MBUDOC.DOC:67`). `DSTNCE` and `DST2`
were added later (see revision history below); their offsets are inferred
by counting the mapping-PROM table's 32 sequential entries in
`MBUCOD.V05:690-713`, which line up exactly with the documented table
through `$1C` and then place `DSTNCE` and `DST2` in the two "unused"
slots. This is a well-supported inference from the source, not a directly
labelled fact.

### Register file

The named registers (A, B, E, F, X, Y, N, X', Y', Z) are the sixteen
4-bit-per-slice general-purpose registers inside the four 2901s — a
16 x 16-bit scratchpad, standard 2901 architecture. `MBUCOD.V05` assigns
them register numbers 0-11 (`RA=0, RB=1, RE=2, RF=3, RX=4, RY=5, RN=6,
RXP=7, RYP=8, RYPL=9, RZL=A, RZH=B`, `MBUCOD.V05:404-415`) plus four temp
registers `T0-T3` at 12-15 (`MBUCOD.V05:416-419`) — all 16 addressable
2901 registers are in use. No separate external scratchpad RAM chip is
mentioned anywhere in `MBUDOC.DOC` or `MBUCOD.V05`; nothing beyond the
2901's own register file was found.

### Status / busy-wait handshake

`MBUDOC.DOC:27` states plainly: status is at offset 0, **bit 7 = 1 means
busy**. The Mathbox's own clock is what stalls: *"The clock is stopped at
the completion of any instruction which has STALL=1. It is re-started...
by the 6502 writing to one of the 32 addresses"* (`MBUDOC.DOC:192-193, 199`) —
the busy bit is a direct reflection of that clock-stopped state.

`BZONE.MAC`'s own poll is a tight spin loop, nothing fancier:

```
WAIT:   BIT MSTAT       ;D  DO WHILE NOT DONE
        BMI WAIT
        LDA MOUTLO
```
(`BZONE.MAC:1736-1738`) — loop while bit 7 is set, then return with the
low result byte in A. No timeout, no fixed delay; a real busy-wait against
real hardware. **A C port's Mathbox model needs the same shape**: either
run the microcode synchronously to completion on the triggering write
(cheapest, and behaviorally equivalent since nothing else can run while
the 6502 spins), or model cycle-accurate busy timing if the port ever
needs to interleave 6502 execution with Mathbox latency.

**A subtlety worth recording explicitly, because it looked like a
contradiction until traced to ground:** `MBDIAG.MAC` defines a
compile-time busy-polarity switch, `$STAT`, defaulting to 0
("D7(LOW)=BUSY", using `BMI` to exit the wait — which on its own reads as
the *opposite* of `MBUDOC.DOC`'s "D7=1 busy"). But `BZSTST.MAC` — the
actual code that includes `MBDIAG.MAC` for Battlezone's self test — sets
`$STAT=1` immediately before the `.INCLUDE MBDIAG` (`BZSTST.MAC:378-380`),
whose comment is **"D7(HIGH)=BUSY"** and which exits the wait loop with
`BPL` (branch when bit 7 is *clear*). That is exactly consistent with
`MBUDOC.DOC:27` and with `BZONE.MAC`'s own `WAIT` routine above: **bit 7
of `$1800` is 1 while busy, 0 when done.** The `$STAT=0` default in
`MBDIAG.MAC` itself is simply never used by this game.

## Commands the game issues

Every write to a Mathbox register in `BZONE.MAC`, grouped by what it's
computing. (Grep-verified against every `STA <mathbox-register-equate>`
in the file; equates as listed in `HARDWARE_NOTES.md`'s memory map:
`ACOS=$1860`, `BSIN=$1862`, `XMATRX=$1868`, `YMATRX=$186A`,
`YSTRT1=$186B`, `RSTRT1=$1872`, `YSTRT2=$186B`†, `ZLOW=$186D`,
`ZHI=$186E`, `DIVCYC=$186C`, `ZDIV=$1874`, `XPL=$1875`, `XPH=$1876`,
`DXP=$1877`, `DYPH=$1879`, `DSTNCE=$187D`, `DST2=$187E`, `TPOSXT=$1864`,
`TPOSYT=$1866`. † `YSTRT2` is a separate equate at `MSTAT+71=$1871`
(`BZONE.MAC:355`), i.e. the **YHSP** "whole mess" trigger, distinct from
`YSTRT1` at `$186B`, the **YHSM** rotate trigger — easy to conflate by
name, don't.)

### Rotate: `ROTPNT` (`$5B1F`, `BZONE.MAC:1684`)

Called from `ROTATE` (`$5946`, `BZONE.MAC:1462`) once per visible object
to rotate its world-relative offset into tank-relative view space, and
again from `PNTPUT` (`$5D4A`, `BZONE.MAC:1994`) once per object *vertex*
during projection. Register setup before the call (example from `ROTATE`,
`BZONE.MAC:1470-1514`):

1. Compute `-sin(angle)` and `cos(angle)` of the tank's heading (via the
   `SINXTR`/`COSXTR` extrapolating sin/cos routines, `BZONE.MAC:2189-2245`)
   and write them to **A** (`ACOS`, `$1860/1861`) and **B** (`BSIN`,
   `$1862/1863`) — these become the multiplier pair `a`,`b` in the YHSM
   formula.
2. Write the object's X offset to **X** (`XMATRX`, `$1868/1869`) and Y
   offset to **Y** (`YMATRX`/`YSTRT1`, `$186A/186B`) — the high-byte write
   to `YSTRT1` **is** the YHSM trigger (offset `$0B`).

`ROTPNT` itself (`BZONE.MAC:1683-1719`):

```
ROTPNT: JSR WAIT        ; low byte of X' = (x)*a - (y)*b, rotated X
        ASL
        STA X2
        LDA MOUTHI       ; high byte of X'
        STA RSTRT1       ; <-- writing here ALSO triggers SYM ($1872, offset $12)
        ...              ;     (range-check the X' result while SYM runs)
        JSR WAIT         ; low byte of Y' = (x)*b + (y)*a, rotated Y
        ...
        LDA MOUTHI       ; high byte of Y'
```

That `STA RSTRT1` is a deliberate pipelining trick, not a coincidence:
`RSTRT1` (`$1872`) is offset `$12`, which is **SYM** — "any write starts
Y' multiply: `out = (x-e)b + (y-f)a`" (`MBUDOC.DOC:52-54`). Since SYM
triggers on *any* write regardless of value, stashing the just-read high
byte of the X' result there both (a) has nowhere better to go for a
moment and (b) kicks off the second half of the rotation — computing the
Y' output — while the 6502 is still range-checking X'. The pair `X',Y'`
computed this way, `(x*a - y*b, x*b + y*a)`, is exactly the standard 2D
rotation matrix applied to `(x,y)` with `a=cos(theta), b=-sin(theta)`
(sign folded into how `BSIN` was negated when it was written, per the
`SINXTR` call above). **A C model must reproduce this exact register
choreography** — not just "call a rotate function" — because the
XMATRX/YMATRX/YSTRT1 write order and the RSTRT1 side-effect are baked
into how the real 6502 code pipelines the two multiplies.

The result comes back as byte-shifted 15-bit magnitude-plus-range-flag
values (`ASL`/`ROL` framing around the read, `BZONE.MAC:1684-1703`) —
`ROTPNT` throws the point out if the sign bit of the shifted X' result is
set (too far / behind the plane) or if the value falls outside a
magnitude window (`CMP I,7B`, `BZONE.MAC:1696`), before ever computing Y'.
That's a Mathbox-adjacent business-logic clip, not part of the Mathbox
command itself, but a C port needs to preserve it in the same order (X'
is checked *before* Y' is even requested) since it affects whether the
second multiply happens at all.

### Perspective divide / projection: `PNTPUT` (`$5D4A`, `BZONE.MAC:1994`)

Per object-space vertex delta: writes `XMATRX`/`XMATRX+1` (dx) and
`YMATRX`/`YSTRT2` (dy) — the high-byte write to `YSTRT2` (`$1871`, offset
`$11`) triggers **YHSP**, "the whole mess": `out = (xb+ya+f)/(xa-yb+e)`
(`BZONE.MAC:2035-2047`, formula from `MBUDOC.DOC:49-51`). This is a single
Mathbox command that both rotates the vertex by the same `a`,`b` matrix
set up for the object and immediately divides by the perspective depth
term folded into `e`/`f` — the "whole mess" name is apt. The result (one
16-bit value, read via `WAIT`) becomes the point's screen-relative X
component, stored negated into the point table (`BZONE.MAC:2048-2056`).

For the Y screen component, `PNTPUT` calls `KLUDGE`
(`$5E05`, `BZONE.MAC:2092-2119`) to add the object's Z-axis (depth)
offset into the pending dividend, then explicitly loads the 16-bit
dividend into **Z** via `DIVOUT`/`DIVOUT+1` (`$186F`/`$1870` = `ZHL*`/
`ZHH*` — note `DIVOUT=186F` is *not* the low word of Z; only the high
word of the 32-bit Z dividend is being loaded here, meaning the low word
of Z was left as whatever the previous divide left it at, or is assumed
zero — this wasn't fully traced and is flagged rather than guessed) and
triggers the divide by writing **`ZDIV`** (`$1874`, offset `$14` =
**SZXD**, "start Z/X' divide") (`BZONE.MAC:2109-2118`). The quotient is
read back the same way, via `WAIT`/`MOUTHI`.

This confirms **the divide is used for the perspective projection's Y
term** (dividing a Z-axis-adjusted dividend by the X' depth value already
sitting in the Mathbox from the rotate step), while the X term is folded
directly into the single YHSP "whole mess" command. A C port needs both
paths — the combined rotate-and-divide (YHSP) and the standalone
Z-dividend divide (SZXD) — not just one generic "project point" function.

### Straight divides elsewhere: `SZXD`/`SYXD`

Two more explicit `ZDIV`-triggered divides appear outside `PNTPUT`, both
following the same `XPL`/`XPH` (`$1875/1876`, write X') + `DIVOUT`/
`DIVOUT+1` (`$186F/1870`) + `STA ZDIV` (`$1874`) pattern
(`BZONE.MAC:3527-3544`), used in the enemy-tank/missile tracking code
(`TRACK`, `$6810`) to compute an angular tracking correction. `SYXD`
(offset `$13`, "Y'/X' divide") is not observed written directly by
`BZONE.MAC` in the same way; it may be reached only through `YHSP`'s
internal use, or not used by the game at all — not confirmed either way
from a full-file grep, and flagged rather than guessed.

### Distance: `DSTNCE`/`DST2`

Writes to `DSTNCE` (`$187D`) and `DST2` (`$187E`) follow `TPOSXT`/
`TPOSYT` (`$1864`/`$1866` — note these alias into the **E/F** operand
pair, `MBUDOC.DOC` offsets `$04`/`$06`) setup in several places (e.g.
`BZONE.MAC:2266-2282`, `2409-2415`, `3598-3615`), used for tank-to-target
range checks throughout the enemy AI (`ROBOT`, `SAUCMV`, `FIREIT`). The
microcode for this (`MBUCOD.V05:660-680`, `.SBTTL DISTANCE ROUTINE GIVEN
TWO POINTS`) computes `|dx|`, `|dy|`, sorts them into smaller/larger, and
approximates the hypotenuse as **`larger + (3/8)*larger's-partner`** — a
classic "alpha max plus beta min" fast-distance approximation (three
successive shift-and-add steps, `MBUCOD.V05:676-680`), **not** a true
`sqrt(dx^2+dy^2)`. Note: the microcode's own inline comment at that point
says `;T1=T1+(2/3*T0)`, but the actual instructions perform `+T0/4` then
`+T0/8`, i.e. `+3/8*T0` — the comment and the code disagree; this is an
inconsistency in Atari's own source, not something to resolve by
guessing which is "right". **A C port must implement the 3/8
approximation the code actually performs**, matching the microcode, not
the stale comment, and not a real Euclidean distance — this is exactly
the kind of bit-exact quirk that would silently break range-dependent AI
and firing behavior if "corrected" to a real hypotenuse.

Revision history in `MBUCOD.V05` dates this precisely: *"4) ADD 'DSTNCE'
FOR BATTLE ZONE (ED ROTBERG)"* (`MBUCOD.V05:81`) — added specifically for
this game, by its project programmer (matches `BZONE.DOC:3`, "PROJECT
PROGRAMMER: EDWARD ROTBERG").

### Clip: `YHSC` (offset `$1C`)

Entirely undocumented in `MBUDOC.DOC`'s prose beyond the one-line table
entry ("writing Y-high starts 'clipping'", `MBUDOC.DOC:66`); the actual
algorithm is only in `MBUCOD.V05:624-658`:

```
;THIS ROUTINE USES A BINARY APPROXIMATION TO FIND A POINT
;ON THE BOUNDARY OF THE WINDOW. THE DEFINITION OF A
;POINT IN THE WINDOW IS X>ZH AND X>Y>-X.
;ENTRY (X1,Y1)=POINT IN WINDOW
;      (X2,Y2)=POINT NOT IN WINDOW
;      (N)=NUMBER OF ITERATIONS+1 USED
;      (ZH)=MINIMUM EDGE FOR WINDOW
;EXIT  (X1,Y1)=POINT IN WINDOW=
```
(`MBUCOD.V05:628-639`)

It's binary bisection between a known-inside point and a known-outside
point: compute the midpoint, test whether it satisfies the window
condition `X > ZH and X > Y > -X` (a symmetric field-of-view wedge plus a
near-plane minimum), move whichever endpoint is on the wrong side to the
midpoint, and repeat for **N** iterations — reusing the same **N**
register (`RN`, offset `$0C`, "NL") that the divide commands use for
their bit-count, via `DEC RN,RAMF,JPL` (`MBUCOD.V05:653`). This detail —
that `YHSC`'s iteration count is set through the *same* register as the
divide's bit-count — is not stated in `MBUDOC.DOC`'s table and is only
visible in the microcode.

This is the near/edge clip for Battlezone's field-of-view window
(consistent with `HARDWARE_NOTES.md`'s note that Battlezone uses the
`avg_start_bzone()` clip-window AVG variant) — `BZONE.MAC` sets a "lower
window" and "upper window" via `VGVTR`/`VGSTAT` calls early in `MAIN`
(`BZONE.MAC:432-437, 472-478`), which is the *display*-side half of the
clip region; `YHSC` is presumably how object geometry gets clipped to
match before being turned into vectors, though the exact call site
writing `YHSC`/`$187C` wasn't isolated in `BZONE.MAC` in the time
available for this pass — flagged as needing a closer look rather than
asserted.

## Arithmetic behavior a C model must reproduce bit-exactly

- **Multiply is sign-magnitude, not raw two's-complement.** The 16-bit
  multiply (`MULXP`/`MULXPP`/`MULXPC`, `MBUCOD.V05:481-521`, and the Y'
  variant `MULYP`, `MBUCOD.V05:523-558`) explicitly forces each operand
  positive before an iterative 16-step conditional-add-and-shift loop
  (`.REPT 16. / CADD ... / .ENDR`, e.g. `MBUCOD.V05:495-497`), tracking
  the sign of the eventual result separately, and re-applies the sign at
  the end. A naive C `int16_t * int16_t` will *not* automatically match
  this unless the intermediate shift/round steps below are also matched.
- **YHSP's combined rotate-and-divide performs an explicit rounding step**
  on the two low-word partial products before summing them: each is
  right-shifted (halved) independently, then added, with a carry
  correction (`MBUCOD.V05:512-516`). This is a specific rounding
  convention (round toward zero on each half, then sum), not a
  full-precision sum-then-shift — reproduce the order of operations, not
  just the final formula.
- **Rev-5 truncation note**: *"CLEARS YPL LSB'S ON POST-ADD DIVIDE (GREG
  RIVERS)"* (`MBUCOD.V05:79`) — the low bits of the Y'-low result are
  explicitly zeroed on the post-add path (`MBUCOD.V05:557-563`). This is
  a deliberate, documented truncation, not noise; a C port must zero
  those same bits in that same path.
- **Division is restoring division**, the textbook subtract/test-sign/
  restore-or-shift algorithm, running for **N** iterations (the value
  written to `NL`, offset `$0C`) with sign-magnitude conversion on entry
  (dividend and divisor both forced positive, with the result sign
  precomputed as `sign(dividend) XOR sign(divisor)` and re-applied to the
  quotient at the end) (`MBUCOD.V05:566-622`, algorithm core at
  `606-622`). **No remainder is ever exposed** — only the quotient is
  driven to the output bus at the end (`ADD Z,Q STALL`,
  `MBUCOD.V05:622`); the remainder sits in a temp register and is
  discarded. If any Battlezone code path were to assume a remainder is
  readable, it would be wrong — none was found doing so, but a C port
  should not invent a remainder-read path that doesn't exist on the real
  hardware.
- **A documented divide bug and a documented divide shortening** both
  predate the shipped microcode: *"1) FIX '2-MSB' DIVIDE BUG"*
  (`MBUCOD.V05:87`) and *"2) SHORTEN DIVIDE"* (`MBUCOD.V05:85`) — the exact nature of the
  original "2-MSB" bug isn't described further in the source, so there's
  nothing more to port here than the fact that the *shipped* divide
  routine (the one actually transcribed above) is already the
  post-fix version.
- **Distance is the 3/8 fast-approximation described above, not a real
  hypotenuse** — repeated for emphasis since it's the single easiest
  "obvious improvement" a C port could accidentally make that would
  actually be a bug.
- **Clock rate is not documented anywhere in `MBUDOC.DOC` or
  `MBUCOD.V05`.** No MHz figure for the Mathbox's own micro-sequencer
  clock was found in either file. Cycle counts for each command (54-59,
  119-131+5N, etc.) are given in Mathbox micro-cycles, not 6502 cycles or
  wall-clock time, and nothing in these two files ties a micro-cycle to a
  real-world duration. If the C port needs real timing for this (e.g. to
  model whether the 6502 could theoretically race ahead of a slow
  Mathbox op), that number has to come from elsewhere — not found in the
  files read for this task.

## What `bzone.cpp` does with all this

Nothing — by design, this is useful to know. `bzone.cpp`'s memory map
wires `MathboxStatusRead`, `MathboxLowbitRead`, `MathboxHighbitRead`, and
`MathboxGo` straight through to an external module (`#include "mathbox.h"`,
not read as part of this task) with zero host-side arithmetic in
`bzone.cpp` itself. That confirms the design intent for this port too:
the Mathbox belongs behind its own seam as a self-contained hardware
model driven by the register semantics above, not folded into the main
CPU-translation code.

## Reading further

- `HARDWARE_NOTES.md` — the full `$1860-$187F` address block in context of
  the rest of the memory map, and the AVG clip-window variant this
  project's `YHSC` command presumably feeds.
- `MAINLINE_NOTES.md` — where `ROTATE`/`PNTPUT`/`ROTPNT` sit in the
  per-frame 3D pipeline, and how their outputs become vector-generator
  display lists.
- `CPORT_READINESS.md` — the Mathbox as one of the four hardware models
  the C port needs (alongside AVG, POKEY, and the discrete sound board).
