# C signatures of translated routines (shared contract between translation agents)

Several agents translate different modules at the same time. A routine's C signature is decided
by the agent that OWNS the routine and registered here BEFORE its body is written, so callers in
other modules can rely on it. Callers: if the routine you need is not here yet, derive the
signature by the rules below, add it to the owner's section marked `(proposed by <your module>)`,
and use it; the owner adopts it or fixes both sides.

## Rules (PLAN.md section 1, same as the Tempest port)

- One C function per Atari label, lowercased, `.` and `$` mangled as in `state_defs.h`
  (`M.SET` -> `m_set`, `R.ATCK` -> `r_atck`, `$DETCT` -> `s_detct`). Local `10$` labels are not
  functions; they are control flow inside their routine.
- Inputs the routine really reads from A / X / Y / carry become parameters, in the order
  `(uint8_t a, uint8_t x, uint8_t y, int c)`, keeping only the ones read. Documented or actually
  used outputs become the return value; when more than one register/flag is consumed by any
  caller, return the small struct `regs` (`typedef struct { uint8_t a, x, y; uint8_t c, z, n, v; } regs;`
  declared in `regs.h`) and fill only what callers use (say which in the comment).
- Everything else goes through RAM cells (`state_defs.h`), exactly as the ROM does it, scratch
  cells included. No cached copies of RAM across calls.
- Hardware only through `hw.h`, in the ROM's order.
- Push-address/RTS dispatches (DTAB, MTAB, TTAB, LNGTBL) become `switch`; fall-through chains
  become shared static tails or a call to the next routine at the end (say so in a comment).
- Each module has its own header `<module>.h` with its prototypes; do not edit another module's
  files. `regs.h` is shared and frozen (created with this file).

## VGUT (owner: M1 agent, vgut.c; prototypes in `vgut.h` - include that, not game.h, from another module)

All of them read/write `VGLIST` ($02/$03) and append bytes to the display buffer at `(VGLIST)`
through `cpu_wr` (dropped if it would land outside RAM/vector RAM); that is not repeated per row
below. Carry is a plain `int` (0/1), not part of `regs` (every routine here has at most one
register output).

| C prototype | ROM label ($addr) | reads (besides VGLIST) | writes (besides VGLIST/buffer) |
|---|---|---|---|
| `void vgrtsl(void);` | VGRTSL $7A19 | - | - (dead code: never called) |
| `void vghalt(void);` | VGHALT $7A1D | - | - |
| `int vghexz(uint8_t a, int c);` | VGHEXZ $7A29 | `VGMSGA` (vector ROM, via `vgjsrl`-style glyph lookup) | - |
| `int vghex(uint8_t a);` | VGHEX $7A2F | `VGMSGA` | - |
| `void vgjmpl(uint8_t a, uint8_t x);` | VGJMPL $7A49 | - | - (dead code: never called) |
| `void vgjsrl(uint8_t a, uint8_t x);` | VGJSRL $7A5A | - | - |
| `void vgsta1(uint8_t a);` | VGSTA1 $7A61 | `VGBRIT` | - (dead code: never called) |
| `void vgstat(uint8_t a, uint8_t y);` | VGSTAT $7A63 | - | - |
| `void vgcntr(void);` | VGCNTR $7A6A | - | - |
| `void vgadd2(uint8_t a, uint8_t x);` | VGADD2 $7A6E | - | - |
| `void vgadd(uint8_t y);` | VGADD $7A76 | - | `VGLIST`/`VGLIST+1` only |
| `void vgsca1(uint8_t a);` | VGSCA1 $7A81 | - | - |
| `void vgscal(uint8_t a, uint8_t y);` | VGSCAL $7A83 | - | - |
| `void vgdot(uint8_t a);` | VGDOT $7A8A | - | `VGBRIT` (via `vgvtr`) |
| `void vgvtr(uint8_t a, uint8_t x, uint8_t y);` | VGVTR $7A8E | - | `VGBRIT`, `XCOMP`..`XCOMP+3` |
| `void vgvtr1(uint8_t a, uint8_t x);` | VGVTR1 $7A90 | - | `XCOMP`..`XCOMP+3` (leaves `VGBRIT` alone) |
| `void vgvtr2(void);` | VGVTR2 $7AAB | `XCOMP`..`XCOMP+3` (caller must have set them) | - |
| `void vgvctr(uint8_t x);` | VGVCTR $7AAD | `x`..`x+3` (zero page; every caller passes `x = A_XCOMP`), `VGBRIT` | - |

Notes for callers: `vgvtr`/`vgvtr1`/`vgdot` take the X/Y beam deltas already divided by 4 and
scale them x4 back into `XCOMP` internally (13-bit AVG units) - pass the same byte values BZONE.MAC
itself passes in A/X, not pre-scaled ones. `vgjsrl`/`vgjmpl` take the target's CPU address as
A=high byte, X=low byte (not a 16-bit `uint16_t` - matches the ROM's own register pair and avoids
ambiguity about which byte order a `uint16_t` parameter would imply). `vghexz`'s carry chains: pass
the previous call's return value as the next call's `c` to suppress leading zeros across a
multi-digit number, matching BZONE.MAC's own `PHP`/`JSR VGHEXZ` chains.

## BZSOUN, COIN65, NMI (owner: agent "nmi-sound-coin": bzsoun.c, coin65.c, bzone_nmi.c)

Status: done, compiles `/W4` clean, see `NOTES_nmi_sound_coin.md`'s RESUME table. `S_DETCT`/
`S_BONUS`/`S_EXTB`(`S_CNVRT`)/`S_EXT` (inside `MOOLAH`) and `MODSND_15`/`MODSND_17`/`MODSND_20`/
`MODSND_30` (inside `MODSND`) are internal fall-through labels only reached by their own routine's
loop/fall-through, never `JSR`-called elsewhere in this ROM - modelled as `static` helpers/loop
bodies inside `coin65.c`/`bzsoun.c`, not exported (same convention `vgut.c` uses for VGUT.MAC's own
chain), so they have no entries below.

| C prototype | ROM label ($addr) | reads (besides RAM cells named in the body) | writes |
|---|---|---|---|
| `void sndon(uint8_t a, uint8_t x);` | SNDON $7985 | `PNTRS` (ROM, `A_PNTRS`=$7864, via `cpu_rd`) | `POINT`/`FRAMES`/`COUNT` (indexed, per-channel) |
| `void modsnd(void);` | MODSND $79B9 | `SOUND_7886`-based envelope table, `AUDCV`/`IDLEV` (ROM, via `cpu_rd`) | `CURRENT`/`FRAMES`/`COUNT`/`POINT` (indexed); POKEY `AUDF1,X`/`AUDCTL` (`hw_pokey_write`) |
| `void moolah(void);` | MOOLAH $56C3 | `hw_in0()` (S_COINA/S_LAM), `S_CNSTT`/`S_INTCT`/`S_CMODE` | `S_CNSTT`/`S_PSTSL`/`S_LMTIM`/`S_BCCNT`/`S_CNCT`/`S_CCTIM` (indexed where noted), `S_S_CRDT`, ROM table `S_MODLO` ($5778, local `#define`, via `cpu_rd`) read only |
| `void nmi(void);` | NMI $558F | `hw_opton2()`, `hw_option()`, `hw_pokey_read(0x08)` (ALLPOT), most zero-page game state (`SUPER`/`POINT`/`SCOLFG`/`OBJCOL`/`SAUCER`/`EXPCNT`/`SXPCNT`/`SNDWRD`/`ATRACT`/`GOVER`/`S_LMTIM`/`S_CMODE`/`S_S_CRDT`/`FRAME`/`BUFRDY`/`VECRAM+1`/`VTIMER`) | `S_INTCT`/`SYNC`/`S_CMODE`/`EMCNT`(`hw_emcnt`)/`NOR2D3`/`ATRACT`/`S_S_CRDT`/`S_BCCNT`/`LOGO`/`HITS`(+1/+2/+3)/`ACTION`/`FIRECT+2`/`GOVER`/`TIMOUT`/`SCOLFG`/`SAUCER`/`TANGLE`/`TPOSX+1`/`TPOSY+1`/`LIVES`/`VECRAM+1`/`BUFRDY`/`VTIMER`; hardware `WTCHDG`/`POTGO`/`SOUND`/`SKCTL`/`VSTOP`/`GOADD` (all via `hw.h`); calls `moolah()`, `modsnd()`, `sndon()` (own module) and `robchk()` (`bzone_game.h`) |

Notes for callers: `nmi()` is the 6502 NMI vector, never `JSR`-called by anything in this ROM - a
driver/lockstep harness invokes it directly once per NMI tick (246.09 Hz). `sndon()`'s `x` parameter
is dead in this ROM (every real call site passes a nonzero `a`; see `NOTES_nmi_sound_coin.md`).
`moolah()`/`modsnd()` must be called with `g.dflag == 0` (binary mode) - `nmi()` already `CLD`s
before calling either, matching the ROM's own defensive `CLD` (`../disasm/CALLING_NOTES.md`
section 0).

## BZONE maths and 3D pipeline (owner: agent "maths": bzone_math.c)

All read/write RAM cells (`state_defs.h`) and hardware (`hw.h`'s `hw_mb_*`) directly, not
repeated per row. `regs.c` (see `regs.h`) is populated on every routine below that computes a
16-bit negate/add and hands its low+high bytes to a caller expecting the exact 6502 carry (COS/
SIN/SIN1/COSXTR/SINXTR - `xtrap` internally) - callers that chain an ADC/SBC off one of these
without their own explicit CLC/SEC must use `regs.c`, not assume 0 (see NOTES_math.md "BSIN
carry-chain fix" for why this matters).

| C prototype | ROM label ($addr) | notes |
|---|---|---|
| `uint8_t wait(void);` | WAIT $5B80 | low byte of the Mathbox result; caller reads `hw_mb_yhigh()` itself for the high byte |
| `void rotate(void);` | ROTATE $5946 | fills PTBLX2/PTBLY2/PTBLO2; calls game's `explde`/`tr7chk` (via `bzone_game.h`) |
| `uint8_t rotpnt(uint8_t y);` | ROTPNT $5B1F | y = PTBLX2/PTBLY2 write index in/out (advances by 2 if kept) |
| `void pntput(uint8_t x);` | PNTPUT $5D4A | x = index into PTBLO2/PTBLX2/PTBLY2; fills PNTTBL/TBLPTR |
| `void kludge(void);` | KLUDGE $5E05 | reads SAVE1/PTBLO2/DDEND/EXPOSZ/BOUNCE; starts the SZXD divide |
| `void bound(void);` | BOUND $5E44 | halves BOUNCE toward 0 |
| `regs cos_(uint8_t a);` | COS $5E4B | falls into SIN; `r.a`/`r.x` = low/high byte, `r.c` = exit carry (see above); named `cos_` not `cos` - integration fix, NOTES_integration.md: cl.exe reserves the bare name as a math intrinsic, `/O2` hard-errors on a same-name different-signature declaration (C2371), no pragma avoids it |
| `regs sin_(uint8_t a);` | SIN $5E4E | `r.a`/`r.x`/`r.c` as COS; named `sin_` for the same reason |
| `regs sin1(uint8_t a);` | SIN1 $5E63 | table read at its real ROM address $5E77 (self-relative in the source, no C equivalent); `r.c` is always 0 (proven: table index never exceeds $40) |
| `regs cosxtr(uint8_t a, uint8_t x);` | COSXTR $5EF9 | a = division bit (bit0 only), x = angle; also writes GARBAG |
| `regs sinxtr(uint8_t a, uint8_t x);` | SINXTR $5F17 | SIN counterpart of COSXTR, shares `xtrap` |
| `void abstmp(void);` | ABSTMP $6149 | TEMP1/TEMP1+1 := absolute value, in place |
| `uint8_t track(void);` | TRACK $6810 | returns the angle to the robot's target; dispatches T_0..T_3 internally via `switch` |
| `uint8_t t_0(uint8_t a);` `t_1` `t_2` `t_3` | T_0/T_1/T_2/T_3 $68D5/$68C6/$68CB/$68D0 | TTAB quadrant fix-up (push-address-then-RTS in the ROM); T_1 falls into T_2, T_3 falls into T_0; exposed non-static (even though only reached internally in this port) for direct lockstep coverage |

`xtrap` (XTRAP $5F32) is `static` - reached only by JMP-tail from COSXTR/SINXTR, never JSR'd
(CALLING_NOTES.md section 3), so it is not part of this module's public contract.

Calls out: `uint8_t explde(uint8_t y);` (EXPLDE $5B89) and `int tr7chk(void);` (TR7CHK $69B5),
both from `rotate()`, matching the signatures now frozen in `bzone_game.h` - see NOTES_math.md.

## BZONE display (owner: agent "display": bzone_disp.c)

All read/write RAM cells (`state_defs.h`), vector RAM (`(VGLIST),Y` direct stores via `cpu_wr`,
noted per row) and call VGUT (`vgut.h`) in the ROM's own order; not repeated per row. Full
ENTRY/EXIT documentation and the `Lxxxx`-commented translation are in `bzone_disp.c`; see
`NOTES_disp.md` for judgement calls. Status/progress: `NOTES_disp.md`'s RESUME table.

| C prototype | ROM label ($addr) | notes |
|---|---|---|
| `void bigwnd(void);` | BIGWND $51D9 | relies on VGUT's Y=3 exit invariant for its tail `vgstat` call - see file header comment in bzone_disp.c |
| `void wnshld(void);` / `pass_signal wnshld_body(void);` | WNSHLD $51E9 | trampoline routine (bzone_game.h): `_body()` returns `PASS_SIG_CKSCOR`/`PASS_SIG_BEND` at the ROM's `JMP CKSCOR`/`JMP B_END`; `wnshld()` runs the body once and drops the signal; calls `tankck()`/`robchk()`/`battle()` (CRACK-wrap fall-through); entered via `JMP` from MAIN, not `JSR` |
| `void battle(void);` | BATTLE $5268 | TRAP1 checksum over vector RAM, `(TBLPTR),Y` reads via `cpu_rd`, translated literally |
| `void batint(void);` | BATINT $531F | |
| `void ckscor(void);` / `pass_signal ckscor_body(void);` | CKSCOR $5344 | calls `sndon(K_SUPBON, 0)` (X unread since A!=0); both exits are `JMP B_END` -> `PASS_SIG_BEND` |
| `void hiscre(void);` / `pass_signal hiscre_body(void);` | HISCRE $53C5 | both exits are `JMP MAIN` -> `PASS_SIG_YIELD` |
| `void letchk(void);` | LETCHK $5465 | A-at-entry only feeds a discarded POTGO strobe write - not modelled as a parameter, see NOTES_disp.md |
| `void distbl(void);` | DISTBL $549B | `(VGLIST),Y` direct stores (tank-count loop, `A_VGMSGA`/`A_TSYMBL` from vector ROM via `cpu_rd`); decimal-mode add bracketed by `g.dflag` save/restore; also `pass_signal distbl_body(void)`, `JMP MAIN` -> `PASS_SIG_YIELD` |
| `void dspalp(uint8_t a);` | DSPALP $556A | a = character code; tail-calls `vgadd2` (DSPALP's own "return" is VGADD2's RTS) |
| `void finish(void);` | FINISH $5574 | writes vector RAM directly (`VRAM(A_VECRAM)`) for the TRAP1 anti-tamper copy |
| `void mounts(void);` | MOUNTS $57C8 | plus file-static `uint8_t mounts_50(uint8_t a)` (not a ROM label, $58A7); horizon skyline + volcano rocks |
| `void volcno(void);` | VOLCNO $58BA | per-rock state machine; one carry chain across all 5 rocks (C=1 on entry from MAIN), verified by lockstep - see NOTES_disp.md |
| `void draw(uint8_t a);` | DRAW $5C5C | a = object #; DTAB (opcode low 3 bits) -> `switch`, D_LABS/D_SPCL fall-through reproduced as literal `switch` fall-through; reads OBJTBL (ROM, via `cpu_rd`) |
| `void dovect(uint8_t a);` | DOVECT $5D11 | a = brightness; reads TEMP1/PNTTBL/CURNTX/CURNTY, calls `vgvtr2()` |
| `void d_next(void);` | D_NEXT $5D41 | DRAW's own loop back-edge (TBLPTR++), exposed standalone per bzone_disp.h for lockstep even though never a real JSR target |
| `void dradar(void);` | DRADAR $6AE9 | calls maths' `sin_`/`cos_`/`track`/`wait` and game's `tankck`/`r2d3ck`; Mathbox registers via `hw_mb_write`/`hw_mb_ylow`/`hw_mb_yhigh`, not RAM macros - see NOTES_disp.md |
| `void shupdt(void);` | SHUPDT $6C66 | processes shell index 2 then 0 (not a skip - see NOTES_disp.md) |
| `void msgs(uint8_t x);` | MSGS $6C98 | x = message # * 2; LNGTBL + ENGLISH/GERMAN/FRENCH/SPANSH flattened into one `switch` calling file-static `mstart(x, hi)` (MSTART $6CD4, not a JSR target, not in bzone_disp.h) |
| `void info(void);` | INFO $6D2E | life-tank loop stores `(VGLIST),Y` directly; calls `msgs()`/`digt2s()` |
| `void digt2s(uint8_t a);` | DIGT2S $7B9E | a = zp addr of the number; falls into `digtys` |
| `void digtys(uint8_t a, uint8_t y);` | DIGTYS $7BA0 | y = digit-pair count; falls into `digits` |
| `void digits(uint8_t a, uint8_t y, int c);` | DIGITS $7BA1 | c = 1 to suppress leading zeros; calls `vghexz` per digit (`vgut.h`) |

Status: all routines in this module's assignment are translated and compile `/W4` clean (see
`NOTES_disp.md`'s RESUME table). Both judgement calls once flagged for lockstep are settled:
VOLCNO's carry-in is a real chain (now modelled), DRADAR's asymmetric `XCOMP+3` sign-extend is
literal and passes. Every routine here passes lockstep (NOTES_integration.md).

## BZONE game logic (owner: agent "game": bzone_game.c)

Status: see `NOTES_game.md`'s RESUME table for what's translated vs. still
TODO. Only routines actually implemented and call-site-checked are listed
below; `bzone_game.h` has draft prototypes for the rest, not yet a frozen
contract.

- `void main_init(void)` - MAIN ($5000), the one-time part (currently empty:
  nothing this module owns runs only once before the first frame).
- `void main_pass(void)` - ONE trip around the frame loop: the pass_signal
  trampoline (`bzone_game.h`) that dispatches `bz_main_body()`/
  `b_end_body()`/`wnshld_body()`/`ckscor_body()`/`hiscre_body()`/
  `distbl_body()` until one yields at a ROM `JMP MAIN`. This is what a
  driver calls once per pass. `bz_main()`/`b_end()` below run only their
  own segment (body once, signal discarded) - the text below describing
  them as tail-calling each other predates the trampoline.
- `void bz_main(void)` - MAIN ($5000-$5158), one trip around the frame loop,
  from the SYNC frame-wait through B_END or a display tail call. No
  register inputs/outputs (MAIN is never `JSR`-called, only entered once
  from BZSTST and looped via `JMP`).  **This is the real name display's
  already-written WNSHLD/CKSCOR/HISCRE/DISTBL tail-call** - adopted from
  their `(proposed by display)` entry instead of the `main_pass` name this
  row used before EXPLDE was translated (nothing outside this module called
  `main_pass` yet, so the rename was free - see NOTES_game.md "bz_main/b_end
  split"). Calls into other modules: `hw_wait_frame` (hw.h, still a
  TODO(seam) prototype - not yet added there), `volcno`, `bigwnd`, `hiscre`,
  `distbl`, `mounts`, `battle`, `rotate`, `pntput`, `draw`, `wnshld`,
  `dradar`, `track`, `msgs` (all display/maths-owned, real headers now
  included). Also calls this module's own `tankck`, `r2d3ck`, `b_end`
  (below); falls through to `b_end()` at its own B_END label.
- `void b_end(void)` - B_END ($5159-$51D8), MAIN's mid-routine label: tank
  stats/game-over/FINISH/super-bonus, then the collision/AI tail (COLCHK x4,
  QWIKCK, ROBOT, SAUCMV, BOUND, MOTION), looping back to `bz_main()` at
  every one of B_END's own `JMP MAIN` sites (tail call, not a plain
  `return` - see NOTES_game.md for the resulting unbounded-C-recursion
  caveat, flagged for integration).  Reached both by falling out of
  `bz_main()` and directly by WNSHLD/CKSCOR's own `JMP B_END`.  Adopted from
  display's `(proposed by display)` entry the same way as `bz_main`.  Calls
  `info`, `msgs`, `finish` (display), `colchk`, `shupdt`(display's),
  `tr7chk`, `qwikck`, `robot`, `saucmv`, `bound`(maths-owned), `motion`
  (this module's own, `shupdt`/`bound` excepted).

- `uint8_t explde(uint8_t y)` - EXPLDE ($5B89): update every active
  explosion-piece slot's X/Y/Z position/velocity/orientation and append the
  survivors to PTBLX2/PTBLY2/PTBLO2 via `rotpnt()`; when every slot has gone
  inactive, clears `COLFLG+2` and calls `robchk()` (this module's own, not
  yet implemented - fine, self-contained forward reference).  ENTRY/EXIT
  y = the running PTBLX2/PTBLY2/PTBLO2 write index (ROTATE's, maths-owned -
  its only call site always passes y=0).  X/A are not real inputs (EXPLDE
  sets X=$0A itself before reading it).  Calls into other modules:
  `rotpnt(uint8_t y) -> uint8_t` (bzone_math.h), `hw_mb_write` (hw.h, for
  the XMATRX/YMATRX/YSTRT1 Mathbox registers - NOT RAM stores), `cpu_rd`
  (state.h, for the EXPTBX/EXPTBY ROM velocity tables at $3FAE/$3FBA, which
  live in the vector-ROM address range per `state.c`'s `cpu_rd`).  Calls
  this module's own `tr7chk()`/`robchk()` (not yet implemented at the time
  EXPLDE was written; declared in bzone_game.h, fine for a same-TU forward
  reference - implemented later per NOTES_game.md's RESUME order).

- `int qwikck(void)` - QWIKCK ($5F4E): quick shell-vs-robot check, player's
  shell (X=0) vs the robot (Y=2) only; no inputs, falls into `shrtck(0,2)`
  when a shell is in flight. Returns carry (0/1 as `int`): 1 = collision
  (SHRTCK has already set flags/counters).
- `int shrtck(uint8_t x, uint8_t y)` - SHRTCK ($5F5B): shell-vs-tank
  collision check, either direction. x = whose shell (0 player, 2 robot/
  buzz-bomb - SHELLX/SHELLY/FIRECT/HITS/SNDWRD/EXPCNT), y = whose position
  is the target (0 player tank, 2 robot - COLFLG/TPOSX/TPOSY/EXPCNT/
  SNDWRD). Returns carry (0/1): 1 = collision. Calls this module's own
  `newlif()`/`exinit()`/`tr7chk()` (not yet implemented - forward
  references, fine) and `hw_mb_write`/`hw_mb_ylow`/`hw_mb_yhigh` (hw.h) for
  the ACOS/BSIN/TPOSXT/TPOSYT/DSTNCE Mathbox distance function. Two
  same-shape-as-EXPLDE hazards worth flagging for lockstep: (1) the
  `CMP ($00,X)`/`LDA ($00),Y` pair right before reading the Mathbox result
  is a deliberate dummy-read delay (TRANSLATING.md's own worked example of
  this idiom) - emitted as nothing but a comment; (2) `TEMP1`'s second
  `ADC #$38` (L5FD1) has no `CLC` before it - its carry-in is deliberately
  whatever the prior `ADC TEMP1` left, not a fresh 0.

- `void colchk(void)` - COLCHK ($6045): per-frame shell-vs-tank/saucer/
  obstacle collision check for both shells (robot's, then the player's).
  No inputs/outputs. Calls `shrtck`/`sauchk` (this module's own, above).
- `void sauchk(uint8_t x, uint8_t y)` - SAUCHK ($60C2): saucer-vs-shell
  check, then always falls through to the playfield-obstacle scan
  (internal `static c21s_(uint8_t x)` helper, not exported - never `JSR`'d/
  `JMP`'d from outside this cluster) regardless of outcome. x = whose shell
  (FIRECT/EXPCNT,X, HITS score-add gate), y = whose SNDWRD gets the LOX bit
  on a hit. No return - the caller (`colchk()`) continues to COLCHK's own
  shared per-shell loop bottom either way. Calls `sndon(uint8_t,uint8_t)`
  (bzsoun.h) and this module's own `newlif()` (not yet implemented -
  forward reference, fine).
- `TR7CHK` is translated above. `ROBCHK`/`TANKCK`/`R2D3CK`/`BUZBOM`/
  `SAUCMV`/`OBJOBJ` are agent "enemy"'s (bzone_enemy.c) - see the "BZONE
  enemy AI" section below and NOTES_enemy.md.

Judgement calls worth flagging for lockstep/integration in the COLCHK/
SAUCHK/C_21S_ cluster: `C_21S_`'s obstacle-hit path stores to plain
(unindexed) `EXPCNT`/`SNDWRD` while `SAUCHK`'s own saucer-hit path and
`SHRTCK` both index them by X - translated exactly as the listing has it
in each routine (not "fixed" to be consistent - may be a genuine ROM
quirk, noted in NOTES_game.md). `PTBLX1`/`PTBLY1`/`PRXTBL` have no
state_defs.h alias (disassembler-local labels, not in bzone_defines.asm) -
referenced in `c21s_()` as literal ROM addresses ($7681/$76AB/$6139) via
`cpu_rd`.

- `void newlif(void)` - NEWLIF ($615F): award a bonus life or the
  super-bonus if the score just crossed the right DSW0-bonus-level
  threshold. No register inputs (its header's "USES (A)" just means it
  clobbers A, not that A is read); `TEMP1` is a hidden RAM input - the
  caller must set it to the score *before* the current increment
  (SHRTCK_18/SAUCHK's own `TEMP1 = HITS[,X]` right before their decimal
  HITS add is exactly this contract, NOT a dead store as this file
  originally guessed - corrected below). No return. Calls
  `sndon(uint8_t,uint8_t)` (bzsoun.h) and `hw_option()` (hw.h).
- `void exinit(uint8_t x)` - EXINIT ($6199): explosion-piece table init,
  robot-death only. **Signature corrected from the pre-existing draft**
  `void exinit(void)` - it genuinely reads x (2 = player exploded, does
  nothing at all; 0 = robot exploded, inits EXPOSX/EXPOSY/EXPOSO/EXPOSZ/
  ZVELOC for the 6 real explosion-piece slots from the robot's own
  TPOSX+2/TPOSY+2). x is unchanged on return either way (its own
  `LDX #$00` tail only runs on the x==0 path, where it's a no-op) - no
  caller needs an updated x back. Calls `hw_pokey_read(0x0A)` (hw.h,
  PRAND) and reads the ROM table `IZVEL` ($3FC6, `A_IZVEL`) via `cpu_rd`.

**Correction to the SAUCHK/SHRTCK entries above**: last session's "dead
store" read of `TEMP1 = HITS[,X]` right before the decimal HITS add was
wrong - `NEWLIF`'s own header ("TEMP1 MUST HAVE SCORE PRIOR TO CURRENT
INCREMENT") and body (`CMP TEMP1`) confirm it IS read, by `newlif()`,
which both `shrtck()` and `sauchk()` call immediately after. Not dead;
NOTES_game.md corrected too.

- `void motion(void)` - MOTION ($61D0): apply the player's tread-lever
  input for this frame (attract-mode auto-drive, or the real ALLPOT/MTAB
  dispatch via the internal `static m_top()`), unless the tank is flagged
  as colliding. No inputs/outputs; always x=0 (the player tank). Calls
  `hw_pokey_read`/`hw_pokey_write` (hw.h), `sndon` (bzsoun.h), `objobj`
  (this module), and the public primitives below.
- `void m_set(uint8_t x)` / `void m_for(uint8_t x)` / `void m_for1(uint8_t
  x)` / `void m_rev(uint8_t x)` / `void itangl(uint8_t x)` / `void
  dtangl(uint8_t x)` / `void saveit(uint8_t x)` / `void bakout(uint8_t x)`
  - generic tank/robot motion primitives ($6310-$63CA), x = 0 (tank) or 2
  (robot); called directly from outside MOTION by ROBOT/BUZBOM (confirmed
  by grep) as well as from MOTION's own internal dispatch. `m_set`/
  `m_for`/`m_rev` call `sin_`/`cos_` (bzone_math.h - note the trailing
  underscore, cl.exe reserves the bare `sin`/`cos` names as intrinsics,
  hard error even on a mere declaration mismatch - the maths agent already
  fixed this in bzone_math.h; see NOTES_game.md if it ever regresses).
- Every other `M_*` label (`M_TOP`, `M_PR`, `M_PL`, `M_LTF`, `M_LTR`,
  `M_RTF`, `M_RTR`, `M_FF`, `M_FR`, `M_F`, `M_R`, `M_NEXT`, `M_6S_`,
  `M_10S_`, `M_STOP`, `M_NEX0`) is `static` in `bzone_game.c` - never
  `JSR`/`JMP`'d from outside MOTION's own dispatch chain (confirmed by
  grep), so no header entry, same convention as `c21s_`. `M_NEX0` calls
  `cosxtr`/`sinxtr` (bzone_math.h) and `sndon` (bzsoun.h, via `M_NEXT`) and
  `objobj` (this module, via `M_NEXT`). MTAB's push-address/RTS dispatch
  decoded by hand into a `switch` - see NOTES_game.md "MOTION structure"
  for the full a->leg table (hand-derived, double-check if lockstep flags
  MOTION). Two `PHP`/`PLP` pairs in `M_NEX0` are simplified away (not
  translated literally) - verified equivalent, reasoning in NOTES_game.md
  ("MOTION structure") and in the code's own comment; this is a real
  judgement call, distinct from EXPLDE's own (kept-literal) PHP/PLP.
- `void react(uint8_t a)` - REACT ($63CB): set the robot's ACTION/SKILL.
  **Signature corrected** from the pre-existing draft `void react(uint8_t
  x)` - REACT never touches X; `a` is a real input on the ATRACT-bit7-set
  path (entered via `JMP`, not `JSR`, from R_RAND/R_ATCK - ROBOT family,
  not yet translated; their header says "REGISTERS: NONE", which looks
  stale - flagged in NOTES_game.md for whoever translates R_RAND/R_ATCK to
  verify/close out). The ATRACT-bit7-clear path's HITS-based computation
  block is PROVEN unreachable (`STA` never touches flags, so the following
  `BNE` is always taken) - translated as an unconditional `goto`, not a
  runtime branch; see NOTES_game.md for the proof.
- `int tr7chk(void)` - TR7CHK ($69B5): is the robot the TR7 tank? Returns
  1 (carry set) for TR7. Translated early (out of the brief's order) once
  its trivial 4-line body was read while confirming its carry convention
  for `explde()`/`shrtck()`. No inputs; reads `NOR2D3` only.

### ROBOT family (owner: agent "robot", bzone_robot.c - see NOTES_robot.md)

Status: **done**, compiles `/W4` clean (`NOTES_robot.md`'s RESUME line).
None of `R_37S_`/`R_40S_`/`R_50S_`/`R_EVAD`/`R_RAND`/`R_ATCK` is ever
`JSR`'d/`JMP`'d from outside `ROBOT`'s own control flow (grepped, same as
MOTION's internal `M_*` legs), but each has its own listing header/`[note]`
block and (for `FIREIT`) real multi-site internal reuse, so all seven get
real functions here rather than `goto` labels inside `robot()` - see
NOTES_robot.md for the full reasoning. The plain numbered `ROBOT_nn` locals
are NOT exposed (internal control flow only, `goto`-labelled inside
`robot()`).

- `void robot(void)` - ROBOT ($6418-$64ED): enemy-tank AI top level -
  collision back-away, decision-time hand-off to `r_40s_()`, turn/fire
  toward RGOAL, then a Mathbox distance check deciding whether to advance
  (`r_37s_()`). No inputs/outputs; x=2 (the robot) hardwired throughout.
  Calls this module's own `r_37s_`/`r_40s_`/`fireit` (below), "game"-owned
  `saveit`/`m_rev`/`itangl`/`dtangl` and `tr7chk` (bzone_game.h), "enemy"-
  owned `buzbom`/(indirectly via `fireit`/`r_37s_`) `objobj` (draft
  prototypes, bzone_game.h), `hw_mb_write`/`hw_mb_yhigh` (hw.h). One real,
  necessary `PHP`/`PLP` carry save kept literal (ROBOT_2, around `JSR
  M_REV`) - see NOTES_robot.md "ROBOT structure". `ROBOT_30`'s Mathbox
  distance check reads `TEMP1`/`TEMP2` as a hidden RAM input (whatever
  `TRACK` last left there, possibly stale from an earlier frame) - not
  recomputed by `ROBOT` itself, flagged for lockstep.
- `void r_37s_(void)` - R_37S_ ($64EE-$6531): move forward toward RGOAL
  (doubled speed via ASL/ROL if `tr7chk()`), OBJOBJ-checked; any collision
  tail-calls `bakout(2)`. **Corrects the brief's own paraphrase** ("sets a
  STATE bit and ACTION=$30 unless an attract-mode hit"): the real gate,
  read from the bytes, is the OPPOSITE - attract mode ALWAYS sets STATE/
  ACTION, and it is a REAL-GAME collision specifically WITH THE PLAYER TANK
  (OBJOBJ's own `A` return negative) that skips the escalation - matches
  the ROM's own inline comment `;D IF COLLIDED WITH PLAYER` verbatim. See
  NOTES_robot.md "R_37S_ structure" for the full trace. Calls `m_set`/
  `m_for1`/`bakout`/`tr7chk` (bzone_game.h), `objobj` (bzone_game.h, "enemy"
  draft), `hw_pokey_read(0x0A)` (hw.h, PRAND).
- `void r_40s_(void)` - R_40S_ ($6534-$6537): decision-time entry
  (ACTION==0); attract mode -> `r_rand()`, else falls into `r_50s_()`.
- `void r_50s_(void)` - R_50S_ ($6538-$6551): in-game decision - FTIMER==
  $FF, a PRAND coin-flip, or HITS+1!=0 -> `r_atck()`; else HITS vs HITS+2:
  equal -> `r_evad()`, HITS<HITS+2 -> `r_rand()`, HITS>=HITS+2 ->
  `r_atck()`. Calls `hw_pokey_read(0x0A)` (hw.h, PRAND).
- `void r_evad(void)` - R_EVAD ($6553-$6570): evasive heading - most
  frames just flags "still evading"; every 8th frame (S_INTCT&7==0)
  re-tracks the player and heads the opposite way. Calls `track()`
  (bzone_math.h).
- `void r_rand(void)` - R_RAND ($6571-$6586): random heading offset from
  PRAND&$1F, combined with the old RGOAL by ADC or SBC depending on
  S_INTCT's sign bit; tail-calls `react(0x00)`. **Entry-carry hazard**
  (`SBC RGOAL`/`ADC RGOAL`, no CLC/SEC upstream): both of this routine's
  real call sites are proven, by hand-tracing back through `ROBOT_2`'s own
  LSR chain and `R_50S_`'s own `BCC`, to always arrive with carry clear -
  modelled as a local `c = 0`, not a parameter. Full trace in
  NOTES_robot.md "R_RAND entry-carry proof". Calls `hw_pokey_read(0x0A)`
  (hw.h, PRAND) and `react()` (bzone_game.h).
- `void r_atck(void)` - R_ATCK ($6589-$6592): track the player, set
  STATE=$80 (attacking), tail-call `react(0x80)`. Calls `track()`
  (bzone_math.h) and `react()` (bzone_game.h).
- `void fireit(void)` - FIREIT ($6595-$6623): range/angle-gated shell
  firing, robot only. **Signature corrected** from the pre-existing
  `bzone_game.h` draft `void fireit(uint8_t x)` - every cell it touches
  uses a hardwired `+2`/`+3` literal offset (never `,X`), and it is only
  ever called from within `ROBOT` with x=2; not a generic tank/robot
  primitive despite the family resemblance to SAVEIT/ITANGL/etc. Possible
  ROM quirk translated faithfully (not "fixed"): its own "fire!" block
  writes the TANK's (unindexed) SNDWRD, not SNDWRD+2 - same shape of
  asymmetry already flagged for C_21S_/SAUCHK vs. SHRTCK in NOTES_game.md.
  Calls `track()`/`cos_()`/`sin_()` (bzone_math.h).

R_RAND/R_ATCK's tail-calls to `react()` close NOTES_game.md's own open flag
("REGISTERS: NONE... re-verify once R_RAND/R_ATCK are translated"):
confirmed by hand-tracing, `react()` is entered with `A=0x00` from
`r_rand()` and `A=0x80` from `r_atck()` - `bzone_game.h`'s already-corrected
`void react(uint8_t a)` signature is right, nothing further to change on
either side.

(`b_end`/`bz_main` were display's `(proposed by display)` placeholders in
an earlier version of this file; now implemented for real above - see the
`bz_main`/`b_end` entries.)

### BZONE enemy AI (owner: agent "enemy", bzone_enemy.c - see NOTES_enemy.md)

Status: **done**, compiles `/W4` clean (`NOTES_enemy.md`'s RESUME table).
BUZBOM/SAUCMV/OBJOBJ/ROBCHK/TANKCK/R2D3CK signatures below match the
pre-existing drafts in `bzone_game.h` unchanged (all confirmed against the
actual code, not just the header). ROB1's own draft prototype
(`void rob1(void);`) is removed from `bzone_game.h`: grepping the whole
disassembly shows it is never `JSR`'d/`JMP`'d from outside this cluster
(reached only via TANKCK's own tail `JMP`/`BEQ`, or R2D3CK's fall-through)
and it genuinely reads an input `a` the old no-argument draft didn't
account for - it is `static void rob1(uint8_t a)` in bzone_enemy.c, same
convention as bzone_game.c's own internal-only fall-through targets
(`c21s_`, `m_top`, etc. - no header entry).

- `void buzbom(void)` - BUZBOM ($6624-$677B): R2D3 buzz-bomber AI/motion,
  "called in lieu of ROBOT". No inputs/outputs. Calls `track()`/`sin_()`/
  `cos_()` (bzone_math.h), `m_for1`/`qwikck`/`exinit` (bzone_game.h, agent
  "game"'s module) and `objobj()` (below). Atari's own header
  ("SUBROUTINES: TRACK,M.FOR,OBJCOL") is stale - also calls QWIKCK/OBJOBJ/
  EXINIT/SIN/COS - same pattern already flagged for TRACK/PNTPUT/DRAW's own
  headers (`../disasm/CALLING_NOTES.md`). Two genuine residual-carry
  hazards (not fresh CLC/SEC) and one deliberate precision-loss simplification
  (the SIN/COS result's low byte feeds only the `x4`-scale carries and is
  never stored, same shape as MOTION's own SINCX/SINCY finding in
  NOTES_game.md) - see NOTES_enemy.md "BUZBOM structure".
- `void saucmv(void)` - SAUCMV ($677C-$680F): flying-saucer entrance/exit/
  motion. No inputs/outputs beyond the RAM cells Atari's own header names.
  One genuine (not always-taken) conditional at the very end (`INC SAUCER`/
  `BNE SAUCMV_30` - skipped only in the 1-in-256 case SAUCER wraps to 0).
- `regs objobj(uint8_t x)` - OBJOBJ ($68D6-$6994): object-object collision
  detect. Signature unchanged from the pre-existing `bzone_game.h` draft
  (x = 0 player tank / 2 robot tank in; r.c = carry, r.a = 0 nontank / -1
  tank out). CALLING_NOTES.md section 3 lists OBJOBJ as one of WAIT's
  callers - the actual code never calls `wait()`, reading `hw_mb_ylow()`/
  `hw_mb_yhigh()` directly after a dummy delay read instead (same idiom as
  `shrtck()`/`sauchk()`/`c21s_()`, none of which call WAIT either) - treated
  as a stale claim in that note. One provably always-taken branch (an
  `LDA #$04` clobbers the very Z flag the next `BNE` tests, same idiom as
  VGADD's own tail-branch hazard in CALLING_NOTES.md section 1) and one
  genuine residual-carry/register hazard at OBJOBJ_50 (the "both tanks
  already colliding" early exit reaches the shared return with `r.a` =
  `COLFLG|COLFLG+2`, NOT the documented 0/$FF, and `r.c` = whatever the
  last obstacle-loop iteration's own compare left) - flagged for lockstep,
  see NOTES_enemy.md "OBJOBJ structure".
- `void robchk(void)` - ROBCHK ($69BE): robot reset check, shared with the
  NMI handler (`../disasm/CALLING_NOTES.md` section 7: called from both
  NMI_65 and mainline). No inputs/outputs; every exit tail-calls
  `tankck()` or `r2d3ck()` (both below). Atari's own header
  ("SUBROUTINES: RAND, OBJBOJ,ROBOT") is stale - none of those are called
  directly from here.
- `void tankck(void)` - TANKCK ($69E8): reset the robot as a plain tank;
  in attract mode also computes a random placement window ahead of the
  player. No inputs/outputs; tail-calls the `static rob1(a)` below. Called
  directly too, from outside this cluster (`bz_main`'s MAIN/MAIN_1,
  `dradar()` - display's).
- `void r2d3ck(void)` - R2D3CK ($6A22): (re)spawn the buzz bomber instead
  of a tank. No inputs/outputs; falls straight into the `static rob1(a)`
  below with `a = PRAND & $0F`. Called directly too (`bz_main`, `dradar()`).
  `EXPOSZ+$C`/`EXPOSZ+$D` are set from `A_STARTZ` (state_defs.h: a literal
  16-bit constant here, `$1800`, NOT the Mathbox/MSTAT address it happens
  to numerically share - the state_defs.h comment on `A_STARTZ` says so
  explicitly).

`buzbom`/`saucmv`/`objobj`/`robchk`/`tankck`/`r2d3ck` are all now real,
verified contracts (not drafts) - callers in other modules (agent "robot"'s
`robot()`/`r_37s_()`/`fireit()`, per the ROBOT family section above; agent
"game"'s `b_end()`) can rely on them.
