/* bzone_math.c - BZONE maths and 3D pipeline: ROTATE/ROTPNT/WAIT ($5946-
 * $5B88), PNTPUT/KLUDGE/BOUND ($5D4A-$5E4A), COS/SIN/SIN1/COSXTR/SINXTR/
 * XTRAP ($5E4B-$5F4D), ABSTMP ($6149), TRACK/T_0-T_3 ($6810-$68D5).
 *
 * See NOTES_math.md for the Mathbox register choreography, ROM table
 * addresses and judgement calls; see bzone_math.h for the calling contract
 * of every exposed routine.
 *
 * These routines are the Mathbox's main users: every 2901 rotate/divide the
 * ROM performs lives in this file, always through the hw_mb_* seam in the
 * ROM's own write order (hw.h; ../disasm/MATHBOX_NOTES.md) - the model
 * behind the seam runs the real microcode, so results are bit-exact only if
 * the writes are the same bytes to the same registers in the same order.
 */
#include "state.h"
#include "hw.h"
#include "bzone_math.h"
#include "bzone_game.h"   /* explde() ($5B89), tr7chk() ($69B5) - game agent's
                            * module now has real prototypes matching what
                            * this file expected (uint8_t explde(uint8_t y);
                            * int tr7chk(void);); the earlier TODO(seam) block
                            * of temporary prototypes has been removed per
                            * TRANSLATING.md's seam rule. */
/* Integration fix (2026-09-18, NOTES_integration.md): COS/SIN are `cos_`/
 * `sin_` below (trailing underscore), not the bare `cos`/`sin` SIGNATURES.md's
 * naming rule would otherwise give them - see bzone_math.h's comment for
 * why (cl.exe reserves the bare names as compiler intrinsics; a `#pragma
 * function` workaround was tried first and does NOT work - it only
 * suppresses inlining of an already-double-typed definition, it cannot
 * repurpose the name for a different signature, so it still failed with
 * "C2371: redefinition; different basic types"). */

/* ---- Mathbox register offsets from $1860 (MBUDOC.DOC names; see
 * NOTES_math.md's choreography table).  Private to this module - not part
 * of any other module's contract. */
#define MB_ACOS     0x00  /* A low  */
#define MB_ACOS1    0x01  /* A high */
#define MB_BSIN     0x02  /* B low  */
#define MB_BSIN1    0x03  /* B high */
#define MB_TPOSXT   0x04  /* E low  */
#define MB_TPOSXT1  0x05  /* E high */
#define MB_TPOSYT   0x06  /* F low  */
#define MB_TPOSYT1  0x07  /* F high */
#define MB_XMATRX   0x08  /* X low  */
#define MB_XMATRX1  0x09  /* X high */
#define MB_YMATRX   0x0A  /* Y low  */
#define MB_YSTRT1   0x0B  /* Y high; write starts YHSM (rotate multiply) */
#define MB_ZLOW     0x0D  /* Z dividend byte 0 (lowest) */
#define MB_ZHI      0x0E  /* Z dividend byte 1 */
#define MB_DIVOUT   0x0F  /* Z dividend byte 2 */
#define MB_DIVOUT1  0x10  /* Z dividend byte 3 (highest) */
#define MB_YSTRT2   0x11  /* Y high (2nd); write starts YHSP ("whole mess") */
#define MB_RSTRT1   0x12  /* any write starts SYM (Y' multiply) */
#define MB_ZDIV     0x14  /* any write starts SZXD (Z/X' divide) */
#define MB_XPL      0x15  /* X' low  */
#define MB_XPH      0x16  /* X' high */

/* ---- ROM tables this module reads (cpu_rd, never hand-transcribed) ------
 * SIN1_99 ($5E77, program ROM): the only self-relative-addressed table in
 * the whole program (CALLING_NOTES.md section 3) - ported by real address,
 * there being no C equivalent of "(src: LDA AX,99$)".
 * PTBLX1/PTBLY1 (program ROM): parallel to PTBLO1 (which DOES have a
 * state_defs.h alias, A_PTBLO1); these two don't, so their real addresses
 * are given here directly, read off the listing. */
#define BZ_SIN1_99   0x5E77
#define BZ_PTBLX1    0x7681
#define BZ_PTBLY1    0x76AB

/* ---- local carry-in/out shift helpers (state.h only has ADC/SBC) -------- */
static inline uint8_t asl8(uint8_t v, unsigned *c) { *c = (unsigned)((v >> 7) & 1u); return (uint8_t)(v << 1); }
static inline uint8_t lsr8(uint8_t v, unsigned *c) { *c = (unsigned)(v & 1u);        return (uint8_t)(v >> 1); }
static inline uint8_t rol8(uint8_t v, unsigned *c) { unsigned cin = *c; *c = (unsigned)((v >> 7) & 1u); return (uint8_t)((v << 1) | cin); }
static inline uint8_t ror8(uint8_t v, unsigned *c) { unsigned cin = *c; *c = (unsigned)(v & 1u);        return (uint8_t)((v >> 1) | (cin << 7)); }

static regs xtrap(uint8_t a, uint8_t x);
/* t_0/t_1/t_2/t_3 are all declared (non-static) in bzone_math.h - exposed
 * for direct lockstep coverage even though only t_0/t_1/t_3 are called from
 * outside this file (track()'s switch reaches t_2 as well). No extra
 * forward declaration needed/wanted here - matching the header avoids the
 * static-after-extern conflict (C4211) a `static` re-declaration would
 * cause. */

/*------------------------------------------------------------------------
 * WAIT ($5B80): "WAIT FOR 2901 MATH BOX & RETURN. LOW BYTE IN A."
 * The real ROM spins BIT MSTAT/BMI WAIT; the Mathbox seam model runs each
 * write's microcode to completion, so status is never busy - call the
 * seam once, do not spin (hw.h / PLAN.md).
 *----------------------------------------------------------------------*/
uint8_t bz_mb_wait(void)
{
    CYC(16);               /* L5B80-L5B88: BIT/BMI (not taken)/LDA/RTS */
    (void)hw_mb_status();  /* L5B80 BIT MSTAT / L5B83 BMI WAIT (never busy in the model) */
    return hw_mb_ylow();   /* L5B85 LDA MOUTLO */
}

/*------------------------------------------------------------------------
 * ROTPNT ($5B1F): rotate one point through the Mathbox (already primed by
 * the caller via ACOS/BSIN/XMATRX/YMATRX/YSTRT1), keep it if it passes the
 * view-frustum clip.  ENTRY y = destination index.  EXIT: new y.
 *----------------------------------------------------------------------*/
uint8_t rotpnt(uint8_t y)
{
    uint8_t a;
    unsigned c;

    CYC(6);      /* L5B1F JSR */
    a = bz_mb_wait();  /* L5B1F JSR WAIT: low byte of X' */
    c = 0;
    a = asl8(a, &c);                          /* L5B22 ASL */
    X2 = a;                                   /* L5B23 STA X2 */
    a = hw_mb_yhigh();                        /* L5B25 LDA MOUTHI */
    hw_mb_write(MB_RSTRT1, a);                /* L5B28 STA RSTRT1 - also starts SYM (Y' multiply), pipelined */
    CYC(15);                                  /* L5B22-L5B2B */
    if ((int8_t)a < 0) { CYC(7); return y; }  /* L5B2B BMI ROTPNT_20 (taken) / L5B7F RTS */
    CYC(4);                                   /* L5B2D ROL / L5B2E BMI */
    a = rol8(a, &c);                          /* L5B2D ROL (carry-in = ASL's carry-out, i.e. old bit7 of MOUTLO) */
    if ((int8_t)a < 0) { CYC(7); return y; }  /* L5B2E BMI ROTPNT_20 - too far, discard */
    CYC(9);                                   /* L5B30-L5B34 */
    X2_AT(1) = a;                             /* L5B30 STA X2+1 */
    a = (uint8_t)(a >> 1);                    /* L5B32 LSR */
    a = (uint8_t)(a >> 1);                    /* L5B33 LSR */
    if (a == 0) { CYC(7); return y; }         /* L5B34 BEQ ROTPNT_20 - too close, discard */
    CYC(7);                                   /* L5B36-L5B3A */
    a = X2_AT(1);                             /* L5B36 LDA X2+1 */
    if (a >= 0x7B) { CYC(7); return y; }      /* L5B38 CMP #$7B / L5B3A BCS ROTPNT_20 - too far, discard */
    CYC(6);                                   /* L5B3C JSR */

    a = bz_mb_wait();  /* L5B3C JSR WAIT: low byte of Y' */
    c = 0;
    a = asl8(a, &c);    /* L5B3F ASL */
    Y2 = a;             /* L5B40 STA Y2 */
    a = hw_mb_yhigh();  /* L5B42 LDA MOUTHI */
    a = rol8(a, &c);    /* L5B45 ROL */
    Y2_AT(1) = a;       /* L5B46 STA Y2+1 */
    CYC(14);            /* L5B3F-L5B46 */

    /* L5B48-L5B51: Y1/Y1+1 := |Y2/Y2+1| (16-bit absolute value, standard
     * negate-if-negative via the sign of the HIGH byte, which the low-byte
     * negate below is gated on too - both branches test the same BIT). */
    CYC((int8_t)Y2_AT(1) < 0 ? 29 : 21);  /* L5B48-L5B5D, both sign paths */
    if ((int8_t)Y2_AT(1) < 0) {
        unsigned cn = 0;
        uint8_t lo = adc6502((uint8_t)(~Y2), 0x01, &cn, NULL);        /* L5B4E CLC/EOR#$FF/ADC#$01 */
        Y1 = lo;                                                      /* L5B53 STA Y1 */
        uint8_t hi = adc6502((uint8_t)(~Y2_AT(1)), 0x00, &cn, NULL);  /* L5B59 EOR#$FF/L5B5B ADC#$00 */
        Y1_AT(1) = hi;                                                /* L5B5D STA Y1+1 */
    } else {
        Y1 = Y2;              /* L5B48 LDA Y2 (kept, BPL ROTPNT_10) */
        Y1_AT(1) = Y2_AT(1);  /* L5B55 LDA Y2+1 (kept, BPL ROTPNT_15) */
    }

    /* L5B5F-L5B67: discard if |Y| >= |X| (16-bit unsigned compare via
     * CMP/SBC/BCS - the ~90-degree field-of-view test). */
    {
        uint16_t ay = (uint16_t)Y1 | ((uint16_t)Y1_AT(1) << 8);
        uint16_t ax = (uint16_t)X2 | ((uint16_t)X2_AT(1) << 8);
        CYC(14);                             /* L5B5F-L5B67 */
        if (ay >= ax) { CYC(7); return y; }  /* L5B67 BCS ROTPNT_20 (taken) / RTS */
    }

    CYC(42);                      /* L5B69-L5B7F */
    PTBLX2_AT(y) = X2;            /* L5B69/L5B6B */
    PTBLX2_AT(y + 1) = X2_AT(1);  /* L5B6E/L5B70 */
    PTBLY2_AT(y) = Y2;            /* L5B73/L5B75 */
    PTBLY2_AT(y + 1) = Y2_AT(1);  /* L5B78/L5B7A */
    y = (uint8_t)(y + 2);         /* L5B7D/L5B7E INY INY */
    return y;                     /* L5B7F RTS */
}

/*------------------------------------------------------------------------
 * ROTATE ($5946): scan playfield/tank/robot/shell/saucer tables, translate
 * each centre point by the tank's own position (TPOSX/TPOSY -> TPOSXT/
 * TPOSYT, the Mathbox E/F operands) and rotate via ROTPNT, keeping only
 * objects in view.  No inputs; fills PTBLX2/PTBLY2/PTBLO2.
 * Atari's header: "SUBROUTINES: ROTPNT,EXPLDE" - both real, both called.
 *----------------------------------------------------------------------*/
void rotate(void)
{
    uint8_t y = 0;  /* L5946 LDY #$00 */
    uint8_t x;
    uint8_t a;
    unsigned c;
    regs r;

    CYC(48);                               /* L5946-L5963 (JSR SINXTR included) */
    hw_mb_write(MB_TPOSXT, TPOSX);         /* L5948/L594A */
    hw_mb_write(MB_TPOSXT1, TPOSX_AT(1));  /* L594D/L594F */
    hw_mb_write(MB_TPOSYT, TPOSY);         /* L5952/L5954 */
    hw_mb_write(MB_TPOSYT1, TPOSY_AT(1));  /* L5957/L5959 */

    a = LANGLE;        /* L595C LDA LANGLE */
    c = 0;             /* L595E CLC */
    a = rol8(a, &c);   /* L595F ROL */
    a = rol8(a, &c);   /* L5960 ROL */
    x = TANGLE;        /* L5961 LDX TANGLE */
    r = sinxtr(a, x);  /* L5963 JSR SINXTR */
    {
        /* L5966 EOR#$FF/L5968 ADC#$01: the ROM does NOT CLC before this ADC (unlike
         * every other 16-bit negate in this module - PNTPUT's ACOS negate at L5D64,
         * ABSTMP at L614F, T_1/T_3 at L68C6/L68D0 - which all explicitly CLC first).
         * The carry-in here is genuinely whatever SINXTR's call chain left on exit:
         * always 0 when SINXTR falls straight into SIN/SIN1 (SIN1's final ASL never
         * sets carry - its index is provably <= $40), but DATA-DEPENDENT whenever
         * the interpolation path (bit0 of the division set, ~half the time via
         * LANGLE) runs through XTRAP, whose own exit carry is the real carry out of
         * its last ADC.  Bug-for-bug fidelity requires threading the actual r.c
         * SINXTR returned, not assuming 0 - fixed here after the first compile pass
         * missed it (see NOTES_math.md "BSIN carry-chain fix"). */
        unsigned cn = r.c;
        uint8_t lo = adc6502((uint8_t)(~r.a), 0x01, &cn, NULL);  /* L5966 EOR#$FF/L5968 ADC#$01 */
        uint8_t hi = adc6502((uint8_t)(~r.x), 0x00, &cn, NULL);  /* L596E EOR#$FF/L5970 ADC#$00 */
        hw_mb_write(MB_BSIN, lo);                                /* L596A STA BSIN */
        hw_mb_write(MB_BSIN1, hi);                               /* L5972 STA BSIN+1 */
    }
    CYC(30);                     /* L5966-L5979 (JSR COSXTR included) */
    a = GARBAG;                  /* L5975 LDA GARBAG - THIS IS CHEATING (Atari's own comment) */
    x = TANGLE;                  /* L5977 LDX TANGLE */
    r = cosxtr(a, x);            /* L5979 JSR COSXTR */
    hw_mb_write(MB_ACOS, r.a);   /* L597C STA ACOS */
    hw_mb_write(MB_ACOS1, r.x);  /* L597F STX ACOS+1 */

    /* Integration fix (2026-09-18, NOTES_integration.md): the branch sense
     * here was inverted.  "BIT LOGO / BPL ROTATE_33" branches to ROTATE_33
     * when LOGO's bit 7 is CLEAR (BPL = "branch if plus", i.e. LOGO >= 0 as
     * signed); the LDX #$00/JMP R10S_ that follows is the FALL-THROUGH,
     * taken when the branch is NOT taken, i.e. when LOGO's bit 7 IS set
     * (LOGO < 0) - the opposite of what this file had.  Found by lockstep:
     * every ROTATE check that got past the tainted ones failed, always at
     * the same Mathbox write (XMATRX, I/O event #8) with the C side sending
     * $00 - because C always took the r10s_ branch instead of ROTATE_33's
     * TPOSX+2-based XMATRX write whenever LOGO>=0 (the common attract-mode
     * case), corrupting every later Mathbox read/write in the same check
     * once the I/O log desynced. */
    CYC(14);                 /* L597C-L5985 */
    if ((int8_t)LOGO < 0) {  /* L5982 BIT LOGO / L5985 BPL ROTATE_33 (fall through here when N set) */
        CYC(5);      /* L5987 LDX / L5989 JMP */
        x = 0;       /* L5987 LDX #$00 */
        goto r10s_;  /* L5989 JMP R10S_ */
    }
    CYC(1);  /* L5985 BPL taken */

    /* ROTATE_33 */
    CYC(5);                   /* L598C LDA / L598E BEQ */
    if (COLFLG_AT(2) != 0) {  /* L598C LDA COLFLG+2 / L598E BEQ ROTATE_1 */
        CYC(6);         /* L5990 JSR */
        y = explde(y);  /* L5990 JSR EXPLDE (game agent's; carries y like rotpnt) */
        CYC(3);         /* L5993 JMP */
        goto r4s_;      /* ROTATE_30: L5993 JMP R4S_ */
    }
    CYC(1);  /* L598E BEQ taken */

    /* ROTATE_1 */
    CYC(14);                      /* L5996-L599F */
    PTBLO2_AT(1) = TANGLE_AT(2);  /* L5996/L5998 */
    a = 0x16;                     /* L599B LDA #$16 */
    if ((int8_t)R2D3FL >= 0) {    /* L599D BIT R2D3FL / L599F BMI ROTATE_3 */
        CYC(8);                               /* L59A1 LDX / L59A3 JSR */
        x = 0x21;                             /* L59A1 LDX #$21 */
        if (!tr7chk()) { CYC(4); x = 0x02; }  /* L59A3 JSR TR7CHK / L59A6 BCS ROTATE_15 / L59A8 LDX #$02 */
        else CYC(3);                          /* L59A6 BCS taken */
        CYC(2);                               /* L59AA TXA */
        a = x;                                /* ROTATE_15: L59AA TXA */
    } else CYC(1);  /* L599F BMI taken */
    /* ROTATE_3 */
    CYC(38);                               /* L59AB-L59C2 (JSR ROTPNT included) */
    PTBLO2 = a;                            /* L59AB STA PTBLO2 */
    hw_mb_write(MB_XMATRX, TPOSX_AT(2));   /* L59AE/L59B0 */
    hw_mb_write(MB_XMATRX1, TPOSX_AT(3));  /* L59B3/L59B5 */
    hw_mb_write(MB_YMATRX, TPOSY_AT(2));   /* L59B8/L59BA */
    hw_mb_write(MB_YSTRT1, TPOSY_AT(3));   /* L59BD/L59BF - starts YHSM */
    y = rotpnt(y);                         /* L59C2 JSR ROTPNT */
    CYC(4);                                /* L59C5 TYA / L59C6 BEQ */
    if (y == 0) { CYC(4); goto r4s_; }     /* L59C5 TYA / L59C6 BEQ ROTATE_30(->R4S_): taken + L5993 JMP */

    CYC(50);                          /* L59C8-L59E8 */
    PTBLO2_AT(y + 1) = PTBLO2_AT(1);  /* L59C8/L59CB - orientation of treads */
    PTBLX2_AT(y) = PTBLX2;            /* L59CE/L59D1 - centre point */
    PTBLX2_AT(y + 1) = PTBLX2_AT(1);  /* L59D4/L59D7 */
    PTBLY2_AT(y) = PTBLY2;            /* L59DA/L59DD */
    PTBLY2_AT(y + 1) = PTBLY2_AT(1);  /* L59E0/L59E3 */

    if ((int8_t)R2D3FL < 0) {  /* L59E6 BIT R2D3FL / L59E8 BPL ROTATE_6 */
        CYC(12);                                               /* L59EA-L59F1, BNE taken */
        a = (uint8_t)(((TRDCTR_AT(2) & 0x07) + 0x24) & 0xFF);  /* L59EA/L59EC/L59EE/L59EF - EGGZAWST picture */
        goto rotate_4;                                         /* L59F1 BNE ROTATE_4 (always: 0x24+0..7 != 0) */
    }
    CYC(1);  /* L59E8 BPL taken */

    /* ROTATE_6 */
    CYC(8);                               /* L59F3 JSR / L59F6 BCS */
    if (tr7chk()) { CYC(1); goto r4s_; }  /* L59F3 JSR TR7CHK / L59F6 BCS R4S_ */
    {
        unsigned cf = 1;
        a = sbc6502(PTBLO2_AT(1), TANGLE, &cf, NULL);  /* L59F8 LDA PTBLO2+1 / L59FB SEC / L59FC SBC TANGLE */
        unsigned ca = 0;
        a = adc6502(a, 0x40, &ca, NULL);    /* L59FE CLC / L59FF ADC #$40 (90 degrees) */
        a = asl8(a, &ca);                   /* L5A01 ASL (carry = old bit7) */
        uint8_t obj = 0x04;                 /* L5A02 LDA #$04 */
        CYC(19 + 13 + (ca ? 2 : 1));        /* L59F8-L5A04, L5A06 or BCC taken, L5A07-L5A0E */
        if (ca) obj = (uint8_t)(obj << 1);  /* L5A04 BCC ROTATE_2 / L5A06 ASL */
        /* ROTATE_2 */
        TEMP1 = obj;  /* L5A07 STA TEMP1 */
        unsigned cadd = 0;
        a = adc6502((uint8_t)(TRDCTR_AT(2) & 0x03), TEMP1, &cadd, NULL);  /* L5A09/L5A0B/L5A0D/L5A0E */
    }
rotate_4:
    CYC(14);                                        /* L5A10-L5A17 */
    PTBLO2_AT(y) = a;                               /* L5A10 STA PTBLO2,Y */
    y = (uint8_t)(y + 2);                           /* L5A13/L5A14 INY INY */
    if ((int8_t)R2D3FL < 0) { CYC(1); goto r4s_; }  /* L5A15 BIT R2D3FL / L5A17 BMI R4S_ */

    CYC(32);                    /* L5A19-L5A2A (JSR COS included) */
    PTBLO2_AT(y) = 0x0D;        /* L5A19/L5A1B - radar antenna */
    PTBLO2_AT(y + 1) = RANGLE;  /* L5A1E/L5A20 */
    {
        unsigned cf = 1;
        NTHETA = sbc6502(TANGLE, TANGLE_AT(2), &cf, NULL);  /* L5A23 LDA TANGLE / L5A25 SEC / L5A26 SBC TANGLE+2 / L5A28 STA NTHETA */
    }
    r = cos_(NTHETA);  /* L5A2A JSR COS - centre of radar */
    {
        uint8_t xh = 0xFF;                           /* L5A2E LDX #$FF */
        a = (uint8_t)(r.x ^ 0xFF);                   /* L5A2D TXA / L5A30 EOR #$FF */
        CYC(8 + ((int8_t)a >= 0 ? 2 : 1));           /* L5A2D-L5A32, L5A34 INX or BMI taken */
        if ((int8_t)a >= 0) xh = (uint8_t)(xh + 1);  /* L5A32 BMI ROTATE_5 / L5A34 INX */
        /* ROTATE_5 */
        unsigned ca = 0;
        a = adc6502(a, 0x01, &ca, NULL);  /* L5A35 CLC / L5A36 ADC #$01 */
        CYC(6 + (ca ? 2 : 1) + 37 + 9);   /* L5A35-L5A38, INX or BCC taken, L5A3B-L5A4C, L5A4F LDA / L5A51 JSR SIN */
        if (ca) xh = (uint8_t)(xh + 1);   /* L5A38 BCC ROTATE_10 / L5A3A INX */
        /* ROTATE_10 */
        unsigned csh = 0;
        a = asl8(a, &csh);    /* L5A3B ASL */
        xh = rol8(xh, &csh);  /* L5A3E ROL (on the saved X) */
        /* ROTATE_11 */
        unsigned cadd = 0;
        uint8_t lo = adc6502(a, PTBLX2, &cadd, NULL);         /* L5A41 CLC / L5A42 ADC PTBLX2 */
        PTBLX2_AT(y) = lo;                                    /* L5A45 STA PTBLX2,Y */
        uint8_t hi = adc6502(xh, PTBLX2_AT(1), &cadd, NULL);  /* L5A48 TXA / L5A49 ADC PTBLX2+1 */
        PTBLX2_AT(y + 1) = hi;                                /* L5A4C STA PTBLX2+1,Y */
    }
    r = sin_(NTHETA);  /* L5A4F LDA NTHETA / L5A51 JSR SIN */
    {
        /* Integration fix (2026-09-18, NOTES_integration.md): L5A54 TXA
         * loads A from X = r.x (SIN's HIGH byte), then L5A55 ASL shifts
         * THAT - this file had `asl8(r.a, ...)`, shifting the LOW byte
         * instead.  Found by lockstep: PTBLY2_AT(y)/(y+1) (the radar-
         * antenna Y coordinate) mismatched by a small amount whenever this
         * ROTATE check wasn't tainted - the X-part just above (PTBLX2, via
         * COS) already does the equivalent shift on r.x correctly, which is
         * what exposed the asymmetry. */
        unsigned cs = 0;
        a = asl8(r.x, &cs);              /* L5A54 TXA (r.x) / L5A55 ASL */
        uint8_t xh = cs ? 0xFF : 0x00;   /* L5A56 LDX #$00 / L5A58 BCC ROTATE_21 / L5A5A LDX #$FF */
        CYC(8 + (cs ? 2 : 1) + 22 + 4);  /* L5A54-L5A58, LDX or BCC taken, L5A5C-L5A67, L5A6A/L5A6B INY INY */
        unsigned cadd = 0;
        uint8_t lo = adc6502(a, PTBLY2, &cadd, NULL);         /* L5A5C CLC / L5A5D ADC PTBLY2 */
        PTBLY2_AT(y) = lo;                                    /* L5A60 STA PTBLY2,Y */
        uint8_t hi = adc6502(xh, PTBLY2_AT(1), &cadd, NULL);  /* L5A63 TXA / L5A64 ADC PTBLY2+1 */
        PTBLY2_AT(y + 1) = hi;                                /* L5A67 STA PTBLY2+1,Y */
    }
    y = (uint8_t)(y + 2);  /* L5A6A/L5A6B INY INY */

r4s_:
    CYC(2);                               /* L5A6C LDX #$02 */
    for (x = 2;; x = (uint8_t)(x - 2)) {  /* L5A6C LDX #$02 ; R8S_: DEX DEX / BPL R5S_ */
        /* R5S_ */
        CYC(6);                   /* L5A6E LDA / L5A70 BEQ */
        if (FIRECT_AT(x) != 0) {  /* L5A6E LDA FIRECT,X / L5A70 BEQ R8S_ */
            CYC(16 + 6);                      /* L5A72-L5A79, L5A7C LDA / L5A7E BPL */
            PTBLO2_AT(y + 1) = TANGLE_AT(x);  /* L5A72/L5A74 */
            PTBLO2_AT(y) = 0x03;              /* L5A77/L5A79 */

            /* R5S__5 */
            a = FIRECT_AT(x);     /* L5A7C LDA FIRECT,X */
            if ((int8_t)a < 0) {  /* L5A7E BPL R6S_ */
                unsigned ca = 0;
                CYC(6);                           /* L5A80-L5A83 */
                a = adc6502(a, 0x0F, &ca, NULL);  /* L5A80 CLC / L5A81 ADC #$0F */
                if ((int8_t)a >= 0) {             /* L5A83 BMI R5S__10 */
                    CYC(9);             /* L5A85-L5A89, BEQ taken */
                    a = 0x00;           /* L5A85 LDA #$00 - end explosion */
                    FIRECT_AT(x) = a;   /* L5A87 STA FIRECT,X */
                    goto r8s_continue;  /* L5A89 BEQ R8S_ */
                }
                /* R5S__10 */
                CYC(1 + 24);       /* L5A83 BMI taken, L5A8B-L5A98 */
                FIRECT_AT(x) = a;  /* L5A8B STA FIRECT,X */
                {
                    unsigned ca2 = 0;
                    uint8_t v = adc6502((uint8_t)(~a), PTBLX2_AT(y + 1), &ca2, NULL);  /* L5A8D EOR#$FF/L5A8F CLC/L5A90 ADC PTBLX2+1,Y */
                    PTBLO2_AT(y + 1) = v;  /* L5A93 STA PTBLO2+1,Y */
                }
                PTBLO2_AT(y) = 0x0E;  /* L5A96/L5A98 */
            } else CYC(1);  /* L5A7E BPL taken */

            /* R6S_ */
            CYC(38);                                    /* L5A9B-L5AAF (JSR ROTPNT included) */
            hw_mb_write(MB_XMATRX, SHELLX_AT(x));       /* L5A9B/L5A9D */
            hw_mb_write(MB_XMATRX1, SHELLX_AT(x + 1));  /* L5AA0/L5AA2 */
            hw_mb_write(MB_YMATRX, SHELLY_AT(x));       /* L5AA5/L5AA7 */
            hw_mb_write(MB_YSTRT1, SHELLY_AT(x + 1));   /* L5AAA/L5AAC - starts YHSM */
            y = rotpnt(y);                              /* L5AAF JSR ROTPNT */
        } else CYC(1);  /* L5A70 BEQ taken */
        /* R8S_ */
    r8s_continue:
        CYC(x == 0 ? 6 : 7);  /* L5AB2 DEX / DEX / L5AB4 BPL (taken while X >= 0) */
        if (x == 0) break;    /* DEX DEX / BPL R5S_: x goes 2 -> 0 -> (would be $FE, BPL fails) */
    }

    CYC(5);             /* L5AB6 LDA / L5AB8 BEQ */
    if (SAUCER != 0) {  /* L5AB6 LDA SAUCER / L5AB8 BEQ R8S__40 */
        CYC(49);                                /* L5ABA-L5AD8 (JSR ROTPNT included) */
        PTBLO2_AT(y) = 0x20;                    /* L5ABA/L5ABC */
        PTBLO2_AT(y + 1) = SCANGL;              /* L5ABF/L5AC1 */
        hw_mb_write(MB_XMATRX, SAPOSX);         /* L5AC4/L5AC6 */
        hw_mb_write(MB_XMATRX1, SAPOSX_AT(1));  /* L5AC9/L5ACB */
        hw_mb_write(MB_YMATRX, SAPOSY);         /* L5ACE/L5AD0 */
        hw_mb_write(MB_YSTRT1, SAPOSY_AT(1));   /* L5AD3/L5AD5 - starts YHSM */
        y = rotpnt(y);                          /* L5AD8 JSR ROTPNT */
        CYC(8);                                 /* L5ADB-L5AE0 */
        if (PTBLO2_AT(y - 2) == 0x20) {         /* L5ADB LDA PTBLO2-2,Y / L5ADE CMP #$20 / L5AE0 BNE R8S__20 */
            CYC(8);         /* L5AE2-L5AE6, BNE taken */
            SAUCER = 0x81;  /* L5AE2/L5AE4 - sound on */
        } else {
            CYC(6);         /* L5AE0 BNE taken, L5AE8/L5AEA */
            SAUCER = 0x01;  /* R8S__20: L5AE8/L5AEA - sound off */
        }
    } else CYC(1);  /* L5AB8 BEQ taken */

    /* R8S__40 */
    CYC(2);  /* L5AEC LDX #$00 */
    x = 0;   /* L5AEC LDX #$00 */

r10s_:
    for (;;) {
        CYC(6);                                                          /* L5AEE LDA / L5AF1 BMI */
        a = cpu_rd((uint16_t)(A_PTBLO1 + x));                            /* L5AEE LDA PTBLO1,X */
        if ((int8_t)a < 0) { CYC(1); break; }                            /* L5AF1 BMI ROTUND (taken) */
        CYC(52);                                                         /* L5AF3-L5B14 (JSR ROTPNT included) */
        PTBLO2_AT(y) = a;                                                /* L5AF3 STA PTBLO2,Y */
        PTBLO2_AT(y + 1) = cpu_rd((uint16_t)(A_PTBLO1 + 1 + x));         /* L5AF6/L5AF9 */
        hw_mb_write(MB_XMATRX, cpu_rd((uint16_t)(BZ_PTBLX1 + x)));       /* L5AFC/L5AFF */
        hw_mb_write(MB_XMATRX1, cpu_rd((uint16_t)(BZ_PTBLX1 + 1 + x)));  /* L5B02/L5B05 */
        hw_mb_write(MB_YMATRX, cpu_rd((uint16_t)(BZ_PTBLY1 + x)));       /* L5B08/L5B0B */
        hw_mb_write(MB_YSTRT1, cpu_rd((uint16_t)(BZ_PTBLY1 + 1 + x)));   /* L5B0E/L5B11 - starts YHSM */
        y = rotpnt(y);                                                   /* L5B14 JSR ROTPNT */
        x = (uint8_t)(x + 2);                                            /* L5B17/L5B18 INX INX */
        CYC(x == 0 ? 6 : 7);                                             /* L5B17-L5B19, BNE taken while X != 0 */
        if (x == 0) break;                                               /* L5B19 BNE R10S_ (wrapped to 0 - table too long: matches ROM, 256/2=128 entries max) */
    }

    /* ROTUND */
    CYC(11);           /* L5B1B STA / L5B1E RTS */
    PTBLO2_AT(y) = a;  /* L5B1B STA PTBLO2,Y - end-of-table marker ($FF) */
}

/*------------------------------------------------------------------------
 * PNTPUT ($5D4A): project one object's 3-D point table into PNTTBL.
 * ENTRY x = index into PTBLO2/PTBLX2/PTBLY2.
 *----------------------------------------------------------------------*/
void pntput(uint8_t x)
{
    uint8_t y;
    regs r;

    /* Integration fix (2026-09-18, NOTES_integration.md): SAVE3 ($0018) is
     * a real RAM cell the ROM writes (STX/STY SAVE3) and leaves changed
     * after PNTPUT returns - it was a local C variable here instead of the
     * state_defs.h macro, so the C port's own g.ram[$18] never actually
     * changed, and every lockstep check spanning a PNTPUT call failed the
     * RAM compare at $0018 (found by lockstep: PNTPUT's own checks and
     * any outer check whose window included a PNTPUT call).  Fixed by
     * using the real cell (SAVE3, state_defs.h) throughout below, matching
     * SAVE1/SAVE2's own treatment three lines up. */

    CYC(23);    /* L5D4A-L5D56 (JSR SIN included) */
    SAVE1 = x;  /* L5D4A STX SAVE1 */
    {
        unsigned c = 1;
        NTHETA = (uint8_t)(sbc6502(TANGLE, PTBLO2_AT((unsigned)x + 1), &c, NULL) ^ 0x80);  /* L5D4C-L5D54 */
    }
    r = sin_(NTHETA);            /* L5D56 JSR SIN */
    CYC(17);                     /* L5D59-L5D61 (JSR COS included) */
    hw_mb_write(MB_BSIN, r.a);   /* L5D59 STA BSIN */
    hw_mb_write(MB_BSIN1, r.x);  /* L5D5C STX BSIN+1 */
    r = cos_(NTHETA);            /* L5D5F LDA NTHETA / L5D61 JSR COS */
    CYC(100);                    /* L5D64-L5DAB */
    {
        unsigned c = 0;
        uint8_t lo = adc6502((uint8_t)(~r.a), 0x01, &c, NULL);  /* L5D64 CLC/L5D65 EOR#$FF/L5D67 ADC#$01 */
        uint8_t hi = adc6502((uint8_t)(~r.x), 0x00, &c, NULL);  /* L5D6C TXA/L5D6D EOR#$FF/L5D6F ADC#$00 */
        hw_mb_write(MB_ACOS, lo);                               /* L5D69 STA ACOS */
        hw_mb_write(MB_ACOS1, hi);                              /* L5D71 STA ACOS+1 */
    }
    x = SAVE1;  /* L5D74 LDX SAVE1 */
    {
        unsigned idx = (unsigned)PTBLO2_AT(x) << 1;             /* L5D76 LDA PTBLO2,X / L5D79 ASL / L5D7A TAY */
        TBLPTR = cpu_rd((uint16_t)(A_OBJPNT + idx));            /* L5D7B/L5D7E */
        TBLPTR_AT(1) = cpu_rd((uint16_t)(A_OBJPNT + 1 + idx));  /* L5D80/L5D83 */
    }
    hw_mb_write(MB_TPOSXT, PTBLX2_AT(x));  /* L5D85/L5D88 */
    {
        uint8_t hi = PTBLX2_AT((unsigned)x + 1);  /* L5D8B LDA PTBLX2+1,X */
        hw_mb_write(MB_TPOSXT1, hi);              /* L5D8E STA TPOSXT+1 */
        DQUE = (uint8_t)(hi & 0xF0);              /* L5D91/L5D93 */
    }
    hw_mb_write(MB_TPOSYT, PTBLY2_AT(x));                 /* L5D95/L5D98 */
    hw_mb_write(MB_TPOSYT1, PTBLY2_AT((unsigned)x + 1));  /* L5D9B/L5D9E */

    x = 0;                         /* L5DA1 LDX #$00 */
    hw_mb_write(MB_ZLOW, x);       /* L5DA3 STX ZLOW - Z dividend low word := 0 */
    hw_mb_write(MB_ZHI, x);        /* L5DA6 STX ZHI */
    SAVE3 = 0;                     /* L5DA9 STX SAVE3 */
    SAVE3 = (uint8_t)(SAVE3 + 1);  /* L5DAB INC SAVE3 */

    for (;;) {
        /* PNTPUT_10 */
        CYC(53);    /* L5DAD-L5DC7 (JSR WAIT included) */
        y = SAVE3;  /* L5DAD LDY SAVE3 */
        uint16_t tbl = (uint16_t)TBLPTR | ((uint16_t)TBLPTR_AT(1) << 8);
        hw_mb_write(MB_XMATRX, cpu_rd((uint16_t)(tbl + y)));       y = (uint8_t)(y + 1);  /* L5DAF-L5DB4 */
        hw_mb_write(MB_XMATRX1, cpu_rd((uint16_t)(tbl + y)));      y = (uint8_t)(y + 1);  /* L5DB5-L5DBA */
        hw_mb_write(MB_YMATRX, cpu_rd((uint16_t)(tbl + y)));       y = (uint8_t)(y + 1);  /* L5DBB-L5DC0 */
        hw_mb_write(MB_YSTRT2, cpu_rd((uint16_t)(tbl + y)));       y = (uint8_t)(y + 1);  /* L5DC1-L5DC6 - starts YHSP */

        {
            uint8_t a = bz_mb_wait();  /* L5DC7 JSR WAIT */
            unsigned c = 0;
            uint8_t lo = adc6502((uint8_t)(~a), 0x01, &c, NULL);              /* L5DCA CLC/L5DCB EOR#$FF/L5DCD ADC#$01 */
            PNTTBL_AT(x) = lo;                                                /* L5DCF STA PNTTBL,X */
            uint8_t hi = adc6502((uint8_t)(~hw_mb_yhigh()), 0x00, &c, NULL);  /* L5DD1 LDA MOUTHI/L5DD4 EOR#$FF/L5DD6 ADC#$00 */
            PNTTBL_AT((unsigned)x + 1) = hi;                                  /* L5DD8 STA PNTTBL+1,X */
        }

        CYC(54);                                                          /* L5DCA-L5DE8 (JSR KLUDGE included) */
        DDEND = cpu_rd((uint16_t)(tbl + y));       y = (uint8_t)(y + 1);  /* L5DDA-L5DDE */
        {
            uint8_t zbyte = cpu_rd((uint16_t)(tbl + y));  /* L5DDF LDA (TBLPTR),Y */
            y = (uint8_t)(y + 1);                         /* L5DE1 INY */
            SAVE3 = y;                                    /* L5DE2 STY SAVE3 */
            DDEND_AT(1) = zbyte;                          /* L5DE4 STA DDEND+1 */
        }
        SAVE2 = x;  /* L5DE6 STX SAVE2 */
        kludge();   /* L5DE8 JSR KLUDGE */
        CYC(6);     /* L5DEB JSR */
        {
            uint8_t a = bz_mb_wait();                          /* L5DEB JSR WAIT */
            x = SAVE2;                                   /* L5DEE LDX SAVE2 */
            PNTTBL_AT((unsigned)x + 2) = a;              /* L5DF0 STA PNTTBL+2,X */
            PNTTBL_AT((unsigned)x + 3) = hw_mb_yhigh();  /* L5DF2/L5DF5 */
        }

        y = SAVE3;                                /* L5DF7 TYA/L5DF8 LDY#$00 (y already holds SAVE3's value from above) */
        CYC(26);                                  /* L5DEE-L5DFC */
        if (y >= cpu_rd(tbl)) { CYC(7); break; }  /* L5DFA CMP (TBLPTR),Y (Y=0: the object's point count) / L5DFC BCS PNTPUT_20 (taken) + RTS */
        x = (uint8_t)(x + 4);                     /* L5DFE-L5E01 INX x4 */
        CYC(10);                                  /* L5DFE-L5E02 */
        if (x == 0) { CYC(6); break; }            /* L5E02 BNE PNTPUT_10 / L5E04 RTS */
        CYC(1);                                   /* BNE taken */
    }
    /* PNTPUT_20: RTS */
}

/*------------------------------------------------------------------------
 * KLUDGE ($5E05): explosion-piece Z offset + BOUNCE, then start the Z/X'
 * divide (SZXD).  No inputs (reads SAVE1/PTBLO2/DDEND/EXPOSZ/BOUNCE).
 *----------------------------------------------------------------------*/
void kludge(void)
{
    uint8_t x = SAVE1;  /* L5E05 LDX SAVE1 */

    CYC(11);                              /* L5E05-L5E0C */
    CYC((PTBLO2_AT(x) & 0x10) ? 30 : 1);  /* L5E0E-L5E21, or BEQ taken */
    CYC(5);                               /* L5E23 LDA BOUNCE / L5E25 BEQ */
    CYC(BOUNCE != 0 ? 20 : 11);           /* L5E27-L5E33 (JMP), or BEQ taken + L5E36-L5E3B */
    CYC(14);                              /* L5E3D-L5E43: STA / STA / RTS */
    if (PTBLO2_AT(x) & 0x10) {            /* L5E07/L5E0A/L5E0C BEQ KLUDGE_20 */
        unsigned idx = (unsigned)(PTBLO2_AT(x) & 0x07) << 1;              /* L5E0E/L5E11/L5E13 ASL */
        unsigned c = 0;                                                   /* ASL's carry-out is always 0: the piece # is 0-7, bit7 never set */
        uint8_t lo = adc6502(EXPOSZ_AT(idx), DDEND, &c, NULL);            /* L5E15/L5E18 */
        DDEND = lo;                                                       /* L5E1A */
        uint8_t hi = adc6502(EXPOSZ_AT(idx + 1), DDEND_AT(1), &c, NULL);  /* L5E1C/L5E1F */
        DDEND_AT(1) = hi;                                                 /* L5E21 */
    }

    /* KLUDGE_20 */
    uint8_t divoutHi;
    if (BOUNCE != 0) {  /* L5E23/L5E25 BEQ KLUDGE_30 */
        unsigned c = 1;                                   /* L5E27 SEC */
        uint8_t lo = sbc6502(DDEND, BOUNCE, &c, NULL);    /* L5E28/L5E2A */
        hw_mb_write(MB_DIVOUT, lo);                       /* L5E2C STA DIVOUT */
        divoutHi = sbc6502(DDEND_AT(1), 0x00, &c, NULL);  /* L5E2F/L5E31 */
    } else {
        /* KLUDGE_30 */
        hw_mb_write(MB_DIVOUT, DDEND);  /* L5E36/L5E38 */
        divoutHi = DDEND_AT(1);         /* L5E3B LDA DDEND+1 */
    }
    /* KLUDGE_40 */
    hw_mb_write(MB_DIVOUT1, divoutHi);  /* L5E3D STA DIVOUT+1 */
    hw_mb_write(MB_ZDIV, divoutHi);     /* L5E40 STA ZDIV - same A value; starts SZXD */
}

/*------------------------------------------------------------------------
 * BOUND ($5E44): halve BOUNCE toward zero.
 *----------------------------------------------------------------------*/
void bound(void)
{
    CYC(BOUNCE != 0 ? 16 : 12);  /* L5E44-L5E4A: LDA / BEQ / (LSR) / RTS */
    if (BOUNCE != 0)             /* L5E44/L5E46 BEQ BOUND_20 */
        BOUNCE = (uint8_t)(BOUNCE >> 1);  /* L5E48 LSR BOUNCE */
}

/*------------------------------------------------------------------------
 * COS/SIN/SIN1 ($5E4B/$5E4E/$5E63)
 *----------------------------------------------------------------------*/
regs cos_(uint8_t a)
{
    unsigned c = 0;
    CYC(4);                          /* L5E4B CLC / L5E4C ADC */
    a = adc6502(a, 0x40, &c, NULL);  /* L5E4B CLC / L5E4C ADC #$40: COS(a) = SIN(a+$40) */
    return sin_(a);                  /* falls into SIN */
}

regs sin_(uint8_t a)
{
    if ((int8_t)a >= 0) {  /* L5E4E BPL SIN1 */
        CYC(3);  /* BPL taken */
        return sin1(a);
    }

    CYC(10);                             /* L5E4E BPL / L5E50 AND / L5E52 JSR */
    regs r = sin1((uint8_t)(a & 0x7F));  /* L5E50 AND #$7F / L5E52 JSR SIN1 */
    CYC(27);                             /* L5E55-L5E62 */
    unsigned c = 0;
    uint8_t lo = adc6502((uint8_t)(~r.a), 0x01, &c, NULL);  /* L5E55 EOR#$FF/L5E57 CLC/L5E58 ADC#$01 */
    uint8_t hi = adc6502((uint8_t)(~r.x), 0x00, &c, NULL);  /* L5E5C EOR#$FF/L5E5E ADC#$00 */
    r.a = lo;
    r.x = hi;
    r.c = (uint8_t)c;      /* exit carry = carry out of the hi-byte ADC above (data-dependent: 1 iff
                             * the pre-negate 16-bit value was exactly 0) - SIN omits its own CLC before
                             * a caller's next ADC, so this must be the real value, not a fixed 0 (see
                             * ROTATE's BSIN negate, which relies on exactly this). */
    return r;  /* L5E62 RTS */
}

regs sin1(uint8_t a)
{
    regs r;

    CYC(4 + (a >= 0x41 ? 4 : 1) + 27);  /* L5E63-L5E65, EOR/ADC or BCC taken, L5E6B-L5E76 */
    if (a >= 0x41) {                    /* L5E63 CMP #$41 / L5E65 BCC SIN1_10 */
        /* Carry-in to the ADC below is always 1 here: CMP #$41 set carry
         * because a >= 0x41, and nothing between it and the ADC touches
         * carry. */
        a = (uint8_t)((a ^ 0x7F) + 1u);  /* L5E67 EOR#$7F / L5E69 ADC#$00 (carry-in 1) */
    }
    /* SIN1_10 */
    {
        unsigned cs = 0;
        uint8_t idxb = asl8(a, &cs);                      /* L5E6B ASL / L5E6C TAX - idxb = a*2, fits in
                                                             * uint8_t since a is always <= $40 here (the
                                                             * direct 0..$40 range and the folded $01..$3F
                                                             * range both stay <= $40), so ASL's carry-out
                                                             * (old bit7 of a) is UNCONDITIONALLY 0 - a
                                                             * proven property of the fold, not a guess. */
        unsigned idx = idxb;
        r.a = cpu_rd((uint16_t)(BZ_SIN1_99 + idx));      /* L5E6D LDA $5E77,X */
        r.x = cpu_rd((uint16_t)(BZ_SIN1_99 + 1 + idx));  /* L5E71 LDA $5E78,X */
        r.c = (uint8_t)cs;                                   /* SIN1's exit carry = 0, always (see above) -
                                                                * recorded for callers chaining off it
                                                                * without their own CLC (SIN's direct-
                                                                * return path; ROTATE via SINXTR - see
                                                                * NOTES_math.md "BSIN carry-chain fix"). */
    }
    return r;  /* L5E76 RTS */
}

/*------------------------------------------------------------------------
 * COSXTR/SINXTR/XTRAP ($5EF9/$5F17/$5F32)
 *----------------------------------------------------------------------*/
regs cosxtr(uint8_t a, uint8_t x)
{
    CYC(7);                   /* L5EF9-L5EFD */
    a = (uint8_t)(a & 0x01);  /* L5EF9 AND #$01 */
    GARBAG = a;               /* L5EFB STA GARBAG */
    if (a == 0) {             /* L5EFD BNE COSXTR_10 */
        CYC(5);          /* L5EFF TXA / L5F00 JMP */
        return cos_(x);  /* L5EFF TXA / L5F00 JMP COS */
    }

    /* COSXTR_10 */
    CYC(1 + 11);                   /* BNE taken, L5F03-L5F06 */
    TEMP3 = x;                     /* L5F03 TXA / L5F04 STA TEMP3 */
    regs r0 = cos_(x);             /* L5F06 JSR COS */
    CYC(20);                       /* L5F09-L5F11 */
    TEMP1 = r0.a;                  /* L5F09 STA TEMP1 */
    TEMP1_AT(1) = r0.x;            /* L5F0B STX TEMP1+1 */
    TEMP3 = (uint8_t)(TEMP3 + 1);  /* L5F0D INC TEMP3 */
    regs r1 = cos_(TEMP3);         /* L5F0F LDA TEMP3 / L5F11 JSR COS */
    CYC(3);                        /* L5F14 JMP */
    return xtrap(r1.a, r1.x);      /* L5F14 JMP XTRAP */
}

regs sinxtr(uint8_t a, uint8_t x)
{
    CYC(7);                   /* L5F17-L5F1B */
    a = (uint8_t)(a & 0x01);  /* L5F17 AND #$01 */
    GARBAG = a;               /* L5F19 STA GARBAG */
    if (a == 0) {             /* L5F1B BNE SINXTR_10 */
        CYC(5);          /* L5F1D TXA / L5F1E JMP */
        return sin_(x);  /* L5F1D TXA / L5F1E JMP SIN */
    }

    /* SINXTR_10 */
    CYC(1 + 11);                   /* BNE taken, L5F21-L5F24 */
    TEMP3 = x;                     /* L5F21 TXA / L5F22 STA TEMP3 */
    regs r0 = sin_(x);             /* L5F24 JSR SIN */
    CYC(20);                       /* L5F27-L5F2F */
    TEMP1 = r0.a;                  /* L5F27 STA TEMP1 */
    TEMP1_AT(1) = r0.x;            /* L5F29 STX TEMP1+1 */
    TEMP3 = (uint8_t)(TEMP3 + 1);  /* L5F2B INC TEMP3 */
    regs r1 = sin_(TEMP3);         /* L5F2D LDA TEMP3 / L5F2F JSR SIN */
    return xtrap(r1.a, r1.x);      /* falls into XTRAP */
}

/* XTRAP ($5F32, internal only - reached by JMP-tail from COSXTR/SINXTR,
 * never JSR'd; CALLING_NOTES.md section 3).  In: a,x = COS/SIN(angle+1);
 * reads TEMP1/TEMP1+1 = COS/SIN(angle) (set by the caller).  Linearly
 * interpolates by the division fraction (halving the difference, signed
 * arithmetic shift via the CMP #$80/ROR trick). */
static regs xtrap(uint8_t a, uint8_t x)
{
    regs r;
    CYC(54);                                             /* L5F32-L5F4D */
    unsigned c = 1;                                      /* L5F32 SEC */
    uint8_t diffLo = sbc6502(a, TEMP1, &c, NULL);        /* L5F33 SBC TEMP1 */
    uint8_t diffHi = sbc6502(x, TEMP1_AT(1), &c, NULL);  /* L5F37 TXA / L5F38 SBC TEMP1+1 */

    unsigned c2 = (diffHi >= 0x80) ? 1u : 0u;  /* L5F3A CMP #$80 (sets carry = sign bit, A unchanged) */
    uint8_t hiShifted = ror8(diffHi, &c2);     /* L5F3C ROR (on A = diffHi) */
    uint8_t loShifted = ror8(diffLo, &c2);     /* L5F3D ROR TEMP2 (in memory) */
    TEMP2 = loShifted;                         /* (STA TEMP2 already folded into the LSR/ROR chain above) */
    TEMP2_AT(1) = hiShifted;                   /* L5F3F STA TEMP2+1 */

    /* XTRAP_10 */
    unsigned ca = 0;
    uint8_t lo = adc6502(TEMP1, TEMP2, &ca, NULL);              /* L5F41 LDA TEMP1 / L5F43 CLC / L5F44 ADC TEMP2 */
    uint8_t hi = adc6502(TEMP1_AT(1), TEMP2_AT(1), &ca, NULL);  /* L5F47 LDA TEMP1+1 / L5F49 ADC TEMP2+1 */
    r.a = lo;                                                   /* L5F46 PHA .. L5F4C PLA */
    r.x = hi;                                                   /* L5F4B TAX */
    r.c = (uint8_t)ca;   /* exit carry = carry out of the final (hi-byte) ADC above - fully
                           * data-dependent (interpolated result), and NOT reset by XTRAP's own
                           * RTS.  COSXTR/SINXTR's callers that chain off this without their own
                           * CLC must see the real value (see ROTATE's BSIN negate, the only such
                           * caller in this module). */
    return r;  /* L5F4D RTS */
}

/*------------------------------------------------------------------------
 * ABSTMP ($6149): TEMP1/TEMP1+1 := |TEMP1/TEMP1+1|.
 *----------------------------------------------------------------------*/
void abstmp(void)
{
    CYC(5 + ((int8_t)TEMP1_AT(1) < 0 ? 22 : 1) + 6);  /* L6149-L614B, negate or BPL taken, L615E RTS */
    if ((int8_t)TEMP1_AT(1) < 0) {                    /* L6149 BIT TEMP1+1 / L614B BPL ABSTMP_10 */
        unsigned c = 0;
        uint8_t lo = adc6502((uint8_t)(~TEMP1), 0x01, &c, NULL);        /* L614D LDA TEMP1/L614F CLC/L6150 EOR#$FF/L6152 ADC#$01 */
        TEMP1 = lo;                                                     /* L6154 STA TEMP1 */
        uint8_t hi = adc6502((uint8_t)(~TEMP1_AT(1)), 0x00, &c, NULL);  /* L6156 LDA TEMP1+1/L6158 EOR#$FF/L615A ADC#$00 */
        TEMP1_AT(1) = hi;                                               /* L615C STA TEMP1+1 */
    }
}

/*------------------------------------------------------------------------
 * TRACK ($6810): angle from the robot tank to the player tank.
 *----------------------------------------------------------------------*/
uint8_t track(void)
{
    uint8_t y = 0;  /* L6810 LDY #$00 */
    unsigned c;
    uint8_t hi;
    uint8_t angle;

    CYC(24);                                           /* L6810-L681F */
    c = 1;                                             /* L6812 SEC */
    TEMP1 = sbc6502(TPOSX, TPOSX_AT(2), &c, NULL);     /* L6813/L6815/L6817 */
    hi = sbc6502(TPOSX_AT(1), TPOSX_AT(3), &c, NULL);  /* L6819/L681B */
    TEMP1_AT(1) = hi;                                  /* L681D */
    if ((int8_t)hi < 0) {                              /* L681F BPL TRACK_10 */
        CYC(6);                /* L6821 JSR */
        abstmp();              /* L6821 */
        CYC(2);                /* L6824 INY */
        y = (uint8_t)(y + 1);  /* L6824 INY */
    } else CYC(1);  /* L681F BPL taken */
    CYC(37);        /* L6825-L683C */

    /* TRACK_10 */
    TEMP2 = TEMP1;              /* L6825/L6827 */
    TEMP2_AT(1) = TEMP1_AT(1);  /* L6829/L682B */
    /* L682D: LDA TEMP1+1 - reloads a value nothing consumes (dead load,
     * next flag-setting instruction is the SEC below); emitted as nothing. */

    c = 1;                                             /* L682F SEC */
    TEMP1 = sbc6502(TPOSY, TPOSY_AT(2), &c, NULL);     /* L6830/L6832/L6834 */
    hi = sbc6502(TPOSY_AT(1), TPOSY_AT(3), &c, NULL);  /* L6836/L6838 */
    TEMP1_AT(1) = hi;                                  /* L683A */
    if ((int8_t)hi < 0) {                              /* L683C BPL TRACK_20 */
        CYC(6);                                        /* L683E JSR */
        abstmp();                                      /* L683E */
        CYC(y == 0 ? 7 : 9);                           /* L6841 TYA / BEQ (taken) / L6847 LDY #3, or L6844 INY / BNE (taken) */
        y = (y == 0) ? (uint8_t)3 : (uint8_t)(y + 1);  /* L6841 TYA/L6842 BEQ TRACK_12(LDY#3)/L6844 INY */
    } else CYC(1);  /* L683C BPL taken */

    /* TRACK_20 */
    if (TEMP1 == TEMP2 && TEMP1_AT(1) == TEMP2_AT(1)) {  /* L6849-L6853: full 16-bit equality */
        CYC(22);        /* L6849-L684D, L684F-L6853 (BEQ taken), L6858 LDA / L685A BNE (taken) */
        angle = 0x20;   /* TRACK_22: L6858 LDA #$20 (45 degrees) */
        goto track_45;  /* L685A BNE TRACK_45 */
    }
    CYC(TEMP1 != TEMP2 ? 9 : 21);    /* L6849-L684D (BNE taken), or L6849-L6856 (SEC / BCS taken) */
    c = (TEMP1 >= TEMP2) ? 1u : 0u;  /* L684B CMP TEMP2 (or L6855 SEC when low bytes tie - same result) */

    /* TRACK_25 */
    {
        uint8_t hidiff = sbc6502(TEMP1_AT(1), TEMP2_AT(1), &c, NULL);  /* L685C/L685E */
        (void)hidiff;
        int did_yx = (c == 0);                 /* L6860 PHP (saved) / L6861 BCC TRACK_30 */
        CYC(11 + (did_yx ? 1 + 32 : 35) + 6);  /* L685C-L6861, TRACK_30 L687D-L6891 or TRACK_28 L6863-L687A (JMP), L6894 JSR WAIT */

        if (did_yx) {
            /* TRACK_30: compute Y/X */
            hw_mb_write(MB_XPL, TEMP2);            /* L687D/L687F */
            hw_mb_write(MB_XPH, TEMP2_AT(1));      /* L6882/L6884 */
            hw_mb_write(MB_DIVOUT, TEMP1);         /* L6887/L6889 */
            hw_mb_write(MB_DIVOUT1, TEMP1_AT(1));  /* L688C/L688E */
            hw_mb_write(MB_ZDIV, TEMP1_AT(1));     /* L6891 STA ZDIV (same A) */
        } else {
            /* TRACK_28: compute X/Y */
            hw_mb_write(MB_XPL, TEMP1);            /* L6863/L6865 */
            hw_mb_write(MB_XPH, TEMP1_AT(1));      /* L6868/L686A */
            hw_mb_write(MB_DIVOUT, TEMP2);         /* L686D/L686F */
            hw_mb_write(MB_DIVOUT1, TEMP2_AT(1));  /* L6872/L6874 */
            hw_mb_write(MB_ZDIV, TEMP2_AT(1));     /* L6877 STA ZDIV */
        }

        /* TRACK_40 */
        {
            uint8_t lo = bz_mb_wait();          /* L6894 JSR WAIT */
            TEMP3 = lo;                   /* L6897 STA TEMP3 */
            uint8_t mhi = hw_mb_yhigh();  /* L6899 LDA MOUTHI */
            unsigned sc = 0;
            mhi = lsr8(mhi, &sc);      /* L689C LSR */
            TEMP3 = ror8(TEMP3, &sc);  /* L689D ROR TEMP3 */
            mhi = lsr8(mhi, &sc);      /* L689F LSR */
            TEMP3 = ror8(TEMP3, &sc);  /* L68A0 ROR TEMP3 */
            (void)mhi;
        }
        angle = cpu_rd((uint16_t)(A_ARCTAN + TEMP3));  /* L68A2 LDX TEMP3 / L68A4 LDA ARCTAN,X */

        CYC(34 + (did_yx ? 1 : 8));  /* L6897-L68A8, BCC taken or L68AA-L68AE */
        if (!did_yx) {               /* L68A7 PLP / L68A8 BCC TRACK_45 */
            TEMP3 = angle;                            /* L68AA STA TEMP3 */
            unsigned c2 = 1;                          /* carry-in from the restored PLP is 1 here (the not-BCC path) */
            angle = sbc6502(0x40, TEMP3, &c2, NULL);  /* L68AC LDA #$40 / L68AE SBC TEMP3 */
        }
    }

track_45:
    /* TRACK_45: TTAB dispatch ($68BE, push-address-then-RTS) modelled as a
     * switch on the quadrant y (0-3) -> T_0..T_3. */
    CYC(30);  /* L68B0-L68BD: TAX .. PHA / TXA / RTS */
    switch (y) {
    case 0:  return t_0(angle);
    case 1:  return t_1(angle);
    case 2:  return t_2(angle);
    default: return t_3(angle);  /* y == 3 */
    }
}

/*------------------------------------------------------------------------
 * T_0/T_1/T_2/T_3 ($68D5/$68C6/$68CB/$68D0): TTAB quadrant fix-up.
 *----------------------------------------------------------------------*/
uint8_t t_0(uint8_t a)
{
    CYC(6);    /* L68D5 RTS */
    return a;  /* L68D5 RTS */
}

uint8_t t_2(uint8_t a)
{
    CYC(5);                   /* L68CB EOR / L68CD JMP */
    a = (uint8_t)(a ^ 0x80);  /* L68CB EOR #$80 */
    return t_0(a);            /* L68CD JMP T_0 */
}

uint8_t t_1(uint8_t a)
{
    unsigned c = 0;
    CYC(6);                                      /* L68C6-L68C9 */
    a = adc6502((uint8_t)(~a), 0x01, &c, NULL);  /* L68C6 CLC/L68C7 EOR#$FF/L68C9 ADC#$01 */
    return t_2(a);                               /* falls into T_2 */
}

uint8_t t_3(uint8_t a)
{
    unsigned c = 0;
    CYC(6);                                      /* L68D0-L68D3 */
    a = adc6502((uint8_t)(~a), 0x01, &c, NULL);  /* L68D0 CLC/L68D1 EOR#$FF/L68D3 ADC#$01 */
    return t_0(a);                               /* falls into T_0 */
}
