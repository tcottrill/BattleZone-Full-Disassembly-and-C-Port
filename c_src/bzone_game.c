/* Battlezone frame orchestration, collision, scoring, and player motion.
 * Native game calculations operate on wrapping fixed-point world values.
 * CYC charges preserve the original device schedule and RAM scratch stores
 * remain part of the observable interface used by the ROM oracle. */
#include <stdint.h>
#include "state.h"
#include "hw.h"
#include "game.h"
#include "bzone_game.h"
#include "bzone_disp.h"
#include "bzone_math.h"
#include "bzone_nmi.h"
#include "bzsoun.h"
#include "coin65.h"

void hw_wait_frame(void);

/* ----------------------------------------------------------------------- */

/* Native game arithmetic; timing annotations preserve the hardware schedule. */
/* Coordinates and headings use little-endian fixed-point storage. */
static uint16_t game_word(uint8_t low, uint8_t high)
{
    return (uint16_t)(low + 256u * high);
}

/* Scores are four decimal digits, stored as two packed digit pairs. */
static void award_points(uint8_t player, unsigned points)
{
    unsigned score = (HITS_AT(player) & 15u) + 10u * (HITS_AT(player) >> 4)
        + 100u * (HITS_AT(player + 1) & 15u) + 1000u * (HITS_AT(player + 1) >> 4);
    score = (score + points) % 10000u;
    HITS_AT(player) = (uint8_t)((score % 10u) + 16u * (score / 10u % 10u));
    HITS_AT(player + 1) = (uint8_t)((score / 100u % 10u) + 16u * (score / 1000u));
}

void main_init(void)
{
    /* nothing yet owned by this module runs once before the first pass */
}

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

void bz_main(void) { (void)bz_main_body(); }
void b_end(void)   { (void)b_end_body(); }

/* Draw the aiming cue and retain the relative bearing for direction text. */
static void draw_aiming_cue(void)
{
    CYC(6);
    uint8_t bearing = track();
    CYC(12);
    int8_t relative_bearing = (int8_t)((bearing ^ 0x80) - TANGLE);
    SAVE2 = (uint8_t)relative_bearing;
    uint8_t separation;
    if (relative_bearing < 0) { CYC(6); separation = (uint8_t)-relative_bearing; }
    else { CYC(1); separation = (uint8_t)relative_bearing; }
    CYC(9);
    PTURN = separation;
    if (LOGO & 0x80) { CYC(1); return; }
    CYC(7);
    if (FIRECT) {
        CYC(7);
        if (!(S_INTCT & 0x20)) { CYC(1); return; }
    } else CYC(1);
    uint16_t crosshair = separation >= 2 ? A_XCROSS : A_XCROS1;
    CYC(separation >= 2 ? 9 : 7);
    CYC(6);
    vgjsrl((uint8_t)(crosshair >> 8), (uint8_t)crosshair);
}

/* Textual bearing hints blink independently of the crosshair. */
static void draw_enemy_direction(void)
{
    CYC(7);
    if (!(FRAME & 2)) { CYC(1); return; }
    CYC(7);
    if (PTURN < 0x16) { CYC(1); return; }
    CYC(8);
    msgs(K_ETO);
    CYC(9);
    uint8_t direction = K_REAR;
    if (PTURN > 0x6B) CYC(1);
    else {
        CYC(7);
        direction = K_LEFT;
        if (SAVE2 & 0x80) { CYC(2); direction = K_RIGHT; }
        else CYC(1);
    }
    CYC(6);
    msgs(direction);
}

pass_signal bz_main_body(void)
{
    hw_wait_frame();
    CYC(6);
    volcno();
    hw_wait_bufrdy();
    uint8_t display_page = BUFRDY;
    CYC(16);
    VGLIST = (uint8_t)((A_VECRAM & 255) + 2);
    VGLIST_AT(1) = display_page;
    vgsca1(1);
    CYC(6);
    vgcntr();
    CYC(12);
    vgvtr(0x81, 0x81, 0);
    CYC(8);
    vgstat(0, 3);
    CYC(7);
    ++FRAME;
    int change_scene = 0;
    if (!FRAME) {
        CYC(10);
        ++TIMOUT;
        change_scene = TIMOUT >= 4;
    } else CYC(1);
    CYC(5);
    if (GOVER & 0x80) {
        CYC(6); bigwnd(); CYC(3);
        return PASS_SIG_HISCRE;
    }
    CYC(1 + 5);
    int gameplay = (ATRACT & 0x80) != 0;
    if (gameplay) CYC(1);
    else {
        CYC(5);
        if (CRACK) { CYC(1); gameplay = 1; }
    }
    if (gameplay) {
        CYC(2);
        if (!change_scene) CYC(1);
        else {
            CYC(5);
            if (R2D3FL & 0x80) CYC(1);
            else { CYC(6); r2d3ck(); }
        }
    } else {
        CYC(2);
        if (!change_scene) CYC(1);
        else {
            CYC(6);
            tankck();
            CYC(20);
            DSPLAY ^= 0x80;
            TIMOUT = 3;
            FRAME = DSPLAY;
            if (DSPLAY & 0x80) CYC(1);
            else {
                CYC(5);
                if (ATRACT & 0x80) { CYC(1); gameplay = 1; }
                else { CYC(6); LOGO = 255; }
            }
        }
        if (!gameplay) {
            CYC(6);
            if (DSPLAY & 0x80) {
                CYC(6); bigwnd(); CYC(3);
                return PASS_SIG_DISTBL;
            }
            CYC(1);
        }
    }

    CYC(6);
    vgcntr();
    CYC(12);
    vgvtr(0x7F, 0x30, 0);
    CYC(8);
    vgstat(2, 3);
    CYC(5);
    if (!(ATRACT & 0x80)) { CYC(8); msgs(K_COPYRT); }
    else CYC(1);
    CYC(6);
    mounts();
    CYC(12);
    --ACTION;
    if (FTIMER != 255) { CYC(5); ++FTIMER; }
    else CYC(1);

    CYC(11);
    hw_mb_write(A_DIVCYC - 0x1860, 10);
    SAVE0 = 0;
    CYC(6);
    rotate();
    CYC(6);
    battle();
    do {
        CYC(9);
        if (PTBLO2_AT(SAVE0) & 0x80) { CYC(1); break; }
        CYC(6);
        pntput(SAVE0);
        CYC(13);
        draw(PTBLO2_AT(SAVE0));
        SAVE0 = (uint8_t)(SAVE0 + 2);
        CYC(SAVE0 ? 13 : 12);
    } while (SAVE0);

    CYC(5);
    if (CRACK) { CYC(3); return PASS_SIG_WNSHLD; }
    CYC(1 + 6);
    bigwnd();
    CYC(6);
    dradar();
    draw_aiming_cue();
    CYC(5);
    if (COLFLG_AT(2)) CYC(1);
    else {
        CYC(5);
        if (ATRACT & 0x80) CYC(1);
        else {
            CYC(17 + 54 * 9 + 53 + 12);
            TBLPTR_AT(1) = VGLIST_AT(1) & 0xF8;
            TBLPTR = 2;
            uint16_t table = game_word(TBLPTR, TBLPTR_AT(1));
            uint8_t checksum = 0;
            for (int entry = 53; entry >= 0; --entry)
                checksum ^= cpu_rd((uint16_t)(table + entry));
            TRAP2 = checksum >> 1;
            if (LOGO & 0x80) { CYC(1); return PASS_SIG_BEND; }
        }
        draw_enemy_direction();
    }
    CYC(5);
    if (!OBJCOL) CYC(1);
    else {
        CYC(7);
        if (FRAME & 4) CYC(1);
        else { CYC(8); msgs(K_BLOCKD); }
    }
    return PASS_SIG_BEND;
}

/* Finish the display, advance projectiles in four collision substeps,
 * then update enemy and player motion for the next frame. */
pass_signal b_end_body(void)
{
    CYC(6);
    info();
    CYC(5);
    if (!GOVER) CYC(1);
    else {
        CYC(5);
        if (CRACK) CYC(1);
        else {
            CYC(6);
            if (LOGO & 0x80) CYC(1);
            else {
                CYC(8);
                msgs(K_GAMOVR);
                CYC(5);
                uint8_t prompt = K_PRSTRT;
                if (S_S_CRDT) CYC(1);
                else {
                    CYC(7);
                    unsigned coin_mode = S_CMODE & 3;
                    if (!coin_mode) CYC(1);
                    else {
                        CYC(12);
                        msgs((uint8_t)(coin_mode * 2 + K_MODE1 - 2));
                        CYC(8);
                        msgs(K_CONPLY);
                        CYC(5);
                        prompt = K_INSCON;
                    }
                }
                if (prompt == K_PRSTRT) CYC(2);
                CYC(5);
                if (S_INTCT & 0x40) { CYC(6); msgs(prompt); }
                else CYC(1);
            }
        }
    }
    CYC(6);
    finish();
    CYC(6);
    if (SUPER & 0x80) {
        CYC(12);
        if ((TRAP2 >> 1) || POINT) CYC(1);
        else {
            CYC(20);
            SUPER = 0;
            SNDWRD |= K_LOX;
            EXPCNT = 255;
        }
        CYC(3);
        return PASS_SIG_YIELD;
    }

    CYC(5);
    SAVE3 = 4;
    do {
        CYC(6); colchk();
        CYC(6); shupdt();
        --SAVE3;
        CYC(SAVE3 ? 8 : 7);
    } while (SAVE3);

    CYC(5);
    int fast_enemy;
    if (R2D3FL & 0x80) { CYC(1); fast_enemy = 1; }
    else {
        CYC(6);
        fast_enemy = tr7chk();
        CYC(fast_enemy ? 2 : 3);
    }
    if (fast_enemy) { CYC(6); qwikck(); }
    CYC(6); robot();
    CYC(6); saucmv();
    CYC(6); bound();
    CYC(6); motion();
    CYC(3);
    return PASS_SIG_YIELD;
}

uint8_t explde(uint8_t output)
{
    CYC(2);
    for (int slot = 10; slot >= 0; slot -= 2) {
        CYC(8);
        if (EXPOSZ_AT(slot + 1) & 0x80) CYC(3);
        else {
            uint8_t orientation;
            CYC(130);
            /* Integrate debris position and the signed vertical velocity. */
            uint16_t px = (uint16_t)(game_word(EXPOSX_AT(slot), EXPOSX_AT(slot + 1))
                + game_word(cpu_rd((uint16_t)(A_EXPTBX + slot)), cpu_rd((uint16_t)(A_EXPTBX + slot + 1))));
            EXPOSX_AT(slot) = (uint8_t)px;
            hw_mb_write((uint8_t)(A_XMATRX - 0x1860), (uint8_t)px);
            EXPOSX_AT(slot + 1) = (uint8_t)(px >> 8);
            hw_mb_write((uint8_t)(A_XMATRX + 1 - 0x1860), (uint8_t)(px >> 8));
            uint16_t py = (uint16_t)(game_word(EXPOSY_AT(slot), EXPOSY_AT(slot + 1))
                + game_word(cpu_rd((uint16_t)(A_EXPTBY + slot)), cpu_rd((uint16_t)(A_EXPTBY + slot + 1))));
            EXPOSY_AT(slot) = (uint8_t)py;
            hw_mb_write((uint8_t)(A_YMATRX - 0x1860), (uint8_t)py);
            EXPOSY_AT(slot + 1) = (uint8_t)(py >> 8);
            uint16_t vertical_step = (uint16_t)((int8_t)ZVELOC_AT(slot) * 4);
            TEMP1 = (uint8_t)vertical_step;
            TEMP1_AT(1) = (uint8_t)(vertical_step >> 8);
            unsigned height = game_word(EXPOSZ_AT(slot), EXPOSZ_AT(slot + 1)) + (unsigned)vertical_step;
            EXPOSZ_AT(slot) = (uint8_t)height;
            EXPOSZ_AT(slot + 1) = (uint8_t)(height >> 8);
            int velocity = (int8_t)ZVELOC_AT(slot);
            if (velocity >= 0) {
                CYC(1 + 7);
                ZVELOC_AT(slot) = (uint8_t)(velocity - 4 + (height > 65535u));
            } else {
                CYC(4);
                if (velocity < -123) CYC(1);
                else { CYC(7); ZVELOC_AT(slot) = (uint8_t)(velocity - 3); }
            }
            CYC(23);
            unsigned spin = slot * 4u + 3u;
            if (slot & 2u) {
                CYC(13);
                TEMP1 = (uint8_t)spin;
                orientation = (uint8_t)(EXPOSO_AT(slot) - spin);
            } else {
                CYC(5);
                orientation = (uint8_t)(EXPOSO_AT(slot) + spin);
            }
            CYC(23);
            EXPOSO_AT(slot) = orientation;
            PTBLO2_AT((uint8_t)(output + 1)) = orientation;

            uint8_t object_type = (uint8_t)(slot / 2 + 0x10);
            if (R2D3FL & 0x80) {
                CYC(5);
                object_type += 8;
            } else {
                CYC(1 + 4);
                if (object_type != 0x13) CYC(1);
                else {
                    CYC(9);
                    TEMP1 = object_type;
                    if (tr7chk()) { CYC(7); ++TEMP1; }
                    else CYC(3);
                    CYC(3);
                    object_type = TEMP1;
                }
            }
            CYC(19);
            PTBLO2_AT(output) = object_type;
            hw_mb_write(A_YSTRT1 - 0x1860, EXPOSY_AT(slot + 1));
            output = rotpnt(output);
        }
        CYC(slot ? 9 : 7);
    }

    CYC(2);
    for (int slot = 10; slot >= 0; slot -= 2) {
        CYC(6);
        if (!(EXPOSZ_AT(slot + 1) & 0x80)) { CYC(7); return output; }
        CYC(slot ? 7 : 6);
    }
    CYC(16);
    COLFLG_AT(2) = 0;
    robchk();
    CYC(12);
    return output;
}

/* Check the player's active shell against the enemy tank. */
int qwikck(void)
{
    CYC(5);
    if (FIRECT & 0x80) { CYC(12); return 0; }
    CYC(2);
    if (!FIRECT) { CYC(11); return 0; }
    CYC(5);
    return shrtck(0, 2);
}

/* Shell-vs-tank collision. The mathbox is also primed for the saucer and
 * obstacle checks that follow a miss. */
int shrtck(uint8_t shooter, uint8_t target)
{
    CYC(38);
    hw_mb_write(A_ACOS - 0x1860, SHELLX_AT(shooter));
    hw_mb_write(A_ACOS + 1 - 0x1860, SHELLX_AT(shooter + 1));
    hw_mb_write(A_BSIN - 0x1860, SHELLY_AT(shooter));
    hw_mb_write(A_BSIN + 1 - 0x1860, SHELLY_AT(shooter + 1));
    if (COLFLG_AT(target)) { CYC(15); return 0; }

    CYC(56);
    hw_mb_write(A_TPOSXT - 0x1860, TPOSX_AT(target));
    hw_mb_write(A_TPOSXT + 1 - 0x1860, TPOSX_AT(target + 1));
    hw_mb_write(A_TPOSYT - 0x1860, TPOSY_AT(target));
    hw_mb_write(A_DSTNCE - 0x1860, TPOSY_AT(target + 1));
    TEMP3 = hw_mb_ylow();
    uint8_t distance_high = hw_mb_yhigh();
    if (distance_high & 0x80) { CYC(12); return 0; }
    CYC(9);
    unsigned distance = game_word(TEMP3, distance_high);
    /* The early distance exit leaves the half-distance scratch byte. */
    TEMP3 = (uint8_t)(distance / 2u);
    CYC(2);
    if (distance >= 1024) { CYC(12); return 0; }
    TEMP3 = (uint8_t)(distance / 4u);
    CYC(10);
    if (!(R2D3FL & 0x80)) CYC(1);
    else {
        CYC(14);
        if (game_word(EXPOSZ_AT(12), EXPOSZ_AT(13)) >= K_TOP) {
            CYC(12); return 0;
        }
    }

    CYC(12);
    uint8_t heading_difference = (uint8_t)((TANGLE - TANGLE_AT(2)) * 2);
    if ((int8_t)heading_difference < 0) {
        CYC(6);
        heading_difference = (uint8_t)-heading_difference;
    } else CYC(1);
    CYC(9 + 5);
    unsigned width = (R2D3FL & 0x80)
        ? heading_difference / 4u + 24u : heading_difference / 8u;
    CYC(20);
    TEMP1 = (uint8_t)(width + width / 2u + 56u);
    if (TEMP1 < TEMP3) { CYC(11); return 0; }
    CYC(1 + 16);
    COLFLG_AT(target) = 0x20;
    TIMOUT = 0;
    if (target != 0) CYC(1);
    else {
        CYC(17);
        CRACK = 2;
        BOUNCE = 255;
        --LIVES;
        if (LIVES) CYC(1);
        else { CYC(5); ++GOVER; }
    }

    CYC(18);
    TEMP1 = HITS_AT(shooter);
    TEMP1_AT(1) = HITS_AT(shooter + 1);
    unsigned points = 1;
    if (shooter != 0) CYC(1 + 2);
    else {
        CYC(7);
        if (R2D3FL & 0x80) { CYC(1); points = 2; }
        else {
            CYC(6);
            int enhanced_tank = tr7chk();
            CYC(4);
            if (enhanced_tank) { CYC(1); points = 3; }
            else CYC(2);
        }
    }
    CYC(28);
    award_points(shooter, points);
    if (shooter) CYC(1);
    else { CYC(6); newlif(); }

    CYC(40);
    FIRECT_AT(shooter) = 0x80;
    EXPCNT_AT(target) = 255;
    SNDWRD_AT(target) |= K_LOX;
    SNDWRD_AT(shooter) &= K_HIX;
    exinit(shooter);
    CYC(14);
    EXPCNT_AT(shooter) = 0x70;
    return 1;
}

/* Search stationary obstacles after the tank and saucer tests miss.
 * Obstacle hits deliberately use the player's explosion/sound slot. */
static void check_shell_obstacles(uint8_t shooter)
{
    CYC(2);
    for (unsigned entry = 0; entry < 256; entry += 2) {
        CYC(6);
        uint8_t object_type = cpu_rd((uint16_t)(A_PTBLO1 + entry));
        if (object_type & 0x80) { CYC(1); return; }
        CYC(71);
        hw_mb_write(A_TPOSXT - 0x1860, cpu_rd((uint16_t)(0x7681 + entry)));
        hw_mb_write(A_TPOSXT + 1 - 0x1860, cpu_rd((uint16_t)(0x7682 + entry)));
        hw_mb_write(A_TPOSYT - 0x1860, cpu_rd((uint16_t)(0x76AB + entry)));
        hw_mb_write(A_DSTNCE - 0x1860, cpu_rd((uint16_t)(0x76AC + entry)));
        TEMP3 = hw_mb_ylow();
        uint8_t distance_high = hw_mb_yhigh();
        uint16_t distance = game_word(TEMP3, distance_high);
        TEMP3 = (uint8_t)(distance / 4u);
        if (distance >= 1024) CYC(1);
        else {
            CYC(19);
            GARBAG = shooter;
            uint8_t radius = cpu_rd((uint16_t)(0x6139 + object_type));
            if (radius < TEMP3) CYC(1);
            else {
                CYC(22);
                FIRECT_AT(shooter) = 0xA0;
                EXPCNT = 0x70;
                SNDWRD &= K_HIX;
                return;
            }
        }
        CYC(entry == 254 ? 6 : 7);
    }
}

void sauchk(uint8_t shooter, uint8_t target)
{
    CYC(5);
    if (!SAUCER) { CYC(4); check_shell_obstacles(shooter); return; }
    CYC(5);
    if (SCOLFG) { CYC(4); check_shell_obstacles(shooter); return; }
    CYC(71);
    hw_mb_write(A_TPOSXT - 0x1860, SAPOSX);
    hw_mb_write(A_TPOSXT + 1 - 0x1860, SAPOSX_AT(1));
    hw_mb_write(A_TPOSYT - 0x1860, SAPOSY);
    hw_mb_write(A_TPOSYT + 1 - 0x1860, SAPOSY_AT(1));
    hw_mb_write(A_DSTNCE - 0x1860, SAPOSY_AT(1));
    TEMP3 = hw_mb_ylow();
    uint8_t distance_high = hw_mb_yhigh();
    uint16_t distance = game_word(TEMP3, distance_high);
    TEMP3 = (uint8_t)(distance / 4u);
    if (distance >= 1024) { CYC(3); check_shell_obstacles(shooter); return; }
    CYC(8);
    if (TEMP3 > 0x90) { CYC(4); check_shell_obstacles(shooter); return; }
    CYC(13);
    SCOLFG = 0x40;
    sndon(K_DISINT);
    CYC(25);
    FIRECT_AT(shooter) = 0xA0;
    EXPCNT_AT(shooter) = 0xA0;
    SNDWRD_AT(target) |= K_LOX;
    if (shooter) { CYC(1); return; }
    CYC(40);
    TEMP1 = HITS;
    TEMP1_AT(1) = HITS_AT(1);
    award_points(0, 5);
    newlif();
    CYC(3);
}

/* Eligibility includes the windshield sequence after the final life. */
static int shell_collision_enabled(uint8_t shooter)
{
    CYC(12);
    if (FIRECT_AT(shooter) & 0x80) { CYC(1); return 0; }
    CYC(2);
    if (!FIRECT_AT(shooter)) { CYC(1); return 0; }
    CYC(5);
    if (CRACK) { CYC(1); return 1; }
    CYC(5);
    if (GOVER) { CYC(1); return 0; }
    return 1;
}

void colchk(void)
{
    CYC(2);
    for (int shooter = 2; shooter >= 0; shooter -= 2) {
        if (shell_collision_enabled((uint8_t)shooter)) {
            uint8_t target = (uint8_t)(2 - shooter);
            CYC(6);
            if (shrtck((uint8_t)shooter, target)) CYC(3);
            else { CYC(5); sauchk((uint8_t)shooter, target); }
        }
        CYC(shooter == 2 ? 9 : 13);
    }
}

/* NEWLIF ($615F): award a bonus life (or the super-bonus) if the score just
 * crossed the right threshold for the current DSW0 bonus-level setting.
 * No register inputs; TEMP1 is a hidden RAM input the caller must set to
 * the score before its current increment (see the header comment on
 * `newlif()` in bzone_game.h). No return. */
void newlif(void)
{
    CYC(22);
    uint8_t bonus_level = cpu_rd((uint16_t)(A_BONTBL + ((hw_option() >> 4) & 3)));
    if (!bonus_level) { CYC(9); return; }
    CYC(5);
    if (HITS_AT(1)) {
        CYC(6);
        if (TEMP1_AT(1)) { CYC(9); return; }
        CYC(25);
        GOVER = 0;
        ++LIVES;
        SUPER = 255;
        sndon(K_SUPBON);
    } else {
        CYC(5);
        if (bonus_level < TEMP1) { CYC(9); return; }
        CYC(5);
        if (bonus_level >= HITS) { CYC(9); return; }
        CYC(18);
        ++LIVES;
        GOVER = 0;
        sndon(K_BONER);
    }
    CYC(8);
}

/* Enemy destruction creates six debris pieces at the enemy position. */
void exinit(uint8_t shooter)
{
    CYC(4);
    if (shooter) { CYC(7); return; }
    CYC(4);
    for (int piece = 5; piece >= 0; --piece) {
        unsigned slot = (unsigned)piece * 2;
        CYC(62);
        ZVELOC_AT(slot) = cpu_rd((uint16_t)(A_IZVEL + piece));
        EXPOSX_AT(slot) = TPOSX_AT(2);
        EXPOSX_AT(slot + 1) = TPOSX_AT(3);
        EXPOSY_AT(slot) = TPOSY_AT(2);
        EXPOSY_AT(slot + 1) = TPOSY_AT(3);
        EXPOSO_AT(slot) = hw_pokey_read(0x0A);
        EXPOSZ_AT(slot) = EXPOSZ_AT(slot + 1) = 0;
        CYC(piece ? 9 : 8);
    }
    CYC(8);
}

/* Save and restore positions around tentative moves and collision checks. */
void saveit(uint8_t tank)
{
    CYC(34);
    OTPOSX = TPOSX_AT(tank);
    OTPOSX_AT(1) = TPOSX_AT(tank + 1);
    OTPOSY = TPOSY_AT(tank);
    OTPOSY_AT(1) = TPOSY_AT(tank + 1);
}

void bakout(uint8_t tank)
{
    CYC(34);
    TPOSX_AT(tank) = OTPOSX;
    TPOSX_AT(tank + 1) = OTPOSX_AT(1);
    TPOSY_AT(tank) = OTPOSY;
    TPOSY_AT(tank + 1) = OTPOSY_AT(1);
}

/* Pivot by half of one coarse heading unit. */
void itangl(uint8_t tank)
{
    CYC(28);
    uint16_t heading = (uint16_t)(game_word(LANGLE_AT(tank), TANGLE_AT(tank)) + 128u);
    LANGLE_AT(tank) = (uint8_t)heading;
    TANGLE_AT(tank) = (uint8_t)(heading >> 8);
}

void dtangl(uint8_t tank)
{
    CYC(28);
    uint16_t heading = (uint16_t)(game_word(LANGLE_AT(tank), TANGLE_AT(tank)) - 128u);
    LANGLE_AT(tank) = (uint8_t)heading;
    TANGLE_AT(tank) = (uint8_t)(heading >> 8);
}

/* Build the tank's motion vector from its coarse heading. */
void m_set(uint8_t tank)
{
    CYC(13);
    TEMP1 = tank;
    /* Two separately rounded fractions reproduce the motion table's
     * three-quarter speed, including its asymmetry for negative values. */
    int component = trig_high(sin_(TANGLE_AT(tank)));
    int half = component >= 0 ? component / 2 : -((-component + 1) / 2);
    int quarter = component >= 0 ? component / 4 : -((-component + 3) / 4);
    int step = half + quarter;
    Y1 = (uint8_t)step;
    Y1_AT(1) = step < 0 ? 255 : 0;
    CYC(23 + (step < 0 ? 5 : 3) + 16);
    component = trig_high(cos_(TANGLE_AT(tank)));
    half = component >= 0 ? component / 2 : -((-component + 1) / 2);
    quarter = component >= 0 ? component / 4 : -((-component + 3) / 4);
    step = half + quarter;
    X1 = (uint8_t)step;
    X1_AT(1) = step < 0 ? 255 : 0;
    CYC(23 + (step < 0 ? 5 : 3) + 12);
}

/* Apply a prepared motion vector; enemy movement also uses this entry. */
void m_for1(uint8_t tank)
{
    CYC(54);
    uint16_t px = (uint16_t)(game_word(TPOSX_AT(tank), TPOSX_AT(tank + 1)) + game_word(X1, X1_AT(1)));
    uint16_t py = (uint16_t)(game_word(TPOSY_AT(tank), TPOSY_AT(tank + 1)) + game_word(Y1, Y1_AT(1)));
    TPOSX_AT(tank) = (uint8_t)px;
    TPOSX_AT(tank + 1) = (uint8_t)(px >> 8);
    TPOSY_AT(tank) = (uint8_t)py;
    TPOSY_AT(tank + 1) = (uint8_t)(py >> 8);
}

void m_for(uint8_t tank)
{
    CYC(6);
    m_set(tank);
    m_for1(tank);
}

/* Reverse motion uses the same vector and rounding as forward motion. */
void m_rev(uint8_t tank)
{
    CYC(6);
    m_set(tank);
    CYC(54);
    uint16_t px = (uint16_t)(game_word(TPOSX_AT(tank), TPOSX_AT(tank + 1)) - game_word(X1, X1_AT(1)));
    uint16_t py = (uint16_t)(game_word(TPOSY_AT(tank), TPOSY_AT(tank + 1)) - game_word(Y1, Y1_AT(1)));
    TPOSX_AT(tank) = (uint8_t)px;
    TPOSX_AT(tank + 1) = (uint8_t)(px >> 8);
    TPOSY_AT(tank) = (uint8_t)py;
    TPOSY_AT(tank + 1) = (uint8_t)(py >> 8);
}

/* Update engine sound and launch a shell only when the previous shell has
 * finished. The two axes retain the original fixed-point quantization. */
static void update_engine_and_fire(uint8_t tank, uint8_t engine_sound)
{
    CYC(22);
    TEMP1 = engine_sound;
    SNDWRD_AT(tank) = (SNDWRD_AT(tank) & 0xEF) | engine_sound;
    if (FIRECT_AT(tank)) { CYC(7); return; }
    CYC(8);
    if (!(PL1MOV & K_PL1FIR)) { CYC(7); return; }
    CYC(53);
    FIRECT_AT(tank) = 0x7F;
    SNDWRD_AT(tank) |= K_LOUDSH;
    SXPCNT = 5;

    uint8_t fine_heading = (uint8_t)(LANGLE_AT(tank) * 4u + LANGLE_AT(tank) / 128u);
    SAVE1 = tank;
    /* Shell speed is the heading component / 128, rounded down; TEMP1 keeps
     * the leftover fraction. */
    trig_sample component = cosxtr(fine_heading, TANGLE_AT(tank));
    CYC(21 + (component.value < 0 ? 3 : 5) + 17 + 15);
    uint16_t velocity = (uint16_t)(trig_high(component) * 2 + trig_low(component) / 128u);
    TEMP1 = (uint8_t)(trig_low(component) * 2u);
    SINCX_AT(tank) = (uint8_t)velocity;
    SINCX_AT(tank + 1) = (uint8_t)(velocity >> 8);

    component = sinxtr(GARBAG, TANGLE_AT(tank));
    CYC(21 + (component.value < 0 ? 3 : 5) + 17 + 32 + 6);
    velocity = (uint16_t)(trig_high(component) * 2 + trig_low(component) / 128u);
    TEMP1 = (uint8_t)(trig_low(component) * 2u);
    SINCY_AT(tank) = (uint8_t)velocity;
    SINCY_AT(tank + 1) = (uint8_t)(velocity >> 8);
    SHELLX_AT(tank) = TPOSX_AT(tank);
    SHELLX_AT(tank + 1) = TPOSX_AT(tank + 1);
    SHELLY_AT(tank) = TPOSY_AT(tank);
    SHELLY_AT(tank + 1) = TPOSY_AT(tank + 1);
}

/* Resolve a tentative move against obstacles before accepting its position. */
static void resolve_player_move(void)
{
    CYC(6);
    object_contact collision = objobj(0);
    CYC(2);
    if (!collision.touching) {
        CYC(1 + 5);
        OBJCOL = 0;
        return;
    }
    CYC(6);
    bakout(0);
    CYC(5);
    if (OBJCOL) { CYC(1); return; }
    CYC(8);
    sndon(K_BOING);
    CYC(13);
    BOUNCE = 0x3F;
    ++OBJCOL;
}

static void drive_player(void)
{
    /* Each pair of tread switches selects a turn and a translation.
     * Opposed treads pivot twice; matching treads travel twice. */
    static const struct { int8_t turn, travel; uint8_t steps; } controls[16] = {
        {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0},
        {0,1,2}, {-1,0,2}, {-1,1,1}, {0,0,0},
        {1,0,2}, {0,-1,2}, {1,-1,1}, {0,0,0},
        {1,1,1}, {-1,-1,1}, {0,0,0}
    };
    CYC(6);
    saveit(0);
    CYC(20);
    hw_pokey_write(0x0B, 0);
    PL1MOV = hw_pokey_read(8);
    unsigned switches = (PL1MOV ^ 15) & 15;
    if (switches < 5) CYC(1);
    else CYC(24);
    int turn = controls[switches].turn;
    int travel = controls[switches].travel;
    unsigned steps = controls[switches].steps;
    if (!steps) {
        CYC(2);
        update_engine_and_fire(0, K_LIDLE);
        return;
    }

    if (turn) {
        unsigned turns = travel ? 1 : steps;
        for (unsigned step = 0; step < turns; ++step) {
            CYC(6);
            if (turn > 0) itangl(0);
            else dtangl(0);
        }
        CYC(3);
    }
    if (travel) {
        for (unsigned step = 0; step < steps; ++step) {
            CYC(6);
            if (travel > 0) m_for(0);
            else m_rev(0);
        }
        if (travel > 0) { CYC(9); ++TRDCTR; }
        else { CYC(6); --TRDCTR; }
        resolve_player_move();
    } else {
        CYC(5);
        OBJCOL = 0;
    }
    CYC(5);
    update_engine_and_fire(0, K_HIDLE);
}

void motion(void)
{
    CYC(5);
    if (ATRACT & 0x80) {
        CYC(1 + 8);
        if (!COLFLG) { CYC(1); drive_player(); }
        else CYC(6);
        return;
    }
    CYC(6);
    if (LOGO & 0x80) { CYC(7); return; }
    CYC(5);
    if (CRACK) { CYC(7); return; }
    CYC(7);
    if (FRAME & 0x40) { CYC(7); m_rev(0); }
    else { CYC(6); m_for(0); CYC(3); }
    CYC(5);
    if (!(TPOSX_AT(3) & 0x80)) { CYC(4); dtangl(0); return; }
    CYC(2);
    if (!(TPOSX_AT(3) & 0x40)) { CYC(7); return; }
    CYC(3);
    itangl(0);
}

void react(uint8_t reaction)
{
    CYC(8);
    if (!(ATRACT & 0x80)) {
        CYC(8 + 2 + 15);
        ACTION = 0x30;
        SKILL = 20;
        return;
    }
    CYC(1 + 4);
    if (reaction >= 5) { CYC(1 + 2); reaction = 4; }
    else {
        CYC(18);
        reaction = (uint8_t)((4 - reaction) * 16);
        if (reaction) CYC(1);
        else { CYC(2); reaction = 4; }
    }
    CYC(8);
    ACTION = reaction;
    if (SKILL & 0x80) { CYC(1 + 2); reaction = 10; }
    else {
        CYC(9);
        if (SKILL > 10) { CYC(5); reaction = 1; }
        else { CYC(1); reaction = (uint8_t)(10 - SKILL); }
    }
    CYC(15);
    SKILL = (uint8_t)(reaction * 2u);
}

int tr7chk(void)
{
    CYC(8);
    if (NOR2D3 & 0x80) { CYC(7); return 0; }
    CYC(8);
    return NOR2D3 >= 5;
}
