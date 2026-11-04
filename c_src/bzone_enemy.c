/* Enemy steering, saucer flight, obstacle contacts and spawn placement.
 * Timing charges retain the original hardware schedule. Coordinates are
 * wrapping 16-bit world values and headings use 256 units per revolution. */
#include <stdint.h>
#include "state.h"
#include "hw.h"
#include "game.h"
#include "bzone_game.h"
#include "bzone_math.h"

#define A_ENEMY_PTBLX1 0x7681u
#define A_ENEMY_PTBLY1 0x76ABu
#define A_ENEMY_PROXTB 0x6995u

static object_contact find_contact(uint8_t tank);
static void spawn_enemy(uint8_t heading_mask);

/* Keep the bomber out of the player's rear hemisphere. Once outside that
 * exclusion zone it turns toward the tracked bearing, with the game's
 * asymmetric one/two-unit steering response. */
static void steer_bomber(void)
{
    int rear_error, goal_error, separation;
    CYC(14);
    rear_error = (int8_t)(uint8_t)(TANGLE - RGOAL);
    TEMP2 = (uint8_t)rear_error;
    CYC(rear_error >= 0 ? 1 : 6);
    separation = rear_error < 0 ? -rear_error : rear_error;
    CYC(4);
    if (separation < 64) {
        CYC(5);
        if (rear_error >= 0) {
            CYC(1);
            CYC(5); --RGOAL;
            CYC(5); --RGOAL;
        } else {
            CYC(3);
            CYC(5); ++RGOAL;
            CYC(8); ++RGOAL;
        }
        return;
    }
    CYC(1);
    CYC(6);
    {
        uint8_t bearing = track();
        CYC(15);
        TEMP2 = bearing;
        goal_error = (int8_t)(uint8_t)(RGOAL - bearing);
    }
    if (!goal_error) { CYC(1); return; }
    CYC(2);
    if (goal_error >= 0) {
        CYC(1);
        CYC(4);
        if (goal_error < 3) CYC(1);
        else { CYC(5); --RGOAL; }
        CYC(5); --RGOAL;
    } else {
        CYC(4);
        if (goal_error < -3) CYC(1);
        else { CYC(5); ++RGOAL; }
        CYC(8); ++RGOAL;
    }
}

/* Bomber weaving starts beyond a difficulty-dependent distance. Difficulty
 * and hit totals are packed decimal, but the distance rule uses their
 * packed-byte difference, an intentional original-game quirk. */
static uint8_t bomber_heading(void)
{
    uint8_t weave_distance = 8;
    int weave = 0;
    CYC(6);
    if (!NOR2D3) {
        CYC(1);
    } else {
        CYC(5);
        if (HITS_AT(1)) {
            CYC(1);
            CYC(2);
        } else {
            uint8_t difficulty;
            unsigned threshold;
            CYC(31);
            difficulty = cpu_rd((uint16_t)(A_MISLVL + ((hw_option() / 4) & 3)));
            threshold = ((difficulty / 16) * 10 + difficulty % 16 + 25) % 100;
            weave_distance = (uint8_t)((threshold / 10) * 16 + threshold % 10 - HITS);
            if ((int8_t)weave_distance < 0) {
                CYC(1);
                CYC(2);
                weave_distance = 8;
            } else {
                CYC(4);
                if (weave_distance >= 8) CYC(1);
                else { CYC(2); weave_distance = 8; }
            }
        }
        CYC(6);
        weave = weave_distance < TDIST;
        if (weave) CYC(1);
    }
    if (!weave) { CYC(6); return RGOAL; }
    CYC(24);
    TEMP1 = FRAME & 31;
    if (FRAME & 8) {
        CYC(4);
        return (uint8_t)(RGOAL - TEMP1);
    }
    CYC(6);
    return (uint8_t)(RGOAL + TEMP1);
}

void buzbom(void)
{
    object_contact contact;
    uint8_t heading;
    CYC(29);
    ++TRDCTR_AT(2);
    GARBAG = (unsigned)EXPOSZ_AT(12) + 256u * EXPOSZ_AT(13) <= K_TOP;
    if (GARBAG & OBJCOL_AT(2)) {
        CYC(3);
        CYC(12);
        ++EXPOSZ_AT(13);
        return;
    }
    steer_bomber();
    heading = bomber_heading();
    CYC(9);
    TANGLE_AT(2) = heading;
    {
        int velocity = sin_(heading).value;
        uint16_t step = (uint16_t)(velocity >= 0 ? velocity / 64 : -((-velocity + 63) / 64));
        CYC(13 + (velocity < 0 ? 5 : 4) + 28 + 9);
        Y1 = (uint8_t)step; Y1_AT(1) = (uint8_t)(step >> 8);
    }
    {
        int velocity = cos_(TANGLE_AT(2)).value;
        uint16_t step = (uint16_t)(velocity >= 0 ? velocity / 64 : -((-velocity + 63) / 64));
        CYC(13 + (velocity < 0 ? 5 : 4) + 28 + 24 + 8);
        X1 = (uint8_t)step; X1_AT(1) = (uint8_t)(step >> 8);
    }
    OTPOSX = TPOSX_AT(2); OTPOSX_AT(1) = TPOSX_AT(3);
    OTPOSY = TPOSY_AT(2); OTPOSY_AT(1) = TPOSY_AT(3);
    m_for1(2);
    CYC(6);
    if (qwikck()) { CYC(3); CYC(6); return; }
    CYC(10);
    contact = find_contact(2);
    CYC(2);
    if (!contact.touching) {
        CYC(1);
        CYC(15);
        OBJCOL_AT(2) = 0;
        if (!(EXPOSZ_AT(12) | EXPOSZ_AT(13))) CYC(1);
        else { CYC(6); --EXPOSZ_AT(13); }
        CYC(6);
        return;
    }
    CYC(5);
    if (!GARBAG) { CYC(6); return; } /* Fly over contacts while high. */
    CYC(1);
    CYC(4);
    if (!(contact.kind & 0x80)) {
        CYC(35);
        TPOSX_AT(2) = OTPOSX; TPOSX_AT(3) = OTPOSX_AT(1);
        TPOSY_AT(2) = OTPOSY; TPOSY_AT(3) = OTPOSY_AT(1);
        ++OBJCOL_AT(2);
        return;
    }
    CYC(1);
    CYC(48);
    COLFLG = COLFLG_AT(2) = 0x20;
    CRACK = 2;
    BOUNCE = 0xFF;
    {
        unsigned count = ((HITS_AT(2) / 16) * 10 + HITS_AT(2) % 16 + 1) % 100;
        HITS_AT(2) = (uint8_t)((count / 10) * 16 + count % 10);
    }
    hw_pokey_write((uint8_t)(A_CHAN3V & 15), 0);
    hw_pokey_write((uint8_t)(A_CHAN4V & 15), 0);
    exinit(0);
    CYC(20);
    EXPCNT = 0xFF;
    SNDWRD &= K_HIX;
    --LIVES;
    if (LIVES) CYC(7);
    else { CYC(11); ++GOVER; }
}

/* Random velocity components are signed world increments. Reads stay at
 * separate hardware times so the two components do not become identical. */
static void choose_saucer_motion(void)
{
    uint8_t horizontal, vertical;
    CYC(11);
    horizontal = hw_pokey_read(0x0A);
    SAINCX = horizontal;
    CYC(((int8_t)horizontal < 0 ? 6 : 8) + 11);
    SAINCX_AT(1) = (int8_t)horizontal < 0 ? 0xFF : 0;
    vertical = hw_pokey_read(0x0A);
    SAINCY = vertical;
    CYC(((int8_t)vertical < 0 ? 6 : 8) + 15);
    SAINCY_AT(1) = (int8_t)vertical < 0 ? 0xFF : 0;
    STIMER = hw_pokey_read(0x0A) / 2;
}

void saucmv(void)
{
    CYC(5);
    if (SCOLFG) {
        unsigned brightness = SCOLFG;
        CYC(4);
        if (SCOLFG < 0x30) CYC(1);
        else { CYC(7); brightness = (uint8_t)(0x60 - SCOLFG); }
        CYC(26);
        SINT = (uint8_t)((brightness + 8) * 4) & 0xF0;
        SCOLFG -= 2;
        if (SCOLFG) CYC(1);
        else {
            CYC(12);
            STIMER = hw_pokey_read(0x0A);
            SAUCER = 0;
        }
        CYC(6);
        return;
    }
    CYC(1);
    CYC(5);
    if (SAUCER) {
        CYC(15);
        SCANGL += 8;
        if (!STIMER) { CYC(1); choose_saucer_motion(); return; }
        CYC(40);
        {
            uint16_t horizontal = (uint16_t)(SAPOSX + 256u * SAPOSX_AT(1) + SAINCX + 256u * SAINCX_AT(1));
            uint16_t vertical = (uint16_t)(SAPOSY + 256u * SAPOSY_AT(1) + SAINCY + 256u * SAINCY_AT(1));
            SAPOSX = (uint8_t)horizontal; SAPOSX_AT(1) = (uint8_t)(horizontal >> 8);
            SAPOSY = (uint8_t)vertical; SAPOSY_AT(1) = (uint8_t)(vertical >> 8);
        }
        CYC(11);
        --STIMER;
        return;
    }
    CYC(1);
    CYC(7);
    if (HITS < 2) { CYC(6); return; }
    CYC(1);
    CYC(5);
    if (STIMER) { CYC(1); CYC(11); --STIMER; return; }
    {
        uint8_t spawn_x, spawn_y;
        CYC(4);
        spawn_x = hw_pokey_read(0x0A);
        CYC(7);
        SAPOSX_AT(1) = spawn_x;
        spawn_y = hw_pokey_read(0x0A);
        CYC(10);
        SAPOSY_AT(1) = spawn_y;
    }
    ++SAUCER;
    if (SAUCER) { CYC(1); choose_saucer_motion(); }
    else (void)track(); /* Retain the original counter-wrap fall-through. */
}

/* The mathbox owns distance measurement. An overflowing bomber distance
 * skips that obstacle; the last overflow is also observable when an active
 * explosion suppresses the final tank check, an original-game quirk. */
static object_contact find_contact(uint8_t tank)
{
    object_contact result = {0, 0};
    uint8_t obstacle_offset = 0;
    int last_distance_overflow = 0;
    CYC(39);
    TEMP1 = tank;
    hw_mb_write((uint8_t)(A_ACOS - 0x1860), TPOSX_AT(tank));
    hw_mb_write((uint8_t)(A_ACOS + 1 - 0x1860), TPOSX_AT((uint8_t)(tank + 1)));
    hw_mb_write((uint8_t)(A_BSIN - 0x1860), TPOSY_AT(tank));
    hw_mb_write((uint8_t)(A_BSIN + 1 - 0x1860), TPOSY_AT((uint8_t)(tank + 1)));
    do {
        uint16_t radius = 0x0480;
        uint8_t obstacle;
        CYC(6);
        obstacle = cpu_rd((uint16_t)(A_PTBLO1 + obstacle_offset));
        if (obstacle & 0x80) { CYC(1); break; }
        CYC(56);
        hw_mb_write((uint8_t)(A_TPOSXT - 0x1860),
                    cpu_rd((uint16_t)(A_ENEMY_PTBLX1 + obstacle_offset)));
        hw_mb_write((uint8_t)(A_TPOSXT + 1 - 0x1860),
                    cpu_rd((uint16_t)(A_ENEMY_PTBLX1 + obstacle_offset + 1)));
        hw_mb_write((uint8_t)(A_TPOSYT - 0x1860),
                    cpu_rd((uint16_t)(A_ENEMY_PTBLY1 + obstacle_offset)));
        hw_mb_write((uint8_t)(A_DSTNCE - 0x1860),
                    cpu_rd((uint16_t)(A_ENEMY_PTBLY1 + obstacle_offset + 1)));
        TEMP3 = hw_mb_ylow();
        TEMP3_AT(1) = hw_mb_yhigh();
        last_distance_overflow = 0;
        if (!tank) {
            CYC(10);
        } else {
            uint8_t radius_index;
            CYC(1);
            CYC(13);
            radius_index = (uint8_t)(cpu_rd((uint16_t)(A_PTBLO1 + obstacle_offset)) * 2);
            if (!(R2D3FL & 0x80)) {
                CYC(1);
            } else {
                uint16_t distance, half;
                int signed_distance;
                unsigned scaled;
                CYC(34);
                distance = (uint16_t)(TEMP3 + 256u * TEMP3_AT(1));
                signed_distance = (int16_t)distance;
                half = (uint16_t)(signed_distance >= 0 ? signed_distance / 2 : -((-signed_distance + 1) / 2));
                scaled = (unsigned)distance + half;
                TEMP2_AT(1) = (uint8_t)(half >> 8);
                TEMP3 = (uint8_t)scaled; TEMP3_AT(1) = (uint8_t)(scaled >> 8);
                last_distance_overflow = scaled > 65535u;
                CYC(last_distance_overflow ? 6 : 1);
            }
            if (!last_distance_overflow) {
                uint8_t radius_low;
                CYC(14);
                radius_low = cpu_rd((uint16_t)(A_ENEMY_PROXTB + radius_index));
                radius = (uint16_t)(radius_low + 256u * cpu_rd((uint16_t)(A_ENEMY_PROXTB + radius_index + 1)));
            }
        }
        if (!last_distance_overflow) {
            CYC(7);
            if (radius >= (unsigned)TEMP3 + 256u * TEMP3_AT(1)) {
                CYC(1);
                CYC(9);
                result.touching = 1;
                return result;
            }
        }
        obstacle_offset = (uint8_t)(obstacle_offset + 2);
        CYC(obstacle_offset ? 7 : 6);
    } while (obstacle_offset);

    CYC(8);
    result.kind = COLFLG | COLFLG_AT(2);
    if (result.kind) {
        CYC(1);
        CYC(9);
        result.touching = last_distance_overflow;
        return result;
    }
    CYC(65);
    tank ^= 2;
    hw_mb_write((uint8_t)(A_TPOSXT - 0x1860), TPOSX_AT(tank));
    hw_mb_write((uint8_t)(A_TPOSXT + 1 - 0x1860), TPOSX_AT((uint8_t)(tank + 1)));
    hw_mb_write((uint8_t)(A_TPOSYT - 0x1860), TPOSY_AT(tank));
    hw_mb_write((uint8_t)(A_DSTNCE - 0x1860), TPOSY_AT((uint8_t)(tank + 1)));
    TEMP3 = hw_mb_ylow();
    TEMP3_AT(1) = hw_mb_yhigh();
    CYC((R2D3FL & 0x80) ? 2 : 1);
    CYC(7);
    result.touching = TEMP3_AT(1) <= ((R2D3FL & 0x80) ? 3 : 5);
    result.kind = 0xFF;
    CYC(9);
    return result;
}

object_contact objobj(uint8_t tank)
{
    return find_contact(tank);
}

void robchk(void)
{
    uint8_t random_sample;
    int choose_bomber;
    CYC(5);
    if (GOVER) { CYC(1); tankck(); return; }
    CYC(5);
    if (HITS_AT(1)) {
        CYC(1);
    } else {
        unsigned difficulty;
        CYC(21);
        difficulty = (hw_option() / 4) & 3;
        if (HITS < cpu_rd((uint16_t)(A_MISLVL + difficulty))) {
            CYC(1); tankck(); return;
        }
    }
    CYC(18);
    random_sample = hw_pokey_read(0x0A);
    choose_bomber = (random_sample ^ OLDRND) & 1;
    OLDRND = random_sample;
    if (!choose_bomber) { CYC(1); tankck(); return; }
    CYC(8);
    TIMOUT = 2;
    r2d3ck();
}

/* The player's lead widens the spawn heading window, up to half a
 * revolution. A zero/negative lead keeps the narrow initial window. */
static uint8_t tank_spawn_window(void)
{
    unsigned lead;
    if (!(ATRACT & 0x80)) { CYC(1); CYC(5); return 15; }
    CYC(5);
    if (HITS_AT(1)) {
        CYC(1);
        CYC(2);
        lead = 7;
    } else {
        CYC(10);
        if (HITS < HITS_AT(2)) { CYC(1); CYC(5); return 15; }
        lead = HITS - HITS_AT(2);
        CYC(2);
        if (!lead) { CYC(1); CYC(5); return 15; }
        CYC(4);
        if (lead < 7) CYC(1);
        else { CYC(2); lead = 7; }
    }
    CYC(4);
    lead /= 2;
    if (!lead) { CYC(1); CYC(5); return 15; }
    CYC(4);
    CYC(9 * lead - 1 + 3);
    return (uint8_t)((16u << lead) - 1);
}

void tankck(void)
{
    CYC(28);
    RGOAL = hw_pokey_read(0x0A);
    R2D3FL = 0;
    COLFLG_AT(2) = 0;
    ACTION = TIMOUT = 1;
    spawn_enemy(tank_spawn_window());
}

void r2d3ck(void)
{
    CYC(46);
    ++NOR2D3;
    EXPOSZ_AT(13) = (uint8_t)(A_STARTZ >> 8);
    EXPOSZ_AT(12) = (uint8_t)(A_STARTZ & 0xFF);
    STATE = 0x80;
    hw_pokey_write((uint8_t)(A_CHAN3F & 15), 0xFF);
    hw_pokey_write((uint8_t)(A_CHAN4F & 15), 0xFE);
    R2D3FL = 0xFF;
    spawn_enemy(hw_pokey_read(0x0A) & 15);
}

/* Spawn an enemy at three quarters of the trigonometric radius. Tanks
 * randomly use half that radius; both axes share the same random choice. */
static void spawn_enemy(uint8_t heading_mask)
{
    int vx, vy, dx, dy;
    uint8_t heading_offset;
    int close_spawn = 0;
    uint16_t position;
    trig_sample direction;

    CYC(15);
    TEMP1 = heading_mask;
    heading_offset = hw_pokey_read(0x0A) & heading_mask;
    if (!(S_INTCT & 0x40)) { CYC(2); heading_offset ^= 0xFF; } else CYC(1);
    CYC(14);
    TEMP1 = (uint8_t)(heading_offset + TANGLE);
    direction = cos_(TEMP1);
    CYC(56);
    X1 = trig_low(direction);
    X1_AT(1) = (uint8_t)trig_high(direction);
    vx = direction.value;
    dx = vx - (vx >= 0 ? vx / 4 : -((-vx + 3) / 4));
    if (R2D3FL & 0x80) {
        CYC(1);
    } else {
        CYC(13);
        close_spawn = hw_pokey_read(0x0A) & 1;
        if (close_spawn) {
            CYC(9);
            dx = dx >= 0 ? dx / 2 : -((-dx + 1) / 2);
        } else CYC(1);
    }
    CYC(12);
    TEMP2 = (uint8_t)dx;
    TEMP2_AT(1) = (uint8_t)((uint16_t)dx >> 8);
    direction = sin_(TEMP1);
    CYC(54);
    Y1 = trig_low(direction);
    Y1_AT(1) = (uint8_t)trig_high(direction);
    vy = direction.value;
    dy = vy - (vy >= 0 ? vy / 4 : -((-vy + 3) / 4));
    if (R2D3FL & 0x80) {
        CYC(1);
    } else {
        CYC(6);
        if (close_spawn) {
            CYC(9);
            dy = dy >= 0 ? dy / 2 : -((-dy + 1) / 2);
        } else CYC(1);
    }
    CYC(48);
    TEMP1 = (uint8_t)dy;
    TEMP1_AT(1) = (uint8_t)((uint16_t)dy >> 8);
    position = (uint16_t)(TPOSX + 256u * TPOSX_AT(1) + dx);
    TPOSX_AT(2) = (uint8_t)position;
    TPOSX_AT(3) = (uint8_t)(position >> 8);
    position = (uint16_t)(TPOSY + 256u * TPOSY_AT(1) + dy);
    TPOSY_AT(2) = (uint8_t)position;
    TPOSY_AT(3) = (uint8_t)(position >> 8);
    if (!(R2D3FL & 0x80)) {
        CYC(1);
    } else {
        CYC(6);
        uint8_t bearing = track();
        CYC(6);
        TANGLE_AT(2) = bearing;
        RGOAL = bearing;
    }
    CYC(14);
    EIRNGE = 0;
    FTIMER = 0;
}
