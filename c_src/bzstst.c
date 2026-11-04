/* bzstst.c - BZSTST.MAC, power-on (PWRON $7ACC-$7B7C) and the self test.
 *
 * The ROM's RESET (and IRQ) vector.  PWRON clears all 1 KB of RAM and all
 * 4 KB of vector RAM (kicking the watchdog as it goes), resets the AVG and
 * the POKEY, then either enters the self test (switch ON) or seeds the
 * display list, the game state and the high-score table and starts MAIN.
 * Neither power-on nor the self test is cycle-counted: the self test is
 * checked against the ROM's picture, not in lockstep.
 */
#include <string.h>
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
    unsigned byte_offset;

    hw_pokey_write(0x0F, 0x00);  /* hold POKEY in reset */
    memset(g.ram, 0, 0x100);     /* C CLEAR RAM PAGES */
    hw_pokey_write(0x0F, 0x07);  /* C RESET POKEY */
    for (byte_offset = 0; byte_offset < 0x100; byte_offset++) {
        /* Pages 1-3 of RAM and all 16 pages of vector RAM, one column at a
         * time, kicking the watchdog after each column. */
        for (unsigned page = 1; page < 4; page++)
            g.ram[page * 0x100u + byte_offset] = 0x00;
        for (unsigned page = 0; page < 16; page++)
            g.vram[page * 0x100u + byte_offset] = 0x00;
        hw_wtchdg();
    }
    hw_vstop();  /* C RESET VECTOR GEN */

    if (!(hw_in0() & K_ST_BIT)) {
        hw_sound(0x20);   /* C ENABLE SOUNDS */
        selftest_boot();
        return 1;
    }

    /* C INIT VG BUFFER: a JMP to the lower buffer, and a HALT at the head of
     * each buffer. */
    VRAM(A_VECRAM + 0)     = 0x01;
    VRAM(A_VECRAM + 1)     = 0xE4;
    VRAM(A_VECRAM + 2)     = 0x20;
    VRAM(A_VECRAM + 3)     = 0x20;
    VRAM(A_VECRAM + 0x802) = 0x20;
    VRAM(A_VECRAM + 0x803) = 0x20;

    ACTION = 0x01;               /* C INIT GAME STATE */
    GOVER  = 0x01;
    hw_pokey_write(0x04, 0xFE);  /* C SET UP BUZZ BOMB SOUND */
    hw_pokey_write(0x06, 0xFE);

    /* C INIT HIGH SCORE TABLE: factory initials, every score 500 (05 in the
     * high byte); the table is filled from the last entry back. */
    for (int entry = K_HSCNUM - 1; entry >= 0; entry--) {
        unsigned first = (unsigned)entry * 3u;
        INITLS_AT(first)     = cpu_rd((uint16_t)(A_FDGTBL + first));
        HSCTBL_AT(first)     = 0x05;
        INITLS_AT(first + 1) = cpu_rd((uint16_t)(A_FDGTBL + first + 1));
        INITLS_AT(first + 2) = cpu_rd((uint16_t)(A_FDGTBL + first + 2));
    }
    batint();  /* C INIT FOR LOGO */
    return 0;  /* the caller enters MAIN */
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
    unsigned byte_offset;

    st_final = 0;
    /* STOP0 ($7C44-$7C4D): RAMTST's success exit - reinit the stack (not
     * modelled) and zero all of zero page. */
    for (byte_offset = 0; byte_offset < 0x100; byte_offset++) g.ram[byte_offset] = 0x00;

    /* STEST1 ($7C4F-$7C7E): VG RAM test, RAM pages 1-3 ($100-$3FF) then
     * VECRAM ($2000-$2FFF).  Each tested cell is left holding $88 - the last
     * pattern STEST1_15 writes and verifies before moving to the next cell.
     * The real ROM kicks the watchdog once per completed page (19 times);
     * collapsed to one call here (hw_wtchdg() has no observable effect in
     * this port). */
    for (byte_offset = 0; byte_offset < 0x300; byte_offset++) g.ram[0x100 + byte_offset] = 0x88;     /* pages 1-3 */
    for (byte_offset = 0; byte_offset < 0x1000; byte_offset++) VRAM(A_VECRAM + byte_offset) = 0x88;  /* VECRAM $2000-$2FFF */
    hw_wtchdg();

    /* STEST3 ($7D01-$7D3A): ROM TEST - EOR checksum (seeded $FF) over 2 KB
     * chunks (8 pages), first of VGROM ($3000-$3FFF), then from PRGROM's
     * base ($5000) up to $8000; one checksum byte per chunk into PNTTBL
     * (8 chunks, all $00 on a good board). */
    TEMP1 = 0x00;
    TEMP1_AT(1) = (uint8_t)(K_VGROM >> 8);
    {
        unsigned rom_chunk = 0;
        for (;;) {
            unsigned page_offset;
            uint8_t cksum = 0xFF;
            TEMP2 = 0x08;  /* 8 pages */
            do {
                for (page_offset = 0; page_offset < 0x100; page_offset++)
                    cksum ^= cpu_rd((uint16_t)(((unsigned)TEMP1_AT(1) << 8) | page_offset));
                TEMP1_AT(1)++;
                hw_wtchdg();
                TEMP2--;
            } while (TEMP2 != 0);
            PNTTBL_AT(rom_chunk) = cksum;
            rom_chunk++;
            if (TEMP1_AT(1) == (uint8_t)((K_VGROM >> 8) + 0x10))
                TEMP1_AT(1) = 0x50;  /* skip the empty $4000-$4FFF: continue at PRGROM */
            if (TEMP1_AT(1) >= 0x80) break;
        }
    }
    {
        int diag_held = !(hw_in0() & 0x20);
        TEMP3 = diag_held ? 0x00 : 0xFF;
    }
}

/* mbdiag_test() - MBDIAG ($7D3C) + EOMBT ($7DF4) + TESDAT_40's tail
 * ($7DFA-$7E0E): re-run the Mathbox diagnostic against TESDAT, set TEMP3+1
 * (0 = no error, else 'T'/'L'/'H'), sound an alert if the VGROM checksum
 * (PNTTBL byte 0) came back bad, then prime VGSIZE/VGBRIT for the picture
 * STEST4 builds next. */
static void mbdiag_test(void)
{
    unsigned record = 0;
    uint8_t error = 0;

    for (;;) {
        uint8_t spec = cpu_rd((uint16_t)(A_TESDAT + record));
        if (spec & 0x80) break;
        hw_mb_write((uint8_t)(spec & 0x1F),
                    cpu_rd((uint16_t)(A_TESDAT + record + 1)));
        {
            unsigned polls;
            for (polls = 0; polls < 9; ++polls)
                if (!(hw_mb_status() & 0x80)) break;
            if (polls == 9) { error = 'T'; break; }
        }
        {
            uint8_t spec2 = cpu_rd((uint16_t)(A_TESDAT + record));
            if (spec2 & 0x40) {  /* Check the low result when requested. */
                uint8_t actual = hw_mb_ylow();
                if (actual != cpu_rd((uint16_t)(A_TESDAT + record + 2))) {
                    error = 'L'; break;
                }
                ++record;
            }
            if (spec2 & 0x20) {  /* Check the high result when requested. */
                uint8_t actual = hw_mb_yhigh();
                if (actual != cpu_rd((uint16_t)(A_TESDAT + record + 2))) {
                    error = 'H'; break;
                }
                ++record;
            }
        }
        record += 2;
    }
    /* TDONE/EOMBT ($7DF4-$7DFA) */
    TEMP3_AT(1) = error;

    /* TESDAT_40's own tail ($7DFA-$7E0E) */
    if (PNTTBL_AT(0) != 0x00) {
        hw_pokey_write(0x00, 0xF0);
        hw_pokey_write(0x01, 0xA2);
    }
    VGSIZE = 0x02;
    VGBRIT = 0x00;
}

/* stest6() - STEST6 ($7E69-$7F27): the plain self-test screen - the HATCH
 * picture, the alphabet, all 16 DIP-switch bits, the coin-mech multipliers,
 * the 8 ROM checksums (2 hex digits each), the Mathbox error code (if any),
 * then VGHALT.  Falls straight through into STEST7 in the ROM; the caller
 * (selftest_pass()) calls stest7() next. */
static void stest6(void)
{
    unsigned table_index;
    uint8_t opt1, opt2, error_code;
    int switch_bit;

    vgjsrl((uint8_t)(A_HATCH >> 8), (uint8_t)(A_HATCH & 0xFF));
    vgcntr();
    vgvtr1(0x82, 0xA7);
    vgjsrl((uint8_t)(A_VGMSGA >> 8), (uint8_t)(A_VGMSGA & 0xFF));  /* JSRL VGMSGA (alphabet) */
    vgcntr();
    vgvtr1(0xD3, 0x13);

    /* All 16 DIP-switch bits, LSB first - OPTON2 then OPTION,
     * both inverted (so an active/closed switch shows as 1). */
    opt1 = (uint8_t)(hw_option() ^ 0xFF);
    opt2 = (uint8_t)(hw_opton2() ^ 0xFF);
    for (switch_bit = 0; switch_bit < 8; switch_bit++) { vghex((uint8_t)(opt2 & 1)); opt2 = (uint8_t)(opt2 >> 1); }
    for (switch_bit = 0; switch_bit < 8; switch_bit++) { vghex((uint8_t)(opt1 & 1)); opt1 = (uint8_t)(opt1 >> 1); }

    vgcntr();
    vgvtr1(0xFB, 0x1D);
    vghex(0x01);  /* left coin mech = 1 */
    vghex((uint8_t)(((hw_opton2() & 0x10) >> 4) + 1));  /* center mech = 1 or 2 */
    table_index = (unsigned)((hw_opton2() & 0x0C) >> 2);
    vghex(cpu_rd((uint16_t)(A_STEST8 + table_index)));  /* dollar mech multiplier */

    /* The 8 ROM checksums, one line each, skipping zero
     * (untested/absent) entries. */
    {
        uint8_t liney = 0x16;
        for (table_index = 8; table_index-- > 0; ) {
            uint8_t cksum = PNTTBL_AT(table_index);
            if (cksum == 0) continue;
            g.ram[0] = cksum;
            vgcntr();
            vgvtr1(0xA0, liney);
            liney = (uint8_t)(liney - 0x08);
            vghex((uint8_t)table_index);  /* ROM # */
            vgvtr1(0x06, 0x00);
            digtys(0x00, 0x01);
        }
    }

    vgcntr();
    vgvtr1(0x60, 0x16);
    error_code = TEMP3_AT(1);
    if (error_code != 0) {
        unsigned glyph_offset = (unsigned)((uint8_t)(error_code - 0x36)) << 1;
        vgadd2(cpu_rd((uint16_t)(A_VGMSGA + glyph_offset)),
               cpu_rd((uint16_t)(A_VGMSGA + glyph_offset + 1)));
    }
    vghalt();
}

/* stest4() - STEST4 ($7E10-$7E66): wait ~16 ms (3 kHz edges), wait for the
 * AVG to halt, reset VGLIST to VECRAM, then either run stest6() (plain self
 * test) or open the AVG clip window and copy in this diag step's DIATST
 * vector pattern (diagnostic-step mode).  Both legs fall through to STEST7
 * in the ROM - the caller (selftest_pass()) calls stest7() next either way. */
static void stest4(void)
{
    uint8_t diagnostic_step, pattern_length;
    int clock_edge, byte_offset;

    for (clock_edge = 0x28; ; clock_edge--) {  /* ~16 ms, 3 kHz edges */
        while (!(hw_in0() & 0x80)) { }
        while (hw_in0() & 0x80) { }
        if (clock_edge <= 0) break;
    }
    while (!(hw_in0() & 0x40)) { }  /* "IF NO HALT, WTCHDGDOG WILL RESET" */
    hw_wtchdg();  /* once, after HALT: a list that never halts lets the watchdog reset the board */

    VGLIST       = (uint8_t)(A_VECRAM & 0xFF);
    VGLIST_AT(1) = (uint8_t)(A_VECRAM >> 8);

    if (TEMP3 & 0x80) { stest6(); return; }

    /* Diagnostic-step mode: open the clip window over the whole tube. */
    vgcntr();
    vgvtr(0x81, 0x81, 0x00);
    vgstat(0x00, 0x03);  /* was 0) */
    vgcntr();
    vgvtr(0x7F, 0x7F, 0x00);
    vgstat(0x02, 0x03);  /* Y = 3 again */

    /* the pattern goes at (VGLIST),Y - after the window setup above, i.e.
     * VECRAM+$10 (fix: was copied to VECRAM itself, over the window) */
    diagnostic_step = TEMP3;
    pattern_length = cpu_rd((uint16_t)(A_DIAGS + diagnostic_step));
    cpu_wr((uint16_t)(VGLIST_ADDR + pattern_length), 0x20);  /* HALT byte at the end */
    for (byte_offset = (int)pattern_length - 2; byte_offset >= 0; byte_offset--)
        cpu_wr((uint16_t)(VGLIST_ADDR + (unsigned)byte_offset), cpu_rd((uint16_t)(A_DIATST + byte_offset)));  /* copy the pattern */
}

/* diagdb() - DIAGDB ($7FB9-$7FC5): edge-detect the diag-step button (IN0
 * b5, active low) against its last-seen state in SAVE1.  Returns nonzero
 * ("D IF DIAG"/"D IF PUSHED" at the two call sites) on a falling edge. */
static int diagdb(void)
{
    uint8_t switches = hw_in0();
    int pressed = (SAVE1 & 0x20) && !(switches & 0x20);
    SAVE1 = switches;
    return pressed;
}

/* stest7() - STEST7 ($7F2A-$7FB6): start the AVG on the picture stest4()/
 * stest6() just built, then either scan the diag-step switches (TEMP3 bit7
 * clear) or scan the coin/tread/fire/start switches (plain self test).
 * Returns 1 where the ROM restarts at PWRON; 0 to be called again (every
 * path back to MBDIAG, since selftest_pass() always restarts the loop from
 * MBDIAG on the next call, matching bzstst.h's documented contract). */
static int stest7_65(void);
static int stest7_70(void);
static int stest7_scan(void);

static int stest7(void)
{
    /* Start the AVG at VGLIST, which stest4() always sets to VECRAM ($2000)
     * (see NOTES_bzstst.md). */
    hw_goadd(VGLIST_AT(1));

    /* Diagnostic-step mode (TEMP3 bit 7 clear): a diag press advances to the
     * next step, otherwise go round MBDIAG again.  Plain self test: a diag
     * press starts stepping (TEMP3 $FF -> $00), otherwise scan the switches. */
    if (!(TEMP3 & 0x80)) {
        if (!diagdb()) return 0;
        return stest7_65();
    }
    if (diagdb()) return stest7_65();
    return stest7_scan();
}

/* STEST7_65 ($7F83-$7F89): next diag step; steps 0-3 go round MBDIAG
 * again (the DIATST patterns), step 4 enters STEST7_70's own loop. */
static int stest7_65(void)
{
    TEMP3++;
    if ((TEMP3 & 0xFC) == 0) return 0;
    st_final = 1;
    return stest7_70();
}

/* STEST7_70/75 ($7F8B-$7FB6): the final diag-mode screen - close the
 * window, clock the Mathbox input shift register, and repeat this screen
 * (not MBDIAG) until one more diag press restarts at PWRON.
 * selftest_pass() calls this directly while st_final is set. */
static int stest7_70(void)
{
    {
        hw_wtchdg();
        hw_sound(0x00);
        hw_mb_write(0x00, 0x00);
        hw_pokey_write(0x00, 0x00);
        (void)hw_mb_status();
        (void)hw_mb_yhigh();
        (void)hw_mb_ylow();
        hw_sound(0x01);
        {
            int i;
            for (i = 0x1F; i >= 0; i--) {
                /* Walking-one diagnostic: eight data bits then a blank. */
                unsigned phase = (31u - (unsigned)i) % 9;
                hw_mb_write((uint8_t)i, (uint8_t)(1u << phase));
            }
        }
        return diagdb() ? 1 : 0;
    }
}

/* The plain self test's switch scan ($7F36-$7F7B): a tone rises or falls
 * whenever the number of closed switches changes. */
/* Count active switches. */
static unsigned active_switches(uint8_t switches)
{
    unsigned count = 0;
    while (switches) {
        switches &= (uint8_t)(switches - 1);
        ++count;
    }
    return count;
}

static int stest7_scan(void)
{
    unsigned count;
    uint8_t volume = 0, frequency = 0;
    hw_pokey_write(0x0B, 0);
    count = active_switches((uint8_t)(hw_pokey_read(0x08) & 0x7F));
    count += active_switches((uint8_t)(~hw_in0() & 0x0F));
    if (count != SAVE0) {
        volume = 0xA7;
        frequency = count < SAVE0 ? 0x20 : 0x80;
    }
    SAVE0 = (uint8_t)count;
    hw_pokey_write(0x03, volume);
    hw_pokey_write(0x02, frequency);
    return (hw_in0() & K_ST_BIT) != 0;
}

/* selftest_pass() - see bzstst.h: one MBDIAG/STEST4/STEST6-STEST7 loop
 * iteration, in ROM order. */
int selftest_pass(void)
{
    if (st_final) return stest7_70();  /* its own loop */
    mbdiag_test();
    stest4();
    return stest7();
}
