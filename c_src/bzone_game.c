/* bzone_game.c - BZONE game logic: MAIN, EXPLDE, QWIKCK/SHRTCK, COLCHK,
 * SAUCHK, NEWLIF/EXINIT, MOTION, REACT, ROBOT (and family), FIREIT, BUZBOM,
 * SAUCMV, OBJOBJ, TR7CHK, ROBCHK, TANKCK, R2D3CK, ROB1 -
 * ../disasm/bzone_program_rom.asm, Atari's battlezone-main/BZONE.MAC.
 *
 * Conventions (TRANSLATING.md, SIGNATURES.md): one C function per Atari
 * label that is a real entry point; an "Lxxxx" address comment on each
 * statement or small group; push-address/RTS dispatches (MTAB) become a
 * switch; RAM cells go through state_defs.h exactly as the ROM stores them,
 * scratch cells included; hardware only through hw.h.
 *
 * Register-tracing hazards worth flagging up front (full detail in
 * NOTES_game.md):
 *  - MAIN's own frame-sync spin (`LSR SYNC`/`BCC MAIN`) is replaced by the
 *    seam call `hw_wait_frame()` (not yet in hw.h - TODO(seam) below).  The
 *    ROM's LSR is not a pure test: it also *shifts* SYNC, consuming an NMI's
 *    worth of "frame ready" state as its side effect - modelled as part of
 *    what the seam call does, since nothing else in this module ever reads
 *    SYNC.
 *  - MAIN's carry flag at L5042/L506B ("BCC MAIN_10"/"BCC MAIN_15") is
 *    *residual*, not from any comparison in between: every intervening
 *    instruction (BIT/LDA/BNE/BMI/BPL) leaves C untouched on the 6502, so
 *    the carry tested there is whatever the last ADC/SBC/CMP/shift left it
 *    at - which is `TIMOUT >= 4` on the 1-in-256 frame where `FRAME` just
 *    wrapped to 0 (the only place upstream that sets C on that path), and is
 *    otherwise VGSTAT's own internal pointer-add carry, which cannot be set
 *    this early in a freshly-started display list (VGLIST's low byte is
 *    only ~$0C at that point).  Modelled as one explicit `periodic_ok` flag,
 *    computed once, reused at both branch points - see main_pass() and
 *    NOTES_game.md "MAIN's residual-carry gate".
 *  - MAIN_42's "BCC MAIN_45" is the same kind of residual carry, this time
 *    genuinely meaningful: `CMP #$02` against PTURN survives two
 *    N/Z-only instructions (`LDA FIRECT`, `AND S_INTCT`) to reach the test.
 *    Modelled as an explicit `pturn_ge2` flag for the same reason.
 */
#include <stdint.h>
#include "state.h"
#include "hw.h"
#include "regs.h"
#include "game.h"
#include "bzone_game.h"
#include "bzone_disp.h"
#include "bzone_math.h"
#include "bzone_nmi.h"
#include "bzsoun.h"
#include "coin65.h"

/* ---------------------------------------------------------------------
 * TODO(seam): routines/headers owned by other agents that still do not
 * exist.  Remove each prototype here once it appears in the real header -
 * TRANSLATING.md section "Rules".
 * ----------------------------------------------------------------------
 * hw_wait_frame(): the seam's frame-wait function (PLAN.md/TRANSLATING.md
 * instruction: "the wait on SYNC going through the seam's frame-wait
 * function, see Tempest's alexec.c / PLAN.md section 1 hw_wait_frame()").
 * Not in hw.h yet (hw.h is frozen to another agent) - same signature as
 * Tempest's C:\Source2026\Tempest Dissasembly\c_src\hw.h.  Battlezone's
 * version additionally has to perform the actual `LSR SYNC` shift(s) that
 * make up the ROM's own busy-wait, since SYNC is otherwise unread by this
 * module - see NOTES_game.md.
 */
void hw_wait_frame(void);

/* ----------------------------------------------------------------------- */

/* ---------------------------------------------------------------------
 * Bit/carry helpers (this module's own, TRANSLATING.md "Conventions": write
 * small static inline helpers for exact 6502 flag behaviour).  adc6502/
 * sbc6502 already live in state.h (shared); CMP/ASL/ROL/LSR do not change
 * A the way ADC/SBC do, so they get their own tiny helpers here.  Each takes
 * the carry in *c and (for cmp6502) an optional *z, exactly mirroring the
 * real flag effects - no widening, no shortcuts, so a routine that threads
 * carry through several of these across intervening non-flag instructions
 * (LDA/STA/TXA/BPL/BMI/BEQ/BNE/BIT all leave C alone on a 6502) gets the
 * identical result the ROM's own residual-carry hazards depend on.
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

/* MAIN ($5000): power-on entry point into the frame loop.  BZSTST's own
 * one-time setup (RAMTST/BATINT/...) is outside this milestone's scope; this
 * module has nothing else that runs only once, but the split is kept to
 * match the Tempest template (PLAN.md, TRANSLATING.md). */
void main_init(void)
{
    /* nothing yet owned by this module runs once before the first pass */
}

/* main_pass(): the ONE trampoline loop - one real trip around the frame
 * loop per call, matching Tempest's mainln()/mainln_pass() convention
 * (PLAN.md).  Acts on the pass_signal each `_body()` returns instead of
 * letting them tail-call each other - see bzone_game.h's "Integration
 * fix" comment for the full design and why (a real stack-overflow crash,
 * NOTES_integration.md).  Returns once a "JMP MAIN" site anywhere in the
 * chain yields (PASS_SIG_YIELD) - i.e. exactly one pass, then control
 * returns to whoever is driving frames (the harness, or a test). */
void main_pass(void)
{
    pass_signal sig = PASS_SIG_MAIN;
    for (;;) {
        switch (sig) {
        case PASS_SIG_MAIN:   sig = bz_main_body(); break;
        case PASS_SIG_BEND:   sig = b_end_body();   break;
        case PASS_SIG_WNSHLD: sig = wnshld_body();  break;
        case PASS_SIG_CKSCOR: sig = ckscor_body();  break;
        case PASS_SIG_HISCRE: sig = hiscre_body();  break;
        case PASS_SIG_DISTBL: sig = distbl_body();  break;
        case PASS_SIG_YIELD:  return;
        default:              return;  /* unreachable; silences /W4 */
        }
    }
}

/* bz_main()/b_end(): standalone entries matching every existing caller's
 * signature (lockstep's own adapters included) - each just runs its own
 * `_body()` once and discards the returned signal, so calling one of
 * these directly executes exactly that routine's own translated code and
 * stops exactly where the ROM would jump elsewhere (never recursing) -
 * see bzone_game.h's "Integration fix" comment. */
void bz_main(void) { (void)bz_main_body(); }
void b_end(void)   { (void)b_end_body(); }

/* MAIN ($5000): one trip around the frame loop, from the SYNC frame-wait
 * through the tail `JMP MAIN` (MAINLINE_NOTES.md "Frame sync" / "Building
 * the display list").  Runs the whole frame: 3-D pipeline, HUD/messages,
 * then the AI/collision tail for the *next* frame's geometry.  Named
 * `bz_main_body()` (not `main_pass()`) because WNSHLD/CKSCOR/HISCRE/
 * DISTBL - all display-owned - reach MAIN via a real ROM `JMP`, modelled
 * as `return PASS_SIG_MAIN`-dispatchable-by-the-trampoline (SIGNATURES.md's
 * display section; bzone_game.h's "Integration fix" comment).
 */
pass_signal bz_main_body(void)
{
    uint8_t a, x;
    int periodic_ok = 0;  /* see the header comment: MAIN's residual-carry gate */

    hw_wait_frame();  /* L5000 LSR SYNC / L5002 BCC MAIN (the seam charges the spin) */
    CYC(6);           /* L5004 JSR */
    volcno();         /* L5004 JSR VOLCNO */

    /* MAIN_5 */
    hw_wait_bufrdy();                           /* L5007 LDX BUFRDY / L5009 BEQ MAIN_5 - NMI-owned; the seam charges the spin */
    x = BUFRDY;                                 /* L5007 LDX BUFRDY */
    CYC(16);                                    /* L500B-L5013 (JSR VGSCA1 included) */
    VGLIST = (uint8_t)((A_VECRAM & 0xFF) + 2);  /* L500B-L500D */
    g.ram[A_VGLIST + 1] = x;                    /* L500F STX VGLIST+1 */
    vgsca1(0x01);                               /* L5011-L5013 */
    CYC(6);                                     /* L5016 JSR */
    vgcntr();                                   /* L5016 */
    CYC(12);                                    /* L5019-L501E (JSR included) */
    vgvtr(0x81, 0x81, 0x00);                    /* L5019-L501E: A=X=$81, Y=0 (LDA #$81/TAX/LDY #0) */
    CYC(8);                                     /* L5021-L5023 (JSR included) */
    vgstat(0x00, 0x03);                         /* L5021-L5023: Y left at 3 by VGVTR's internal VGWAI1 tail */
    CYC(7);                                     /* L5026 INC / L5028 BNE */
    FRAME++;                                    /* L5026 INC FRAME */
    if (FRAME == 0) {                           /* L5028 BNE MAIN_6 */
        CYC(10);                         /* L502A-L502E */
        TIMOUT++;                        /* L502A */
        periodic_ok = (TIMOUT >= 0x04);  /* L502C-L502E: CMP #$04 -> carry = TIMOUT>=4 */
    } else CYC(1);                       /* L5028 BNE taken */
    /* else: periodic_ok stays 0 - carry here is VGSTAT's own residual (see
     * header comment), always clear this early in a fresh display list */

    /* MAIN_6 */
    CYC(5);              /* L5030 BIT / L5032 BPL */
    if (GOVER & 0x80) {  /* L5030-L5032 */
        CYC(6);                  /* L5034 JSR */
        bigwnd();                /* L5034 */
        CYC(3);                  /* L5037 JMP */
        return PASS_SIG_HISCRE;  /* L5037 JMP HISCRE */
    }
    CYC(1);  /* L5032 BPL taken */

    /* MAIN_1 */
    CYC(5);                                    /* L503A BIT / L503C BMI */
    if (ATRACT & 0x80) { CYC(1); goto MAIN_3; }  /* L503A-L503C */
    CYC(5);                                    /* L503E LDA / L5040 BNE */
    if (CRACK != 0) { CYC(1); goto MAIN_3; }   /* L503E-L5040 ;ALLOW ENDING SEQUENCE TO FINISH */
    CYC(2);                                    /* L5042 BCC */
    if (!periodic_ok) { CYC(1); goto MAIN_10; }  /* L5042 BCC MAIN_10 */
    CYC(6);                                    /* L5044 JSR */
    tankck();                                  /* L5044 */
    CYC(20);                                   /* L5047-L5055 */
    x = 0x03;                                  /* L5047 */
    a = (uint8_t)(DSPLAY ^ 0x80);              /* L5049-L504C */
    DSPLAY = a;                                /* L504E */
    TIMOUT = x;                                /* L5051 */
    FRAME = a;                                 /* L5053 */
    if (a & 0x80) { CYC(1); goto MAIN_10; }    /* L5055 ;D IF GOING INTO DISPLAY */
    CYC(5);                                    /* L5057 BIT / L5059 BMI */
    if (ATRACT & 0x80) { CYC(1); goto MAIN_15; }  /* L5057-L5059 ;C CLOSE WINDOW */
    CYC(6);                                    /* L505B LDA / L505D STA */
    LOGO = 0xFF;                               /* L505B-L505D ;C SET UP FOR LOGO */
    goto MAIN_10;

MAIN_3:
    CYC(2);                                     /* L506B BCC */
    if (!periodic_ok) { CYC(1); goto MAIN_15; }  /* L506B BCC MAIN_15 */
    CYC(5);                                     /* L506D BIT / L506F BMI */
    if (!(R2D3FL & 0x80)) {                     /* L506D-L506F */
        CYC(6);    /* L5071 JSR */
        r2d3ck();  /* L5071 */
    } else CYC(1);  /* L506F BMI taken */
    goto MAIN_15;

MAIN_10:
    CYC(6);               /* L5060 BIT / L5063 BPL */
    if (DSPLAY & 0x80) {  /* L5060-L5063 */
        CYC(6);                  /* L5065 JSR */
        bigwnd();                /* L5065 */
        CYC(3);                  /* L5068 JMP */
        return PASS_SIG_DISTBL;  /* L5068 JMP DISTBL */
    }
    CYC(1);  /* L5063 BPL taken */

MAIN_15:
    CYC(6);                   /* L5074 JSR */
    vgcntr();                 /* L5074 ;C SET UPPER WINDOW */
    CYC(12);                  /* L5077-L507D (JSR included) */
    vgvtr(0x7F, 0x30, 0x00);  /* L5077-L507D */
    CYC(8);                   /* L5080-L5082 (JSR included) */
    vgstat(0x02, 0x03);       /* L5080-L5082: Y left at 3 again */
    CYC(5);                   /* L5085 BIT / L5087 BMI */
    if (!(ATRACT & 0x80)) {   /* L5085-L5087 ;D IF GAME */
        CYC(8);          /* L5089 LDX / L508B JSR */
        msgs(K_COPYRT);  /* L5089-L508B ;C COPYRIGHT 1980 */
    } else CYC(1);       /* L5087 BMI taken */

    /* MAIN_16 */
    CYC(6);                /* L508E JSR */
    mounts();              /* L508E ;C DRAW MOUNTAINS & HORIZON */
    CYC(12);               /* L5091-L5097 */
    ACTION--;              /* L5091 */
    if (FTIMER != 0xFF) {  /* L5093-L5097 */
        CYC(5);    /* L5099 INC */
        FTIMER++;  /* L5099 */
    } else CYC(1);  /* L5097 BEQ taken */

    /* MAIN_18 */
    CYC(11);                                          /* L509B-L50A2 */
    hw_mb_write((uint8_t)(A_DIVCYC - 0x1860), 0x0A);  /* L509B-L509D ;C INIT DIVIDER (Mathbox register, not RAM) */
    SAVE0 = 0x00;                                     /* L50A0-L50A2 ;C INIT INDEX */
    CYC(6);                                           /* L50A4 JSR */
    rotate();                                         /* L50A4 ;C CHOOSE OBJECTS IN VIEW */
    CYC(6);                                           /* L50A7 JSR */
    battle();                                         /* L50A7 */

MAIN_20:
    CYC(9);                                                /* L50AA-L50AF */
    x = SAVE0;                                             /* L50AA */
    if ((int8_t)PTBLO2_AT(x) < 0) { CYC(1); goto MAIN_30; }  /* L50AC-L50AF ;D IF END OF TABLE */
    CYC(6);                                                /* L50B1 JSR */
    pntput(x);                                             /* L50B1 ;C GENERATE POINT TABLE */
    CYC(13);                                               /* L50B4-L50B9 (JSR included) */
    x = SAVE0;                                             /* L50B4 ;C GET OBJECT # IN A */
    a = PTBLO2_AT(x);                                      /* L50B6 */
    draw(a);                                               /* L50B9 ;C GENERATE VECTOR INSTRUCTIONS */
    SAVE0 = (uint8_t)(SAVE0 + 2);                          /* L50BC-L50C0 INC SAVE0 twice / BNE MAIN_20 */
    CYC(SAVE0 != 0 ? 13 : 12);                             /* L50BC-L50C0, BNE taken while SAVE0 != 0 */
    if (SAVE0 != 0) goto MAIN_20;

MAIN_30:
    CYC(5);            /* L50C2 LDA / L50C4 BEQ */
    if (CRACK != 0) {  /* L50C2-L50C4 */
        CYC(3);                  /* L50C6 JMP */
        return PASS_SIG_WNSHLD;  /* L50C6 JMP WNSHLD */
    }
    CYC(1);  /* L50C4 BEQ taken */

    /* MAIN_31 */
    CYC(6);    /* L50C9 JSR */
    bigwnd();  /* L50C9 ;C OPEN WINDOW */
    CYC(6);    /* L50CC JSR */
    dradar();  /* L50CC */

    /* MAIN_35 */
    CYC(6);       /* L50CF JSR */
    a = track();  /* L50CF ;D IF ROBOT IN SIGHTS */
    CYC(12);      /* L50D2-L50D9 */
    a ^= 0x80;    /* L50D2 */
    {
        unsigned c = 1;                    /* L50D4 SEC */
        a = sbc6502(a, TANGLE, &c, NULL);  /* L50D5 SBC TANGLE */
    }
    SAVE2 = a;            /* L50D7 */
    if ((int8_t)a < 0) {  /* L50D9 BPL MAIN_41 */
        CYC(6);                         /* L50DB-L50DE */
        a = (uint8_t)((a ^ 0xFF) + 1);  /* L50DB-L50DE: negate (two's complement) */
    } else CYC(1);                      /* L50D9 BPL taken */

    /* MAIN_41 */
    CYC(9);                /* L50E0 STA / L50E2 BIT / L50E5 BMI */
    PTURN = a;             /* L50E0 */
    if (!(LOGO & 0x80)) {  /* L50E2-L50E5 */
        int pturn_ge2 = (PTURN >= 0x02);  /* L50E7 CMP #$02 */
        int skip = 0;
        CYC(7);                           /* L50E7-L50EB */
        if (FIRECT != 0) {                /* L50E9-L50EB BEQ MAIN_42 */
            CYC(7);                                         /* L50ED-L50F1 */
            if ((S_INTCT & 0x20) == 0) { CYC(1); skip = 1; }  /* L50ED-L50F1 BEQ MAIN_50 */
        } else CYC(1);                                      /* L50EB BEQ taken */
        if (!skip) {
            /* MAIN_42 */
            uint16_t addr;
            if (pturn_ge2) {
                CYC(2 + 7);       /* L50F3 BCC, L50F5-L50F9 (BNE taken) */
                addr = A_XCROSS;  /* MAIN_43 */
            } else {
                CYC(3 + 4);       /* L50F3 BCC taken, L50FB-L50FD */
                addr = A_XCROS1;  /* MAIN_45 */
            }
            CYC(6);                                                /* L50FF JSR */
            vgjsrl((uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF));  /* MAIN_48 */
        }
    } else CYC(1);  /* L50E5 BMI taken */

    /* MAIN_50 */
    CYC(5);                                                 /* L5102 LDA / L5104 BNE */
    if (COLFLG_AT(2) != 0) { CYC(1); goto MAIN_53; }        /* L5102-L5104 BNE MAIN_53 - enemy exploding: no direction message
                                                              * (fix: this used to fall into MAIN_51 - NOTES_integration.md #17) */
    {
        CYC(5);                  /* L5106 BIT / L5108 BMI */
        if (!(ATRACT & 0x80)) {  /* L5106-L5108 */
            uint8_t pg = (uint8_t)(VGLIST_AT(1) & 0xF8);  /* L510A-L510C */
            CYC(17 + 54 * 9 + 53 + 12);                   /* L510A-L5116, the 54-pass EOR loop (BPL taken 53 times), L511D-L5124 */
            g.ram[A_TBLPTR + 1] = pg;                     /* L510E */
            TBLPTR = 0x02;                                /* L5110-L5112 */
            {
                int y;
                uint8_t sum = 0;  /* L5116 */
                uint16_t base = (uint16_t)((pg << 8) | TBLPTR);
                for (y = 0x35; y >= 0; y--)  /* L5114, L511A-L511B */
                    sum ^= cpu_rd((uint16_t)(base + y));  /* L5118 EOR (TBLPTR),Y */
                TRAP2 = (uint8_t)(sum >> 1);  /* L511D-L511E */
            }
            if (LOGO & 0x80) {  /* L5121-L5124 */
                CYC(1);                /* L5124 BMI taken */
                return PASS_SIG_BEND;  /* L5124 JMP-equivalent: falls straight to B_END */
            }
        } else CYC(1);  /* L5108 BMI taken */
    }

    /* MAIN_51 */
    CYC(7);              /* L5126-L512A */
    if (FRAME & 0x02) {  /* L5126-L512A */
        CYC(7);               /* L512C-L5130 */
        if (PTURN >= 0x16) {  /* L512C-L5130 */
            CYC(8);                  /* L5132 LDX / L5134 JSR */
            msgs(K_ETO);             /* L5132-L5134 */
            CYC(9);                  /* L5137-L513D */
            x = K_REAR;              /* L5137 */
            if (!(0x6B >= PTURN)) {  /* L5139-L513D: CMP PTURN/BCC MAIN_52 -> branch (keep REAR) iff PTURN>0x6B */
                CYC(1);  /* L513D BCC taken */
            } else {
                CYC(7);              /* L513F-L5143 */
                x = K_LEFT;          /* L513F */
                if (SAVE2 & 0x80) {  /* L5141-L5143 BPL MAIN_52 */
                    CYC(2);       /* L5145 LDX */
                    x = K_RIGHT;  /* L5145 */
                } else CYC(1);    /* L5143 BPL taken */
            }
            /* MAIN_52 */
            CYC(6);   /* L5147 JSR */
            msgs(x);  /* L5147 */
        } else CYC(1);  /* L5130 BCC taken */
    } else CYC(1);      /* L512A BEQ taken */

MAIN_53:
    CYC(5);             /* L514A LDA / L514C BEQ */
    if (OBJCOL != 0) {  /* L514A-L514C */
        CYC(7);                 /* L514E-L5152 */
        if (!(FRAME & 0x04)) {  /* L514E-L5152 */
            CYC(8);          /* L5154 LDX / L5156 JSR */
            msgs(K_BLOCKD);  /* L5154-L5156 */
        } else CYC(1);       /* L5152 BNE taken */
    } else CYC(1);           /* L514C BEQ taken */

    return PASS_SIG_BEND;  /* L5159 falls into B_END */
}

/* B_END ($5159): MAIN's mid-routine label - tank stats, game-over/finish
 * text, FINISH's TRAP1 refresh, the super-bonus short-circuit, then the
 * collision/AI tail (COLCHK x4, QWIKCK, ROBOT, SAUCMV, BOUND, MOTION)
 * before looping back to MAIN (`JMP MAIN`).  Reached both by falling out
 * of bz_main_body() above and directly by WNSHLD/CKSCOR (JMP B_END in the
 * ROM - SIGNATURES.md's display section). */
pass_signal b_end_body(void)
{
    uint8_t a, x;

    CYC(6);            /* L5159 JSR */
    info();            /* L5159 ;C DISPLAY STATS */
    CYC(5);            /* L515C LDA / L515E BEQ */
    if (GOVER != 0) {  /* L515C-L515E */
        CYC(5);            /* L5160 LDA / L5162 BNE */
        if (CRACK == 0) {  /* L5160-L5162 */
            CYC(6);                /* L5164 BIT / L5167 BMI */
            if (!(LOGO & 0x80)) {  /* L5164-L5167 */
                CYC(8);               /* L5169 LDX / L516B JSR */
                msgs(K_GAMOVR);       /* L5169-L516B ;C OUTPUT GAME OVER */
                CYC(5);               /* L516E LDA / L5170 BNE */
                if (S_S_CRDT == 0) {  /* L516E-L5170 ;D IF NO COINS NEEDED */
                    CYC(7);                /* L5172-L5176 */
                    if (S_CMODE & 0x03) {  /* L5172-L5176 */
                        uint8_t idx = (uint8_t)(((S_CMODE & 0x03) << 1) + (K_MODE1 - 2));  /* L5178-L517B ;C OUTPUT COIN MODE */
                        CYC(12);         /* L5178-L517C (JSR included) */
                        msgs(idx);       /* L517C */
                        CYC(8);          /* L517F LDX / L5181 JSR */
                        msgs(K_CONPLY);  /* L517F-L5181 */
                        CYC(5);          /* L5184 LDX / L5186 BNE (taken) */
                        x = K_INSCON;    /* L5184 ;C INSERT COIN */
                        goto B_END_13;   /* L5186 BNE B_END_13 (A=INSCON<>0, always taken) */
                    }
                    CYC(1);  /* L5176 BEQ taken */
                } else CYC(1);  /* L5170 BNE taken */
                CYC(2);         /* L5188 LDX */
                x = K_PRSTRT;   /* B_END_10, L5188 */
B_END_13:
                CYC(5);                /* L518A BIT / L518C BVC */
                if (S_INTCT & 0x40) {  /* L518A-L518C: BIT/BVC tests V = bit6 */
                    CYC(6);   /* L518E JSR */
                    msgs(x);  /* L518E */
                } else CYC(1);  /* L518C BVC taken */
            } else CYC(1);  /* L5167 BMI taken */
        } else CYC(1);  /* L5162 BNE taken */
    } else CYC(1);  /* L515E BEQ taken */

    /* B_END_15 */
    CYC(6);              /* L5191 JSR */
    finish();            /* L5191 */
    CYC(6);              /* L5194 BIT / L5197 BMI */
    if (SUPER & 0x80) {  /* L5194-L5197 */
        /* B_END_30 */
        CYC(1 + 11);   /* L5197 BMI taken, L51C2-L51C8 */
        a = TRAP2;     /* L51C2 */
        a >>= 1;       /* L51C5 LSR */
        a |= POINT;    /* L51C6 ORA POINT */
        if (a == 0) {  /* L51C8 BNE B_END_20 */
            CYC(20);          /* L51CA-L51D7 (BNE taken) */
            SUPER = 0;        /* L51CA STA SUPER (A=0 here) */
            SNDWRD |= K_LOX;  /* L51CD-L51D1 */
            EXPCNT = 0xFF;    /* L51D3-L51D5 */
        } else CYC(1);        /* L51C8 BNE taken */
        /* B_END_20 */
        CYC(3);                 /* L51BF JMP MAIN */
        return PASS_SIG_YIELD;  /* L51BF/L51D7 JMP MAIN */
    }

    /* B_END_18 */
    CYC(5);        /* L5199 LDA / L519B STA */
    SAVE3 = 0x04;  /* L5199-L519B */
    do {
        CYC(6);    /* L519D JSR */
        colchk();  /* L519D */
        CYC(6);    /* L51A0 JSR */
        shupdt();  /* L51A0 */
        SAVE3--;   /* L51A3 */
        CYC(SAVE3 != 0 ? 8 : 7);  /* L51A3 DEC / L51A5 BNE (taken while SAVE3 != 0) */
    } while (SAVE3 != 0);  /* L51A5 */

    /* L51A7-L51A9: BMI B_END_17 (skip TR7CHK, always fire QWIKCK) - else run
     * TR7CHK and only fire QWIKCK if it returns carry set (L51AE BCC B_END_19
     * skips QWIKCK on carry clear).  `||` short-circuits exactly like the
     * BMI: tr7chk() is not called when R2D3FL bit7 is set. */
    CYC(5);  /* L51A7 BIT / L51A9 BMI */
    {
        int fire;
        if (R2D3FL & 0x80) {
            CYC(1);  /* L51A9 BMI taken */
            fire = 1;
        } else {
            CYC(6);          /* L51AB JSR */
            fire = tr7chk();
            CYC(fire ? 2 : 3);  /* L51AE BCC (taken on carry clear) */
        }
        if (fire) {  /* B_END_17 */
            CYC(6);    /* L51B0 JSR */
            qwikck();  /* L51B0 */
        }
    }

    /* B_END_19 */
    CYC(6);    /* L51B3 JSR */
    robot();   /* L51B3 */
    CYC(6);    /* L51B6 JSR */
    saucmv();  /* L51B6 */
    CYC(6);    /* L51B9 JSR */
    bound();   /* L51B9 */
    CYC(6);    /* L51BC JSR */
    motion();  /* L51BC ;C UPDATE TANK POSITION */
    /* B_END_20 */
    CYC(3);                 /* L51BF JMP MAIN */
    return PASS_SIG_YIELD;  /* L51BF JMP MAIN */
}

/* EXPLDE ($5B89): update every active explosion-piece slot's X/Y/Z position,
 * apply gravity to its Z velocity, spin its orientation byte, and append the
 * survivor to PTBLX2/PTBLY2/PTBLO2 via ROTPNT.  When every slot has finally
 * gone inactive, reset COLFLG+2 and call ROBCHK.
 *
 * ENTRY/EXIT: y = the running PTBLX2/PTBLY2/PTBLO2 write index (ROTATE's own
 * - its only call site always passes 0: `LDY #$00` at ROTATE's own entry is
 * never touched before `JSR EXPLDE`).  X is NOT a real input - EXPLDE loads
 * X=$0A itself (L5B89) before ever reading it.
 *
 * Slot count hazard (see NOTES_game.md "EXPLDE loop bound"): the loop below
 * only visits X = $0A,$08,...,$00 - SIX slots (byte offsets 0/2/4/6/8/10),
 * not all EXNOBJ=8.  This is deliberate, not a bug: R2D3CK ($6A22) reuses
 * the other two slots' Z-position cells (EXPOSZ+$C/+$D) as the buzz-bomb's
 * own Z-depth ("STARTZ"), not as an explosion piece, so EXPLDE's own
 * per-frame physics loop must never touch them.
 */
uint8_t explde(uint8_t y)
{
    uint8_t x, a;
    unsigned c;

    CYC(2);    /* L5B89 LDX */
    x = 0x0A;  /* L5B89 LDX #$0A */

    /* EXPLDE_1 (L5B8B): the `for` loop below IS EXPLDE_1 - reaching its
     * bottom without `break` is the ROM's `JMP EXPLDE_1` (L5C41). */
    for (;;) {
        CYC(8);                /* L5B8B-L5B8F */
        a = EXPOSZ_AT(x + 1);  /* L5B8B LDA EXPOSZ+1,X */
        c = 0;                                          /* L5B8E CLC - real: carry-in for the X-add
                                                          * chain below IF this piece is active (falls
                                                          * through); has no effect on the BPL itself */
        if ((int8_t)a < 0) { CYC(3); goto EXPLDE_30; }  /* L5B8F/L5B91: inactive piece, skip to the tail (JMP) */
        CYC(1 + 75 + 5 + 17 + 26 + 6);      /* BPL taken, L5B94-L5BC7, sign-extend (5 either way), L5BCF-L5BD5, L5BD7-L5BE5, L5BE8 LDA / L5BEB BPL */

        /* EXPLDE_2: X position += X velocity (16-bit), also latch to the
         * Mathbox's XMATRX input register. */
        a = EXPOSX_AT(x);                                                /* L5B94 */
        a = adc6502(a, cpu_rd((uint16_t)(A_EXPTBX + x)), &c, NULL);      /* L5B97 ADC EXPTBX,X */
        EXPOSX_AT(x) = a;                                                /* L5B9A */
        hw_mb_write((uint8_t)(A_XMATRX - 0x1860), a);                    /* L5B9D STA XMATRX */
        a = EXPOSX_AT(x + 1);                                            /* L5BA0 */
        a = adc6502(a, cpu_rd((uint16_t)(A_EXPTBX + x + 1)), &c, NULL);  /* L5BA3 */
        EXPOSX_AT(x + 1) = a;                                            /* L5BA6 */
        hw_mb_write((uint8_t)(A_XMATRX + 1 - 0x1860), a);                /* L5BA9 STA XMATRX+1 */

        /* Y position += Y velocity (16-bit); only the low byte is latched to
         * YMATRX - the ROM never writes YMATRX+1 (asymmetric with X, verified
         * against the listing; translated faithfully). */
        c = 0;                                                           /* L5BAC CLC ;Y */
        a = EXPOSY_AT(x);                                                /* L5BAD */
        a = adc6502(a, cpu_rd((uint16_t)(A_EXPTBY + x)), &c, NULL);      /* L5BB0 */
        EXPOSY_AT(x) = a;                                                /* L5BB3 */
        hw_mb_write((uint8_t)(A_YMATRX - 0x1860), a);                    /* L5BB6 STA YMATRX */
        a = EXPOSY_AT(x + 1);                                            /* L5BB9 */
        a = adc6502(a, cpu_rd((uint16_t)(A_EXPTBY + x + 1)), &c, NULL);  /* L5BBC */
        EXPOSY_AT(x + 1) = a;                                            /* L5BBF */

        /* ZVELOC,X sign-extended and x4-scaled into TEMP1(lo)/TEMP1+1(hi):
         * the classic double ASL/ROL widen-by-shift idiom (same shape as
         * VGVTR1's x4 rescale). */
        a = ZVELOC_AT(x);                  /* L5BC2 */
        TEMP1 = a;                         /* L5BC5 */
        if ((int8_t)a < 0) goto EXPLDE_5;  /* L5BC7 BMI */
        a = 0x00;                          /* L5BC9 */
EXPLDE_5:
        if (a == 0) goto EXPLDE_6;  /* L5BCB BEQ */
        a = 0xFF;                   /* L5BCD */
EXPLDE_6:
        TEMP1 = asl6502(TEMP1, &c);  /* L5BCF ASL TEMP1 */
        a = rol6502(a, &c);          /* L5BD1 ROL */
        TEMP1 = asl6502(TEMP1, &c);  /* L5BD2 ASL TEMP1 */
        a = rol6502(a, &c);          /* L5BD4 ROL */
        TEMP1_AT(1) = a;             /* L5BD5 STA TEMP1+1 */

        /* Z position += the x4-scaled ZVELOC (16-bit). */
        a = EXPOSZ_AT(x);                            /* L5BD7 */
        c = 0;                                       /* L5BDA CLC */
        a = adc6502(a, TEMP1, &c, NULL);             /* L5BDB */
        EXPOSZ_AT(x) = a;                            /* L5BDD */
        a = TEMP1_AT(1);                             /* L5BE0 */
        a = adc6502(a, EXPOSZ_AT(x + 1), &c, NULL);  /* L5BE2 */
        EXPOSZ_AT(x + 1) = a;                        /* L5BE5 */

        /* ZVELOC,X drifts toward GRAVTY (falls faster) unless already
         * saturated near $80.  Real residual-carry hazard: on the "ZVELOC
         * already negative" BPL-taken path, the ADC's carry-in below is
         * whatever L5BE2's ADC left, NOT a fresh CLC/SEC - translated by
         * simply not touching `c` on that path, same as the ROM. */
        a = ZVELOC_AT(x);                     /* L5BE8 */
        if (!((int8_t)a < 0)) { CYC(1); goto EXPLDE_8; }  /* L5BEB BPL */
        CYC(4);                               /* L5BED CMP / L5BEF BCC */
        {
            int z;
            cmp6502(a, 0x85, &c, &z);  /* L5BED CMP #<[$80-GRAVTY+1] = #$85 */
            (void)z;
        }
        if (!c) { CYC(1); goto EXPLDE_10; }  /* L5BEF BCC */
EXPLDE_8:
        CYC(7);                          /* L5BF1 ADC / L5BF3 STA */
        a = adc6502(a, 0xFC, &c, NULL);  /* L5BF1 ADC #<GRAVTY ($FFFC's low byte = $FC = -4) */
        ZVELOC_AT(x) = a;                /* L5BF3 */
EXPLDE_10:
        /* Per-piece orientation spin: alternates add/subtract by bit1 of X
         * (i.e. every other piece slot), threading carry through a literal
         * PHP/PLP save exactly as the ROM does (`saved_c` below). */
        CYC(23);             /* L5BF6-L5C00 */
        a = x;               /* L5BF6 TXA */
        a = lsr6502(a, &c);  /* L5BF7 LSR */
        a = lsr6502(a, &c);  /* L5BF8 LSR */
        {
            unsigned saved_c = c;            /* L5BF9 PHP */
            a = x;                           /* L5BFA TXA */
            a = asl6502(a, &c);              /* L5BFB ASL */
            a = asl6502(a, &c);              /* L5BFC ASL */
            a = adc6502(a, 0x03, &c, NULL);  /* L5BFD ADC #$03 */
            c = saved_c;                     /* L5BFF PLP */
        }
        if (!c) { CYC(1 + 4); goto EXPLDE_20; }  /* L5C00 BCC (taken) + L5C0C ADC */
        CYC(13);                          /* L5C02-L5C09 */
        TEMP1 = a;                        /* L5C02 STA TEMP1 */
        a = EXPOSO_AT(x);                 /* L5C04 */
        a = sbc6502(a, TEMP1, &c, NULL);  /* L5C07 SBC TEMP1 */
        goto EXPLDE_25;                   /* L5C09 JMP */
EXPLDE_20:
        a = adc6502(a, EXPOSO_AT(x), &c, NULL);  /* L5C0C ADC EXPOSO,X */
EXPLDE_25:
        CYC(10 + 13);                     /* L5C0F-L5C12, L5C15-L5C1C */
        EXPOSO_AT(x) = a;                 /* L5C0F STA EXPOSO,X */
        PTBLO2_AT((uint8_t)(y + 1)) = a;  /* L5C12 STA PTBLO2+1,Y */

        /* Pick the PTBLO2 object-type byte: tank vs. R2D3 buzz-bomb piece,
         * and (for the tank's radar-object slot only) whether it should be
         * the TR7's radar-exempt variant. */
        a = x;                                 /* L5C15 TXA */
        a = lsr6502(a, &c);                    /* L5C16 LSR - object # base */
        c = 0;                                 /* L5C17 CLC */
        a = adc6502(a, 0x10, &c, NULL);        /* L5C18 ADC #$10 */
        if (!(R2D3FL & 0x80)) { CYC(1); goto EXPLDE_26; }  /* L5C1A BIT R2D3FL / L5C1C BPL: bit7 clear = tank */
        CYC(4);                                /* L5C1E ADC / L5C20 BNE */
        a = adc6502(a, 0x08, &c, NULL);        /* L5C1E ADC #$08 (R2D3 piece) */
        if (a != 0) { CYC(1); goto EXPLDE_28; }  /* L5C20 BNE */
EXPLDE_26:
        {
            int z;
            CYC(4);                    /* L5C22 CMP / L5C24 BNE */
            cmp6502(a, 0x13, &c, &z);  /* L5C22 CMP #$13 */
            if (!z) { CYC(1); goto EXPLDE_28; }  /* L5C24 BNE */
        }
        CYC(9);                         /* L5C26 STA / L5C28 JSR */
        TEMP1 = a;                      /* L5C26 STA TEMP1 (a == $13 here) */
        if (!tr7chk()) { CYC(3); goto EXPLDE_27; }  /* L5C28/L5C2B JSR TR7CHK / BCC */
        CYC(7);                         /* L5C2B BCC / L5C2D INC */
        TEMP1++;                        /* L5C2D INC TEMP1 */
EXPLDE_27:
        CYC(3);     /* L5C2F LDA */
        a = TEMP1;  /* L5C2F LDA TEMP1 */
EXPLDE_28:
        CYC(19);                                       /* L5C31-L5C3A (JSR ROTPNT included) */
        PTBLO2_AT(y) = a;                              /* L5C31 STA PTBLO2,Y */
        a = EXPOSY_AT(x + 1);                          /* L5C34 */
        hw_mb_write((uint8_t)(A_YSTRT1 - 0x1860), a);  /* L5C37 STA YSTRT1 - start the matrix multiply */
        y = rotpnt(y);                                 /* L5C3A JSR ROTPNT - rotate & keep-or-discard */

EXPLDE_30:
        x = (uint8_t)(x - 2);      /* L5C3D/L5C3E DEX DEX */
        CYC((int8_t)x < 0 ? 7 : 9);  /* L5C3D-L5C3F, BMI taken or L5C41 JMP */
        if ((int8_t)x < 0) break;  /* L5C3F BMI EXPLDE_34 (else L5C41 JMP EXPLDE_1, i.e. loop) */
    }

    /* EXPLDE_34: if every one of the 6 slots this loop covers is now
     * inactive, the explosion is over - clear the collision flag and let
     * ROBCHK put the robot back into play.  Y is saved/restored around the
     * call because ROBCHK clobbers it and EXPLDE's own caller (ROTATE)
     * still needs the table-index Y this function is about to return. */
    CYC(2);    /* L5C44 LDX */
    x = 0x0A;  /* L5C44 */
    for (;;) {
        CYC(6);                          /* L5C46 LDA / L5C49 BPL */
        a = EXPOSZ_AT(x + 1);            /* L5C46 */
        if (!((int8_t)a < 0)) { CYC(7); return y; }  /* L5C49 BPL EXPLDE_40 (taken + RTS) */
        x = (uint8_t)(x - 2);            /* L5C4B/L5C4C */
        CYC((int8_t)x < 0 ? 6 : 7);      /* L5C4B-L5C4D, BPL taken while X >= 0 */
        if ((int8_t)x < 0) break;        /* L5C4D BPL EXPLDE_35 (loop) else fall through */
    }
    CYC(16);              /* L5C4F-L5C55 (JSR ROBCHK included) */
    COLFLG_AT(2) = 0x00;  /* L5C4F/L5C51 */
    {
        uint8_t saved_y = y;  /* L5C53/L5C54 TYA/PHA */
        robchk();             /* L5C55 JSR ROBCHK */
        CYC(12);              /* L5C58 PLA / L5C59 TAY / L5C5A RTS */
        y = saved_y;          /* L5C58/L5C59 PLA/TAY */
    }

    return y;  /* L5C5A RTS */
}

/* QWIKCK ($5F4E): quick collision check, player's shell (X=0) vs the robot
 * (Y=2) only.  No inputs; falls into SHRTCK with those two hardwired.
 * Returns carry: 0 = no collision, 1 = collision (SHRTCK has already set
 * the flags/counters). */
int qwikck(void)
{
    uint8_t a;

    CYC(5);                            /* L5F4E LDA / L5F50 BMI */
    a = FIRECT;                        /* L5F4E LDA FIRECT */
    if ((int8_t)a < 0) { CYC(1); goto QWIKCK_5; }  /* L5F50 BMI: no shell in flight */
    CYC(2);                            /* L5F52 BNE */
    if (a != 0) { CYC(1); goto QWIKCK_10; }  /* L5F52 BNE: shell in flight, check it */

QWIKCK_5:
    CYC(3 + 8);  /* L5F54 JMP, L6043 CLC / L6044 RTS */
    return 0;  /* JMP Q_20S_ (L6043 CLC / L6044 RTS) */

QWIKCK_10:
    CYC(4);                     /* L5F57 LDX / L5F59 LDY */
    return shrtck(0x00, 0x02);  /* L5F57/L5F59 LDX #$00/LDY #$02, falls into SHRTCK */
}

/* SHRTCK ($5F5B): shell-vs-tank collision check, either direction.
 * ENTRY x = whose shell (0 = player's, 2 = robot's/buzz-bomb's - indexes
 * SHELLX/SHELLY/FIRECT/HITS/SNDWRD/EXPCNT); y = whose position is the
 * target (0 = player tank, 2 = robot - indexes COLFLG/TPOSX/TPOSY/EXPCNT/
 * SNDWRD).  Returns carry: 0 = no collision, 1 = collision. */
int shrtck(uint8_t x, uint8_t y)
{
    uint8_t a;
    unsigned c = 0;

    /* Set up the Mathbox distance function: shell position (ACOS/BSIN
     * inputs, reused here as plain X/Y argument registers - see
     * bzone_math.h) vs. the target tank's position (TPOSXT/TPOSYT,
     * STA DSTNCE starts the function). */
    CYC(38);                                         /* L5F5B-L5F72 */
    a = SHELLX_AT(x);                                /* L5F5B */
    hw_mb_write((uint8_t)(A_ACOS - 0x1860), a);      /* L5F5D STA ACOS */
    a = SHELLX_AT(x + 1);                            /* L5F60 */
    hw_mb_write((uint8_t)(A_ACOS + 1 - 0x1860), a);  /* L5F62 STA ACOS+1 */
    a = SHELLY_AT(x);                                /* L5F65 */
    hw_mb_write((uint8_t)(A_BSIN - 0x1860), a);      /* L5F67 STA BSIN */
    a = SHELLY_AT(x + 1);                            /* L5F6A */
    hw_mb_write((uint8_t)(A_BSIN + 1 - 0x1860), a);  /* L5F6C STA BSIN+1 */

    a = COLFLG_AT(y);            /* L5F6F LDA COLFLG,Y (forced abs) */
    if (a != 0) { CYC(1); goto SHRTCK_16; }  /* L5F72 BNE: already flagged - skip the distance calc, `a` stays COLFLG,Y for the BNE at SHRTCK_16 below */

    CYC(56);                                           /* L5F74-L5F98 (dummy reads L5F8C/L5F8E included) */
    a = TPOSX_AT(y);                                   /* L5F74 LDA TPOSX,Y (forced abs) */
    hw_mb_write((uint8_t)(A_TPOSXT - 0x1860), a);      /* L5F77 STA TPOSXT */
    a = TPOSX_AT(y + 1);                               /* L5F7A */
    hw_mb_write((uint8_t)(A_TPOSXT + 1 - 0x1860), a);  /* L5F7D STA TPOSXT+1 */
    a = TPOSY_AT(y);                                   /* L5F80 */
    hw_mb_write((uint8_t)(A_TPOSYT - 0x1860), a);      /* L5F83 STA TPOSYT */
    a = TPOSY_AT(y + 1);                               /* L5F86 */
    hw_mb_write((uint8_t)(A_DSTNCE - 0x1860), a);      /* L5F89 STA DSTNCE - starts the distance function */

    /* L5F8C/L5F8E: `CMP ($00,X)` / `LDA ($00),Y` - deliberate dummy reads
     * used as a delay before reading the Mathbox result (TRANSLATING.md's
     * own example of this exact idiom); both results are discarded (the
     * next real LDA overwrites A). Emit nothing. */

    a = hw_mb_ylow();                   /* L5F90 LDA MOUTLO */
    TEMP3 = a;                          /* L5F93 */
    a = hw_mb_yhigh();                  /* L5F95 LDA MOUTHI */
    if ((int8_t)a < 0) { CYC(1); goto SHRTCK_22; }  /* L5F98 BMI: bad/negative result - no collision */
    CYC(9);                             /* L5F9A-L5F9D */
    a = lsr6502(a, &c);                 /* L5F9A LSR */
    TEMP3 = ror6502(TEMP3, &c);         /* L5F9B ROR TEMP3 */
    a = lsr6502(a, &c);                 /* L5F9D LSR */

SHRTCK_16:
    CYC(2);                      /* L5F9E BNE */
    if (a != 0) { CYC(1); goto SHRTCK_22; }  /* L5F9E BNE - see the SHRTCK_16 entry comment above: also reached with a=COLFLG,Y (nonzero), always taken there */
    TEMP3 = ror6502(TEMP3, &c);  /* L5FA0 */
    CYC(10);                     /* L5FA0-L5FA4 */

    if (!(R2D3FL & 0x80)) { CYC(1); goto SHRTCK_17; }  /* L5FA2/L5FA4 BIT R2D3FL / BPL: tank - skip the buzz-bomb height check */
    CYC(14);                               /* L5FA6-L5FB0 */
    a = EXPOSZ_AT(0x0C);                   /* L5FA6 - buzz-bomb's Z depth low byte (R2D3CK's STARTZ, not an explosion piece) */
    {
        int z;
        cmp6502(a, (uint8_t)(K_TOP & 0xFF), &c, &z);  /* L5FA9 CMP #TOP&$FF */
        (void)z;
    }
    a = EXPOSZ_AT(0x0D);                              /* L5FAB */
    a = sbc6502(a, (uint8_t)(K_TOP >> 8), &c, NULL);  /* L5FAE SBC #TOP/$100 */
    if (c) { CYC(1); goto SHRTCK_22; }                /* L5FB0 BCS: too high up - no collision */

SHRTCK_17:
    CYC(12);                                 /* L5FB2-L5FB8 */
    a = TANGLE;                              /* L5FB2 */
    c = 1;                                   /* L5FB4 SEC */
    a = sbc6502(a, TANGLE_AT(2), &c, NULL);  /* L5FB5 SBC TANGLE+2 */
    a = asl6502(a, &c);                      /* L5FB7 ASL */
    if (!(a & 0x80)) { CYC(1); goto SHRTCK_30; }  /* L5FB8 BPL */
    CYC(6);                                  /* L5FBA-L5FBD */
    a ^= 0xFF;                               /* L5FBA */
    c = 0;                                   /* L5FBC CLC */
    a = adc6502(a, 0x01, &c, NULL);          /* L5FBD */

SHRTCK_30:
    CYC(9);                             /* L5FBF-L5FC3 */
    a = lsr6502(a, &c);                 /* L5FBF */
    a = lsr6502(a, &c);                 /* L5FC0 */
    if (R2D3FL & 0x80) { CYC(1); goto SHRTCK_35; }  /* L5FC1/L5FC3 BIT R2D3FL / BMI */
    CYC(4);                             /* L5FC5 LSR / L5FC6 BPL */
    a = lsr6502(a, &c);                 /* L5FC5 - always leaves bit7 clear, so the BPL below always falls to SHRTCK_36 (translated literally anyway) */
    if (!(a & 0x80)) { CYC(1); goto SHRTCK_36; }  /* L5FC6 BPL */

SHRTCK_35:
    CYC(4);                          /* L5FC8 CLC / L5FC9 ADC */
    c = 0;                           /* L5FC8 CLC */
    a = adc6502(a, 0x18, &c, NULL);  /* L5FC9 */

SHRTCK_36:
    CYC(20);                          /* L5FCB-L5FD7 */
    TEMP1 = a;                        /* L5FCB */
    a = lsr6502(a, &c);               /* L5FCD */
    c = 0;                            /* L5FCE CLC */
    a = adc6502(a, TEMP1, &c, NULL);  /* L5FCF */
    a = adc6502(a, 0x38, &c, NULL);   /* L5FD1 ADC #$38 - carry-in is whatever the previous ADC left, NOT a fresh CLC (residual, deliberate: no CLC between the two ADCs) */
    TEMP1 = a;                        /* L5FD3 */
    {
        int z;
        cmp6502(a, TEMP3, &c, &z);  /* L5FD5 CMP TEMP3 */
        (void)z;
    }
    if (!c) goto SHRTCK_22;  /* L5FD7 BCC-complement: BCS SHRTCK_38 falls through here on carry clear */
    CYC(1);                  /* L5FD7 BCS taken */
    goto SHRTCK_38;

SHRTCK_22:
    CYC(3 + 8);  /* L5FD9 JMP, L6043 CLC / L6044 RTS */
    return 0;  /* JMP Q_20S_ (L6043 CLC / L6044 RTS) */

SHRTCK_38:
    CYC(16);                         /* L5FDC-L5FE6 */
    COLFLG_AT(y) = 0x20;             /* L5FDC/L5FDE */
    TIMOUT = 0x00;                   /* L5FE1/L5FE3 */
    a = y;                           /* L5FE5 TYA */
    if (a != 0) { CYC(1); goto SHRTCK_18; }  /* L5FE6 BNE */
    CYC(17);                         /* L5FE8-L5FF2 */
    CRACK = 0x02;                    /* L5FE8/L5FEA */
    BOUNCE = 0xFF;                   /* L5FEC/L5FEE */
    LIVES--;                         /* L5FF0 DEC LIVES */
    if (LIVES != 0) { CYC(1); goto SHRTCK_18; }  /* L5FF2 BNE */
    CYC(5);                          /* L5FF4 INC */
    GOVER++;                         /* L5FF4 INC GOVER */

SHRTCK_18:
    CYC(18);                            /* L5FF6-L5FFF */
    TEMP1 = HITS_AT(x);                 /* L5FF6/L5FF8 */
    TEMP1_AT(1) = HITS_AT(x + 1);       /* L5FFA/L5FFC */
    a = x;                              /* L5FFE TXA */
    if (a != 0) { CYC(1); goto SHRTCK_4; }  /* L5FFF BNE */
    CYC(7);                             /* L6001-L6005 */
    a = 0x02;                           /* L6001 */
    if (R2D3FL & 0x80) { CYC(1); goto SHRTCK_90; }  /* L6003/L6005 BMI (a stays 2: not R2D3, wait - BMI taken means R2D3FL bit7 SET, i.e. IS R2D3 - "D IF NOT R2D3" comments the BIT, so BMI-taken = R2D3 present, skip TR7CHK) */
    {
        CYC(6);                   /* L6007 JSR */
        int trc = tr7chk();       /* L6007 JSR TR7CHK */
        CYC(4);                   /* L600A LDA / L600C BCS */
        a = 0x03;                 /* L600A LDA #$03 - unconditional; doesn't touch carry */
        if (trc) { CYC(1); goto SHRTCK_90; }  /* L600C BCS */
    }

SHRTCK_4:
    CYC(2);    /* L600E LDA */
    a = 0x01;  /* L600E */

SHRTCK_90:
    {
        unsigned cc = 0;                        /* L6010 CLC */
        CYC(24 + 4);                            /* L6010-L601B, L601D TXA / L601E BNE */
        g.dflag = 1;                            /* L6011 SED */
        a = adc6502(a, HITS_AT(x), &cc, NULL);  /* L6012 ADC HITS,X */
        HITS_AT(x) = a;                         /* L6014 */
        a = HITS_AT(x + 1);                     /* L6016 */
        a = adc6502(a, 0x00, &cc, NULL);        /* L6018 ADC #$00 */
        g.dflag = 0;                            /* L601A CLD */
        HITS_AT(x + 1) = a;                     /* L601B */
    }
    a = x;                       /* L601D TXA */
    if (a != 0) { CYC(1); goto SHRTCK_19; }  /* L601E BNE */
    CYC(6);                      /* L6020 JSR */
    newlif();                    /* L6020 JSR NEWLIF */

SHRTCK_19:
    CYC(34 + 6);            /* L6023-L6038, L603A JSR EXINIT */
    FIRECT_AT(x) = 0x80;    /* L6023/L6025 */
    EXPCNT_AT(y) = 0xFF;    /* L6027/L6029 STA EXPCNT,Y (forced abs) */
    SNDWRD_AT(y) |= K_LOX;  /* L602C-L6031 */
    SNDWRD_AT(x) &= K_HIX;  /* L6034-L6038 */
    exinit(x);              /* L603A JSR EXINIT */
    CYC(14);                /* L603D-L6042 */
    EXPCNT_AT(x) = 0x70;    /* L603D/L603F */
    return 1;               /* L6041 SEC / L6042 RTS */
}

/* C_21S_ ($6061): COLCHK's playfield-obstacle scan - internal continuation
 * of SAUCHK, never JSR'd/JMP'd from anywhere outside this cluster, so it is
 * a `static` helper (TRANSLATING.md/SIGNATURES.md convention for such
 * fall-through-only labels - same as coin65.c's S_DETCT etc.). ENTRY x =
 * whose shell (FIRECT/GARBAG save-restore). Every exit path of the real
 * C_21S_ reaches C_60S_ (COLCHK's shared per-shell loop bottom) - modelled
 * here as a plain `return`, with the caller (`sauchk()`, and `sauchk()`'s
 * own caller `colchk()`) responsible for continuing to that bottom
 * themselves (see `colchk()` below).
 *
 * PTBLX1 ($7681) and PTBLY1 ($76AB) and PRXTBL ($6139) have no
 * state_defs.h alias - they're disassembler-local labels for ROM data
 * tables, not exported by bzone_defines.asm (unlike PTBLO1, which IS
 * exported as A_PTBLO1) - used here as literal ROM addresses via cpu_rd,
 * per TRANSLATING.md "ROM tables are read... at their ROM address". */
static void c21s_(uint8_t x)
{
    uint8_t y = 0x00;  /* L6061 LDY #$00 */
    CYC(2);            /* L6061 LDY */
    uint8_t a;
    unsigned c;

    for (;;) {
        CYC(6);                                /* L6063 LDA / L6066 BMI */
        a = cpu_rd((uint16_t)(A_PTBLO1 + y));  /* L6063 LDA PTBLO1,Y */
        if ((int8_t)a < 0) { CYC(1); return; }  /* L6066 BMI C_60S_ - table done */
        CYC(71);                               /* L6068-L6096 (dummy read L6080 included) */

        a = cpu_rd((uint16_t)(0x7681 + y));                /* L6068 LDA PTBLX1,Y */
        hw_mb_write((uint8_t)(A_TPOSXT - 0x1860), a);      /* L606B STA TPOSXT */
        a = cpu_rd((uint16_t)(0x7681 + y + 1));            /* L606E */
        hw_mb_write((uint8_t)(A_TPOSXT + 1 - 0x1860), a);  /* L6071 */
        a = cpu_rd((uint16_t)(0x76AB + y));                /* L6074 LDA PTBLY1,Y */
        hw_mb_write((uint8_t)(A_TPOSYT - 0x1860), a);      /* L6077 */
        a = cpu_rd((uint16_t)(0x76AB + y + 1));            /* L607A */
        hw_mb_write((uint8_t)(A_DSTNCE - 0x1860), a);      /* L607D STA DSTNCE - starts the distance function */

        /* L6080: CMP ($00,X) - deliberate dummy delay read before the
         * Mathbox result (TRANSLATING.md's own worked example). Emit
         * nothing. */

        a = hw_mb_ylow();   /* L6082 LDA MOUTLO */
        TEMP3 = a;          /* L6085 */
        a = hw_mb_yhigh();  /* L6087 LDA MOUTHI */
        c = 0;
        cmp6502(a, 0x80, &c, NULL);  /* L608A CMP #$80 - ASR setup */
        a = ror6502(a, &c);          /* L608C ROR (accumulator, sign-preserving) */
        TEMP3 = ror6502(TEMP3, &c);  /* L608D ROR TEMP3 */
        cmp6502(a, 0x80, &c, NULL);  /* L608F */
        a = ror6502(a, &c);          /* L6091 */
        TEMP3 = ror6502(TEMP3, &c);  /* L6092 */
        {
            int z;
            cmp6502(a, 0x00, &c, &z);  /* L6094 CMP #$00 */
            if (!z) { CYC(1); goto C_21S__40; }  /* L6096 BNE: too far */
        }

        {
            CYC(19);              /* L6098-L60A4 */
            uint8_t saved_x = x;  /* L6098 STX GARBAG */
            GARBAG = saved_x;
            x = cpu_rd((uint16_t)(A_PTBLO1 + y));  /* L609A LDX PTBLO1,Y - X reused as a PRXTBL index */
            a = cpu_rd((uint16_t)(0x6139 + x));    /* L609D LDA PRXTBL,X */
            x = GARBAG;                            /* L60A0 LDX GARBAG - restore the real shooter index */
        }
        {
            int z;
            cmp6502(a, TEMP3, &c, &z);  /* L60A2 CMP TEMP3 */
            (void)z;
        }
        if (!c) { CYC(1); goto C_21S__40; }  /* L60A4 BCC: still too far */
        CYC(22);                 /* L60A6-L60B4, BCS taken */

        FIRECT_AT(x) = 0xA0;  /* L60A6/L60A8 */
        /* L60AC/L60B2: EXPCNT/SNDWRD here, unlike SAUCHK's own hit path
         * below, are NOT indexed by X - a playfield-obstacle hit always
         * uses slot 0's EXPCNT/SNDWRD regardless of which shell (X) hit it.
         * Translated faithfully (matches the listing exactly); flagged in
         * NOTES_game.md as a possible ROM quirk, not "fixed". */
        EXPCNT = 0x70;    /* L60AA/L60AC */
        SNDWRD &= K_HIX;  /* L60AE-L60B2 */
        return;           /* L60B4 BCS C_60S_ - carry is still set (from the CMP TEMP3 above), always taken */

C_21S__40:
        y = (uint8_t)(y + 2);  /* L60B6/L60B7 INY INY */
        CYC(y != 0 ? 7 : 6);   /* L60B6-L60B8, BNE taken while Y != 0 */
        if (y != 0) continue;  /* L60B8 BNE C_21S__21 */
        return;                /* Y wrapped to 0: falls straight into C_60S_ */
    }
}

/* SAUCHK ($60C2): saucer-vs-shell collision check, then falls into C_21S_'s
 * playfield-obstacle scan regardless of outcome (every SAUCHK exit reaches
 * C_60S_, same as C_21S_ - see the file-header note on c21s_() above).
 * ENTRY x = whose shell (FIRECT/EXPCNT,X, and the score-add's HITS gate);
 * y = whose SNDWRD gets the LOX bit on a hit (the target's index, matching
 * SHRTCK's own X/Y convention).  No return - COLCHK's shared loop bottom is
 * the caller's responsibility (`colchk()` below). */
void sauchk(uint8_t x, uint8_t y)
{
    uint8_t a;
    unsigned c = 0;

    CYC(5);                            /* L60C2 LDA / L60C4 BEQ */
    a = SAUCER;                        /* L60C2 */
    if (a == 0) { CYC(1 + 3); c21s_(x); return; }  /* L60C4 BEQ SAUCHK_10 (JMP C_21S_) */
    CYC(5);                            /* L60C6 LDA / L60C8 BNE */
    a = SCOLFG;                        /* L60C6 */
    if (a != 0) { CYC(1 + 3); c21s_(x); return; }  /* L60C8 BNE SAUCHK_10 */
    CYC(71);                           /* L60CA-L60F7 (dummy read L60E1 included) */

    a = SAPOSX;                                        /* L60CA */
    hw_mb_write((uint8_t)(A_TPOSXT - 0x1860), a);      /* L60CC */
    a = SAPOSX_AT(1);                                  /* L60CF */
    hw_mb_write((uint8_t)(A_TPOSXT + 1 - 0x1860), a);  /* L60D1 */
    a = SAPOSY;                                        /* L60D4 */
    hw_mb_write((uint8_t)(A_TPOSYT - 0x1860), a);      /* L60D6 */
    a = SAPOSY_AT(1);                                  /* L60D9 */
    hw_mb_write((uint8_t)(A_TPOSYT + 1 - 0x1860), a);  /* L60DB STA TPOSYT+1 */
    hw_mb_write((uint8_t)(A_DSTNCE - 0x1860), a);      /* L60DE STA DSTNCE - same `a`, no reload; starts the function */

    /* L60E1: CMP ($00,X) - dummy delay read, emit nothing. */

    a = hw_mb_ylow();            /* L60E3 */
    TEMP3 = a;                   /* L60E6 */
    a = hw_mb_yhigh();           /* L60E8 */
    cmp6502(a, 0x80, &c, NULL);  /* L60EB */
    a = ror6502(a, &c);          /* L60ED */
    TEMP3 = ror6502(TEMP3, &c);  /* L60EE */
    cmp6502(a, 0x80, &c, NULL);  /* L60F0 */
    a = ror6502(a, &c);          /* L60F2 */
    TEMP3 = ror6502(TEMP3, &c);  /* L60F3 */
    {
        int z;
        cmp6502(a, 0x00, &c, &z);      /* L60F5 CMP #$00 */
        if (!z) { CYC(3); c21s_(x); return; }  /* L60F7 BEQ SAUCHK_20 (negated), L60F9 JMP C_21S_ */
        CYC(1);                        /* L60F7 BEQ taken */
    }

    /* SAUCHK_20 */
    CYC(7);                        /* L60FC-L6100 */
    a = 0x90;                      /* L60FC */
    cmp6502(a, TEMP3, &c, NULL);   /* L60FE CMP TEMP3 */
    if (!c) { CYC(1 + 3); c21s_(x); return; }  /* L6100 BCC SAUCHK_10 (JMP C_21S_) */
    CYC(13);                       /* L6102-L6108 (JSR SNDON included) */

    SCOLFG = 0x40;          /* L6102/L6104 */
    sndon(K_DISINT, x);     /* L6106/L6108 - x unread by sndon() since a=K_DISINT!=0 */
    CYC(25);                /* L610B-L611A */
    FIRECT_AT(x) = 0xA0;    /* L610B/L610D */
    EXPCNT_AT(x) = 0xA0;    /* L610F STA EXPCNT,X - same `a` */
    SNDWRD_AT(y) |= K_LOX;  /* L6111-L6116 */

    a = x;               /* L6119 TXA */
    if (a != 0) { CYC(1); return; }  /* L611A BNE C_60S_ - robot's shell: no score credit */
    CYC(34 + 6);         /* L611C-L6131, L6133 JSR NEWLIF */

    TEMP1 = HITS;              /* L611C/L611E - dead store (never read back in this ROM), kept for RAM fidelity */
    TEMP1_AT(1) = HITS_AT(1);  /* L6120/L6122 */
    {
        unsigned cc = 0;                     /* L6126 CLC */
        g.dflag = 1;                         /* L6127 SED */
        a = adc6502(0x05, HITS, &cc, NULL);  /* L6124/L6128 LDA #$05 / ADC HITS */
        HITS = a;                            /* L612A */
        a = HITS_AT(1);                      /* L612C */
        a = adc6502(a, 0x00, &cc, NULL);     /* L612E */
        g.dflag = 0;                         /* L6130 CLD */
        HITS_AT(1) = a;                      /* L6131 */
    }
    newlif();  /* L6133 JSR NEWLIF */
    CYC(3);    /* L6136 JMP C_60S_ */
    /* SAUCHK_40: JMP C_60S_ - return; the caller (colchk()) continues to
     * its own shared loop bottom regardless. */
}

/* COLCHK ($6045): per-frame shell-vs-tank/saucer/obstacle collision check
 * for both shells (robot's first, then the player's - `LDX #$02`).  No
 * inputs/outputs; SHRTCK/SAUCHK/C_21S_ do all the flag/counter bookkeeping
 * as side effects. */
void colchk(void)
{
    uint8_t x = 0x02;  /* L6045 LDX #$02 */
    CYC(2);            /* L6045 LDX */

    for (;;) {  /* C_10S_ */
        CYC(12);                          /* L6047-L604D */
        uint8_t y = (uint8_t)(x ^ 0x02);  /* L6047/L6048/L604A TXA/EOR #$02/TAY */
        uint8_t a = FIRECT_AT(x);         /* L604B */

        if ((int8_t)a < 0) { CYC(1); goto BOTTOM; }  /* L604D BMI: no shell fired */
        CYC(2);                                      /* L604F BEQ */
        if (a == 0) { CYC(1); goto BOTTOM; }         /* L604F BEQ: no shell fired */

        CYC(5);        /* L6051 LDA / L6053 BNE */
        a = CRACK;     /* L6051 */
        if (a == 0) {  /* L6053 BNE (negated) */
            CYC(5);                   /* L6055 LDA / L6057 BNE */
            a = GOVER;                /* L6055 */
            if (a != 0) { CYC(1); goto BOTTOM; }  /* L6057 BNE: game over, windshield not cracking - skip */
        } else CYC(1);                /* L6053 BNE taken */

        CYC(6);                         /* L6059 JSR */
        if (shrtck(x, y)) { CYC(3); goto BOTTOM; }  /* L6059/L605C JSR SHRTCK / BCS: collision already handled */
        CYC(2 + 3);                     /* L605C BCS, L605E JMP */
        sauchk(x, y);                   /* L605E JMP SAUCHK - falls through to C_60S_ either way */

BOTTOM:  /* C_60S_ */
        x = (uint8_t)(x - 2);  /* L60BA/L60BB DEX DEX */
        CYC(x != 0 ? 13 : 9);  /* L60BA-L60BC, BNE taken + L60C1 RTS, or L60BE JMP C_10S_ */
        if (x != 0) return;    /* L60BC BNE C_60S__61 -> RTS */
        /* x==0: L60BE JMP C_10S_ - loop again for the other shell. */
    }
}

/* NEWLIF ($615F): award a bonus life (or the super-bonus) if the score just
 * crossed the right threshold for the current DSW0 bonus-level setting.
 * No register inputs; TEMP1 is a hidden RAM input the caller must set to
 * the score before its current increment (see the header comment on
 * `newlif()` in bzone_game.h). No return. */
void newlif(void)
{
    uint8_t a;
    unsigned c;

    CYC(22);                               /* L615F-L616C */
    a = hw_option();                       /* L615F LDA OPTION */
    a = (uint8_t)(a >> 4);                 /* L6162-L6165: four LSRs */
    a &= 0x03;                             /* L6166 AND #$03 */
    a = cpu_rd((uint16_t)(A_BONTBL + a));  /* L6169 LDA BONTBL,X */
    if (a == 0) { CYC(9); return; }        /* L616C BEQ NEWLIF_20 (taken), L6185 LDX / L6187 RTS */

    CYC(5);                               /* L616E LDX / L6170 BNE */
    if (HITS_AT(1) != 0) { CYC(1); goto NEWLIF_40; }  /* L616E/L6170 LDX HITS+1 / BNE: not in range for a plain bonus */

    c = 0;
    CYC(5);                       /* L6172 CMP / L6174 BCC */
    cmp6502(a, TEMP1, &c, NULL);  /* L6172 CMP TEMP1 */
    if (!c) { CYC(9); return; }   /* L6174 BCC NEWLIF_20 (taken + LDX/RTS) */
    CYC(5);                       /* L6176 CMP / L6178 BCS */
    cmp6502(a, HITS, &c, NULL);   /* L6176 CMP HITS */
    if (c) { CYC(9); return; }    /* L6178 BCS NEWLIF_20 (taken + LDX/RTS) */
    CYC(18);                      /* L617A-L6182 (JSR SNDON included) */

    LIVES++;               /* L617A */
    GOVER = 0x00;          /* L617C/L617E */
    sndon(K_BONER, 0x00);  /* L6180/L6182 JSR SNDON */
    CYC(8);                /* L6185 LDX / L6187 RTS */
    return;                /* NEWLIF_20 (L6185/L6187 LDX #$00/RTS - X unread by any caller) */

NEWLIF_40:
    CYC(5);                        /* L6188 LDA / L618A BNE */
    if (TEMP1_AT(1) != 0) { CYC(9); return; }  /* L6188/L618A BNE NEWLIF_20 (taken + LDX/RTS) */
    CYC(19 + 6);                   /* L618C-L6197 (BNE taken), L6182 JSR SNDON */
    GOVER = 0x00;                  /* L618C STA GOVER (a == TEMP1+1 == 0 here) */
    LIVES++;                       /* L618E */
    SUPER = 0xFF;                  /* L6190/L6192 */
    sndon(K_SUPBON, 0x00);         /* L6195/L6197 (BNE NEWLIF_10, always taken - A=K_SUPBON!=0) */
    CYC(8);                        /* L6185 LDX / L6187 RTS */
}

/* EXINIT ($6199): explosion-piece table init, robot-death only - see the
 * header comment on `exinit()` in bzone_game.h for the ENTRY x contract. */
void exinit(uint8_t x)
{
    uint8_t a, y;

    CYC(4);              /* L6199 TXA / L619A BNE */
    if (x != 0) { CYC(7); return; }  /* L6199/L619A TXA/BNE EXINIT_20: player exploded - nothing to do here */
    CYC(4);              /* L619C LDX / L619E LDY */

    x = 0x05;  /* L619C */
    y = 0x0A;  /* L619E */
    for (;;) {
        CYC(62);                              /* L61A0-L61C5 */
        a = cpu_rd((uint16_t)(A_IZVEL + x));  /* L61A0 LDA IZVEL,X */
        ZVELOC_AT(y) = a;                     /* L61A3 */
        a = TPOSX_AT(2);                      /* L61A6 - robot's own X position */
        EXPOSX_AT(y) = a;                     /* L61A8 */
        a = TPOSX_AT(3);                      /* L61AB */
        EXPOSX_AT(y + 1) = a;                 /* L61AD */
        a = TPOSY_AT(2);                      /* L61B0 - robot's own Y position */
        EXPOSY_AT(y) = a;                     /* L61B2 */
        a = TPOSY_AT(3);                      /* L61B5 */
        EXPOSY_AT(y + 1) = a;                 /* L61B7 */
        a = hw_pokey_read(0x0A);              /* L61BA LDA PRAND */
        EXPOSO_AT(y) = a;                     /* L61BD */
        EXPOSZ_AT(y) = 0x00;                  /* L61C0/L61C2 */
        EXPOSZ_AT(y + 1) = 0x00;              /* L61C5 */

        y = (uint8_t)(y - 2);      /* L61C8/L61C9 DEY DEY */
        x = (uint8_t)(x - 1);      /* L61CA DEX */
        CYC((int8_t)x < 0 ? 8 : 9);  /* L61C8-L61CB, BPL taken while X >= 0 */
        if ((int8_t)x < 0) break;  /* L61CB BPL EXINIT_10 (negated) */
    }
    CYC(8);  /* L61CD LDX / L61CF RTS */
    /* L61CD LDX #$00 - x is already 0 here (the loop ran exactly 6 times,
     * x: 5,4,3,2,1,0,then -1 to exit), so this is a no-op; kept only as a
     * comment since there is no caller-visible X to reset in this model
     * (see the header comment: x is unchanged either way). */
}

/* SAVEIT ($63A9)/BAKOUT ($63BA): save/restore TPOSX/TPOSY around a
 * collision-retry backout. ENTRY x = 0 (tank) or 2 (robot). */
void saveit(uint8_t x)
{
    CYC(34);                         /* L63A9-L63B9 */
    OTPOSX = TPOSX_AT(x);            /* L63A9/L63AB */
    OTPOSX_AT(1) = TPOSX_AT(x + 1);  /* L63AD/L63AF */
    OTPOSY = TPOSY_AT(x);            /* L63B1/L63B3 */
    OTPOSY_AT(1) = TPOSY_AT(x + 1);  /* L63B5/L63B7 */
}

void bakout(uint8_t x)
{
    CYC(34);                         /* L63BA-L63CA */
    TPOSX_AT(x) = OTPOSX;            /* L63BA/L63BC */
    TPOSX_AT(x + 1) = OTPOSX_AT(1);  /* L63BE/L63C0 */
    TPOSY_AT(x) = OTPOSY;            /* L63C2/L63C4 */
    TPOSY_AT(x + 1) = OTPOSY_AT(1);  /* L63C6/L63C8 */
}

/* ITANGL ($638D)/DTANGL ($639B): pivot the tank/robot's 9-bit heading
 * (LANGLE/TANGLE,x) one step left/right. ENTRY x = 0 or 2. */
void itangl(uint8_t x)
{
    unsigned c = 0;    /* L638F CLC */
    uint8_t a = 0x80;  /* L638D */

    CYC(28);           /* L638D-L639A */

    a = adc6502(a, LANGLE_AT(x), &c, NULL);  /* L6390 */
    LANGLE_AT(x) = a;                        /* L6392 */
    a = 0x00;                                /* L6394 */
    a = adc6502(a, TANGLE_AT(x), &c, NULL);  /* L6396 */
    TANGLE_AT(x) = a;                        /* L6398 */
}

void dtangl(uint8_t x)
{
    unsigned c = 1;            /* L639D SEC */
    uint8_t a = LANGLE_AT(x);  /* L639B */

    CYC(28);                   /* L639B-L63A8 */

    a = sbc6502(a, 0x80, &c, NULL);  /* L639E */
    LANGLE_AT(x) = a;                /* L63A0 */
    a = TANGLE_AT(x);                /* L63A2 */
    a = sbc6502(a, 0x00, &c, NULL);  /* L63A4 */
    TANGLE_AT(x) = a;                /* L63A6 */
}

/* M_SET ($6310): compute the per-tick motion vector (X1/Y1) from
 * TANGLE,x's SIN/COS - see the header comment on m_set() in bzone_game.h.
 * `LDX TEMP1`/`STX TEMP1` (saving the outer index around SIN/COS, which
 * return their own result in A/X) needs no RAM round-trip here: this
 * function keeps the outer index in its own `x` parameter throughout. */
void m_set(uint8_t x)
{
    regs r;
    uint8_t a;
    unsigned c;

    CYC(13);           /* L6310-L6314 (JSR SIN included) */
    a = TANGLE_AT(x);  /* L6310 */
    /* Integration fix (2026-09-18, NOTES_integration.md): L6312 "STX TEMP1
     * ;SAVE X" was missing - the C translation already reuses the plain
     * parameter `x` correctly across the SIN()/COS() calls (no register-
     * clobbering issue in C the way there is on real hardware, where SIN
     * clobbers X), but that leaves TEMP1 itself never written, so the ROM's
     * own real RAM side effect (TEMP1 := entry X, restored via LDX TEMP1 at
     * L632F/L634E) never happens in the C port - found by lockstep: M_SET/
     * M_FOR/MOTION all failed at $0008 (TEMP1) with the C side showing
     * whatever TEMP1 held before the call, the ROM showing the entry X. */
    TEMP1 = x;                          /* L6312 STX TEMP1 */
    r = sin_(a);                        /* L6314 JSR SIN */
    CYC(23);                            /* L6317-L6325 */
    a = r.x;                            /* L6317 TXA */
    cmp6502(a, 0x80, &c, NULL);         /* L6318 CMP #$80 */
    a = ror6502(a, &c);                 /* L631A ROR */
    Y1 = a;                             /* L631B */
    cmp6502(a, 0x80, &c, NULL);         /* L631D CMP #$80 */
    a = ror6502(a, &c);                 /* L631F ROR */
    c = 0;                              /* L6320 CLC */
    a = adc6502(a, Y1, &c, NULL);       /* L6321 */
    Y1 = a;                             /* L6323 */
    a = ((int8_t)a < 0) ? 0xFF : 0x00;  /* L6325-L632B BPL/LDA #$FF/BMI/LDA #$00: sign-extend */
    CYC(((int8_t)a < 0 ? 5 : 3) + 16);  /* sign-extend, L632D-L6333 (JSR COS included) */
    Y1_AT(1) = a;                       /* L632D */

    a = TANGLE_AT(x);                   /* L6331 */
    r = cos_(a);                        /* L6333 JSR COS */
    CYC(23);                            /* L6336-L6344 */
    a = r.x;                            /* L6336 TXA */
    cmp6502(a, 0x80, &c, NULL);         /* L6337 */
    a = ror6502(a, &c);                 /* L6339 */
    X1 = a;                             /* L633A */
    cmp6502(a, 0x80, &c, NULL);         /* L633C */
    a = ror6502(a, &c);                 /* L633E */
    c = 0;                              /* L633F CLC */
    a = adc6502(a, X1, &c, NULL);       /* L6340 */
    X1 = a;                             /* L6342 */
    a = ((int8_t)a < 0) ? 0xFF : 0x00;  /* L6344-L634A: sign-extend */
    CYC(((int8_t)a < 0 ? 5 : 3) + 12);  /* sign-extend, L634C STA / L634E LDX / L6350 RTS */
    X1_AT(1) = a;                       /* L634C */
    /* L634E LDX TEMP1 / L6350 RTS - x is our own parameter, unchanged. */
}

/* M_FOR1 ($6354): apply the already-computed X1/Y1 vector to TPOSX/TPOSY
 * (forward). M_FOR ($6351) computes it first via m_set(). Both are called
 * directly from outside MOTION (ROBOT/BUZBOM, confirmed by grep), so both
 * are public. */
void m_for1(uint8_t x)
{
    uint8_t a;
    unsigned c = 0;  /* L6356 CLC */

    CYC(54);                             /* L6354-L636E */
    a = TPOSX_AT(x);                     /* L6354 */
    a = adc6502(a, X1, &c, NULL);        /* L6357 */
    TPOSX_AT(x) = a;                     /* L6359 */
    a = TPOSX_AT(x + 1);                 /* L635B */
    a = adc6502(a, X1_AT(1), &c, NULL);  /* L635D */
    TPOSX_AT(x + 1) = a;                 /* L635F */
    a = TPOSY_AT(x);                     /* L6361 */
    c = 0;                               /* L6363 CLC */
    a = adc6502(a, Y1, &c, NULL);        /* L6364 */
    TPOSY_AT(x) = a;                     /* L6366 */
    a = TPOSY_AT(x + 1);                 /* L6368 */
    a = adc6502(a, Y1_AT(1), &c, NULL);  /* L636A */
    TPOSY_AT(x + 1) = a;                 /* L636C */
}

void m_for(uint8_t x)
{
    CYC(6);     /* L6351 JSR */
    m_set(x);   /* L6351 JSR M_SET */
    m_for1(x);  /* falls into M_FOR1 */
}

/* M_REV ($636F): m_set() then subtract X1/Y1 from TPOSX/TPOSY (reverse). */
void m_rev(uint8_t x)
{
    uint8_t a;
    unsigned c;

    CYC(6);                              /* L636F JSR */
    m_set(x);                            /* L636F JSR M_SET */
    CYC(54);                             /* L6372-L638C */
    a = TPOSX_AT(x);                     /* L6372 */
    c = 1;                               /* L6374 SEC */
    a = sbc6502(a, X1, &c, NULL);        /* L6375 */
    TPOSX_AT(x) = a;                     /* L6377 */
    a = TPOSX_AT(x + 1);                 /* L6379 */
    a = sbc6502(a, X1_AT(1), &c, NULL);  /* L637B */
    TPOSX_AT(x + 1) = a;                 /* L637D */
    a = TPOSY_AT(x);                     /* L6380 */
    c = 1;                               /* L637F SEC */
    a = sbc6502(a, Y1, &c, NULL);        /* L6382 */
    TPOSY_AT(x) = a;                     /* L6384 */
    a = TPOSY_AT(x + 1);                 /* L6386 */
    a = sbc6502(a, Y1_AT(1), &c, NULL);  /* L6388 */
    TPOSY_AT(x + 1) = a;                 /* L638A */
}

/* ---------------------------------------------------------------------
 * The rest of MOTION's own M_* legs (M_TOP's MTAB dispatch and everything
 * it reaches) are never JSR'd/JMP'd from outside MOTION itself (confirmed
 * by grepping the whole disassembly) - `static` helpers, same convention
 * as C_21S_ above. All operate on x=0 only in this ROM (M_TOP is only ever
 * entered from MOTION_10 with x hardwired to 0), but keep an `x` parameter
 * for symmetry with the public primitives above.
 * ----------------------------------------------------------------------
 */

/* M_NEX0 ($6294): shared tail of every MTAB leg - set the engine idle/
 * running sound bit, then (if the fire button is newly pressed) fire a
 * shell: computes its X/Y velocity increment (SINCX/SINCY) from COSXTR/
 * SINXTR of the tank's current heading, and snapshots TPOSX/TPOSY into
 * SHELLX/SHELLY as the shell's starting position.
 *
 * The ROM's two PHP/PLP pairs here (L62CA/L62CF and L62EA/L62EF) exist
 * only to protect the flags of a `TXA` against the incidental `LDX SAVE1`
 * that follows (X is needed both as the outer tank index and, moments
 * earlier, as COSXTR's/SINXTR's own angle parameter, on real hardware -
 * see CALLING_NOTES.md's register-pressure discussion). This C function
 * keeps the outer index in its own `x` parameter throughout, so that
 * reload never happens, and the PHP/PLP has nothing to protect against -
 * simplified to a direct sign test of the freshly-computed value (verified
 * bit-for-bit equivalent by hand-tracing every intervening instruction). */
static void m_nex0(uint8_t x, uint8_t sound)
{
    uint8_t a;
    unsigned c;
    regs r;

    CYC(16 + 6);       /* L6294-L629D, L629F LDA / L62A1 BNE */
    TEMP1 = sound;     /* L6294 STA TEMP1 */
    a = SNDWRD_AT(x);  /* L6296 LDA SNDWRD,X (forced absolute) */
    a &= 0xEF;         /* L6299 */
    a |= TEMP1;        /* L629B */
    SNDWRD_AT(x) = a;  /* L629D */

    if (FIRECT_AT(x) != 0) { CYC(7); return; }     /* L629F/L62A1 M_NEX0_5: shell already fired (BNE taken + RTS) */
    CYC(8);                                        /* L62A3-L62A8 */
    if (!(PL1MOV & K_PL1FIR)) { CYC(7); return; }  /* L62A3-L62A8 M_NEX0_10: fire not pressed (BEQ taken + RTS) */
    CYC(21 + 32);                                  /* L62AA-L62B6, L62B8-L62C4 (JSR COSXTR included) */

    FIRECT_AT(x) = 0x7F;       /* L62AA/L62AC indicate shell fired */
    SNDWRD_AT(x) |= K_LOUDSH;  /* L62AE-L62B2 */
    SXPCNT = 0x05;             /* L62B4/L62B6 turn on explosion */

    a = LANGLE_AT(x);                        /* L62B8 */
    c = 0;                                   /* L62BA CLC */
    a = rol6502(a, &c);                      /* L62BB ROL */
    a = rol6502(a, &c);                      /* L62BC ROL */
    SAVE1 = x;                               /* L62C0 STX SAVE1 - the RAM side effect stays even though C keeps x in a parameter (NOTES_integration.md "Fixes made" #13) */
    r = cosxtr(a, TANGLE_AT(x));             /* L62BD-L62C4: PHA/LDA TANGLE,X/STX SAVE1/TAX/PLA/JSR COSXTR */
    CYC(21 + ((int8_t)r.x < 0 ? 3 : 5) + 17 + 15);  /* L62C7-L62D0 (PHP/LDX/PLP included), sign-extend, L62D8-L62DD, L62DF-L62E4 (JSR SINXTR included) */
    TEMP1 = r.a;                             /* L62C7 */
    a = r.x;                                 /* L62C9 TXA */
    SINCX_AT(x) = a;                         /* L62CD STA SINCX,X (PHP/PLP simplified away - see header comment) */
    a = ((int8_t)a < 0) ? 0xFF : 0x00;       /* L62D0-L62D6: sign-extend */
    TEMP1 = asl6502(TEMP1, &c);              /* L62D8 */
    SINCX_AT(x) = rol6502(SINCX_AT(x), &c);  /* L62DA */
    a = rol6502(a, &c);                      /* L62DC */
    SINCX_AT(x + 1) = a;                     /* L62DD */

    a = TANGLE_AT(x);                        /* L62DF */
    r = sinxtr(GARBAG, a);                   /* L62E1-L62E4: TAX/LDA GARBAG/JSR SINXTR */
    CYC(21 + ((int8_t)r.x < 0 ? 3 : 5) + 17 + 32 + 6);  /* L62E7-L62F0, sign-extend, L62F8-L62FD, L62FF-L630D, L630F RTS */
    TEMP1 = r.a;                             /* L62E7 */
    a = r.x;                                 /* L62E9 TXA */
    SINCY_AT(x) = a;                         /* L62ED STA SINCY,X (PHP/PLP simplified away) */
    a = ((int8_t)a < 0) ? 0xFF : 0x00;       /* L62F0-L62F6: sign-extend */
    TEMP1 = asl6502(TEMP1, &c);              /* L62F8 */
    SINCY_AT(x) = rol6502(SINCY_AT(x), &c);  /* L62FA */
    a = rol6502(a, &c);                      /* L62FC */
    SINCY_AT(x + 1) = a;                     /* L62FD */

    SHELLX_AT(x) = TPOSX_AT(x);          /* L62FF/L6301 */
    SHELLX_AT(x + 1) = TPOSX_AT(x + 1);  /* L6303/L6305 */
    SHELLY_AT(x) = TPOSY_AT(x);          /* L6307/L6309 */
    SHELLY_AT(x + 1) = TPOSY_AT(x + 1);  /* L630B/L630D */
}

static void m_stop(uint8_t x) { CYC(2); m_nex0(x, K_LIDLE); }  /* L6292: falls into M_NEX0 */
static void m_10s_(uint8_t x) { CYC(5); m_nex0(x, K_HIDLE); }  /* L628E/L6290: BNE M_NEX0, always taken (HIDLE!=0) */
static void m_6s_(uint8_t x)  { CYC(5); OBJCOL = 0x00; m_10s_(x); }  /* L628A/L628C: falls into M_10S_ */

/* M_NEXT ($6271): after moving, check for an object collision (OBJOBJ);
 * back out and either flag/sound a new collision or (if already flagged)
 * just stay backed out. */
static void m_next(uint8_t x)
{
    CYC(6);                                  /* L6271 JSR */
    regs r = objobj(x);                      /* L6271 JSR OBJOBJ */
    CYC(2);                                  /* L6274 BCC */
    if (!r.c) { CYC(1); m_6s_(x); return; }  /* L6274 BCC M_6S_ */
    CYC(6);                                  /* L6276 JSR */
    bakout(x);                               /* L6276 JSR BAKOUT */
    CYC(5);                                  /* L6279 LDA / L627B BNE */
    if (OBJCOL != 0) { CYC(1); m_10s_(x); return; }  /* L6279/L627B: already in collision */
    CYC(8);                                  /* L627D LDA / L627F JSR */
    sndon(K_BOING, x);                       /* L627D/L627F */
    CYC(13);                                 /* L6282-L6288, BNE taken */
    BOUNCE = 0x3F;                           /* L6282/L6284 */
    OBJCOL++;                                /* L6286 (was 0, so this is always nonzero next) */
    m_10s_(x);                               /* L6288 BNE M_10S_, always taken */
}

static void m_f(uint8_t x)  { CYC(6); m_for(x); CYC(9); TRDCTR_AT(x)++; m_next(x); }  /* L6261-L6266: JSR / INC / JMP */
static void m_ff(uint8_t x) { CYC(6); m_for(x); m_f(x); }             /* L625E: extra M_FOR, falls into M_F */
static void m_r(uint8_t x)  { CYC(6); m_rev(x); CYC(6); TRDCTR_AT(x)--; m_next(x); }  /* L626C-L626F, falls into M_NEXT */
static void m_fr(uint8_t x) { CYC(6); m_rev(x); m_r(x); }             /* L6269: extra M_REV, falls into M_R */

static void m_pr(uint8_t x)  { CYC(6); dtangl(x); CYC(6); dtangl(x); CYC(3); m_6s_(x); }  /* L6234-L623A: pivot right */
static void m_pl(uint8_t x)  { CYC(6); itangl(x); CYC(6); itangl(x); CYC(3); m_6s_(x); }  /* L623D-L6243: pivot left */
static void m_ltf(uint8_t x) { CYC(6); itangl(x); CYC(3); m_f(x); }  /* L6246-L6249 */
static void m_ltr(uint8_t x) { CYC(6); itangl(x); CYC(3); m_r(x); }  /* L624C-L624F */
static void m_rtf(uint8_t x) { CYC(6); dtangl(x); CYC(3); m_f(x); }  /* L6252-L6255 */
static void m_rtr(uint8_t x) { CYC(6); dtangl(x); CYC(3); m_r(x); }  /* L6258-L625B */

/* M_TOP ($61FF): MOTION's control dispatcher - reads the tread/fire
 * switches from ALLPOT (inverted so 1=pressed), then the MTAB push/RTS
 * dispatch (table at L621E, 11 word entries covering the inverted nibble
 * 5..15 - decoded by hand, see NOTES_game.md) becomes a plain `switch`
 * per TRANSLATING.md. Codes 0-4 (and the table's own repeated M_STOP-1
 * entries at 8/12/15) all route to m_stop(). */
static void m_top(uint8_t x)
{
    uint8_t a;
    unsigned c;

    CYC(6);                            /* L61FF JSR */
    saveit(x);                         /* L61FF JSR SAVEIT */
    CYC(20);                           /* L6202-L6211 */
    hw_pokey_write(0x0B, x);           /* L6202 STX POTGO ($182B&$0F=$0B) */
    a = hw_pokey_read(0x08);           /* L6205 LDA ALLPOT */
    PL1MOV = a;                        /* L6208 */
    a = (uint8_t)((a ^ 0x0F) & 0x0F);  /* L620B/L620D */

    cmp6502(a, 0x05, &c, NULL);     /* L620F CMP #$05 */
    if (!c) { CYC(1); m_stop(x); return; }  /* L6211 BCC M_STOP */
    CYC(24);                        /* L6213-L621D: MTAB push/RTS dispatch */

    switch (a) {
    case 5:  m_ff(x);  break;
    case 6:  m_pr(x);  break;
    case 7:  m_rtf(x); break;
    case 9:  m_pl(x);  break;
    case 10: m_fr(x);  break;
    case 11: m_ltr(x); break;
    case 13: m_ltf(x); break;
    case 14: m_rtr(x); break;
    default: m_stop(x); break;  /* a == 8, 12, 15 - MTAB's own M_STOP-1 entries */
    }
}

/* MOTION ($61D0): apply the player's tread-lever input for this frame -
 * attract-mode auto-drive (mechanical M_FOR/M_REV + a pivot, gated on
 * LOGO/CRACK), or (in a real game) M_TOP's real switch dispatch, unless
 * the tank is currently flagged as colliding (COLFLG,0 != 0). Always
 * operates on the player tank (x=0). */
void motion(void)
{
    uint8_t x;

    CYC(5);                             /* L61D0 BIT / L61D2 BMI */
    if (ATRACT & 0x80) { CYC(1); goto MOTION_10; }  /* L61D0/L61D2 BIT ATRACT/BMI: "D IF IN GAME" */
    CYC(6);                             /* L61D4 BIT / L61D7 BMI */
    if (LOGO & 0x80) { CYC(7); return; }  /* L61D4/L61D7 BIT LOGO/BMI MOTION_20 (taken + RTS) */
    CYC(5);                             /* L61D9 LDA / L61DB BNE */
    if (CRACK != 0) { CYC(7); return; }  /* L61D9/L61DB (taken + RTS) */

    CYC(7);              /* L61DD LDX / L61DF BIT / L61E1 BVS */
    x = 0x00;            /* L61DD */
    if (FRAME & 0x40) {  /* L61DF/L61E1 BIT FRAME/BVS: bit6 -> V */
        CYC(1 + 6);  /* BVS taken, L61E9 JSR */
        m_rev(x);  /* MOTION_3, L61E9 */
    } else {
        CYC(6);    /* L61E3 JSR */
        m_for(x);  /* L61E3 */
        CYC(3);    /* L61E6 JMP MOTION_4 */
    }

    CYC(5);                       /* L61EC BIT / L61EE BPL */
    if (!(TPOSX_AT(3) & 0x80)) {  /* L61EC/L61EE BIT TPOSX+3/BPL MOTION_6 */
        CYC(1 + 3);  /* BPL taken, L61F5 JMP */
        dtangl(x);  /* MOTION_6: L61F5 JMP DTANGL (tail call) */
        return;
    }
    CYC(2);                             /* L61F0 BVC */
    if (!(TPOSX_AT(3) & 0x40)) { CYC(7); return; }  /* L61F0 BVC MOTION_20 (taken + RTS) */
    CYC(3);                             /* L61F2 JMP */
    itangl(x);                          /* L61F2 JMP ITANGL (tail call) */
    return;

MOTION_10:
    CYC(8);                   /* L61F8 LDX / L61FA LDA / L61FC BEQ */
    x = 0x00;                 /* L61F8 */
    if (COLFLG_AT(x) == 0) {  /* L61FA/L61FC BEQ M_TOP */
        CYC(1);               /* BEQ taken */
        m_top(x);
    } else CYC(6);            /* L61FE RTS */
    /* else MOTION_20: RTS - tank frozen while flagged as colliding. */
}

/* REACT ($63CB): set the robot's ACTION (reaction delay) and SKILL (aim
 * accuracy). See the header comment on react() in bzone_game.h for the
 * entry-`a` caveat.
 *
 * L63D0-L63EB (the ATRACT-bit7-clear path's HITS-based SKILL/ACTION
 * computation) is UNREACHABLE: `STA ACTION` (L63D2) never touches the
 * flags, so `BNE REACT_35` (L63D4) tests whatever `LDA #$30` (L63D0) just
 * set - always Z=0 (0x30 != 0) - so the branch is always taken. Verified
 * by checking every 6502 flag-effect table entry for STA (none: it never
 * affects flags). Translated as an unconditional jump per that proof, not
 * as a runtime branch - see NOTES_game.md "REACT dead code". */
void react(uint8_t a)
{
    unsigned c;

    CYC(8);                            /* L63CB PHA / L63CC BIT / L63CE BMI */
    if (ATRACT & 0x80) { CYC(1); goto REACT_10; }  /* L63CC/L63CE BIT ATRACT/BMI - uses entry `a` from here */

    CYC(8);         /* L63D0 LDA / L63D2 STA / L63D4 BNE (taken) */
    ACTION = 0x30;  /* L63D0/L63D2 */
    goto REACT_35;  /* L63D4: BNE, always taken - see header comment */

REACT_10:
    CYC(4);                            /* L63ED CMP / L63EF BCS */
    cmp6502(a, 0x05, &c, NULL);        /* L63ED CMP #$05 */
    if (c) { CYC(1); goto REACT_20; }  /* L63EF BCS: a>=5 */
    CYC(18);                           /* L63F1-L63FB */
    ACTION = a;                        /* L63F1 (a<5 here) */
    a = 0x05;                          /* L63F3 */
    a = sbc6502(a, ACTION, &c, NULL);  /* L63F5 SBC ACTION - carry-in is the CMP's (0, since we're on the BCC path) */
    a = asl6502(a, &c);                /* L63F7 */
    a = asl6502(a, &c);                /* L63F8 */
    a = asl6502(a, &c);                /* L63F9 */
    a = asl6502(a, &c);                /* L63FA */
    if (a != 0) { CYC(1); goto REACT_30; }  /* L63FB BNE */

REACT_20:
    CYC(2);    /* L63FD LDA */
    a = 0x04;  /* L63FD */

REACT_30:
    CYC(8);                           /* L63FF STA / L6401 BIT / L6403 BMI */
    ACTION = a;                       /* L63FF */
    if (SKILL & 0x80) { CYC(1); goto REACT_35; }  /* L6401/L6403 BIT SKILL/BMI */
    CYC(9);                           /* L6405-L640A */
    a = 0x0A;                         /* L6405 */
    c = 1;                            /* L6407 SEC */
    a = sbc6502(a, SKILL, &c, NULL);  /* L6408 */
    if (!c) {                         /* L640A BCS REACT_40 (negated) */
        CYC(5);    /* L640C LDA / L640E BNE (taken) */
        a = 0x01;  /* L640C */
    } else CYC(1);  /* L640A BCS taken */
    goto REACT_40;  /* L640E BNE, always taken (a=1 here) */

REACT_35:
    CYC(2);    /* L6410 LDA */
    a = 0x0A;  /* L6410 */

REACT_40:
    CYC(15);             /* L6412 ASL / L6413 STA / L6415 PLA / L6416 RTS */
    a = asl6502(a, &c);  /* L6412 */
    SKILL = a;           /* L6413 */
    /* L6415/L6416 PLA/RTS - restores the caller's own A; nothing to do in
     * C (this function's `a` parameter is already a private copy). */
}

/* TR7CHK ($69B5): is the robot the enhanced TR7 tank? Carry convention
 * confirmed twice already (EXPLDE, SHRTCK - see NOTES_game.md): returns 1
 * (carry set) for TR7. No inputs; reads NOR2D3 only. Translated early
 * (out of the brief's routine order) because EXPLDE/SHRTCK/`b_end()`
 * already call it and its body turned out to be this trivial once read. */
int tr7chk(void)
{
    uint8_t a;
    unsigned c = 0;  /* L69B5 CLC */

    CYC(8);                            /* L69B5 CLC / L69B6 LDA / L69B9 BMI */
    a = NOR2D3;                        /* L69B6 */
    if ((int8_t)a < 0) { CYC(7); return (int)c; }  /* L69B9 BMI TR7CHK_10: carry stays clear (taken + RTS) */
    CYC(8);                            /* L69BB CMP / L69BD RTS */
    cmp6502(a, 0x05, &c, NULL);        /* L69BB CMP #$05 */
    return (int)c;                     /* TR7CHK_10: L69BD RTS */
}
