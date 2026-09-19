/* coin65.c - COIN65/TC65 ($56C3-$57C7, assembled inline in BZONE.MAC): the
 * coin/credit routine, Atari's shared library (Downend & Albaugh), THIS
 * game's own earlier revision of the module Tempest's alcoin.c and Space
 * Duel's coins.c also carry.
 *
 * Started from Tempest's C:\Source2026\Tempest Dissasembly\c_src\alcoin.c
 * (compared against Space Duel's coins.c too) and re-checked instruction by
 * instruction against THIS ROM's own listing
 * (../disasm/bzone_program_rom.asm $56C3-$57C7) and Atari's source
 * (../battlezone-main/BZONE.MAC, right after the coin-option equates -
 * MECHS=3, EMCTRS=3, $LMBIT=8, BONADD=1 - and right before ".INCLUDE TC65").
 *
 * The genuine difference from the later COIN65 (../disasm/CALLING_NOTES.md
 * section 6, TRANSLATING.md brief): THIS revision has no separate "$BC"
 * bonus-coin-earned cell. S_BONUS (here also labelled S_DETCT_120) credits
 * a bonus coin by incrementing S_CNCT ($21, the unit-coin count) directly
 * ("INC S_CNCT" at L576E/L5774) - so by the time S_EXTB/S_CNVRT converts
 * unit-coins to credits, any bonus coins are already folded into S_CNCT and
 * need no separate "cover the shortfall from $BC" step the later COIN65 has
 * (compare alcoin.c's s_extb(), which adds S_BC before testing for a
 * shortfall - here, on a shortfall, S_EXTB just leaves S_CNCT untouched and
 * falls into S_EXT, L578F "BCC S_EXT"). Confirmed by diffing this listing
 * against Tempest's - see NOTES_nmi_sound_coin.md.
 *
 * Debounce state per mech, indexed X = 2 (right), 1 (centre), 0 (left):
 *   S_CNSTT,X  coin-on down-counter (bits 0-4, starts $1F) / coin-off
 *              up-counter (bits 5-7), packed in one byte
 *   S_PSTSL,X  post-coin slam timer (a slam within this window invalidates
 *              a just-accepted coin - "give credit a little early" is the
 *              one place a pending post-coin timer wins a race)
 *   S_CCTIM,X  electro-mechanical counter pulse timer: queued/running pulse
 *              count in the low nibble, "is running" flag via bit4+ (S_EXT)
 * Shared across mechs: S_LMTIM (pre-coin slam timer), S_CMODE (OPTON2 copy,
 * coin mode/multipliers/bonus-adder mode), S_BCCNT (bonus-adder unit-coin
 * accumulator), S_CNCT (unit-coin count), S_S_CRDT (credits).
 *
 * Runs entirely in binary mode (no SED anywhere in this module) - nmi()
 * CLDs before calling MOOLAH precisely to protect this arithmetic
 * (../disasm/CALLING_NOTES.md section 0).
 *
 * Cycle counting (TRANSLATING.md/hw.h CYC()): MOOLAH/S_DETCT/S_BONUS/S_EXTB/
 * S_EXT are ONE ROM routine (../disasm/bzone_program_rom.asm $56C3-$57C7)
 * that falls from one label into the next via plain branches, never JSR -
 * detct1()/detct6()/s_bonus()/s_extb() below are C functions purely for
 * readability, not real ROM subroutines, so calling them costs nothing (no
 * JSR charged by the caller) and returning from them costs nothing (no RTS
 * charged inside) - all their instructions are charged inline as ordinary
 * straight-line/branch cost, same as if they'd been pasted in place. Only
 * s_ext(), which ends in the routine's one real RTS ($57C7), charges that
 * RTS - at each of its `return`s and at its natural fall-off. See
 * NOTES_nmi_sound_coin.md "Cycle counting" for the S_DETCT_8 BCC (charged
 * once in moolah(), not duplicated in detct1()/detct6()) and the constant-
 * cost observation in s_extb(). */
#include "state.h"
#include "hw.h"
#include "coin65.h"


/* S_MODLO ($5778): unit-coins required per bonus-adder mode (OPTON2 bits
 * 5-7), 8 bytes inline in program ROM right after S_BONUS. $7F = "never"
 * sentinel (an 8-bit SBC always goes negative against it). ROM data, read
 * through cpu_rd() - never retyped (TRANSLATING.md). */
#define A_S_MODLO 0x5778

static void s_bonus(void);
static void s_extb(void);
static void s_ext(void);

/* S_DETCT_1 ($56E9): the generic debounce tail shared by the coin-present
 * path (straight fallthrough) and one of S_DETCT_6's two exits (BCS
 * S_DETCT_1). Stores the updated coin-status byte, re-arms the pre-coin
 * slam timer from S_LAM, runs S_LMTIM/S_PSTSL, and returns the "coin
 * falls out" carry for S_DETCT_8.
 *
 * Not a real ROM subroutine (see file header) - no JSR/RTS cost here; the
 * caller charges nothing for the call and S_DETCT_8's own BCC (the ROM
 * instruction right after this fallthrough tail) is charged once in
 * moolah(), not here. */
static unsigned detct1(int x, uint8_t a)
{
    unsigned credit;

    CYC(4);                               /* L56E9 STA S_CNSTT,X */
    S_CNSTT_AT(x) = a;                    /* L56E9 STA S_CNSTT,X */
    CYC(6);                               /* L56EB LDA S_LAM / L56EE AND #$LMBIT */
    if (!(hw_in0() & K_S_LMBIT))       {  /* L56F0 BNE S_DETCT_2 (not taken) */
        CYC(2);
        S_LMTIM = 0xF0;  /* L56F2/F4: pre-coin slam timer, PRST*8 */
        CYC(5);          /* L56F2 LDA #$F0 / L56F4 STA S_LMTIM */
    } else {
        CYC(3);  /* L56F0 BNE taken */
    }
    CYC(3);              /* S_DETCT_2 L56F6 LDA S_LMTIM */
    if (S_LMTIM != 0) {  /* S_DETCT_2 L56F6/F8 (not taken) */
        CYC(2);
        S_LMTIM = (uint8_t)(S_LMTIM - 1);  /* L56FA */
        CYC(5);                            /* L56FA DEC S_LMTIM */
        S_CNSTT_AT(x) = 0;                 /* L56FC/FE */
        CYC(6);                            /* L56FC LDA #$00 / L56FE STA S_CNSTT,X */
        S_PSTSL_AT(x) = 0;                 /* L5700 */
        CYC(4);                            /* L5700 STA S_PSTSL,X */
    } else {
        CYC(3);  /* S_DETCT_2 L56F8 BEQ taken */
    }
    credit = 0;  /* S_DETCT_3 L5702 CLC: default no coin */
    CYC(2);
    CYC(4);                    /* L5703 LDA S_PSTSL,X */
    if (S_PSTSL_AT(x) != 0) {  /* L5703/05 (not taken) */
        CYC(2);
        S_PSTSL_AT(x) = (uint8_t)(S_PSTSL_AT(x) - 1);  /* L5707 */
        CYC(6);                                        /* L5707 DEC S_PSTSL,X */
        if (S_PSTSL_AT(x) == 0) {                      /* L5709 BNE S_DETCT_8 (not taken) */
            CYC(2);
            credit = 1;  /* L570B/0C SEC */
            CYC(2);      /* L570B SEC */
            CYC(3);      /* L570C BCS S_DETCT_1... (S_DETCT_8, always taken) */
        } else {
            CYC(3);  /* L5709 BNE taken */
        }
    } else {
        CYC(3);  /* L5705 BEQ taken */
    }
    return credit;  /* S_DETCT_8 ($572A) - BCC charged by the caller */
}

/* S_DETCT_6's OTHER exit ($571F, carry clear on entry): a genuinely valid
 * coin-off transition. Stores $1F directly (no slam re-check - that's the
 * difference from detct1), gives credit a little early if a post-coin
 * timer was already pending, then unconditionally reloads it.
 *
 * Not a real ROM subroutine (see file header/detct1 comment) - no JSR/RTS
 * cost here. */
static unsigned detct6(int x, uint8_t a)
{
    unsigned credit;

    CYC(4);                    /* L571F STA S_CNSTT,X */
    S_CNSTT_AT(x) = a;         /* L571F STA S_CNSTT,X */
    CYC(4);                    /* L5721 LDA S_PSTSL,X */
    if (S_PSTSL_AT(x) != 0) {  /* L5723 BEQ S_DETCT_7 (not taken) */
        CYC(2);
        credit = 1u;  /* L5725 SEC */
        CYC(2);       /* L5725 SEC */
    } else {
        CYC(3);  /* L5723 BEQ taken */
        credit = 0u;
    }
    S_PSTSL_AT(x) = 0x78;  /* S_DETCT_7 L5726/28: POST*4 frames */
    CYC(6);                /* L5726 LDA #$78 / L5728 STA S_PSTSL,X */
    return credit;         /* S_DETCT_8 - BCC charged by the caller */
}

/* MOOLAH ($56C3): the mech-debounce loop (S_DETCT, $56C5-$5759), then falls
 * into S_BONUS. */
void moolah(void)
{
    int x;

    CYC(2);                     /* L56C3 LDX #$02 */
    for (x = 2; x >= 0; x--) {  /* L56C3 LDX #$02; S_DETCT_9 L5756/59 DEX/JMP S_DETCT */
        uint8_t a;
        unsigned coin_absent;
        unsigned credit;
        CYC(4);                  /* L56C5 LDA S_COINA */
        uint8_t in0 = hw_in0();  /* L56C5 LDA S_COINA (aliases IN0, $0800) */

        /* L56C8-56D0: shift this mech's coin bit into carry. X=2 (right):
         * 1 LSR -> bit0. X=1 (centre): 2 LSRs -> bit1. X=0 (left): 3 LSRs
         * -> bit2. */
        CYC(x == 0 ? 12 : 9);  /* L56C8 CPX #$01 / L56CA BEQ / L56CC BCS / LSR(s) */
        coin_absent = (unsigned)(in0 >> (x == 2 ? 0 : x == 1 ? 1 : 2)) & 1u;
        CYC(6);                               /* L56D1 LDA S_CNSTT,X / L56D3 AND #$1F */
        a = (uint8_t)(S_CNSTT_AT(x) & 0x1F);  /* L56D1/D3 LDA S_CNSTT,X / AND #$1F */

        CYC(coin_absent ? 3 : 2);  /* L56D5 BCS S_DETCT_5 */
        if (!coin_absent) {        /* L56D5 BCS S_DETCT_5: 0 = coin present */
            if (a != 0) {  /* L56D7 BEQ S_DETCT_1 (not taken) */
                CYC(2);
                CYC(2);           /* L56D9 CMP #$1B */
                if (a >= 0x1B) {  /* L56DB BCS S_DETCT_10 (taken) */
                    CYC(3);
                    a = (uint8_t)(a - 1);  /* S_DETCT_10 L56E7 SBC #$01 (C set by the CMP) */
                    CYC(2);                /* L56E7 SBC #$01 */
                } else {
                    CYC(2);                          /* L56DB BCS not taken */
                    CYC(11);                         /* L56DD TAY / L56DE LDA S_INTCT / L56E0 AND #$07 / L56E2 CMP #$07 / L56E4 TYA */
                    if ((S_INTCT & 0x07) == 0x07) {  /* L56E5 BCC (not taken) */
                        CYC(2);
                        a = (uint8_t)(a - 1);  /* S_DETCT_10 (shared) */
                        CYC(2);                /* L56E7 SBC #$01 */
                    } else {
                        CYC(3);  /* L56E5 BCC taken */
                    }
                }
            } else {
                CYC(3);  /* L56D7 BEQ taken */
            }
            credit = detct1(x, a);  /* fallthrough into S_DETCT_1 */
        } else {
            /* S_DETCT_5 ($570E): coin absent. */
            unsigned c;
            CYC(2);           /* L570E CMP #$1B */
            if (a >= 0x1B) {  /* L5710 BCS S_DETCT_6 (taken) */
                CYC(8);  /* L5710 BCS taken / S_DETCT_6 L571B LDA #$1F / L571D BCS S_DETCT_1 (taken) */
                c = 1;
                a = 0x1F;               /* L571B (S_DETCT_6) LDA #$1F */
                credit = detct1(x, a);  /* L571D BCS S_DETCT_1 (c set) */
            } else {
                CYC(2);                                          /* L5710 BCS not taken */
                CYC(6);                                          /* L5712 LDA S_CNSTT,X / L5714 ADC #$20 (Cin=0) */
                unsigned sum = (unsigned)S_CNSTT_AT(x) + 0x20u;  /* L5712/14 LDA S_CNSTT,X / ADC #$20 (Cin=0) */
                a = (uint8_t)sum;
                if (sum <= 0xFFu) {  /* L5716 BCC S_DETCT_1 (taken): no wrap */
                    CYC(3);
                    credit = detct1(x, a);
                } else {
                    CYC(2);  /* L5716 BCC not taken */
                    /* L5718 BEQ S_DETCT_6 else L571A CLC, then S_DETCT_6's
                     * shared LDA #$1F / BCS S_DETCT_1 - both the "c=1" and
                     * "c=0" paths cost exactly 8 (3+2+3 taken vs 2+2+2+2
                     * not-taken): a genuine coincidence of this cascade's
                     * instruction lengths, not a simplification - see
                     * NOTES_nmi_sound_coin.md. */
                    CYC(8);                                    /* L5718 BEQ / [L571A CLC] / S_DETCT_6 L571B LDA #$1F / L571D BCS */
                    c = (a == 0) ? 1u : 0u;                    /* L5718 BEQ S_DETCT_6 else L571A CLC */
                    a = 0x1F;                                  /* S_DETCT_6 L571B */
                    credit = c ? detct1(x, a) : detct6(x, a);  /* L571D BCS S_DETCT_1 */
                }
            }
        }

        /* S_DETCT_8 ($572A): BCC S_DETCT_9 - skip the mech-multiplier
         * unless a coin genuinely fell out this tick. */
        CYC(2);  /* S_DETCT_8 L572A BCC S_DETCT_9 (not taken, base) */
        if (credit) {
            uint8_t units = 0;  /* L572C LDA #$00 (to add 1 below) */
            CYC(2);             /* L572C LDA #$00 */
            CYC(2);             /* L572E CPX #$01 */
            if (x == 0) {       /* L572E/30 CPX #$01/BCC S_DETCT_85 (taken): left, always +1 */
                CYC(3);
                /* units stays 0 */
            } else {
                CYC(2);        /* L5730 BCC not taken */
                if (x == 1) {  /* L5732 BEQ S_DETCT_83 (taken): centre */
                    CYC(3);
                    CYC(5);                /* S_DETCT_83 L5740 LDA S_CMODE / L5742 AND #$10 */
                    if (S_CMODE & 0x10) {  /* L5744 BEQ (not taken) */
                        CYC(2);
                        units = 1;  /* L5746 */
                        CYC(2);     /* L5746 LDA #$01 */
                    } else {
                        CYC(3);  /* L5744 BEQ taken */
                    }
                } else {  /* x == 2: right, GDM dollar mul */
                    CYC(2);                                    /* L5732 BEQ not taken */
                    CYC(9);                                    /* L5734 LDA S_CMODE / L5736 AND #$0C / L5738 LSR / L5739 LSR */
                    units = (uint8_t)((S_CMODE & 0x0C) >> 2);  /* L5734-39 */
                    if (units != 0) {                          /* L573A BEQ (not taken) */
                        CYC(2);
                        units = (uint8_t)(units + 2);  /* L573C: map 1,2,3 to 3,4,5 */
                        CYC(2);                        /* L573C ADC #$02 */
                        CYC(3);                        /* L573E BNE S_DETCT_85 (always taken) */
                    } else {
                        CYC(3);  /* L573A BEQ taken */
                    }
                }
            }
            CYC(29);                                       /* S_DETCT_85 L5748-5754: SEC/PHA/ADC/STA/PLA/SEC/ADC/STA/INC */
            S_BCCNT = (uint8_t)(units + S_BCCNT + 1u);     /* S_DETCT_85 L5748-4C: SEC/ADC S_BCCNT */
            S_CNCT  = (uint8_t)(units + S_CNCT + 1u);      /* L574E-52: SEC/ADC S_CNCT */
            S_CCTIM_AT(x) = (uint8_t)(S_CCTIM_AT(x) + 1);  /* L5754: queue an E.M. counter pulse */
        } else {
            CYC(1);  /* S_DETCT_8 BCC taken */
        }
        CYC(x > 0 ? 7 : 5);  /* S_DETCT_9 L5756 DEX / L5757 BMI S_BONUS / L5759 JMP S_DETCT */
    }
    s_bonus();  /* S_DETCT_9 L5757 BMI S_BONUS */
}

/* S_BONUS / S_DETCT_120 ($575C): bonus-adder. If enough unit-coins (S_BCCNT)
 * have accumulated for the DIP-selected bonus-adder mode (S_CMODE bits 5-7),
 * grant one bonus coin straight into S_CNCT (two in mode 3) - THIS COIN65
 * has no separate bonus-coin cell (see the file header). Falls into S_EXTB.
 *
 * Not a real ROM subroutine (see file header) - no JSR/RTS cost here. */
static void s_bonus(void)
{
    CYC(15);                                                             /* L575C LDA S_CMODE / LSR x5 / L5763 TAY */
    uint8_t y = (uint8_t)(S_CMODE >> 5);                                 /* L575C-63: GBAM, TAY */
    CYC(9);                                                              /* L5764 LDA S_BCCNT / L5766 SEC / L5767 SBC S_MODLO,Y */
    uint8_t a = (uint8_t)(S_BCCNT - cpu_rd((uint16_t)(A_S_MODLO + y)));  /* L5764-67: SEC/SBC S_MODLO,Y */

    if (!(a & 0x80)) {  /* L576A BMI S_EXTB (not taken): not enough yet */
        CYC(2);
        S_BCCNT = a;                     /* L576C */
        CYC(3);                          /* L576C STA S_BCCNT */
        S_CNCT = (uint8_t)(S_CNCT + 1);  /* L576E INC S_CNCT (straight to CNCT) */
        CYC(5);                          /* L576E INC S_CNCT */
        CYC(2);                          /* L5770 CPY #$03 */
        if (y == 3) {                    /* L5770/72 CPY #$03/BNE S_EXTB (not taken) */
            CYC(2);
            S_CNCT = (uint8_t)(S_CNCT + 1);  /* L5774: mode 3 yields 2 for 4 inserted */
            CYC(5);                          /* L5774 INC S_CNCT */
            CYC(3);                          /* L5776 BNE S_EXTB (always taken) */
        } else {
            CYC(3);  /* L5772 BNE taken */
        }
    } else {
        CYC(3);  /* L576A BMI taken */
    }
    s_extb();
}

/* S_EXTB / S_CNVRT ($5780): convert unit-coins to credits. Free play (mode
 * 0) just clears S_CNCT. Otherwise price = 0,1,1,2 for modes 0-3 (mode 0
 * never reaches here); a shortfall (borrow) leaves S_CNCT untouched and
 * skips straight to S_EXT - since bonus coins are already inside S_CNCT
 * (S_BONUS above), there is no separate "cover the shortfall" step here,
 * unlike the later COIN65 (alcoin.c's s_extb()). Falls into S_EXT.
 *
 * Not a real ROM subroutine (see file header) - no JSR/RTS cost here. */
static void s_extb(void)
{
    CYC(7);                                    /* L5780 LDA S_CMODE / L5782 AND #$03 / L5784 TAY */
    uint8_t mode = (uint8_t)(S_CMODE & 0x03);  /* L5780-84: GCM, TAY */

    if (mode != 0) {  /* L5785 BEQ S_CNVRT_2 (not taken): free play */
        CYC(2);
        CYC(11);                                                  /* L5787 LSR / L5788 ADC #$00 / L578A EOR #$FF / L578C SEC / L578D ADC S_CNCT */
        uint8_t price = (uint8_t)((mode >> 1) + (mode & 1u));     /* L5787/88: LSR/ADC #$00 -> 0,1,1,2 */
        unsigned diff = (unsigned)(uint8_t)~price + 1u + S_CNCT;  /* L578A-8D: EOR #$FF/SEC/ADC S_CNCT */

        if (diff > 0xFFu) {  /* L578F BCC S_EXT (not taken): borrow -> insufficient */
            CYC(2);
            uint8_t a = (uint8_t)diff;
            CYC(2);          /* L5791 CPY #$02 */
            if (mode < 2) {  /* L5791/93 CPY #$02/BCS S_CNVRT_1 (not taken) */
                CYC(2);
                S_S_CRDT = (uint8_t)(S_S_CRDT + 1);  /* L5795: modes 0,1 give two */
                CYC(5);                              /* L5795 INC S_S_CRDT */
            } else {
                CYC(3);  /* L5793 BCS taken */
            }
            S_S_CRDT = (uint8_t)(S_S_CRDT + 1);  /* S_CNVRT_1 L5797 */
            CYC(5);                              /* L5797 INC S_S_CRDT */
            S_CNCT = a;                          /* S_CNVRT_2 L5799: reduced coin count */
            CYC(3);                              /* L5799 STA S_CNCT */
        } else {
            CYC(3);  /* L578F BCC taken (borrow) */
        }
        /* diff <= 0xFF (borrow): S_CNCT left as-is, no credit this tick. */
    } else {
        CYC(3);      /* L5785 BEQ taken */
        S_CNCT = 0;  /* S_CNVRT_2 (mode 0: A was 0 from L5780/82) */
        CYC(3);      /* L5799 STA S_CNCT */
    }
    s_ext();
}

/* S_EXT ($579B): every other NMI tick (S_INTCT bit0), run the electro-
 * mechanical coin-counter pulse timers - phase 1 decrements the on-time of
 * any already-running pulse; if none is running, phase 2 starts at most one
 * pending pulse. Identical to Tempest's/Space Duel's (this part of COIN65
 * did not change between revisions).
 *
 * This IS where the whole MOOLAH/S_DETCT/S_BONUS/S_EXTB/S_EXT chain
 * actually returns (RTS $57C7, S_EXT_99) - charged at every `return` below
 * and at the natural fall-off, since all four exits share that one RTS. */
static void s_ext(void)
{
    int x;
    unsigned any_on;

    CYC(5);             /* L579B LDA S_INTCT / L579D LSR */
    if (S_INTCT & 1) {  /* L579E BCS S_EXT_99 (taken) */
        CYC(3);
        CYC(6);  /* S_EXT_99 L57C7 RTS */
        return;
    }
    CYC(2);  /* L579E BCS not taken */

    any_on = 0;                 /* L57A0 LDY #$00 */
    CYC(2);                     /* L57A0 LDY #$00 */
    CYC(2);                     /* L57A2 LDX #$02 */
    for (x = 2; x >= 0; x--) {  /* L57A2 LDX #$02 ... S_EXT_3 DEX/BPL */
        uint8_t t = S_CCTIM_AT(x);  /* L57A4 */
        CYC(4);                     /* L57A4 LDA S_CCTIM,X */
        if (t == 0) {               /* L57A6 BEQ S_EXT_3 (taken) */
            CYC(3);
            CYC(x > 0 ? 5 : 4);  /* S_EXT_3 L57B1 DEX / L57B2 BPL */
            continue;
        }
        CYC(2);          /* L57A6 BEQ not taken */
        CYC(2);          /* L57A8 CMP #$10 */
        if (t < 0x10) {  /* L57AA BCC S_EXT_3 (taken): neither running nor pending */
            CYC(3);
            CYC(x > 0 ? 5 : 4);  /* S_EXT_3 L57B1 DEX / L57B2 BPL */
            continue;
        }
        CYC(2);                               /* L57AA BCC not taken */
        S_CCTIM_AT(x) = (uint8_t)(t + 0xF0);  /* L57AC ADC #$EF (C=1): dec 4 MSBs */
        CYC(8);                               /* L57AC ADC #$EF / L57AE INY / L57AF STA S_CCTIM,X */
        any_on = 1;                           /* L57AE INY */
        CYC(x > 0 ? 5 : 4);                   /* S_EXT_3 L57B1 DEX / L57B2 BPL */
    }
    CYC(2);        /* L57B4 TYA */
    if (any_on) {  /* L57B5 BNE S_EXT_99 (taken) */
        CYC(3);
        CYC(6);  /* S_EXT_99 L57C7 RTS */
        return;
    }
    CYC(2);  /* L57B5 BNE not taken */

    CYC(2);                     /* L57B7 LDX #$02 */
    for (x = 2; x >= 0; x--) {  /* L57B7 LDX #$02 ... S_EXT_5 DEX/BPL */
        uint8_t t = S_CCTIM_AT(x);  /* L57B9 */
        CYC(4);                     /* L57B9 LDA S_CCTIM,X */
        if (t == 0) {               /* L57BB BEQ S_EXT_5 (taken): nothing pending */
            CYC(3);
            CYC(x > 0 ? 5 : 4);  /* S_EXT_5 L57C4 DEX / L57C5 BPL */
            continue;
        }
        CYC(2);                   /* L57BB BEQ not taken */
        t = (uint8_t)(t + 0xEF);  /* L57BD/BE CLC/ADC #$EF: start it */
        CYC(4);                   /* L57BD CLC / L57BE ADC #$EF */
        S_CCTIM_AT(x) = t;        /* L57C0 */
        CYC(4);                   /* L57C0 STA S_CCTIM,X */
        if (t & 0x80) {           /* L57C2 BMI S_EXT_99 (taken): only one at a time */
            CYC(3);
            CYC(6);  /* S_EXT_99 L57C7 RTS */
            return;
        }
        CYC(2);              /* L57C2 BMI not taken */
        CYC(x > 0 ? 5 : 4);  /* S_EXT_5 L57C4 DEX / L57C5 BPL */
    }
    CYC(6);  /* S_EXT_99 L57C7 RTS (natural fall-off) */
}
