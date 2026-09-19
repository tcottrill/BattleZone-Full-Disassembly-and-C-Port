/* bzstst.c - BZSTST.MAC, power-on (PWRON $7ACC-$7B7C, game path only).
 *
 * The ROM's RESET (and IRQ) vector.  PWRON clears all 1 KB of RAM and all
 * 4 KB of vector RAM (kicking the watchdog in the loop), resets the AVG and
 * the POKEY, then either enters the self test (switch ON) or seeds the
 * display list, the game state and the high-score table and JMPs to MAIN.
 * The self-test path (RAMTST onwards) is PLAN.md's M8.
 *
 * Register-level details that have no C-visible effect are noted, not
 * modelled: LDX #$7F / TXS (the stack pointer), CLD (g.dflag cleared), the
 * INX at $7B54 (X is reloaded two instructions later).
 */
#include "state.h"
#include "hw.h"
#include "bzone_disp.h"  /* batint(), digtys() */
#include "vgut.h"        /* vgcntr/vgvtr/vgvtr1/vgstat/vgadd2/vgjsrl/vghalt/vghex */
#include "bzstst.h"

#define A_FDGTBL 0x7B7Fu  /* FDGTBL: factory initials, 10 x 3 VGMSGA codes (ROM) */

/* ROM tables the self test reads by address (../disasm/bzone_program_rom.asm). */
#define A_TESDAT  0x7D87u  /* TESDAT: Mathbox self-test vectors */
#define A_STEST8  0x7FC6u  /* STEST8: dollar-mech multiplier table, 4 bytes */
#define A_DIAGS   0x7FCAu  /* DIAGS: DIATST picture byte-lengths, 4 bytes */
#define A_DIATST  0x7FCEu  /* DIATST: the 4 diagnostic-step AVG pictures */

int pwron(void)
{
    unsigned i;
    int x, y;

    /* L7ACC/L7ACE LDX #$7F / TXS - stack pointer, not modelled. */
    hw_pokey_write(0x0F, 0x00);                   /* L7ACF/L7AD1 LDA #$00 / STA SKCTL */
    g.dflag = 0;                                  /* L7AD4 CLD */
    for (i = 0; i < 0x100; i++) g.ram[i] = 0x00;  /* L7AD5-L7AD9 TAX / PWRON_10: STA $00,X / INX / BNE - C CLEAR RAM PAGES */
    hw_pokey_write(0x0F, 0x07);                   /* L7ADB/L7ADD LDY #$07 / STY SKCTL - C RESET POKEY */
    for (i = 0; i < 0x100; i++) {                 /* PWRON_12: X = 0..$FF */
        g.ram[0x100 + i] = 0x00;  /* L7AE0 STA $0100,X */
        g.ram[0x200 + i] = 0x00;  /* L7AE3 STA $0200,X */
        g.ram[0x300 + i] = 0x00;  /* L7AE6 STA $0300,X */
        for (y = 0; y < 16; y++)  /* L7AE9-L7B16 STA VECRAM+$000..$F00,X */
            g.vram[(unsigned)y * 0x100u + i] = 0x00;
        hw_wtchdg();  /* L7B19 STA WTCHDG */
    }            /* L7B1C/L7B1D INX / BNE PWRON_12 */
    hw_vstop();  /* L7B1F STA VSTOP - C RESET VECTOR GEN */

    if (!(hw_in0() & K_ST_BIT)) {  /* L7B22-L7B27 LDA HALT / AND #ST_BIT / BNE PWRON_15 */
        hw_sound(0x20);   /* L7B29/L7B2B LDA #$20 / STA SOUND - C ENABLE SOUNDS */
        selftest_boot();  /* L7B2E JMP RAMTST - the self test (bzstst.h) */
        return 1;
    }

    /* PWRON_15 - C INIT VG BUFFER */
    VRAM(A_VECRAM + 0)     = 0x01;  /* L7B31/L7B33 */
    VRAM(A_VECRAM + 1)     = 0xE4;  /* L7B36/L7B38 */
    VRAM(A_VECRAM + 2)     = 0x20;  /* L7B3B/L7B3D */
    VRAM(A_VECRAM + 3)     = 0x20;  /* L7B40 */
    VRAM(A_VECRAM + 0x802) = 0x20;  /* L7B43 */
    VRAM(A_VECRAM + 0x803) = 0x20;  /* L7B46 */

    ACTION = 0x01;               /* L7B49/L7B4B LDA #$01 / STA ACTION - C INIT GAME STATE */
    GOVER  = 0x01;               /* L7B4D STA GOVER */
    hw_pokey_write(0x04, 0xFE);  /* L7B4F/L7B51 LDA #$FE / STA CHAN3F - C SET UP BUZZ BOMB SOUND */
    hw_pokey_write(0x06, 0xFE);  /* L7B54 INX (not modelled) / L7B55 STA CHAN4F */

    for (x = K_HSCNUM * 3 - 1; x >= 0; x -= 3) {  /* L7B58/L7B5A LDX #[HSCNUM*3]-1 / LDY #$00 ; PWRON_20 ... DEX x3 / BPL */
        INITLS_AT(x - 2) = cpu_rd((uint16_t)(A_FDGTBL + x - 2));  /* L7B5C/L7B5F - C INIT HIGH SCORE TABLE */
        HSCTBL_AT(x - 2) = 0x05;                                  /* L7B62/L7B64 LDA #$05 / STA HSCTBL-2,X */
        INITLS_AT(x - 1) = cpu_rd((uint16_t)(A_FDGTBL + x - 1));  /* L7B67/L7B6A */
        INITLS_AT(x)     = cpu_rd((uint16_t)(A_FDGTBL + x));      /* L7B6D/L7B70 */
        /* L7B73 INY - Y is never read (a count nobody uses) */
    }
    batint();  /* L7B79 JSR BATINT - C INIT FOR LOGO */
    return 0;  /* L7B7C JMP MAIN - the caller's loop */
}

/* selftest_boot() - RAMTST ($7BCB) through STEST3's tail ($7D3A): see
 * bzstst.h.  RAMTST and STEST1 are moving-inversion RAM tests that can only
 * find this port's g.ram/g.vram good (bzstst.h) - their bad-memory legs
 * (RAMTST_5..RT_LP_14 $7BFC-$7C41; STEST1_20..STOP1 $7C81-$7CFE, including
 * STEST2's bad-RAM sound/display) are unreachable here and are not
 * translated; the "good" exit is taken directly, with the RAM/VRAM side
 * effects the ROM's successful pass leaves behind. */
static int st_final;  /* in STEST7_70's own loop (the last diag step) */

void selftest_boot(void)
{
    unsigned i;

    st_final = 0;
    /* STOP0 ($7C44-$7C4D): RAMTST's success exit - reinit the stack (not
     * modelled) and zero all of zero page. */
    for (i = 0; i < 0x100; i++) g.ram[i] = 0x00;  /* L7C47-L7C4D */

    /* STEST1 ($7C4F-$7C7E): VG RAM test, RAM pages 1-3 ($100-$3FF) then
     * VECRAM ($2000-$2FFF).  Each tested cell is left holding $88 - the last
     * pattern STEST1_15 writes and verifies before moving to the next cell
     * (L7C56-L7C64).  The real ROM kicks the watchdog once per completed
     * page (L7C69, 19 times); collapsed to one call here (hw_wtchdg() has
     * no observable effect in this port). */
    for (i = 0; i < 0x300; i++) g.ram[0x100 + i] = 0x88;     /* pages 1-3 */
    for (i = 0; i < 0x1000; i++) VRAM(A_VECRAM + i) = 0x88;  /* VECRAM $2000-$2FFF */
    hw_wtchdg();                                             /* L7C69 STA WTCHDG */

    /* STEST3 ($7D01-$7D3A): ROM TEST - EOR checksum (seeded $FF) over 2 KB
     * chunks (8 pages), first of VGROM ($3000-$3FFF), then continuing at
     * PRGROM's base - the literal <[PRGROM/$100] baked into L7D28's LDA
     * immediate, $50 in the ROM (A9 50): after VGROM ends at $4000 the test
     * skips to $5000 - up to $8000; one checksum byte per chunk into PNTTBL
     * (8 chunks, all $00 on a good board). */
    TEMP1 = 0x00;                           /* L7D05 STA TEMP1 */
    TEMP1_AT(1) = (uint8_t)(K_VGROM >> 8);  /* L7D07 LDA #VGROM/$100 */
    {
        unsigned x = 0;
        for (;;) {
            unsigned y;
            uint8_t cksum = 0xFF;  /* L7D0F LDA #$FF */
            TEMP2 = 0x08;          /* L7D0B/L7D0D - 8 pages */
            do {
                for (y = 0; y < 0x100; y++)  /* L7D11-L7D14 */
                    cksum ^= cpu_rd((uint16_t)(((unsigned)TEMP1_AT(1) << 8) | y));
                TEMP1_AT(1)++;  /* L7D16 INC TEMP1+1 */
                hw_wtchdg();    /* L7D18 STA WTCHDG */
                TEMP2--;        /* L7D1B DEC TEMP2 */
            } while (TEMP2 != 0);                                 /* L7D1D BNE STEST3_10 */
            PNTTBL_AT(x) = cksum;                                 /* L7D1F STA PNTTBL,X */
            x++;                                                  /* L7D21 INX */
            if (TEMP1_AT(1) == (uint8_t)((K_VGROM >> 8) + 0x10))  /* L7D22-L7D26 CMP #$40 */
                TEMP1_AT(1) = 0x50;                               /* L7D28/L7D2A LDA #$50 (<[PRGROM/$100], ROM bytes A9 50) - fix: was $40,
                                                                   * which checksummed the empty $4000-$4FFF and showed ROMs 2/3 as bad */
            if (TEMP1_AT(1) >= 0x80) break;  /* L7D2C/L7D2E CMP #$80 / BCC */
        }
    }
    {
        int diag_held = !(hw_in0() & 0x20);  /* L7D32-L7D35 LDA HALT / AND #$20 */
        TEMP3 = diag_held ? 0x00 : 0xFF;     /* L7D30/L7D37/L7D39/L7D3A */
    }
}

/* mbdiag_test() - MBDIAG ($7D3C) + EOMBT ($7DF4) + TESDAT_40's tail
 * ($7DFA-$7E0E): re-run the Mathbox diagnostic against TESDAT, set TEMP3+1
 * (0 = no error, else 'T'/'L'/'H'), sound an alert if the VGROM checksum
 * (PNTTBL byte 0) came back bad, then prime VGSIZE/VGBRIT for the picture
 * STEST4 builds next. */
static void mbdiag_test(void)
{
    unsigned x = 0;  /* L7D3C LDX #$00 */
    uint8_t a = 0x00;
    int carry = 0;

    for (;;) {
        uint8_t spec = cpu_rd((uint16_t)(A_TESDAT + x));  /* L7D3E/L7D3F CLC / LDA TESDAT,X */
        carry = 0;
        if (spec & 0x80) break;  /* L7D42 BMI TDONE */
        hw_mb_write((uint8_t)(spec & 0x1F),
                    cpu_rd((uint16_t)(A_TESDAT + x + 1)));  /* L7D44-L7D4A */
        {
            int y = 8;  /* L7D4D LDY #$08 */
            for (;;) {
                if (!(hw_mb_status() & 0x80)) break;                  /* L7D4F/L7D52 BIT MBSTAT / BPL */
                y--;                                                  /* L7D54 DEY */
                if (y < 0) { a = 0x54; carry = 1; goto have_error; }  /* L7D55/L7D57/L7D59 timeout: 'T' */
            }
        }
        {
            uint8_t spec2 = cpu_rd((uint16_t)(A_TESDAT + x));  /* L7D5B LDA TESDAT,X */
            uint8_t shifted = (uint8_t)(spec2 << 1);           /* L7D5E ASL (D6 -> sign) */
            if (shifted & 0x80) {                              /* L7D60 BPL fails: check low */
                a = hw_mb_ylow();                                 /* L7D62 LDA YLOW */
                if (a != cpu_rd((uint16_t)(A_TESDAT + x + 2))) {  /* L7D65 CMP */
                    a = 0x4C; carry = 1; goto have_error;  /* L7D6A: 'L' */
                }
                x++;  /* L7D6E INX */
            }
            shifted = (uint8_t)(shifted << 1);  /* L7D6F/L7D70 TYA / ASL (D5 -> sign) */
            if (shifted & 0x80) {               /* L7D71 BPL fails: check high */
                a = hw_mb_yhigh();                                /* L7D73 LDA YHIGH */
                if (a != cpu_rd((uint16_t)(A_TESDAT + x + 2))) {  /* L7D76 CMP */
                    a = 0x48; carry = 1; goto have_error;  /* L7D81: 'H' */
                }
                x++;  /* L7D7B INX */
            }
        }
        x += 2;  /* L7D7C/L7D7D INX / INX: skip spec, input */
    }
    goto tdone;
have_error:
    ;
tdone:
    /* TDONE/EOMBT ($7DF4-$7DFA) */
    TEMP3_AT(1) = carry ? a : 0x00;  /* L7DF4/L7DF6/L7DF8 */

    /* TESDAT_40's own tail ($7DFA-$7E0E) */
    if (PNTTBL_AT(0) != 0x00) {  /* L7DFA/L7DFC LDA PNTTBL / BEQ */
        hw_pokey_write(0x00, 0xF0);  /* L7DFE/L7E02 LDA #$F0 / STA POKEY */
        hw_pokey_write(0x01, 0xA2);  /* L7E00/L7E05 LDX #$A2 / STA POKEY+1 */
    }
    VGSIZE = 0x02;  /* L7E08/L7E0A */
    VGBRIT = 0x00;  /* L7E0C/L7E0E */
}

/* stest6() - STEST6 ($7E69-$7F27): the plain self-test screen - the HATCH
 * picture, the alphabet, all 16 DIP-switch bits, the coin-mech multipliers,
 * the 8 ROM checksums (2 hex digits each), the Mathbox error code (if any),
 * then VGHALT.  Falls straight through into STEST7 in the ROM; the caller
 * (selftest_pass()) calls stest7() next. */
static void stest6(void)
{
    unsigned x;
    uint8_t opt1, opt2, a;
    int b;

    vgjsrl((uint8_t)(A_HATCH >> 8), (uint8_t)(A_HATCH & 0xFF));    /* L7E69-L7E6D LAH/LXL HATCH / JSR VGJSRL */
    vgcntr();                                                      /* L7E70 */
    vgvtr1(0x82, 0xA7);                                            /* L7E73-L7E77 */
    vgjsrl((uint8_t)(A_VGMSGA >> 8), (uint8_t)(A_VGMSGA & 0xFF));  /* L7E7A-L7E7E: JSRL VGMSGA (alphabet) */
    vgcntr();                                                      /* L7E81 */
    vgvtr1(0xD3, 0x13);                                            /* L7E84-L7E88 */

    /* L7E8B-L7EA8: all 16 DIP-switch bits, LSB first - OPTON2 then OPTION,
     * both inverted (so an active/closed switch shows as 1). */
    opt1 = (uint8_t)(hw_option() ^ 0xFF);  /* L7E8D-L7E92 */
    opt2 = (uint8_t)(hw_opton2() ^ 0xFF);  /* L7E94/L7E97 */
    for (b = 0; b < 8; b++) { vghex((uint8_t)(opt2 & 1)); opt2 = (uint8_t)(opt2 >> 1); }
    for (b = 0; b < 8; b++) { vghex((uint8_t)(opt1 & 1)); opt1 = (uint8_t)(opt1 >> 1); }

    vgcntr();                                           /* L7EAA */
    vgvtr1(0xFB, 0x1D);                                 /* L7EAD-L7EB1 */
    vghex(0x01);                                        /* L7EB4/L7EB6: left coin mech = 1 */
    vghex((uint8_t)(((hw_opton2() & 0x10) >> 4) + 1));  /* L7EB9-L7EC4: center mech = 1 or 2 */
    x = (unsigned)((hw_opton2() & 0x0C) >> 2);          /* L7EC7-L7ECE */
    vghex(cpu_rd((uint16_t)(A_STEST8 + x)));            /* L7ECF/L7ED2: dollar mech multiplier */

    /* L7ED5-L7F09: the 8 ROM checksums, one line each, skipping zero
     * (untested/absent) entries. */
    {
        uint8_t liney = 0x16;     /* L7ED5/L7ED7 */
        for (x = 8; x-- > 0; ) {  /* L7ED9 LDX #$07 */
            uint8_t cksum = PNTTBL_AT(x);     /* L7EDB LDA PNTTBL,X */
            if (cksum == 0) continue;         /* L7EDD BEQ STEST6_30 */
            g.ram[0] = cksum;                 /* L7EDF STA $00 */
            vgcntr();                         /* L7EE3 */
            vgvtr1(0xA0, liney);              /* L7EE6-L7EF0 (position, old Y) */
            liney = (uint8_t)(liney - 0x08);  /* L7EE8-L7EEC */
            vghex((uint8_t)x);                /* L7EF3/L7EF5: ROM # */
            vgvtr1(0x06, 0x00);               /* L7EF8-L7EFC */
            digtys(0x00, 0x01);               /* L7EFF-L7F03 */
        }
    }

    vgcntr();            /* L7F0B */
    vgvtr1(0x60, 0x16);  /* L7F0E-L7F12 */
    a = TEMP3_AT(1);     /* L7F15 LDA TEMP3+1 */
    if (a != 0) {        /* L7F17 BEQ */
        unsigned y = (unsigned)((uint8_t)(a - 0x36)) << 1;  /* L7F19-L7F1D */
        vgadd2(cpu_rd((uint16_t)(A_VGMSGA + y)),
               cpu_rd((uint16_t)(A_VGMSGA + y + 1)));  /* L7F1E-L7F24 */
    }
    vghalt();  /* L7F27 */
}

/* stest4() - STEST4 ($7E10-$7E66): wait ~16 ms (3 kHz edges), wait for the
 * AVG to halt, reset VGLIST to VECRAM, then either run stest6() (plain self
 * test) or open the AVG clip window and copy in this diag step's DIATST
 * vector pattern (diagnostic-step mode).  Both legs fall through to STEST7
 * in the ROM - the caller (selftest_pass()) calls stest7() next either way. */
static void stest4(void)
{
    uint8_t x, len;
    int i, y;

    for (i = 0x28; ; i--) {  /* L7E10-L7E1D: ~16 ms, 3 kHz edges */
        while (!(hw_in0() & 0x80)) { }  /* L7E12/L7E15 BIT HALT / BPL */
        while (hw_in0() & 0x80) { }     /* L7E17/L7E1A BIT HALT / BMI */
        if (i <= 0) break;              /* L7E1C/L7E1D DEX / BPL */
    }
    while (!(hw_in0() & 0x40)) { }  /* L7E1F/L7E22 BIT HALT / BVC STEST4_10 - "IF NO HALT, WTCHDGDOG WILL RESET" */
    hw_wtchdg();                                                   /* L7E24 STA WTCHDG - once, after HALT (fix: was kicked inside the wait, so a
                                                                    * list that never halts hung instead of letting the watchdog reset the board) */

    VGLIST       = (uint8_t)(A_VECRAM & 0xFF);  /* L7E27/L7E29 */
    VGLIST_AT(1) = (uint8_t)(A_VECRAM >> 8);    /* L7E2B/L7E2D */

    if (TEMP3 & 0x80) { stest6(); return; }  /* L7E2F/L7E31 BIT TEMP3 / BMI STEST6 */

    /* Diagnostic-step mode: open the clip window over the whole tube. */
    vgcntr();                 /* L7E33 */
    vgvtr(0x81, 0x81, 0x00);  /* L7E36-L7E3B */
    vgstat(0x00, 0x03);       /* L7E3E/L7E40 - Y = 3, VGVTR's exit (fix: was 0) */
    vgcntr();                 /* L7E43 */
    vgvtr(0x7F, 0x7F, 0x00);  /* L7E46-L7E4B */
    vgstat(0x02, 0x03);       /* L7E4E/L7E50 - Y = 3 again */

    /* the pattern goes at (VGLIST),Y - after the window setup above, i.e.
     * VECRAM+$10 (fix: was copied to VECRAM itself, over the window) */
    x = TEMP3;                                    /* L7E53 LDX TEMP3 */
    len = cpu_rd((uint16_t)(A_DIAGS + x));        /* L7E55 LDY DIAGS,X */
    cpu_wr((uint16_t)(VGLIST_ADDR + len), 0x20);  /* L7E58/L7E5A STA (VGLIST),Y: HALT byte at the end */
    for (y = (int)len - 2; y >= 0; y--)           /* L7E5C-L7E64 */
        cpu_wr((uint16_t)(VGLIST_ADDR + (unsigned)y), cpu_rd((uint16_t)(A_DIATST + y)));  /* L7E5E/L7E61: copy the pattern */
}

/* diagdb() - DIAGDB ($7FB9-$7FC5): edge-detect the diag-step button (IN0
 * b5, active low) against its last-seen state in SAVE1.  Returns nonzero
 * ("D IF DIAG"/"D IF PUSHED" at the two call sites) on a falling edge. */
static int diagdb(void)
{
    uint8_t h = hw_in0();  /* L7FB9 LDA HALT */
    uint8_t old = SAVE1;
    uint8_t a = (uint8_t)((h ^ old) & old);  /* L7FBD/L7FBF EOR SAVE1 / AND SAVE1 */
    SAVE1 = h;                               /* L7FC1 STY SAVE1 */
    return a & 0x20;                         /* L7FC3/L7FC5 AND #$20 / RTS */
}

/* stest7() - STEST7 ($7F2A-$7FB6): start the AVG on the picture stest4()/
 * stest6() just built, then either scan the diag-step switches (TEMP3 bit7
 * clear) or scan the coin/tread/fire/start switches (plain self test).
 * Returns 1 where the ROM does "JMP PWRON" (L7F7B, L7FB6); 0 to be called
 * again (covers every "JMP MBDIAG" edge - STEST7_45, and the diag-step
 * loop's own re-entry - since selftest_pass() always restarts the loop from
 * MBDIAG on the next call, matching bzstst.h's documented contract). */
static int stest7_65(void);
static int stest7_70(void);
static int stest7_scan(void);

static int stest7(void)
{
    /* L7F2A STA GOADD: BZSTST always targets VGLIST, which stest4() always
     * sets to VECRAM ($2000) - this port passes VGLIST's own high byte
     * rather than tracking the ROM's incidental leftover A value across the
     * STEST4/STEST6 branches (see NOTES_bzstst.md). */
    hw_goadd(VGLIST_AT(1));

    /* Fix (user report "F1 does nothing"): the branch sense was inverted and
     * the plain self test never looked at the diag button.  ROM: BIT TEMP3 /
     * BPL STEST7_60 - bit7 CLEAR (diag-step mode) -> STEST7_60: JSR DIAGDB /
     * BEQ STEST7_45 (JMP MBDIAG) else STEST7_65; bit7 SET (plain self test)
     * -> fall through: JSR DIAGDB / BNE STEST7_65 (a press starts stepping,
     * TEMP3 $FF -> $00), else the switch scan below. */
    if (!(TEMP3 & 0x80)) {  /* L7F2D/L7F2F BIT TEMP3 / BPL STEST7_60 */
        if (!diagdb()) return 0;  /* STEST7_60 L7F7E/L7F81 JSR DIAGDB / BEQ STEST7_45 -> JMP MBDIAG */
        return stest7_65();
    }
    if (diagdb()) return stest7_65();  /* L7F31/L7F34 JSR DIAGDB / BNE STEST7_65 */
    return stest7_scan();
}

/* STEST7_65 ($7F83-$7F89): next diag step; steps 0-3 go round MBDIAG
 * again (the DIATST patterns), step 4 enters STEST7_70's own loop. */
static int stest7_65(void)
{
    TEMP3++;                            /* L7F83 INC TEMP3 */
    if ((TEMP3 & 0xFC) == 0) return 0;  /* L7F85-L7F89: step 0-3 -> JMP MBDIAG */
    st_final = 1;
    return stest7_70();
}

/* STEST7_70/75 ($7F8B-$7FB6): the final diag-mode screen - close the
 * window, clock the Mathbox input shift register, and loop HERE (L7FB4 BEQ
 * STEST7_70, not back to MBDIAG) until one more diag press -> JMP PWRON.
 * selftest_pass() calls this directly while st_final is set. */
static int stest7_70(void)
{
    {
        hw_wtchdg();                 /* L7F8D */
        hw_sound(0x00);              /* L7F90 */
        hw_mb_write(0x00, 0x00);     /* L7F93 STA MBOXIN */
        hw_pokey_write(0x00, 0x00);  /* L7F96 STA POKEY */
        (void)hw_mb_status();        /* L7F99 LDA MSTAT */
        (void)hw_mb_yhigh();         /* L7F9C LDA MOUTHI */
        (void)hw_mb_ylow();          /* L7F9F LDA MOUTLO */
        hw_sound(0x01);              /* L7FA2/L7FA4 */
        {
            uint8_t val = 0x01, carry = 0;  /* L7FA2 A=$01 / L7FA9 CLC */
            int i;
            for (i = 0x1F; i >= 0; i--) {  /* L7FA7 LDX #$1F .. STEST7_75 */
                uint8_t new_carry;
                hw_mb_write((uint8_t)i, val);  /* L7FAA STA MBOXIN,X */
                new_carry = (uint8_t)(val >> 7);
                val = (uint8_t)((val << 1) | carry);  /* L7FAD ROL */
                carry = new_carry;
            }
        }
        return diagdb() ? 1 : 0;  /* L7FB1/L7FB4/L7FB6 */
    }
}

/* L7F36-L7F7B: the plain self test's switch scan (STEST7 fall-through). */
static int stest7_scan(void)
{
    uint8_t a, x;
    unsigned y;

    /* L7F36-L7F76: plain self-test mode - popcount the tread/fire/start
     * switches (ALLPOT) and the coin/slam IN0 nibble (inverted), click a
     * sound when the total active count changes. */
    hw_pokey_write(0x0B, 0x00);  /* L7F36 STA POTGO */
    {
        uint8_t v = (uint8_t)(hw_pokey_read(0x08) & 0x7F);          /* L7F3B/L7F3E LDA ALLPOT / AND #$7F */
        for (y = 0; v != 0; v = (uint8_t)(v >> 1)) if (v & 1) y++;  /* L7F40-L7F46 */
        v = (uint8_t)((hw_in0() & 0x0F) ^ 0x0F);                    /* L7F48-L7F4D */
        for (; v != 0; v = (uint8_t)(v >> 1)) if (v & 1) y++;       /* L7F4F-L7F55 */
    }
    if (y == SAVE0) {  /* L7F57/L7F59 CPY SAVE0 / BEQ */
        a = 0x00; x = 0x00;  /* L7F63: no change, no sound */
    } else {
        a = 0xA7;                       /* L7F5B */
        x = (y < SAVE0) ? 0x20 : 0x80;  /* L7F5D-L7F67 */
    }
    SAVE0 = (uint8_t)y;       /* L7F69 STY SAVE0 */
    hw_pokey_write(0x03, a);  /* L7F6B STA CHAN2V */
    hw_pokey_write(0x02, x);  /* L7F6E STX CHAN2F */

    if (hw_in0() & K_ST_BIT) return 1;  /* L7F71-L7F7B: switch off -> JMP PWRON */
    return 0;                           /* L7F78: JMP MBDIAG */
}

/* selftest_pass() - see bzstst.h: one MBDIAG/STEST4/STEST6-STEST7 loop
 * iteration, in ROM order. */
int selftest_pass(void)
{
    if (st_final) return stest7_70();  /* L7FB4 BEQ STEST7_70: its own loop */
    mbdiag_test();
    stest4();
    return stest7();
}
