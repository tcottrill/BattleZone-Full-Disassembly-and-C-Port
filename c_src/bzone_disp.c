/* bzone_disp.c - BZONE display module (agent "display"), BZONE.MAC.
 *
 * Routines translated (see NOTES_disp.md for the full status table and every
 * judgement call): BIGWND, WNSHLD, BATTLE(+BATTLE_10..30), BATINT, CKSCOR,
 * HISCRE, LETCHK, DISTBL, DSPALP, FINISH, MOUNTS(+MOUNTS_50), VOLCNO,
 * DRAW/DTAB/D_DOT/D_SBRT/D_LABS/D_BVCT/D_VCTR/D_SPCL/D_SLIM/DOVECT/D_NEXT,
 * DRADAR, SHUPDT, MSGS/ENGLISH/GERMAN/FRENCH/SPANSH/MSTART, INFO,
 * DIGT2S/DIGTYS/DIGITS.
 *
 * Every routine builds the AVG display list: real vector words go out
 * through VGUT (game.h - vgcntr/vgvtr/vgstat/vgadd2/vgadd/vgscal/vgsca1/
 * vgdot/vgjsrl/vgvtr2/vghalt), and a few places store directly through
 * (VGLIST),Y (DISTBL's tank-count loop, INFO's life-tank loop, MSTART's
 * character loop) - reproduced byte for byte, never routed through a VGUT
 * call that isn't what the ROM actually used.
 *
 * IMPORTANT DISCOVERED INVARIANT (see NOTES_disp.md section 2): VGUT's
 * VGVTR/VGDOT/VGVTR1/VGVTR2/VGVCTR family always leaves the real 6502 Y
 * register at exactly 3 when it returns (VGVCTR always sequences Y through
 * 0,1,2 and VGWAI1's INY makes 3, unconditionally - no branch in that chain
 * ever skips out early). game.h's vgvtr()/vgdot()/etc. are void, so they
 * don't expose this. BIGWND is the one call site in this module that relies
 * on the leftover Y (its "LDA #$02 / JMP VGSTAT" right after "JSR VGVTR" -
 * with no intervening LDY - inherits Y=3 as VGSTAT's brightness parameter);
 * translated as a literal vgstat(0x02, 3) with this comment, not a bug.
 *
 * Temporary externs (TRANSLATING.md): bzone_math.h, bzone_game.h, bzsoun.h
 * now exist (M3/M4/M6 headers landed) - sin/cos/track/wait/objobj/robchk/
 * tankck/sndon come from them now, real signatures.  Only b_end/bz_main
 * remain temporary: bzone_game.h's main_pass() owns B_END/MAIN's bodies but
 * doesn't expose them as separately callable functions yet - registered as
 * "(proposed by display)" in SIGNATURES.md's game section; remove this
 * block once bzone_game.h grows them.
 */
#include "state.h"
#include "game.h"
#include "hw.h"
#include "bzone_math.h"
#include "bzsoun.h"
#include "bzone_game.h"
#include "bzone_disp.h"

/* ---------------------------------------------------------------------------
 * TEMPORARY: cross-module prototypes not yet in bzone_game.h (see above).
 * --------------------------------------------------------------------------- */
extern void    b_end(void);    /* B_END  $5159 (mainline, bzone_game.c) */
extern void    bz_main(void);  /* MAIN   $5000 (mainline, bzone_game.c) - "MAIN" renamed, see above */

/* ---------------------------------------------------------------------------
 * ROM addresses with no generated state_defs.h symbol: internal BZONE.MAC
 * labels, not .alias equates in ../disasm/bzone_defines.asm, so tools/gen_state.py
 * never emits them. Addresses confirmed against ../disasm/bzone_program_rom.asm
 * (line numbers noted). Never hand-transcribe the DATA at these addresses -
 * always read it back through cpu_rd().
 * --------------------------------------------------------------------------- */
#define A_OBJTBL 0x7472  /* OBJTBL: object# -> edge-list pointer table, .asm:5624 */
#define A_ENGMSG 0x6D93  /* ENGMSG: English message-index -> text pointer table, .asm:5189 */
#define A_FRNMSG 0x6F3A  /* FRNMSG: French  "                                    , .asm:5301 */
#define A_SPNMSG 0x70F6  /* SPNMSG: Spanish "                                    , .asm:5412 */
#define A_GRMMSG 0x7288  /* GRMMSG: German  "                                    , .asm:5509 */

/*==============================================================================
 * BIGWND ($51D9) - C OPEN WINDOW (comment at the call). callers: MAIN family.
 *============================================================================*/
void bigwnd(void)
{
    CYC(6);                   /* L51D9 JSR */
    vgcntr();                 /* L51D9 JSR VGCNTR - C SET UPPER WINDOW */
    CYC(12);                  /* L51DC-L51E1 (JSR included) */
    vgvtr(0x7F, 0x7F, 0x00);  /* L51DC LDY#0 / L51DE LDA#$7F / L51E0 TAX / L51E1 JSR VGVTR */
    CYC(5);                   /* L51E4 LDA / L51E6 JMP */
    /* L51E4 LDA #$02 / L51E6 JMP VGSTAT - tail call; Y is NOT reloaded, so
     * VGSTAT's brightness parameter is whatever VGVTR's call chain left the
     * real Y register at - always 3 (see file header invariant). */
    vgstat(0x02, 0x03);
}

/*==============================================================================
 * WNSHLD ($51E9) - windshield-crack overlay; see bzone_disp.h.
 *============================================================================*/
/* Integration fix (2026-09-18, NOTES_integration.md "CRITICAL fix... open
 * item 1 resolved"): wnshld()'s own tail calls into ckscor()/b_end() used
 * to be real, unbounded C calls - and b_end()/bz_main() call each other
 * forever by design, so replaying wnshld() for real (e.g. under lockstep)
 * stack-overflowed the moment a real game reached this routine (CRACK
 * != 0).  Fixed with the trampoline bzone_game.h documents: wnshld_body()
 * returns a pass_signal instead of tail-calling; wnshld() (below) just
 * runs it once and discards the signal, matching every existing caller's
 * signature and stopping exactly where the ROM would jump elsewhere. */
void wnshld(void) { (void)wnshld_body(); }

pass_signal wnshld_body(void)
{
    uint8_t y, x, a;

    CYC(6);                   /* L51E9 LDY / L51EB STY */
    y = 0x00;                 /* L51E9 LDY #$00 */
    hw_pokey_write(0x07, y);  /* L51EB STY CHAN4V ($1827, offset 7 from POKEY base $1820) */

    for (;;) {  /* WNSHLD_10 */
        CYC(17);                                   /* L51EE-L51F6 (JSR included) */
        a = cpu_rd((uint16_t)(A_CRACKS + y));      /* L51EE LDA CRACKS,Y */
        x = cpu_rd((uint16_t)(A_CRACKS + y + 1));  /* L51F1 LDX CRACKS+1,Y */
        TEMP1 = y;                                 /* L51F4 STY TEMP1 */
        vgadd2(a, x);                              /* L51F6 JSR VGADD2 */
        y = TEMP1;                                 /* L51F9 LDY TEMP1 */
        y++; y++;                                  /* L51FB/L51FC INY / INY */
        CYC(12);                                   /* L51F9-L51FF */
        if (y >= CRACK) { CYC(1); break; }         /* L51FD CPY CRACK / L51FF BCS WNSHLD_20 */
        CYC(4);                                    /* L5201 CPY / L5203 BCC */
        if (y < 0x10) { CYC(1); continue; }        /* L5201 CPY #$10 / L5203 BCC WNSHLD_10 */
        CYC(7);                                    /* L5205-L5209 */
        a = 0x2C;                                  /* L5205 LDA #$2C */
        if (a >= CRACK) { CYC(1); break; }                     /* L5207 CMP CRACK / L5209 BCS WNSHLD_20 - CMP sets C when A >= CRACK: keep growing the crack until CRACK passes $2C (NOTES_integration.md "Fixes made" #14) */
        CYC(5);                                    /* L520B LDA / L520D BNE */
        a = FIRECT;                                /* L520B LDA FIRECT */
        if (a != 0) { CYC(1); goto wnshld_18; }    /* L520D BNE WNSHLD_18 */
        CYC(17);                                   /* L520F-L5219 */
        /* A is 0 here (FIRECT read as 0) */
        CRACK = a;                   /* L520F STA CRACK - C RESET CRACK */
        STATE = a;                   /* L5211 STA STATE */
        COLFLG = a;                  /* L5213 STA COLFLG */
        FTIMER = a;                  /* L5215 STA FTIMER */
        x = GOVER;                   /* L5217 LDX GOVER */
        if (x == 0) { CYC(1); goto wnshld_13; }  /* L5219 BEQ WNSHLD_13 */
        CYC(9);                      /* L521B-L521F (JMP CKSCOR) */
        ATRACT = a;                  /* L521B STA ATRACT - C SET TO ATTRACT */
        OBJCOL = a;                  /* L521D STA OBJCOL */
        return PASS_SIG_CKSCOR;      /* L521F JMP CKSCOR */

wnshld_13:
        /* Each PRAND read is charged up to its own LDA: RANDOM only moves
         * with machine time, so reads sharing one charge return the same
         * byte (the player came back at TPOSX == TPOSY, hi bytes r&$3F / r). */
        CYC(9);                   /* L5222-L5226 */
        a = 0x30;                 /* L5222 LDA #$30 */
        ACTION = a;               /* L5224 STA ACTION */
        a = hw_pokey_read(0x0A);  /* L5226 LDA PRAND - C RESET POSITION */
        CYC(10);                  /* L5229-L522D */
        RGOAL = a;                /* L5229 STA RGOAL */
        TPOSX = a;                /* L522B STA TPOSX */
        a = hw_pokey_read(0x0A);  /* L522D LDA PRAND */
        CYC(3);                   /* L5230 */
        TPOSY = a;                /* L5230 STA TPOSY */

        for (;;) {
            CYC(4);                                     /* L5232 */
            a = (uint8_t)(hw_pokey_read(0x0A) & 0x3F);  /* L5232 LDA PRAND / L5235 AND #$3F */
            CYC(9);                                     /* L5235-L5239 */
            TPOSX_AT(1) = a;                            /* L5237 STA TPOSX+1 */
            a = hw_pokey_read(0x0A);                    /* L5239 LDA PRAND */
            CYC(11);                                    /* L523C-L5240 (JSR OBJOBJ included) */
            TPOSY_AT(1) = a;                            /* L523C STA TPOSY+1 */
            {
                regs r = objobj(0x00);  /* L523E LDX #$00 / L5240 JSR OBJOBJ */
                CYC(2);                 /* L5243 BCS */
                if (r.c) { CYC(1); continue; }  /* L5243 BCS WNSHLD_15 */
            }
            break;
        }
        CYC(18);                               /* L5245-L5250 */
        OBJCOL = 0x00;                         /* L5245 STX OBJCOL - X is 0 here (last OBJOBJ call had X=0 in, untouched on no-collision exit per its own contract) */
        EIRNGE = 0x00;                         /* L5247 STX EIRNGE */
        a = hw_pokey_read(0x0A);               /* L5249 LDA PRAND - C RESET ANGLE */
        TANGLE = a;                            /* L524C STA TANGLE */
        if (!(R2D3FL & 0x80)) { CYC(1); goto wnshld_17; }  /* L524E BIT R2D3FL / L5250 BPL WNSHLD_17 */
        CYC(6);                                /* L5252 JSR */
        tankck();                              /* L5252 JSR TANKCK - C INSURE TANK */
        CYC(3);                                /* L5255 JMP B_END */
        return PASS_SIG_BEND;                  /* L5255 JMP B_END */

wnshld_17:
        CYC(5);                                 /* L5258 LDA / L525A BNE */
        if (COLFLG_AT(2) != 0) { CYC(1); goto wnshld_18; }  /* L5258 LDA COLFLG+2 / L525A BNE WNSHLD_18 */
        CYC(6);                                 /* L525C JSR */
        robchk();                               /* L525C JSR ROBCHK - C MOVE TANK IN FRONT OF HIM */

wnshld_18:
        CYC(3);                /* L525F JMP B_END */
        return PASS_SIG_BEND;  /* L525F JMP B_END */
    }

    /* WNSHLD_20: L5262-L5266 */
    CRACK++; CRACK++;                      /* INC CRACK / INC CRACK */
    CYC(12);                               /* L5262-L5266 */
    if (CRACK != 0) { CYC(1 + 3); return PASS_SIG_BEND; }  /* L5266 BNE WNSHLD_18 (-> JMP B_END) */
    /* Not taken only if CRACK wrapped to exactly 0 (was $FE) - falls
     * straight into BATTLE, the next routine in ROM order (a genuine
     * fall-through hazard between adjacent labels; see NOTES_disp.md).
     * BATTLE is an ordinary, RTS-returning routine (not part of the
     * b_end()/bz_main() recursion this fix addresses), so calling it
     * plainly is safe; what "control flow" continues after it on real
     * hardware in this specific, pathological (CRACK wraps to exactly 0)
     * case is undefined territory this port doesn't model - treated as
     * the end of this pass, same as any other unmodelled tail. */
    battle();
    return PASS_SIG_YIELD;
}

/*==============================================================================
 * BATTLE ($5268) - BATTLE ZONE LOGO. TRAP1 security checksum computed here
 * (attract mode only) over the vector buffer being built - translate the
 * checksum literally, it reads vector RAM through (TBLPTR),Y.
 *============================================================================*/
static void battle_10(void);

void battle(void)
{
    int y;
    unsigned c;
    uint8_t a;

    CYC(5);                     /* L5268 BIT / L526A BMI */
    if (ATRACT & 0x80) { CYC(7); return; }  /* L5268 BIT ATRACT / L526A BMI BATTLE_5 (RTS) */
    CYC(19 + 54 * 9 + 53 + 16);  /* L526C-L527A, the 54-pass SBC loop (BPL taken 53 times), L5280-L528A */

    TBLPTR_AT(1) = (uint8_t)(VGLIST_AT(1) & 0xF8);  /* L526C LDA VGLIST+1 / L526E AND #$F8 / L5270 STA TBLPTR+1 */
    TBLPTR = 0x02;                                  /* L5272 LDA #$02 / L5274 STA TBLPTR */

    a = 0x00;                    /* L5278 LDA #$00 */
    c = 1;                       /* L527A SEC */
    for (y = 0x35; y >= 0; y--)  /* L5276 LDY #$35 ... L527D DEY / L527E BPL BATTLE_99 */
        a = sbc6502(a, cpu_rd((uint16_t)(((uint16_t)TBLPTR_AT(1) << 8) | TBLPTR) + (uint16_t)y), &c, NULL);  /* L527B SBC (TBLPTR),Y */

    c = 1;                                                          /* L5280 SEC */
    a = sbc6502(a, cpu_rd((uint16_t)(A_OBJTBL + 0x21)), &c, NULL);  /* L5281 SBC OBJTBL+$21 */
    TRAP1 = a;                                                      /* L5284 STA TRAP1 */

    if (!(LOGO & 0x80)) { CYC(6); return; }  /* L5287 BIT LOGO / L528A BMI BATTLE_10 (else falls to BATTLE_5 RTS) */

    CYC(1);  /* L528A BMI taken */
    battle_10();
}

static void battle_10(void)
{
    uint8_t x, y, a;

    a = LOGOZ_AT(1);    /* L528D LDA LOGOZ+1 - D IF DONE WITH SEQUENCE */
    CYC(6);             /* L528D LDA / L5290 BMI */
    if (!(a & 0x80)) {  /* L5290 BMI BATTLE_13 */
        CYC(4);           /* L5292 CMP / L5294 BCC */
        if (a >= 0x02) {  /* L5292 CMP #$02 / L5294 BCC BATTLE_13 */
            CYC(3);    /* L5296 JMP */
            batint();  /* L5296 JMP BATINT */
            return;
        }
        CYC(1);  /* L5294 BCC taken */
    } else CYC(1);  /* L5290 BMI taken */

    /* BATTLE_13 */
    CYC(2);                           /* L5299 LDX */
    x = 0x00;                         /* L5299 LDX #$00 - C FIND NEXT SPOT IN TABLE */
    while (CYC(6), !(PTBLO2_AT(x) & 0x80)) {  /* L529B LDA PTBLO2,X / L529E BMI BATTLE_20 */
        CYC(7);    /* L52A0-L52A2, BNE taken */
        x++; x++;  /* L52A0/L52A1 INX / INX */
    }  /* L52A2 BNE BATTLE_15 (X never wraps to 0 in practice) */
    CYC(1 + 8);  /* L529E BMI taken, L52A4-L52A9 */

    /* BATTLE_20 */
    a = 0xF0;  /* L52A4 LDA #$F0 - C SET BRITENESS */
    SINT = a;  /* L52A6 STA SINT */
    y = 0x00;  /* L52A9 LDY #$00 */

    for (;;) {
        /* BATTLE_22 */
        CYC(57 + 6);                           /* L52AB-L52CF, L52D0 LDA / L52D3 BPL */
        a = cpu_rd((uint16_t)(A_LOGOBJ + y));  /* L52AB LDA LOGOBJ,Y - C SET UP OBJ #,X,Y */
        PTBLO2_AT(x) = a;                      /* L52AE STA PTBLO2,X */
        PTBLX2_AT(x) = LOGOX;                  /* L52B1/L52B4 LDA LOGOX / STA PTBLX2,X */
        PTBLY2_AT(x) = 0x00;                   /* L52B7/L52B9 LDA #$00 / STA PTBLY2,X */
        x++; y++;                              /* L52BC/L52BD INX / INY */
        PTBLO2_AT(x) = TANGLE;                 /* L52BE/L52C0 LDA TANGLE / STA PTBLO2,X - USE TANK'S ANGLE */
        PTBLX2_AT(x) = LOGOX_AT(1);            /* L52C3/L52C6 LDA LOGOX+1 / STA PTBLX2,X */
        PTBLY2_AT(x) = 0x00;                   /* L52C9/L52CB LDA #$00 / STA PTBLY2,X */
        x++; y++;                              /* L52CE/L52CF INX / INY */

        a = LOGOZ_AT(1);  /* L52D0 LDA LOGOZ+1 - D THE ULTIMATE KLUDGE */
        if (a & 0x80) {   /* L52D3 BPL BATTLE_90 - the kludge chain runs only while LOGOZ+1 is NEGATIVE */
            CYC(4);          /* L52D5 CMP / L52D7 BCS */
            if (a < 0xFD) {  /* L52D5 CMP #$FD / L52D7 BCS BATTLE_90 */
                CYC(8);              /* L52D9-L52DE */
                if (LOGOZ < 0xB0) {  /* L52D9 LDA LOGOZ / L52DC CMP #$B0 / L52DE BCS BATTLE_90 */
                    CYC(5 + (y < 0x03 ? 3 : 2));  /* L52E0 CPY / L52E2 JMP, BATTLE_95's BCC */
                    if (y < 0x03) continue;  /* L52E0 CPY #$03 / L52E2 JMP BATTLE_95 (BCC BATTLE_22) */
                    goto battle_95;
                }
                CYC(1);  /* L52DE BCS taken */
            } else CYC(1);  /* L52D7 BCS taken */
        } else CYC(1);  /* L52D3 BPL taken */
        /* BATTLE_90 */
        CYC(2 + (y < 0x05 ? 3 : 2));  /* L52E5 CPY / L52E7 BCC */
        if (y < 0x05) continue;  /* L52E5 CPY #$05 / L52E7 BCC BATTLE_22 */

battle_95:
        break;
    }

    /* BATTLE_95 fell through (Y >= the required count) */
    {
        uint8_t lz = LOGOZ, lzh = LOGOZ_AT(1);  /* L52E9/L52EC LDA LOGOZ / LDY LOGOZ+1 - C SET UP Z */
        unsigned c;
        CYC(34);                           /* L52E9-L5301 */
        EXPOSZ_AT(0xC) = lz;               /* L52EF STA EXPOSZ+$C */
        EXPOSZ_AT(0xD) = lzh;              /* L52F2 STY EXPOSZ+$D */
        EXPOSZ_AT(0xE) = lz;               /* L52F5 STA EXPOSZ+$E */
        EXPOSZ_AT(0xF) = lzh;              /* L52F8 STY EXPOSZ+$F */
        c = 0;                             /* L52FB CLC - C UPDATE Z */
        lz = adc6502(lz, 0x08, &c, NULL);  /* L52FC ADC #$08 */
        LOGOZ = lz;                        /* L52FE STA LOGOZ */
        if (c) { CYC(2); lzh++; } else CYC(1);  /* L5301 BCC BATTLE_25 / L5303 INY */
        CYC(22);                           /* L5304-L5313 */
        LOGOZ_AT(1) = lzh;                 /* L5304 STY LOGOZ+1 */
    }
    {
        uint8_t lxh = LOGOX_AT(1), lx = LOGOX;  /* L5308 LDY LOGOX+1 / L530B LDA LOGOX - C UPDATE DISTANCE */
        unsigned c = 0;                         /* L5307 CLC */
        lx = adc6502(lx, 0x40, &c, NULL);       /* L530E ADC #$40 */
        LOGOX = lx;                             /* L5310 STA LOGOX */
        if (c) { CYC(2); lxh++; } else CYC(1);  /* L5313 BCC BATTLE_30 / L5315 INY */
        CYC(17);                                /* L5316-L531E */
        LOGOX_AT(1) = lxh;                      /* L5316 STY LOGOX+1 */
    }
    PTBLO2_AT(x) = 0xFF;  /* L5319/L531B LDA #$FF / STA PTBLO2,X - C END TABLE */
}

/*==============================================================================
 * BATINT ($531F) - C INIT FOR LOGO (comment at the call).
 *============================================================================*/
void batint(void)
{
    CYC(46);             /* L531F-L533F */
    LOGOZ_AT(1) = 0xFC;  /* L531F LDA #$FC / L5321 STA LOGOZ+1 - C INIT POSITION */
    LOGOX = 0x00;        /* L5324/L5326 LDA #$00 / STA LOGOX */
    LOGOZ = 0x00;        /* L5329 STA LOGOZ */
    LOGO = 0x00;         /* L532C STA LOGO */
    LOGOX_AT(1) = 0x04;  /* L532F/L5331 LDA #$04 / STA LOGOX+1 */
    TIMOUT = 0x03;       /* L5334/L5336 LDA #$03 / STA TIMOUT - C TEMPORARY */
    DSPLAY = 0x00;       /* L5338/L533A LDA #$00 / STA DSPLAY */
    FRAME = 0x00;        /* L533D STA FRAME */
}

/*==============================================================================
 * CKSCOR ($5344) - HIGH SCORE STUFF: is HITS good enough to make the table?
 *============================================================================*/
/* Integration fix (2026-09-18, NOTES_integration.md "CRITICAL fix... open
 * item 1 resolved") - same trampoline as wnshld(): ckscor_body() returns
 * a pass_signal instead of tail-calling bz_main()/b_end() for real. */
void ckscor(void) { (void)ckscor_body(); }

pass_signal ckscor_body(void)
{
    uint8_t x;

    CYC(9);         /* L5344-L5348 */
    TIMOUT = 0x00;  /* L5344/L5346 LDX #$00 / STX TIMOUT - C RESET TIMER */
    DSPLAY = 0x00;  /* L5348 STX DSPLAY */

    for (x = 0x00; ; ) {
        uint8_t lo = HSCTBL_AT(x);  /* L534B LDY HSCTBL,X - D IF GOOD ENOUGH TO MAKE TABLE */
        CYC(16);                    /* L534B-L5355 */
        unsigned c;
        uint8_t hi = HSCTBL_AT(x + 1);
        uint8_t r;
        c = 1;                            /* CPY sets carry as SBC would (no explicit SEC needed - CPY performs its own borrow-free compare) */
        r = sbc6502(lo, HITS, &c, NULL);  /* L534E CPY HITS (compare, discard result, keep C) */
        (void)r;
        r = sbc6502(hi, HITS_AT(1), &c, NULL);  /* L5350/L5353 LDA HSCTBL+1,X / SBC HITS+1 */
        (void)r;
        if (!c) { CYC(1); break; }       /* L5355 BCC CKSCOR_20 */
        x += 3;                          /* L5357-L5359 INX INX INX */
        CYC(10);                         /* L5357-L535C */
        if (x < K_HSCNUM * 3) { CYC(1); continue; }  /* L535A CPX #HSCNUM*3 / L535C BCC CKSCOR_10 */
        CYC(3);                          /* L535E JMP B_END */
        /* Integration fix (2026-09-18, NOTES_integration.md): the ROM
         * disassembly (../disasm/bzone_program_rom.asm:562) confirms
         * "L535E: JMP B_END ;B NOT GOOD ENOUGH" - this site really does
         * jump to B_END, not MAIN.  The pre-existing C code called
         * bz_main() here (a real translation bug, predating this
         * session's trampoline refactor - found while converting this
         * exact call site to a pass_signal, since the call target and
         * its own comment disagreed). Fixed to PASS_SIG_BEND. */
        return PASS_SIG_BEND;  /* L535E JMP B_END - NOT GOOD ENOUGH */
    }

    /* CKSCOR_20 */
    {
        uint8_t savx = x;
        int i;

        CYC(22 + 8);                      /* L5361-L536F, L5371 LDA / L5373 JSR SNDON */
        FRAME = 0x00;                     /* L5361/L5363 LDA #$00 / STA FRAME - C RESET TIME OUT */
        LETR = 0x00;                      /* L5365 STA LETR */
        SAVX = savx;                      /* L5368 STX SAVX */
        OBJCOL = 0x00;                    /* L536B STA OBJCOL - C KILL ANY CONTINUOUS SOUNDS */
        SCOLFG = 0x00;                    /* L536D STA SCOLFG */
        SAUCER = 0x00;                    /* L536F STA SAUCER */
        sndon(K_SUPBON, 0x00);            /* L5371/L5373 LDA #SUPBON / JSR SNDON - C PLAY THE TUNE; X unread since A!=0 (bzsoun.h) */
        GOVER = (uint8_t)(GOVER | 0x80);  /* L5376/L5378/L537A LDA GOVER / ORA #$80 / STA GOVER - C SET HI SCORE MODE */

        CYC(10);                            /* L5376-L537C */
        for (i = (K_HSCNUM - 1) * 3; ; ) {  /* L537C LDX #[HSCNUM-1]*3 ; CKSCOR_25: CPX SAVX / BEQ CKSCOR_30 */
            CYC(6);                                     /* L537E CPX / L5381 BEQ */
            if ((uint8_t)i == savx) { CYC(1); break; }  /* L5381 BEQ CKSCOR_30 */
            CYC(45);                                    /* L5383-L539E */
            HSCTBL_AT(i)     = HSCTBL_AT(i - 3);  /* L5383/L5386 */
            HSCTBL_AT(i + 1) = HSCTBL_AT(i - 2);  /* L5389/L538C */
            INITLS_AT(i)     = INITLS_AT(i - 3);  /* L538F/L5392 */
            INITLS_AT(i + 1) = INITLS_AT(i - 2);  /* L5395/L5398 */
            INITLS_AT(i + 2) = INITLS_AT(i - 1);  /* L539B/L539E */
            i -= 3;
            CYC(i >= 0 ? 9 : 8);                  /* L53A1-L53A4, BPL taken while X >= 0 */
            if (i < 0) break;
        }  /* L53A1-L53A4 DEX DEX DEX / BPL CKSCOR_25 */

        /* CKSCOR_30 - i (X) == savx here */
        CYC(41);                           /* L53A6-L53C2 (JMP B_END) */
        HSCTBL_AT(savx)     = HITS;        /* L53A6/L53A8 LDA HITS / STA HSCTBL,X */
        HSCTBL_AT(savx + 1) = HITS_AT(1);  /* L53AB/L53AD LDA HITS+1 / STA HSCTBL+1,X */
        LTIMER = 0x04;                     /* L53B0/L53B2 LDA #$04 / STA LTIMER */
        STINTL = 0x16;                     /* L53B5/L53B7 LDA #$16 / STA STINTL - C SET UP INITIALS */
        STINTL_AT(1) = 0x4C;               /* L53BA/L53BC LDA #$4C / STA STINTL+1 */
        STINTL_AT(2) = 0x4C;               /* L53BF STA STINTL+2 */
        return PASS_SIG_BEND;              /* L53C2 JMP B_END */
    }
}

/*==============================================================================
 * HISCRE ($53C5) - high-score initials entry state; see bzone_disp.h.
 *============================================================================*/
/* Integration fix (2026-09-18, NOTES_integration.md "CRITICAL fix... open
 * item 1 resolved") - same trampoline as wnshld()/ckscor(). */
void hiscre(void) { (void)hiscre_body(); }

pass_signal hiscre_body(void)
{
    uint8_t x, a;

    CYC(7);                              /* L53C5-L53C9 */
    if (TIMOUT >= 0x04) { CYC(1); goto hiscre_60; }  /* L53C5/L53C7/L53C9 LDA TIMOUT / CMP #$04 / BCS HISCRE_60 */

    CYC(5);            /* L53CB LDA / L53CD BNE */
    if (POINT == 0) {  /* L53CB LDA POINT / L53CD BNE HISCRE_10 */
        CYC(5);          /* L53CF LDX / L53D1 BEQ */
        x = HITS_AT(2);  /* L53CF LDX HITS+2 - D IF EPLO DONE ALREADY (CHEAP TRICK) */
        if (x == 0) CYC(1);  /* L53D1 BEQ taken */
        if (x != 0) {    /* L53D1 BEQ HISCRE_10 */
            CYC(13);            /* L53D3-L53DB */
            HITS_AT(2) = 0x00;  /* L53D3 STA HITS+2 - C MAKE CHEAP TRICK VIABLE (A was 0 from LDA POINT) */
            SNDWRD = 0x02;      /* L53D5/L53D7 LDA #$02 / STA SNDWRD - C CANNON EXPLOSION */
            EXPCNT = 0xFF;      /* L53D9/L53DB LDA #$FF / STA EXPCNT */
        }
    } else CYC(1);  /* L53CD BNE taken */

    /* HISCRE_10 (fall-through target, no goto lands here - the D IF POINT/HITS+2
     * branches above rejoin here on every path) */
    CYC(6); info();         /* L53DD JSR INFO */
    CYC(8); msgs(K_LINE1);  /* L53E0/L53E2 LDX #LINE1 / JSR MSGS - C OUTPUT INSTRUCTIONS */
    CYC(8); msgs(K_LINE2);  /* L53E5/L53E7 */
    CYC(8); msgs(K_LINE3);  /* L53EA/L53EC */
    CYC(8); msgs(K_LINE4);  /* L53EF/L53F1 */
    CYC(6); letchk();       /* L53F4 JSR LETCHK - C LETTER CHANGE */
    CYC(28);                /* L53F7-L540B */

    a = hw_pokey_read(0x08);      /* L53F7 LDA ALLPOT - D IF BUTTON NOT PUSHED (AND DEBOUNCE) */
    NWSTAT = a;                   /* L53FA STA NWSTAT */
    a = (uint8_t)(a ^ OLSTAT);    /* L53FD EOR OLSTAT */
    a = (uint8_t)(a & NWSTAT);    /* L5400 AND NWSTAT */
    x = NWSTAT;                   /* L5403 LDX NWSTAT */
    OLSTAT = x;                   /* L5406 STX OLSTAT */
    a = (uint8_t)(a & K_PL1FIR);  /* L5409 AND #PL1FIR */
    if (a == 0) CYC(1);           /* L540B BEQ taken */
    if (a != 0) {                 /* L540B BEQ HISCRE_20 */
        CYC(8);                         /* L540D-L5412 */
        x = LETR;                       /* L540D LDX LETR - D IF LAST LETTER */
        if (x >= 0x02) { CYC(1); goto hiscre_60; }  /* L5410/L5412 CPX #$02 / BCS HISCRE_60 */
        CYC(13);                        /* L5414-L541A */
        x++;                            /* L5414 INX - C NEXT LETTER */
        LETR = x;                       /* L5415 STX LETR */
        STINTL_AT(x) = 0x16;            /* L5418/L541A LDA #$16 / STA STINTL,X - BLANK NEXT LETTER */
    }

    /* HISCRE_20 */
    CYC(6);                   /* L541D JSR */
    vgcntr();                 /* L541D JSR VGCNTR */
    CYC(15 + 2);              /* L5420-L5428 (JSR VGVTR included), L542B LDX */
    VGSIZE = 0x00;            /* L5420/L5422 LDY #$00 / STY VGSIZE - C OUTPUT INITIALS */
    vgvtr(0xEE, 0xEE, 0x00);  /* L5424-L5428 LDA/LDX #$EE / JSR VGVTR (Y still 0 from above) */

    for (x = 0x00; x < 0x03; x++) {  /* L542B LDX #$00 ; HISCRE_30 loop */
        CYC(13);           /* L542D-L5432 (JSR DSPALP included) */
        TEMP1 = x;         /* L542D STX TEMP1 */
        a = STINTL_AT(x);  /* L542F LDA STINTL,X */
        dspalp(a);         /* L5432 JSR DSPALP */
        x = TEMP1;         /* L5435 LDX TEMP1 */
        CYC(x + 1 < 0x03 ? 10 : 9);  /* L5435-L543A, BCC taken while X < 3 */
    }                       /* L5437/L5438/L543A INX / CPX #$03 / BCC HISCRE_30 */
    CYC(6);                 /* L543C JSR */
    finish();               /* L543C JSR FINISH */
    CYC(3);                 /* L543F JMP MAIN */
    return PASS_SIG_YIELD;  /* L543F JMP MAIN */

hiscre_60:
    {
        uint8_t hx = SAVX, hy = 0x00;  /* L5442 LDX SAVX / L5445 LDY #$00 - MOVE INITIALS */
        CYC(6);                        /* L5442-L5445 */
        for (;;) {                     /* HISCRE_61 */
            INITLS_AT(hx) = STINTL_AT(hy);            /* L5447/L544A LDA STINTL,Y / STA INITLS,X */
            CYC(13);                                  /* L5447-L544F */
            if (hy < 0x02) { CYC(1 + 7); hx++; hy++; continue; }  /* L544D/L544F CPY #$02 / BCC HISCRE_65 (-> INX/INY, L5461/L5462) */
            break;                                    /* y == 2: fall through, finalize */
        }
        CYC(20);                /* L5451-L545E (JMP MAIN) */
        TIMOUT = 0x03;          /* L5451/L5453 LDA #$03 / STA TIMOUT */
        GOVER = 0x03;           /* L5455 STA GOVER */
        DSPLAY = 0x81;          /* L5457/L5459 LDA #$81 / STA DSPLAY */
        FRAME = 0x81;           /* L545C STA FRAME */
        return PASS_SIG_YIELD;  /* L545E JMP MAIN */
    }
}

/*==============================================================================
 * LETCHK ($5465) - C LETTER CHANGE (comment at the call): advance/retreat
 * the high-score initial letter being entered, once every LTIMER ticks.
 * Caller: HISCRE.
 *============================================================================*/
void letchk(void)
{
    uint8_t x, y, a;
    unsigned c;

    /* L5465 STA POTGO - C READ CONTROLLER: start a pot scan (strobe write).
     * The real 6502 A at entry is whatever HISCRE's preceding MSGS call left
     * (MSGS/MSTART's own exit state, not a value any caller controls), and
     * is immediately overwritten below by "LDA ALLPOT" before it is used
     * for anything else - so the byte actually written to POTGO here can
     * never affect any later comparison/store in this routine. Not modelled
     * as a parameter (see NOTES_disp.md); written as a fixed placeholder,
     * matching the CALLING_NOTES.md SNDON precedent for an unreliable
     * incidental register read. */
    CYC(12);                     /* L5465-L546D */
    hw_pokey_write(0x0B, 0x00);
    a = hw_pokey_read(0x08);     /* L5468 LDA ALLPOT */
    a = (uint8_t)(a & 0x03);     /* L546B AND #$03 - D IF PUSHED */
    if (a != 0) { CYC(1); goto letchk_10; }  /* L546D BNE LETCHK_10 */

letchk_3:
    CYC(12);        /* L546F-L5474 */
    LTIMER = 0x04;  /* L546F/L5471 LDA #$04 / STA LTIMER - C RESET TIMER */
    return;         /* LETCHK_5 L5474 RTS */

letchk_10:
    LTIMER--;                 /* L5475 DEC LTIMER - C DELAY */
    CYC(8);                   /* L5475 DEC / L5478 BNE */
    if (LTIMER != 0) { CYC(7); return; }  /* L5478 BNE LETCHK_5 (taken + RTS) */
    CYC(12);                  /* L547A-L5481 */

    x = LETR;          /* L547A LDX LETR - C GET LETTER */
    y = STINTL_AT(x);  /* L547D LDY STINTL,X */
    c = (unsigned)(a & 0x01);            /* L5480 LSR - carry out = old bit0 of A (the ALLPOT&$03 bits read above);
                                          * D IF INCREMENT LETTER */
    a = (uint8_t)(a >> 1);

    if (!c) {  /* L5481 BCC LETCHK_30 */
        /* LETCHK_30 - C INCREMENT LETTER */
        y++; y++;                 /* L548D/L548E INY / INY */
        CYC(1 + 8 + (y >= 0x4C ? 2 : 1));  /* BCC taken, L548D-L5491, L5493 LDY or BCC taken */
        if (y >= 0x4C) y = 0x16;  /* L548F CPY #$4C / L5491 BCC LETCHK_40 / L5493 LDY #$16 */
    } else {
        /* C DECREMENT LETTER */
        y--; y--;                /* L5483/L5484 DEY / DEY */
        CYC(8 + (y < 0x16 ? 5 : 1));  /* L5483-L5487, L5489 LDY / L548B BNE or BCS taken */
        if (y < 0x16) y = 0x4A;  /* L5485 CPY #$16 / L5487 BCS LETCHK_40 / L5489 LDY #$4A */
    }
    CYC(9);                      /* L5495-L5499 */

    /* LETCHK_40 */
    a = y;             /* L5495 TYA */
    STINTL_AT(x) = a;  /* L5496 STA STINTL,X */
    if (a != 0) { CYC(1); goto letchk_3; }      /* L5499 BNE LETCHK_3 - always taken for real STINTL letter codes
                                                  * ($16..$4B); kept as a real test, not hard-coded, per the brief. */
    /* a == 0: falls straight through into DISTBL in the ROM (the next
     * adjacent label) - a genuine fall-through hazard, never exercised by
     * any real STINTL value but modelled literally. */
    distbl();
}

/*==============================================================================
 * DISTBL ($549B) - high-score table display (attract mode), plus the
 * bonus-tank message at DISTBL_40.  Caller: MAIN family.
 *============================================================================*/
/* Integration fix (2026-09-18, NOTES_integration.md "CRITICAL fix... open
 * item 1 resolved") - same trampoline as wnshld()/ckscor()/hiscre(). */
void distbl(void) { (void)distbl_body(); }

pass_signal distbl_body(void)
{
    uint8_t x, a, ydisp;

    CYC(6); info();          /* L549B JSR INFO - C OUTPUT CURRENT SCORE */
    CYC(8); msgs(K_HISCOR);  /* L549E/L54A0 LDX #HISCOR / JSR MSGS */
    CYC(6); vgcntr();        /* L54A3 JSR VGCNTR */
    CYC(14);                 /* L54A6-L54AF */
    SAVX = 0x00;     /* L54A6/L54A8 LDY #$00 / STY SAVX */
    a = 0xE0;        /* L54AB LDA #$E0 */
    ydisp = 0x1E;    /* L54AD LDX #$1E */
    YDISP = ydisp;   /* L54AF STX YDISP */

    for (;;) {
        uint8_t lo, hi;

        /* DISTBL_10 */
        CYC(6);                              /* L54B2 JSR */
        vgvtr(a, ydisp, 0x00);               /* L54B2 JSR VGVTR */
        CYC(17);                             /* L54B5-L54C0 */
        x = SAVX;                            /* L54B5 LDX SAVX */
        lo = HSCTBL_AT(x);                   /* L54B8 LDA HSCTBL,X */
        TEMP2 = lo;                          /* L54BB STA TEMP2 */
        hi = HSCTBL_AT(x + 1);               /* value ORA'd next, and reused below */
        if ((uint8_t)(lo | hi) == 0) { CYC(1); break; }  /* L54BD ORA HSCTBL+1,X - D IF DONE / L54C0 BEQ DISTBL_40 */
        CYC(15);                             /* L54C2-L54C9 (JSR DIGT2S included) */
        TEMP2_AT(1) = hi;                    /* L54C2/L54C5 LDA HSCTBL+1,X / STA TEMP2+1 */
        digt2s((uint8_t)A_TEMP2);            /* L54C7/L54C9 LDA #TEMP2 / JSR DIGT2S */
        CYC(8);                              /* L54CC LDX / L54CE JSR */
        msgs(K_ZEROS);                       /* L54CC/L54CE LDX #ZEROS / JSR MSGS */

        /* DISTBL_20 - C OUTPUT INITIALS (X reloaded from SAVX before each
         * DSPALP call because DSPALP clobbers X). */
        CYC(14); x = SAVX; dspalp(INITLS_AT(x));      /* L54D1-L54D7 */
        CYC(14); x = SAVX; dspalp(INITLS_AT(x + 1));  /* L54DA-L54E0 */
        CYC(14); x = SAVX; dspalp(INITLS_AT(x + 2));  /* L54E3-L54E9 */

        {
            int y = 0;              /* L54EC LDY #$00 */
            CYC(12);                /* L54EC-L54F4 */
            x = SAVX;               /* L54EE LDX SAVX */
            hi = HSCTBL_AT(x + 1);  /* L54F1 LDA HSCTBL+1,X - D */
            if (hi != 0) {          /* L54F4 BEQ DISTBL_25 */
                uint16_t vg = VGLIST_ADDR;
                uint8_t nsym = hi;  /* L54F6 TAX */

                CYC(28 + (nsym >= 0x0A ? 2 : 1));                /* L54F6-L5504, L5506 LDX or BCC taken */
                cpu_wr(vg + (uint16_t)y, cpu_rd(A_VGMSGA));      /* L54F7/L54FA LDA VGMSGA / STA (VGLIST),Y - C BLANK BEFORE TANK */
                y++;                                             /* L54FC INY */
                cpu_wr(vg + (uint16_t)y, cpu_rd(A_VGMSGA + 1));  /* L54FD/L5500 LDA VGMSGA+1 / STA (VGLIST),Y */
                if (nsym >= 0x0A) nsym = 0x0A;                   /* L5502 CPX #$0A / L5504 BCC DISTBL_23 / L5506 LDX #$0A */

                do {
                    /* DISTBL_23 */
                    y++;                                             /* L5508 INY */
                    cpu_wr(vg + (uint16_t)y, cpu_rd(A_TSYMBL));      /* L5509/L550C LDA TSYMBL / STA (VGLIST),Y */
                    y++;                                             /* L550E INY */
                    cpu_wr(vg + (uint16_t)y, cpu_rd(A_TSYMBL + 1));  /* L550F/L5512 LDA TSYMBL+1 / STA (VGLIST),Y */
                    nsym--;                                          /* L5514 DEX */
                    CYC(nsym != 0 ? 29 : 28);                        /* L5508-L5515, BNE taken while X != 0 */
                } while (nsym != 0);  /* L5515 BNE DISTBL_23 */
                CYC(6);               /* L5517 JSR */
                vgadd((uint8_t)y);    /* L5517 JSR VGADD */
            } else CYC(1);            /* L54F4 BEQ taken */
        }

        /* DISTBL_25 - D IF DONE */
        CYC(8);                              /* L551A-L551F */
        x = SAVX;                            /* L551A LDX SAVX */
        if (x >= (K_HSCNUM - 1) * 3) { CYC(1); break; }  /* L551D CPX #[HSCNUM-1]*3 / L551F BCS DISTBL_40 */
        CYC(10 + 6);                         /* L5521-L5524, L5527 JSR VGCNTR */
        x = (uint8_t)(x + 3);                /* L5521-L5523 INX INX INX */
        SAVX = x;                            /* L5524 STX SAVX */
        vgcntr();                            /* L5527 JSR VGCNTR - C MOVE BEAM TO NEXT LINE */
        {
            unsigned c = 1;                          /* L552D SEC */
            ydisp = sbc6502(YDISP, 0x0A, &c, NULL);  /* L552A/L552E LDA YDISP / SBC #$0A */
            YDISP = ydisp;                           /* L5530 STA YDISP */
        }
        CYC(21);   /* L552A-L5538 */
        a = 0xE0;  /* L5534 LDA #$E0 (L5536 LDY #$00 / L5538 JMP DISTBL_10 - loop) */
    }

    /* DISTBL_40 */
    CYC(16);                                   /* L553D-L5546 */
    a = (uint8_t)((hw_option() >> 4) & 0x03);  /* L553D-L5544 LDA OPTION / LSR x4 / AND #$03 - C SET TEMP2 W/BONUS LEVEL */
    if (a == 0) CYC(1);                        /* L5546 BEQ taken */
    if (a != 0) {                              /* L5546 BEQ DISTBL_45 */
        CYC(17 + 8);                                    /* L5548-L5551, L5553 LDX / L5555 JSR MSGS */
        uint8_t bt = cpu_rd((uint16_t)(A_BONTBL + a));  /* L5548/L5549 TAX / LDA BONTBL,X */
        unsigned c = 0;                                 /* L554C CLC */
        uint8_t savedf = g.dflag;
        g.dflag = 1;                       /* L554D SED */
        bt = adc6502(bt, 0x01, &c, NULL);  /* L554E ADC #$01 */
        g.dflag = savedf;                  /* L5550 CLD */
        SAVE1 = bt;                        /* L5551 STA SAVE1 */
        msgs(K_BONPLN);                    /* L5553/L5555 LDX #BONPLN / JSR MSGS - C OUTPUT BONUS MESSAGE */
        CYC(10);                           /* L5558-L555C (JSR included) */
        digtys((uint8_t)A_SAVE1, 0x01);    /* L5558-L555C LDA #SAVE1 / LDY #$01 / JSR DIGTYS - C OUTPUT BONUS LEVEL */
        CYC(8);                            /* L555F LDX / L5561 JSR */
        msgs(K_BONPL1);                    /* L555F/L5561 LDX #BONPL1 / JSR MSGS - C OUTPUT REMAINING MESSAGE */
    }

    /* DISTBL_45 */
    CYC(6);                 /* L5564 JSR */
    finish();               /* L5564 JSR FINISH */
    CYC(3);                 /* L5567 JMP MAIN */
    return PASS_SIG_YIELD;  /* L5567 JMP MAIN */
}

/*==============================================================================
 * DSPALP ($556A) - draw one alphanumeric vector character (A = code); looks
 * up its JSRL pair in VGMSGA and tail-calls VGADD2 (DSPALP's own "return" is
 * VGADD2's RTS - CALLING_NOTES.md section 2).  Callers: HISCRE, DISTBL.
 *============================================================================*/
void dspalp(uint8_t a)
{
    uint8_t y = a;                                     /* L556A TAY - C OUTPUT ALPHA NUM CHAR. */
    CYC(13);                                           /* L556A-L5571 (JMP VGADD2) */
    uint8_t x = cpu_rd((uint16_t)(A_VGMSGA + 1 + y));  /* L556B LDX VGMSGA+1,Y */
    a = cpu_rd((uint16_t)(A_VGMSGA + y));              /* L556E LDA VGMSGA,Y */
    vgadd2(a, x);                                      /* L5571 JMP VGADD2 */
}

/*==============================================================================
 * FINISH ($5574) - end of a display build: stop the AVG, mark the double
 * buffer ready, and (attract mode, 4+ buzz bombers destroyed) copy TRAP1
 * into vector RAM as the anti-tamper check.  Callers: HISCRE, DISTBL, MAIN.
 *============================================================================*/
void finish(void)
{
    CYC(6);               /* L5574 JSR */
    vghalt();             /* L5574 JSR VGHALT - C STOP VG */
    CYC(10);              /* L5577-L557D */
    BUFRDY = 0x00;        /* L5577/L5579 LDA #$00 / STA BUFRDY - C INDICATE BUFFER READY */
    if (ATRACT & 0x80) {  /* L557B BIT ATRACT / L557D BPL FINISH_10 - C SECURITY */
        CYC(6);                           /* L557F LDA / L5582 BMI */
        if (NOR2D3 & 0x80) CYC(1);        /* L5582 BMI taken */
        else {
            CYC(4);                       /* L5584 CMP / L5586 BCC */
            if (NOR2D3 < 0x04) CYC(1);    /* L5586 BCC taken */
        }
        if (!(NOR2D3 & 0x80) && NOR2D3 >= 0x04) {  /* L557F LDA NOR2D3 / L5582 BMI FINISH_10 / L5584 CMP #$04 / L5586 BCC FINISH_10 */
            CYC(8);                  /* L5588 LDA / L558B STA */
            VRAM(A_VECRAM) = TRAP1;  /* L5588/L558B LDA TRAP1 / STA VECRAM - VECRAM ($2000) is vector RAM, not flat RAM */
        }
    } else CYC(1);        /* L557D BPL taken */
    CYC(6);               /* L558E RTS */

}

/*==============================================================================
 * MOUNTS_50 - not a real ROM label (file-static helper for MOUNTS' own
 * horizon-tile drawing loop, $58A7-$58B9); shares MTNS-table-index bookkeeping
 * across its 3 chained calls in mounts() (see NOTES_disp.md).
 *============================================================================*/
static uint8_t mounts_50(uint8_t a)
{
    uint8_t y  = (uint8_t)(a & 0x0E);             /* L58A7 AND #$0E */
    CYC(25);                                      /* L58A7-L58B4 (JSR VGADD2 included) */
    uint8_t hi = cpu_rd((uint16_t)(A_MTNS + y));  /* L58AA LDA MTNS,Y */
    y++;                                          /* L58AD INY */
    uint8_t lo = cpu_rd((uint16_t)(A_MTNS + y));  /* L58AE LDX MTNS,Y */
    y++;                                          /* L58B1 INY */
    TEMP1 = y;                                    /* L58B2 STY TEMP1 */
    vgadd2(hi, lo);                               /* L58B4 JSR VGADD2 - DRAW MOUNTAINS */
    CYC(9);                                       /* L58B7 LDA / L58B9 RTS */
    return TEMP1;                                 /* L58B7/L58B9 LDA TEMP1 / RTS */
}

/*==============================================================================
 * MOUNTS ($57C8) - draw the horizon-mountain skyline (view-angle-scrolled,
 * bounce-shaken) and, when the volcano is on screen, its rock particles.
 * Caller: MAIN family.
 *============================================================================*/
void mounts(void)
{
    uint8_t a;
    unsigned c;
    uint8_t saved_hi;

    CYC(68);             /* L57C8-L57ED */
    XCOMP_AT(3) = 0x00;  /* L57C8/L57CA LDX #$00 / STX XCOMP+3 - LDA VIEW FOR 1 BIT RESOLUTION */
    XCOMP_AT(2) = 0x00;  /* L57CC STX XCOMP+2 */
    XCOMP = LANGLE;      /* L57CE/L57D0 LDA LANGLE / STA XCOMP */

    a = TANGLE;  /* L57D2 LDA TANGLE - X=ANGLE (9 BITS) */
    {
        /* 4x (LSR A / ROR XCOMP): shift TANGLE right into XCOMP's top bit,
         * four times - wraps every 360 degrees, per the ROM comment. */
        uint8_t xc = XCOMP;
        int i;
        for (i = 0; i < 4; i++) {  /* L57D4/L57D5, L57D7/L57D8, L57DA/L57DB, L57DD/L57DE */
            unsigned bit0 = a & 1;
            a  = (uint8_t)(a >> 1);
            xc = (uint8_t)((xc >> 1) | (bit0 << 7));
        }
        XCOMP = xc;
    }
    saved_hi = a;                                /* L57E0 PHA - A is now TANGLE>>4 */
    XCOMP_AT(1) = (uint8_t)((a & 0x01) | 0x02);  /* L57E1/L57E3/L57E5 AND #$01 / ORA #$02 / STA XCOMP+1 - 512 TO 1023 RANGE */

    a = (uint8_t)(BOUNCE >> 4);  /* L57E7-L57EC LDA BOUNCE / LSR x4 - C PROVIDE BOUNCE */
    if (a == 0) CYC(1);          /* L57ED BEQ taken */
    if (a != 0) {                /* L57ED BEQ MOUNTS_20 */
        CYC(19);                                              /* L57EF-L57FA */
        TEMP1 = a;                                            /* L57EF STA TEMP1 */
        c = 1;                                                /* L57F1 SEC */
        XCOMP_AT(2) = sbc6502(XCOMP_AT(2), TEMP1, &c, NULL);  /* L57F2/L57F4/L57F6 LDA XCOMP+2 / SBC TEMP1 / STA XCOMP+2 */
        XCOMP_AT(3) = 0xFF;                                   /* L57F8/L57FA LDA #$FF / STA XCOMP+3 */
    }

    /* MOUNTS_20 */
    CYC(6);                                                        /* L57FC JSR */
    vgcntr();                                                      /* L57FC JSR VGCNTR - CENTER BEAM */
    CYC(6);                                                        /* L57FF JSR */
    vgvtr2();                                                      /* L57FF JSR VGVTR2 - POSITION BEAM */
    CYC(10);                                                       /* L5802-L5806 (JSR included) */
    vgjsrl((uint8_t)(A_HORIZN >> 8), (uint8_t)(A_HORIZN & 0xFF));  /* L5802-L5806 LAH/LXL HORIZN / JSR VGJSRL - ADD HORIZON LINE */

    a = (uint8_t)(saved_hi ^ 0x0F);  /* L5809/L580A PLA / EOR #$0F */
    CYC(12);                         /* L5809-L580C (JSR included) */
    a = mounts_50(a);                /* L580C JSR MOUNTS_50 - DRAW 3/2 FULL SCREEN */
    CYC(6);                          /* L580F JSR */
    a = mounts_50(a);                /* L580F JSR MOUNTS_50 - TO INSURE FULL VIEW */
    CYC(6);                          /* L5812 JSR */
    a = mounts_50(a);                /* L5812 JSR MOUNTS_50 */

    CYC(13);                /* L5815-L581D */
    XCOMP = LANGLE;         /* L5815/L5817 LDA LANGLE / STA XCOMP - SET UP FOR SIFT */
    a = TANGLE;             /* L5819 LDA TANGLE - D IF VOLCANO NOT ON SCREEN */
    if (a >= 0x88) { CYC(7); return; }  /* L581B/L581D CMP #$88 / BCS MOUNTS_21 (taken + RTS) */
    CYC(4);                 /* L581F CMP / L5821 BCS */
    if (a < 0x39) { CYC(6); return; }   /* L581F/L5821 CMP #$39 / BCS MOUNTS_22 (else falls straight into MOUNTS_21's RTS) */
    CYC(1 + 31 + 30);       /* BCS taken, L5824-L5830, L5832-L5846 */

    /* MOUNTS_22 - C DOUBLE SIFT 4 TO SCALE */
    {
        uint8_t xc = XCOMP;
        int i;
        for (i = 0; i < 4; i++) {  /* L5824/L5825, L5827/L5828, L582A/L582B, L582D/L582E */
            unsigned bit0 = a & 1;
            a  = (uint8_t)(a >> 1);
            xc = (uint8_t)((xc >> 1) | (bit0 << 7));
        }
        XCOMP = xc;
        XCOMP_AT(1) = a;  /* L5830 STA XCOMP+1 */
    }
    c = 1;                                                /* L5834 SEC */
    XCOMP        = sbc6502(XCOMP, 0x05, &c, NULL);        /* L5832/L5835/L5837 LDA XCOMP / SBC #$05 / STA XCOMP */
    XCOMP_AT(1)  = sbc6502(XCOMP_AT(1), 0x06, &c, NULL);  /* L5839/L583B/L583D LDA XCOMP+1 / SBC #$06 / STA XCOMP+1 */
    c = 0;                                                /* L583F CLC */
    XCOMP_AT(2) = adc6502(0x5E, XCOMP_AT(2), &c, NULL);   /* L5840/L5842/L5844 LDA #$5E / ADC XCOMP+2 / STA XCOMP+2 */
    if (c) { CYC(5); XCOMP_AT(3)++; } else CYC(1);        /* L5846/L5848 BCC MOUNTS_25 / INC XCOMP+3 */

    /* MOUNTS_25 - C MOVE BEAM TO TOP OF VOLCANO */
    CYC(6);                              /* L584A JSR */
    vgcntr();                            /* L584A JSR VGCNTR */
    CYC(6);                              /* L584D JSR */
    vgvtr2();                            /* L584D JSR VGVTR2 */
    CYC(14 + 2);                         /* L5850-L5858, L585A LDX */
    CURNTX = 0x00; CURNTX_AT(1) = 0x00;  /* L5850-L5854 LDA #$00 / STA CURNTX / STA CURNTX+1 */
    CURNTY = 0x00; CURNTY_AT(1) = 0x00;  /* L5856/L5858 STA CURNTY / STA CURNTY+1 */

    {
        int x;
        for (x = K_NOROCK - 1; x >= 0; x--, CYC(x >= 0 ? 5 : 4)) {  /* L585A LDX #NOROCK-1 ; MOUNTS_30 loop; MOUNTS_40 DEX / BPL */
            uint8_t t;
            unsigned cc;

            CYC(6);                           /* L585C LDA / L585F BEQ */
            if (OBJTIM_AT(x) == 0) { CYC(1); continue; }  /* L585C/L585F LDA OBJTIM,X / BEQ MOUNTS_40 (= loop step) - D IF NOT ACTIVE */
            CYC(64 + 14);                     /* L5861-L5889, L588B-L5891 (JSR VGVTR2 included) */

            /* C DELTA X */
            cc = 1;                                             /* L5864 SEC */
            t = XPOSL_AT(x);                                    /* L5861 LDA XPOSL,X */
            XCOMP = sbc6502(t, CURNTX, &cc, NULL);              /* L5865/L5866/L5868 TAY / SBC CURNTX / STA XCOMP */
            CURNTX = t;                                         /* L586A STY CURNTX */
            t = XPOSH_AT(x);                                    /* L586C LDA XPOSH,X */
            XCOMP_AT(1) = sbc6502(t, CURNTX_AT(1), &cc, NULL);  /* L586F/L5870/L5872 TAY / SBC CURNTX+1 / STA XCOMP+1 */
            CURNTX_AT(1) = t;                                   /* L5874 STY CURNTX+1 */

            /* C DELTA Y */
            cc = 1;                                             /* L5876 SEC */
            t = YPOSL_AT(x);                                    /* L5877 LDA YPOSL,X */
            XCOMP_AT(2) = sbc6502(t, CURNTY, &cc, NULL);        /* L587A/L587B/L587D TAY / SBC CURNTY / STA XCOMP+2 */
            CURNTY = t;                                         /* L587F STY CURNTY */
            t = YPOSH_AT(x);                                    /* L5881 LDA YPOSH,X */
            XCOMP_AT(3) = sbc6502(t, CURNTY_AT(1), &cc, NULL);  /* L5884/L5885/L5887 TAY / SBC CURNTY+1 / STA XCOMP+3 */
            CURNTY_AT(1) = t;                                   /* L5889 STY CURNTY+1 */

            VGBRIT = 0x00;       /* L588B/L588D LDA #$00 / STA VGBRIT - C VECTOR TO POINT */
            TEMP1 = (uint8_t)x;  /* L588F STX TEMP1 */
            vgvtr2();            /* L5891 JSR VGVTR2 */
            x = TEMP1;           /* L5894 LDX TEMP1 */

            CYC(21);                                             /* L5894-L589E (JSR VGDOT included) */
            a = (uint8_t)((uint8_t)(OBJTIM_AT(x) << 3) & 0xE0);  /* L5896-L589C LDA OBJTIM,X / ASL x3 / AND #$E0 */
            vgdot(a);                                            /* L589E JSR VGDOT */
            CYC(3);                                              /* L58A1 LDX */
            x = TEMP1;                                           /* L58A1 LDX TEMP1 */

            /* MOUNTS_40: L58A3/L58A4 DEX / BPL MOUNTS_30 - the for loop's own decrement+test */
        }
    }
    CYC(6);  /* MOUNTS_45: L58A6 RTS */
}

/*==============================================================================
 * VOLCNO ($58BA) - move up to NOROCK (5) volcano-eruption rock particles,
 * one call per frame; occasionally spawns a new rock when one goes idle.
 * Caller: MAIN family.
 *============================================================================*/
void volcno(void)
{
    int x;
    /* Integration fix (2026-09-18, NOTES_integration.md), continued: `c`
     * must persist ACROSS loop iterations, not just within one - the
     * "start a new rock" ADC pair's carry-in is whatever the LAST carry-
     * touching instruction anywhere in this same call left it as (DEC/BNE/
     * AND/the loop's own DEX/BPL never touch C), which can be an EARLIER
     * iteration's own VOLCNO_31/VOLCNO_33 chain (each of which has its own
     * real CLC, correctly modelled below) just as easily as the original
     * JSR-entry carry.  Initialised to 1 ONCE here (the proven entry value,
     * from bz_main()'s own "LSR SYNC/BCC MAIN" fall-through) and never
     * force-reset before L58D2 - found by lockstep: the single-iteration
     * fix above still failed on later rocks in the SAME call (6/99 VOLCNO
     * checks), always at a DIFFERENT rock's XSPD than the first fix
     * covered. */
    unsigned c = 1;

    CYC(2);  /* L58BA LDX */
    for (x = K_NOROCK - 1; x >= 0; x--, CYC(x >= 0 ? 7 : 5)) {  /* L58BA LDX #NOROCK-1 ; VOLCNO_10 loop - 5 ROCKS; VOLCNO_50 DEX / BMI / JMP */
        uint8_t a, sp;
        int y;

        CYC(6);                                 /* L58BC LDA / L58BF BNE */
        if (OBJTIM_AT(x) != 0) { CYC(1); goto volcno_30; }  /* L58BC/L58BF LDA OBJTIM,X / BNE VOLCNO_30 - THIS ROCK ACTIVE? */

        /* not active - should we start one? */
        CYC(8);                   /* L58C1-L58C6 */
        a = hw_pokey_read(0x0A);  /* L58C1 LDA PRAND */
        a = (uint8_t)(a & 0x07);  /* L58C4 AND #$07 (this # may change) */
        if (a != 0) { CYC(1); continue; }  /* L58C6 BNE VOLCNO_50 (= loop step) - don't start this time */

        CYC(11);              /* L58C8-L58CD - up to the LDA PRAND; the BIT PRAND below is charged separately (RANDOM only moves with machine time) */
        OBJTIM_AT(x) = 0x1F;  /* L58C8/L58CA LDA #$1F / STA OBJTIM,X - rock active time */

        a = hw_pokey_read(0x0A);  /* L58CD LDA PRAND */
        a = (uint8_t)(a & 0x03);  /* L58D0 AND #$03 */
        /* Integration fix (2026-09-18, NOTES_integration.md): this ADC's
         * carry-in is NOT actually uncontrolled - the empirical lockstep
         * check NOTES_disp.md asked for found a real, provable mismatch
         * (XSPD off by exactly 1) and traced it: VOLCNO is JSR'd from
         * exactly one site, bz_main()'s own $5004 ("JSR VOLCNO", right
         * after "L5000 LSR SYNC / L5002 BCC MAIN" falls through) - and a
         * BCC falls through (does not branch) only when carry is SET, so
         * the FIRST time this ADC runs in a given call, carry is 1,
         * guaranteed (`c`'s initialiser above), not merely assumed.
         * Nothing between VOLCNO's own entry and this ADC (LDX/LDA/BNE/
         * AND/STA, none of which touch C) disturbs it on that first pass;
         * on any later loop iteration that reaches this same ADC, `c` is
         * instead whatever an intervening VOLCNO_31/VOLCNO_33 chain left
         * it as (see the persistence comment above the loop) - use it as
         * given, do not reset. */
        a = adc6502(a, 0x01, &c, NULL);                           /* L58D2 ADC #$01 - SOME RANDOM HORIZ POSITION */
        CYC(8);                                                   /* L58D0-L58D4 */
        {
            uint8_t pr = hw_pokey_read(0x0A);                     /* L58D4 BIT PRAND */
            CYC(2);                                               /* L58D7 */
            if (pr & 0x40) { CYC(2); a = (uint8_t)(a ^ 0xFF); } else CYC(1);  /* L58D7/L58D9 BVC VOLCNO_11 / EOR #$FF - LEFT OR RIGHT, change horiz speed (+-1) */
        }
        CYC(18);                  /* L58DB-L58E5 */

        /* VOLCNO_11 */
        XSPD_AT(x) = a;           /* L58DB STA XSPD,X */
        a = hw_pokey_read(0x0A);  /* L58DE LDA PRAND */
        a = (uint8_t)(a & 0x07);  /* L58E1 AND #$07 */
        /* This second ADC's carry-in is likewise not independent: BIT/BVC/
         * EOR/STA/LDA/AND between L58D2 and here never touch C, so it is
         * exactly the carry OUT of the first ADC above - a chain, the same
         * pattern the maths agent found for ROTATE's BSIN negate (thread
         * the real `c`, don't reset it to 0). */
        a = adc6502(a, 0x05, &c, NULL);  /* L58E3 ADC #$05 - SOME RANDOM VERT VELOCITY */
        YSPD_AT(x) = a;                  /* L58E5 STA YSPD,X */

volcno_12:
        CYC(25);             /* L58E8-L58F6, BEQ taken */
        XPOSL_AT(x) = 0x00;  /* L58E8-L58EA LDA #$00 / STA XPOSL,X */
        XPOSH_AT(x) = 0x00;  /* L58ED STA XPOSH,X */
        YPOSL_AT(x) = 0x00;  /* L58F0 STA YPOSL,X */
        YPOSH_AT(x) = 0x00;  /* L58F3 STA YPOSH,X */
        continue;            /* L58F6 BEQ VOLCNO_50 - ALWAYS (= loop step) */

volcno_30:
        OBJTIM_AT(x)--;                         /* L58F8 DEC OBJTIM,X */
        CYC(9);                                 /* L58F8 DEC / L58FB BEQ */
        if (OBJTIM_AT(x) == 0) { CYC(1); goto volcno_12; }  /* L58FB BEQ VOLCNO_12 */
        YSPD_AT(x)--;                           /* L58FD DEC YSPD,X - GRAVITY TAMES HOLD */
        CYC(17 + ((int8_t)YSPD_AT(x) < 0 ? 2 : 1) + 15);  /* L58FD-L5906, DEY or BPL taken, L5909-L5912 */

        y = 0;                       /* L5900 LDY #$00 - MOVE ROCKS */
        c = 0;                       /* L5902 CLC */
        sp = YSPD_AT(x);             /* L5903 LDA YSPD,X */
        if ((int8_t)sp < 0) y = -1;  /* L5906/L5908 BPL VOLCNO_31 / DEY - A MINUS #, SIGN EXTEND */

        /* VOLCNO_31 */
        YPOSL_AT(x) = adc6502(sp, YPOSL_AT(x), &c, NULL);  /* L5909/L590C ADC YPOSL,X / STA YPOSL,X */

        if ((int8_t)YPOSH_AT(x) >= 0) { CYC(1); goto volcno_33; }  /* L590F/L5912 LDA YPOSH,X / BPL VOLCNO_33 (C still = ADC YPOSL carry-out) */
        CYC(8);                                        /* L5914-L5919 */
        /* The CMP overwrites C on both exits: C=1 into L5923's ADC YPOSH
         * when it branches, C=0 when the rock lands (and that 0 persists
         * into a later rock's L58D2 ADC).  Integration fix, see
         * NOTES_integration.md "Fixes made" #11. */
        c = (YPOSL_AT(x) >= 0xA2);  /* L5914/L5917 LDA YPOSL,X - LOW ENOUGH / CMP #$A2 */
        if (c) { CYC(1); goto volcno_33; }  /* L5919 BCS VOLCNO_33 */
        CYC(10);                    /* L591B-L5920, BEQ taken */
        OBJTIM_AT(x) = 0x00;        /* L591B/L591D LDA #$00 / STA OBJTIM,X */
        goto volcno_12;             /* L5920 BEQ VOLCNO_12 - ALWAYS */

volcno_33:
        CYC(21 + ((int8_t)XSPD_AT(x) < 0 ? 2 : 1) + 20);           /* L5922-L592F, DEY or BPL taken, L5932-L593C */
        YPOSH_AT(x) = adc6502((uint8_t)y, YPOSH_AT(x), &c, NULL);  /* L5922/L5923/L5926 TYA - GET MSB / ADC YPOSH,X / STA YPOSH,X */

        y = 0;                       /* L5929 LDY #$00 - MSB 0(GUESS) */
        c = 0;                       /* L592B CLC */
        sp = XSPD_AT(x);             /* L592C LDA XSPD,X */
        if ((int8_t)sp < 0) y = -1;  /* L592F/L5931 BPL VOLCNO_32 / DEY - AS ABOVE, BUT FOR X */

        /* VOLCNO_32 */
        XPOSL_AT(x) = adc6502(sp, XPOSL_AT(x), &c, NULL);          /* L5932/L5935 ADC XPOSL,X / STA XPOSL,X */
        XPOSH_AT(x) = adc6502((uint8_t)y, XPOSH_AT(x), &c, NULL);  /* L5938/L5939/L593C TYA / ADC XPOSH,X / STA XPOSH,X - DONE MOVED THEM! */

        /* VOLCNO_50: L593F DEX / L5940 BMI VOLCNO_55 / L5942 JMP VOLCNO_10 - the for loop's own decrement+test */
    }
    CYC(6);  /* VOLCNO_55: L5945 RTS */

}

/*==============================================================================
 * DRAW ($5C5C) - build the AVG display list for one object from PNTTBL (its
 * already-projected 2-D points, from PNTPUT) and OBJTBL (its edge-list
 * opcode stream, ROM).  DTAB is the opcode dispatch (push-address-then-RTS
 * in the ROM, TRANSLATING.md -> switch); D_LABS falls into D_BVCT, D_SPCL
 * falls into D_SLIM - both reproduced as literal switch fall-through.
 * Caller: MAIN family (once per visible object per frame, after PNTPUT).
 *============================================================================*/
void draw(uint8_t a)
{
    uint8_t y = (uint8_t)(a << 1);  /* L5C5C/L5C5D ASL / TAY - C BUILD INDEX TO OBJ TBL */

    CYC(18);                                              /* L5C5C-L5C66 */
    TBLPTR       = cpu_rd((uint16_t)(A_OBJTBL + y));      /* L5C5E/L5C61 LDA OBJTBL,Y / STA TBLPTR - GET POINTER TO OBJ IN MEM */
    TBLPTR_AT(1) = cpu_rd((uint16_t)(A_OBJTBL + y + 1));  /* L5C63/L5C66 LDA OBJTBL+1,Y / STA TBLPTR+1 */

    for (;;) {  /* D_TOP */
        uint16_t ptr = (uint16_t)(((uint16_t)TBLPTR_AT(1) << 8) | TBLPTR);
        uint8_t op = cpu_rd(ptr);  /* L5C68/L5C6A LDY #$00 / LDA (TBLPTR),Y - D IF NOT DONE (value from table) */

        CYC(11);                                /* L5C68-L5C6E */
        if (op == 0xFF) { CYC(6); return; }     /* L5C6C/L5C6E CMP #$FF / BNE D_TOP_11 (else RTS) */

        /* D_TOP_11 - C DO COMPUTED GO TO */
        CYC(1 + 35);                   /* BNE taken, L5C71-L5C83 (DTAB push/RTS dispatch) */
        TEMP1 = (uint8_t)(op & 0xF8);  /* L5C71/L5C72/L5C74 TAX / AND #$F8 / STA TEMP1 - save index/value */

        switch ((uint8_t)(op & 0x07)) {  /* L5C76-L5C7A TXA / AND #$07 / ASL / TAX -> DTAB push-addr-then-RTS dispatch */
        case 0: {                        /* D_DOT */
            CYC(8);        /* L5C92 LDA / L5C94 JSR */
            dovect(0x00);  /* L5C92/L5C94 LDA #$00 / JSR DOVECT - C DRAW BLANK VECTOR TO POINT */
            CYC(8);        /* L5C97 LDA / L5C99 JSR */
            vgdot(0x20);   /* L5C97/L5C99 LDA #$20 / JSR VGDOT - C DRAW DOT AT CURRENT INTENSITY */
            CYC(3);        /* L5C9C JMP */
            break;         /* L5C9C JMP D_NEXT */
        }
        case 1: {  /* D_SBRT */
            uint8_t saveo  = SAVE0;             /* L5C9F LDX SAVE0 - D IF NOT SAUCER */
            uint8_t obtype = PTBLO2_AT(saveo);  /* L5CA1 LDA PTBLO2,X */
            uint8_t bright;

            if (obtype == 0x17 || obtype == 0x1E || obtype == 0x1F ||  /* L5CA4-L5CAE CMP #$17/#$1E/#$1F / BEQ D_SBRT_5 each - D IF LOGO */
                (obtype == 0x20 && SCOLFG != 0)) {  /* L5CB0-L5CB6 CMP #$20 / BNE D_SBRT_10 ; LDA SCOLFG / BEQ D_SBRT_10 - D IF NOT SAUCER / NOT IN COLLISION */
                /* L5C9F-L5CB6 up to the branch that lands here, then D_SBRT_5's LDA / JMP (7) */
                CYC(obtype == 0x17 ? 12 : obtype == 0x1E ? 16 : obtype == 0x1F ? 20 : 28);
                CYC(7);
                bright = SINT;                      /* D_SBRT_5 L5CB8 LDA SINT */
            } else {
                unsigned cc = 1;                              /* L5CC0 SEC */
                uint8_t t = sbc6502(TEMP1, DQUE, &cc, NULL);  /* L5CBE/L5CC1 LDA TEMP1 / SBC DQUE - C GET BRITNESS */
                CYC(obtype == 0x20 ? 29 : 24);                /* L5C9F-L5CB6 to D_SBRT_10 (BNE or BEQ taken) */
                CYC(!cc ? 13 : t < 0x30 ? 16 : 15);           /* D_SBRT_10 L5CBE-L5CC7, D_SBRT_12's LDA where reached */
                bright = (cc && t >= 0x30) ? t : 0x30;        /* L5CC3 BCC D_SBRT_12 / L5CC5/L5CC7 CMP #$30 / BCS D_SBRT_15 (else D_SBRT_12: LDA #$30) */
            }
            CYC(10);               /* L5CCB TAY / L5CCC LDA / L5CCE JSR */
            vgstat(0x04, bright);  /* L5CCB-L5CCE TAY / LDA #$04 / JSR VGSTAT - C SET BRITNESS REGISTER, no clipping window */
            CYC(3);                /* L5CD1 JMP */
            break;                 /* L5CD1 JMP D_NEXT */
        }
        case 3:  /* D_LABS - falls into D_BVCT (case 2), below */
            CYC(14 + 6);  /* L5CD4-L5CDC, L5CDE JSR */
            CURNTX = 0x00; CURNTX_AT(1) = 0x00; CURNTY = 0x00; CURNTY_AT(1) = 0x00;  /* L5CD4-L5CDC LDA #$00 / STA CURNTX/+1/CURNTY/+1 - C RESET CURRENT X&Y */
            vgcntr();  /* L5CDE JSR VGCNTR - C MOVE BEAM */
            /* FALLTHROUGH */
        case 2:  /* D_BVCT */
            CYC(5 + 6);    /* L5CE1 LDA / L5CE3 BEQ (taken), L5CE7 JSR */
            dovect(0x00);  /* L5CE1/L5CE3 (D_VCT1) LDA #$00 / JSR DOVECT - C DRAW BLANK VECTOR */
            CYC(3);        /* L5CEA JMP */
            break;         /* L5CEA JMP D_NEXT */
        case 4:  /* D_VCTR */
            CYC(2 + 6);    /* L5CE5 LDA, L5CE7 JSR */
            dovect(0x20);  /* L5CE5 (D_VCT1) LDA #$20 / JSR DOVECT - C DRAW AT CURRENT BRITHNESS */
            CYC(3);        /* L5CEA JMP */
            break;         /* L5CEA JMP D_NEXT */
        case 5: {  /* D_SPCL - falls into D_SLIM (case 6), below */
            uint8_t saveo = SAVE0;                              /* L5CED LDX SAVE0 - C SPECIAL COUNTER */
            uint8_t sb    = PTBLO2_AT(saveo + 1);               /* L5CEF LDA PTBLO2+1,X */
            uint8_t lin   = (uint8_t)((sb & 0x3F) << 1);        /* L5CF2-L5CF5 TAX / AND #$3F / ASL - LINEAR (Y) */
            uint8_t pw    = (uint8_t)(((sb >> 6) & 0x03) + 1);  /* L5CF7-L5CFD TXA/AND #$C0/ASL/ROL/ROL/ADC #$01 - POWER OF 2; see NOTES_disp.md for the bit-by-bit derivation of this formula */

            CYC(27 + 6);                                                 /* L5CED-L5CFD, L5CFF JSR */
            vgscal(pw, lin);                                             /* L5CFF JSR VGSCAL - C SET SCALE FACTOR */
            CYC(10);                                                     /* L5D02-L5D06 (JSR included) */
            vgjsrl((uint8_t)(A_EXPIC >> 8), (uint8_t)(A_EXPIC & 0xFF));  /* L5D02-L5D06 LXL/LAH EXPIC / JSR VGJSRL - C OUTPUT EXPLOSION PICTURE */
            CYC(8);                                                      /* L5D09 LDA / L5D0B JSR */
            vgsca1(0x01);                                                /* L5D09/L5D0B LDA #$01 / JSR VGSCA1 - C RESET SCALE */
            /* FALLTHROUGH */
        }
        case 6:  /* D_SLIM */
            CYC(3);  /* L5D0E JMP */
            break;  /* L5D0E JMP D_NEXT */
        default:
            /* opcode 7: DTAB only has 7 entries (0-6); no real OBJTBL
             * edge-list byte in this ROM ever encodes opcode 7. Defensive
             * no-op (not a real ROM path) so this loop can't spin forever
             * if it ever were - see NOTES_disp.md. */
            break;
        }

        d_next();  /* L5D41-L5D47 INC TBLPTR(+1) / JMP D_TOP - advance, then loop back to fetch the next opcode */
    }
}

/*==============================================================================
 * DOVECT ($5D11) - draw one vector of the current object at brightness A:
 * compute the delta from CURNTX/CURNTY to the next PNTTBL point (indexed by
 * TEMP1, set by DRAW's own dispatcher before the opcode handler ran) and
 * append it via VGVTR2.  Callers: D_DOT, D_BVCT/D_VCTR (via draw()'s switch).
 *============================================================================*/
void dovect(uint8_t a)
{
    uint8_t x, p0, p1, p2, p3;
    unsigned c;

    CYC(80);                    /* L5D11-L5D3D (JSR VGVTR2 included) */
    VGBRIT = a;                 /* L5D11 STA VGBRIT - C SET BRITNESS */
    x = (uint8_t)(TEMP1 >> 1);  /* L5D13/L5D15/L5D16 LDA TEMP1 / LSR / TAX - C GET INDEX */

    c = 1;                                  /* L5D1A SEC - C COMPUTE DELTA X */
    p0 = PNTTBL_AT(x);                      /* L5D17 LDA PNTTBL,X */
    XCOMP = sbc6502(p0, CURNTX, &c, NULL);  /* L5D19/L5D1B/L5D1D TAY / SBC CURNTX / STA XCOMP */
    CURNTX = p0;                            /* L5D1F STY CURNTX - save new current */

    p1 = PNTTBL_AT(x + 1);                              /* L5D21 LDA PNTTBL+1,X */
    XCOMP_AT(1) = sbc6502(p1, CURNTX_AT(1), &c, NULL);  /* L5D23/L5D24/L5D26 TAY / SBC CURNTX+1 / STA XCOMP+1 */
    CURNTX_AT(1) = p1;                                  /* L5D28 STY CURNTX+1 */

    c = 1;                                        /* L5D2A SEC - C COMPUTE DELTA Y */
    p2 = PNTTBL_AT(x + 2);                        /* L5D2B LDA PNTTBL+2,X */
    XCOMP_AT(2) = sbc6502(p2, CURNTY, &c, NULL);  /* L5D2D/L5D2E/L5D30 TAY / SBC CURNTY / STA XCOMP+2 */
    CURNTY = p2;                                  /* L5D32 STY CURNTY - save new current */

    p3 = PNTTBL_AT(x + 3);                              /* L5D34 LDA PNTTBL+3,X */
    XCOMP_AT(3) = sbc6502(p3, CURNTY_AT(1), &c, NULL);  /* L5D36/L5D37/L5D39 TAY / SBC CURNTY+1 / STA XCOMP+3 */
    CURNTY_AT(1) = p3;                                  /* L5D3B STY CURNTY+1 */

    vgvtr2();  /* L5D3D JSR VGVTR2 - C ADD VCTR TO VGLIST */
    CYC(6);    /* L5D40 RTS */
}

/*==============================================================================
 * D_NEXT ($5D41) - DRAW's own loop back-edge (16-bit TBLPTR increment, then
 * "JMP D_TOP" - control returns into draw()'s own fetch loop, its caller,
 * not to a JSR caller of D_NEXT itself).  Exposed as its own function per
 * bzone_disp.h for direct lockstep coverage, even though it is never a real
 * JSR target on real hardware.
 *============================================================================*/
void d_next(void)
{
    TBLPTR++;                         /* L5D41 INC TBLPTR - C GET NEXT COMMAND */
    CYC(TBLPTR == 0 ? 7 + 5 + 3 : 8 + 3);  /* L5D41 INC / L5D43 BNE (taken unless it wrapped) / L5D45 INC, L5D47 JMP D_TOP */
    if (TBLPTR == 0) TBLPTR_AT(1)++;  /* L5D43/L5D45 BNE D_NEXT_10 / INC TBLPTR+1 */
    /* D_NEXT_10: L5D47 JMP D_TOP - the caller (draw()'s own loop) provides this */
}

/*==============================================================================
 * DRADAR ($6AE9) - display the radar sweep and, if an enemy is within it,
 * its blip and warning sound/message.  Caller: MAIN family.
 *============================================================================*/
void dradar(void)
{
    uint8_t a;
    regs r;

    CYC(10);                                                        /* L6AE9-L6AED (JSR included) */
    vgjsrl((uint8_t)(A_RDRING >> 8), (uint8_t)(A_RDRING & 0xFF));  /* L6AE9-L6AED LXL/LAH RDRING / JSR VGJSRL */
    CYC(27);                                                        /* L6AF0-L6AFF (JSR SIN included) */

    XCOMP_AT(1) = 0x00;   /* L6AF0/L6AF2 LDA #$00 / STA XCOMP+1 */
    YCOMP_AT(1) = 0x00;   /* L6AF4 STA YCOMP+1 */
    CURNTX_AT(1) = 0x00;  /* L6AF6 STA CURNTX+1 */

    {
        unsigned c = 0;                            /* L6AFA CLC */
        SANGLE = adc6502(0x0B, SANGLE, &c, NULL);  /* L6AF8-L6AFD LDA #$0B / ADC SANGLE / STA SANGLE - C INCREMENT SWEEP ANGLE */
    }

    r = sin_(SANGLE);  /* L6AFF JSR SIN - C SIN/2 */
    {
        uint8_t half = (uint8_t)((r.x >> 1) | (r.x & 0x80));  /* L6B02-L6B05 TXA / CMP #$80 / ROR - sign-extending 1-bit right shift */
        CYC(14 + ((int8_t)half < 0 ? 8 : 1) + 9);             /* L6B02-L6B0A, L6B0C-L6B10 or BPL taken, L6B12 LDA / L6B14 JSR COS */
        XCOMP = half;                                         /* L6B06 STA XCOMP */
        CURNTX = half;                                        /* L6B08 STA CURNTX */
        if ((int8_t)half < 0) {                               /* L6B0A BPL DRADAR_10 (skip if non-negative) */
            XCOMP_AT(1) = 0xFF;   /* L6B0C/L6B0E LDA #$FF / STA XCOMP+1 */
            CURNTX_AT(1) = 0xFF;  /* L6B10 STA CURNTX+1 */
        }
    }

    /* DRADAR_10 - C COS/2 */
    r = cos_(SANGLE);  /* L6B12/L6B14 LDA SANGLE / JSR COS */
    {
        uint8_t half = (uint8_t)((r.x >> 1) | (r.x & 0x80));  /* L6B17-L6B1A TXA / CMP #$80 / ROR */
        CYC(11 + ((int8_t)half < 0 ? 5 : 1) + 29);            /* L6B17-L6B1D, L6B1F-L6B21 or BPL taken, L6B23-L6B34 (JSR VGVTR2 included) */
        XCOMP_AT(2) = half;                                   /* L6B1B STA XCOMP+2 */
        if ((int8_t)half < 0) {                               /* L6B1D BPL DRADAR_20 */
            XCOMP_AT(3) = 0xFF;                                          /* L6B1F/L6B21 LDA #$FF / STA XCOMP+3 - see NOTES_disp.md "DRADAR
                                                                            * asymmetric sign-extend": there is no matching "else clear to 0"
                                                                            * in the ROM, so XCOMP+3 is genuinely left stale from whatever a
                                                                            * previous call left it as when this half-value is non-negative;
                                                                            * translated literally. */
        }
    }

    /* DRADAR_20 - C DRAW BRIGHT VECTOR */
    VGBRIT = 0xA0;  /* L6B23/L6B25 LDA #$A0 / STA VGBRIT */
    {
        unsigned c = 0;  /* L6B29 CLC */
        CURNTY       = adc6502((uint8_t)(K_CENTRY & 0xFF), XCOMP_AT(2), &c, NULL);  /* L6B27/L6B2A/L6B2C LDA #CENTRY&$FF / ADC XCOMP+2 / STA CURNTY */
        CURNTY_AT(1) = adc6502((uint8_t)(K_CENTRY >> 8), XCOMP_AT(3), &c, NULL);  /* L6B2E/L6B30/L6B32 LDA #CENTRY/$100 / ADC XCOMP+3 / STA CURNTY+1 */
    }
    vgvtr2();  /* L6B34 JSR VGVTR2 */

    CYC(5);                   /* L6B37 LDA / L6B39 BEQ */
    if (COLFLG_AT(2) != 0) {  /* L6B37/L6B39 LDA COLFLG+2 / BEQ DRADAR_21 - D IF NOT EXPLODING */
        CYC(16);                     /* L6B3B-L6B43 */
        hw_pokey_write(0x05, 0x00);  /* L6B3B/L6B3D LDA #$00 / STA CHAN3V */
        hw_pokey_write(0x07, 0x00);  /* L6B40 STA CHAN4V */
        return;                      /* L6B43 RTS */
    }

    /* DRADAR_21 - C TRACK THE ROBOT */
    CYC(1 + 6);               /* L6B39 BEQ taken, L6B44 JSR */
    a = track();              /* L6B44 JSR TRACK */
    CYC(26);                  /* L6B47-L6B57 */
    a = (uint8_t)(a ^ 0x80);  /* L6B47 EOR #$80 */
    GARBAG = a;               /* L6B49/L6B4A SEC / STA GARBAG (the SEC only primes the next SBC) */
    {
        unsigned c = 1;
        GARBAG = sbc6502(TANGLE, GARBAG, &c, NULL);  /* L6B4C/L6B4E/L6B50 LDA TANGLE / SBC GARBAG / STA GARBAG - C RELATIVE ANGLE OF ROBOT */
    }
    {
        unsigned c = 1;                                    /* L6B54 SEC */
        uint8_t diff = sbc6502(SANGLE, GARBAG, &c, NULL);  /* L6B52/L6B55 LDA SANGLE / SBC GARBAG - D IF NOT ON SWEEP LINE */
        if ((int8_t)diff < 0) CYC(1);                      /* L6B57 BMI taken */
        else CYC(diff < 0x0C ? 4 : 5);                     /* L6B59 CMP / L6B5B BCS (taken when >= $0C) */
        if ((int8_t)diff >= 0 && diff < 0x0C) {            /* L6B57/L6B59/L6B5B BMI DRADAR_23 / CMP #$0C / BCS DRADAR_23 */
            CYC(6);       /* L6B5D LDA / L6B5F STA */
            BLIP = 0xF0;  /* L6B5D/L6B5F LDA #$F0 / STA BLIP - C SET BRIGHTNESS OF BLIP */
        }
    }

    /* DRADAR_23 - C COMPUTE DISTANCE */
    CYC(32 + 25);                    /* L6B62-L6B76, L6B79-L6B87 (dummy read L6B79 included) */
    hw_mb_write(0x04, TEMP1);        /* L6B62/L6B64 LDA TEMP1 / STA TPOSXT */
    hw_mb_write(0x05, TEMP1_AT(1));  /* L6B67/L6B69 LDA TEMP1+1 / STA TPOSXT+1 */
    hw_mb_write(0x06, TEMP2);        /* L6B6C/L6B6E LDA TEMP2 / STA TPOSYT */
    hw_mb_write(0x07, TEMP2_AT(1));  /* L6B71/L6B73 LDA TEMP2+1 / STA TPOSYT+1 */
    hw_mb_write(0x1E, TEMP2_AT(1));  /* L6B76 STA DST2 (same A as just loaded) */
    /* L6B79 CMP ($00,X) - WASTE TIME dummy indirect read (CALLING_NOTES.md
     * section 3): not a real memory access, emits nothing. */
    TEMP3 = hw_mb_ylow();  /* L6B7B/L6B7E LDA MOUTLO / STA TEMP3 */
    {
        uint8_t mhi = hw_mb_yhigh();  /* L6B80 LDA MOUTHI */
        uint8_t x;

        TDIST = mhi;  /* L6B83 STA TDIST */
        x = mhi;      /* L6B86 TAX */

        if ((int8_t)x < 0) {  /* L6B87 BMI DRADAR_24 */
            CYC(1 + 2);  /* BMI taken, DRADAR_24 L6B94 LDA */
            a = 0x00;  /* DRADAR_24 L6B94 LDA #$00 */
        } else {
            a = (uint8_t)(((x >> 3) & 0x0F) ^ 0xAF);  /* L6B89-L6B8E LSR x3 / AND #$0F / EOR #$AF - C SET VOLUME */
            CYC((R2D3FL & 0x80) ? 16 : 17);           /* L6B89-L6B92, BMI taken or L6B94 LDA */
            if (!(R2D3FL & 0x80)) a = 0x00;           /* L6B90/L6B92 BIT R2D3FL / BMI DRADAR_25 (else falls to DRADAR_24) */
        }
        CYC(18);  /* L6B96-L6BA2 */
        /* DRADAR_25 */
        hw_pokey_write(0x05, a);  /* L6B96 STA CHAN3V */
        hw_pokey_write(0x07, a);  /* L6B99 STA CHAN4V */

        a = x;                 /* L6B9C TXA */
        hw_mb_write(0x08, a);  /* L6B9D STA XMATRX */

        if (a >= 0x80) {  /* L6BA0/L6BA2 CMP #$80 / BCC DRADAR_35 */
            CYC(10);              /* L6BA4-L6BAA */
            EIRNGE = 0x00;        /* L6BA4/L6BA6 LDA #$00 / STA EIRNGE - C OUT OF RANGE */
            if (R2D3FL & 0x80) {  /* L6BA8/L6BAA BIT R2D3FL / BPL DRADAR_30 */
                CYC(7);                /* L6BAC-L6BB0 */
                if (TIMOUT >= 0x04) {  /* L6BAC/L6BAE/L6BB0 LDA TIMOUT / CMP #$04 / BCC DRADAR_27 */
                    CYC(6);       /* L6BB2 JSR */
                    tankck();     /* L6BB2 JSR TANKCK */
                    CYC(3);       /* L6BB5 JMP */
                    goto dr_blp;  /* L6BB5 JMP DR_BLP */
                }
                CYC(1 + 6);  /* BCC taken, L6BB8 JSR */
                r2d3ck();  /* DRADAR_27 L6BB8 JSR R2D3CK */
            } else CYC(1);  /* L6BAA BPL taken */
            CYC(3);       /* L6BBB JMP */
            goto dr_blp;  /* DRADAR_30 L6BBB JMP DR_BLP */
        }
        CYC(1);  /* L6BA2 BCC taken */
    }

    /* DRADAR_35 */
    CYC(6);                 /* L6BBE LDA / L6BC1 BNE */
    if (BLIP == 0) { CYC(6); return; }  /* L6BBE/L6BC1 LDA BLIP / BNE DRADAR_36 (else RTS) */
    CYC(1 + 4);             /* BNE taken, L6BC4 CMP / L6BC6 BCC */

    if (BLIP >= 0xF0) {  /* DRADAR_36 L6BC4/L6BC6 CMP #$F0 / BCC DRADAR_40 */
        CYC(8);                /* L6BC8 LDA / L6BCA JSR */
        sndon(K_RBEEP, 0x00);  /* L6BC8/L6BCA LDA #RBEEP / JSR SNDON */
    } else CYC(1);             /* L6BC6 BCC taken */

    /* DRADAR_40 - C SETUP SIN/COS FOR MATRIX MULT */
    CYC(9);                  /* L6BCD LDA / L6BCF JSR */
    r = sin_(GARBAG);        /* L6BCD/L6BCF LDA GARBAG / JSR SIN */
    CYC(17);                 /* L6BD2-L6BDA (JSR COS included) */
    hw_mb_write(0x00, r.a);  /* L6BD2 STA ACOS */
    hw_mb_write(0x01, r.x);  /* L6BD5 STX ACOS+1 */
    r = cos_(GARBAG);        /* L6BD8/L6BDA LDA GARBAG / JSR COS */
    CYC(55);                 /* L6BDD-L6C04 (JSR WAIT included) */
    hw_mb_write(0x02, r.a);  /* L6BDD STA BSIN */
    hw_mb_write(0x03, r.x);  /* L6BE0 STX BSIN+1 */

    hw_mb_write(0x04, (uint8_t)(K_CENTRX & 0xFF));  /* L6BE3/L6BE5 TPOSXT */
    hw_mb_write(0x05, (uint8_t)(K_CENTRX >> 8));    /* L6BE8/L6BEA TPOSXT+1 */
    hw_mb_write(0x06, (uint8_t)(K_CENTRY & 0xFF));  /* L6BED/L6BEF TPOSYT */
    hw_mb_write(0x07, (uint8_t)(K_CENTRY >> 8));    /* L6BF2/L6BF4 TPOSYT+1 */
    hw_mb_write(0x09, 0x00);                        /* L6BF7/L6BF9 XMATRX+1 */
    VGBRIT = 0x00;                                  /* L6BFC STA VGBRIT - C SET ZERO BRITE */
    hw_mb_write(0x0A, 0x00);                        /* L6BFE STA YMATRX - C START MATRIX MULT */
    hw_mb_write(0x11, 0x00);                        /* L6C01 STA YSTRT2 */

    {
        uint8_t lo = bz_mb_wait();  /* L6C04 JSR WAIT - C SCALE RESLT */
        uint8_t hi;
        unsigned c;

        CYC(58);                /* L6C07-L6C2B (JSR VGVTR2 included) */
        hw_mb_write(0x17, lo);  /* L6C07 STA DXP */

        lo = hw_mb_ylow();                                  /* L6C0A LDA MOUTLO */
        c = 1;                                              /* L6C0D SEC */
        XCOMP = sbc6502(lo, CURNTX, &c, NULL);              /* L6C0E/L6C10 SBC CURNTX / STA XCOMP */
        hi = hw_mb_yhigh();                                 /* L6C12 LDA MOUTHI */
        hw_mb_write(0x19, hi);                              /* L6C15 STA DYPH - restat multiplier */
        XCOMP_AT(1) = sbc6502(hi, CURNTX_AT(1), &c, NULL);  /* L6C18/L6C1A SBC CURNTX+1 / STA XCOMP+1 */

        lo = hw_mb_ylow();                                  /* L6C1C LDA MOUTLO */
        c = 1;                                              /* L6C1F SEC */
        YCOMP = sbc6502(lo, CURNTY, &c, NULL);              /* L6C20/L6C22 SBC CURNTY / STA YCOMP */
        hi = hw_mb_yhigh();                                 /* L6C24 LDA MOUTHI */
        YCOMP_AT(1) = sbc6502(hi, CURNTY_AT(1), &c, NULL);  /* L6C27/L6C29 SBC CURNTY+1 / STA YCOMP+1 */
    }
    vgvtr2();  /* L6C2B JSR VGVTR2 */

    a = (uint8_t)(BLIP & 0xE0);  /* L6C2E/L6C31 LDA BLIP / AND #$E0 - C DRAW BLIP */
    CYC(12);                     /* L6C2E-L6C33 (JSR included) */
    vgdot(a);                    /* L6C33 JSR VGDOT */
    CYC(9);                      /* L6C36 LDA / L6C38 JSR */
    vgdot(VGBRIT);               /* L6C36/L6C38 LDA VGBRIT / JSR VGDOT */

    CYC(5);  /* L6C3B BIT / L6C3D BMI */
    if (ATRACT & 0x80) CYC(1);                 /* L6C3D BMI D_5 taken */
    else CYC((LOGO & 0x80) ? 6 + 1 : 6);       /* L6C3F BIT / L6C42 BMI (D_10 taken) */
    if (!(((ATRACT & 0x80) == 0) && ((LOGO & 0x80) != 0))) {  /* L6C3B-L6C42 BIT ATRACT/BMI D_5; BIT LOGO/BMI D_10 - run D_5 unless (!ATRACT.7 && LOGO.7) */
        /* D_5 */
        CYC(7);              /* L6C44-L6C48 */
        if (FRAME & 0x02) {  /* L6C44/L6C46/L6C48 LDA #$02 / AND FRAME / BEQ D_10 */
            CYC(8);          /* L6C4A LDX / L6C4C JSR */
            msgs(K_ERANGE);  /* L6C4A/L6C4C LDX #ERANGE / JSR MSGS - C INDICATE IN RANGE */
        } else CYC(1);       /* L6C48 BEQ taken */
    }

    /* D_10 */
    CYC(5);             /* L6C4F LDA / L6C51 BNE */
    if (EIRNGE == 0) {  /* L6C4F/L6C51 LDA EIRNGE / BNE DR_BLP - D IF ALREADY IN RANGE */
        CYC(13);               /* L6C53-L6C57 (JSR SNDON included) */
        EIRNGE++;              /* L6C53 INC EIRNGE - C INDICATE IN RANGE */
        sndon(K_WARNG, 0x00);  /* L6C55/L6C57 LDA #WARNG / JSR SNDON - C OUTPUT WARNING SOUND */
    } else CYC(1);             /* L6C51 BNE taken */

dr_blp:
    CYC(6);           /* L6C5A LDA / L6C5D BEQ */
    if (BLIP != 0) {  /* L6C5A/L6C5D LDA BLIP / BEQ DR_RTN - D IF SWEEP WAY PAST */
        unsigned c = 1;                        /* L6C5F SEC */
        CYC(8);                                /* L6C5F-L6C62 */
        BLIP = sbc6502(BLIP, 0x08, &c, NULL);  /* L6C60/L6C62 SBC #$08 / STA BLIP */
    } else CYC(1);                             /* L6C5D BEQ taken */
    CYC(6);  /* DR_RTN: L6C65 RTS */

}

/*==============================================================================
 * SHUPDT ($6C66) - update shell positions for the *second* shell then the
 * first (LDX #$02, "C START WITH SECOND SHELL" - processing order 2, then 0,
 * not a skip; see NOTES_disp.md).  Caller: MAIN family.
 *============================================================================*/
void shupdt(void)
{
    int x;

    CYC(2);               /* L6C66 LDX */
    for (x = 0x02; ; ) {  /* L6C66 LDX #$02 */
        uint8_t fc = FIRECT_AT(x);  /* L6C68 LDA FIRECT,X - D IF SHELL NOT IN AIR */

        CYC(fc == 0 ? 7 : (fc & 0x80) ? 9 : 66);  /* L6C68-L6C6C (BEQ or BMI taken), or through L6C88 */
        if (fc != 0 && !(fc & 0x80)) {  /* L6C6A/L6C6C BEQ SHUPDT_20 / BMI SHUPDT_20 (skip unless in flight) */
            unsigned c;

            FIRECT_AT(x) = (uint8_t)(fc - 1);  /* L6C6E DEC FIRECT,X - C RANGE SHELL */

            c = 0;                                                            /* L6C70 CLC */
            SHELLX_AT(x)     = adc6502(SHELLX_AT(x), SINCX_AT(x), &c, NULL);  /* L6C71-L6C75 LDA SHELLX,X / ADC SINCX,X / STA SHELLX,X - C UPDATE SHELL'S X POS */
            SHELLX_AT(x + 1) = adc6502(SHELLX_AT(x + 1), SINCX_AT(x + 1), &c, NULL);  /* L6C77-L6C7B LDA SHELLX+1,X / ADC SINCX+1,X / STA SHELLX+1,X */

            c = 0;                                                            /* L6C7F CLC */
            SHELLY_AT(x)     = adc6502(SHELLY_AT(x), SINCY_AT(x), &c, NULL);  /* L6C7D-L6C82 LDA SHELLY,X / ADC SINCY,X / STA SHELLY,X - C UPDATE SHELL'S Y POS */
            SHELLY_AT(x + 1) = adc6502(SHELLY_AT(x + 1), SINCY_AT(x + 1), &c, NULL);  /* L6C84-L6C88 LDA SHELLY+1,X / ADC SINCY+1,X / STA SHELLY+1,X */
        }

        /* SHUPDT_20 */
        x -= 2;                /* L6C8A/L6C8B DEX / DEX */
        CYC(x == 0 ? 7 : 6 + 6);  /* L6C8A-L6C8C, BEQ taken, or L6C8E RTS */
        if (x == 0) continue;  /* L6C8C BEQ SHUPDT_10 */
        break;
    }
    /* L6C8E RTS */
}

/*==============================================================================
 * MSTART ($6CD4) - not exposed in bzone_disp.h (reached only by falling out
 * of ENGLISH/GERMAN/FRENCH/SPANSH, never JSR'd - CALLING_NOTES.md section 2);
 * file-static helper shared by msgs()'s flattened language dispatch.  Draws
 * the packed-character message TBLPTR/TBLPTR+1 (hi, passed in) points at,
 * for message index x (still needed here for the GAMOVR scale test).
 *============================================================================*/
static void mstart(uint8_t x, uint8_t hi)
{
    uint8_t a, y;
    uint16_t ptr;

    TBLPTR_AT(1) = hi;  /* L6CD4 STA TBLPTR+1 */
    /* L6CD6 LDY #$00 */
    a = x;                                        /* L6CD8 TXA */
    CYC(13 + (a >= K_GAMOVR ? 2 : 1) + 17);       /* L6CD4-L6CDD, L6CDF LDA or BCC taken, L6CE1-L6CE8 */
    TEMP3_AT(1) = (a >= K_GAMOVR) ? 0x01 : 0x02;  /* L6CD9-L6CE1 CMP #GAMOVR / LDA #$02 / BCC MSTART_5 / LDA #$01 / STA TEMP3+1 - C SET SCALING */

    ptr = (uint16_t)(((uint16_t)TBLPTR_AT(1) << 8) | TBLPTR);
    {
        uint8_t b0 = cpu_rd(ptr);                  /* L6CE3 LDA (TBLPTR),Y (Y=0) */
        uint8_t b1 = cpu_rd((uint16_t)(ptr + 1));  /* L6CE5/L6CE6 INY / ORA (TBLPTR),Y (Y=1) - D IF NO POSITIONING NEEDED */
        if ((uint8_t)(b0 | b1) == 0) CYC(1);       /* L6CE8 BEQ taken */
        if ((uint8_t)(b0 | b1) != 0) {             /* L6CE8 BEQ MSTART_10 */
            CYC(6);    /* L6CEA JSR */
            vgcntr();  /* L6CEA JSR VGCNTR - C POSITION FOR MSG */
            CYC(22);   /* L6CED-L6CF5 (JSR VGVTR included) */
            {
                uint8_t xh = cpu_rd((uint16_t)(ptr + 1));  /* L6CED/L6CEF LDY #$01 / LDA (TBLPTR),Y */
                uint8_t al = cpu_rd(ptr);                  /* L6CF2/L6CF3 DEY / LDA (TBLPTR),Y (Y=0) */
                vgvtr(al, xh, 0x00);                       /* L6CF5 JSR VGVTR (Y still 0 from the DEY) */
            }
        }
    }

    /* MSTART_10 */
    CYC(9);               /* L6CF8 LDA / L6CFA JSR */
    vgsca1(TEMP3_AT(1));  /* L6CF8/L6CFA LDA TEMP3+1 / JSR VGSCA1 */
    CYC(7);               /* L6CFD-L6D01 */
    y = 0x02;             /* L6CFD LDY #$02 - C INIT LOOP */
    TEMP1 = 0x00;         /* L6CFF/L6D01 LDA #$00 / STA TEMP1 - C INIT VGLIST OFFSET */

    for (;;) {  /* MSTART_20 */
        uint8_t ch   = cpu_rd((uint16_t)(ptr + y));  /* L6D03 LDA (TBLPTR),Y - C GET CHARACTER REPRESENTATION */
        /* Integration fix (2026-09-18, NOTES_integration.md): "STA TEMP2"
         * was documented in the comment but never actually done - the RAM
         * store of the raw (unmasked) character byte into TEMP2 was
         * missing, even though the C code correctly used `ch & 0x80` in
         * place of the later "BIT TEMP2" test (behaviourally equivalent
         * for that branch, but TEMP2 itself never changed in the C port).
         * Found by lockstep: every MSGS check failed at $000A (TEMP2). */
        TEMP2 = ch;                           /* L6D05 STA TEMP2 */
        uint8_t code = (uint8_t)(ch & 0x7F);  /* L6D07 AND #$7F */
        uint16_t vg;

        y++;  /* L6D09 INY */
        TEMP3 = y;                                                            /* L6D0A STY TEMP3 - save the character pointer (Y is about
                                                                                * to be reused below for the VGLIST offset) */

        vg = VGLIST_ADDR;
        cpu_wr(vg + (uint16_t)TEMP1, cpu_rd((uint16_t)(A_VGMSGA + code)));      /* L6D0C-L6D12 TAX / LDA VGMSGA,X / LDY TEMP1 / STA (VGLIST),Y - GET CORRECT JSRL */
        TEMP1++;                                                                /* L6D14 INY */
        cpu_wr(vg + (uint16_t)TEMP1, cpu_rd((uint16_t)(A_VGMSGA + 1 + code)));  /* L6D15/L6D18 LDA VGMSGA+1,X / STA (VGLIST),Y */
        TEMP1++;                                                                /* L6D1A/L6D1B INY / STY TEMP1 */
        /* L6D1D LDY TEMP3 - restore the character pointer: already intact in
         * `y`, which the VGLIST-offset bookkeeping above never touched. */

        CYC((ch & 0x80) ? 55 : 56);  /* L6D03-L6D21, BPL taken while not the last character */
        if (!(ch & 0x80)) continue;  /* L6D1F/L6D21 BIT TEMP2 / BPL MSTART_20 - D IF NOT END OF STRING */
        break;
    }

    CYC(11);                      /* L6D23-L6D26 (JSR included) */
    vgadd((uint8_t)(TEMP1 - 1));  /* L6D23-L6D26 LDY TEMP1 / DEY / JSR VGADD - C UPDATE VGLIST */
    CYC(5);                       /* L6D29 LDA / L6D2B JMP */
    vgsca1(0x01);                 /* L6D29/L6D2B LDA #$01 / JMP VGSCA1 - C RESET SCALE */
}

/*==============================================================================
 * MSGS ($6C98) - output a message from the ENGLISH/GERMAN/FRENCH/SPANSH
 * table selected by OPTION's language bits, flattened per CALLING_NOTES.md
 * section 2's own suggestion (MSGS never itself returns to its caller on
 * real hardware - MSTART's RTS is the real return - so a single function
 * that switches internally and calls mstart() reproduces the same effect).
 * Caller: many (score/message display sites throughout MAIN/HISCRE/etc).
 *============================================================================*/
void msgs(uint8_t x)
{
    /* L6C98-L6CA1 LDA OPTION / ROL x4 / AND #$06 / TAY: proven carry-
     * independent by hand (the 4 ROLs' carry-in only ever lands in the bit
     * AND #$06 masks away) - see NOTES_disp.md for the full bit derivation.
     * Closed form: y = (OPTION & 0xC0) >> 5, giving 0/2/4/6. */
    uint8_t y = (uint8_t)((hw_option() & 0xC0) >> 5);

    CYC(36);                       /* L6C98-L6CAA: LNGTBL push/RTS dispatch */
    CYC(y == 0 ? 11 : 14);         /* ENGLISH (falls into MSTART) or the other legs' LDA/STA/LDA/JMP MSTART */
    switch (y) {  /* LNGTBL push-address-then-RTS dispatch */
    case 0:       /* ENGLISH */
        TBLPTR = cpu_rd((uint16_t)(A_ENGMSG + x));        /* L6CCC/L6CCF LDA ENGMSG,X / STA TBLPTR - C SET UP POINTER */
        mstart(x, cpu_rd((uint16_t)(A_ENGMSG + x + 1)));  /* L6CD1 LDA ENGMSG+1,X */
        break;
    case 2:  /* GERMAN */
        TBLPTR = cpu_rd((uint16_t)(A_GRMMSG + x));        /* L6CC1/L6CC4 LDA GRMMSG,X / STA TBLPTR */
        mstart(x, cpu_rd((uint16_t)(A_GRMMSG + x + 1)));  /* L6CC6 LDA GRMMSG+1,X */
        break;
    case 4:  /* FRENCH */
        TBLPTR = cpu_rd((uint16_t)(A_FRNMSG + x));        /* L6CB6/L6CB9 LDA FRNMSG,X / STA TBLPTR */
        mstart(x, cpu_rd((uint16_t)(A_FRNMSG + x + 1)));  /* L6CBB LDA FRNMSG+1,X */
        break;
    case 6:   /* SPANISH */
    default:  /* (AND #$06 never yields anything else, but a switch needs it) */
        TBLPTR = cpu_rd((uint16_t)(A_SPNMSG + x));        /* L6CAB/L6CAE LDA SPNMSG,X / STA TBLPTR */
        mstart(x, cpu_rd((uint16_t)(A_SPNMSG + x + 1)));  /* L6CB0 LDA SPNMSG+1,X */
        break;
    }
}

/*==============================================================================
 * INFO ($6D2E) - display the lives-remaining tank icons, current score and
 * high score.  Caller: MAIN family, HISCRE.
 *============================================================================*/
void info(void)
{
    CYC(6);                   /* L6D2E JSR */
    vgcntr();                 /* L6D2E JSR VGCNTR */
    CYC(15);                  /* L6D31-L6D39 (JSR included) */
    VGSIZE = 0x00;            /* L6D31/L6D33 LDY #$00 / STY VGSIZE */
    vgvtr(0x20, 0x5A, 0x00);  /* L6D35-L6D39 LDX #$5A / LDA #$20 / JSR VGVTR (Y still 0) */

    CYC(5);            /* L6D3C LDA / L6D3E BEQ */
    if (LIVES == 0) CYC(1);  /* L6D3E BEQ taken */
    if (LIVES != 0) {  /* L6D3C/L6D3E LDA LIVES / BEQ INFO_20 - D IF DEAD */
        CYC(9);        /* L6D40-L6D44 */
        uint8_t count = LIVES;  /* L6D40 STA TEMP1 */
        uint16_t vg = VGLIST_ADDR;
        uint8_t tlo = cpu_rd(A_TSYMBL);  /* L6D44/L6D47 LDX TSYMBL+1 / LDA TSYMBL (read once, constant through the loop) */
        uint8_t thi = cpu_rd((uint16_t)(A_TSYMBL + 1));
        int y = 0;  /* L6D42 LDY #$00 */

        for (;;) {  /* INFO_10 */
            cpu_wr(vg + (uint16_t)y, tlo);  /* L6D4A STA (VGLIST),Y - C OUTPUT 1 TANK PER LIFE */
            y++;                            /* L6D4D INY */
            cpu_wr(vg + (uint16_t)y, thi);  /* L6D4C/L6D4E TXA / STA (VGLIST),Y */
            y++;                            /* L6D50 INY */
            count--;                        /* L6D51 DEC TEMP1 */
            CYC(count != 0 ? 30 : 29);      /* L6D47-L6D53, BNE taken while TEMP1 != 0 */
            if (count != 0) continue;       /* L6D53 BNE INFO_10 */
            break;
        }
        y--;                /* L6D55 DEY */
        CYC(8);             /* L6D55 DEY / L6D56 JSR */
        vgadd((uint8_t)y);  /* L6D56 JSR VGADD */
    }

    /* INFO_20 */
    CYC(8);                      /* L6D59 LDX / L6D5B JSR */
    msgs(K_YSCORE);              /* L6D59/L6D5B LDX #YSCORE / JSR MSGS */
    CYC(12);                     /* L6D5E-L6D64 (JSR included) */
    vgvtr(0xD6, 0x00, 0x00);     /* L6D5E-L6D64 LDY #$00 / LDX #$00 / LDA #$D6 / JSR VGVTR */
    CYC(8);                      /* L6D67 LDA / L6D69 JSR */
    digt2s((uint8_t)A_HITS);     /* L6D67/L6D69 LDA #HITS / JSR DIGT2S */
    CYC(8);                      /* L6D6C LDX / L6D6E JSR */
    msgs(K_CHISCR);              /* L6D6C/L6D6E LDX #CHISCR / JSR MSGS */
    CYC(8);                      /* L6D71 LDA / L6D73 JSR */
    vgsca1(0x02);                /* L6D71/L6D73 LDA #$02 / JSR VGSCA1 */
    CYC(12);                     /* L6D76-L6D7C (JSR included) */
    vgvtr(0xD6, 0x00, 0x00);     /* L6D76-L6D7C LDY #$00 / LDX #$00 / LDA #$D6 / JSR VGVTR */
    CYC(14 + 8);                 /* L6D7F-L6D87, L6D89 LDA / L6D8B JSR */
    TEMP2       = HSCTBL;        /* L6D7F/L6D82 LDA HSCTBL / STA TEMP2 */
    TEMP2_AT(1) = HSCTBL_AT(1);  /* L6D84/L6D87 LDA HSCTBL+1 / STA TEMP2+1 */
    digt2s((uint8_t)A_TEMP2);    /* L6D89/L6D8B LDA #TEMP2 / JSR DIGT2S */
    CYC(5);                      /* L6D8E LDA / L6D90 JMP */
    vgsca1(0x01);                /* L6D8E/L6D90 LDA #$01 / JMP VGSCA1 - C RESET SCALE INFO */
}

/*==============================================================================
 * DIGT2S ($7B9E) - display a 2-digit-pair (4-hex-digit) number with leading-
 * zero suppression; falls into DIGTYS with Y=2.  Callers: DISTBL, INFO.
 *============================================================================*/
void digt2s(uint8_t a)
{
    CYC(2);           /* L7B9E LDY */
    digtys(a, 0x02);  /* L7B9E LDY #$02, falls into DIGTYS */
}

/*==============================================================================
 * DIGTYS ($7BA0) - display Y digit-pairs with leading-zero suppression;
 * falls into DIGITS with C=1 (suppress).  Callers: DISTBL.
 *============================================================================*/
void digtys(uint8_t a, uint8_t y)
{
    CYC(2);           /* L7BA0 SEC */
    digits(a, y, 1);  /* L7BA0 SEC, falls into DIGITS */
}

/*==============================================================================
 * DIGITS ($7BA1) - general BCD-digit-pair display via VGHEXZ, MSB-pair
 * first, with the caller's zero-suppression carry chained through.
 * Atari's header: ENTRY (C)=carry set for zero suppression, (A)=zero-page
 * address of the (Y) zero-page locations holding the number (LSB to MSB),
 * (Y)=number of zero-page locations to use (1-256).
 *============================================================================*/
void digits(uint8_t a, uint8_t y, int c)
{
    uint8_t x;

    /* L7BA1 PHP - the entry carry (c) is saved here and restored below,
     * unaffected by the ADC (which uses its own CLC) in between. */
    CYC(19);          /* L7BA1-L7BA9 */
    y--;              /* L7BA2 DEY */
    TEMP1_AT(1) = y;  /* L7BA3 STY TEMP1+1 */
    {
        unsigned cc = 0;                         /* L7BA5 CLC */
        a = adc6502(a, TEMP1_AT(1), &cc, NULL);  /* L7BA6 ADC TEMP1+1 */
    }
    /* L7BA8 PLP - restore c */
    x = a;  /* L7BA9 TAX - MSB of digits */

    for (;;) {  /* DIGITS_10 */
        uint8_t v;

        /* L7BAA PHP - save c (the LSRs below would otherwise clobber it) */
        CYC(28);                  /* L7BAA-L7BB4 (JSR VGHEXZ included) */
        TEMP1 = x;                /* L7BAB STX TEMP1 - save digit zp offset */
        v = cpu_rd((uint16_t)x);  /* L7BAD LDA $00,X */
        v = (uint8_t)(v >> 4);    /* L7BAF-L7BB2 LSR x4 */
        /* L7BB3 PLP - restore c */
        c = vghexz(v, c);  /* L7BB4 JSR VGHEXZ - FIRST DIGIT */

        CYC(5 + (TEMP1_AT(1) == 0 ? 2 : 1));  /* L7BB7 LDA / L7BB9 BNE, L7BBB CLC or BNE taken */
        if (TEMP1_AT(1) == 0) c = 0;  /* L7BB7/L7BB9/L7BBB LDA TEMP1+1 / BNE DIGITS_20 / CLC - display last digit (even 0) */

        /* DIGITS_20 */
        CYC(13);                  /* L7BBC-L7BC0 (JSR included) */
        x = TEMP1;                /* L7BBC LDX TEMP1 */
        v = cpu_rd((uint16_t)x);  /* L7BBE LDA $00,X */
        c = vghexz(v, c);         /* L7BC0 JSR VGHEXZ - SECOND DIGIT */

        x = TEMP1;                           /* L7BC3 LDX TEMP1 */
        x--;                                 /* L7BC5 DEX */
        TEMP1_AT(1)--;                       /* L7BC6 DEC TEMP1+1 */
        CYC((int8_t)TEMP1_AT(1) < 0 ? 12 + 6 : 13);  /* L7BC3-L7BC8, BPL taken, or L7BCA RTS */
        if ((int8_t)TEMP1_AT(1) < 0) break;  /* L7BC8 BPL DIGITS_10 (else loop) */
    }
    /* L7BCA RTS */

}
