# Battlezone disassembly - status (2026-09-18)

## Method

Same as Tempest / Space Duel: Atari's own source (`../battlezone-main/*.MAC`) is
re-assembled by `macasm.py` and must reproduce the ROM byte for byte; the listings,
defines and shapes page are generated from that assembly, so every name and comment
is Atari's. Nothing is hand-edited: fix the tool, regenerate.

Whole chain, one command (last run: **all steps passed**):

```
cd disasm
python gen_from_roms.py --check
```

## Verified results

| check | result |
|---|---|
| source re-assembly vs ROM (`macasm.py`) | program $5000-$7FFF 12288/12288, vector ROM 4093/4096 (the other 3 are `00` fill at $3FFD), 0 mismatches, 167/167 code labels decode to the source mnemonic, BZONE.MAP globals agree |
| `bzone_program_rom.asm` read-back (`verify.py`) | 12288 of 12288 bytes, 4269 instructions, MISMATCHES 0 |
| ca65/ld65 round trip (`emit_ca65.py`) | 12288 bytes, 0 mismatches |
| `bzone_vector_rom.asm` read-back (`verify_vrom.py`) | 4096 bytes, 132 symbols, 0 MISMATCHES |
| RAM coverage (`check_ramrefs.py`) | 1441 memory operands below $0800, all inside a named cell or explained |

## Deliverables

| file | state |
|---|---|
| `bzone_program_rom.asm` | done - 120 routine headers (75 from Atari's source, 45 `[note]`) |
| `bzone_defines.asm` | done - 396 aliases, all described: 121 RAM variables (all the source declares), 53 hardware registers, 131 vector ROM symbols, 73 constants |
| `bzone_vector_rom.asm` | done - AVG display lists, VGAN character set, 3D point tables; no 6502 code in $3000-$3FFF |
| `bzone_shapes.html` | done - 12 AVG pictures, 37 characters, 42 3D objects (third coordinate = height, per-edge SBRITE brightness, drag to rotate). R2D3TB = buzz bomb/missile, TR7TBL = super tank (from BZONE.MAC comments) |
| `HARDWARE_NOTES.md`, `MATHBOX_NOTES.md`, `MAINLINE_NOTES.md`, `CPORT_READINESS.md` | written from the source + AAE driver, then fact-checked claim by claim against the source |

## Findings worth remembering

- **ROM version.** The source assembles to the $5000 chip with CRC `efbc3fa0`. Atari's
  release disk (BZONE.DOC, 31-AUG-81) calls that image `036414.02`; MAME calls it
  `036414-01` (set `bzonea`). MAME set `bzone` (CRC `13de36d5`) differs in 149 bytes:
  $5340 and $54A8-$553C (high score table display). **Primary target = the source's
  image (`roms/bzonea.zip`); `bzone` is the alternate** (`bzone_alt_cpu64k.bin`).
- **Missing includes.** `TC65` = `COIN65`; `COND65` = older name of `HLL65` (Gravitar /
  Tempest archives); `VGAN` from `../red-baron-main`; `ASCVG` from the Tempest /
  Space Duel archives. See `macsrc.py` `ALIASES` / `ALT_DIRS`. The ROM match proves them.
- **MAC65 rules learned.** Default section is the unnamed `.CSECT` (VGUT); `.BSECT` is a
  base-page section (BZONE at $00, BZSOUN at $EE); without `.ENABL AMA` an external is
  sized by its linked value (zero page); `.GLOBB` = base-page global.
- `SOUND` names both the discrete sound latch ($1840) and a BZSOUN table ($7886) -
  6-character truncation; handled in `emit.py`.
- Security trap: the game XOR-checksums part of the vector list it just built and gates
  the super bonus on it, and deducts a second checksum from the credits at START (see below).

## Disassembly: complete (2026-09-18)

Final `python gen_from_roms.py --check`: all steps passed (source re-assembly 0 mismatches;
program 12288/12288; vector ROM 4096/4096; check_ramrefs 1441 operands OK; mkfunctions
totals reconcile; alt fragment 0 mismatches; ca65 round trip 0 mismatches).

- `bzone_defines.asm`: 396 aliases, **every one described** (0 without a comment): Atari's
  comment verbatim, plus a `[note]` where Atari's text does not identify the cell.
- Review pass done: 45 routine notes checked (20 corrected), 31 variable notes checked
  (7 corrected, 18 clarifying notes added), hardware register notes corrected (OPTION /
  OPTON2 had been swapped; ACOS/BSIN are the rotation's cosine/sine inputs), the four notes
  files and both READMEs fact-checked against the source.
- `FUNCTIONS.md` (generated, 874 named locations) and `CALLING_NOTES.md` (entry/exit
  contracts and translation hazards of ~55 routines) written.
- Security traps, verified against the listing: TRAP1/TRAP2 cover $36 = 54 bytes (offsets
  $02-$37) of the vector buffer, attract mode only; **TRAP1 is what the START handler
  subtracts from the credits ($564D)**, so the attract display list must be byte-exact.
- The Mathbox is byte-identical to Tempest's (microcode source + all 7 PROMs).

Open items the source cannot settle (recorded in the notes, not blocking): HALT's equate
comment says D0 while the code tests bits 6/7; where/if the Mathbox YHSC (clip) and SYXD
commands are issued; Mathbox clock rate.

## C port: started 2026-09-18 (user go-ahead)

Plan and progress live in `../c_src/PLAN.md`. Reuse first: Tempest `mathbox.c`, POKEY
`c012294.c`, platform layer, harness on `shared/ref6502`; VGUT.MAC is identical to
Gravitar's; COIN65 is an earlier version of Tempest's/Space Duel's.

## Housekeeping

The git repository in this folder is a clone of the **Space Duel** repo (origin points
there; every Space Duel file shows as deleted). Nothing has been committed. Re-init or
re-point the remote before committing Battlezone work.
