"""Emit disasm/bzone_defines.asm - hardware map, RAM variables and equates (Ophis .alias form).

Layout copied from the Tempest / Space Duel projects: a hand-written memory map with
descriptive names, then sections generated from build/symbols.json (Atari's names from
the BZONE.COM module set, as assembled by macasm.py):
  Hardware registers   every I/O equate, with bit notes
  Zero page variables  RAM $0000-$00FF
  Game RAM             RAM $0100-$03FF (the stack occupies $0100-$01FF; nothing is named there)
  Vector RAM           vector RAM equates the program uses
  Vector ROM           vector ROM labels the program uses
  Constants            every equate the source defines (56 total: no separate ALCOMN-style
                        common file exists for Battlezone, so this is all of them)
Identifiers are the same ones bzone_program_rom.asm uses (emit.Names), so this file defines
every symbol the listing needs (build/program_refs.json lists them) - and, per the project's
own priority, every RAM/hardware variable macasm.py found, referenced by the listing or not.
Descriptions are Atari's own comments on the definitions (upper case) where the source has
one; a hardware bit note or a short description written for this disassembly (from reading
the code, or from TC65.MAC's own documentation for the COIN65 cells) is tagged [note] and
used only where the source is silent.  Multi-byte cells (.BLKB > 1) say their size.
Aliases with no source comment can also take a description from define_notes_*.txt
(NAME<TAB>text per line, '#' comments); unknown or already-commented names are warned about.

Run:  python emit_defines.py   (after emit.py)
"""
import os, sys, re, json, collections, glob
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from emit import Names, clean, tidy, LO, HI
import macsrc
from macsrc import split_comment, read_source

BUILD = os.path.join(HERE, "build")
OUT = os.path.join(HERE, "bzone_defines.asm")

MEMORY_MAP = [
    ("ZeroPageRam",  0x0000, "Through $00FF. Control, work, tank/player and display variables."),
    ("GameRam",      0x0100, "Through $03FF. Object tables, explosions, high scores/initials, logo, "
                              "volcano-rock arrays (the 6502 stack occupies $0100-$01FF; nothing is named there)."),
    ("Inputs",       0x0800, "IN0 (read): coin switches, self-test, diag step, VG HALT, 3 kHz clock (see Hardware registers)."),
    ("OptionSw1",    0x0A00, "DSW1 option switches (read): coinage / bonus coins."),
    ("OptionSw2",    0x0C00, "DSW2 option switches (read): lives / missile level / bonus life / language."),
    ("CoinCounters", 0x1000, "Coin counter / lamp pulses (write, EMCNT)."),
    ("VgGo",         0x1200, "Start the AVG running the display list at vector RAM $2000 (write, GOADD)."),
    ("Watchdog",     0x1400, "Watchdog clear (write, WTCHDG)."),
    ("VgReset",      0x1600, "Reset (halt) the AVG (write, VSTOP)."),
    ("Mathbox",      0x1800, "Status (read $1800), result low/high (read $1810/$1818), registers / function "
                              "start (write $1860-$187F)."),
    ("Pokey",        0x1820, "Through $182F. POKEY: sound channels 1-4, random number, pot/spinner input, control."),
    ("SoundLatch",   0x1840, "Discrete-sound latch (write, SOUND)."),
    ("VectorRam",    0x2000, "Through $2FFF. AVG display list, built each frame from VGLIST."),
    ("VectorRom",    0x3000, "Through $3FFF. Vector shapes/pictures (vector-ROM track, not this file)."),
    ("Unmapped4000", 0x4000, "Through $4FFF. Not decoded by this board revision."),
    ("ProgramRom",   0x5000, "Through $7FFF (bzone_program_rom.asm); the 6502 vectors are at $7FFA."),
    ("RomMirror",    0x8000, "Through $FFFF. The 6502 only decodes 15 address bits: an exact mirror of "
                              "$0000-$7FFF (RAM, hardware and ROM alike); the 6502 reads its vectors here."),
]

# Hardware bit notes: from BZONE.MAC's/BZSOUN.MAC's own equates (authoritative addresses) and
# the AAE driver ../bzone.cpp (BzoneRead/BzoneWrite, BzoneIN0read, INPUT_PORTS_START(bzone)).
HW_NOTES = {
    "S_COINA": "Read (also known as $LAM/HALT at this address, IN0): b0 coin 1 (active low), b1 coin 2 "
               "(active low), b4 self-test/service mode (active low), b5 diagnostic step (active low).",
    "S_LAM":   "Same address as $COINA/HALT (IN0); COIN65 reads the slam bit here (see $LMBIT).",
    "HALT":    "Same address as $COINA/$LAM (IN0); bits 6/7 are tested with BIT everywhere in this source: V "
               "(bit 6) = VG HALT (the AVG has finished the display list; BSTST.MAC's own comment at the "
               "poll loop even says so - \"BIT 7 (SIGN FLAG) OF HALT = 3000 HZ SQUARE WAVE\", confirming "
               "bit 7 too), N (bit 7) = the 3 kHz clock square wave.  Bits 4/5 (self-test, diagnostic step) "
               "are instead read with AND (e.g. PWRON: AND #ST_BIT; STEST3: AND #$20).  Atari's own comment "
               "on this equate, \"D0=1 FOR HALTED\", names a different bit to the code's own usage - "
               "probably an earlier board revision's documentation that was not updated here.",
    "OPTION":  "DSW0/ip_port_1_r (Atari's own schematic numbering calls this bank \"DSW1\"; MAME's tag is "
               "\"DSW0\"): b0-1 lives, b2-3 missile-appears-at threshold, b4-5 bonus-life schedule (NEWLIF: "
               "LSR x4, AND #$03), b6-7 language (MSGS: ROL x4, AND #$06; 00 English, 01 German, 10 French, "
               "11 Spanish; see LNGTBL).",
    "OPTON2":  "DSW1/ip_port_2_r (Atari's own schematic numbering calls this bank \"DSW2\"; MAME's tag is "
               "\"DSW1\"): b0-1 coinage, b2-3 Coin B multiplier, b4 Coin A multiplier, b5-7 bonus-coin "
               "schedule; copied to zero-page S_CMODE by the NMI (\"C SET COIN MODE\") for COIN65 to read.",
    "EMCNT":   "Write: pulses the 3 electro-mechanical coin counters, one bit per counter from the sign "
               "bits of COIN65's $CCTIM timers; the AAE driver leaves this address unhandled (MWA_ROM).",
    "GOADD":   "Write (any value): start the AVG running the display list at vector RAM $2000.",
    "WTCHDG":  "Write (any value): clear the watchdog timer; must be written periodically (once per NMI, "
               "and again while STEST4/RAMTST poll the 3 kHz clock) or the board resets.",
    "VSTOP":   "Write (any value): reset/halt the AVG; the complement of GOADD.",
    "MSTAT":   "Read: Mathbox status, D7 = 1 while busy; also the base address for the rest of the Mathbox "
               "register block below (MSTAT+offset: ACOS, BSIN, TPOSXT, ... DST2).  This Mathbox is the same "
               "hardware as Tempest's, byte-identical microcode and mapping PROM (see MATHBOX_NOTES.md).",
    "MBSTAT":  "Same address as MSTAT; used where the code is reading the busy/status bit rather than the "
               "register block base.",
    "STARTZ":  "Same numeric value as MSTAT/MBSTAT ($1800), but used by R2D3CK only as a 16-bit constant "
               "(STARTZ/100, STARTZ&$FF) to set a newly-spawned buzz bomber's starting Z depth - not as an "
               "address.",
    "MOUTLO":  "Read: Mathbox result, low byte (same address as YLOW) - the generic result byte for "
               "whichever function was last started (rotation, divide, distance, ...); also read by "
               "BZSTST's MBDIAG self-test.",
    "YLOW":    "Same address as MOUTLO; used where the value being read is specifically a Y-matrix/rotation "
               "result.",
    "MOUTHI":  "Read: Mathbox result, high byte (same address as YHIGH) - the generic result byte, as MOUTLO.",
    "YHIGH":   "Same address as MOUTHI; used where the value being read is specifically a Y-matrix/rotation "
               "result.",
    "SOUND":   "Write: discrete-sound latch (BzoneSounds in the AAE driver): explosion/shell/engine-rumble "
               "trigger bits, decoded outside the 6502 program.",
    "POKEY":   "POKEY ($1820-$182F): sound channels 1-4 (frequency/volume pairs), control, random number, "
               "pot scan (spinner/cocktail input).",
    "AUDF1":   "POKEY write: channel 1 frequency (same address as CHAN1F).",
    "CHAN1F":  "POKEY write: channel 1 frequency; MODSND writes the running sound's current value here.",
    "CHAN1V":  "POKEY write: channel 1 volume/distortion.",
    "CHAN2F":  "POKEY write: channel 2 frequency.",
    "CHAN2V":  "POKEY write: channel 2 volume/distortion.",
    "CHAN3F":  "POKEY write: channel 3 frequency.",
    "CHAN3V":  "POKEY write: channel 3 volume/distortion.",
    "CHAN4F":  "POKEY write: channel 4 frequency.",
    "CHAN4V":  "POKEY write: channel 4 volume/distortion.",
    "ALLPOT":  "POKEY read: pot-scan result byte; this board wires it to the player buttons/switches "
               "(BzoneControls / bzone_IN3_r in the AAE driver) - fire, start 1/2, tread switches - not an "
               "analogue pot.",
    "AUDCTL":  "POKEY write: audio control (same address as ALLPOT read).",
    "PRAND":   "POKEY read: random number, used throughout as the game's dice (enemy AI, sound tables).",
    "POTGO":   "POKEY write: start a pot scan (must be struck before reading ALLPOT).",
    "SKCTL":   "POKEY write: serial/keyboard control.",
    "ACOS":    "Mathbox register block (MSTAT+offset), write-only: the rotation-matrix cosine INPUT - the "
               "6502 writes its own COSXTR-extrapolated cosine table value here (e.g. ROTATE: JSR COSXTR; "
               "STA ACOS), not a Mathbox-computed arccosine.",
    "MBOXIN":  "Same address as ACOS ($1860): start of the Mathbox write-registers block (MAL..MDYPL-style "
               "offsets used as MSTAT+n through this source).",
    "BSIN":    "Mathbox register block, write-only: the rotation-matrix sine INPUT - the 6502 writes its own "
               "SINXTR-extrapolated sine table value here (e.g. ROTATE: JSR SINXTR; STA BSIN), not a "
               "Mathbox-computed arcsine.",
    "TPOSXT":  "Mathbox register block: target X position input to the distance/arctangent functions.",
    "TPOSYT":  "Mathbox register block: target Y position input to the distance/arctangent functions.",
    "XMATRX":  "Mathbox register block: X rotation-matrix input.",
    "YMATRX":  "Mathbox register block: Y rotation-matrix input.",
    "YSTRT1":  "Mathbox register block: Y-matrix-multiply start/strobe (first of two, see YSTRT2).",
    "DIVCYC":  "Mathbox register block, write-only: written once (LDA #$0A; STA DIVCYC, \"C INIT DIVIDER\") "
               "each time MAIN starts a frame's rotation, to initialize the divider - not a status byte "
               "read back by the 6502.",
    "ZLOW":    "Mathbox register block: Z (depth) divisor, low byte.",
    "ZHI":     "Mathbox register block: Z (depth) divisor, high byte.",
    "DIVOUT":  "Mathbox register block, write-only: the divide dividend; the 6502 never reads it back, only "
               "MOUTLO/MOUTHI (e.g. TRACK writes the X or Y delta here before STA ZDIV starts the divide).",
    "YSTRT2":  "Mathbox register block: Y-matrix-multiply start/strobe (second of two, see YSTRT1).",
    "RSTRT1":  "Mathbox register block: rotation-matrix-multiply start/strobe.",
    "ZDIV":    "Mathbox register block: generic start-divide strobe - despite the name, not just for the Z "
               "divisor (ZLOW/ZHI); TRACK also uses it to start the arctangent division after loading DIVOUT.",
    "XPL":     "Mathbox register block: divide dividend, low byte.",
    "XPH":     "Mathbox register block: divide dividend, high byte.",
    "DXP":     "Mathbox register block: delta-X projection result.",
    "DYPH":    "Mathbox register block: delta-Y projection result, high byte.",
    "DSTNCE":  "Mathbox register block: distance-function input/result (collision and radar checks).",
    "DST2":    "Mathbox register block: second distance-function input/result.",
}

# RAM cells where Atari's own comment, read on its own line, evidently describes the *next*
# declared cell instead (a comment written one .BLKB statement early is common in this source -
# see also SHELLX/SINCY/SAPOSY/SAINCY below, which just have no comment of their own rather
# than a misplaced one).  Confirmed by usage: TRDCTR is read/written only as TRDCTR/TRDCTR+2/
# TRDCTR,X ("MOVE TREAD" at BZONE.MAC:3043) with no shell-position use anywhere; GARBAG is
# reused as plain scratch by ABSTMP, DRADAR, ROBCHK etc. with no shell-position use either;
# LOGOZ is "LDA LOGOZ ;C SET UP Z" (BZONE.MAC:490) paired with LOGOX, not POKEY-related.  Each
# entry replaces the misattributed .alias comment with the cell's own real one and points at
# where Atari's original comment actually belongs.
MISPLACED_COMMENT = {
    "TRDCTR": ("Per-object (index 0 tank, 2 robot/buzz bomber, via TRDCTR,X) tread/thrust "
               "animation-frame counter, incremented each time that object moves (\"C MOVE "
               "TREAD\", BZONE.MAC:3043); also read as TRDCTR+2 (aliasing into GARBAG+1, see "
               "GARBAG below).",
               "GARBAG"),
    "GARBAG":  ("General-purpose 2-byte scratch pair, reused independently by ABSTMP, DRADAR "
                "(\"RELATIVE ANGLE OF ROBOT\") and ROBCHK; its second byte (GARBAG+1) is also "
                "reachable as TRDCTR+2 (see TRDCTR above) when a tread-counter call happens to "
                "run right before one of these routines reuses it.",
                "SHELLX/SHELLY"),
    "LOGOZ":   ("Z (depth) position of the 3-D attract-mode logo, 2 bytes (\"LDA LOGOZ ;C SET "
                "UP Z\", BZONE.MAC:490); pairs with LOGOX below, whose comment \"LOGO "
                "POSITIONS\" covers this cell too.",
                "OLDRND"),
}

# Cells whose own Atari comment is correct for their primary (index-0/tank) use, but which the
# code also reaches at a fixed +2 offset (or via ,X with X=0 or 2 - SAVEIT's own doc says "X=0
# MEANS TANK, X=2 MEANS ROBOT", BZONE.MAC:2843-2845) to get a robot/buzz-bomber twin that lands
# inside the *next* declared cell - confirmed by check_ramrefs.py's cross-reference report.
# Appended to the existing comment, not a replacement (unlike MISPLACED_COMMENT above).
CROSS_REF = {
    "COLFLG": "Also indexed as COLFLG,X (X=0 tank, 2 robot - BZONE.MAC:3056); COLFLG+2 lands on SAVE1+1.",
    "SAVE1":  "Second byte (SAVE1+1) is also reachable as COLFLG+2, the robot's twin of the tank's COLFLG (see COLFLG).",
    "FIRECT": "Also indexed as FIRECT,X (X=0 tank, 2 robot, throughout ROBOT/BUZBOM); FIRECT+2 lands on DQUE+1.",
    "DQUE":   "Second byte (DQUE+1) is also reachable as FIRECT+2, the robot's twin of the tank's FIRECT (see FIRECT).",
    "TANGLE": "Also indexed as TANGLE,X (X=0 tank, 2 robot, throughout ROBOT/TRACK); TANGLE+2 lands on NTHETA+1.",
    "NTHETA": "Second byte (NTHETA+1) is also reachable as TANGLE+2, the robot's twin of the tank's TANGLE (see TANGLE).",
    "OBJCOL": "Also reached as OBJCOL+2 in BUZBOM (BZONE.MAC:3899-3906), the buzz bomber's twin of the tank's OBJCOL; lands on EIRNGE+1.",
    "EIRNGE": "Second byte (EIRNGE+1) is also reachable as OBJCOL+2, the buzz bomber's twin of the tank's OBJCOL (see OBJCOL).",
    "SNDWRD": "Also indexed as SNDWRD,X (X=0 tank, 2 robot/buzz bomber - SHRTCK/BUZBOM); SNDWRD+2 lands on SAVE2+1.",
    "SAVE2":  "Second byte (SAVE2+1) is also reachable as SNDWRD+2, the robot's twin of the tank's SNDWRD (see SNDWRD).",
}

# Cells (RAM or Constants) that DO carry an Atari comment of their own, but where that comment
# is a terse tag, an in-joke or a note-to-a-person that doesn't actually tell a reader what the
# cell holds (e.g. STATE's "CALIFORNIA (HERE)", OBJTIM's "VOLCANO STUFF - SEE RUBIN"). Unlike
# the notes loaded from define_notes_*.txt (which apply only where Atari left no comment at
# all - see described() below), these are appended after Atari's verbatim comment, the same way
# CROSS_REF does above; Atari's text is never altered or dropped. Applied to any kind (ram,
# equate) regardless of what other mechanism already touched the cell's comment.
CLARIFY_NOTES = {
    "STATE":  "Robot/buzz-bomber behavior-state flags, not indexed (there is only ever one active enemy): "
              "bit 0 = backing out of a collision or evading, bit 1 = which way to turn while doing so "
              "(meaningful only with bit 0 set), bit 7 = set by R_ATCK (attacking) / cleared by R_RAND (not "
              "attacking) - though nothing in this source re-tests bit 7 once set.  Atari's own comment, "
              "\"CALIFORNIA (HERE)\", is a joke (California = \"here\"), not a description.",
    "OBJTIM": "Per-rock (NOROCK=5) active/lifetime countdown timer: 0 = rock inactive, nonzero counts down "
              "each frame (VOLCNO) toward re-triggering a new rock; also scaled (ASL x3, AND #$E0) as the "
              "rock's dot brightness when drawn (MOUNTS).",
    "S_CCTIM": "COIN65's per-mechanism (MECHS=3) electro-mechanical-counter pulse timer: sign bit set = "
               "pulse that counter on, clear = off (documented in TC65.MAC's own $CCTIM header comment).",
    "ACTION": "Robot behavior countdown: decremented once per frame (MAIN); 0 = decision time (ROBOT enters "
              "R_40S_ to pick a new behavior), otherwise counts down the frames left in the current one "
              "(e.g. TANKCK/R2D3CK set it to 1 for an immediate re-decision, ROBOT_5 sets $34 after backing "
              "out of a collision, R_EVAD sets $40 while evading).",
    "SAVE0":  "Scratch, reused per-routine rather than tied to one purpose - e.g. DRAW's own INPUT PARAMS "
              "comment: \"SAVE0=OBJECT # POINTER\", its running index into the rotated/translated object "
              "table.",
    "SAVE3":  "Scratch, reused per-routine - e.g. DRAW/PNTPUT's byte offset into the current object's point "
              "data (\"OBJECT POINT TABLE\", inline comment where it's set up).",
    "TRAP1":  "Security checksum, computed by BATTLE ($526C-$5284) in attract mode only (ATRACT bit 7 clear): "
              "0 minus the $36 (54) bytes at offsets $02-$37 of the vector buffer being built (page = VGLIST+1 "
              "AND $F8), SBC-chained, minus OBJTBL+$21.  Consumers: NMI's START handler deducts it from the "
              "credits ($564D: SBC TRAP1 - so it must come out as exactly 1 for one credit per game); FINISH "
              "($5588) stores it to VECRAM+0 during play once NOR2D3 >= 4.  Independent of TRAP2.",
    "TRAP2":  "Security checksum, computed by MAIN ($510A-$511E) in attract mode only: EOR of the same $36 "
              "(54) bytes at offsets $02-$37 of the vector buffer being built, shifted right once.  Consumer: "
              "B_END_30 ($51C2: LDA TRAP2 / LSR / ORA POINT / BNE B_END_20) - only when TRAP2 is 0 or 1 and no "
              "sound is playing (POINT=0) does it go on to store 0 in SUPER, set LOX in SNDWRD and EXPCNT=$FF.  "
              "Independent of TRAP1.",
    "DSPLAY": "Attract-mode sub-state: alternates which attract-mode display is showing (the BATTLE ZONE "
              "logo animation vs. the high-score table), toggled once per attract cycle.",
    "BLIP":   "Radar-blip fade timer/brightness: DRADAR sets it to $F0 when the sweep passes an enemy "
              "(\"SET BRIGHTNESS OF BLIP\"), decrements it by 8 every frame after (DR_BLP), and also uses "
              "it directly (AND #$E0) as the blip's own draw intensity; reaching 0 stops the sweep beep and "
              "draw.  Atari's own comment, \"BLIMP TIMER\", is source shorthand for this radar \"blip\" "
              "timer - Battlezone has no blimp object.",
    "VTIMER": "Countdown timer, decremented once per NMI (\"VG TIMER\") and reloaded elsewhere; gates how "
              "often certain per-frame work runs.",
    "S_INTCT": "Free-running counter incremented once per NMI (\"64 MS\" in the source comment at the "
               "increment); besides timing, its low bits also serve as a slow pseudo-random/staggering "
               "source elsewhere (R_EVAD tests S_INTCT&7, R_RAND tests its sign bit).",
    "NWSTAT": "Current-frame ALLPOT switch reading, captured for HISCRE/LETCHK's fire-button edge-detect "
              "against the previous frame's copy in OLSTAT (see OLSTAT).",
    "CURNTX": "Current vector-beam X position, tracked in RAM so routines that draw a sequence of connected "
              "vectors (DOVECT, VOLCNO's mountain-rock draw) can compute each new delta from the beam's "
              "last plotted point.  Atari's own comment, \"CURRENT BEAN POSITION X\", is a typo for \"BEAM\".",
    "CURNTY": "Current vector-beam Y position, pairs with CURNTX (see CURNTX; same \"BEAN\"/BEAM typo on "
              "its own comment line).",
    "LANGLE": "Low bit(s) of the tank/robot's 9-bit heading angle (TANGLE/NTHETA hold the other 8 bits); "
              "also indexed as LANGLE,X (X=0 tank, 2 robot).",
    "SOUNDN": "Declared by BZSOUN's own .BSECT layout (\"SOUND # IN PROGRESS\") but never referenced by "
              "name anywhere in this source; POINT/CURRENT/FRAMES/COUNT below are the per-channel sound "
              "state SNDON/MODSND actually read and write.",
    "CCTRS":  "Number of coin counters actually used by the pulse/count logic: EMCTRS (3) normally, or 1 "
              "under the FTEST field-test override just above (not set by this game).",
}

# Bytes in $0000-$07FF that no RAM cell names - computed from the extents themselves (see
# main(), not hand-counted), then each range individually accounted for by reading the source/
# check_ramrefs.py's report, and written here as plain comments (not .alias lines: nothing is
# invented a name it doesn't have).  Keyed by (section, gap_lo) as found by gaps_in().
GAP_COMMENTS = {
    ("Zero page variables", 0x00E3): "1 unlabeled byte ($00E3): an anonymous '.BLKB 1' right "
        "after $CCTIM/EMCTRS (BZONE.MAC:277) - padding, never addressed by any instruction "
        "(see check_ramrefs.py).",
    ("Zero page variables", 0x00FF): "1 unlabeled byte ($00FF): BZSOUN's own .BSECT ends at "
        "COUNT ($FB-$FE, 4 bytes); nothing claims $FF.",
    ("Game RAM", 0x0100): "$0100-$01FF: the 6502 hardware stack, not named RAM.  PWRON zeroes "
        "it along with the rest of RAM (BZSTST.MAC:96-100, 'STA $0100,X'), then STOP0 sets "
        "SP=$7F once RAMTST's zero-page shift-pattern test passes (BZSTST.MAC:232-233); SNDON "
        "also reads it directly, 'TSX; LDA $0103,X', to peek 3 bytes up its own just-pushed "
        "A/X/Y frame.",
    ("Game RAM", 0x02EF): "17 unlabeled bytes ($02EF-$02FF) between TRAP2 and HSCTBL, never "
        "addressed by name; the last 2 ($02FD/$02FE) are the compile-time base of CKSCOR's "
        "'HSCTBL-3,X'/'HSCTBL-2,X' shift-loop reads, but X is always >= 3 whenever those run "
        "(check_ramrefs.py), so even they are never actually read at runtime.",
    ("Game RAM", 0x0372): "1166 unlabeled bytes ($0372-$07FF) through the end of RAM before the "
        "hardware registers at $0800 - YDISP ($0371) is the last cell this source ever names "
        "or addresses; nothing beyond it is ever read or written.",
}

def gaps_in(extents, lo, hi):
    """Sorted [(gap_lo, gap_hi), ...] for the bytes in [lo, hi) that no RAM extent covers."""
    covered = bytearray(hi - lo)
    for elo, ehi, _name in extents:
        for a in range(max(elo, lo), min(ehi, hi)):
            covered[a - lo] = 1
    out, a = [], lo
    while a < hi:
        if not covered[a - lo]:
            s = a
            while a < hi and not covered[a - lo]:
                a += 1
            out.append((s, a))
        else:
            a += 1
    return out

def blkb_size(expr, equates):
    """Evaluate a .BLKB operand (digits, NAME, NAME*NUM, NUM*NAME, NAME*NUM-NUM, <...> grouping)
    using the module's own equate values (from symbols.json) - no radix guessing needed, since
    macasm.py already resolved every name to its assembled integer value."""
    s = expr.strip()
    if s.startswith("<") and s.endswith(">"):
        s = s[1:-1].strip()
    def repl(m):
        tok = m.group(0)
        if re.match(r"^[0-9]+\.?$", tok):
            return str(int(tok.rstrip(".")))
        v = equates.get(tok.upper())
        if v is None:
            raise KeyError(tok)
        return str(v)
    s2 = re.sub(r"[A-Za-z_][A-Za-z0-9_.$]*|[0-9]+\.?", repl, s)
    if not re.match(r"^[0-9+\-*/() ]+$", s2):
        raise ValueError(s2)
    return eval(s2, {"__builtins__": {}})

_blkb_re = re.compile(r"^[A-Za-z0-9$._]+:\s*\.BLKB\s+(.*?)\s*$", re.I)

def blkb_sizes(symbols):
    """{(name, addr): size} for every ram-kind symbol whose own source line is 'LABEL: .BLKB expr'."""
    equates = {}
    for s in symbols:
        if s["kind"] == "equate" and not s.get("redefined") and not s.get("from_macro"):
            equates.setdefault(s["name"], s["addr"])
    sizes = {}
    for s in symbols:
        if s["kind"] != "ram":
            continue
        raw = read_source(s["file"])[s["line"] - 1]
        code, _ = split_comment(raw)
        m = _blkb_re.match(code.strip())
        if not m:
            continue
        try:
            n = blkb_size(m.group(1), equates)
        except (KeyError, ValueError, SyntaxError, ZeroDivisionError):
            continue
        if isinstance(n, int) and n > 1:
            sizes[(s["name"], s["addr"])] = n
    return sizes

def load_notes():
    """Descriptions written for this disassembly, for aliases Atari left uncommented.
    Every define_notes_*.txt next to this script: NAME<TAB>description, '#' lines are comments."""
    notes = {}
    for path in sorted(glob.glob(os.path.join(HERE, "define_notes_*.txt"))):
        with open(path, encoding="utf-8") as fp:
            for n, line in enumerate(fp, 1):
                line = line.rstrip("\r\n")
                if not line.strip() or line.lstrip().startswith("#"):
                    continue
                name, sep, text = line.partition("\t")
                name, text = name.strip(), text.strip()
                if not sep or not name or not text:
                    print("!! %s:%d: expected NAME<TAB>description" % (os.path.basename(path), n))
                    continue
                if name in notes:
                    print("warning: %s:%d: %s described again (keeping the later one)" %
                          (os.path.basename(path), n, name))
                notes[name] = (text, "%s:%d" % (os.path.basename(path), n))
    return notes

def banner(t):
    return ";" + "-" * 34 + "[ " + t + " ]" + "-" * 34

def fmt(name, value, comment):
    v = ("$%02X" % value) if value < 0x100 else ("$%04X" % value)
    s = ".alias %-16s %s" % (name, v)
    if comment:
        s = "%-32s ;%s" % (s, comment)
    return s

# Vector ROM data tables BZMTNS.MAC leaves uncommented; each statement checked against the
# code that reads the table (addresses are the program listing's).
VROM_NOTES = {
    "UNDERL": "VGAN: underline stroke (one SVEC), JSRL'd by the message/initials display",
    "CHKS0":  "Checksum adjust byte for the $3000 vector ROM chip (BZSTST ROM test); never read by name",
    "CHKS1":  "Checksum adjust byte for the $3800 vector ROM chip (BZSTST ROM test); never read by name",
    "ARCTAN": "Arctangent table, indexed by the Mathbox quotient in TRACK ($68A4) to get the angle to a target",
    "BONTBL": "Bonus tank score levels, indexed by OPTION bits 4-5 (NEWLIF $6169, DISTBL $5549)",
    "MISLVL": "Score at which the missile (R2D3) starts to appear, indexed by OPTION bits 2-3 ($668A, ROBCHK $69D0)",
    "OBJPNT": "Object number*2 -> address of that object's 3D point table (.WORD per object; see DRAW/PNTPUT)",
    "EXPTBX": "Explosion pieces: per-piece X increment words added to EXPOSX each frame ($5B97)",
    "EXPTBY": "Explosion pieces: per-piece Y increment words added to EXPOSY each frame ($5BB0)",
    "IZVEL":  "Explosion pieces: initial Z velocities copied to ZVELOC ($61A0, 'INITIALIZE Z VELOCITIES')",
    "PTBLO1": "Fixed playfield obstacle table: object number and orientation byte per obstacle, "
              "$FF-terminated; parallel to PTBLX1/PTBLY1 in the program ROM ($5AEE, COLCHK $6063)",
    "LOGOBJ": "The three BATTLE ZONE logo objects: object number, $80 pairs read by BATTLE ($52AB)",
}
for _n in ("ETKTBL", "ETREAD", "ERADAR", "EEXP", "EEX2", "EROCK", "REXEND", "ESAUCE", "TR7END"):
    VROM_NOTES[_n] = ("End-of-table marker: used only in the preceding point table's length byte "
                      "(.BYTE end-start); same address as the next table")

def main():
    symbols = json.load(open(os.path.join(BUILD, "symbols.json")))
    refs = json.load(open(os.path.join(BUILD, "program_refs.json")))["refs"]
    names = Names(symbols, None, None)
    sizes = blkb_sizes(symbols)
    out = [
        ";Battlezone (Atari, 1980) - hardware map, RAM variables and equates for",
        ";bzone_program_rom.asm.",
        ";Names are Atari's identifiers from the BZONE.COM module set (MAC65 symbols are",
        ";significant to 6 characters, so a longer spelling is the same symbol as its first 6",
        ";characters; '.' -> '_' and '$' -> 'S_').  A name the source defines with two",
        ";different values keeps the name for the main one; a second ROM/RAM/hardware label",
        ";among the others gets an _ADDR suffix.",
        ";Descriptions are Atari's comments on the definitions (upper case) where the source",
        ";has one; text tagged [note] was written for this disassembly (bit notes from the AAE",
        ";driver ../bzone.cpp, RAM/constant notes from reading the code or, for the COIN65",
        ";cells, from TC65.MAC's own header comments - see define_notes_bzone.txt).  A",
        ";multi-byte cell says its size, from the source's own .BLKB count.",
        ";Generated by disasm/emit_defines.py; every RAM/hardware/constant symbol macasm.py",
        ";found is here, whether or not the program listing itself refers to it by name.",
        "",
        banner("Memory map"),
    ]
    for n, a, c in MEMORY_MAP:
        out.append(fmt(n, a, c))

    done = set(n for n, _, _ in MEMORY_MAP)
    rows = collections.defaultdict(list)     # section -> [(addr, ident, comment, sortkey, rec)]
    for s in symbols:
        if s["kind"] == "local":
            continue
        ident = names.ident.get((s["name"], s["addr"]))
        if ident is None or ident in done:
            continue
        a, k = s["addr"], s["kind"]
        com = tidy(s.get("comment") or "")
        if k == "io":
            sec = "Hardware registers"
            note = HW_NOTES.get(ident)
            if com and note:
                com = com + ".  [note] " + note
            elif note:
                com = "[note] " + note
        elif k == "ram" and a < 0x100:
            sec = "Zero page variables"
        elif k == "ram":
            sec = "Game RAM"
        elif 0x2000 <= a < 0x3000 and k in ("code", "data", "ram", "equate"):
            sec = "Vector RAM"
        elif 0x3000 <= a < 0x4000 and k in ("code", "data"):
            sec = "Vector ROM"
        elif k in ("code", "data") and LO <= a < HI:
            continue                                  # labels live in the listing
        elif k == "equate" and not s.get("redefined") and not s.get("from_macro"):
            sec = "Constants"                          # every equate the source defines
        elif ident in refs:
            sec = "Constants"
        else:
            continue
        if k == "ram":
            mis = MISPLACED_COMMENT.get(ident)
            if mis:
                own, elsewhere = mis
                com = "[note] %s  (Atari's comment here, \"%s\", belongs to %s below - see there.)" % (
                    own, com, elsewhere)
            elif ident in CROSS_REF:
                extra = "[note] " + CROSS_REF[ident]
                com = (com + "  " + extra) if com else extra
        if ident in CLARIFY_NOTES:
            extra = "[note] " + CLARIFY_NOTES[ident]
            com = (com + "  " + extra) if com else extra
        done.add(ident)
        rows[sec].append((a, ident, com, (s["file"], s["line"]), s, sizes.get((s["name"], a))))
    missing = [r for r in refs if r not in done]
    for r in missing:
        info = refs[r]
        rows["Constants"].append((info["addr"], r, tidy(info.get("comment") or ""), (info["file"], info["line"]),
                                  {"file": info["file"], "sbttl": ""}, None))
        done.add(r)

    notes = load_notes()
    seen = set()
    def described(ident, com, size):
        if ident in notes:
            seen.add(ident)
            if com:
                print("warning: %s: %s already has a comment; note not used" % (notes[ident][1], ident))
            else:
                com = "[note] " + notes[ident][0]
        if size:
            suffix = "%d bytes." % size
            com = (com + "  " + suffix) if com else suffix
        return com

    # gaps: computed once from the actual RAM extents (Zero page + Game RAM together, since a
    # cell's declared size can carry its extent past a section's own address boundary), then
    # each gap is shown - as a comment, never a made-up alias - in whichever section it starts in.
    ram_extents = [(a, a + (n or 1), ident) for a, ident, _, _, _, n in
                   rows["Zero page variables"] + rows["Game RAM"]]
    # each section's own gaps, clamped to its own address range, so a gap that would otherwise
    # span the $0100 section boundary (the 1 stray zero-page byte at $00FF immediately
    # followed by the stack page) is reported as two separately-explained pieces, one per section
    ram_gaps = (gaps_in(ram_extents, 0x0000, 0x0100) + gaps_in(ram_extents, 0x0100, 0x0800))
    unexplained_gaps = [(lo, hi) for lo, hi in ram_gaps
                        if ("Zero page variables", lo) not in GAP_COMMENTS
                        and ("Game RAM", lo) not in GAP_COMMENTS]
    for lo, hi in unexplained_gaps:
        print("warning: unexplained RAM gap $%04X-$%04X (%d bytes) - add it to GAP_COMMENTS"
              % (lo, hi - 1, hi - lo))

    vrom_desc = {}
    vl = os.path.join(HERE, "bzone_vector_rom.asm")
    if os.path.exists(vl):
        for line in open(vl, encoding="utf-8", errors="replace"):
            m = re.match(r"^([A-Za-z0-9_]+):\s+;(.*)$", line.rstrip())
            if m:
                d = re.sub(r";?\s*\d+ refs?\s*$", "", m.group(2)).strip()
                if d and d != "same address":
                    vrom_desc.setdefault(m.group(1), d)
    final = {}       # (sec, ident) -> rendered comment, for the stats below
    for sec in ("Hardware registers", "Zero page variables", "Game RAM", "Vector RAM", "Vector ROM"):
        out.append("")
        out.append(banner(sec))
        gap_here = [(lo, hi) for lo, hi in ram_gaps if (sec, lo) in GAP_COMMENTS] if sec in (
            "Zero page variables", "Game RAM") else []
        entries = [(a, "alias", (ident, com, n)) for a, ident, com, _, s, n in rows[sec]]
        entries += [(lo, "gap", (lo, hi)) for lo, hi in gap_here]
        for a, kind, payload in sorted(entries, key=lambda x: (x[0], x[1] == "alias")):
            if kind == "gap":
                lo, hi = payload
                out.append(";" + GAP_COMMENTS[(sec, lo)])
                continue
            ident, com, n = payload
            final[(sec, ident)] = described(ident, com, n)
            if sec == "Vector ROM" and not final[(sec, ident)] and ident in VROM_NOTES:
                final[(sec, ident)] = "[note] " + VROM_NOTES[ident]
            if sec == "Vector ROM" and not final[(sec, ident)] and re.match(r"^CHAR_.$", ident):
                final[(sec, ident)] = "[note] VGAN character picture '%s' (AVG subroutine, ends in RTSL)" % (
                    " " if ident == "CHAR_" else ident[-1])
            if sec == "Vector ROM" and not final[(sec, ident)] and re.match(r"^CHAR..$", ident):
                final[(sec, ident)] = "[note] VGAN: local label inside the character picture '%s'" % ident[4]
            if sec == "Vector ROM" and not final[(sec, ident)] and ident in vrom_desc:
                # the vector ROM listing's own label description (emit_vrom.py), so the two agree
                final[(sec, ident)] = "[note] " + vrom_desc[ident]
            out.append(fmt(ident, a, final[(sec, ident)]))
    out.append("")
    out.append(banner("Constants"))
    last = None
    for a, ident, com, key, s, n in sorted(rows["Constants"], key=lambda x: x[3]):
        head = "%s  %s" % (s["file"], s.get("sbttl", ""))
        if head != last:
            out.append(";" + head.strip())
            last = head
        final[("Constants", ident)] = described(ident, com, n)
        out.append(fmt(ident, a, final[("Constants", ident)]))
    for mname, _, mcom in MEMORY_MAP:
        if mname in notes:
            described(mname, mcom, None)
    for name in sorted(set(notes) - seen):
        print("warning: %s: %s is not an alias in bzone_defines.asm" % (notes[name][1], name))
    uncommented = sum(1 for l in out if l.startswith(".alias") and ";" not in l)
    with open(OUT, "w", newline="\n") as fp:
        fp.write("\n".join(out) + "\n")
    cnt = {k: len(v) for k, v in rows.items()}
    ram_count = cnt.get("Zero page variables", 0) + cnt.get("Game RAM", 0)
    ram_noted = sum(1 for sec in ("Zero page variables", "Game RAM")
                    for a, ident, com, _, s, n in rows[sec] if "[note]" in final[(sec, ident)])
    print("wrote %s: %s; %d identifiers referenced by the listing, all defined" %
          (os.path.relpath(OUT, HERE), ", ".join("%s %d" % kv for kv in sorted(cnt.items())), len(refs)))
    print("  RAM variables: %d (%d needed a [note] description); %d descriptions from define_notes_*.txt; "
          "%d aliases still without a comment" %
          (ram_count, ram_noted, len(seen - set(n for n, _, _ in MEMORY_MAP)), uncommented))

if __name__ == "__main__":
    main()
