/* bzone_math.c - BZONE maths and 3D pipeline: ROTATE/ROTPNT/WAIT ($5946-
 * $5B88), PNTPUT/KLUDGE/BOUND ($5D4A-$5E4A), COS/SIN/SIN1/COSXTR/SINXTR/
 * XTRAP ($5E4B-$5F4D), ABSTMP ($6149), TRACK/T_0-T_3 ($6810-$68D5).
 *
 * See NOTES_math.md for the Mathbox register choreography, ROM table
 * addresses and judgement calls; see bzone_math.h for the calling contract
 * of every exposed routine.
 *
 * These routines are the Mathbox's main users: every 2901 rotate/divide the
 * ROM performs lives in this file, always through the hw_mb_* seam in the
 * ROM's own write order (hw.h; ../disasm/MATHBOX_NOTES.md) - the model
 * behind the seam runs the real microcode, so results are bit-exact only if
 * the writes are the same bytes to the same registers in the same order.
 */
#include "state.h"
#include "hw.h"
#include "bzone_math.h"
#include "bzone_game.h"

/* ---- Mathbox register offsets from $1860 (MBUDOC.DOC names; see
 * NOTES_math.md's choreography table).  Private to this module - not part
 * of any other module's contract. */
#define MB_ACOS     0x00  /* A low  */
#define MB_ACOS1    0x01  /* A high */
#define MB_BSIN     0x02  /* B low  */
#define MB_BSIN1    0x03  /* B high */
#define MB_TPOSXT   0x04  /* E low  */
#define MB_TPOSXT1  0x05  /* E high */
#define MB_TPOSYT   0x06  /* F low  */
#define MB_TPOSYT1  0x07  /* F high */
#define MB_XMATRX   0x08  /* X low  */
#define MB_XMATRX1  0x09  /* X high */
#define MB_YMATRX   0x0A  /* Y low  */
#define MB_YSTRT1   0x0B  /* Y high; write starts YHSM (rotate multiply) */
#define MB_ZLOW     0x0D  /* Z dividend byte 0 (lowest) */
#define MB_ZHI      0x0E  /* Z dividend byte 1 */
#define MB_DIVOUT   0x0F  /* Z dividend byte 2 */
#define MB_DIVOUT1  0x10  /* Z dividend byte 3 (highest) */
#define MB_YSTRT2   0x11  /* Y high (2nd); write starts YHSP ("whole mess") */
#define MB_RSTRT1   0x12  /* any write starts SYM (Y' multiply) */
#define MB_ZDIV     0x14  /* any write starts SZXD (Z/X' divide) */
#define MB_XPL      0x15  /* X' low  */
#define MB_XPH      0x16  /* X' high */

/* ---- ROM tables this module reads (cpu_rd, never hand-transcribed) ------
 * SIN1_99 ($5E77, program ROM): the only self-relative-addressed table in
 * the whole program (CALLING_NOTES.md section 3) - ported by real address,
 * since Atari's source names it only by a local label.
 * PTBLX1/PTBLY1 (program ROM): parallel to PTBLO1 (which DOES have a
 * state_defs.h alias, A_PTBLO1); these two don't, so their real addresses
 * are given here directly, read off the listing. */
#define BZ_SIN1_99   0x5E77
#define BZ_PTBLX1    0x7681
#define BZ_PTBLY1    0x76AB

/* Arithmetic below uses whole coordinates and fixed-point values. */
static trig_sample interpolate_trig(uint16_t endpoint);
/* Quadrant functions are public for direct ROM comparison coverage. */

/*------------------------------------------------------------------------
 * WAIT ($5B80): "WAIT FOR 2901 MATH BOX & RETURN. LOW BYTE IN A."
 * The real ROM polls the Mathbox status until it is idle; the Mathbox seam
 * model runs each write's microcode to completion, so status is never busy
 * - read it once, do not spin (hw.h / PLAN.md).
 *----------------------------------------------------------------------*/
uint8_t wait(void)
{
    CYC(16);
    (void)hw_mb_status();
    return hw_mb_ylow();
}

/*------------------------------------------------------------------------
 * ROTPNT ($5B1F): rotate one point through the Mathbox (already primed by
 * the caller via ACOS/BSIN/XMATRX/YMATRX/YSTRT1), keep it if it passes the
 * view-frustum clip.  ENTRY y = destination index.  EXIT: new y.
 *----------------------------------------------------------------------*/
uint8_t rotpnt(uint8_t y)
{
    uint16_t depth, lateral, magnitude;
    uint8_t low, high;
    CYC(6);
    low = wait();
    X2 = (uint8_t)(low * 2u);
    high = hw_mb_yhigh();
    hw_mb_write(MB_RSTRT1, high);
    CYC(15);
    if (high >= 128) { CYC(7); return y; }
    depth = (uint16_t)((low + 256u * high) * 2u);
    CYC(4);
    if (depth >= 32768u) { CYC(7); return y; }
    CYC(9);
    X2_AT(1) = (uint8_t)(depth / 256u);
    if (depth < 1024u) { CYC(7); return y; }
    CYC(7);
    if (depth >= 0x7B00u) { CYC(7); return y; }
    CYC(6);
    low = wait();
    Y2 = (uint8_t)(low * 2u);
    lateral = (uint16_t)((low + 256u * hw_mb_yhigh()) * 2u);
    Y2_AT(1) = (uint8_t)(lateral / 256u);
    CYC(14);
    CYC((int16_t)lateral < 0 ? 29 : 21);
    magnitude = (int16_t)lateral < 0 ? (uint16_t)(-(int16_t)lateral) : lateral;
    Y1 = (uint8_t)magnitude; Y1_AT(1) = (uint8_t)(magnitude / 256u);
    CYC(14);
    if (magnitude >= depth) { CYC(7); return y; }
    CYC(42);
    PTBLX2_AT(y) = X2; PTBLX2_AT(y + 1) = X2_AT(1);
    PTBLY2_AT(y) = Y2; PTBLY2_AT(y + 1) = Y2_AT(1);
    return (uint8_t)(y + 2);
}

/*------------------------------------------------------------------------
 * ROTATE ($5946): scan playfield/tank/robot/shell/saucer tables, translate
 * each centre point by the tank's own position (TPOSX/TPOSY -> TPOSXT/
 * TPOSYT, the Mathbox E/F operands) and rotate via ROTPNT, keeping only
 * objects in view.  No inputs; fills PTBLX2/PTBLY2/PTBLO2.
 * Atari's header: "SUBROUTINES: ROTPNT,EXPLDE" - both real, both called.
 *----------------------------------------------------------------------*/
/* Append the tank's radar separately from its body and treads. */
static uint8_t append_radar(uint8_t destination)
{
    int8_t whole;
    CYC(32);
    PTBLO2_AT(destination) = 0x0D;
    PTBLO2_AT(destination + 1) = RANGLE;
    NTHETA = (uint8_t)(TANGLE - TANGLE_AT(2));
    whole = trig_high(cos_(NTHETA));
    {
        int offset = -(int)whole;
        uint16_t center = (uint16_t)(PTBLX2 + 256u * PTBLX2_AT(1));
        uint16_t antenna = (uint16_t)(center + offset * 2);
        CYC(8 + (whole < 0 ? 2 : 1));
        CYC(6 + (whole == 0 ? 2 : 1) + 37 + 9);
        PTBLX2_AT(destination) = (uint8_t)antenna;
        PTBLX2_AT(destination + 1) = (uint8_t)(antenna / 256u);
    }
    whole = trig_high(sin_(NTHETA));
    {
        /* This projection drops the fraction before doubling. */
        int offset = 2 * whole;
        uint16_t center = (uint16_t)(PTBLY2 + 256u * PTBLY2_AT(1));
        uint16_t antenna = (uint16_t)(center + offset);
        CYC(8 + (whole < 0 ? 2 : 1) + 22 + 4);
        PTBLY2_AT(destination) = (uint8_t)antenna;
        PTBLY2_AT(destination + 1) = (uint8_t)(antenna / 256u);
    }
    destination = (uint8_t)(destination + 2);

    return destination;
}

/* Render the enemy body, then the visible tread/exhaust detail. An
 * exploding or clipped enemy has no additional detail records. */
static uint8_t append_enemy(uint8_t destination)
{
    uint8_t picture;
    CYC(5);
    if (COLFLG_AT(2)) {
        CYC(6);
        destination = explde(destination);
        CYC(3);
        return destination;
    }
    CYC(1 + 14);
    PTBLO2_AT(1) = TANGLE_AT(2);
    picture = 0x16;
    if (!(R2D3FL & 0x80)) {
        CYC(8);
        picture = 0x21;
        if (!tr7chk()) { CYC(4); picture = 2; }
        else CYC(3);
        CYC(2);
    } else CYC(1);

    CYC(38);
    PTBLO2 = picture;
    hw_mb_write(MB_XMATRX, TPOSX_AT(2));
    hw_mb_write(MB_XMATRX1, TPOSX_AT(3));
    hw_mb_write(MB_YMATRX, TPOSY_AT(2));
    hw_mb_write(MB_YSTRT1, TPOSY_AT(3));
    destination = rotpnt(destination);
    CYC(4);
    if (!destination) { CYC(4); return destination; }

    CYC(50);
    PTBLO2_AT(destination + 1) = PTBLO2_AT(1);
    PTBLX2_AT(destination) = PTBLX2;
    PTBLX2_AT(destination + 1) = PTBLX2_AT(1);
    PTBLY2_AT(destination) = PTBLY2;
    PTBLY2_AT(destination + 1) = PTBLY2_AT(1);

    if (R2D3FL & 0x80) {
        CYC(12);
        picture = (uint8_t)(0x24 + (TRDCTR_AT(2) & 7));
    } else {
        uint8_t view_heading;
        int rear_view;
        CYC(1 + 8);
        if (tr7chk()) { CYC(1); return destination; }
        view_heading = (uint8_t)(PTBLO2_AT(1) - TANGLE + 64);
        rear_view = view_heading >= 128;
        CYC(19 + 13 + (rear_view ? 2 : 1));
        TEMP1 = rear_view ? 8 : 4;
        picture = (uint8_t)(TEMP1 + (TRDCTR_AT(2) & 3));
    }

    CYC(14);
    PTBLO2_AT(destination) = picture;
    destination = (uint8_t)(destination + 2);
    if (R2D3FL & 0x80) { CYC(1); return destination; }
    return append_radar(destination);
}

static uint8_t append_shell(unsigned owner, uint8_t destination)
{
    uint8_t explosion_phase;
    CYC(6);
    if (!FIRECT_AT(owner)) { CYC(1); return destination; }

    CYC(16 + 6);
    PTBLO2_AT(destination + 1) = TANGLE_AT(owner);
    PTBLO2_AT(destination) = 3;
    if (FIRECT_AT(owner) & 0x80) {
        CYC(6);
        explosion_phase = (uint8_t)(FIRECT_AT(owner) + 15);
        if (explosion_phase < 128) {
            CYC(9);
            FIRECT_AT(owner) = 0;
            return destination;
        }
        CYC(1 + 24);
        FIRECT_AT(owner) = explosion_phase;
        PTBLO2_AT(destination + 1) = (uint8_t)(PTBLX2_AT(destination + 1) - explosion_phase - 1);
        PTBLO2_AT(destination) = 0x0E;
    } else CYC(1);

    CYC(38);
    hw_mb_write(MB_XMATRX, SHELLX_AT(owner));
    hw_mb_write(MB_XMATRX1, SHELLX_AT(owner + 1));
    hw_mb_write(MB_YMATRX, SHELLY_AT(owner));
    hw_mb_write(MB_YSTRT1, SHELLY_AT(owner + 1));
    return rotpnt(destination);
}

static uint8_t append_saucer(uint8_t destination)
{
    CYC(5);
    if (!SAUCER) { CYC(1); return destination; }
    CYC(49);
    PTBLO2_AT(destination) = 0x20;
    PTBLO2_AT(destination + 1) = SCANGL;
    hw_mb_write(MB_XMATRX, SAPOSX);
    hw_mb_write(MB_XMATRX1, SAPOSX_AT(1));
    hw_mb_write(MB_YMATRX, SAPOSY);
    hw_mb_write(MB_YSTRT1, SAPOSY_AT(1));
    destination = rotpnt(destination);
    CYC(8);
    if (PTBLO2_AT(destination - 2) == 0x20) {
        CYC(8);
        SAUCER = 0x81;
    } else {
        CYC(6);
        SAUCER = 1;
    }
    return destination;
}

void rotate(void)
{
    uint8_t destination = 0, picture = 0, table_offset;
    trig_sample coefficient;
    CYC(48);
    hw_mb_write(MB_TPOSXT, TPOSX);
    hw_mb_write(MB_TPOSXT1, TPOSX_AT(1));
    hw_mb_write(MB_TPOSYT, TPOSY);
    hw_mb_write(MB_TPOSYT1, TPOSY_AT(1));
    coefficient = sinxtr((uint8_t)(LANGLE * 4u + LANGLE / 128u), TANGLE);
    {
        /* Preserve the interpolator's one-unit wrap bias. */
        uint16_t sine = (uint16_t)(-(int)(uint16_t)coefficient.value + coefficient.wrapped);
        hw_mb_write(MB_BSIN, (uint8_t)sine);
        hw_mb_write(MB_BSIN1, (uint8_t)(sine / 256u));
    }
    CYC(30);
    coefficient = cosxtr(GARBAG, TANGLE);
    hw_mb_write(MB_ACOS, trig_low(coefficient));
    hw_mb_write(MB_ACOS1, (uint8_t)trig_high(coefficient));
    CYC(14);

    if (LOGO & 0x80) {
        CYC(5);
    } else {
        int owner;
        CYC(1);
        destination = append_enemy(destination);
        CYC(2);
        for (owner = 2; owner >= 0; owner -= 2) {
            destination = append_shell((unsigned)owner, destination);
            CYC(owner ? 7 : 6);
        }
        destination = append_saucer(destination);
        CYC(2);
    }

    /* The scenery table is terminated by a negative picture number. */
    table_offset = 0;
    do {
        CYC(6);
        picture = cpu_rd((uint16_t)(A_PTBLO1 + table_offset));
        if (picture & 0x80) { CYC(1); break; }
        CYC(52);
        PTBLO2_AT(destination) = picture;
        PTBLO2_AT(destination + 1) = cpu_rd((uint16_t)(A_PTBLO1 + 1 + table_offset));
        hw_mb_write(MB_XMATRX, cpu_rd((uint16_t)(BZ_PTBLX1 + table_offset)));
        hw_mb_write(MB_XMATRX1, cpu_rd((uint16_t)(BZ_PTBLX1 + 1 + table_offset)));
        hw_mb_write(MB_YMATRX, cpu_rd((uint16_t)(BZ_PTBLY1 + table_offset)));
        hw_mb_write(MB_YSTRT1, cpu_rd((uint16_t)(BZ_PTBLY1 + 1 + table_offset)));
        destination = rotpnt(destination);
        table_offset = (uint8_t)(table_offset + 2);
        CYC(table_offset ? 7 : 6);
    } while (table_offset);
    CYC(11);
    PTBLO2_AT(destination) = picture;
}

/*------------------------------------------------------------------------
 * PNTPUT ($5D4A): project one object's 3-D point table into PNTTBL.
 * ENTRY x = index into PTBLO2/PTBLX2/PTBLY2.
 *----------------------------------------------------------------------*/
void pntput(uint8_t object_index)
{
    uint8_t output_offset = 0;
    uint8_t source_offset = 1;
    uint16_t point_table;
    trig_sample direction;

    CYC(23);
    SAVE1 = object_index;
    NTHETA = (uint8_t)((TANGLE - PTBLO2_AT((unsigned)object_index + 1)) ^ 0x80);
    direction = sin_(NTHETA);
    CYC(17);
    hw_mb_write(MB_BSIN, trig_low(direction));
    hw_mb_write(MB_BSIN1, (uint8_t)trig_high(direction));
    direction = cos_(NTHETA);
    CYC(100);
    {
        uint16_t cosine = (uint16_t)(-(int)(uint16_t)direction.value);
        unsigned table_entry = PTBLO2_AT(object_index) * 2u;
        hw_mb_write(MB_ACOS, (uint8_t)cosine);
        hw_mb_write(MB_ACOS1, (uint8_t)(cosine / 256u));
        TBLPTR = cpu_rd((uint16_t)(A_OBJPNT + table_entry));
        TBLPTR_AT(1) = cpu_rd((uint16_t)(A_OBJPNT + table_entry + 1));
    }
    hw_mb_write(MB_TPOSXT, PTBLX2_AT(object_index));
    hw_mb_write(MB_TPOSXT1, PTBLX2_AT((unsigned)object_index + 1));
    DQUE = (uint8_t)(PTBLX2_AT((unsigned)object_index + 1) & 0xF0);
    hw_mb_write(MB_TPOSYT, PTBLY2_AT(object_index));
    hw_mb_write(MB_TPOSYT1, PTBLY2_AT((unsigned)object_index + 1));
    hw_mb_write(MB_ZLOW, 0);
    hw_mb_write(MB_ZHI, 0);
    SAVE3 = source_offset;
    point_table = (uint16_t)(TBLPTR + 256u * TBLPTR_AT(1));

    /* Each source record contains three little-endian coordinates. The
     * output contains the two projected coordinates. SAVE2/SAVE3 remain
     * observable scratch outputs, not simulated register saves. */
    do {
        uint8_t low;
        uint16_t projected;
        CYC(53);
        hw_mb_write(MB_XMATRX, cpu_rd((uint16_t)(point_table + source_offset)));
        hw_mb_write(MB_XMATRX1, cpu_rd((uint16_t)(point_table + (uint8_t)(source_offset + 1))));
        hw_mb_write(MB_YMATRX, cpu_rd((uint16_t)(point_table + (uint8_t)(source_offset + 2))));
        hw_mb_write(MB_YSTRT2, cpu_rd((uint16_t)(point_table + (uint8_t)(source_offset + 3))));
        low = wait();
        projected = (uint16_t)(-(int)(low + 256u * hw_mb_yhigh()));
        PNTTBL_AT(output_offset) = (uint8_t)projected;
        PNTTBL_AT((unsigned)output_offset + 1) = (uint8_t)(projected / 256u);

        CYC(54);
        DDEND = cpu_rd((uint16_t)(point_table + (uint8_t)(source_offset + 4)));
        low = cpu_rd((uint16_t)(point_table + (uint8_t)(source_offset + 5)));
        source_offset = (uint8_t)(source_offset + 6);
        SAVE3 = source_offset;
        DDEND_AT(1) = low;
        SAVE2 = output_offset;
        kludge();
        CYC(6);
        PNTTBL_AT((unsigned)output_offset + 2) = wait();
        PNTTBL_AT((unsigned)output_offset + 3) = hw_mb_yhigh();

        CYC(26);
        if (source_offset >= cpu_rd(point_table)) { CYC(7); break; }
        output_offset = (uint8_t)(output_offset + 4);
        CYC(10);
        if (!output_offset) { CYC(6); break; }
        CYC(1);
    } while (output_offset);
}

/*------------------------------------------------------------------------
 * KLUDGE ($5E05): explosion-piece Z offset + BOUNCE, then start the Z/X'
 * divide (SZXD).  No inputs (reads SAVE1/PTBLO2/DDEND/EXPOSZ/BOUNCE).
 *----------------------------------------------------------------------*/
void kludge(void)
{
    uint8_t object = PTBLO2_AT(SAVE1);
    uint16_t height = (uint16_t)(DDEND + 256u * DDEND_AT(1));
    uint16_t projected;
    CYC(11);
    CYC((object & 0x10) ? 30 : 1);
    CYC(5);
    CYC(BOUNCE ? 20 : 11);
    CYC(14);
    if (object & 0x10) {
        unsigned piece = (object & 7u) * 2u;
        height = (uint16_t)(height + EXPOSZ_AT(piece) + 256u * EXPOSZ_AT(piece + 1));
        DDEND = (uint8_t)height;
        DDEND_AT(1) = (uint8_t)(height / 256u);
    }
    projected = (uint16_t)(height - BOUNCE);
    hw_mb_write(MB_DIVOUT, (uint8_t)projected);
    hw_mb_write(MB_DIVOUT1, (uint8_t)(projected / 256u));
    hw_mb_write(MB_ZDIV, (uint8_t)(projected / 256u));
}

/*------------------------------------------------------------------------
 * BOUND ($5E44): halve BOUNCE toward zero.
 *----------------------------------------------------------------------*/
void bound(void)
{
    CYC(BOUNCE != 0 ? 16 : 12);
    if (BOUNCE != 0)
        BOUNCE = (uint8_t)(BOUNCE / 2);
}

/*------------------------------------------------------------------------
 * COS/SIN/SIN1 ($5E4B/$5E4E/$5E63)
 *----------------------------------------------------------------------*/
trig_sample cos_(uint8_t angle)
{
    CYC(4);
    return sin_((uint8_t)(angle + 64));
}

/* The second half-turn is the negated first half. */
trig_sample sin_(uint8_t angle)
{
    trig_sample sample;
    if (angle < 128) { CYC(3); return sin1(angle); }
    CYC(10);
    sample = sin1((uint8_t)(angle & 127));
    CYC(27);
    sample.wrapped = sample.value == 0;
    sample.value = (int16_t)(uint16_t)(-(int)(uint16_t)sample.value);
    return sample;
}

/* First half-turn, from the quarter-wave table (mirrored past $40). */
trig_sample sin1(uint8_t angle)
{
    trig_sample sample;
    unsigned entry;
    CYC(angle >= 0x41 ? 35 : 32);
    if (angle >= 0x41) angle = (uint8_t)(128 - angle);
    entry = (uint8_t)(angle * 2u);
    sample.value = (int16_t)(uint16_t)(cpu_rd((uint16_t)(BZ_SIN1_99 + entry)) +
        256u * cpu_rd((uint16_t)(BZ_SIN1_99 + 1 + entry)));
    sample.wrapped = angle >= 128;
    return sample;
}

/*------------------------------------------------------------------------
 * COSXTR/SINXTR/XTRAP ($5EF9/$5F17/$5F32)
 *----------------------------------------------------------------------*/
/* The half-step selector is a fixed-point angle fraction. The public
 * interfaces retain the original byte arguments for callers and tests. */
static trig_sample sample_trig(uint8_t fraction, uint8_t angle, int cosine)
{
    trig_sample start, endpoint;
    CYC(7);
    GARBAG = fraction & 1;
    if (!GARBAG) {
        CYC(5);
        return cosine ? cos_(angle) : sin_(angle);
    }
    CYC(1 + 11);
    TEMP3 = angle;
    start = cosine ? cos_(angle) : sin_(angle);
    CYC(20);
    TEMP1 = trig_low(start);
    TEMP1_AT(1) = (uint8_t)trig_high(start);
    TEMP3 = (uint8_t)(angle + 1);
    endpoint = cosine ? cos_(TEMP3) : sin_(TEMP3);
    if (cosine) CYC(3);
    return interpolate_trig((uint16_t)endpoint.value);
}

trig_sample cosxtr(uint8_t fraction, uint8_t angle)
{
    return sample_trig(fraction, angle, 1);
}

trig_sample sinxtr(uint8_t fraction, uint8_t angle)
{
    return sample_trig(fraction, angle, 0);
}

/* Interpolate halfway between neighboring trig samples.  Negative odd
 * differences round downward; TEMP2 retains the interpolation adjustment. */
static trig_sample interpolate_trig(uint16_t endpoint)
{
    trig_sample sample;
    uint16_t start = (uint16_t)(TEMP1 + 256u * TEMP1_AT(1));
    int difference = (int16_t)(uint16_t)(endpoint - start);
    int half = difference >= 0 ? difference / 2 : -((-difference + 1) / 2);
    uint16_t adjustment = (uint16_t)half;
    unsigned interpolated = start + (unsigned)adjustment;
    CYC(54);
    TEMP2 = (uint8_t)adjustment;
    TEMP2_AT(1) = (uint8_t)(adjustment / 256u);
    sample.value = (int16_t)(uint16_t)interpolated;
    sample.wrapped = interpolated > 65535u;
    return sample;
}

/*------------------------------------------------------------------------
 * ABSTMP ($6149): TEMP1/TEMP1+1 := |TEMP1/TEMP1+1|.
 *----------------------------------------------------------------------*/
void abstmp(void)
{
    int coordinate = (int16_t)(uint16_t)(TEMP1 + 256u * TEMP1_AT(1));
    CYC(5 + (coordinate < 0 ? 22 : 1) + 6);
    if (coordinate < 0) {
        uint16_t magnitude = (uint16_t)-coordinate;
        TEMP1 = (uint8_t)magnitude;
        TEMP1_AT(1) = (uint8_t)(magnitude / 256u);
    }
}

/*------------------------------------------------------------------------
 * TRACK ($6810): angle from the robot tank to the player tank.
 *----------------------------------------------------------------------*/
uint8_t track(void)
{
    uint8_t quadrant = 0;
    uint16_t difference;
    uint8_t difference_high;
    uint8_t angle;

    CYC(24);
    difference = (uint16_t)((TPOSX + 256u * TPOSX_AT(1)) -
                            (TPOSX_AT(2) + 256u * TPOSX_AT(3)));
    TEMP1 = (uint8_t)difference;
    difference_high = (uint8_t)(difference / 256u);
    TEMP1_AT(1) = difference_high;
    if ((int8_t)difference_high < 0) {
        CYC(6);
        abstmp();
        CYC(2);
        quadrant = (uint8_t)(quadrant + 1);
    } else CYC(1);
    CYC(37);


    TEMP2 = TEMP1;
    TEMP2_AT(1) = TEMP1_AT(1);


    difference = (uint16_t)((TPOSY + 256u * TPOSY_AT(1)) -
                            (TPOSY_AT(2) + 256u * TPOSY_AT(3)));
    TEMP1 = (uint8_t)difference;
    difference_high = (uint8_t)(difference / 256u);
    TEMP1_AT(1) = difference_high;
    if ((int8_t)difference_high < 0) {
        CYC(6);
        abstmp();
        CYC(quadrant == 0 ? 7 : 9);
        quadrant = (quadrant == 0) ? (uint8_t)3 : (uint8_t)(quadrant + 1);
    } else CYC(1);


    if (TEMP1 == TEMP2 && TEMP1_AT(1) == TEMP2_AT(1)) {
        CYC(22);
        angle = 0x20;
    } else {
    CYC(TEMP1 != TEMP2 ? 9 : 21);
    {
        int y_is_smaller = (TEMP1 + 256u * TEMP1_AT(1)) < (TEMP2 + 256u * TEMP2_AT(1));
        CYC(11 + (y_is_smaller ? 1 + 32 : 35) + 6);

        if (y_is_smaller) {

            hw_mb_write(MB_XPL, TEMP2);
            hw_mb_write(MB_XPH, TEMP2_AT(1));
            hw_mb_write(MB_DIVOUT, TEMP1);
            hw_mb_write(MB_DIVOUT1, TEMP1_AT(1));
            hw_mb_write(MB_ZDIV, TEMP1_AT(1));
        } else {

            hw_mb_write(MB_XPL, TEMP1);
            hw_mb_write(MB_XPH, TEMP1_AT(1));
            hw_mb_write(MB_DIVOUT, TEMP2);
            hw_mb_write(MB_DIVOUT1, TEMP2_AT(1));
            hw_mb_write(MB_ZDIV, TEMP2_AT(1));
        }


        {
            uint8_t low = wait();
            uint16_t ratio;
            TEMP3 = low;
            ratio = (uint16_t)(low + 256u * hw_mb_yhigh());
            TEMP3 = (uint8_t)(ratio / 4u);
        }
        angle = cpu_rd((uint16_t)(A_ARCTAN + TEMP3));

        CYC(34 + (y_is_smaller ? 1 : 8));
        if (!y_is_smaller) {
            TEMP3 = angle;
            angle = (uint8_t)(64 - TEMP3);
        }
    }
    }
    /* Apply the quadrant to the first-quadrant angle. */
    CYC(30);
    switch (quadrant) {
    case 0:  return t_0(angle);
    case 1:  return t_1(angle);
    case 2:  return t_2(angle);
    default: return t_3(angle);  /* quadrant == 3 */
    }
}

/*------------------------------------------------------------------------
 * T_0/T_1/T_2/T_3 ($68D5/$68C6/$68CB/$68D0): TTAB quadrant fix-up.
 *----------------------------------------------------------------------*/
uint8_t t_0(uint8_t angle)
{
    CYC(6);
    return angle;
}

uint8_t t_2(uint8_t angle)
{
    CYC(11);
    return (uint8_t)(angle + 128);
}

uint8_t t_1(uint8_t angle)
{
    CYC(17);
    return (uint8_t)(128 - angle);
}

uint8_t t_3(uint8_t angle)
{
    CYC(12);
    return (uint8_t)-angle;
}
