/* NMI ($558F-$56C2): the 250 Hz interrupt.  Counts frames, runs the coin
 * mechanisms and sound envelopes, starts a game when START is pressed with
 * credit, and flips the vector-display buffers every sixth interrupt.
 * Each CYC charge is the ROM's cycle total for that path through the
 * handler, including its entry register saves and the closing RTI. */
#include "state.h"
#include "hw.h"
#include "coin65.h"
#include "bzsoun.h"
#include "bzone_game.h"
#include "bzone_nmi.h"

/* Start a paid or free-play game ($5643-$568B). */
static void start_game(void)
{
    CYC(21);
    NOR2D3 = 0xFF;
    ATRACT = 0xFF;
    S_S_CRDT = (uint8_t)(S_S_CRDT - TRAP1);  /* pay for the game */

    CYC(6);
    hw_pokey_write(0x0F, 0x00);  /* hold POKEY in reset while the game is set up */
    S_BCCNT = 0;
    LOGO = 0;
    CYC(11);
    hw_sound(0);                 /* turn off the lamps */

    /* Clear both scores and the play state.  The ROM clears the second
     * player's high score byte before its low byte. */
    HITS = 0;
    HITS_AT(1) = 0;
    HITS_AT(3) = 0;
    HITS_AT(2) = 0;
    ACTION = 0;
    FIRECT_AT(2) = 0;
    GOVER = 0;
    TIMOUT = 0;
    SCOLFG = 0;
    SAUCER = 0;
    TANGLE = 0;
    TPOSX_AT(1) = 0;
    TPOSY_AT(1) = 0;
    CYC(45);
    hw_pokey_write(0x0F, 0x07);  /* release POKEY */

    CYC(6);
    robchk();

    /* The two option switches give 2 to 5 lives. */
    CYC(16);
    LIVES = (uint8_t)((hw_option() & 3) + 2);
}

/* Attract mode with no game-over display: alarm, credit lamp and start
 * button polling.  The lamp/sound write precedes the start-button read. */
static void poll_start_button(void)
{
    uint8_t lamps = 0;

    if (S_LMTIM) {
        lamps = 0x20;
        if (!POINT) {
            CYC(23);
            sndon(K_DISINT);
            CYC(4);
        } else {
            CYC(13);
        }
    } else {
        CYC(6);
    }

    CYC(10);
    if (S_CMODE & 3) {
        if (!S_S_CRDT) {
            /* Not free play and no credit: only the lamp output changes. */
            CYC(16);
            hw_sound((uint8_t)(lamps | 0x40));
            return;
        }
        CYC(5);
    } else {
        CYC(1);
    }

    /* Blink the start lamp. */
    if (FRAME & 2) {
        lamps |= 0x40;
        CYC(15);
    } else {
        CYC(14);
    }

    CYC(4); hw_pokey_write(0x0B, lamps);
    CYC(4); hw_sound(lamps);
    CYC(6);
    if (!(hw_pokey_read(0x08) & K_START)) { CYC(3); return; }
    CYC(2);
    start_game();
}

/* Choose one new sound effect when channel 0 is free.  The priority is
 * disintegrating saucer, object collision, then a saucer on screen; the
 * super-bonus tune locks every other effect out. */
static void start_priority_sound(void)
{
    uint8_t effect = 0;

    if (SUPER & 0x80)        CYC(7);
    else if (POINT != 0)     CYC(12);
    else if (SCOLFG != 0)  { CYC(21); effect = K_DISINT; }
    else if (OBJCOL != 0)  { CYC(27); effect = K_BLKSND; }
    else if (SAUCER & 0x80) { CYC(30); effect = K_SAUSND; }
    else                     CYC(29);

    if (effect) {
        CYC(6);
        sndon(effect);
    }
}

/* The explosion and shell-explosion sounds are timed by their own
 * countdowns; return their SOUND latch bits. */
static uint8_t explosion_sound_bits(void)
{
    uint8_t bits = 0;

    if (EXPCNT == 0)           CYC(8);
    else if (--EXPCNT != 0)  { CYC(17); bits = 0x01; }
    else                       CYC(19);

    if (SXPCNT == 0)           CYC(6);
    else if (--SXPCNT != 0)  { CYC(18); bits |= 0x04; }
    else                       CYC(13);

    return bits;
}

/* NMI ($558F): see bzone_nmi.h. */
void nmi(void)
{
    uint8_t sound;  /* the SOUND ($1840) latch value being built up */

    CYC(24);
    ++S_INTCT;
    if ((S_INTCT & 0x0F) == 0) {
        CYC(7);
        ++SYNC;  /* one game frame every 16 interrupts */
    } else {
        CYC(1);
    }

    CYC(11);
    hw_wtchdg();
    CYC(7);
    S_CMODE = hw_opton2();
    CYC(6);
    moolah();

    /* Coin counters: each counter's drive bit is bit 7 of its timer. */
    CYC(30);
    hw_emcnt((uint8_t)((S_CCTIM_AT(1) << 3) |
        ((S_CCTIM >> 7) << 2) | ((S_CCTIM_AT(1) >> 7) << 1) |
        (S_CCTIM_AT(2) >> 7)));

    CYC(6);
    modsnd();
    start_priority_sound();
    sound = (uint8_t)(explosion_sound_bits() | SNDWRD);
    CYC(5);

    if (ATRACT & 0x80) {
        CYC(14);
        hw_sound((uint8_t)(sound | 0xA0));
    } else if (GOVER & 0x80) {
        CYC(21);
        hw_sound((uint8_t)(sound | 0x60));
    } else {
        CYC(13);
        poll_start_button();
    }

    /* Every sixth interrupt: stop the vector generator, flip the JMP at the
     * head of vector RAM to the other buffer if the game has finished one,
     * and restart the generator. */
    CYC(5);
    if (--VTIMER != 0) {
        CYC(25);
        return;
    }
    CYC(6);
    hw_vstop();
    if (BUFRDY == 0) {
        uint8_t page = (uint8_t)(VRAM(A_VECRAM + 1) ^ 0x04);
        VRAM(A_VECRAM + 1) = page;
        if (page & 0x04) {
            CYC(25);
            BUFRDY = (uint8_t)(A_VECRAM / 0x100);
        } else {
            CYC(27);
            BUFRDY = (uint8_t)(A_VECRAM / 0x100 + 8);
        }
    } else {
        CYC(6);
    }
    CYC(4);
    hw_goadd(BUFRDY);
    VTIMER = 6;
    CYC(27);
}
