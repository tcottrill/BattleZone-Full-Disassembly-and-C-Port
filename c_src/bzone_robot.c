/* bzone_robot.c - BZONE enemy-tank AI: ROBOT and its whole family through
 * FIREIT - ../disasm/bzone_program_rom.asm, Atari's battlezone-main/BZONE.MAC
 * ("ROBOT" .SBTTL block).
 *
 * Routines translated here (one C function per Atari label that is a real
 * routine entry, TRANSLATING.md/SIGNATURES.md conventions):
 *   ROBOT   ($6418) - decide/turn/fire/close-distance dispatcher, x=2 (the
 *                      robot) hardwired throughout (LDX #$02 at L6424, never
 *                      reloaded - every callee below takes x=2 as its own C
 *                      parameter instead of relying on a live X register).
 *   R_37S_  ($64EE) - move forward toward RGOAL, OBJOBJ-checked, BAKOUT on
 *                      collision.
 *   R_40S_  ($6534) - decision-time entry: attract mode -> R_RAND, else
 *                      falls into R_50S_.
 *   R_50S_  ($6538) - in-game decision: FTIMER==$FF, or a PRAND coin-flip,
 *                      or HITS+1!=0 -> R_ATCK; else compare HITS to HITS+2.
 *   R_EVAD  ($6553) - evasive heading.
 *   R_RAND  ($6571) - random heading offset.
 *   R_ATCK  ($6589) - track and attack.
 *   FIREIT  ($6595) - range/angle-gated shell firing (robot only).
 *
 * All five of R_37S_/R_40S_/R_50S_/R_EVAD/R_RAND/R_ATCK are reached only
 * from inside ROBOT's own control flow (grepped the whole disassembly - no
 * external JSR/JMP), same as MOTION's internal M_* legs, but unlike those
 * they each have their own listing header/`[note]` block (routine_docs.py
 * treats them as distinct routines, and FIREIT - definitely a real routine,
 * JSR'd 8 times from within ROBOT alone - shares the same "tail-JMP,
 * never-RTS-to-caller" shape) - so, per the brief's explicit naming of them
 * and TRANSLATING.md's "one C function per routine entry... a fall-through
 * entry other code jumps to", each gets its own function instead of being
 * inlined as `goto` labels inside robot() the way plain `ROBOT_nn` locals
 * are.  See NOTES_robot.md for the full reasoning and the register/carry
 * audit that resolves every hazard below.
 *
 * Conventions reused verbatim from bzone_game.c (NOTES_game.md's own
 * "Helpers / conventions" section - MOTION/REACT are this file's style
 * model, per the brief): `_AT(i)` twin-cell macros, `adc6502`/`sbc6502`
 * (state.h, shared) plus this module's own copy of the CMP/ASL/ROL/LSR/ROR
 * flag-exact helpers (TRANSLATING.md: "if you need the static inline
 * helpers that live in bzone_game.c... copy them into your file... do not
 * edit bzone_game.c"), an explicit `Lxxxx` comment per statement/small
 * group, hardware only through hw.h in ROM order, Mathbox registers written
 * via `hw_mb_write((uint8_t)(A_NAME - 0x1860), value)` (never a `g.ram[]`
 * store - NOTES_game.md's own "Mathbox writes that are NOT RAM" finding).
 */
#include <stdint.h>
#include "state.h"
#include "hw.h"
#include "regs.h"
#include "bzone_game.h"
#include "bzone_math.h"

/* ---------------------------------------------------------------------
 * Bit/carry helpers - this module's own copy of bzone_game.c's static
 * inline block (TRANSLATING.md: copy, do not edit that file).  adc6502/
 * sbc6502 already live in state.h (shared).
 * ----------------------------------------------------------------------
 */
static inline uint8_t asl6502(uint8_t v, unsigned *c)
{
    *c = (v & 0x80u) ? 1u : 0u;
    return (uint8_t)(v << 1);
}

static inline uint8_t rol6502(uint8_t v, unsigned *c)
{
    unsigned cin = *c ? 1u : 0u;
    *c = (v & 0x80u) ? 1u : 0u;
    return (uint8_t)((v << 1) | cin);
}

static inline uint8_t lsr6502(uint8_t v, unsigned *c)
{
    *c = v & 1u;
    return (uint8_t)(v >> 1);
}

static inline uint8_t ror6502(uint8_t v, unsigned *c)
{
    unsigned cin = *c ? 1u : 0u;
    *c = v & 1u;
    return (uint8_t)((v >> 1) | (cin << 7));
}

/* CMP is always binary (unaffected by the D flag) and never stores to A. */
static inline void cmp6502(uint8_t a, uint8_t v, unsigned *c, int *z)
{
    *c = (a >= v) ? 1u : 0u;
    if (z) *z = (a == v);
}

/* ROBOT ($6418): enemy-tank AI top level.  If not currently exploding
 * (COLFLG+2==0) and not the buzz-bomb variant (R2D3FL bit7 clear), backs
 * away from a fresh collision (STATE bits 0/1) for a few frames, then on
 * decision-time (ACTION==0) hands off to R_40S_; otherwise turns toward
 * RGOAL (firing while turning) and, once aimed, fires again and checks the
 * Mathbox distance to the player to decide whether to advance (R_37S_).
 * No inputs/outputs; x=2 (the robot) hardwired throughout - see the file
 * header comment. */
void robot(void)
{
    uint8_t a, y;
    unsigned c;
    int trc;

    CYC(5);                               /* L6418 LDA / L641A BEQ */
    if (COLFLG_AT(2) == 0) { CYC(1); goto ROBOT_1; }  /* L6418/L641A LDA COLFLG+2/BEQ (negated) */
    CYC(6);                               /* L641C RTS */
    return;                               /* L641C RTS */

ROBOT_1:
    CYC(5);                              /* L641D BIT / L641F BPL */
    if (!(R2D3FL & 0x80)) { CYC(1); goto ROBOT_2; }  /* L641D/L641F BIT R2D3FL/BPL */
    CYC(3);                              /* L6421 JMP */
    buzbom();                            /* L6421 JMP BUZBOM (tail call - enemy agent's, bzone_game.h) */
    return;

ROBOT_2:
    CYC(8);                          /* L6424 LDX / L6426 JSR */
    saveit(2);                       /* L6424 LDX #$02/L6426 JSR SAVEIT */
    CYC(17);                         /* L6429-L6433 */
    c = 0;                           /* L6429 CLC */
    a = RANGLE;                      /* L642A */
    a = adc6502(a, 0x0B, &c, NULL);  /* L642C ADC #$0B */
    RANGLE = a;                      /* L642E */

    a = STATE;              /* L6430 */
    a = lsr6502(a, &c);     /* L6432 LSR: c = original STATE bit0 */
    if (!c) { CYC(1); goto ROBOT_10; }  /* L6433 BCC: bit0 clear - not backing away */
    a = lsr6502(a, &c);     /* L6435 LSR: c = original STATE bit1 */
    CYC(11);                /* L6435 LSR / L6436 PHP / L6437 JSR */
    {
        unsigned saved_c = c;                                                 /* L6436 PHP - real carry save: M_REV's own ADC/SBC
                                                                                * chain would otherwise clobber it before the BCC
                                                                                * below reads it (same kind of hazard as EXPLDE's
                                                                                * PHP/PLP, kept literal - NOT MOTION's M_NEX0-style
                                                                                * "simplify away", see NOTES_robot.md). */
        m_rev(2);     /* L6437 JSR M_REV */
        c = saved_c;  /* L643A PLP */
    }
    CYC(6);                /* L643A PLP / L643B BCC */
    if (!c) { CYC(1); goto ROBOT_7; }  /* L643B BCC */
    CYC(6);                /* L643D JSR */
    itangl(2);             /* L643D JSR ITANGL */
    CYC(3);                /* L6440 JMP */
    goto ROBOT_8;          /* L6440 JMP */

ROBOT_7:
    CYC(6);     /* L6443 JSR */
    dtangl(2);  /* L6443 JSR DTANGL */

ROBOT_8:
    CYC(10);         /* L6446 DEC / L6448 LDA / L644A BEQ */
    TRDCTR_AT(2)--;  /* L6446 DEC TRDCTR+2 */

    /* ROBOT_3 */
    if (ACTION == 0) { CYC(1); goto ROBOT_5; }  /* L6448/L644A LDA ACTION/BEQ */
    CYC(6);                         /* L644C RTS */
    return;                         /* L644C RTS: still backing away */

ROBOT_5:
    CYC(25);               /* L644D-L645B */
    STATE &= 0xFC;         /* L644D-L6451: reset collision retreat */
    RGOAL = TANGLE_AT(2);  /* L6453/L6455: new goal = current heading */
    ACTION = 0x34;         /* L6457/L6459 */
    return;                /* L645B RTS */

ROBOT_10:
    CYC(5);                          /* L645C LDA / L645E BNE */
    if (ACTION != 0) { CYC(1); goto ROBOT_11; }  /* L645C/L645E LDA ACTION/BNE */
    CYC(3);                          /* L6460 JMP */
    r_40s_();                        /* L6460 JMP R_40S_ (tail call) */
    return;

ROBOT_11:
    CYC(12);                            /* L6463-L6469 */
    a = TANGLE_AT(2);                   /* L6463 */
    c = 1;                              /* L6465 SEC */
    a = sbc6502(a, RGOAL, &c, NULL);    /* L6466 SBC RGOAL */
    y = a;                              /* L6468 TAY (saved for the turn-direction test below) */
    if ((int8_t)a >= 0) { CYC(1); goto ROBOT_12; }  /* L6469 BPL */
    CYC(6);                             /* L646B-L646E */
    a ^= 0xFF;                          /* L646B EOR #$FF */
    c = 0;                              /* L646D CLC */
    a = adc6502(a, 0x01, &c, NULL);     /* L646E ADC #$01: a = |TANGLE+2 - RGOAL| */

ROBOT_12:
    CYC(5);                             /* L6470 CMP / L6472 BCC */
    cmp6502(a, SKILL, &c, NULL);        /* L6470 CMP SKILL */
    if (!c) { CYC(1); goto ROBOT_20; }  /* L6472 BCC: within SKILL degrees already */
    CYC(4);                             /* L6474 TYA / L6475 BPL */
    a = y;                              /* L6474 TYA */
    if ((int8_t)a >= 0) { CYC(1); goto ROBOT_15; }  /* L6475 BPL: turn right */
    CYC(6); itangl(2); CYC(6); fireit();  /* L6477/L647A: turn left, fire */
    CYC(6); itangl(2); CYC(6); fireit();  /* L647D/L6480 */
    CYC(6);                             /* L6483 JSR */
    trc = tr7chk();                     /* L6483 */
    CYC(2);                             /* L6486 BCS */
    if (trc) { CYC(1); goto ROBOT_14; }  /* L6486 BCS: TR7 gets an extra double-turn */
    CYC(6);                             /* L6488 RTS */
    return;                             /* ROBOT_13: L6488 RTS */

ROBOT_14:
    CYC(6); itangl(2); CYC(6); fireit();  /* L6489/L648C */
    CYC(6); itangl(2);    /* L648F */
    CYC(3);               /* L6492 JMP */
    fireit();             /* L6492 JMP FIREIT (tail call) */
    return;

ROBOT_15:
    CYC(6); dtangl(2); CYC(6); fireit();  /* L6495/L6498: turn right, fire */
    CYC(6); dtangl(2); CYC(6); fireit();  /* L649B/L649E */
    CYC(6);               /* L64A1 JSR */
    trc = tr7chk();       /* L64A1 */
    CYC(2);               /* L64A4 BCC */
    if (!trc) { CYC(7); return; }  /* L64A4 BCC -> ROBOT_13: RTS */
    CYC(6); dtangl(2); CYC(6); fireit();  /* L64A6/L64A9: TR7's extra double-turn */
    CYC(6); dtangl(2);    /* L64AC */
    CYC(3);               /* L64AF JMP */
    fireit();             /* L64AF JMP FIREIT (tail call) */
    return;

ROBOT_20:
    CYC(4);                             /* L64B2 CMP / L64B4 BEQ */
    if (a == 0) { CYC(1); goto ROBOT_30; }  /* L64B2/L64B4 CMP #$00/BEQ: dead on */
    CYC(4);                             /* L64B6 TYA / L64B7 BPL */
    a = y;                              /* L64B6 TYA */
    if ((int8_t)a >= 0) { CYC(1); goto ROBOT_25; }  /* L64B7 BPL */
    CYC(6);                             /* L64B9 JSR */
    itangl(2);                          /* L64B9: fine-turn left */
    CYC(3);                             /* L64BC JMP */
    goto ROBOT_30;                      /* L64BC JMP */

ROBOT_25:
    CYC(6);     /* L64BF JSR */
    dtangl(2);  /* L64BF: fine-turn right */

ROBOT_30:
    CYC(6);    /* L64C2 JSR */
    fireit();  /* L64C2 */
    CYC(38);   /* L64C5-L64DC (JSR TR7CHK included) */

    /* Mathbox distance check: TEMP1/TEMP2 are NOT recomputed here - they
     * are whatever TRACK last left them (TRACK's own header: "TEMP1=ABS
     * VALUE(Y), TEMP2=ABS VALUE(X)" - bzone_math.h), a hidden-RAM input
     * possibly stale from an earlier frame's FIREIT_5/R_EVAD/R_ATCK call -
     * see NOTES_robot.md "ROBOT_30's stale TEMP1/TEMP2". */
    a = TEMP1;                                         /* L64C5 */
    hw_mb_write((uint8_t)(A_TPOSXT - 0x1860), a);      /* L64C7 STA TPOSXT */
    a = TEMP1_AT(1);                                   /* L64CA */
    hw_mb_write((uint8_t)(A_TPOSXT + 1 - 0x1860), a);  /* L64CC STA TPOSXT+1 */
    a = TEMP2;                                         /* L64CF */
    hw_mb_write((uint8_t)(A_TPOSYT - 0x1860), a);      /* L64D1 STA TPOSYT */
    a = TEMP2_AT(1);                                   /* L64D4 */
    hw_mb_write((uint8_t)(A_TPOSYT + 1 - 0x1860), a);  /* L64D6 STA TPOSYT+1 */
    hw_mb_write((uint8_t)(A_DST2 - 0x1860), a);        /* L64D9 STA DST2 - starts the distance function; same accumulator value as TPOSYT+1, per the listing */

    trc = tr7chk();               /* L64DC JSR TR7CHK */
    a = hw_mb_yhigh();            /* L64DF LDA MOUTHI (carry unaffected by LDA - still TR7CHK's) */
    CYC(6);                       /* L64DF LDA / L64E2 BCC */
    if (!trc) { CYC(1); goto ROBOT_32; }  /* L64E2 BCC: not TR7 */
    CYC(4);                       /* L64E4 CMP / L64E6 BCS */
    cmp6502(a, 0x08, &c, NULL);   /* L64E4 CMP #$08 */
    if (c) { CYC(1); r_37s_(); return; }  /* L64E6 BCS */
    CYC(6);                       /* L64E8 RTS */
    return;                       /* L64E8 RTS */

ROBOT_32:
    CYC(4);                       /* L64E9 CMP / L64EB BCS */
    cmp6502(a, 0x05, &c, NULL);   /* L64E9 CMP #$05 */
    if (c) { CYC(1); r_37s_(); return; }  /* L64EB BCS */
    CYC(6);                       /* L64ED RTS */
    return;                       /* L64ED RTS */
}

/* R_37S_ ($64EE): move forward toward RGOAL - M_SET the per-tick vector
 * (doubled via ASL/ROL if TR7CHK says this is the faster TR7-class robot),
 * then M_FOR1/OBJOBJ to advance; if already aimed exactly at RGOAL, makes
 * one extra M_FOR1/OBJOBJ move.  Any collision (OBJOBJ carry set) tail-
 * calls BAKOUT to undo the move; see NOTES_robot.md for the exact STATE/
 * ACTION side-effect logic (a correction to the brief's own "unless an
 * attract-mode hit" paraphrase - the real gate is "player-tank hit", not
 * attract mode). */
void r_37s_(void)
{
    uint8_t a;
    unsigned c;
    int z;
    regs r;

    CYC(8);          /* L64EE LDX / L64F0 JSR */
    m_set(2);        /* L64EE LDX #$02/L64F0 JSR M_SET */
    CYC(6);          /* L64F3 JSR */
    if (tr7chk()) {  /* L64F3/L64F6 BCC R_37S__20 (negated) */
        CYC(22);                           /* L64F6 BCC, L64F8-L64FE */
        Y1 = asl6502(Y1, &c);              /* L64F8 ASL Y1 */
        Y1_AT(1) = rol6502(Y1_AT(1), &c);  /* L64FA ROL Y1+1 */
        X1 = asl6502(X1, &c);              /* L64FC ASL X1 */
        X1_AT(1) = rol6502(X1_AT(1), &c);  /* L64FE ROL X1+1 */
    } else CYC(3);                         /* L64F6 BCC taken */

/* R_37S__20 */
    CYC(6);                   /* L6500 JSR */
    m_for1(2);                /* L6500 JSR M_FOR1 */
    CYC(6);                   /* L6503 JSR */
    r = objobj(2);            /* L6503 JSR OBJOBJ */
    CYC(2);                   /* L6506 BCS */
    if (r.c) { CYC(1); goto R_37S__39; }  /* L6506 BCS: collision */
    CYC(13);                  /* L6508 INC / L650A LDA / L650C CMP / L650E BEQ */
    TRDCTR_AT(2)++;           /* L6508 INC TRDCTR+2 */

/* R_37S__40 */
    a = TANGLE_AT(2);           /* L650A */
    cmp6502(a, RGOAL, &c, &z);  /* L650C CMP RGOAL */
    if (!z) { CYC(6); return; }  /* L650E/L6510: not yet aimed exactly at RGOAL - RTS */
    CYC(1);                     /* L650E BEQ taken */

/* R_37S__38 */
    CYC(6);            /* L6511 JSR */
    m_for1(2);         /* L6511 JSR M_FOR1 */
    CYC(6);            /* L6514 JSR */
    r = objobj(2);     /* L6514 JSR OBJOBJ */
    CYC(2);            /* L6517 BCS */
    if (!r.c) { CYC(6); return; }  /* L6517/L6519: no collision - RTS */
    CYC(1);            /* L6517 BCS taken */

R_37S__39:
    CYC(5);            /* L651A BIT / L651C BPL */
    a = r.a;                                                                                        /* OBJOBJ's own A return (0 non-tank / $FF tank), unaffected
                                                                                                       * by the intervening BCS - both entries above land here
                                                                                                       * immediately after their own JSR OBJOBJ. */
    if (!(ATRACT & 0x80)) { CYC(1); goto R_37S__42; }    /* L651A/L651C BIT ATRACT/BPL: attract mode - always set STATE/ACTION */
    CYC(4);                                              /* L651E ORA / L6520 BMI */
    if ((int8_t)a < 0) { CYC(1); goto R_37S__41; }       /* L651E ORA #$00 ;D IF COLLIDED WITH PLAYER / L6520 BMI: player-tank hit - skip the STATE/ACTION escalation */
    CYC(8);                                              /* L6522-L6527 */
    a = (uint8_t)((hw_pokey_read(0x0A) & 0x02) | 0x01);  /* L6522-L6527: non-tank obstacle hit - pick a PRAND-chosen retreat-turn bit */

R_37S__42:
    CYC(11);        /* L6529-L652F */
    STATE |= a;     /* L6529/L652B */
    ACTION = 0x30;  /* L652D/L652F: set up reaction time */

R_37S__41:
    CYC(3);     /* L6531 JMP */
    bakout(2);  /* L6531 JMP BAKOUT (tail call) - undo the move */
}

/* R_40S_ ($6534): ROBOT's decision-time entry (ACTION==0).  Attract mode
 * (ATRACT bit7 clear) goes straight to R_RAND (aimless wandering); a real
 * game falls into R_50S_. */
void r_40s_(void)
{
    CYC(5);                  /* L6534 BIT / L6536 BPL */
    if (!(ATRACT & 0x80)) {  /* L6534/L6536 BIT ATRACT/BPL */
        CYC(1);              /* BPL taken */
        r_rand();
        return;
    }
    r_50s_();  /* fall-through to R_50S_ in the ROM */
}

/* R_50S_ ($6538): in-game decision - escalate to R_ATCK once FTIMER pegs at
 * $FF, on a random (PRAND bit0) chance, or if HITS+1 (hundreds digit) is
 * nonzero; otherwise compare HITS to HITS+2: equal -> R_EVAD, HITS<HITS+2
 * (player behind) -> R_RAND, HITS>=HITS+2 (player ahead) -> R_ATCK. */
void r_50s_(void)
{
    uint8_t a;
    unsigned c;
    int z;

    CYC(7);                       /* L6538-L653C */
    a = FTIMER;                   /* L6538 */
    cmp6502(a, 0xFF, &c, &z);     /* L653A CMP #$FF */
    if (z) { CYC(1); r_atck(); return; }  /* L653C BEQ */

    CYC(8);                        /* L653E-L6542 */
    a = hw_pokey_read(0x0A);       /* L653E LDA PRAND */
    a = lsr6502(a, &c);            /* L6541 LSR */
    if (!c) { CYC(1); r_atck(); return; }  /* L6542 BCC */

    CYC(5);                            /* L6544 LDA / L6546 BNE */
    a = HITS_AT(1);                    /* L6544 LDA HITS+1 */
    if (a != 0) { CYC(1); r_atck(); return; }  /* L6546 BNE */

    CYC(10);                             /* L6548-L654D */
    a = HITS;                            /* L6548 */
    c = 1;                               /* L654A SEC */
    a = sbc6502(a, HITS_AT(2), &c, &z);  /* L654B SBC HITS+2 */
    (void)a;                             /* the difference itself is never read again on any path */
    if (z) { CYC(1); r_evad(); return; }  /* L654D BEQ */
    CYC(2);                              /* L654F BCC */
    if (!c) { CYC(1); r_rand(); return; }  /* L654F BCC */
    CYC(3);                              /* L6551 BCS (always taken) */
    r_atck();                            /* L6551 BCS - always true on this path */
}

/* R_EVAD ($6553): evasive heading - most frames (S_INTCT low 3 bits != 0)
 * just sets a "still evading" STATE bit and keeps the existing RGOAL; every
 * 8th frame it re-tracks the player and heads the OPPOSITE way (EOR #$40 -
 * a half-turn). */
void r_evad(void)
{
    uint8_t a;

    CYC(7);                         /* L6553-L6557 */
    a = (uint8_t)(S_INTCT & 0x07);  /* L6553/L6555 */
    if (a != 0) { CYC(1); goto R_EVAD_10; }  /* L6557 BNE */
    CYC(11);                        /* L6559-L655F, BNE taken */
    STATE |= 0x01;                  /* L6559-L655D LDA #$01/ORA STATE/STA STATE */
    goto R_EVAD_20;                 /* L655F BNE - always taken (STATE|1 != 0 whenever STATE was 0, and this OR only ever sets bit0) */

R_EVAD_10:
    CYC(11);                      /* L6561 LDA / L6563 STA / L6565 JSR */
    STATE = 0x00;                 /* L6561/L6563 */
    a = track();                  /* L6565 JSR TRACK */
    CYC(5);                       /* L6568 EOR / L656A STA */
    RGOAL = (uint8_t)(a ^ 0x40);  /* L6568/L656A */

R_EVAD_20:
    CYC(11);        /* L656C-L6570: LDA / STA / RTS */
    ACTION = 0x40;  /* L656C/L656E */
}

/* R_RAND ($6571): pick a new random heading offset from PRAND&$1F (0..31):
 * if S_INTCT's sign bit is set, RGOAL = old RGOAL + the random value;
 * otherwise RGOAL = the random value - old RGOAL (falling through to ALSO
 * add RGOAL back on if that subtraction lands on exactly 0 - a literal
 * quirk of the listing's own fall-through, translated as-is).  Clears
 * STATE (not attacking) and tail-calls REACT.
 *
 * Entry-carry hazard (TRANSLATING.md's own warning: "check whether an
 * ADC/SBC really has a CLC/SEC upstream"): neither the SBC RGOAL nor the
 * fall-through ADC RGOAL below has one.  Both of R_RAND's real call sites
 * (R_40S_'s `BPL R_RAND` and R_50S_'s `BCC R_RAND`) are proven, by hand-
 * tracing every instruction back to ROBOT_2's own LSR chain (which is the
 * last thing to touch C on either path), to arrive with carry ALWAYS
 * clear - see NOTES_robot.md "R_RAND entry-carry proof" for the full
 * trace. Modelled as a local `c = 0`, not a parameter. */
void r_rand(void)
{
    uint8_t a;
    unsigned c = 0;  /* proven entry residual - see the header comment above */

    CYC(11);                                    /* L6571-L6578 */
    a = (uint8_t)(hw_pokey_read(0x0A) & 0x1F);  /* L6571/L6574 LDA PRAND/AND #$1F */
    if (S_INTCT & 0x80) { CYC(1); goto R_RAND_10; }  /* L6576/L6578 BIT S_INTCT/BMI */
    CYC(5);                                     /* L657A SBC / L657C BNE */
    a = sbc6502(a, RGOAL, &c, NULL);            /* L657A SBC RGOAL (carry-in = the proven-0 entry residual) */
    if (a != 0) { CYC(1); goto R_RAND_20; }     /* L657C BNE */

R_RAND_10:
    CYC(3);                                                            /* L657E ADC */
    a = adc6502(a, RGOAL, &c, NULL);                                   /* L657E ADC RGOAL - carry-in is the proven-0 residual if
                                                                         * reached directly via BMI, or the SBC's own real carry-
                                                                         * out if reached by falling through from R_657C. */

R_RAND_20:
    CYC(11);       /* L6580-L6586 (JMP REACT included) */
    RGOAL = a;     /* L6580 */
    STATE = 0x00;  /* L6582/L6584 */
    react(0x00);   /* L6586 JMP REACT (A=0 at entry - the LDA #$00 just above is unclobbered by the two STAs) */
}

/* R_ATCK ($6589): track the player and attack - RGOAL = TRACK's result,
 * STATE = $80 (attacking), then tail-call REACT. */
void r_atck(void)
{
    CYC(6);           /* L6589 JSR */
    RGOAL = track();  /* L6589/L658C JSR TRACK/STA RGOAL */
    CYC(11);          /* L658C-L6592 (JMP REACT included) */
    STATE = 0x80;     /* L658E/L6590 */
    react(0x80);      /* L6592 JMP REACT (A=0x80 at entry - the LDA #$80 just above is unclobbered by STA STATE) */
}

/* FIREIT ($6595): range/angle-gated shell firing for the robot.  Every cell
 * this routine touches is the robot's own (+2/+3, hardwired absolute
 * addressing in the ROM - never `,X` - so, unlike its own header comment's
 * generic-looking "USES A", FIREIT is robot-only; see NOTES_robot.md
 * "fireit() signature correction" for why the pre-existing bzone_game.h
 * draft `void fireit(uint8_t x)` was wrong).  No inputs/outputs. */
void fireit(void)
{
    uint8_t a;
    unsigned c;
    int z;
    regs r;

    CYC(7);                      /* L6595-L6599 */
    a = FTIMER;                  /* L6595 */
    cmp6502(a, 0x20, &c, NULL);  /* L6597 CMP #$20 */
    if (!c) { CYC(6); return; }  /* L6599 BCS (negated) - FIREIT_2: RTS, too soon after appearing */
    CYC(1);                      /* L6599 BCS taken */

/* FIREIT_3 */
    CYC(4);                      /* L659C CMP / L659E BEQ */
    cmp6502(a, 0xFF, &c, &z);    /* L659C CMP #$FF */
    if (z) { CYC(1); goto FIREIT_5; }  /* L659E BEQ */
    CYC(5);                      /* L65A0 LDA / L65A2 BNE */
    a = HITS_AT(1);              /* L65A0 LDA HITS+1 */
    if (a != 0) { CYC(1); goto FIREIT_5; }  /* L65A2 BNE */
    CYC(7);                      /* L65A4-L65A7 */
    a = HITS;                    /* L65A4 */
    a = lsr6502(a, &c);          /* L65A6 LSR */
    if (a != 0) { CYC(1); goto FIREIT_5; }  /* L65A7 BNE */
    CYC(7);                      /* L65A9-L65AD */
    a = PTURN;                   /* L65A9 */
    cmp6502(a, 0x20, &c, NULL);  /* L65AB CMP #$20 */
    if (c) { CYC(7); return; }   /* L65AD BCS - FIREIT_2: RTS, not in view */
    CYC(8);                      /* L65AF-L65B4 */

/* FIREIT_1 */
    a = TDIST;                   /* L65AF */
    cmp6502(a, 0x24, &c, NULL);  /* L65B2 CMP #$24 */
    if (c) { CYC(7); return; }   /* L65B4 BCS - FIREIT_50: RTS, too far */

FIREIT_5:
    CYC(6);       /* L65B6 JSR */
    a = track();  /* L65B6 JSR TRACK */
    CYC(9);       /* L65B9-L65BE */
    /* L65B9 LDX #$02: sets up X for the ROM's own indexed addressing later
     * in this routine - no observable effect in this C model (every cell
     * here is written via its own +2/+3 literal offset, never `,X`; the
     * ROM's own final "LDX #$02 / RTS" at L6621-L6623 is the same kind of
     * caller-register restore, also a no-op here). Emit nothing. */
    c = 1;                                   /* L65BB SEC */
    a = sbc6502(a, TANGLE_AT(2), &c, NULL);  /* L65BC SBC TANGLE+2 */
    if ((int8_t)a >= 0) { CYC(1); goto FIREIT_10; }  /* L65BE BPL */
    CYC(6);                                  /* L65C0-L65C3 */
    c = 0;                                   /* L65C0 CLC */
    a ^= 0xFF;                               /* L65C1 EOR #$FF */
    a = adc6502(a, 0x02, &c, NULL);          /* L65C3 ADC #$02 */

FIREIT_10:
    CYC(4);                      /* L65C5 CMP / L65C7 BCS */
    cmp6502(a, 0x02, &c, NULL);  /* L65C5 CMP #$02 */
    if (c) { CYC(7); return; }   /* L65C7 BCS - FIREIT_50: RTS, not on target */
    CYC(5);                      /* L65C9 LDA / L65CB BNE */
    a = FIRECT_AT(2);            /* L65C9 LDA FIRECT+2 */
    if (a != 0) { CYC(7); return; }  /* L65CB BNE - FIREIT_50: RTS, shell not available */
    CYC(5);                      /* L65CD LDA / L65CF BNE */
    a = COLFLG;                  /* L65CD LDA COLFLG - the PLAYER's own collision flag (unindexed) */
    if (a != 0) { CYC(7); return; }  /* L65CF BNE - FIREIT_50: RTS, player already hit */
    CYC(18 + 9);                 /* L65D1-L65DD, L65DF LDA / L65E1 JSR COS */

/* FIREIT_35: fire! */
    FIRECT_AT(2) = 0x7F;  /* L65D1/L65D3 */
    SXPCNT = 0x05;        /* L65D5/L65D7 */
    SNDWRD &= K_SOFTSH;                                                                                                                                /* L65D9-L65DD: the TANK's own SNDWRD, unindexed - matches the
                                                                                                                                                        * listing exactly (not SNDWRD+2); same shape of possible ROM
                                                                                                                                                        * asymmetry NOTES_game.md already flagged for C_21S_/SAUCHK vs.
                                                                                                                                                        * SHRTCK - translated faithfully, not "fixed"; see NOTES_robot.md. */

    a = TANGLE_AT(2);                        /* L65DF */
    r = cos_(a);                             /* L65E1 JSR COS */
    CYC(8 + ((int8_t)r.x < 0 ? 5 : 7) + 15 + 9);  /* L65E4-L65E7, BMI/LDA sign-extend, L65F1-L65F6, L65F8 LDA / L65FA JSR SIN */
    TEMP1 = r.a;                             /* L65E4 STA TEMP1 */
    a = r.x;                                 /* L65E6 TXA */
    SINCX_AT(2) = a;                         /* L65E7 */
    a = ((int8_t)a < 0) ? 0xFF : 0x00;       /* L65E9-L65ED: BMI/LDA #$FF else LDA #$00 (sign-extend) */
    TEMP1 = asl6502(TEMP1, &c);              /* L65F1 ASL TEMP1 */
    SINCX_AT(2) = rol6502(SINCX_AT(2), &c);  /* L65F3 ROL SINCX+2 */
    a = rol6502(a, &c);                      /* L65F5 ROL */
    SINCX_AT(3) = a;                         /* L65F6 */

    a = TANGLE_AT(2);                        /* L65F8 */
    r = sin_(a);                             /* L65FA JSR SIN */
    CYC(8 + ((int8_t)r.x < 0 ? 5 : 7) + 15 + 24 + 8);  /* L65FD-L6600, sign-extend, L660A-L660F, L6611-L661F, L6621 LDX / L6623 RTS */
    TEMP1 = r.a;                             /* L65FD */
    a = r.x;                                 /* L65FF TXA */
    SINCY_AT(2) = a;                         /* L6600 */
    a = ((int8_t)a < 0) ? 0xFF : 0x00;       /* L6602-L6606 */
    TEMP1 = asl6502(TEMP1, &c);              /* L660A */
    SINCY_AT(2) = rol6502(SINCY_AT(2), &c);  /* L660C */
    a = rol6502(a, &c);                      /* L660E */
    SINCY_AT(3) = a;                         /* L660F */

    SHELLX_AT(2) = TPOSX_AT(2);  /* L6611/L6613 */
    SHELLX_AT(3) = TPOSX_AT(3);  /* L6615/L6617 */
    SHELLY_AT(2) = TPOSY_AT(2);  /* L6619/L661B */
    SHELLY_AT(3) = TPOSY_AT(3);  /* L661D/L661F */
    /* L6621 LDX #$02 / L6623 RTS - no effect in this model, see above. */
}
