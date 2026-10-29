/* bzone_enemy.c - BZONE enemy AI: BUZBOM, SAUCMV, OBJOBJ, ROBCHK, TANKCK,
 * R2D3CK, ROB1 - ../disasm/bzone_program_rom.asm, Atari's own
 * battlezone-main/BZONE.MAC.
 *
 * Owner: agent "enemy" (TRANSLATING.md).  Sibling files being written at the
 * same time: bzone_game.c (agent "game": MAIN, EXPLDE, QWIKCK/SHRTCK,
 * COLCHK/SAUCHK, NEWLIF/EXINIT, MOTION, REACT, TR7CHK - all done, see
 * NOTES_game.md), bzone_robot.c (agent "robot": ROBOT family, FIREIT).
 *
 * Conventions (TRANSLATING.md, SIGNATURES.md; modelled on bzone_game.c's own
 * MOTION/EXPLDE/COLCHK clusters): one C function per Atari label that is a
 * real JSR/JMP entry point; an "Lxxxx" address comment on each statement or
 * small group; RAM cells go through state_defs.h exactly as the ROM stores
 * them, scratch cells included; hardware only through hw.h; ROM data tables
 * read through cpu_rd() at their real address, never retyped.
 *
 * Bit/carry helpers below are copied from bzone_game.c (TRANSLATING.md: "if
 * you need the static inline helpers that live in bzone_game.c... copy them
 * into your file as static inline, do not edit bzone_game.c") - identical
 * bodies, so lockstep sees the same 6502 flag behaviour from either file.
 * adc6502/sbc6502 (BCD-aware) already live in state.h, shared, not copied.
 *
 * Judgement calls, residual-carry hazards and open questions are written up
 * in NOTES_enemy.md as they're found; only brief pointers are repeated here.
 */
#include <stdint.h>
#include "state.h"
#include "hw.h"
#include "regs.h"
#include "game.h"
#include "bzone_game.h"
#include "bzone_math.h"

/* ---------------------------------------------------------------------
 * Bit/carry helpers (copied from bzone_game.c - see the file header above).
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

/* PTBLX1 ($7681) and PTBLY1 ($76AB) - the fixed playfield obstacle table's X/Y
 * halves - and OBJOBJ's own PROXTB ($6995) have no state_defs.h alias
 * (disassembler-local labels for ROM data, like bzone_game.c's own C_21S_
 * already documents for this exact same PTBLX1/PTBLY1 pair and its sibling
 * PRXTBL) - used here as literal ROM addresses via cpu_rd(), per
 * TRANSLATING.md "ROM tables are read... at their ROM address". */
#define A_ENEMY_PTBLX1 0x7681u
#define A_ENEMY_PTBLY1 0x76ABu
#define A_ENEMY_PROXTB 0x6995u

static void rob1(uint8_t a);

/* BUZBOM ($6624-$677B): R2D3 buzz-bomber AI/motion - "called in lieu of
 * ROBOT and flies the buzz bomb directly at player, at high speed, swooping
 * in on him" (Atari's header).  No inputs/outputs (Atari's header says
 * "REGISTERS: A,X,Y" - clobbered only, not read).  Calls track()/sin_()/
 * cos_() (bzone_math.h), m_for1()/qwikck()/exinit() (bzone_game.h, this
 * module's siblings), objobj() (this file).  Atari's own "SUBROUTINES:
 * TRACK,M.FOR,OBJCOL" header is stale (also calls QWIKCK/OBJOBJ/EXINIT/
 * SIN/COS) - same pattern CALLING_NOTES.md documents for TRACK/PNTPUT/DRAW's
 * own headers; not "fixed", just noted. */
void buzbom(void)
{
    uint8_t a;
    unsigned c = 0;
    regs orr;

    CYC(29);         /* L6624-L6637 */
    TRDCTR_AT(2)++;  /* L6624 INC TRDCTR+2 */

    /* BUZBOM_1: is the buzz bomb above the TOP height threshold AND already
     * flagged as colliding with something?  16-bit CMP/SBC of TOP ($0200)
     * against EXPOSZ+$C/+$D (the buzz bomb's own Z depth - R2D3CK's STARTZ
     * cell, per bzone_game.c's EXPLDE loop-bound finding, NOT an explosion
     * piece here). */
    a = (uint8_t)(K_TOP & 0xFF);                /* L6626 LDA #TOP&$FF */
    cmp6502(a, EXPOSZ_AT(0x0C), &c, NULL);      /* L6628 CMP EXPOSZ+$C */
    a = (uint8_t)(K_TOP >> 8);                  /* L662B LDA #TOP/$100 */
    a = sbc6502(a, EXPOSZ_AT(0x0D), &c, NULL);  /* L662D SBC EXPOSZ+$D */
    a = 0x00;                                   /* L6630 */
    a = rol6502(a, &c);                         /* L6632 ROL - A = carry (0/1) */
    GARBAG = a;                                 /* L6633 STA GARBAG */
    a &= OBJCOL_AT(2);                          /* L6635 AND OBJCOL+2 */
    if (a != 0) { CYC(3); goto BUZBOM_30; }     /* L6637/L6639 BEQ BUZBOM_2 (inverted) / JMP BUZBOM_30 */

    /* BUZBOM_2: |TANGLE - RGOAL| ("INSURE NO BACK ATTACKS") - TEMP2 keeps
     * the original SIGNED difference; only A gets the absolute value. */
    {
        CYC(1 + 13);                        /* L6637 BEQ taken, L663C-L6643 */
        a = TANGLE;                         /* L663C */
        c = 1;                              /* L663E SEC */
        a = sbc6502(a, RGOAL, &c, NULL);    /* L663F SBC RGOAL */
        TEMP2 = a;                          /* L6641 */
        if ((int8_t)a >= 0) { CYC(1); goto BUZBOM_3; }  /* L6643 BPL */
        CYC(6);                             /* L6645-L6648 */
        c = 0;                              /* L6645 CLC */
        a ^= 0xFF;                          /* L6646 EOR #$FF */
        a = adc6502(a, 0x01, &c, NULL);     /* L6648 ADC #$01 - two's complement negate */
    }

BUZBOM_3:
    CYC(4);                                  /* L664A CMP / L664C BCS */
    cmp6502(a, 0x40, &c, NULL);              /* L664A CMP #$40 */
    if (c) { CYC(1); goto BUZBOM_4; }        /* L664C BCS */
    CYC(5);                                  /* L664E BIT / L6650 BPL */
    if ((int8_t)TEMP2 >= 0) { CYC(1); goto BUZBOM_12; }  /* L6650/L6652 BIT TEMP2 / BPL */
    CYC(3);                                  /* L6654 BMI (taken) */
    goto BUZBOM_6;                           /* L6654 BMI - always taken (BPL just failed) */

BUZBOM_4:
    CYC(6);                              /* L6656 JSR */
    a = track();                         /* L6656 JSR TRACK - adjust direction */
    CYC(15);                             /* L6659-L6662 */
    TEMP2 = a;                           /* L665B STA TEMP2 (X#$02 at L6659 not otherwise used here) */
    a = RGOAL;                           /* L665D */
    c = 1;                               /* L665F SEC */
    a = sbc6502(a, TEMP2, &c, NULL);     /* L6660 SBC TEMP2 */
    if (a == 0) { CYC(1); goto BUZBOM_20; }  /* L6662 BEQ */
    CYC(2);                              /* L6664 BPL */
    if ((int8_t)a >= 0) { CYC(1); goto BUZBOM_10; }  /* L6664 BPL - decrement needed */
    CYC(4);                              /* L6666 CMP / L6668 BCC */
    cmp6502(a, 0xFD, &c, NULL);          /* L6666 CMP #$FD */
    if (!c) { CYC(1); goto BUZBOM_5; }   /* L6668 BCC */

BUZBOM_6:
    CYC(5);   /* L666A INC */
    RGOAL++;  /* L666A INC RGOAL */

BUZBOM_5:
    CYC(8);          /* L666C INC / L666E JMP */
    RGOAL++;         /* L666C INC RGOAL */
    goto BUZBOM_20;  /* L666E JMP */

BUZBOM_10:
    CYC(4);                      /* L6671 CMP / L6673 BCC */
    cmp6502(a, 0x03, &c, NULL);  /* L6671 CMP #$03 */
    if (!c) { CYC(1); goto BUZBOM_15; }  /* L6673 BCC */

BUZBOM_12:
    CYC(5);   /* L6675 DEC */
    RGOAL--;  /* L6675 DEC RGOAL */

BUZBOM_15:
    CYC(5);   /* L6677 DEC */
    RGOAL--;  /* L6677 DEC RGOAL */

BUZBOM_20:
    CYC(6);                                /* L6679 LDA / L667C BEQ */
    a = NOR2D3;                            /* L6679 LDA NOR2D3 - if first R2D3 */
    if (a == 0) { CYC(1); goto BUZBOM_37; }  /* L667C BEQ */
    CYC(5);                                /* L667E LDA / L6680 BNE */
    a = HITS_AT(1);                        /* L667E */
    if (a != 0) { CYC(1); goto BUZBOM_33; }  /* L6680 BNE */
    CYC(31);                               /* L6682-L6695 */
    a = hw_option();                       /* L6682 LDA OPTION */
    a = lsr6502(a, &c);                    /* L6685 */
    a = lsr6502(a, &c);                    /* L6686 */
    a &= 0x03;                             /* L6687 */
    a = cpu_rd((uint16_t)(A_MISLVL + a));  /* L6689/L668A TAX / LDA MISLVL,X */
    c = 0;                                 /* L668D CLC */
    g.dflag = 1;                           /* L668E SED */
    a = adc6502(a, 0x25, &c, NULL);        /* L668F ADC #$25 */
    g.dflag = 0;                           /* L6691 CLD */
    c = 1;                                 /* L6692 SEC */
    a = sbc6502(a, HITS, &c, NULL);        /* L6693 SBC HITS */
    if ((int8_t)a < 0) { CYC(1); goto BUZBOM_33; }  /* L6695 BMI */
    CYC(4);                                /* L6697 CMP / L6699 BCS */
    cmp6502(a, 0x08, &c, NULL);            /* L6697 CMP #$08 */
    if (c) { CYC(1); goto BUZBOM_35; }     /* L6699 BCS */

BUZBOM_33:
    CYC(2);    /* L669B LDA */
    a = 0x08;  /* L669B */

BUZBOM_35:
    CYC(6);                       /* L669D CMP / L66A0 BCC */
    cmp6502(a, TDIST, &c, NULL);  /* L669D CMP TDIST */
    if (!c) { CYC(1); goto BUZBOM_40; }  /* L66A0 BCC */

BUZBOM_37:
    CYC(6);          /* L66A2 LDA / L66A4 JMP */
    a = RGOAL;       /* L66A2 */
    goto BUZBOM_19;  /* L66A4 JMP */

BUZBOM_40:
    /* Set up X1/Y1 for M_FOR1: pivot RGOAL by a small pseudo-random step
     * (FRAME's own low 5 bits), using FRAME bit 3's own residual carry to
     * pick ADC vs SBC - genuine residual, not a fresh CLC/SEC, since LDA
     * never touches C and the intervening AND #$1F only touches N/Z. */
    CYC(24);                          /* L66A7-L66B5 */
    a = FRAME;                        /* L66A7 LDA FRAME */
    a = lsr6502(a, &c);               /* L66A9 */
    a = lsr6502(a, &c);               /* L66AA */
    a = lsr6502(a, &c);               /* L66AB */
    a = lsr6502(a, &c);               /* L66AC - c now = FRAME's original bit 3 */
    a = FRAME;                        /* L66AD LDA FRAME - reload; LDA doesn't touch C */
    a &= 0x1F;                        /* L66AF AND #$1F */
    TEMP1 = a;                        /* L66B1 */
    a = RGOAL;                        /* L66B3 */
    if (c) { CYC(1 + 3); goto BUZBOM_18; }  /* L66B5 BCS (taken), L66BC SBC */
    CYC(6);                           /* L66B7 ADC / L66B9 JMP */
    a = adc6502(a, TEMP1, &c, NULL);  /* L66B7 ADC TEMP1 - residual carry-in (FRAME bit 3), not CLC */
    goto BUZBOM_19;                   /* L66B9 JMP */

BUZBOM_18:
    a = sbc6502(a, TEMP1, &c, NULL);  /* L66BC SBC TEMP1 - same residual carry-in */

BUZBOM_19:
    CYC(9);            /* L66BE STA / L66C0 JSR SIN */
    TANGLE_AT(2) = a;  /* L66BE */
    {
        regs r = sin_(a);  /* L66C0 JSR SIN */
        uint8_t lo = r.a;  /* L66C5 PHA (kept in `lo`) */

        CYC(13 + ((int8_t)r.x < 0 ? 5 : 4) + 28 + 9);  /* L66C3-L66CA, sign-extend + L66CE STA, L66D0-L66D9, L66DB LDA / L66DD JSR COS */
        Y1 = r.x;                              /* L66C3 STX Y1 */
        Y1_AT(1) = (Y1 & 0x80) ? 0xFF : 0x00;  /* L66C6-L66CC LDA #$00/BIT Y1/BPL/LDA #$FF */
        c = 0;
        lo = asl6502(lo, &c);              /* L66D1 ASL - low byte's own final value is never stored (matches MOTION's own SINCX/SINCY precision-loss precedent, NOTES_game.md) */
        Y1 = rol6502(Y1, &c);              /* L66D2 ROL Y1 */
        Y1_AT(1) = rol6502(Y1_AT(1), &c);  /* L66D4 ROL Y1+1 */
        lo = asl6502(lo, &c);              /* L66D6 ASL */
        Y1 = rol6502(Y1, &c);              /* L66D7 ROL Y1 */
        Y1_AT(1) = rol6502(Y1_AT(1), &c);  /* L66D9 ROL Y1+1 */
    }
    a = TANGLE_AT(2);  /* L66DB */
    {
        regs r = cos_(a);  /* L66DD JSR COS */
        uint8_t lo = r.a;  /* L66E2 PHA */

        CYC(13 + ((int8_t)r.x < 0 ? 5 : 4) + 28 + 24 + 8);  /* L66E0-L66E7, sign-extend + L66EB STA, L66ED-L66F6, L66F8-L6706, L6708 LDX / L670A JSR M_FOR1 */
        X1 = r.x;                              /* L66E0 STX X1 */
        X1_AT(1) = (X1 & 0x80) ? 0xFF : 0x00;  /* L66E3-L66E9 */
        c = 0;
        lo = asl6502(lo, &c);              /* L66EE */
        X1 = rol6502(X1, &c);              /* L66EF ROL X1 */
        X1_AT(1) = rol6502(X1_AT(1), &c);  /* L66F1 ROL X1+1 */
        lo = asl6502(lo, &c);              /* L66F3 */
        X1 = rol6502(X1, &c);              /* L66F4 */
        X1_AT(1) = rol6502(X1_AT(1), &c);  /* L66F6 */
        (void)lo;
    }

    OTPOSX = TPOSX_AT(2);        /* L66F8/L66FA */
    OTPOSX_AT(1) = TPOSX_AT(3);  /* L66FC/L66FE */
    OTPOSY = TPOSY_AT(2);        /* L6700/L6702 */
    OTPOSY_AT(1) = TPOSY_AT(3);  /* L6704/L6706 */

    m_for1(0x02);                     /* L6708-L670A LDX #$02/JSR M_FOR1 */
    CYC(6);                           /* L670D JSR */
    if (qwikck()) { CYC(3); goto BUZBOM_26; }  /* L670D-L6710 JSR QWIKCK/BCS - already blown up */
    CYC(2 + 8);                       /* L6710 BCS, L6712 LDX / L6714 JSR */
    orr = objobj(0x02);               /* L6712-L6714 LDX #$02/JSR OBJOBJ */
    CYC(2);                           /* L6717 BCC */
    if (!orr.c) { CYC(1); goto BUZBOM_25; }  /* L6717 BCC - no collision */
    CYC(5);                           /* L6719 LDX / L671B BNE */
    if (GARBAG != 0) { CYC(1); goto BUZBOM_28; }  /* L6719-L671B LDX GARBAG/BNE - GARBAG was set at BUZBOM_1 */
    CYC(6);                           /* L671D RTS */
    return;                           /* L671D RTS - too high up, ignore the collision */

BUZBOM_25:
    CYC(15);                     /* L671E-L6728 */
    OBJCOL_AT(2) = 0x00;         /* L671E/L6720 */
    a = EXPOSZ_AT(0x0C);         /* L6722 */
    a |= EXPOSZ_AT(0x0D);        /* L6725 ORA EXPOSZ+$D */
    if (a == 0) { CYC(1); goto BUZBOM_26; }  /* L6728 BEQ */
    CYC(6);                      /* L672A DEC */
    EXPOSZ_AT(0x0D)--;           /* L672A DEC EXPOSZ+$D */

BUZBOM_26:
    CYC(6);  /* L672D RTS */
    return;  /* L672D RTS */

BUZBOM_28:
    CYC(4);                                 /* L672E ORA / L6730 BMI */
    if ((int8_t)orr.a < 0) { CYC(1); goto BUZBOM_60; }  /* L672E/L6730 ORA #$00 / BMI - "we got him" (tank collision) */
    CYC(35);                                /* L6732-L6744 */
    TPOSX_AT(2) = OTPOSX;                   /* L6732/L6734 back out position change */
    TPOSX_AT(3) = OTPOSX_AT(1);             /* L6736/L6738 */
    TPOSY_AT(2) = OTPOSY;                   /* L673A/L673C */
    TPOSY_AT(3) = OTPOSY_AT(1);             /* L673E/L6740 */
    OBJCOL_AT(2)++;                         /* L6742 INC OBJCOL+2 - indicate collision */
    return;                                 /* L6744 RTS */

BUZBOM_30:
    CYC(12);            /* L6745 INC / L6748 RTS */
    EXPOSZ_AT(0x0D)++;  /* L6745 INC EXPOSZ+$D (BUZBOM_50: RTS, shared) */
    return;             /* L6748 RTS */

BUZBOM_60:
    CYC(18 + 14 + 10 + 6);                             /* L6749-L6755, L6757-L675E, L6760-L6765, L6768 JSR EXINIT */
    COLFLG = 0x20;                                     /* L6749/L674B blow'm both up */
    COLFLG_AT(2) = 0x20;                               /* L674D */
    CRACK = 0x02;                                      /* L674F/L6751 */
    BOUNCE = 0xFF;                                     /* L6753/L6755 */
    a = 0x01;                                          /* L6757 - only points for enemy */
    c = 0;                                             /* L6759 CLC */
    g.dflag = 1;                                       /* L675A SED */
    a = adc6502(a, HITS_AT(2), &c, NULL);              /* L675B ADC HITS+2 */
    g.dflag = 0;                                       /* L675D CLD */
    HITS_AT(2) = a;                                    /* L675E */
    hw_pokey_write((uint8_t)(A_CHAN3V & 0x0F), 0x00);  /* L6760-L6762 LDX #$00/STX CHAN3V */
    hw_pokey_write((uint8_t)(A_CHAN4V & 0x0F), 0x00);  /* L6765 STX CHAN4V */
    exinit(0x00);                                      /* L6768 JSR EXINIT */
    CYC(20);                                           /* L676B-L6777 */
    EXPCNT = 0xFF;                                     /* L676B/L676D */
    SNDWRD &= K_HIX;                                   /* L676F-L6773 - unindexed, matches C_21S_'s own analogous asymmetry (NOTES_enemy.md) */
    LIVES--;                                           /* L6775 DEC LIVES */
    if (LIVES != 0) { CYC(7); return; }                /* L6777 BNE BUZBOM_70 (taken + RTS) */
    CYC(11);                                           /* L6779 INC / L677B RTS */
    GOVER++;                                           /* L6779 INC GOVER */
    /* BUZBOM_70: falls straight into RTS. */                                                                                                                                                                                                                                                                                                                                    /* L677B */
}

/* SAUCMV ($677C-$680F): flying-saucer AI (entrance, exit, motion).  Atari's
 * header: "INPUT PARAMS: SCDLFG,STIMER,SAUCER,SCANGL,SAPOSX,SAPOSY. OUTPUT
 * PARAMS: SAPOSX,SAPOSY,SAINCX,SAINCX[sic],SCANGL,SINT,STIMTER[sic].
 * REGISTERS: A" - matches; no real inputs/outputs beyond the RAM cells it
 * names (nothing here is read from A/X/Y at entry). */
void saucmv(void)
{
    uint8_t a;
    unsigned c = 0;

    CYC(5);                            /* L677C LDA / L677E BEQ */
    a = SCOLFG;                        /* L677C LDA SCOLFG - if not blowing up */
    if (a == 0) { CYC(1); goto SAUCMV_20; }  /* L677E BEQ */
    CYC(4);                            /* L6780 CMP / L6782 BCC */
    cmp6502(a, 0x30, &c, NULL);        /* L6780 CMP #$30 - if < 30 */
    if (!c) { CYC(1); goto SAUCMV_10; }  /* L6782 BCC */
    CYC(7);                            /* L6784-L6787 */
    a = 0x60;                          /* L6784 */
    c = 1;                             /* L6786 SEC */
    a = sbc6502(a, SCOLFG, &c, NULL);  /* L6787 SBC SCOLFG */

SAUCMV_10:
    CYC(26);                          /* L6789-L6797 */
    c = 0;                            /* L6789 CLC - set intensity for explosion */
    a = adc6502(a, 0x08, &c, NULL);   /* L678A ADC #$08 */
    a = asl6502(a, &c);               /* L678C ASL */
    a = asl6502(a, &c);               /* L678D ASL */
    a &= 0xF0;                        /* L678E AND #$F0 */
    SINT = a;                         /* L6790 */
    SCOLFG--;                         /* L6793 DEC SCOLFG - if explosion not over */
    SCOLFG--;                         /* L6795 DEC SCOLFG */
    if (SCOLFG != 0) { CYC(1); goto SAUCMV_15; }  /* L6797 BNE */
    CYC(12);                          /* L6799-L67A0 */
    a = hw_pokey_read(0x0A);          /* L6799 LDA PRAND - reset timer till next saucer */
    STIMER = a;                       /* L679C */
    SAUCER = 0x00;                    /* L679E/L67A0 */

SAUCMV_15:
    CYC(6);  /* L67A2 RTS */
    return;  /* L67A2 RTS */

SAUCMV_20:
    CYC(5);                          /* L67A3 LDA / L67A5 BEQ */
    a = SAUCER;                      /* L67A3 LDA SAUCER - if not saucer */
    if (a == 0) { CYC(1); goto SAUCMV_40; }  /* L67A5 BEQ */
    CYC(15);                         /* L67A7-L67B0 */
    a = SCANGL;                      /* L67A7 - rotate saucer */
    c = 0;                           /* L67A9 CLC */
    a = adc6502(a, 0x08, &c, NULL);  /* L67AA ADC #$08 */
    SCANGL = a;                      /* L67AC */
    a = STIMER;                      /* L67AE LDA STIMER - if decision timer */
    if (a == 0) { CYC(1); goto SAUCMV_30; }  /* L67B0 BEQ */
    CYC(40);                         /* L67B2-L67CA */

    c = 0;                                   /* L67B2 CLC - move saucer */
    a = SAINCX;                              /* L67B3 */
    a = adc6502(a, SAPOSX, &c, NULL);        /* L67B5 */
    SAPOSX = a;                              /* L67B7 */
    a = SAPOSX_AT(1);                        /* L67B9 */
    a = adc6502(a, SAINCX_AT(1), &c, NULL);  /* L67BB */
    SAPOSX_AT(1) = a;                        /* L67BD */
    a = SAPOSY;                              /* L67BF */
    c = 0;                                   /* L67C1 CLC */
    a = adc6502(a, SAINCY, &c, NULL);        /* L67C2 */
    SAPOSY = a;                              /* L67C4 */
    a = SAPOSY_AT(1);                        /* L67C6 */
    a = adc6502(a, SAINCY_AT(1), &c, NULL);  /* L67C8 */
    SAPOSY_AT(1) = a;                        /* L67CA */

SAUCMV_28:
    CYC(11);   /* L67CC DEC / L67CE RTS */
    STIMER--;  /* L67CC DEC STIMER */
    return;    /* L67CE RTS */

SAUCMV_30:
    CYC(11);                  /* L67CF-L67D6 */
    a = hw_pokey_read(0x0A);  /* L67CF LDA PRAND - set new motion values */
    SAINCX = a;               /* L67D2 */
    /* L67D4-L67DA ORA #$00/BMI/LDA #$00/BEQ: sign-extend `a` into $00/$FF;
     * the BEQ after "LDA #$00" is a provably always-taken forward branch
     * (A is always 0 there), same idiom as OBJOBJ's own L6921 - modelled as
     * a plain if/else. */
    CYC(((int8_t)a < 0 ? 6 : 8) + 11);             /* sign-extend + L67DE STA, L67E0-L67E7 */
    SAINCX_AT(1) = ((int8_t)a < 0) ? 0xFF : 0x00;  /* L67D6-L67DC */
    a = hw_pokey_read(0x0A);                       /* L67E0 LDA PRAND */
    SAINCY = a;                                    /* L67E3 */
    CYC(((int8_t)a < 0 ? 6 : 8) + 15);             /* sign-extend + L67EF STA, L67F1-L67F7 */
    SAINCY_AT(1) = ((int8_t)a < 0) ? 0xFF : 0x00;  /* L67E5-L67ED, same sign-extend idiom */
    a = hw_pokey_read(0x0A);                       /* L67F1 LDA PRAND - reset timer */
    a = lsr6502(a, &c);                            /* L67F4 LSR */
    STIMER = a;                                    /* L67F5 */
    return;                                        /* L67F7 RTS */

SAUCMV_40:
    CYC(7);                      /* L67F8-L67FB */
    a = HITS;                    /* L67F8 LDA HITS - if score not too low */
    a = lsr6502(a, &c);          /* L67FA LSR */
    if (a != 0) { CYC(1); goto SAUCMV_41; }  /* L67FB BNE */
    CYC(6);                      /* L67FD RTS */
    return;                      /* L67FD RTS */

SAUCMV_41:
    CYC(5);                           /* L67FE LDA / L6800 BNE */
    a = STIMER;                       /* L67FE LDA STIMER - if not time yet */
    if (a != 0) { CYC(1); goto SAUCMV_28; }  /* L6800 BNE */
    /* Two PRAND reads: each charged up to its own LDA, because RANDOM only
     * moves with machine time - read in one charge they return the same
     * byte (the saucer always came out at X hi == Y hi, on the world's
     * diagonal; user report: "appearing way too close"). */
    CYC(4);                           /* L6802 */
    a = hw_pokey_read(0x0A);          /* L6802 LDA PRAND - bring out saucer */
    CYC(7);                           /* L6805-L6807 */
    SAPOSX_AT(1) = a;                 /* L6805 */
    a = hw_pokey_read(0x0A);          /* L6807 LDA PRAND */
    CYC(10);                          /* L680A-L680E */
    SAPOSY_AT(1) = a;                 /* L680A */
    SAUCER++;                         /* L680C INC SAUCER */
    if (SAUCER != 0) { CYC(1); goto SAUCMV_30; }  /* L680E BNE - a genuine (not always-taken) test: skipped only if SAUCER wraps $FF->$00 */
    /* SAUCER wrapped $FF->$00: the BNE falls through out of SAUCMV into the
     * next routine in ROM order, TRACK ($6810).  TRACK runs with its usual
     * side effects (TEMP1-TEMP3, the Mathbox divide) and its RTS returns to
     * SAUCMV's caller; B_END ignores the angle it leaves in A. */
    (void)track();
}

/* OBJOBJ ($68D6-$6994): object-object collision detect.  Atari's header:
 * "INPUT: (X)=0 FOR PLAYER TANK, (X)=2 FOR ROBOT TANK... OUTPUT PARMS:
 * CARRY SET COLLISION, 0=NONTANK COLLISION, -1=TANK COLLISION IN(A)."
 * Returns via `regs` (SIGNATURES.md rule: more than one register/flag
 * consumed by callers - both `a` and carry are real outputs here, already
 * frozen in bzone_game.h from an earlier draft this file adopts unchanged):
 * r.c = carry, r.a = 0 (nontank) or 0xFF (tank); carry clear = no collision.
 * CALLING_NOTES.md section 3 lists OBJOBJ as one of WAIT's callers, but the
 * actual code never calls WAIT (nor `bz_mb_wait()`) - it reads hw_mb_ylow/yhigh
 * directly after a dummy delay read, the same idiom bzone_game.c's own
 * SHRTCK/SAUCHK/C_21S_ use without WAIT either; treated as a stale claim in
 * that note, not translated as a WAIT call (see NOTES_enemy.md). */
regs objobj(uint8_t x)
{
    regs r = {0};
    uint8_t a, y, idx;
    unsigned c = 0;

    CYC(39);                                         /* L68D6-L68ED */
    TEMP1 = x;                                       /* L68D7 STX TEMP1 - save X; set up the Mathbox distance function */
    a = TPOSX_AT(x);                                 /* L68D9 */
    hw_mb_write((uint8_t)(A_ACOS - 0x1860), a);      /* L68DB STA ACOS */
    a = TPOSX_AT((uint8_t)(x + 1));                  /* L68DE */
    hw_mb_write((uint8_t)(A_ACOS + 1 - 0x1860), a);  /* L68E0 */
    a = TPOSY_AT(x);                                 /* L68E3 */
    hw_mb_write((uint8_t)(A_BSIN - 0x1860), a);      /* L68E5 */
    a = TPOSY_AT((uint8_t)(x + 1));                  /* L68E8 */
    hw_mb_write((uint8_t)(A_BSIN + 1 - 0x1860), a);  /* L68EA */
    y = 0x00;                                        /* L68ED LDY #$00 */

    for (;;) {
        /* OBJOBJ_10 (loop top - reached by the `continue` below, not by an
         * explicit goto, so no label is emitted here). */
        CYC(6);                                /* L68EF LDA / L68F2 BMI */
        a = cpu_rd((uint16_t)(A_PTBLO1 + y));  /* L68EF LDA PTBLO1,Y - if done */
        if ((int8_t)a < 0) { CYC(1); goto OBJOBJ_50; }  /* L68F2 BMI */
        CYC(56);                               /* L68F4-L6919 (dummy read L690C included) */

        a = cpu_rd((uint16_t)(A_ENEMY_PTBLX1 + y));        /* L68F4 LDA PTBLX1,Y - get distance to object */
        hw_mb_write((uint8_t)(A_TPOSXT - 0x1860), a);      /* L68F7 STA TPOSXT */
        a = cpu_rd((uint16_t)(A_ENEMY_PTBLX1 + y + 1));    /* L68FA */
        hw_mb_write((uint8_t)(A_TPOSXT + 1 - 0x1860), a);  /* L68FD */
        a = cpu_rd((uint16_t)(A_ENEMY_PTBLY1 + y));        /* L6900 LDA PTBLY1,Y */
        hw_mb_write((uint8_t)(A_TPOSYT - 0x1860), a);      /* L6903 */
        a = cpu_rd((uint16_t)(A_ENEMY_PTBLY1 + y + 1));    /* L6906 */
        hw_mb_write((uint8_t)(A_DSTNCE - 0x1860), a);      /* L6909 STA DSTNCE - starts the distance function */

        /* L690C: CMP ($00,X) - deliberate dummy delay read before the
         * Mathbox result (TRANSLATING.md's own worked example). Emit
         * nothing. */

        a = hw_mb_ylow();   /* L690E LDA MOUTLO */
        TEMP3 = a;          /* L6911 */
        a = hw_mb_yhigh();  /* L6913 LDA MOUTHI */
        TEMP3_AT(1) = a;    /* L6916 STA TEMP3+1 */

        if (x != 0) { CYC(1); goto OBJOBJ_20; }  /* L6918/L6919 TXA/BNE - if robot */
        CYC(10);                     /* L691B-L6921, BNE taken */

        a = 0x80;                     /* L691B */
        cmp6502(a, TEMP3, &c, NULL);  /* L691D CMP TEMP3 - the real payload: carry-in for OBJOBJ_30's SBC */
        a = 0x04;                     /* L691F LDA #$04 */
        /* L6921 BNE OBJOBJ_30: the LDA #$04 just clobbered Z (always 0,
         * since 4!=0) - a provably always-taken branch, the same idiom as
         * VGADD's own tail-branch hazard (CALLING_NOTES.md section 1: "not
         * a bug, but would read as an 'if not X then Y' branch to someone
         * porting line-by-line without checking what's actually tested").
         * Only the CMP's carry (not Z) matters here, and it survives the
         * LDA untouched. Modelled as an unconditional jump. */
        goto OBJOBJ_30;

OBJOBJ_20:
        CYC(13);                               /* L6923-L692A */
        a = cpu_rd((uint16_t)(A_PTBLO1 + y));  /* L6923 LDA PTBLO1,Y (re-read) */
        a = asl6502(a, &c);                    /* L6926 ASL */
        idx = a;                               /* L6927 TAX - object#*2, PROXTB index; kept separate from `x` (the routine's own parameter), which OBJOBJ_25/30 restore via "LDX TEMP1" in the ROM - this port simply never overwrites `x` in the first place */
        if (!(R2D3FL & 0x80)) { CYC(1); goto OBJOBJ_25; }  /* L6928/L692A BIT/BPL */
        CYC(34);                               /* L692C-L6941 */

        a = TEMP3_AT(1);                        /* L692C LDA TEMP3+1 - 3/4 tank radius */
        cmp6502(a, 0x80, &c, NULL);             /* L692E CMP #$80 */
        a = ror6502(a, &c);                     /* L6930 ROR */
        TEMP2_AT(1) = a;                        /* L6931 */
        a = TEMP3;                              /* L6933 */
        a = ror6502(a, &c);                     /* L6935 ROR */
        c = 0;                                  /* L6936 CLC */
        a = adc6502(a, TEMP3, &c, NULL);        /* L6937 ADC TEMP3 */
        TEMP3 = a;                              /* L6939 */
        a = TEMP2_AT(1);                        /* L693B */
        a = adc6502(a, TEMP3_AT(1), &c, NULL);  /* L693D ADC TEMP3+1 */
        TEMP3_AT(1) = a;                        /* L693F */
        if (!c) { CYC(1); goto OBJOBJ_25; }     /* L6941 BCC */
        CYC(6);                                 /* L6943 LDX / L6945 BCS (taken) */
        goto OBJOBJ_40;                         /* L6943/L6945 LDX TEMP1 (no-op: x unchanged)/BCS - carry proven set, always taken */

OBJOBJ_25:
        CYC(14);                                           /* L6947-L694F */
        a = cpu_rd((uint16_t)(A_ENEMY_PROXTB + idx));      /* L6947 LDA PROXTB,X - if collision */
        cmp6502(a, TEMP3, &c, NULL);                       /* L694A CMP TEMP3 */
        a = cpu_rd((uint16_t)(A_ENEMY_PROXTB + idx + 1));  /* L694C LDA PROXTB+1,X */
        /* L694F LDX TEMP1: x (the routine's parameter) was never touched, so
         * there is nothing to restore in this port. */

OBJOBJ_30:
        CYC(7);                                 /* L6951-L6955 */
        a = sbc6502(a, TEMP3_AT(1), &c, NULL);  /* L6951 SBC TEMP3+1 */
        a = 0x00;                               /* L6953 LDA #$00 - set for nontank collision */
        if (c) { CYC(1); goto OBJOBJ_60; }      /* L6955 BCS */

OBJOBJ_40:
        y = (uint8_t)(y + 2);  /* L6957/L6958 INY INY - next object */
        CYC(y != 0 ? 7 : 6);   /* L6957-L6959, BNE taken while Y != 0 */
        if (y != 0) continue;  /* L6959 BNE OBJOBJ_10 */
        goto OBJOBJ_50;
    }

OBJOBJ_50:
    CYC(8);                      /* L695B-L695F */
    a = COLFLG;                  /* L695B LDA COLFLG - if either in collision */
    a |= COLFLG_AT(2);           /* L695D ORA COLFLG+2 */
    if (a != 0) { CYC(1); goto OBJOBJ_60; }  /* L695F BNE - genuine hazard: reaches the shared exit with r.a = COLFLG|COLFLG+2 (NOT the documented 0/$FF) and r.c = whatever the LAST obstacle-loop iteration's own CMP/SBC left (or, if the table's very first entry is already $FF, whatever carry this function's own Mathbox-register stores left untouched from the caller) - see NOTES_enemy.md */

    CYC(6 + 32 + 22 + 5);                              /* L6961-L6964, L6965-L6976, L6979-L6985 (dummy read included), L6987 BIT / L6989 BPL */
    x ^= 0x02;                                         /* L6961/L6962/L6964 TXA/EOR #$02/TAX - flip to the OTHER tank; safe to reuse `x` now, nothing afterward needs the original parameter */
    a = TPOSX_AT(x);                                   /* L6965 */
    hw_mb_write((uint8_t)(A_TPOSXT - 0x1860), a);      /* L6967 */
    a = TPOSX_AT((uint8_t)(x + 1));                    /* L696A */
    hw_mb_write((uint8_t)(A_TPOSXT + 1 - 0x1860), a);  /* L696C */
    a = TPOSY_AT(x);                                   /* L696F */
    hw_mb_write((uint8_t)(A_TPOSYT - 0x1860), a);      /* L6971 */
    a = TPOSY_AT((uint8_t)(x + 1));                    /* L6974 */
    hw_mb_write((uint8_t)(A_DSTNCE - 0x1860), a);      /* L6976 STA DSTNCE - starts the OTHER tank's distance function */

    /* L6979: CMP ($00,X) - dummy delay read ("WASTE TIME", the ROM's own
     * comment), emit nothing. */

    a = hw_mb_ylow();   /* L697B LDA MOUTLO */
    TEMP3 = a;          /* L697E */
    a = hw_mb_yhigh();  /* L6980 LDA MOUTHI */
    TEMP3_AT(1) = a;    /* L6983 */
    a = 0x05;           /* L6985 */

    if (!(R2D3FL & 0x80)) { CYC(1); goto OBJOBJ_55; }  /* L6987/L6989 BIT/BPL - smaller collision radius for R2D3 */
    CYC(2);                                /* L698B LDA */
    a = 0x03;                              /* L698B */

OBJOBJ_55:
    CYC(7);                                 /* L698D-L6990 */
    c = 1;                                  /* L698D SEC */
    a = sbc6502(a, TEMP3_AT(1), &c, NULL);  /* L698E SBC TEMP3+1 - carry set if collision */
    a = 0xFF;                               /* L6990 LDA #$FF - indicate tanks, unconditional */

OBJOBJ_60:
    CYC(9);   /* L6992 LDX / L6994 RTS */
    r.a = a;  /* L6992 LDX TEMP1 (this port has no X output) */
    r.c = (uint8_t)c;
    return r;  /* L6994 RTS */
}

/* PROXTB ($6995) is the ROM data table read above (16 words, indexed by
 * PTBLO1's own object#*2) - kept in progrom.c/vecrom.c per TRANSLATING.md
 * ("ROM tables... never hand-transcribed"), read only via cpu_rd() above. */

/* ROBCHK ($69BE): robot reset check, shared with the NMI handler
 * (CALLING_NOTES.md section 7: called from both NMI_65 and mainline
 * MAIN_1/MAIN_20-adjacent).  Atari's header: "WILL RANDOMLY RESET HIS GOAL
 * AND TAKE HIM OUT OF ATTACK MODE FOR A SHORT TIME... INPUT: COLFLG.
 * OUTPUT: TPOSX,TPOSY,COLFLG,STATE,ACTION,RGOAL. SUBROUTINES: RAND,
 * OBJBOJ,ROBOT" - none of RAND/OBJOBJ/ROBOT are actually called from here
 * (they're reached, if at all, deeper inside TANKCK/R2D3CK/ROB1's own
 * chain, not directly); same stale-header pattern already flagged for
 * BUZBOM/TRACK/PNTPUT/DRAW (CALLING_NOTES.md).  No inputs/outputs of its
 * own; tail-calls tankck()/r2d3ck() (this file) for every real exit. */
void robchk(void)
{
    uint8_t a, x, y;
    unsigned c;

    CYC(5);                                                  /* L69BE LDA / L69C0 BNE */
    a = GOVER;                                               /* L69BE */
    if (a != 0) { CYC(1); tankck(); return; }                /* L69C0 BNE TANKCK - tail call */
    CYC(5);                                                  /* L69C2 LDA / L69C4 BNE */
    a = HITS_AT(1);                                          /* L69C2 */
    if (a != 0) { CYC(1); goto ROBCHK_10; }                  /* L69C4 BNE */
    CYC(21);                                                 /* L69C6-L69D3 */
    a = hw_option();                                         /* L69C6 LDA OPTION */
    a = lsr6502(a, &c);                                      /* L69C9 */
    a = lsr6502(a, &c);                                      /* L69CA */
    a &= 0x03;                                               /* L69CB */
    x = a;                                                   /* L69CD TAX */
    a = HITS;                                                /* L69CE LDA HITS - if not buzz bomb */
    cmp6502(a, cpu_rd((uint16_t)(A_MISLVL + x)), &c, NULL);  /* L69D0 CMP MISLVL,X */
    if (!c) { CYC(1); tankck(); return; }                    /* L69D3 BCC TANKCK - tail call */

ROBCHK_10:
    CYC(18);                       /* L69D5-L69E0 */
    y = hw_pokey_read(0x0A);       /* L69D5 LDY PRAND - fudge POKEY randomness */
    a = y;                         /* L69D8 TYA */
    a ^= OLDRND;                   /* L69D9 EOR OLDRND */
    OLDRND = y;                    /* L69DC STY OLDRND - stores the ORIGINAL PRAND byte, not the EORed `a` */
    a = lsr6502(a, &c);            /* L69DF LSR */
    if (!c) { CYC(1); tankck(); return; }  /* L69E0 BCC TANKCK - tail call */
    CYC(8);                        /* L69E2-L69E6, BNE taken */
    TIMOUT = 0x02;                 /* L69E2/L69E4 - set shorter wait time */
    r2d3ck();                      /* L69E6 BNE R2D3CK - TIMOUT just set to 2 (nonzero); STA doesn't touch flags, so this tests the LDA #$02's own Z, always false - provably always taken, same idiom noted elsewhere in this file. Tail call. */
}

/* TANKCK ($69E8): reset the robot as a plain tank - new random RGOAL, clear
 * R2D3FL/COLFLG+2, short reaction/timeout; in attract mode also computes a
 * random placement window ahead of the player before reaching ROB1;
 * otherwise picks $0F and falls into ROB1 directly (listing's own [note]
 * header, reviewed against the code and reliable per TRANSLATING.md). No
 * inputs/outputs; called directly from outside this cluster too
 * (bz_main's MAIN_1/MAIN, DRADAR - CALLING_NOTES.md/NOTES_game.md). */
void tankck(void)
{
    uint8_t a, x;
    unsigned c;
    int z;

    CYC(28);                               /* L69E8-L69FB */
    a = hw_pokey_read(0x0A);               /* L69E8 LDA PRAND - reset goal */
    RGOAL = a;                             /* L69EB */
    R2D3FL = 0x00;                         /* L69ED/L69EF - reset to tank */
    COLFLG_AT(2) = 0x00;                   /* L69F1 - out of collision */
    ACTION = 0x01;                         /* L69F3/L69F5 - set reaction time short */
    TIMOUT = 0x01;                         /* L69F7 */
    if (!(ATRACT & 0x80)) { CYC(1); goto TANKCK_10; }  /* L69F9/L69FB BIT ATRACT/BPL - if attract, put tank in front */
    CYC(5);                                /* L69FD LDA / L69FF BNE */
    a = HITS_AT(1);                        /* L69FD */
    if (a != 0) { CYC(1); goto TANKCK_6; }  /* L69FF BNE */
    CYC(10);                               /* L6A01-L6A06 */
    c = 1;                                 /* L6A01 SEC - determine window for tank */
    a = HITS;                              /* L6A02 */
    a = sbc6502(a, HITS_AT(2), &c, &z);    /* L6A04 SBC HITS+2 */
    if (!c) { CYC(1); goto TANKCK_10; }    /* L6A06 BCC */
    CYC(2);                                /* L6A08 BEQ */
    if (z) { CYC(1); goto TANKCK_10; }     /* L6A08 BEQ */
    CYC(4);                                /* L6A0A CMP / L6A0C BCC */
    cmp6502(a, 0x07, &c, NULL);            /* L6A0A CMP #$07 */
    if (!c) { CYC(1); goto TANKCK_8; }     /* L6A0C BCC */

TANKCK_6:
    CYC(2);    /* L6A0E LDA */
    a = 0x07;  /* L6A0E */

TANKCK_8:
    CYC(4);                      /* L6A10 LSR / L6A11 BEQ */
    a = lsr6502(a, &c);          /* L6A10 LSR */
    if (a == 0) { CYC(1); goto TANKCK_10; }  /* L6A11 BEQ */
    CYC(4);                      /* L6A13 TAX / L6A14 LDA */
    x = a;                       /* L6A13 TAX */
    a = 0x0F;                    /* L6A14 */

TANKCK_9:
    c = 1;                      /* L6A16 SEC */
    a = rol6502(a, &c);         /* L6A17 ROL */
    x--;                        /* L6A18 DEX */
    CYC(x != 0 ? 9 : 8);        /* L6A16-L6A19, BNE taken while X != 0 */
    if (x != 0) goto TANKCK_9;  /* L6A19 BNE */
    CYC(3);                     /* L6A1B BEQ (taken) */
    rob1(a);                    /* L6A1B BEQ ROB1 - x==0, always taken here; tail call */
    return;

TANKCK_10:
    CYC(5);    /* L6A1D LDA / L6A1F JMP */
    a = 0x0F;  /* L6A1D */
    rob1(a);   /* L6A1F JMP ROB1 - tail call */
}

/* R2D3CK ($6A22): (re)spawn the buzz bomber (R2D3) instead of a tank -
 * increments NOR2D3, sets EXPOSZ+$C/+$D to STARTZ (the buzz bomber's own
 * starting Z depth - a literal 16-bit constant here, NOT the Mathbox/MSTAT
 * address that happens to share $1800, per state_defs.h's own A_STARTZ
 * note), sets STATE=$80 (the attack flag), forces the engine sound channels
 * to their idle frequencies, sets R2D3FL=-1, then falls straight into ROB1
 * with a = PRAND&$0F (listing's own [note] header, reviewed and reliable).
 * No inputs/outputs; called directly too (bz_main's MAIN, DRADAR). */
void r2d3ck(void)
{
    uint8_t a;

    CYC(46);                                           /* L6A22-L6A43 */
    NOR2D3++;                                          /* L6A22 INC NOR2D3 - sets NOR2D3 to 0 for the first R2D3 */
    EXPOSZ_AT(0x0D) = (uint8_t)(A_STARTZ >> 8);        /* L6A25/L6A27 LDA #STARTZ/$100 / STA EXPOSZ+$D */
    EXPOSZ_AT(0x0C) = (uint8_t)(A_STARTZ & 0xFF);      /* L6A2A/L6A2C LDA #STARTZ&$FF / STA EXPOSZ+$C */
    STATE = 0x80;                                      /* L6A2F/L6A31 */
    hw_pokey_write((uint8_t)(A_CHAN3F & 0x0F), 0xFF);  /* L6A33/L6A35 LDX #$FF/STX CHAN3F */
    hw_pokey_write((uint8_t)(A_CHAN4F & 0x0F), 0xFE);  /* L6A38/L6A39 DEX/STX CHAN4F */
    R2D3FL = 0xFF;                                     /* L6A3C/L6A3E - put buzz bomb in front of him */
    a = hw_pokey_read(0x0A);                           /* L6A40 LDA PRAND */
    a &= 0x0F;                                         /* L6A43 */
    rob1(a);                                           /* falls straight into ROB1 (no branch) */
}

/* ROB1 (falls in from R2D3CK, or reached from TANKCK's own "JMP ROB1"/
 * "BEQ ROB1") - robot AI helper: pick a random starting position/angle
 * ahead of the player for the newly (re)spawned enemy.  Never JSR'd/JMP'd
 * from outside this ROBCHK/TANKCK/R2D3CK/ROB1 cluster (grepped the whole
 * disassembly: no other reference) - `static` per TRANSLATING.md/
 * SIGNATURES.md's own convention for such internal-only fall-through
 * targets (matches bzone_game.c's `c21s_`/`m_top` etc.; no bzone_game.h
 * entry - the pre-existing `void rob1(void);` draft prototype there is
 * removed, see NOTES_enemy.md).  In: a = PRAND-derived pseudo-random seed
 * (TANKCK's own ROL-loop result, or R2D3CK's PRAND&$0F) - a genuine input,
 * confirmed by ROB1's own first instruction (STA TEMP1).  No outputs. */
static void rob1(uint8_t a)
{
    uint8_t x;
    unsigned c = 0;
    unsigned saved_c = 0;
    regs r;

    CYC(15);                           /* L6A45-L6A4E */
    TEMP1 = a;                         /* L6A45 STA TEMP1 */
    a = hw_pokey_read(0x0A);           /* L6A47 LDA PRAND */
    a &= TEMP1;                        /* L6A4A AND TEMP1 */
    if (!(S_INTCT & 0x40)) { CYC(2); a ^= 0xFF; } else CYC(1);  /* L6A4C/L6A4E/L6A50 BIT S_INTCT (V=bit6)/BVS/EOR #$FF */
    CYC(14);                           /* L6A52-L6A57 (JSR COS included) */

    c = 0;                             /* L6A52 CLC */
    a = adc6502(a, TANGLE, &c, NULL);  /* L6A53 ADC TANGLE */
    TEMP1 = a;                         /* L6A55 */
    r = cos_(a);                       /* L6A57 JSR COS */
    a = r.a;
    CYC(56);                                /* L6A5A-L6A7B */
    TEMP2 = a;                              /* L6A5A STA TEMP2 - X = X - 1/4*X */
    X1 = a;                                 /* L6A5C STA X1 */
    X1_AT(1) = r.x;                         /* L6A5E STX X1+1 */
    a = r.x;                                /* L6A60 TXA */
    cmp6502(a, 0x80, &c, NULL);             /* L6A61 CMP #$80 */
    a = ror6502(a, &c);                     /* L6A63 ROR */
    TEMP2 = ror6502(TEMP2, &c);             /* L6A64 ROR TEMP2 */
    cmp6502(a, 0x80, &c, NULL);             /* L6A66 CMP #$80 */
    a = ror6502(a, &c);                     /* L6A68 ROR */
    TEMP2 = ror6502(TEMP2, &c);             /* L6A69 ROR TEMP2 */
    TEMP2_AT(1) = a;                        /* L6A6B STA TEMP2+1 */
    a = X1;                                 /* L6A6D LDA X1 */
    c = 1;                                  /* L6A6F SEC */
    a = sbc6502(a, TEMP2, &c, NULL);        /* L6A70 SBC TEMP2 */
    TEMP2 = a;                              /* L6A72 */
    a = X1_AT(1);                           /* L6A74 LDA X1+1 */
    a = sbc6502(a, TEMP2_AT(1), &c, NULL);  /* L6A76 SBC TEMP2+1 */
    x = a;                                  /* L6A78 TAX */
    if (R2D3FL & 0x80) { CYC(1); goto ROB1_45; }  /* L6A79/L6A7B BIT/BMI */
    CYC(13);                                /* L6A7D-L6A83 */
    a = hw_pokey_read(0x0A);                /* L6A7D LDA PRAND */
    a = lsr6502(a, &c);                     /* L6A80 LSR */
    saved_c = c;                            /* L6A81 PHP - genuinely needed (unlike MOTION's M_NEX0 PHP/PLPs, NOTES_game.md): the SIN call and several CMP/ROR below overwrite the live carry long before the matching PLP at L6AB3 reuses this exact value. Reached only on this (R2D3FL clear) path, and the matching PLP below is reached only when R2D3FL is STILL clear there too (same flag, never modified in between) - so `saved_c` is always set before it's read; see NOTES_enemy.md. */
    a = x;                                  /* L6A82 TXA */
    if (!c) { CYC(1); goto ROB1_45; }       /* L6A83 BCC - tests the SAME live carry saved_c just captured */
    CYC(9);                                 /* L6A85-L6A88 */
    cmp6502(a, 0x80, &c, NULL);             /* L6A85 CMP #$80 */
    a = ror6502(a, &c);                     /* L6A87 ROR */
    TEMP2 = ror6502(TEMP2, &c);             /* L6A88 ROR TEMP2 */

ROB1_45:
    CYC(12);          /* L6A8A-L6A8E (JSR SIN included) */
    TEMP2_AT(1) = a;  /* L6A8A STA TEMP2+1 */
    a = TEMP1;        /* L6A8C */
    r = sin_(a);      /* L6A8E JSR SIN */
    a = r.a;
    CYC(54);                                /* L6A91-L6AB1 */
    TEMP1 = a;                              /* L6A91 STA TEMP1 - Y = Y - 1/4*Y */
    Y1 = a;                                 /* L6A93 STA Y1 */
    Y1_AT(1) = r.x;                         /* L6A95 STX Y1+1 */
    a = r.x;                                /* L6A97 TXA */
    cmp6502(a, 0x80, &c, NULL);             /* L6A98 CMP #$80 */
    a = ror6502(a, &c);                     /* L6A9A ROR */
    TEMP1 = ror6502(TEMP1, &c);             /* L6A9B ROR TEMP1 */
    cmp6502(a, 0x80, &c, NULL);             /* L6A9D CMP #$80 */
    a = ror6502(a, &c);                     /* L6A9F ROR */
    TEMP1 = ror6502(TEMP1, &c);             /* L6AA0 ROR TEMP1 */
    TEMP1_AT(1) = a;                        /* L6AA2 STA TEMP1+1 */
    c = 1;                                  /* L6AA4 SEC */
    a = Y1;                                 /* L6AA5 LDA Y1 */
    a = sbc6502(a, TEMP1, &c, NULL);        /* L6AA7 SBC TEMP1 */
    TEMP1 = a;                              /* L6AA9 */
    a = Y1_AT(1);                           /* L6AAB LDA Y1+1 */
    a = sbc6502(a, TEMP1_AT(1), &c, NULL);  /* L6AAD SBC TEMP1+1 */
    if (R2D3FL & 0x80) { CYC(1); goto ROB1_49; }  /* L6AAF/L6AB1 BIT/BMI */
    CYC(6);                                 /* L6AB3 PLP / L6AB4 BCC */
    c = saved_c;                            /* L6AB3 PLP - restore the PRAND-LSR carry PHP saved above */
    if (!c) { CYC(1); goto ROB1_49; }       /* L6AB4 BCC */
    CYC(9);                                 /* L6AB6-L6AB9 */
    cmp6502(a, 0x80, &c, NULL);             /* L6AB6 CMP #$80 */
    a = ror6502(a, &c);                     /* L6AB8 ROR */
    TEMP1 = ror6502(TEMP1, &c);             /* L6AB9 ROR TEMP1 */

ROB1_49:
    CYC(48);                                /* L6ABB-L6AD9 */
    TEMP1_AT(1) = a;                        /* L6ABB STA TEMP1+1 - set up starting pos */
    c = 0;                                  /* L6ABD CLC */
    a = TPOSX;                              /* L6ABE */
    a = adc6502(a, TEMP2, &c, NULL);        /* L6AC0 */
    TPOSX_AT(2) = a;                        /* L6AC2 */
    a = TPOSX_AT(1);                        /* L6AC4 */
    a = adc6502(a, TEMP2_AT(1), &c, NULL);  /* L6AC6 */
    TPOSX_AT(3) = a;                        /* L6AC8 */
    a = TPOSY;                              /* L6ACA */
    c = 0;                                  /* L6ACC CLC */
    a = adc6502(a, TEMP1, &c, NULL);        /* L6ACD */
    TPOSY_AT(2) = a;                        /* L6ACF */
    a = TPOSY_AT(1);                        /* L6AD1 */
    a = adc6502(a, TEMP1_AT(1), &c, NULL);  /* L6AD3 */
    TPOSY_AT(3) = a;                        /* L6AD5 */
    if (!(R2D3FL & 0x80)) { CYC(1); goto ROB1_50; }  /* L6AD7/L6AD9 BIT/BPL */
    CYC(6);                                 /* L6ADB JSR */
    a = track();                            /* L6ADB JSR TRACK - set up starting angle */
    CYC(6);                                 /* L6ADE STA / L6AE0 STA */
    TANGLE_AT(2) = a;                       /* L6ADE */
    RGOAL = a;                              /* L6AE0 */

ROB1_50:
    CYC(14);        /* L6AE2-L6AE8 */
    EIRNGE = 0x00;  /* L6AE2/L6AE4 */
    FTIMER = 0x00;  /* L6AE6 */
                                                                                                                                                                                                                                                                                               /* L6AE8 RTS */
}
