/* coin65.c - COIN65/TC65 ($56C3-$57C7, assembled inline in BZONE.MAC): the
 * coin/credit routine, Atari's shared library (Downend & Albaugh), THIS
 * game's own earlier revision of the module Tempest's alcoin.c and Space
 * Duel's coins.c also carry.
 *
 * The genuine difference from the later COIN65 (../disasm/CALLING_NOTES.md
 * section 6): THIS revision has no separate "$BC" bonus-coin-earned cell.
 * The bonus adder credits a bonus coin by adding it to S_CNCT (the
 * unit-coin count) directly, so by the time unit-coins are converted to
 * credits any bonus coins are already counted, and a shortfall simply leaves
 * the coins queued for a later interrupt.
 *
 * Debounce state per mechanism, 2 = right, 1 = centre, 0 = left:
 *   S_CNSTT  coin-on down-counter (bits 0-4, starts $1F) / coin-off
 *            up-counter (bits 5-7), packed in one byte
 *   S_PSTSL  post-coin slam timer (a slam within this window invalidates
 *            a just-accepted coin - "give credit a little early" is the
 *            one place a pending post-coin timer wins a race)
 *   S_CCTIM  electro-mechanical counter pulse timer: queued pulses in the
 *            low nibble, the running pulse's remaining on-time in the high
 *            nibble
 * Shared across mechanisms: S_LMTIM (pre-coin slam timer), S_CMODE (OPTON2
 * copy: coin mode, multipliers, bonus-adder mode), S_BCCNT (bonus-adder
 * unit-coin accumulator), S_CNCT (unit-coin count), S_S_CRDT (credits).
 *
 * The helpers below are one ROM routine split for readability: only the
 * routine's single return (in pulse_coin_counters) carries a return cost,
 * and calling a helper costs nothing.  Each CYC is the ROM's cycle total for
 * that path. */
#include "state.h"
#include "hw.h"
#include "coin65.h"


/* S_MODLO ($5778): unit-coins required per bonus-adder mode (OPTON2 bits
 * 5-7), 8 ROM bytes.  $7F means "never". */
#define A_S_MODLO 0x5778

/* Store a mechanism's new debounce state, then run the slam timers.
 * Returns 1 when the post-coin timer expires: that is when the coin counts. */
static int update_coin_debounce(int mechanism, uint8_t debounce_state)
{
    CYC(10);
    S_CNSTT_AT(mechanism) = debounce_state;
    if (!(hw_in0() & K_S_LMBIT)) {
        CYC(7);
        S_LMTIM = 0xF0;  /* slam switch closed: start the pre-coin slam timer */
    } else {
        CYC(3);
    }

    /* While the pre-coin slam timer runs, any coin in progress is void. */
    if (S_LMTIM != 0) {
        CYC(20);
        --S_LMTIM;
        S_CNSTT_AT(mechanism) = 0;
        S_PSTSL_AT(mechanism) = 0;
    } else {
        CYC(6);
    }

    CYC(6);
    if (S_PSTSL_AT(mechanism) == 0) { CYC(3); return 0; }
    if (--S_PSTSL_AT(mechanism) == 0) { CYC(15); return 1; }
    CYC(11);
    return 0;
}

/* A valid coin-off transition: arm the post-coin timer.  A timer already
 * pending gives the previous coin credit early. */
static int accept_coin(int mechanism)
{
    int credit;
    CYC(8);
    S_CNSTT_AT(mechanism) = 0x1F;
    if (S_PSTSL_AT(mechanism) != 0) { CYC(4); credit = 1; }
    else                            { CYC(3); credit = 0; }
    CYC(6);
    S_PSTSL_AT(mechanism) = 0x78;
    return credit;
}

/* Debounce one coin mechanism; return 1 when a coin is accepted. */
static int detect_coin(int mechanism, uint8_t inputs)
{
    /* Right, centre and left coin switches are inputs bits 0, 1 and 2,
     * active low. */
    int coin_present = !((inputs >> (2 - mechanism)) & 1u);
    uint8_t insertion_timer = (uint8_t)(S_CNSTT_AT(mechanism) & 0x1F);
    CYC(mechanism == 0 ? 20 : 17);

    if (coin_present) {
        /* Count the insertion timer down: every sample while it is above
         * $1B, otherwise one sample in eight. */
        if (insertion_timer == 0) {
            CYC(3);
        } else if (insertion_timer >= 0x1B) {
            CYC(9);
            --insertion_timer;
        } else if ((S_INTCT & 0x07) == 0x07) {
            CYC(21);
            --insertion_timer;
        } else {
            CYC(20);
        }
        return update_coin_debounce(mechanism, insertion_timer);
    }

    /* Coin switch open. */
    if (insertion_timer >= 0x1B) {
        CYC(11);  /* too short to be a coin: restart */
        return update_coin_debounce(mechanism, 0x1F);
    }
    if (S_CNSTT_AT(mechanism) / 32u < 7) {
        /* Count consecutive released samples in the upper three bits. */
        CYC(14);
        return update_coin_debounce(mechanism, (uint8_t)(S_CNSTT_AT(mechanism) + 32));
    }
    CYC(21);
    if (insertion_timer == 0)
        return update_coin_debounce(mechanism, 0x1F);
    return accept_coin(mechanism);
}

/* Unit-coin value of an accepted coin, from the coin-multiplier switches. */
static uint8_t coin_units(int mechanism)
{
    if (mechanism == 0) {
        CYC(7);
        return 1;
    }
    if (mechanism == 1) {
        if (S_CMODE & 0x10) { CYC(18); return 2; }
        CYC(17);
        return 1;
    }
    unsigned multiplier = (S_CMODE & 0x0C) / 4u;  /* right mechanism: 1, 4, 5 or 6 */
    if (multiplier != 0) { CYC(24); return (uint8_t)(multiplier + 3); }
    CYC(20);
    return 1;
}

/* Bonus adder: once enough unit-coins have built up for the selected mode,
 * add one bonus coin (two in mode 3). */
static void add_bonus_coins(void)
{
    uint8_t bonus_mode = (uint8_t)(S_CMODE >> 5);
    int8_t remainder = (int8_t)(S_BCCNT - cpu_rd((uint16_t)(A_S_MODLO + bonus_mode)));

    if (remainder < 0) { CYC(27); return; }
    S_BCCNT = (uint8_t)remainder;
    ++S_CNCT;
    if (bonus_mode == 3) {
        CYC(46);
        ++S_CNCT;
    } else {
        CYC(39);
    }
}

/* Convert unit-coins to credits.  Coin modes 1-3 are 2 credits per coin,
 * 1 credit per coin and 1 credit per 2 coins; mode 0 (free play) discards
 * the coins. */
static void convert_coins_to_credits(void)
{
    unsigned mode = S_CMODE & 0x03;

    if (mode == 0) {
        CYC(13);
        S_CNCT = 0;
        return;
    }
    unsigned price = (mode + 1) / 2;
    if (S_CNCT < price) { CYC(23); return; }  /* wait for more coins */
    if (mode == 1) {
        CYC(39);
        S_S_CRDT = (uint8_t)(S_S_CRDT + 2);
    } else {
        CYC(35);
        ++S_S_CRDT;
    }
    S_CNCT = (uint8_t)(S_CNCT - price);
}

/* Every other interrupt, drive the electro-mechanical coin counters: count
 * down a running pulse, or when none is running start the next queued one
 * (one counter at a time).  This is the routine's only return. */
static void pulse_coin_counters(void)
{
    int mechanism;
    int pulse_running = 0;

    if (S_INTCT & 1) { CYC(14); return; }
    CYC(11);

    for (mechanism = 2; mechanism >= 0; --mechanism) {
        uint8_t pulse_state = S_CCTIM_AT(mechanism);
        if (pulse_state == 0) {
            CYC(7);
        } else if (pulse_state < 0x10) {
            CYC(11);
        } else {
            CYC(18);
            S_CCTIM_AT(mechanism) = (uint8_t)(pulse_state - 0x10);
            pulse_running = 1;
        }
        CYC(mechanism > 0 ? 5 : 4);
    }
    if (pulse_running) { CYC(11); return; }
    CYC(6);

    for (mechanism = 2; mechanism >= 0; --mechanism) {
        uint8_t queued = S_CCTIM_AT(mechanism);
        if (queued != 0) {
            /* 15 ticks on, one fewer queued. */
            CYC(23);
            S_CCTIM_AT(mechanism) = (uint8_t)(0xF0 + queued - 1);
            return;
        }
        CYC(mechanism > 0 ? 12 : 11);
    }
    CYC(6);
}

/* MOOLAH ($56C3): see coin65.h. */
void moolah(void)
{
    CYC(2);
    for (int mechanism = 2; mechanism >= 0; --mechanism) {
        CYC(4);
        uint8_t inputs = hw_in0();
        CYC(2);
        if (detect_coin(mechanism, inputs)) {
            uint8_t units = coin_units(mechanism);
            CYC(29);
            S_BCCNT = (uint8_t)(S_BCCNT + units);
            S_CNCT = (uint8_t)(S_CNCT + units);
            ++S_CCTIM_AT(mechanism);  /* queue a coin-counter pulse */
        } else {
            CYC(1);
        }
        CYC(mechanism > 0 ? 7 : 5);
    }
    add_bonus_coins();
    convert_coins_to_credits();
    pulse_coin_counters();
}
