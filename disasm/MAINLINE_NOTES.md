# The main line

Reset, the frame loop, and how a picture actually gets from the object
tables to the tube. Source: `battlezone-main\BZONE.MAC` (the 4900-line
main module), `BZSTST.MAC` (power-on), `VGUT.MAC` (vector-list helpers),
`BZMTNS.MAC` (the vector ROM data), cross-checked against
`disasm\build\symbols.json`/`srclines.json` for addresses.

## Reset

`PWRON` (`$7ACC`, `BZSTST.MAC:47`) is both the 6502 RESET and IRQ vector
(`.VCTRS 7FFA,NMI,PWRON,PWRON`, `BZSTST.MAC:648` — IRQ is unused
hardware-wise; if it ever fired, the board would effectively reset). It:

1. Sets the stack pointer, zeroes POKEY's `SKCTL`, clears all of zero page
   (`BZSTST.MAC:47-55`).
2. Zeroes vector RAM ($2000-$2FFF, sixteen 256-byte pages) and the
   watchdog (`BZSTST.MAC:58-77`).
3. Halts the vector generator (`STA VSTOP`, `BZSTST.MAC:80`).
4. Reads IN0 bit 4 (self-test switch, `ST.BIT=$10`) — if held, jumps into
   `RAMTST` (self test); otherwise enables sound and falls through
   (`BZSTST.MAC:81-86`).
5. Initializes the vector-list buffer header, the high-score table (from
   a compiled-in default table, `FDGTBL`), the initial game state
   (`ACTION`/`GOVER`), and buzz-bomb sound channels (`BZSTST.MAC:87-117`).
6. `JSR BATINT` (sets up the attract-mode logo sequence), then
   `JMP MAIN` (`BZSTST.MAC:118-119`). It never returns.

## Frame sync

`MAIN` (`$5000`, `BZONE.MAC:421`) is a single routine that runs one whole
game frame per call and loops on itself — it is not re-entered per NMI,
it just spins waiting for one:

```
MAIN:   LSR SYNC        ;S  SYNC UP
        BCC MAIN
```
(`BZONE.MAC:421-422`) — `SYNC` is a byte the NMI handler increments once
every 16 NMIs (`INC SYNC`, `BZONE.MAC:1086`), i.e. once every ~65 ms (see
`HARDWARE_NOTES.md` for the exact NMI rate). `MAIN` shifts it right and
loops until a bit falls out into carry, which happens once per those 16
NMIs — this is the entire frame-rate governor. There is no vsync
interrupt as such; the mainline is entirely paced by counting NMIs.

Once synced, `MAIN` runs the whole frame's logic — 3D pipeline, collision,
AI, sound-word assembly, message drawing — then falls all the way through
to `JMP MAIN` at the bottom (`BZONE.MAC:621`) to wait for the next sync
bit. **This means one video frame's worth of Battlezone logic is exactly
one call through `MAIN`'s body, and the whole thing is one giant routine**
(with subroutine calls out to dozens of helpers) rather than a dispatched
per-system update loop — worth knowing before trying to decompose it into
separate C "systems".

Independently, the NMI handler also runs `VOLCNO` (volcano rock motion,
`$58BA`) directly from `MAIN`'s very first instruction after the sync
check (`JSR VOLCNO`, `BZONE.MAC:423`) — so volcano rocks move once per
frame like everything else, just called before the frame-sync body proper
gets going.

## Vector RAM: double buffering

`VECRAM` ($2000) is split into (at least) two picture buffers so the AVG
can be drawing one while the 6502 builds the next. The NMI handler owns
the switch (`BZONE.MAC:1212-1228`):

```
40$:    DEC VTIMER              ; VTIMER counts down once per NMI
        BNE 20$
        STA VSTOP               ; halt the AVG - buffer's done
        LDA BUFRDY               ;S  IF BUFFER NOT COMPLETE
        BNE 30$
        LDA VECRAM+1             ;C  SWITCH BUFFERS
        EOR I,4
        STA VECRAM+1
        AND I,4
        BNE 25$                  ;D  IF LOWER BUFFER AVAILABLE
        LDA I,VECRAM/100+8       ;C  USE UPPER BUFFER
        BNE 26$
25$:    LDA I,VECRAM/100         ;C  USE LOWER BUFFER
26$:    STA BUFRDY
30$:    STA GOADD                ;C  START UP VECTOR GENERATOR FOR 24 MS.
        LDA I,6
        STA VTIMER
```

Every 6th NMI (`VTIMER` reloaded to 6, matching the ~41 Hz picture rate
from `HARDWARE_NOTES.md`), the handler halts the AVG, flips a bank bit
(bit 2 of the vector-list-pointer high byte, toggling between page
`$20xx` and `$28xx` — "upper"/"lower" 2 KB buffers inside the 4 KB vector
RAM region), records which buffer is now free in `BUFRDY`, and restarts
the AVG (`STA GOADD`) on whichever buffer the mainline isn't currently
writing into. `MAIN` itself waits for a free buffer near the very top of
the frame (`5$: LDX BUFRDY; BEQ 5$`, `BZONE.MAC:424-425`) before it starts
writing a new display list into it, and sets `VGLIST` to point at that
buffer plus a 2-byte header offset (`BZONE.MAC:426-428`). This is the
entire double-buffering scheme — two 2 KB halves of one 4 KB region, a
free/busy flag, and NMI-driven flip — not a general N-buffer arrangement.

## Building the display list: one frame, in call order

Everything below runs inside `MAIN` (`$5000`) after the sync wait, in
this order (routine name, address, what it emits into `VGLIST`):

1. `VGSCA1`/`VGCNTR` — set scale to full size, center the beam
   (`BZONE.MAC:429-431`).
2. Set the lower clip window via `VGVTR`/`VGSTAT` (`BZONE.MAC:432-437`) —
   this is the display-side half of the field-of-view window the
   Mathbox's `YHSC` clip command matches on the geometry side (see
   `MATHBOX_NOTES.md`).
3. Game-over / high-score-table branch check (`BIT GOVER`,
   `BZONE.MAC:443-446`) — if set, jumps out to `HISCRE`/`BIGWND` instead
   of drawing a normal frame (see Game states, below).
4. Attract-mode state machine tick (`BZONE.MAC:447-471`) — alternates
   `DSPLAY` between the logo and high-score display, calls `TANKCK`/
   `R2D3CK` to reset/spawn the demo opponent.
5. `VGCNTR` + upper clip window (`BZONE.MAC:472-478`), copyright message
   in attract mode (`MSGS` with `COPYRT`, `BZONE.MAC:479-482`).
6. `MOUNTS` (`$57C8`) — draws the horizon line and the mountain-scape
   silhouette by JSRL-ing into vector-ROM-resident mountain data
   (`BZMTNS.MAC`); see below (`BZONE.MAC:483`).
7. `ROTATE` (`$5946`) — walks the static playfield object table
   (`PTBLO1`/`PTBLX1`/`PTBLY1`, ROM) plus the dynamic actors (tank
   shells, the enemy saucer) and rotates each into view-relative space
   via the Mathbox, culling anything out of frame; survivors land in
   `PTBLO2`/`PTBLX2`/`PTBLY2` (RAM) (`BZONE.MAC:493, 1449-1678`).
8. `BATTLE` (`$5268`) — a second, similar pass specifically for the
   player's opponent tank / R2D3 buzz bomb (`BZONE.MAC:494, 705-`).
9. Per surviving object in `PTBLO2` (loop `20$`, `BZONE.MAC:495-504`):
   `PNTPUT` (`$5D4A`) projects its point table through the Mathbox
   (rotate + perspective divide, see `MATHBOX_NOTES.md`), then `DRAW`
   (`$5C5C`) walks the projected point list emitting `VGVTR`/`VGDOT`
   vector-generator instructions for that object's shape.
10. Windshield-crack overlay if `CRACK` is nonzero (`WNSHLD`,
    `$51E9`, `BZONE.MAC:505-507, 640-697`) — drawn *instead of* the
    normal HUD/radar for that frame; see Game states below.
11. Otherwise: `BIGWND` (open the wide field-of-view window, `$51D9`),
    `DRADAR` (`$6AE9`, the radar sweep display), `TRACK` (`$6810`,
    target-lead angle used both for AI and the "enemy to LEFT/RIGHT/REAR"
    on-screen prompt), collision/message annotations, `INFO` (`$6D2E`,
    score/lives/credits HUD) (`BZONE.MAC:508-577`).
12. Game-over message sequence if applicable (`BZONE.MAC:578-602`) —
    "GAME OVER", coin-mode prompt, "PRESS START"/"INSERT COIN".
13. `FINISH` (`$5574`) — terminates the vector list (adds the VG halt
    instruction) (`BZONE.MAC:603`).
14. Gameplay-logic tail, run *after* the display list is finished but
    still within the same frame: `COLCHK`/`SHUPDT` four times
    (collision check / shell update, `BZONE.MAC:606-611`), `QWIKCK` or
    `TR7CHK`-gated skip (fast robot-shell collision check,
    `BZONE.MAC:612-616`), `ROBOT` (enemy tank AI, `$6418`), `SAUCMV`
    (saucer AI, `$677C`), `BOUND` (collision bounce/view-shake decay,
    `$5E44`), `MOTION` (apply the player's tread-lever input to tank
    position, `$61D0`) (`BZONE.MAC:617-620`).
15. `JMP MAIN` (`BZONE.MAC:621`) — back to the top to wait for the next
    sync bit.

So the "3D pipeline" per frame is: **rotate playfield objects into view
space (Mathbox rotate) -> cull -> project each surviving object's point
table (Mathbox rotate+divide) -> emit vector-generator draw instructions
-> run gameplay/AI/collision for the *next* frame's geometry** — the
display list being finished (`FINISH`) always happens before the AI/
collision tail runs, so what's on screen this NMI-driven flip is always
one full frame "behind" the AI update that produced the state for the
*next* list.

### The vector-list emission primitives

All emission goes through `VGUT.MAC`'s small helper set — `VGCNTR`
(center beam), `VGVTR`/`VGVTR1`/`VGVCTR` (draw a relative vector, given
dx/dy already sign-extended into `XCOMP`), `VGDOT` (a zero-length vector,
i.e. a dot), `VGJMPL`/`VGJSRL` (jump/call to a fixed address *inside
vector-generator address space*, encoded as the target's word address
divided by 2, relative to vector-instruction-space origin — see "self-
relative tables" in `CPORT_READINESS.md`), `VGSTAT` (set the clip
window/mode), `VGSCAL`/`VGSCA1` (set the AVG's scale factor), `VGHEX`/
`VGHEXZ` (draw one hex digit, used for score/HUD digits), `VGADD`/
`VGADD2` (bump the `VGLIST` pointer). Every one of these appends bytes at
`(VGLIST)` and advances it — `VGLIST` **is** the write cursor into
whichever vector-RAM buffer the frame is currently building
(`VGUT.MAC:52-68` documents the calling convention). Several of these
routines fall through into each other with no `RTS` between them (e.g.
`VGCNTR` falls into `VGADD2`, `VGUT.MAC:250-253`; `VGDOT` falls into
`VGVTR`, `VGUT.MAC:313-317`) — see `CPORT_READINESS.md` for why that
matters to a 1:1 translation.

### `MOUNTS` and the vector ROM

`MOUNTS` (`$57C8`, `BZONE.MAC:1237-1373`) computes the tank's heading as
a vector-generator-space angle, positions the beam, then `VGJSRL`s three
times into `HORIZN`/mountain data that lives **in the vector ROM itself**
(`BZMTNS.MAC`, mapped at `$3000-$3FFF`) — the mountain silhouette, the
horizon line, and related shapes (character font `ASCVG` data, the logo,
the "cracks" windshield-damage vectors, explosion-piece shapes, radar
ring) are static vector programs pre-assembled into ROM, not built at
runtime; `MOUNTS` merely calls into them with the current heading/scale
already set up. Vector ROM ranges: `HORIZN=$3000` (`BZMTNS.MAC:9`),
`MTNS=$3006` (`BZMTNS.MAC:11`), `CRACKS=$36F8`, `ARCTAN=$3785`,
`BONTBL=$3886`, `MISLVL=$388A`, `OBJPNT=$388E`, `PTBLO1=$3FCC`,
`LOGOBJ=$3FF7` (all `BZMTNS.MAC`, per `symbols.json`).

## Game states

Several independent one-byte flags in zero page gate what a frame does,
rather than one enum-style state variable:

| Flag | Meaning | Cite |
|---|---|---|
| `ATRACT` | 0 = attract/demo mode, $FF = a coin has started a game | `BZONE.MAC:262`, set/cleared in `NMI` around the start-button check, `BZONE.MAC:1170-1211` |
| `GOVER` | game-over / high-score-table display in progress | `BZONE.MAC:261`; checked first thing in `MAIN` (`BZONE.MAC:443`) to divert into `HISCRE` |
| `DSPLAY` | attract-mode sub-state: alternates logo vs. high-score table | `BZONE.MAC:307`, toggled in the attract tick (`BZONE.MAC:454-465`) |
| `LOGO` | $FF while the attract-mode "BATTLE ZONE" logo animation is active | `BZONE.MAC:318`, set at `BZONE.MAC:463` |
| `CRACK` | windshield-crack damage counter; nonzero diverts the frame into `WNSHLD` instead of the normal HUD, and reaching 32 ends the game (`BZONE.MAC:649-667`, transitions to `ATRACT` or a fresh player life depending on `GOVER`) | `BZONE.MAC:256` |
| `SUPER` | Super Bonus Tank flag — see the security-trap note below | `BZONE.MAC:314` |

`GOVER`+`HISCRE` (`$53C5`) run the "GAME OVER" -> high-score-entry ->
high-score-table attract sequence; `CKSCOR` (`$5344`) is what decides
whether the just-finished game's `HITS` (score) beat any table entry and
splices the player's initials in.

### A trap worth knowing about: the "Super Bonus" checksum gate

Deep in `MAIN`'s tail, after the frame's vector list is built, there's a
self-check that at first glance looks like ordinary code and is actually
a copy-protection trick doubling as a gameplay gate:

```
        LDA VGLIST+1            ;C  SECURITY
        AND I,0F8
        STA TBLPTR+1
        LDA I,2
        STA TBLPTR
        LDY I,35
        LDA I,0
99$:    EOR NY,TBLPTR            ; XOR-checksum $36 = 54 bytes (Y=$35..0) of the just-built
        DEY                      ; vector list buffer
        BPL 99$
        LSR
        STA TRAP2                ; result -> TRAP2
```
(`BZONE.MAC:539-550`), later consumed at:
```
30$:    LDA TRAP2
        LSR
        ORA Z,POINT
        BNE 20$
        STA SUPER                ; only reachable if TRAP2's checksum came out right
```
(`BZONE.MAC:622-626`, ROM `$51C2`; note the value stored in `SUPER` there is
the 0 left in A). `TRAP1`/`TRAP2` are explicitly labeled
`;SOFTWARE SECURITY`/`;SECURITY TRAP` in their declarations
(`BZONE.MAC:327`, `302`).

Facts, checked against the listing (an earlier draft of these notes said
"36 bytes"; `LDY I,35` is hex, so the window is `$36` = **54** bytes):

- Both checksums cover the same window: offsets `$02-$37` of the vector
  buffer being built (page = `VGLIST+1 AND $F8`), and both are computed
  **only in attract mode** (`BIT ATRACT` / `BMI` skips them during play).
- `TRAP2` (`MAIN`, `$510A-$511E`) = EOR of the 54 bytes, shifted right once.
  Consumer: `B_END_30` (`$51C2`) as quoted above.
- `TRAP1` (`BATTLE`, `$526C-$5284`) = 0 minus the 54 bytes (SBC chain) minus
  `OBJTBL+$21`. Consumers: **the NMI's START handler deducts it from the
  credits** (`$564A: LDA $$CRDT / SEC / SBC TRAP1 / STA $$CRDT`), so on an
  unmodified board it must come out as exactly 1 - a changed attract-mode
  display list makes a game cost the wrong number of credits; and `FINISH`
  (`$5588`) stores it to `VECRAM+0` during play once `NOR2D3 >= 4`.

**A C port gets this right only if the first `$38` bytes of the attract-mode
display list are byte-identical to the original's** - which is one more
reason the port writes real AVG words into a modelled vector RAM - see
`CPORT_READINESS.md`.

## The object model

Two families of tables: a **static, ROM-resident playfield table**
(the fixed obstacles/terrain features for the current game — rocks,
bunkers, etc., laid out once per level in vector ROM: `PTBLO1`/`PTBLX1`/
`PTBLY1`, `NOBJ=28` entries, `BZONE.MAC:287-290`, `BZMTNS.MAC:840`), and
**RAM tables for everything that moves**, all in zero page or the low
`$0200` object-table page:

| Table | What | Cite |
|---|---|---|
| `TPOSX`/`TPOSY` (4 bytes each) | player tank world position | `BZONE.MAC:231-232` |
| `TANGLE` | player tank heading | `BZONE.MAC:229` |
| `SAPOSX`/`SAPOSY`, `SCANGL` | enemy saucer position/heading | `BZONE.MAC:269-271` |
| `SAINCX`/`SAINCY` | saucer motion increment | `BZONE.MAC:272-273` |
| `SHELLX`/`SHELLY` (4 bytes each), `SINCX`/`SINCY` | player shell position + velocity | `BZONE.MAC:242-245` |
| `FIRECT` | shell-in-flight timer/state | `BZONE.MAC:225` |
| `HITS` (4 bytes) | score, BCD (see `CPORT_READINESS.md`) | `BZONE.MAC:246` |
| `RGOAL`/`ACTION`/`STATE` | enemy tank AI goal angle / current behavior / state bits | `BZONE.MAC:247, 253-254` |
| `EXPOSX`/`EXPOSY`/`EXPOSZ`/`ZVELOC` (`EXNOBJ=8` slots) | explosion-piece positions/Z-velocity, i.e. the "tank blows apart into flying chunks" effect | `BZONE.MAC:291-296` |
| `OBJTIM`/`XSPD`/`YSPD`/`XPOSL`/`XPOSH`/`YPOSL`/`YPOSH` (`NOROCK=5` slots) | volcano rock spawner/motion state (comment credits "RUBIN" — an engineer, not otherwise identified in this source) | `BZONE.MAC:319-326` |
| `PTBLX2`/`PTBLY2`/`PTBLO2` (`NOBJ=28`) | this frame's rotated+translated, view-space object list (`ROTATE`'s output, `PNTPUT`'s input) | `BZONE.MAC:288-290` |
| `PNTTBL` (`NPTS=26`) | this object's projected screen-space point list (`PNTPUT`'s output, `DRAW`'s input) | `BZONE.MAC:238-239` |
| `HSCTBL`/`INITLS` (`HSCNUM=10`) | high score table + initials | `BZONE.MAC:304-306` |

## Enemy AI, briefly

`ROBOT` (`$6418`) drives the enemy tank's high-level behavior through the
`ACTION`/`STATE` bytes and a target heading `RGOAL`, with helpers
`TRACK` (lead/lag angle to target, shared with the player's on-screen
"ENEMY TO ..." prompt), `FIREIT` (range/angle-gated shell firing,
`$6595`), `TANKCK`/`ROBCHK` (respawn/reset), and `TR7CHK` (branches
behavior for the faster "TR7" tank variant vs. the base tank,
`$69B5`). `R2D3CK`/`R2D3FL` gate an alternate "buzz bomb" opponent that
replaces the tank under some condition (`BZONE.MAC:3769-3803` for setup);
`SAUCMV` (`$677C`) is a separate, simpler flying-saucer AI. None of this
was traced instruction-by-instruction for this pass — treat the above as
an orientation map for where to look, not a full behavior spec.

## Sound request flow

Two independent sound paths, both touched every NMI:

1. **POKEY tonal sounds** — `SNDON` (`$7985`, `BZSOUN.MAC:255`) starts a
   named sound (by index, e.g. `BOING`, `SAUSND`, `DISINT`) into an
   internal priority-ordered sound-slot table; `MODSND` (`$79B9`,
   `BZSOUN.MAC:301`) is called every NMI to advance whichever sound is
   currently playing through its frame-by-frame envelope/frequency
   sequence and write the result to the actual POKEY channel registers.
   `SNDON` is called both from the NMI handler itself (crash/saucer/
   explosion priority logic, `BZONE.MAC:1106-1122`) **and** directly from
   mainline code (e.g. the object-collision "boing" in `MOTION`,
   `BZONE.MAC:2675-2676`) — see `CPORT_READINESS.md` for why a routine
   invoked from both contexts needs care in translation.
2. **The discrete sound/lamp latch** ($1840, fully bit-mapped in
   `HARDWARE_NOTES.md`) — built up in the zero-page shadow `SNDWRD`
   by mainline code across the frame (engine speed/idle bit set in
   `MOTION`, shell-fire/explosion bits set at their trigger points) and
   combined with a few NMI-local bits (game-mode enable, alarm, LED) each
   interrupt before being written out as one byte
   (`BZONE.MAC:1138-1211`).

## Reading further

- `HARDWARE_NOTES.md` — the memory map and every I/O bit referenced above
  (`SOUND` latch, `GOADD`/`VSTOP`, POKEY, option switches).
- `MATHBOX_NOTES.md` — exactly what `ROTATE`/`PNTPUT`/`ROTPNT` ask the
  Mathbox to compute, and what a C model of it has to get bit-exact.
- `CPORT_READINESS.md` — the stack-based computed-jump idiom, the
  fall-through routine chains in `VGUT.MAC`, the NMI/mainline shared-code
  hazard, and the vector-RAM checksum trap, all called out above, pulled
  together into a translation checklist.
