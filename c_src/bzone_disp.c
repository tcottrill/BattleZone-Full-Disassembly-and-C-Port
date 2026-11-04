/* Battlezone display composition: scene overlays, scores, scenery, object
 * vectors and radar. Native calculations retain RAM scratch results required
 * by callers and the ROM oracle. CYC annotations preserve device timing. */
#include "state.h"
#include "game.h"
#include "hw.h"
#include "bzone_math.h"
#include "bzsoun.h"
#include "bzone_game.h"
#include "bzone_disp.h"

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
    CYC(6);
    vgcntr();  /* C SET UPPER WINDOW */
    CYC(12);
    vgvtr(0x7F, 0x7F, 0x00);
    CYC(5);
    /* Preserve the original window command's fixed brightness of three. */
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
    CYC(6);
    hw_pokey_write(0x07, 0);
    uint8_t segment = 0;
    for (;;) {
        CYC(17);
        uint8_t low = cpu_rd((uint16_t)(A_CRACKS + segment));
        uint8_t high = cpu_rd((uint16_t)(A_CRACKS + segment + 1));
        TEMP1 = segment;
        vgadd2(low, high);
        segment = (uint8_t)(TEMP1 + 2);
        CYC(12);
        if (segment >= CRACK) { CYC(1); break; }
        CYC(4);
        if (segment < 16) { CYC(1); continue; }
        CYC(7);
        if (CRACK <= 0x2C) { CYC(1); break; }
        CYC(5);
        if (FIRECT != 0) { CYC(1); CYC(3); return PASS_SIG_BEND; }

        /* Crack animation has finished; either enter scores or respawn. */
        CYC(17);
        CRACK = STATE = COLFLG = FTIMER = 0;
        if (GOVER != 0) {
            CYC(9);
            ATRACT = OBJCOL = 0;
            return PASS_SIG_CKSCOR;
        }
        CYC(1);
        CYC(9);
        ACTION = 0x30;
        uint8_t position = hw_pokey_read(0x0A);
        CYC(10);
        RGOAL = TPOSX = position;
        position = hw_pokey_read(0x0A);
        CYC(3);
        TPOSY = position;

        /* Keep the independent random sampling times while finding free ground. */
        for (;;) {
            CYC(4);
            position = hw_pokey_read(0x0A) & 0x3F;
            CYC(9);
            TPOSX_AT(1) = position;
            position = hw_pokey_read(0x0A);
            CYC(11);
            TPOSY_AT(1) = position;
            object_contact collision = objobj(0);
            CYC(2);
            if (!collision.touching) break;
            CYC(1);
        }
        CYC(18);
        OBJCOL = EIRNGE = 0;
        TANGLE = hw_pokey_read(0x0A);
        if (R2D3FL & 0x80) {
            CYC(6); tankck();
            CYC(3);
            return PASS_SIG_BEND;
        }
        CYC(1);
        CYC(5);
        if (COLFLG_AT(2) != 0) CYC(1);
        else { CYC(6); robchk(); }
        CYC(3);
        return PASS_SIG_BEND;
    }
    CRACK = (uint8_t)(CRACK + 2);
    CYC(12);
    if (CRACK != 0) { CYC(4); return PASS_SIG_BEND; }
    battle();  /* Preserve the original wraparound continuation. */
    return PASS_SIG_YIELD;
}

/*==============================================================================
 * BATTLE ($5268) - BATTLE ZONE LOGO. TRAP1 security checksum computed here
 * (attract mode only) over the vector buffer being built - translate the
 * checksum literally, it reads vector RAM through (TBLPTR),Y.
 *============================================================================*/
static void draw_logo_scene(void);

void battle(void)
{
    int checksum = 0;

    CYC(5);
    if (ATRACT & 0x80) { CYC(7); return; }
    CYC(19 + 54 * 9 + 53 + 16);

    TBLPTR_AT(1) = (uint8_t)(VGLIST_AT(1) & 0xF8);
    TBLPTR = 0x02;

    /* The security checksum folds each underflow into the next byte. */
    for (int offset = 0x35; offset >= 0; offset--)
        checksum = (uint8_t)checksum - (checksum < 0) -
            cpu_rd((uint16_t)(VGLIST_ADDR & 0xF800) + 2 + (uint16_t)offset);
    TRAP1 = (uint8_t)(checksum - cpu_rd(A_OBJTBL + 0x21));

    if (!(LOGO & 0x80)) { CYC(6); return; }

    CYC(1);
    draw_logo_scene();
}

static void draw_logo_scene(void)
{
    uint8_t depth_page = LOGOZ_AT(1);
    CYC(6);
    if (depth_page < 128) {
        CYC(4);
        if (depth_page >= 2) { CYC(3); batint(); return; }
        CYC(1);
    } else CYC(1);
    CYC(2);
    uint8_t slot = 0;
    while (CYC(6), !(PTBLO2_AT(slot) & 0x80)) {
        CYC(7);
        slot = (uint8_t)(slot + 2);
    }
    CYC(9);
    SINT = 0xF0;
    uint8_t object = 0;
    for (;;) {
        CYC(63);
        PTBLO2_AT(slot) = cpu_rd((uint16_t)(A_LOGOBJ + object));
        PTBLX2_AT(slot) = LOGOX;
        PTBLY2_AT(slot) = 0;
        PTBLO2_AT((uint8_t)(slot + 1)) = TANGLE;
        PTBLX2_AT((uint8_t)(slot + 1)) = LOGOX_AT(1);
        PTBLY2_AT((uint8_t)(slot + 1)) = 0;
        slot = (uint8_t)(slot + 2);
        object = (uint8_t)(object + 2);

        /* The distant logo has two pieces; reveal the third as it approaches. */
        depth_page = LOGOZ_AT(1);
        if (depth_page >= 128) {
            CYC(4);
            if (depth_page < 0xFD) {
                CYC(8);
                if (LOGOZ < 0xB0) {
                    CYC(5 + (object < 3 ? 3 : 2));
                    if (object < 3) continue;
                    break;
                }
                CYC(1);
            } else CYC(1);
        } else CYC(1);
        CYC(2 + (object < 5 ? 3 : 2));
        if (object >= 5) break;
    }
    uint16_t depth = (uint16_t)(LOGOZ | ((unsigned)LOGOZ_AT(1) << 8));
    CYC(34);
    EXPOSZ_AT(0xC) = EXPOSZ_AT(0xE) = (uint8_t)depth;
    EXPOSZ_AT(0xD) = EXPOSZ_AT(0xF) = (uint8_t)(depth >> 8);
    depth = (uint16_t)(depth + 8);
    LOGOZ = (uint8_t)depth;
    CYC(LOGOZ < 8 ? 2 : 1);
    CYC(22);
    LOGOZ_AT(1) = (uint8_t)(depth >> 8);
    uint16_t distance = (uint16_t)((LOGOX | ((unsigned)LOGOX_AT(1) << 8)) + 64);
    LOGOX = (uint8_t)distance;
    CYC(LOGOX < 64 ? 2 : 1);
    CYC(17);
    LOGOX_AT(1) = (uint8_t)(distance >> 8);
    PTBLO2_AT(slot) = 0xFF;
}

/*==============================================================================
 * BATINT ($531F) - C INIT FOR LOGO (comment at the call).
 *============================================================================*/
void batint(void)
{
    CYC(46);
    LOGOZ_AT(1) = 0xFC;  /* C INIT POSITION */
    LOGOX = 0x00;
    LOGOZ = 0x00;
    LOGO = 0x00;
    LOGOX_AT(1) = 0x04;
    TIMOUT = 0x03;  /* C TEMPORARY */
    DSPLAY = 0x00;
    FRAME = 0x00;
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
    uint8_t entry;

    CYC(9);
    TIMOUT = 0x00;  /* C RESET TIMER */
    DSPLAY = 0x00;

    for (entry = 0x00; ; ) {
        uint8_t lo = HSCTBL_AT(entry);  /* D IF GOOD ENOUGH TO MAKE TABLE */
        CYC(16);
        uint8_t hi = HSCTBL_AT(entry + 1);
        if ((lo | ((unsigned)hi << 8)) <
            (HITS | ((unsigned)HITS_AT(1) << 8))) { CYC(1); break; }
        entry += 3;
        CYC(10);
        if (entry < K_HSCNUM * 3) { CYC(1); continue; }
        CYC(3);
        /* Not good enough: the ROM continues at B_END here, not MAIN. */
        return PASS_SIG_BEND;  /* NOT GOOD ENOUGH */
    }

    /* CKSCOR_20 */
    {
        uint8_t savx = entry;
        int i;

        CYC(22 + 8);
        FRAME = 0x00;  /* C RESET TIME OUT */
        LETR = 0x00;
        SAVX = savx;
        OBJCOL = 0x00;  /* C KILL ANY CONTINUOUS SOUNDS */
        SCOLFG = 0x00;
        SAUCER = 0x00;
        sndon(K_SUPBON);  /* C PLAY THE TUNE */
        GOVER = (uint8_t)(GOVER | 0x80);  /* C SET HI SCORE MODE */

        CYC(10);
        for (i = (K_HSCNUM - 1) * 3; ; ) {
            CYC(6);
            if ((uint8_t)i == savx) { CYC(1); break; }
            CYC(45);
            HSCTBL_AT(i)     = HSCTBL_AT(i - 3);
            HSCTBL_AT(i + 1) = HSCTBL_AT(i - 2);
            INITLS_AT(i)     = INITLS_AT(i - 3);
            INITLS_AT(i + 1) = INITLS_AT(i - 2);
            INITLS_AT(i + 2) = INITLS_AT(i - 1);
            i -= 3;
            CYC(i >= 0 ? 9 : 8);
            if (i < 0) break;
        }

        /* CKSCOR_30 - the new score's slot */
        CYC(41);
        HSCTBL_AT(savx)     = HITS;
        HSCTBL_AT(savx + 1) = HITS_AT(1);
        LTIMER = 0x04;
        STINTL = 0x16;  /* C SET UP INITIALS */
        STINTL_AT(1) = 0x4C;
        STINTL_AT(2) = 0x4C;
        return PASS_SIG_BEND;
    }
}

/*==============================================================================
 * HISCRE ($53C5) - high-score initials entry state; see bzone_disp.h.
 *============================================================================*/
/* Integration fix (2026-09-18, NOTES_integration.md "CRITICAL fix... open
 * item 1 resolved") - same trampoline as wnshld()/ckscor(). */
void hiscre(void) { (void)hiscre_body(); }

static pass_signal accept_initials(void)
{
    uint8_t destination = SAVX;
    CYC(6);
    for (unsigned initial = 0; initial < 3; ++initial) {
        INITLS_AT(destination) = STINTL_AT(initial);
        CYC(13);
        if (initial < 2) { CYC(8); ++destination; }
    }
    CYC(20);
    TIMOUT = GOVER = 3;
    DSPLAY = FRAME = 0x81;
    return PASS_SIG_YIELD;
}

pass_signal hiscre_body(void)
{
    CYC(7);
    if (TIMOUT >= 4) { CYC(1); return accept_initials(); }
    CYC(5);
    if (POINT == 0) {
        CYC(5);
        if (HITS_AT(2) == 0) CYC(1);
        else {
            CYC(13);
            HITS_AT(2) = 0;
            SNDWRD = 2;
            EXPCNT = 0xFF;
        }
    } else CYC(1);
    CYC(6); info();
    CYC(8); msgs(K_LINE1);
    CYC(8); msgs(K_LINE2);
    CYC(8); msgs(K_LINE3);
    CYC(8); msgs(K_LINE4);
    CYC(6); letchk();
    CYC(28);
    NWSTAT = hw_pokey_read(0x08);
    int confirm = ((NWSTAT ^ OLSTAT) & NWSTAT & K_PL1FIR) != 0;
    OLSTAT = NWSTAT;
    if (!confirm) CYC(1);
    else {
        CYC(8);
        uint8_t initial = LETR;
        if (initial >= 2) { CYC(1); return accept_initials(); }
        CYC(13);
        LETR = (uint8_t)(initial + 1);
        STINTL_AT(initial + 1) = 0x16;
    }

    CYC(6); vgcntr();
    CYC(17);
    VGSIZE = 0;
    vgvtr(0xEE, 0xEE, 0);
    for (uint8_t initial = 0; initial < 3; ++initial) {
        CYC(13);
        TEMP1 = initial;
        dspalp(STINTL_AT(initial));
        CYC(initial < 2 ? 10 : 9);
    }
    CYC(6); finish();
    CYC(3);
    return PASS_SIG_YIELD;
}

/*==============================================================================
 * LETCHK ($5465) - C LETTER CHANGE (comment at the call): advance/retreat
 * the high-score initial letter being entered, once every LTIMER ticks.
 * Caller: HISCRE.
 *============================================================================*/
void letchk(void)
{
    CYC(12);  /* sample the initial-selection controls. */
    hw_pokey_write(0x0B, 0);
    uint8_t controls = hw_pokey_read(0x08) & 3;
    if (controls == 0) {
        CYC(12);
        LTIMER = 4;
        return;
    }
    CYC(1);
    --LTIMER;
    CYC(8);
    if (LTIMER != 0) { CYC(7); return; }
    CYC(12);
    uint8_t initial = LETR;
    uint8_t letter = STINTL_AT(initial);
    if (controls & 1) {
        letter = (uint8_t)(letter - 2);
        CYC(8 + (letter < 0x16 ? 5 : 1));
        if (letter < 0x16) letter = 0x4A;
    } else {
        letter = (uint8_t)(letter + 2);
        CYC(9 + (letter >= 0x4C ? 2 : 1));
        if (letter >= 0x4C) letter = 0x16;
    }
    CYC(9);
    STINTL_AT(initial) = letter;
    if (letter != 0) {
        CYC(1);
        CYC(12);
        LTIMER = 4;
    } else {
        /* Preserve the historical zero-code continuation for malformed initials. */
        distbl();
    }
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
    uint8_t row, ydisp;

    CYC(6); info();  /* C OUTPUT CURRENT SCORE */
    CYC(8); msgs(K_HISCOR);
    CYC(6); vgcntr();
    CYC(14);
    SAVX = 0x00;
    ydisp = 0x1E;
    YDISP = ydisp;

    for (;;) {
        uint8_t lo, hi;

        /* DISTBL_10 */
        CYC(6);
        vgvtr(0xE0, ydisp, 0x00);
        CYC(17);
        row = SAVX;
        lo = HSCTBL_AT(row);
        TEMP2 = lo;
        hi = HSCTBL_AT(row + 1);
        if ((uint8_t)(lo | hi) == 0) { CYC(1); break; }
        CYC(15);
        TEMP2_AT(1) = hi;
        digt2s((uint8_t)A_TEMP2);
        CYC(8);
        msgs(K_ZEROS);

        for (unsigned initial = 0; initial < 3; ++initial) {
            CYC(14);
            dspalp(INITLS_AT(SAVX + initial));
        }

        {
            int offset = 0;
            CYC(12);
            row = SAVX;
            hi = HSCTBL_AT(row + 1);  /* D */
            if (hi != 0) {
                uint16_t vg = VGLIST_ADDR;
                uint8_t nsym = hi;

                CYC(28 + (nsym >= 0x0A ? 2 : 1));
                cpu_wr(vg + (uint16_t)offset, cpu_rd(A_VGMSGA));  /* C BLANK BEFORE TANK */
                offset++;
                cpu_wr(vg + (uint16_t)offset, cpu_rd(A_VGMSGA + 1));
                if (nsym >= 0x0A) nsym = 0x0A;

                do {
                    /* DISTBL_23 */
                    offset++;
                    cpu_wr(vg + (uint16_t)offset, cpu_rd(A_TSYMBL));
                    offset++;
                    cpu_wr(vg + (uint16_t)offset, cpu_rd(A_TSYMBL + 1));
                    nsym--;
                    CYC(nsym != 0 ? 29 : 28);
                } while (nsym != 0);
                CYC(6);
                vgadd((uint8_t)offset);
            } else CYC(1);
        }

        /* DISTBL_25 - D IF DONE */
        CYC(8);
        row = SAVX;
        if (row >= (K_HSCNUM - 1) * 3) { CYC(1); break; }
        CYC(10 + 6);
        row = (uint8_t)(row + 3);
        SAVX = row;
        vgcntr();  /* C MOVE BEAM TO NEXT LINE */
        {
            ydisp = (uint8_t)(YDISP - 10);  /* next score-table line */
            YDISP = ydisp;
        }
        CYC(21);
    }

    /* DISTBL_40 */
    CYC(16);
    uint8_t bonus_mode = (uint8_t)((hw_option() >> 4) & 0x03);  /* C SET TEMP2 W/BONUS LEVEL */
    if (bonus_mode == 0) CYC(1);
    if (bonus_mode != 0) {
        CYC(17 + 8);
        uint8_t bt = cpu_rd((uint16_t)(A_BONTBL + bonus_mode));
        unsigned bonus = (bt >> 4) * 10 + (bt & 15) + 1;
        bt = (uint8_t)(((bonus / 10) << 4) | (bonus % 10));
        SAVE1 = bt;
        msgs(K_BONPLN);  /* C OUTPUT BONUS MESSAGE */
        CYC(10);
        digtys((uint8_t)A_SAVE1, 0x01);  /* C OUTPUT BONUS LEVEL */
        CYC(8);
        msgs(K_BONPL1);  /* C OUTPUT REMAINING MESSAGE */
    }

    /* DISTBL_45 */
    CYC(6);
    finish();
    CYC(3);
    return PASS_SIG_YIELD;
}

/*==============================================================================
 * DSPALP ($556A) - draw one alphanumeric vector character: look up its
 * JSRL pair in VGMSGA and append it with VGADD2.  Callers: HISCRE, DISTBL.
 *============================================================================*/
void dspalp(uint8_t glyph)
{
    CYC(13);
    uint8_t high = cpu_rd((uint16_t)(A_VGMSGA + 1 + glyph));
    uint8_t low = cpu_rd((uint16_t)(A_VGMSGA + glyph));
    vgadd2(low, high);
}

/*==============================================================================
 * FINISH ($5574) - end of a display build: stop the AVG, mark the double
 * buffer ready, and (attract mode, 4+ buzz bombers destroyed) copy TRAP1
 * into vector RAM as the anti-tamper check.  Callers: HISCRE, DISTBL, MAIN.
 *============================================================================*/
void finish(void)
{
    CYC(6);
    vghalt();  /* C STOP VG */
    CYC(10);
    BUFRDY = 0x00;  /* C INDICATE BUFFER READY */
    if (ATRACT & 0x80) {  /* C SECURITY */
        CYC(6);
        if (NOR2D3 & 0x80) CYC(1);
        else {
            CYC(4);
            if (NOR2D3 < 0x04) CYC(1);
        }
        if (!(NOR2D3 & 0x80) && NOR2D3 >= 0x04) {
            CYC(8);
            VRAM(A_VECRAM) = TRAP1;  /* VECRAM ($2000) is vector RAM, not flat RAM */
        }
    } else CYC(1);
    CYC(6);

}

/*==============================================================================
 * MOUNTS_50 - not a real ROM label (file-static helper for MOUNTS' own
 * horizon-tile drawing loop, $58A7-$58B9); shares MTNS-table-index bookkeeping
 * across its 3 chained calls in mounts() (see NOTES_disp.md).
 *============================================================================*/
static uint8_t draw_mountain_strip(uint8_t strip)
{
    unsigned index = strip & 0x0E;
    CYC(25);
    uint8_t first = cpu_rd((uint16_t)(A_MTNS + index));
    uint8_t second = cpu_rd((uint16_t)(A_MTNS + index + 1));
    TEMP1 = (uint8_t)(index + 2);
    vgadd2(first, second);
    CYC(9);
    return TEMP1;
}

/*==============================================================================
 * MOUNTS ($57C8) - draw the horizon-mountain skyline (view-angle-scrolled,
 * bounce-shaken) and, when the volcano is on screen, its rock particles.
 * Caller: MAIN family.
 *============================================================================*/
void mounts(void)
{
    uint8_t skyline_sector;

    CYC(68);
    XCOMP_AT(3) = 0x00;
    XCOMP_AT(2) = 0x00;
    XCOMP = LANGLE;

    /* Nine-bit view angle, scaled into the horizon's coordinate space. */
    unsigned view = ((unsigned)TANGLE << 8) | LANGLE;
    XCOMP = (uint8_t)(view >> 4);
    skyline_sector = TANGLE >> 4;
    XCOMP_AT(1) = (uint8_t)((skyline_sector & 1) | 2);  /* 512 TO 1023 RANGE */

    uint8_t shake = BOUNCE >> 4;  /* C PROVIDE BOUNCE */
    if (shake == 0) CYC(1);
    if (shake != 0) {
        CYC(19);
        TEMP1 = shake;
        XCOMP_AT(2) = (uint8_t)(XCOMP_AT(2) - TEMP1);
        XCOMP_AT(3) = 0xFF;
    }

    /* MOUNTS_20 */
    CYC(6);
    vgcntr();  /* CENTER BEAM */
    CYC(6);
    vgvtr2();  /* POSITION BEAM */
    CYC(10);
    vgjsrl((uint8_t)(A_HORIZN >> 8), (uint8_t)(A_HORIZN & 0xFF));  /* ADD HORIZON LINE */

    uint8_t strip = (uint8_t)(skyline_sector ^ 0x0F);
    CYC(12);
    strip = draw_mountain_strip(strip);  /* DRAW 3/2 FULL SCREEN */
    CYC(6);
    strip = draw_mountain_strip(strip);  /* TO INSURE FULL VIEW */
    CYC(6);
    strip = draw_mountain_strip(strip);

    CYC(13);
    XCOMP = LANGLE;  /* SET UP FOR SIFT */
    uint8_t heading = TANGLE;  /* D IF VOLCANO NOT ON SCREEN */
    if (heading >= 0x88) { CYC(7); return; }
    CYC(4);
    if (heading < 0x39) { CYC(6); return; }
    CYC(1 + 31 + 30);

    /* Offset the volcano from the scaled view angle. */
    {
        uint16_t volcano_x = (uint16_t)((view >> 4) - 0x0605);
        unsigned volcano_y = XCOMP_AT(2) + 0x5Eu;
        XCOMP = (uint8_t)volcano_x;
        XCOMP_AT(1) = (uint8_t)(volcano_x >> 8);
        XCOMP_AT(2) = (uint8_t)volcano_y;
        if (volcano_y > 255) { CYC(5); XCOMP_AT(3)++; } else CYC(1);
    }

    /* MOUNTS_25 - C MOVE BEAM TO TOP OF VOLCANO */
    CYC(6);
    vgcntr();
    CYC(6);
    vgvtr2();
    CYC(14 + 2);
    CURNTX = 0x00; CURNTX_AT(1) = 0x00;
    CURNTY = 0x00; CURNTY_AT(1) = 0x00;

    {
        int rock;
        for (rock = K_NOROCK - 1; rock >= 0; rock--, CYC(rock >= 0 ? 5 : 4)) {
            uint16_t point, delta;

            CYC(6);
            if (OBJTIM_AT(rock) == 0) { CYC(1); continue; }  /* D IF NOT ACTIVE */
            CYC(64 + 14);

            point = (uint16_t)(XPOSL_AT(rock) | ((unsigned)XPOSH_AT(rock) << 8));
            delta = (uint16_t)(point - (CURNTX | ((unsigned)CURNTX_AT(1) << 8)));
            XCOMP = (uint8_t)delta;
            CURNTX = (uint8_t)point;
            XCOMP_AT(1) = (uint8_t)(delta >> 8);
            CURNTX_AT(1) = (uint8_t)(point >> 8);
            point = (uint16_t)(YPOSL_AT(rock) | ((unsigned)YPOSH_AT(rock) << 8));
            delta = (uint16_t)(point - (CURNTY | ((unsigned)CURNTY_AT(1) << 8)));
            XCOMP_AT(2) = (uint8_t)delta;
            CURNTY = (uint8_t)point;
            XCOMP_AT(3) = (uint8_t)(delta >> 8);
            CURNTY_AT(1) = (uint8_t)(point >> 8);

            VGBRIT = 0x00;  /* C VECTOR TO POINT */
            TEMP1 = (uint8_t)rock;
            vgvtr2();

            CYC(21);
            uint8_t brightness = (uint8_t)((OBJTIM_AT(rock) << 3) & 0xE0);
            vgdot(brightness);
            CYC(3);

        }
    }
    CYC(6);
}

/*==============================================================================
 * VOLCNO ($58BA) - move up to NOROCK (5) volcano-eruption rock particles,
 * one call per frame; occasionally spawns a new rock when one goes idle.
 * Caller: MAIN family.
 *============================================================================*/
static void reset_rock_position(unsigned rock)
{
    CYC(25);
    XPOSL_AT(rock) = XPOSH_AT(rock) = 0;
    YPOSL_AT(rock) = YPOSH_AT(rock) = 0;
}

void volcno(void)
{
    /* A horizontal wrap biases the next new rock's speed by one. */
    unsigned launch_bias = 1;
    CYC(2);
    for (int rock = K_NOROCK - 1; rock >= 0;
         --rock, CYC(rock >= 0 ? 7 : 5)) {
        CYC(6);
        if (OBJTIM_AT(rock) == 0) {
            CYC(8);
            if ((hw_pokey_read(0x0A) & 7) != 0) { CYC(1); continue; }
            CYC(11);
            OBJTIM_AT(rock) = 31;
            uint8_t speed = (uint8_t)((hw_pokey_read(0x0A) & 3) + 1 + launch_bias);
            launch_bias = 0;
            CYC(8);
            uint8_t direction = hw_pokey_read(0x0A);
            CYC(2);
            if (direction & 0x40) { CYC(2); speed = (uint8_t)(-1 - speed); }
            else CYC(1);
            CYC(18);
            XSPD_AT(rock) = speed;
            YSPD_AT(rock) = (uint8_t)((hw_pokey_read(0x0A) & 7) + 5);
            reset_rock_position((unsigned)rock);
            continue;
        }

        CYC(1);
        --OBJTIM_AT(rock);
        CYC(9);
        if (OBJTIM_AT(rock) == 0) {
            CYC(1);
            reset_rock_position((unsigned)rock);
            continue;
        }
        --YSPD_AT(rock);
        CYC(17 + ((int8_t)YSPD_AT(rock) < 0 ? 2 : 1) + 15);
        uint16_t height = (uint16_t)(YPOSL_AT(rock) | ((unsigned)YPOSH_AT(rock) << 8));
        uint16_t next_height = (uint16_t)(height + (int8_t)YSPD_AT(rock));
        YPOSL_AT(rock) = (uint8_t)next_height;
        if (YPOSH_AT(rock) < 128) CYC(1);
        else {
            CYC(8);
            if (YPOSL_AT(rock) < 0xA2) {
                launch_bias = 0;
                CYC(10);
                OBJTIM_AT(rock) = 0;
                reset_rock_position((unsigned)rock);
                continue;
            }
            CYC(1);
            /* The original ground threshold rounds the high byte upward,
             * even when the low-byte addition did not cross a boundary. */
            next_height = (uint16_t)((((unsigned)YPOSH_AT(rock) +
                ((int8_t)YSPD_AT(rock) < 0 ? 0 : 1)) << 8) | YPOSL_AT(rock));
        }
        CYC(21 + ((int8_t)XSPD_AT(rock) < 0 ? 2 : 1) + 20);
        YPOSH_AT(rock) = (uint8_t)(next_height >> 8);
        uint16_t horizontal = (uint16_t)(XPOSL_AT(rock) | ((unsigned)XPOSH_AT(rock) << 8));
        unsigned displacement = (uint16_t)(int16_t)(int8_t)XSPD_AT(rock);
        unsigned next_horizontal = horizontal + displacement;
        XPOSL_AT(rock) = (uint8_t)next_horizontal;
        XPOSH_AT(rock) = (uint8_t)(next_horizontal >> 8);
        launch_bias = next_horizontal > 65535;
    }
    CYC(6);
}

/*==============================================================================
 * DRAW ($5C5C) - build the AVG display list for one object from PNTTBL (its
 * already-projected 2-D points, from PNTPUT) and OBJTBL (its edge-list
 * opcode stream, ROM).  Each edge-list byte's low three bits select the
 * action (Atari's DTAB table); D_LABS falls into D_BVCT and D_SPCL into
 * D_SLIM.
 * Caller: MAIN family (once per visible object per frame, after PNTPUT).
 *============================================================================*/
void draw(uint8_t object)
{
    uint8_t table_offset = (uint8_t)(object << 1);  /* C BUILD INDEX TO OBJ TBL */

    CYC(18);
    TBLPTR       = cpu_rd((uint16_t)(A_OBJTBL + table_offset));  /* GET POINTER TO OBJ IN MEM */
    TBLPTR_AT(1) = cpu_rd((uint16_t)(A_OBJTBL + table_offset + 1));

    for (;;) {  /* D_TOP */
        uint16_t ptr = (uint16_t)(((uint16_t)TBLPTR_AT(1) << 8) | TBLPTR);
        uint8_t op = cpu_rd(ptr);  /* D IF NOT DONE (value from table) */

        CYC(11);
        if (op == 0xFF) { CYC(6); return; }

        /* D_TOP_11 - C DO COMPUTED GO TO */
        CYC(1 + 35);
        TEMP1 = (uint8_t)(op & 0xF8);  /* save index/value */

        switch ((uint8_t)(op & 0x07)) {
        case 0: {                        /* D_DOT */
            CYC(8);
            dovect(0x00);  /* C DRAW BLANK VECTOR TO POINT */
            CYC(8);
            vgdot(0x20);  /* C DRAW DOT AT CURRENT INTENSITY */
            CYC(3);
            break;
        }
        case 1: {  /* D_SBRT */
            uint8_t saveo  = SAVE0;  /* D IF NOT SAUCER */
            uint8_t obtype = PTBLO2_AT(saveo);
            uint8_t bright;

            if (obtype == 0x17 || obtype == 0x1E || obtype == 0x1F ||  /* D IF LOGO */
                (obtype == 0x20 && SCOLFG != 0)) {  /* D IF NOT SAUCER / NOT IN COLLISION */
                CYC(obtype == 0x17 ? 12 : obtype == 0x1E ? 16 : obtype == 0x1F ? 20 : 28);
                CYC(7);
                bright = SINT;
            } else {
                int brightness = (int)TEMP1 - DQUE;
                CYC(obtype == 0x20 ? 29 : 24);
                CYC(brightness < 0 ? 13 : brightness < 0x30 ? 16 : 15);
                bright = (uint8_t)(brightness < 0x30 ? 0x30 : brightness);
            }
            CYC(10);
            vgstat(0x04, bright);  /* C SET BRITNESS REGISTER, no clipping window */
            CYC(3);
            break;
        }
        case 3:  /* D_LABS - falls into D_BVCT (case 2), below */
            CYC(14 + 6);
            CURNTX = 0x00; CURNTX_AT(1) = 0x00; CURNTY = 0x00; CURNTY_AT(1) = 0x00;  /* C RESET CURRENT X&Y */
            vgcntr();  /* C MOVE BEAM */
            /* FALLTHROUGH */
        case 2:  /* D_BVCT */
            CYC(5 + 6);
            dovect(0x00);  /* C DRAW BLANK VECTOR */
            CYC(3);
            break;
        case 4:  /* D_VCTR */
            CYC(2 + 6);
            dovect(0x20);  /* C DRAW AT CURRENT BRITHNESS */
            CYC(3);
            break;
        case 5: {  /* D_SPCL - falls into D_SLIM (case 6), below */
            uint8_t saveo = SAVE0;  /* C SPECIAL COUNTER */
            uint8_t sb    = PTBLO2_AT(saveo + 1);
            uint8_t lin   = (uint8_t)((sb & 0x3F) << 1);  /* LINEAR (Y) */
            uint8_t pw    = (uint8_t)(((sb >> 6) & 0x03) + 1);  /* POWER OF 2; see NOTES_disp.md for the bit-by-bit derivation of this formula */

            CYC(27 + 6);
            vgscal(pw, lin);  /* C SET SCALE FACTOR */
            CYC(10);
            vgjsrl((uint8_t)(A_EXPIC >> 8), (uint8_t)(A_EXPIC & 0xFF));  /* C OUTPUT EXPLOSION PICTURE */
            CYC(8);
            vgsca1(0x01);  /* C RESET SCALE */
            /* FALLTHROUGH */
        }
        case 6:  /* D_SLIM */
            CYC(3);
            break;
        default:
            /* opcode 7: DTAB only has 7 entries (0-6); no real OBJTBL
             * edge-list byte in this ROM ever encodes opcode 7. Defensive
             * no-op (not object real ROM path) so this loop can't spin forever
             * if it ever were - see NOTES_disp.md. */
            break;
        }

        d_next();  /* advance, then loop back to fetch the next opcode */
    }
}

/*==============================================================================
 * DOVECT ($5D11) - draw one vector of the current object at brightness A:
 * compute the delta from CURNTX/CURNTY to the next PNTTBL point (indexed by
 * TEMP1, set by DRAW's own dispatcher before the opcode handler ran) and
 * append it via VGVTR2.  Callers: D_DOT, D_BVCT/D_VCTR (via draw()'s switch).
 *============================================================================*/
void dovect(uint8_t brightness)
{
    CYC(80);
    VGBRIT = brightness;
    unsigned point_index = TEMP1 >> 1;
    uint16_t point = (uint16_t)(PNTTBL_AT(point_index) | ((unsigned)PNTTBL_AT(point_index + 1) << 8));
    uint16_t delta = (uint16_t)(point - (CURNTX | ((unsigned)CURNTX_AT(1) << 8)));
    XCOMP = (uint8_t)delta;
    CURNTX = (uint8_t)point;
    XCOMP_AT(1) = (uint8_t)(delta >> 8);
    CURNTX_AT(1) = (uint8_t)(point >> 8);
    point = (uint16_t)(PNTTBL_AT(point_index + 2) | ((unsigned)PNTTBL_AT(point_index + 3) << 8));
    delta = (uint16_t)(point - (CURNTY | ((unsigned)CURNTY_AT(1) << 8)));
    XCOMP_AT(2) = (uint8_t)delta;
    CURNTY = (uint8_t)point;
    XCOMP_AT(3) = (uint8_t)(delta >> 8);
    CURNTY_AT(1) = (uint8_t)(point >> 8);

    vgvtr2();  /* C ADD VCTR TO VGLIST */
    CYC(6);
}

/*==============================================================================
 * D_NEXT ($5D41) - advance DRAW to the next edge-list byte (16-bit TBLPTR
 * increment).  Exposed as its own function for direct lockstep coverage,
 * though in the ROM it is part of DRAW's loop, not a subroutine.
 *============================================================================*/
void d_next(void)
{
    uint16_t next_command = (uint16_t)((TBLPTR | ((unsigned)TBLPTR_AT(1) << 8)) + 1);
    TBLPTR = (uint8_t)next_command;
    CYC(TBLPTR == 0 ? 15 : 11);  /* page crossing costs four cycles. */
    TBLPTR_AT(1) = (uint8_t)(next_command >> 8);
}

/*==============================================================================
 * DRADAR ($6AE9) - display the radar sweep and, if an enemy is within it,
 * its blip and warning sound/message.  Caller: MAIN family.
 *============================================================================*/
static void fade_radar_blip(void)
{
    CYC(6);
    if (BLIP != 0) { CYC(8); BLIP = (uint8_t)(BLIP - 8); }
    else CYC(1);
    CYC(6);
}

/* Half of a trig sample's whole part, rounded down: the sweep line's end. */
static int8_t sweep_half(trig_sample sample)
{
    int whole = trig_high(sample);
    return (int8_t)(whole >= 0 ? whole / 2 : -((1 - whole) / 2));
}

void dradar(void)
{
    trig_sample direction;

    CYC(10);
    vgjsrl((uint8_t)(A_RDRING >> 8), (uint8_t)(A_RDRING & 0xFF));
    CYC(27);

    XCOMP_AT(1) = 0x00;
    YCOMP_AT(1) = 0x00;
    CURNTX_AT(1) = 0x00;

    SANGLE = (uint8_t)(SANGLE + 11);  /* radar sweep advance */

    {
        int8_t half = sweep_half(sin_(SANGLE));  /* C SIN/2 */
        CYC(14 + (half < 0 ? 8 : 1) + 9);
        XCOMP = (uint8_t)half;
        CURNTX = (uint8_t)half;
        if (half < 0) {
            XCOMP_AT(1) = 0xFF;
            CURNTX_AT(1) = 0xFF;
        }
    }

    /* DRADAR_10 - C COS/2 */
    {
        int8_t half = sweep_half(cos_(SANGLE));
        CYC(11 + (half < 0 ? 5 : 1) + 29);
        XCOMP_AT(2) = (uint8_t)half;
        /* Only a negative half sets the high byte: a non-negative one leaves
         * XCOMP+3 as the previous call left it, as in the original game
         * (NOTES_disp.md "DRADAR asymmetric sign-extend"). */
        if (half < 0) XCOMP_AT(3) = 0xFF;
    }

    /* DRADAR_20 - C DRAW BRIGHT VECTOR */
    VGBRIT = 0xA0;
    {
        uint16_t endpoint = (uint16_t)(K_CENTRY +
            (XCOMP_AT(2) | ((unsigned)XCOMP_AT(3) << 8)));
        CURNTY = (uint8_t)endpoint;
        CURNTY_AT(1) = (uint8_t)(endpoint >> 8);
    }
    vgvtr2();

    CYC(5);
    if (COLFLG_AT(2) != 0) {  /* D IF NOT EXPLODING */
        CYC(16);
        hw_pokey_write(0x05, 0x00);
        hw_pokey_write(0x07, 0x00);
        return;
    }

    /* DRADAR_21 - C TRACK THE ROBOT */
    CYC(1 + 6);
    uint8_t bearing = track();
    CYC(26);
    GARBAG = (uint8_t)(TANGLE - (bearing ^ 0x80));
    {
        uint8_t diff = (uint8_t)(SANGLE - GARBAG);
        if ((int8_t)diff < 0) CYC(1);
        else CYC(diff < 0x0C ? 4 : 5);
        if ((int8_t)diff >= 0 && diff < 0x0C) {
            CYC(6);
            BLIP = 0xF0;  /* C SET BRIGHTNESS OF BLIP */
        }
    }

    /* DRADAR_23 - C COMPUTE DISTANCE */
    CYC(32 + 25);
    hw_mb_write(0x04, TEMP1);
    hw_mb_write(0x05, TEMP1_AT(1));
    hw_mb_write(0x06, TEMP2);
    hw_mb_write(0x07, TEMP2_AT(1));
    hw_mb_write(0x1E, TEMP2_AT(1));
    /* The ROM wastes a few cycles here for the Mathbox ("WASTE TIME"). */
    TEMP3 = hw_mb_ylow();
    {
        uint8_t distance = hw_mb_yhigh();
        uint8_t volume = 0;
        TDIST = distance;
        if (distance >= 128) CYC(3);
        else {
            volume = (uint8_t)(((distance >> 3) & 15) ^ 0xAF);
            CYC((R2D3FL & 0x80) ? 16 : 17);
            if (!(R2D3FL & 0x80)) volume = 0;
        }
        CYC(18);
        hw_pokey_write(0x05, volume);
        hw_pokey_write(0x07, volume);
        hw_mb_write(0x08, distance);

        if (distance >= 128) {
            CYC(10);
            EIRNGE = 0x00;  /* C OUT OF RANGE */
            if (R2D3FL & 0x80) {
                CYC(7);
                if (TIMOUT >= 0x04) {
                    CYC(6);
                    tankck();
                    CYC(3);
                    fade_radar_blip(); return;
                }
                CYC(1 + 6);
                r2d3ck();
            } else CYC(1);
            CYC(3);
            fade_radar_blip(); return;
        }
        CYC(1);
    }

    /* DRADAR_35 */
    CYC(6);
    if (BLIP == 0) { CYC(6); return; }
    CYC(1 + 4);

    if (BLIP >= 0xF0) {  /* a fresh blip beeps */
        CYC(8);
        sndon(K_RBEEP);
    } else CYC(1);

    /* DRADAR_40 - C SETUP SIN/COS FOR MATRIX MULT */
    CYC(9);
    direction = sin_(GARBAG);
    CYC(17);
    hw_mb_write(0x00, trig_low(direction));
    hw_mb_write(0x01, (uint8_t)trig_high(direction));
    direction = cos_(GARBAG);
    CYC(55);
    hw_mb_write(0x02, trig_low(direction));
    hw_mb_write(0x03, (uint8_t)trig_high(direction));

    hw_mb_write(0x04, (uint8_t)(K_CENTRX & 0xFF));
    hw_mb_write(0x05, (uint8_t)(K_CENTRX >> 8));
    hw_mb_write(0x06, (uint8_t)(K_CENTRY & 0xFF));
    hw_mb_write(0x07, (uint8_t)(K_CENTRY >> 8));
    hw_mb_write(0x09, 0x00);
    VGBRIT = 0x00;  /* C SET ZERO BRITE */
    hw_mb_write(0x0A, 0x00);  /* C START MATRIX MULT */
    hw_mb_write(0x11, 0x00);

    {
        uint8_t lo = wait();  /* C SCALE RESLT */
        uint8_t hi;
        uint16_t point, delta;

        CYC(58);
        hw_mb_write(0x17, lo);
        lo = hw_mb_ylow();
        hi = hw_mb_yhigh();
        hw_mb_write(0x19, hi);
        point = (uint16_t)(lo | ((unsigned)hi << 8));
        delta = (uint16_t)(point - (CURNTX | ((unsigned)CURNTX_AT(1) << 8)));
        XCOMP = (uint8_t)delta;
        XCOMP_AT(1) = (uint8_t)(delta >> 8);
        lo = hw_mb_ylow();
        hi = hw_mb_yhigh();
        point = (uint16_t)(lo | ((unsigned)hi << 8));
        delta = (uint16_t)(point - (CURNTY | ((unsigned)CURNTY_AT(1) << 8)));
        YCOMP = (uint8_t)delta;
        YCOMP_AT(1) = (uint8_t)(delta >> 8);
    }
    vgvtr2();

    uint8_t brightness = (uint8_t)(BLIP & 0xE0);  /* C DRAW BLIP */
    CYC(12);
    vgdot(brightness);
    CYC(9);
    vgdot(VGBRIT);

    CYC(5);
    if (ATRACT & 0x80) CYC(1);
    else CYC((LOGO & 0x80) ? 6 + 1 : 6);
    if (!(((ATRACT & 0x80) == 0) && ((LOGO & 0x80) != 0))) {  /* run D_5 unless (!ATRACT.7 && LOGO.7) */
        /* D_5 */
        CYC(7);
        if (FRAME & 0x02) {
            CYC(8);
            msgs(K_ERANGE);  /* C INDICATE IN RANGE */
        } else CYC(1);
    }

    /* D_10 */
    CYC(5);
    if (EIRNGE == 0) {  /* D IF ALREADY IN RANGE */
        CYC(13);
        EIRNGE++;  /* C INDICATE IN RANGE */
        sndon(K_WARNG);  /* C OUTPUT WARNING SOUND */
    } else CYC(1);

    fade_radar_blip();
}

/*==============================================================================
 * SHUPDT ($6C66) - update shell positions for the *second* shell then the
 * first ("C START WITH SECOND SHELL" - processing order 2, then 0, not a
 * skip; see NOTES_disp.md).  Caller: MAIN family.
 *============================================================================*/
void shupdt(void)
{
    CYC(2);
    for (int shooter = 2; shooter >= 0; shooter -= 2) {
        uint8_t remaining_range = FIRECT_AT(shooter);  /* D IF SHELL NOT IN AIR */

        CYC(remaining_range == 0 ? 7 : (remaining_range & 0x80) ? 9 : 66);
        if (remaining_range != 0 && !(remaining_range & 0x80)) {
            FIRECT_AT(shooter) = (uint8_t)(remaining_range - 1);  /* remaining shell range */
            uint16_t position = (uint16_t)((SHELLX_AT(shooter) | ((unsigned)SHELLX_AT(shooter + 1) << 8)) +
                (SINCX_AT(shooter) | ((unsigned)SINCX_AT(shooter + 1) << 8)));
            SHELLX_AT(shooter) = (uint8_t)position;
            SHELLX_AT(shooter + 1) = (uint8_t)(position >> 8);
            position = (uint16_t)((SHELLY_AT(shooter) | ((unsigned)SHELLY_AT(shooter + 1) << 8)) +
                (SINCY_AT(shooter) | ((unsigned)SINCY_AT(shooter + 1) << 8)));
            SHELLY_AT(shooter) = (uint8_t)position;
            SHELLY_AT(shooter + 1) = (uint8_t)(position >> 8);
        }

        CYC(shooter == 2 ? 7 : 12);  /* next shooter or return. */
    }
}

/*==============================================================================
 * MSTART ($6CD4) - draw the packed-character message TBLPTR/TBLPTR+1 (hi,
 * passed in) points at; the message index decides its scale (GAMOVR and
 * later are drawn larger).  Shared by msgs()'s four languages.
 *============================================================================*/
static void mstart(uint8_t message, uint8_t hi)
{
    uint8_t offset;
    uint16_t ptr;

    TBLPTR_AT(1) = hi;
    CYC(13 + (message >= K_GAMOVR ? 2 : 1) + 17);
    TEMP3_AT(1) = (message >= K_GAMOVR) ? 0x01 : 0x02;  /* C SET SCALING */

    ptr = (uint16_t)(((uint16_t)TBLPTR_AT(1) << 8) | TBLPTR);
    {
        uint8_t b0 = cpu_rd(ptr);
        uint8_t b1 = cpu_rd((uint16_t)(ptr + 1));  /* D IF NO POSITIONING NEEDED */
        if ((uint8_t)(b0 | b1) == 0) CYC(1);
        if ((uint8_t)(b0 | b1) != 0) {
            CYC(6);
            vgcntr();  /* C POSITION FOR MSG */
            CYC(22);
            {
                uint8_t xh = cpu_rd((uint16_t)(ptr + 1));
                uint8_t al = cpu_rd(ptr);
                vgvtr(al, xh, 0x00);
            }
        }
    }

    /* MSTART_10 */
    CYC(9);
    vgsca1(TEMP3_AT(1));
    CYC(7);
    offset = 0x02;  /* C INIT LOOP */
    TEMP1 = 0x00;  /* C INIT VGLIST OFFSET */

    for (;;) {  /* MSTART_20 */
        uint8_t ch   = cpu_rd((uint16_t)(ptr + offset));  /* C GET CHARACTER REPRESENTATION */
        /* The raw character byte (end-of-string bit included) stays in TEMP2. */
        TEMP2 = ch;
        uint8_t code = (uint8_t)(ch & 0x7F);
        uint16_t vg;

        offset++;
        TEMP3 = offset;  /* the ROM keeps the character pointer here */

        vg = VGLIST_ADDR;
        cpu_wr(vg + (uint16_t)TEMP1, cpu_rd((uint16_t)(A_VGMSGA + code)));  /* GET CORRECT JSRL */
        TEMP1++;
        cpu_wr(vg + (uint16_t)TEMP1, cpu_rd((uint16_t)(A_VGMSGA + 1 + code)));
        TEMP1++;

        CYC((ch & 0x80) ? 55 : 56);
        if (!(ch & 0x80)) continue;  /* D IF NOT END OF STRING */
        break;
    }

    CYC(11);
    vgadd((uint8_t)(TEMP1 - 1));  /* C UPDATE VGLIST */
    CYC(5);
    vgsca1(0x01);  /* C RESET SCALE */
}

/*==============================================================================
 * MSGS ($6C98) - output a message from the ENGLISH/GERMAN/FRENCH/SPANSH
 * table selected by OPTION's language switches.
 * Caller: many (score/message display sites throughout MAIN/HISCRE/etc).
 *============================================================================*/
void msgs(uint8_t message)
{
    /* OPTION's top two switches pick the language: 0/2/4/6 = English,
     * German, French, Spanish. */
    uint8_t language = (uint8_t)((hw_option() & 0xC0) >> 5);

    CYC(36);
    CYC(language == 0 ? 11 : 14);
    switch (language) {
    case 0:       /* ENGLISH */
        TBLPTR = cpu_rd((uint16_t)(A_ENGMSG + message));  /* C SET UP POINTER */
        mstart(message, cpu_rd((uint16_t)(A_ENGMSG + message + 1)));
        break;
    case 2:  /* GERMAN */
        TBLPTR = cpu_rd((uint16_t)(A_GRMMSG + message));
        mstart(message, cpu_rd((uint16_t)(A_GRMMSG + message + 1)));
        break;
    case 4:  /* FRENCH */
        TBLPTR = cpu_rd((uint16_t)(A_FRNMSG + message));
        mstart(message, cpu_rd((uint16_t)(A_FRNMSG + message + 1)));
        break;
    case 6:   /* SPANISH */
    default:
        TBLPTR = cpu_rd((uint16_t)(A_SPNMSG + message));
        mstart(message, cpu_rd((uint16_t)(A_SPNMSG + message + 1)));
        break;
    }
}

/*==============================================================================
 * INFO ($6D2E) - display the lives-remaining tank icons, current score and
 * high score.  Caller: MAIN family, HISCRE.
 *============================================================================*/
void info(void)
{
    CYC(6);
    vgcntr();
    CYC(15);
    VGSIZE = 0x00;
    vgvtr(0x20, 0x5A, 0x00);

    CYC(5);
    if (LIVES == 0) CYC(1);
    if (LIVES != 0) {  /* D IF DEAD */
        CYC(9);
        uint8_t count = LIVES;
        uint16_t vg = VGLIST_ADDR;
        uint8_t tlo = cpu_rd(A_TSYMBL);
        uint8_t thi = cpu_rd((uint16_t)(A_TSYMBL + 1));
        int offset = 0;

        for (;;) {  /* INFO_10 */
            cpu_wr(vg + (uint16_t)offset, tlo);  /* C OUTPUT 1 TANK PER LIFE */
            offset++;
            cpu_wr(vg + (uint16_t)offset, thi);
            offset++;
            count--;
            CYC(count != 0 ? 30 : 29);
            if (count != 0) continue;
            break;
        }
        offset--;
        CYC(8);
        vgadd((uint8_t)offset);
    }

    /* INFO_20 */
    CYC(8);
    msgs(K_YSCORE);
    CYC(12);
    vgvtr(0xD6, 0x00, 0x00);
    CYC(8);
    digt2s((uint8_t)A_HITS);
    CYC(8);
    msgs(K_CHISCR);
    CYC(8);
    vgsca1(0x02);
    CYC(12);
    vgvtr(0xD6, 0x00, 0x00);
    CYC(14 + 8);
    TEMP2       = HSCTBL;
    TEMP2_AT(1) = HSCTBL_AT(1);
    digt2s((uint8_t)A_TEMP2);
    CYC(5);
    vgsca1(0x01);  /* C RESET SCALE INFO */
}

/*==============================================================================
 * DIGT2S ($7B9E) - display a 2-digit-pair (4-hex-digit) number with leading-
 * zero suppression (DIGTYS with two pairs).  Callers: DISTBL, INFO.
 *============================================================================*/
void digt2s(uint8_t address)
{
    CYC(2);
    digtys(address, 0x02);
}

/*==============================================================================
 * DIGTYS ($7BA0) - display Y digit-pairs with leading-zero suppression;
 * DIGITS with zero suppression on.  Callers: DISTBL.
 *============================================================================*/
void digtys(uint8_t address, uint8_t pairs)
{
    CYC(2);
    digits(address, pairs, 1);
}

/* DIGITS ($7BA1): render packed decimal pairs most significant first.
 * Leading zeroes are blank until a nonzero digit appears; the units digit
 * is always visible. Inputs specify the byte address and pair count. */
void digits(uint8_t address, uint8_t pairs, int suppress_zero)
{
    CYC(19);
    TEMP1_AT(1) = (uint8_t)(pairs - 1);
    uint8_t current = (uint8_t)(address + TEMP1_AT(1));
    for (;;) {
        CYC(28);
        TEMP1 = current;
        suppress_zero = vghexz(cpu_rd(current) >> 4, suppress_zero);
        CYC(5 + (TEMP1_AT(1) == 0 ? 2 : 1));
        if (TEMP1_AT(1) == 0) suppress_zero = 0;  /* Always show units. */
        CYC(13);
        suppress_zero = vghexz(cpu_rd(TEMP1), suppress_zero);
        current = (uint8_t)(TEMP1 - 1);
        --TEMP1_AT(1);
        CYC((int8_t)TEMP1_AT(1) < 0 ? 18 : 13);
        if ((int8_t)TEMP1_AT(1) < 0) break;
    }
}
