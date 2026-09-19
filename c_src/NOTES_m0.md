# M0 notes - scaffold, state, seam, Mathbox, oracle

What was copied from where, what was adapted and why, and every
hardware-model decision with its evidence.  Companion to `PLAN.md`'s M0 row.

## 1. What was copied verbatim (file by file)

| file(s) | from | why |
|---|---|---|
| `mathbox.c`, `mathbox.h` | `C:\Source2026\Tempest Dissasembly\c_src\mathbox.c/.h` | unchanged (see section 2: PROM byte-identity re-verified here, not just cited) |
| `c012294.c`, `c012294.h` | `C:\Source2026\Tempest Dissasembly\c_src\c012294.c/.h` | PLAN.md: "POKEY - one chip at $1820 - `c012294.c/.h` verbatim" |
| `tests\ref6502\ref6502.c`, `ref6502.h` | `C:\Source2026\shared\ref6502\` | the user's standing instruction (`CLAUDE.md`): never write a new 6502 core |

No byte of any of these five files was edited after copying (`fc`-equivalent
diff not re-run here, but they are plain `cp` copies, not re-typed).

## 2. Mathbox PROM comparison (PLAN.md M0 deliverable 4)

Verified two ways:

1. **Directly, byte for byte**, in this session, comparing the Battlezone
   ROM survey's extracted PROM files (`disasm\_survey\roms\bzone\`, from the
   project's own ROM set - not re-unzipped from `roms\bzonea.zip` by hand)
   against Tempest's `disasm\_survey\roms_extracted\tempest3\`:

   | Battlezone | Tempest | result |
   |---|---|---|
   | `036174-01.b1` (32B, mapping) | `136002-126.a1` | **MATCH** |
   | `036175-01.m1` (256B) | `136002-132.l1` | **MATCH** |
   | `036176-01.l1` | `136002-131.k1` | **MATCH** |
   | `036177-01.k1` | `136002-130.j1` | **MATCH** |
   | `036178-01.j1` | `136002-129.h1` | **MATCH** |
   | `036179-01.h1` | `136002-128.f1` | **MATCH** |
   | `036180-01.f1` | `136002-127.e1` | **MATCH** |

   All 7 PROMs byte-identical, taking care of the reversed part order
   MATHBOX_NOTES.md documents (Battlezone's 174..180 = Tempest's
   126, 132..127 - note the microcode six run **high-to-low** in Battlezone's
   part numbering where Tempest's run low-to-high).
2. **Automatically, every regeneration**: `tools\gen_roms.py` re-does the
   same comparison (`cmp`-equivalent, not just a CRC) before it emits
   `mbprom.c/.h`, and refuses to write the files if a single byte differs.
   Ran clean: `Mathbox PROM check: all 7 PROMs byte-identical to Tempest's
   ... - reuse confirmed`.

Conclusion: `mathbox.c`/`mbprom.c` are the same hardware as Tempest's,
confirmed independently of MATHBOX_NOTES.md's CRC32 citations - safe to
reuse unchanged, only the bus addresses in the seam differ ($1800/$1810/
$1818/$1860+n here vs Tempest's $6040/$6060/$6070/$6080+n).

## 3. Generators (PLAN.md M0 deliverables 1-2)

- `tools\gen_state.py` -> `state_defs.h`, adapted from Tempest's script:
  - `RAM_END = 0x0400` (Battlezone RAM is $0000-$03FF, 1 KB - not Tempest's
    2 KB $0000-$07FF).
  - `VAR_SECTIONS = ("Zero page variables", "Game RAM")` - Battlezone's
    defines file has no separate "Stack page" section (the 6502 stack
    $0100-$01FF is unnamed, folded into "Game RAM").
  - **New**: besides the scalar `NAME` lvalue, every RAM cell also gets
    `NAME_AT(i)` = `(g.ram[(A_NAME + (i)) & 0x3FF])` - the milestone's own
    requirement for the tank/robot twin-cell idiom (`NAME,X` with X=0/2:
    `COLFLG`/`SAVE1`, `FIRECT`/`DQUE`, `TANGLE`/`NTHETA`, `OBJCOL`/`EIRNGE`,
    `SNDWRD`/`SAVE2`+1, `SHELLX`/`SHELLY`/`SINCX`/`SINCY`, `LANGLE`,
    `HITS`...).  Emitted for every cell, not just the documented twins
    (cheap, and the idiom recurs beyond the list PLAN.md calls out).
    Verified: `COLFLG_AT(2)` and `SAVE1_AT(1)` both expand to
    `g.ram[(A_COLFLG+2)&0x3FF]` = `g.ram[0x14]`, matching the defines file's
    own note ("COLFLG+2 lands on SAVE1+1").
  - Ran: 323 addresses, 121 RAM cells (242 macros with `_AT`), 73 constants.
    `S_S_CRDT` (the `$` = `S_`-mangled `$CRDT`) came through unmodified, as
    instructed.
- `tools\gen_roms.py` -> `progrom.c/.h` ($5000-$7FFF, 12288 bytes),
  `vecrom.c/.h` ($3000-$3FFF, 4096 bytes), `mbprom.c/.h`, adapted from
  Tempest's script:
  - Different base addresses/sizes (Battlezone's program ROM is 12 KB at
    $5000, not Tempest's 20 KB at $9000).
  - Vector-sanity check uses **$7FFA**, not $FFFA (Battlezone's own vector
    table location before the 15-bit-decode mirror - HARDWARE_NOTES.md
    "The 15-bit address decode / ROM mirror").
  - Also emits `mbprom.c/.h` from Battlezone's *own* PROM dump (not a copy
    of Tempest's `mbprom.c`) with the reversed nibble order noted above, and
    self-checks against Tempest's PROMs (section 2) before writing.
  - Same two source-derived assertions as Tempest's script (RSTO's "LDL 0
    LDAB 16 JMP" at uPC $20 = `$1073B4`; RDISP's uPC $10+n = `$0n3B10`) -
    both passed, as expected for byte-identical microcode.

No ROM byte was hand-transcribed; both generators read
`disasm\_survey\roms_extracted\bzone_cpu64k.bin` / `disasm\_survey\roms\bzone\`.

## 4. `state.h` / `state.c` / `hw.h`

Modelled on Tempest's, with Battlezone's own facts:

- `machine_state.ram[0x400]` (not `[0x800]`), `vram[0x1000]`.  **No colour
  RAM** (Battlezone's AVG is monochrome - HARDWARE_NOTES.md, MATHBOX_NOTES.md
  "no colour latch bit exists on this board").  **No EAROM field** (high
  scores are volatile RAM - HARDWARE_NOTES.md "EAROM").
- `cpu_rd`/`cpu_wr` mask the address to 15 bits (`& 0x7FFF`) before ranging,
  reproducing the board's mirror (HARDWARE_NOTES.md) instead of special-
  casing `$8000+`.
- `adc6502`/`sbc6502` (BCD-correct ADC/SBC) carried over unchanged from
  Tempest's state.h, ready for when `HITS` (the BCD score, CPORT_READINESS.md
  "Decimal (BCD) mode") is translated - not exercised by M0's scaffold, kept
  now so state.h's shape matches the template and needs no interface change
  later.
- `hw.h`: one function per register in PLAN.md section 1's table (`hw_in0`,
  `hw_option`, `hw_opton2`, `hw_pokey_read/write`, `hw_mb_*`, `hw_emcnt`,
  `hw_goadd`, `hw_wtchdg`, `hw_vstop`, `hw_sound`).  `hw_goadd(uint8_t page)`
  takes the written byte as a parameter rather than being a bare strobe -
  see section 6, "GOADD's data byte".  No implementation (`hw.c`) yet: no
  game code exists to call it (M0 is scaffold only); compiles clean on its
  own (`obj\hwcheck.c` smoke test, `/W4`, then deleted).

## 5. `tests\refrun.c` - the oracle

Adapted from Tempest's `tests\refrun.c`, trimmed to what M0 needs (no
EAROM, no lockstep hooks, no self-test dual-loop-head machinery - those
belong to later milestones) and extended for Battlezone's differences:

- **NMI, not IRQ.**  Battlezone's NMI vector (`NMI`, $558F) is a real,
  separate, edge-triggered, unmaskable 6502 NMI (unlike Tempest's board,
  which wires IRQ and NMI to the same address and is modelled as `cpu_nmi()`
  gated by the I flag).  The harness raises `nmi_pending` only while IN0 b4
  is set (self-test off - HARDWARE_NOTES.md "NMI: rate, source, and a
  header typo") and delivers it unconditionally at the next instruction
  boundary, no I-flag poll.
- **DIP defaults** read directly from `..\bzone.cpp`'s
  `INPUT_PORTS_START(bzone)` (`PORT_DIPNAME`/`PORT_DIPSETTING` default
  values, not HARDWARE_NOTES.md's prose paraphrase): `OPTION` ($0A00) =
  `$15` (lives 01=3, missile 01=10000, bonus-life 01=15k&100k, language
  00=English); `OPTON2` ($0C00) = `$02` (coinage 10=1c/1p, coin B 00=x1,
  coin A 0=x1, bonus coins 000=none).  `bzone.cpp:420-461` cross-checked
  bit for bit against HARDWARE_NOTES.md's table (matches).
- **ALLPOT/RANDOM through the real POKEY model** (`c012294.c`'s
  `ad_pokey_*`), per the milestone's explicit instruction, not an inline
  LFSR the way Tempest's *current* oracle does it (Tempest's own history
  shows it started this way and later simplified - PLAN.md for this port
  asks for the chip model, so that's what's wired in).  `ad_pokey_advance()`
  is called lazily (`pokey_sync()`) before every register access, which the
  chip's own contract guarantees gives identical results to per-cycle
  stepping.  `ALLPOT` is set from the script's switch byte via
  `ad_pokey_set_allpot()`, matching `bzone.cpp`'s `bzone_IN3_r` bit layout
  (rt_down/rt_up/lt_down/lt_up/fire/start1/start2 - `bzone.cpp:473-479`).
- **`--mbtest`**: transcribes `battlezone-main\MBDIAG.MAC`'s own `TESDAT`
  test table (30 records) and replays its exact algorithm (write offset+
  value, then check `YLOW`/`YHIGH` against the record's expected bytes) -
  not a from-scratch formula test the way Tempest's `mbtest()` is.  Verified
  by hand-decoding the first ~18 records against MBUDOC's documented
  formulas (e.g. record 11, `YHSM` with `A=$7F00 X=$0100`: `(X-E)*A` =
  `256*32512 = 0x7F0000`, `>>16 = 0x7F` - matches the table's expected
  `$007F` exactly) before trusting the rest to the microcode itself.
  **Result: 30/30 PASS.**
- **Frame capture point**: `MAIN` ($5000) is `LSR SYNC` / `BCC MAIN` - a
  spin loop revisited many times per NMI (MAINLINE_NOTES.md "Frame sync").
  Capturing PC==$5000 would dump many times per real frame.  Read the
  listing (`bzone_program_rom.asm:39-42`) to find the instruction the BCC
  falls through to when SYNC's carry bit finally comes out: `$5004`
  (`JSR VOLCNO`) - reached exactly once per real frame.  `--snap-pc`
  defaults there.
- **Watchdog**: tracked (kicks, max gap between kicks) against
  `--watchdog-cycles` (default 1,000,000, deliberately generous - no source
  gives Battlezone's actual hardware timeout, same situation Tempest's own
  notes flag for its board).  Not exercised as a reset/reboot path in M0 (no
  self-test scripting yet) - PLAN.md's exit criterion is just that it never
  fires, which it does not: measured max gap 6155 cycles (barely over one
  NMI period, from the NMI's per-interrupt `STA WTCHDG`) against the
  1,000,000-cycle default, a ~160x margin.

## 6. Hardware-model decisions, with evidence

### GOADD's data byte is the buffer page, not a bare strobe

HARDWARE_NOTES.md's own table calls `GOADD` a "strobe: start the vector
generator on the current list" without saying what "current" means or
whether the written value matters.  Reading the actual NMI code settled it
(`bzone_program_rom.asm:1062-1081`, quoted in `MAINLINE_NOTES.md`):

```
L56A2:  LDA  VECRAM+1               ;C SWITCH BUFFERS
L56A5:  EOR  #$04
L56A7:  STA  VECRAM+1
L56AA:  AND  #$04
L56AC:  BNE  NMI_25                 ;D IF LOWER BUFFER AVAILABLE
L56AE:  LDA  #[VECRAM/$100]+8       ;C USE UPPER BUFFER
L56B0:  BNE  NMI_26
NMI_25:
L56B2:  LDA  #VECRAM/$100           ;C USE LOWER BUFFER
NMI_26:
L56B4:  STA  BUFRDY
NMI_30:
L56B6:  STA  GOADD                  ;C START UP VECTOR GENERATOR FOR 24 MS.
```

`A` is loaded with `$20` (`VECRAM/$100`) or `$28` (`+8`) and **the same
accumulator value** is stored to both `BUFRDY` and `GOADD` - the write to
`GOADD` is not an arbitrary strobe, its data byte is literally the buffer's
page.  Separately, `MAIN_5` (`bzone_program_rom.asm:45-49`) sets `VGLIST` to
that same page, **plus 2**:

```
L5007:  LDX  BUFRDY                 ;S USE AVAILABLE BUFFER
L500B:  LDA  #[VECRAM&$FF]+2
L500D:  STA  VGLIST
L500F:  STX  VGLIST+1
```

So the mainline always starts *writing* a display list at `page*$100 + 2`.
Modelled: `vg_start_addr = (page << 8) + 2` - the AVG is walked from
exactly where the 6502 started writing, which is also the only address the
"start" byte and the write cursor agree on.  The 2 reserved bytes at
offset 0-1 are `FINISH`'s conditional `TRAP1` security byte
(`bzone_program_rom.asm` `FINISH`, MAINLINE_NOTES.md "A trap worth knowing
about") - consistent with them never being part of the AVG's own read
stream.  This is inferred from the source, not confirmed against a
schematic or MAME's `avgdvg_go_w` (not available to read in this session);
flagged the same way HARDWARE_NOTES.md flags its own inferences.  **The
strongest evidence it is right: TRAP1 comes out as exactly 1** (section 7)
- and TRAP1 is pure 6502-side arithmetic over bytes the CPU itself wrote,
completely independent of this AVG-address assumption, so a wrong GOADD
model could not have produced that result by accident.

### AVG draw time: authentic state-PROM timing (mid-task correction)

The M0 brief's original plan used a fixed `--vg-draw-cycles` guess (4000
cycles).  The coordinator's mid-task instruction required the real timing
instead, matching Tempest's `avg.c` TIMING model: since Tempest and
Battlezone share the same AVG state PROM (`c_src\avg.h`'s own header:
"AVG state PROM, for reference ... CRC32 `5903AF03`" - the same figure
MATHBOX_NOTES.md gives), the per-opcode/per-vector tick formula is
authentic hardware behaviour, not an approximation specific to either game.

The other agent's `c_src\avg.c`/`avg.h` (Gate V, shape-only) does not carry
a cycle field, and per the coordinator's explicit direction **that file was
not touched**.  Instead `tests\refrun.c` computes the draw time itself,
locally (`avg_norm_shifts`/`avg_timer_reg`/`avg_draw_cycles`, transcribed
from Tempest's `avg.c` `norm_shifts`/`timer_reg` and the per-opcode
`TICKS(n)` sums it documents in its `avg.h` "TIMING" section) - kept as a
small, clearly-marked, file-local stand-in rather than copying Tempest's
whole `avg.c`, so it can never collide with the other agent's work.
**TODO for whoever picks this up next**: once `c_src\avg.c` grows an
`avg_result.cycles` field, delete `avg_draw_cycles()` and its two helpers
from `tests\refrun.c` and call that instead - both walk the identical
opcode stream (already cross-checked against `../../disasm/avg.py`), so the
switch should be a pure deletion, not a behaviour change.

Model: at the `GOADD` that starts a list, the harness classifies the list
(HALT / LOOP / BAD, via the existing `avg_walk()`) and, only if it ends in
`HALT`, computes its total draw time and sets `vg_busy_until = T +
ceil(avg_cycles / 8)`.  A list that loops or is malformed never reports
halted on its own (matches a real AVG free-running a looping list until
reset - the same rule Tempest's oracle uses).  `VSTOP` halts at once
regardless.  IN0 bit 6 polarity was checked against **both**
`bzone_program_rom.asm`'s `BIT HALT`/`BVS` idiom (bit 6 = the V flag; the
NMI/`BZSTST` code branches on V *set* meaning "done") and `HARDWARE_NOTES.md`'s
citation of `../bzone.cpp`'s `avgdvg_done()` (`IN0_VG_HALT`, "set when the
vector generator has finished/halted") - both agree bit 6 = 1 means halted,
which is what `in0_byte()` implements.

Measured over 600 attract-mode frames (`tests\refrun.exe --frames 600`):
1649 `GOADD`s, **average draw time 23,611 state-PROM-timed cycles = 15.62
ms** (min 2 cycles / 0.001 ms - a nearly-empty list; max 48,159 cycles /
31.85 ms), and the AVG was still busy when a 6-NMI VTIMER window tried to
halt it (a forced `VSTOP` while not yet finished) **4 times out of 1650
(0.2%)** - i.e. the picture almost always finishes comfortably inside its
~24.4 ms (6 x 4.06 ms) budget, with only rare, short overruns.  This is
recorded per-pass in `tests\ref\frame_sched.txt` (new `draw_cycles`/
`draw_ms` columns) and per-capture in `ref_index.json`.

### TRAP1

Exit criterion: "in attract mode TRAP1 ($0370) must come out as 1... report
the value you observe."  **Observed: `TRAP1 = 1`**, every run (600 frames,
5000 frames, the `coin_start` scenario), byte-identical across two full
600-frame runs into the same `--outdir` (see section 8).  No further
investigation was needed - the hardware model is consistent with real
hardware on this specific, security-critical check.

## 7. `--mbtest`

`tests\refrun.exe --mbtest`: **PASS, 30/30 records** (MBDIAG.MAC's own
`TESDAT`, transcribed - see section 5).  `steps 292, runaway 0`.

## 8. `refrun --frames 600` result

```
REFRUN: stop = frames reached
  passes 600 (target 600), cycles 62325498 (41.22 s), NMIs raised 10144 taken 10144 lost 0, max NMIs/pass 247
  watchdog kicks 10400 (max cycles between kicks 6155, timeout 1000000), BRK executed 0
  GOADD (VG start) 1649, VSTOP 1650 (list walks for HALT 1649), vector-RAM writes 425042 (1536 non-zero bytes)
  display list from $2002: 930 AVG ops, 632 vectors (402 beam-on), ends in HALT
  AVG draw time (authentic state-PROM timing, avg_draw_cycles()): 1649 lists, avg 23611 cyc (15.616 ms), min 2 cyc (0.001 ms), max 48159 cyc (31.851 ms)
  6-NMI VTIMER windows where VSTOP found the AVG still busy: 4 / 1650 (0.2%)
  RANDOM reads 1091, ALLPOT reads 0, POKEY writes 42782, EMCNT writes 10144, SOUND writes 10144
  Mathbox starts 338677 (8679838 microsteps, 0 runaway, 0 xor-sign)
  unmapped reads 234, writes 0
  RAM: ATRACT($00CE)=$00 GOVER($00CD)=$01 SYNC($0039)=$00 TRAP1($0370)=1 TRAP2($02ED)=$0001 $$CRDT($000E)=$00 SP $7F PC $5004
  distinct PCs 2222; captured 34 frames into tests\ref
REFRUN: PASS
```

- Display list is sane: 930 AVG ops, decodes cleanly, **ends in HALT** (no
  `AVG_BAD`/`AVG_CAP`).
- `--frames 5000` also PASS with the same shape of numbers (13,819 `GOADD`s,
  avg draw time 15.24 ms, 10/13820 = 0.1% VSTOP-while-busy, TRAP1 still 1).
- The `coin_start.txt` scenario (section 9) shows `ATRACT` flip from `$00`
  to `$FF` after the scripted coin + START, i.e. the NMI's coin/start
  handling drives the game out of attract mode under this hardware model -
  a strong independent check on IN0/ALLPOT scripting, beyond what M0
  strictly requires.

## 9. Determinism

Two full `--frames 600` runs into the same `--outdir`: **stdout identical,
all 71 output files (34 x `.ram` + 34 x `.vram` + `frame_sched.txt` +
`ref_index.json` + `coverage_pcs.txt`) byte-identical (MD5)**.  (Two runs
into *different* `--outdir`s naturally differ only in the embedded path
text in the final summary line - not a determinism issue, just confirmed by
also running the same-`--outdir` comparison above.)

## 10. Unresolved / open items (not blocking M0)

- **234 unmapped reads in 600 frames** (733 in 5000), all from a single PC,
  `$6B79` (`CMP ($00,X)`, inside code that also stores to the Mathbox's
  `DST2` register just before - looks like distance/AI code, not traced
  further).  The indirect pointer this instruction dereferences lands on
  several different unmapped addresses across the run ($4EA0, $0C21, $1C29,
  $12FF, $053B, $058C, ...), always read (never written), and the run's
  `REFRUN: PASS` / `TRAP1 = 1` are unaffected either way (the harness
  returns 0 for an unmapped read, which only affects a comparison flag the
  ROM branches on).  Left as "harness returns 0, ROM branches on it, not
  further investigated" - worth a closer look once `ROBOT`/`TRACK` are
  translated (PLAN.md M3-M5), when lockstep can pin down exactly what the
  ROM is trying to read there.
- **Watchdog bite/reset is not implemented** (only tracked/flagged) - not
  needed until a self-test-style scenario exists (later milestone).
- **`hw.h` has no implementation yet** (no `hw.c`) - correct for M0 (no game
  code exists to call it); a later milestone's `app_loop.c`/`lockstep.c`
  each implement it, the same split Tempest's `hw.h` documents.
- **AVG timing model duplication**: see section 6, "AVG draw time" - the
  `avg_draw_cycles()` stand-in in `tests\refrun.c` should be deleted once
  `c_src\avg.c` exposes its own `cycles` field (not this agent's file to
  edit).
