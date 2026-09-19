"""Hand-written routine descriptions for bzone_program_rom.asm.

Used by emit.py only for routines that have neither an Atari source description
(.SBTTL / label comment / comment block above the label / comment on the calls)
nor any other source.  Written for this disassembly from reading the code (and
the neighbouring source comments); they are tagged [note] in the listing.
Keep them short and factual.  A second line may follow after "\n".
"""

DOCS = {
    # MAIN LOOP / windshield damage, high score entry
    "WNSHLD": "Draw the windshield-crack overlay (CRACKS picture, one 2-word section per call) up to the\n"
              "CRACK counter; once at least 8 sections are drawn and CRACK has reached $2C, and no shell is\n"
              "in flight (FIRECT=0), clears CRACK/STATE/COLFLG/FTIMER - ends the game (attract mode) if\n"
              "GOVER, else respawns the robot via ROBCHK/TANKCK for another life.",
    "HISCRE": "High-score initials entry state: after a timeout (TIMOUT>=4) ends entry (HISCRE_60), show INFO\n"
              "plus the 4-line entry instructions (LINE1-4 via MSGS) and call LETCHK for initials input; a\n"
              "one-shot gate (POINT=0 and HITS+2 nonzero, cleared here) plays the cannon-explosion sound once\n"
              "the first time through after CKSCOR queues the entry.",
    "DSPALP": "Draw one alphanumeric vector character (code in A): looks up its JSRL address in VGMSGA and\n"
              "JMP VGADD2 to add it to the vector list - used to spell out initials directly, not from a\n"
              "fixed message table.",
    "FINISH": "End of a display build: VGHALT stops the AVG on the finished list and BUFRDY is cleared (buffer\n"
              "ready to swap); in attract mode, once 4+ buzz bombers have been destroyed, also copies the\n"
              "TRAP1 checksum byte into VECRAM (anti-tamper check).",
    # DRAW OBJECT: DTAB computed-jump table entries (PNTTBL command bytes)
    "D_DOT":  "DTAB opcode 0 (dot): draw a blank vector to the point (DOVECT) then a dot at current intensity\n"
              "(VGDOT), then JMP D_NEXT to fetch the next command byte.",
    "D_SBRT": "DTAB opcode 1 (special brightness): for the logo or a saucer in collision, use SINT as the\n"
              "brightness; otherwise brightness = TEMP1-DQUE clamped to $30 (fades with depth); either way\n"
              "JSR VGSTAT with no clip window, then D_NEXT.",
    "D_LABS": "DTAB opcode 3 (reset beam): zero CURNTX/CURNTY and VGCNTR (re-centre the beam), then falls into\n"
              "D_BVCT to draw a blank vector from there.",
    "D_BVCT": "DTAB opcode 2 (blank vector): draw an invisible vector to the next point; shares DOVECT/D_NEXT\n"
              "with D_VCTR via D_VCT1.",
    "D_VCTR": "DTAB opcode 4 (visible vector): draw at brightness $20 to the next point; shares DOVECT/D_NEXT\n"
              "with D_BVCT via D_VCT1.",
    "D_SPCL": "DTAB opcode 5 (special/explosion picture): build the scale factor from the object table's\n"
              "linear + power-of-2 scale byte (PTBLO2+1), JSRL the explosion picture EXPIC, reset scale to 1,\n"
              "then falls into D_SLIM's JMP D_NEXT to fetch the next command byte.",
    "D_SLIM": "DTAB opcode 6 (limit, no-op): JMP D_NEXT - reached either as its own command byte or by\n"
              "falling through from D_SPCL above.",
    "DOVECT": "Draw one vector of the object at brightness A (saved to VGBRIT): compute the delta from\n"
              "CURNTX/CURNTY to the next PNTTBL point and JSR VGVTR2 to add it to the vector list.  Called\n"
              "from D_DOT directly and from D_VCT1 (shared entry for D_BVCT/D_VCTR) - the two DTAB entries\n"
              "that draw an actual vector; D_SBRT/D_SPCL/D_LABS/D_SLIM don't call it.",
    # EXTRAPOLATE SIN & COS
    "SINXTR": "Sine counterpart of COSXTR above (same block, same interpolation): extrapolate SIN between the\n"
              "table's angle steps by the division fraction in A; shares the correction code at XTRAP.",
    # QUICK ROBOT-SHELL COLLISION CHECK / COLLISION CHECK
    "Q_20S_": "QWIKCK/SHRTCK's no-collision exit: CLC then RTS - the source's own comment on this line reads\n"
              "\"TELL 'EM NO - NO - NO, NO-NO-NO-NO-NO!\".",
    "C_21S_": "COLCHK: scan the playfield obstacle table (PTBLO1/PTBLX1/PTBLY1 - fixed ROM scenery, not the\n"
              "tank) for a shell hit: Mathbox square-root distance in TEMP3 compared with PRXTBL[object\n"
              "type]; a hit explodes shell X (FIRECT,X=$A0) and starts its sound.  C_60S_ then switches to\n"
              "the other shell (DEX,DEX) and loops back through C_10S_ until both are checked.",
    "SAUCHK": "COLCHK: check the saucer for a shell hit the same way (distance from SAPOSX/SAPOSY through the\n"
              "Mathbox, DSTNCE/MOUTLO/MOUTHI), then falls into C_21S_ to scan the playfield obstacle table too.",
    # MOTION OF TANK: MTAB computed-jump table entries (joystick/tread switch combinations)
    "M_TOP":  "MOTION's control dispatcher: reads the tread/fire switches from ALLPOT (inverted so 1=pressed),\n"
              "and pushes/RTS-jumps through MTAB to the M_xx handler for that combination.  Codes 0-4 route to\n"
              "M_STOP (CMP #$05/BCC) - Atari's own inline comment here reads \"< 4 IS ILLEGAL\", one off from\n"
              "the actual test.",
    "M_PR":   "MTAB entry: pivot right in place - two DTANGL calls (double turn rate), then JMPs to M_6S_ to\n"
              "clear the OBJCOL collision-retry flag.",
    "M_PL":   "MTAB entry: pivot left in place - two ITANGL calls, then JMPs to M_6S_.",
    "M_LTF":  "MTAB entry: turn left while moving forward - one ITANGL, then JMPs to M_F (single-speed\n"
              "forward; compare M_FF, which falls into M_F after an extra M_FOR).",
    "M_LTR":  "MTAB entry: turn left while reversing - one ITANGL, then JMPs to M_R (single-speed reverse).",
    "M_RTF":  "MTAB entry: turn right while moving forward - one DTANGL, then JMPs to M_F.",
    "M_RTR":  "MTAB entry: turn right while reversing - one DTANGL, then JMPs to M_R.",
    "M_FF":   "MTAB entry: full forward - an extra JSR M_FOR here before falling into M_F (which calls M_FOR\n"
              "again), for double-distance movement; M_LTF/M_RTF instead enter at M_F directly.",
    "M_FR":   "MTAB entry: full reverse - an extra JSR M_REV here before falling into M_R, for double-distance\n"
              "reverse; M_LTR/M_RTR instead enter at M_R directly.",
    "M_STOP": "MTAB entry (also M_TOP's illegal-code fallback): selects the idle engine sound (LIDLE) instead\n"
              "of moving, then falls into the shared engine-sound/fire-button code below M_TOP's dispatch.",
    "M_SET":  "Compute the per-tick motion vector for the tank/robot at index X from its TANGLE angle:\n"
              "Y1=SIN(angle)/2, X1=COS(angle)/2 (rounded via the CMP #$80/ROR trick, sign-extended into\n"
              "Y1+1/X1+1); used by M_FOR/M_REV to advance TPOSX/TPOSY.",
    # ROBOT
    "R_37S_": "ROBOT: move forward (X=2) - M_SET the per-tick vector, doubled (ASL/ROL) if TR7CHK says this\n"
              "is the faster TR7-class robot, then M_FOR1/OBJOBJ to advance.  (Turning toward RGOAL already\n"
              "happened in ROBOT_11-30 before this is reached.)  If already aimed at RGOAL (TANGLE+2=RGOAL)\n"
              "makes one extra M_FOR1/OBJOBJ move.  Any collision (OBJOBJ carry set) JMPs to BAKOUT to undo\n"
              "the move.  Before that ($651A): in attract mode (ATRACT bit 7 clear) it always ORs OBJOBJ's A\n"
              "into STATE and sets ACTION=$30; during play it skips both when A is negative (collided with\n"
              "the player), otherwise ORs (PRAND AND 2) OR 1 into STATE and sets ACTION=$30.",
    "R_40S_": "ROBOT's decision-time entry (ACTION=0): in attract mode goes straight to R_RAND (aimless\n"
              "wandering); in-game escalates to R_ATCK (attack) once FTIMER pegs at $FF, on a random chance,\n"
              "or if HITS+1 (hundreds digit) is nonzero; otherwise compares HITS to HITS+2 - equal goes to\n"
              "R_EVAD, HITS<HITS+2 (player behind) goes to R_RAND, HITS>=HITS+2 (player ahead) goes to R_ATCK.",
    "R_RAND": "ROBOT: pick a new random heading offset from PRAND&$1F (0-31): if S_INTCT's sign bit is set,\n"
              "RGOAL = old RGOAL + the random value; otherwise RGOAL = the random value - old RGOAL.  Clears\n"
              "STATE (not attacking) and JMPs to REACT to apply it.",
    "R_ATCK": "ROBOT: aim RGOAL at the player via TRACK, set STATE=$80 (attacking) and JMP REACT.",
    # TRACKING ROUTINE: TTAB computed-jump table entries (quadrant correction of the arctangent)
    "T_1":    "TTAB quadrant-1 entry: negate the arctangent-table angle (two's complement), then falls into\n"
              "T_2 to also flip it 180 degrees.",
    "T_2":    "TTAB quadrant-2 entry: flip the arctangent-table angle 180 degrees (EOR #$80), then JMP T_0\n"
              "(RTS) with the finished angle in A.",
    "T_3":    "TTAB quadrant-3 entry: negate the arctangent-table angle (two's complement), then falls into\n"
              "T_0 (RTS).",
    "T_0":    "TTAB quadrant-0 entry, and the shared exit for T_2/T_3: RTS with the finished angle in A.",
    # ROBOT RESET CHECK
    "TANKCK": "ROBCHK: reset the robot as a plain tank - new random RGOAL, clear R2D3FL/COLFLG+2, short\n"
              "reaction/timeout (ACTION=TIMOUT=1); in attract mode also computes a random placement window\n"
              "ahead of the player, reaching ROB1 by a BEQ; otherwise (TANKCK_10) LDA #$0F and JMP ROB1 to\n"
              "pick the new position.",
    "R2D3CK": "ROBCHK: (re)spawn the buzz bomber (R2D3) instead of a tank - increments NOR2D3, sets one\n"
              "explosion-table Z slot (EXPOSZ+$C/$D) to STARTZ (the buzz bomber's starting depth), sets\n"
              "STATE=$80 (the same 'attacking' flag R_ATCK sets - collision retreat is tracked in STATE's\n"
              "bits 0-1, not bit 7), forces the engine sound channels to their idle frequencies, and sets\n"
              "R2D3FL=-1.  Falls straight into ROB1 (no branch).",
    # MESSAGE ROUTINE: LNGTBL per-language entries
    "SPANSH": "MSGS's LNGTBL entry for Spanish: point TBLPTR at SPNMSG for message X*2, then fall into MSTART\n"
              "to draw it.",
    "FRENCH": "MSGS's LNGTBL entry for French: point TBLPTR at FRNMSG, then fall into MSTART.",
    "GERMAN": "MSGS's LNGTBL entry for German: point TBLPTR at GRMMSG, then fall into MSTART.",
    "ENGLISH": "MSGS's LNGTBL entry for English (OPTION language bits = 0): point TBLPTR at ENGMSG for\n"
               "message X*2, then fall into MSTART to draw it.",
    # BZSOUN
    "SNDON":  "Start a sound effect: A = the effect's SNDWRD bit; a bit-scan loop (ASL/DEX) finds its 0-7\n"
              "index, which is converted to an index into PNTRS (8 effects x 4 channel-pointer bytes each);\n"
              "for every one of the 4 POKEY channels whose PNTRS byte for this effect is nonzero, sets that\n"
              "channel's POINT/FRAMES/COUNT so MODSND starts playing it next frame.",
    # VGUT
    "VGSTAT": "VGSTA1's second entry point, for callers that already have the brightness in Y (skips LDY\n"
              "VGBRIT): ORA #$60 packs the enable/hi-low/in-out flags into the STAT opcode, then JMP VGADD2.",
    # BZSTST
    "STOP0":  "RAMTST's success exit (the shift-pattern test came back clean): reinitialise the stack pointer\n"
              "to $7F and zero all of zero page before continuing into the VG RAM test.",
    "STEST6": "STEST4 when TEMP3's sign bit is set (plain self-test mode, not the diagnostic-step mode entered\n"
              "by holding the diag switch at power-on): skip the per-step DIATST vector test pattern and\n"
              "JSRL the cross-hatch picture HATCH.",
}
