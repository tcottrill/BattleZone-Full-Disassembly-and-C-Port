/* Enemy tank steering, decisions and shell launch.  World vectors are
 * 16-bit fixed point; headings wrap at one byte.  Cycle charges preserve
 * the hardware schedule and RAM temporaries remain visible to the ROM
 * comparison harness, but calculations operate on whole game values. */
#include <stdint.h>
#include "state.h"
#include "hw.h"
#include "bzone_game.h"
#include "bzone_math.h"

void robot(void)
{
    int heading_error, turn_distance, fast_tank;
    uint8_t distance;

    CYC(5);
    if (COLFLG_AT(2)) { CYC(6); return; }
    CYC(1 + 5);
    if (R2D3FL & 0x80) { CYC(3); buzbom(); return; }
    CYC(1 + 8);
    saveit(2);
    CYC(17);
    RANGLE = (uint8_t)(RANGLE + 11);

    if (STATE & 1) {
        /* Preserve the chosen retreat direction across movement. */
        int turn_left = (STATE & 2) != 0;
        CYC(11);
        m_rev(2);
        CYC(6);
        if (turn_left) {
            CYC(6); itangl(2); CYC(3);
        } else {
            CYC(1 + 6); dtangl(2);
        }
        CYC(10);
        --TRDCTR_AT(2);
        if (ACTION) { CYC(6); return; }
        CYC(1 + 25);
        STATE &= 0xFC;
        RGOAL = TANGLE_AT(2);
        ACTION = 0x34;
        return;
    }

    CYC(1 + 5);
    if (!ACTION) { CYC(3); r_40s_(); return; }
    CYC(1 + 12);
    heading_error = (int8_t)(uint8_t)(TANGLE_AT(2) - RGOAL);
    turn_distance = heading_error < 0 ? -heading_error : heading_error;
    CYC(heading_error < 0 ? 6 : 1);
    CYC(5);
    if (turn_distance >= SKILL) {
        CYC(4);
        if (heading_error < 0) {
            CYC(6); itangl(2); CYC(6); fireit();
            CYC(6); itangl(2); CYC(6); fireit();
            CYC(6); fast_tank = tr7chk(); CYC(2);
            if (!fast_tank) { CYC(6); return; }
            CYC(1 + 6); itangl(2); CYC(6); fireit();
            CYC(6); itangl(2);
        } else {
            CYC(1 + 6); dtangl(2); CYC(6); fireit();
            CYC(6); dtangl(2); CYC(6); fireit();
            CYC(6); fast_tank = tr7chk(); CYC(2);
            if (!fast_tank) { CYC(7); return; }
            CYC(6); dtangl(2); CYC(6); fireit();
            CYC(6); dtangl(2);
        }
        CYC(3); fireit();
        return;
    }

    CYC(1 + 4);
    if (!turn_distance) {
        CYC(1);
    } else {
        CYC(4);
        if (heading_error < 0) {
            CYC(6); itangl(2); CYC(3);
        } else {
            CYC(1 + 6); dtangl(2);
        }
    }
    CYC(6); fireit();
    CYC(38);
    /* TRACK's last absolute player offsets may be from an earlier frame.
     * Preserve that intentional stale-distance behavior and write order. */
    hw_mb_write((uint8_t)(A_TPOSXT - 0x1860), TEMP1);
    hw_mb_write((uint8_t)(A_TPOSXT + 1 - 0x1860), TEMP1_AT(1));
    hw_mb_write((uint8_t)(A_TPOSYT - 0x1860), TEMP2);
    hw_mb_write((uint8_t)(A_TPOSYT + 1 - 0x1860), TEMP2_AT(1));
    hw_mb_write((uint8_t)(A_DST2 - 0x1860), TEMP2_AT(1));
    fast_tank = tr7chk();
    distance = hw_mb_yhigh();
    CYC(6);
    if (!fast_tank) CYC(1);
    CYC(4);
    if (distance >= (fast_tank ? 8 : 5)) {
        CYC(1); r_37s_(); return;
    }
    CYC(6);
}

/* Advance once, or twice when pointing exactly at the goal. */
void r_37s_(void)
{
    object_contact collision;
    uint8_t retreat;
    CYC(8); m_set(2);
    CYC(6);
    if (tr7chk()) {
        uint16_t dx = (uint16_t)((X1 + 256u * X1_AT(1)) * 2u);
        uint16_t dy = (uint16_t)((Y1 + 256u * Y1_AT(1)) * 2u);
        CYC(22);
        Y1 = (uint8_t)dy; Y1_AT(1) = (uint8_t)(dy / 256u);
        X1 = (uint8_t)dx; X1_AT(1) = (uint8_t)(dx / 256u);
    } else CYC(3);

    CYC(6); m_for1(2);
    CYC(6); collision = objobj(2);
    CYC(2);
    if (!collision.touching) {
        CYC(13);
        ++TRDCTR_AT(2);
        if (TANGLE_AT(2) != RGOAL) { CYC(6); return; }
        CYC(1 + 6); m_for1(2);
        CYC(6); collision = objobj(2);
        CYC(2);
        if (!collision.touching) { CYC(6); return; }
    }
    CYC(1 + 5);
    retreat = collision.kind;
    if (!(ATRACT & 0x80)) {
        CYC(1);
    } else {
        CYC(4);
        if ((int8_t)retreat < 0) {
            /* A player collision only undoes the move. */
            CYC(1 + 3); bakout(2); return;
        }
        CYC(8);
        retreat = (uint8_t)((hw_pokey_read(0x0A) & 2) | 1);
    }
    CYC(11);
    STATE |= retreat;
    ACTION = 0x30;
    CYC(3); bakout(2);
}

void r_40s_(void)
{
    CYC(5);
    if (!(ATRACT & 0x80)) { CYC(1); r_rand(); return; }
    r_50s_();
}

void r_50s_(void)
{
    CYC(7);
    if (FTIMER == 0xFF) { CYC(1); r_atck(); return; }
    CYC(8);
    if (!(hw_pokey_read(0x0A) & 1)) { CYC(1); r_atck(); return; }
    CYC(5);
    if (HITS_AT(1)) { CYC(1); r_atck(); return; }
    CYC(10);
    if (HITS == HITS_AT(2)) { CYC(1); r_evad(); return; }
    CYC(2);
    if (HITS < HITS_AT(2)) { CYC(1); r_rand(); return; }
    CYC(3); r_atck();
}

void r_evad(void)
{
    CYC(7);
    if (!(S_INTCT & 7)) {
        CYC(11);
        STATE |= 1;
    } else {
        CYC(1 + 11);
        STATE = 0;
        RGOAL = (uint8_t)(track() ^ 0x40);
        CYC(5);
    }
    CYC(11);
    ACTION = 0x40;
}

void r_rand(void)
{
    uint8_t random_heading, goal;
    CYC(11);
    random_heading = (uint8_t)(hw_pokey_read(0x0A) & 0x1F);
    if (S_INTCT & 0x80) {
        CYC(1 + 3);
        goal = (uint8_t)(RGOAL + random_heading);
    } else {
        CYC(5);
        /* The original wandering rule has a one-unit bias and maps an
         * exact zero result back to the random heading. */
        goal = (uint8_t)(random_heading - RGOAL - 1);
        if (goal) CYC(1);
        else {
            CYC(3);
            goal = random_heading > RGOAL ? random_heading : RGOAL;
        }
    }
    CYC(11);
    RGOAL = goal;
    STATE = 0;
    react(0);
}

void r_atck(void)
{
    CYC(6); RGOAL = track();
    CYC(11);
    STATE = 0x80;
    react(0x80);
}

void fireit(void)
{
    int heading_error;
    uint16_t velocity;
    trig_sample direction;
    CYC(7);
    if (FTIMER < 0x20) { CYC(6); return; }
    CYC(1 + 4);
    if (FTIMER == 0xFF) {
        CYC(1);
    } else {
        CYC(5);
        if (HITS_AT(1)) {
            CYC(1);
        } else {
            CYC(7);
            if (HITS >= 2) {
                CYC(1);
            } else {
                CYC(7);
                if (PTURN >= 0x20) { CYC(7); return; }
                CYC(8);
                if (TDIST >= 0x24) { CYC(7); return; }
            }
        }
    }
    CYC(6);
    heading_error = (int8_t)(uint8_t)(track() - TANGLE_AT(2));
    CYC(9);
    /* The firing window is asymmetric: only exact aim and +1 qualify. */
    CYC(heading_error < 0 ? 6 : 1);
    CYC(4);
    if (heading_error < 0 || heading_error >= 2) { CYC(7); return; }
    CYC(5);
    if (FIRECT_AT(2)) { CYC(7); return; }
    CYC(5);
    if (COLFLG) { CYC(7); return; }
    CYC(27);
    FIRECT_AT(2) = 0x7F;
    SXPCNT = 5;
    SNDWRD &= K_SOFTSH;

    direction = cos_(TANGLE_AT(2));
    CYC(8 + (direction.value < 0 ? 5 : 7) + 15 + 9);
    /* Trig returns signed 8.8.  Doubling gives shell speed; the low
     * fractional byte remains in TEMP1, as in the original game. */
    velocity = (uint16_t)(direction.value * 2);
    TEMP1 = (uint8_t)(trig_low(direction) * 2u);
    SINCX_AT(2) = (uint8_t)(velocity / 256u);
    SINCX_AT(3) = direction.value < 0 ? 0xFF : 0;

    direction = sin_(TANGLE_AT(2));
    CYC(8 + (direction.value < 0 ? 5 : 7) + 15 + 24 + 8);
    velocity = (uint16_t)(direction.value * 2);
    TEMP1 = (uint8_t)(trig_low(direction) * 2u);
    SINCY_AT(2) = (uint8_t)(velocity / 256u);
    SINCY_AT(3) = direction.value < 0 ? 0xFF : 0;

    SHELLX_AT(2) = TPOSX_AT(2); SHELLX_AT(3) = TPOSX_AT(3);
    SHELLY_AT(2) = TPOSY_AT(2); SHELLY_AT(3) = TPOSY_AT(3);
}
