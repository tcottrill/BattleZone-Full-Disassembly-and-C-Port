/* mathbox.c - Math Box microcode interpreter.  See mathbox.h.
 *
 * Microcode word (24 bits; MBUCOD.V05 $OUT macro, nibble order checked by
 * tools/gen_roms.py):
 *   23-20 A    register A / jump target high nibble
 *   19-16 B    register B / jump target low nibble
 *   15    I2HI source-select I2 for the high byte slices
 *   14    I2LO source-select I2 for the low byte slices
 *   13-12 I1,I0
 *   11    STALL stop the clock after this instruction
 *   10-8  I5-I3 ALU function
 *   7     LDAB  jump latch := A,B fields
 *   6-4   I8-I6 destination
 *   3     SIGN  MSB* = OVR xor F15 (else 0)
 *   2     JMP   jump to the latch if MSB* = 0
 *   1     MULT  invert I1 if the latched Q0 was 0 (CADD)
 *   0     CARIN
 *
 * Am2901 semantics (standard datasheet tables):
 *   source I2I1I0: 0 A,Q  1 A,B  2 0,Q  3 0,B  4 0,A  5 D,A  6 D,Q  7 D,0
 *   function I5-3: 0 R+S  1 S-R  2 R-S  3 R|S  4 R&S  5 ~R&S  6 R^S  7 ~(R^S)
 *   dest     I8-6: 0 QREG 1 NOP 2 RAMA 3 RAMF 4 RAMQD 5 RAMD 6 RAMQU 7 RAMU
 * D bus = the 6502 data byte on both halves (D7 tied to D15, MBUDOC).
 * Carry ripples slice to slice (four 4-bit slices); OVR is the top slice's.
 *
 * Documentation gaps, chosen and flagged rather than guessed silently:
 *   - C4/OVR of EXOR/EXNOR: the datasheet's expressions are not in MBUDOC;
 *     modelled as 0 and every SIGN use after them is counted (xor_sign).
 *   - Q0 latch when Q0 is not driven (dest 0-3): holds its previous value.
 *     The microcode only CADDs after RAMQD, where Q0 is driven.
 *   - RAMU shifts in 0 ("garbage from floating input", MBUDOC).
 */
#include "mathbox.h"
#include "mbprom.h"

#define MB_STEP_CAP 4096u

void mb_reset(mathbox *m)
{
    for (int i = 0; i < 16; i++) m->r[i] = 0;
    m->q = 0; m->y = 0; m->jt = 0; m->q0 = 1;
    m->starts = m->steps = m->runaway = m->xor_sign = 0;
}

static void pick(int src, uint16_t a, uint16_t b, uint16_t q, uint16_t d,
                 uint16_t *r, uint16_t *s)
{
    switch (src & 7) {
    case 0: *r = a; *s = q; break;
    case 1: *r = a; *s = b; break;
    case 2: *r = 0; *s = q; break;
    case 3: *r = 0; *s = b; break;
    case 4: *r = 0; *s = a; break;
    case 5: *r = d; *s = a; break;
    case 6: *r = d; *s = q; break;
    default: *r = d; *s = 0; break;
    }
}

/* One microinstruction; returns 1 when it carried STALL. */
static int mb_step(mathbox *m, uint16_t d, uint8_t *upc)
{
    uint32_t w = mb_ucode[*upc];
    int ra_i = (int)((w >> 20) & 15), rb_i = (int)((w >> 16) & 15);
    int i2hi = (int)((w >> 15) & 1), i2lo = (int)((w >> 14) & 1);
    int i10 = (int)((w >> 12) & 3);
    int stall = (int)((w >> 11) & 1), func = (int)((w >> 8) & 7);
    int ldab = (int)((w >> 7) & 1), dest = (int)((w >> 4) & 7);
    int sign = (int)((w >> 3) & 1), jmp = (int)((w >> 2) & 1);
    int mult = (int)((w >> 1) & 1);
    unsigned c = (unsigned)(w & 1);
    uint16_t av = m->r[ra_i], bv = m->r[rb_i];
    uint16_t rh, sh, rl, sl, R, S, F = 0;
    unsigned ovr = 0;

    if (mult && m->q0 == 0) i10 ^= 2;                 /* CADD: ADD n,m -> ADD 0,m */
    pick((i2hi << 2) | i10, av, bv, m->q, d, &rh, &sh);
    pick((i2lo << 2) | i10, av, bv, m->q, d, &rl, &sl);
    R = (uint16_t)((rh & 0xFF00) | (rl & 0x00FF));
    S = (uint16_t)((sh & 0xFF00) | (sl & 0x00FF));

    for (int k = 0; k < 4; k++) {                      /* four 2901 slices */
        unsigned r4 = (R >> (4 * k)) & 15u, s4 = (S >> (4 * k)) & 15u, f4, c4;
        switch (func) {
        case 0: case 1: case 2: {
            unsigned rr = (func == 1) ? (~r4 & 15u) : r4;
            unsigned ss = (func == 2) ? (~s4 & 15u) : s4;
            unsigned sum = rr + ss + c;
            unsigned c3 = ((rr & 7u) + (ss & 7u) + c) >> 3;
            f4 = sum & 15u; c4 = sum >> 4; ovr = c3 ^ c4;
            break;
        }
        case 3: f4 = r4 | s4; c4 = (f4 != 15u) | c; ovr = c4; break;
        case 4: f4 = r4 & s4; c4 = (f4 != 0u) | c; ovr = c4; break;
        case 5: f4 = ~r4 & s4 & 15u; c4 = (f4 != 0u) | c; ovr = c4; break;
        case 6: f4 = r4 ^ s4; c4 = 0; ovr = 0; break;
        default: f4 = ~(r4 ^ s4) & 15u; c4 = 0; ovr = 0; break;
        }
        F = (uint16_t)(F | (f4 << (4 * k)));
        c = c4;
    }
    unsigned f15 = (F >> 15) & 1u;
    unsigned msb = sign ? (ovr ^ f15) : 0u;
    if (sign && func >= 6 && (jmp || dest == 4 || dest == 5)) m->xor_sign++;

    m->y = F;
    switch (dest) {
    case 0: m->q = F; break;
    case 1: break;
    case 2: m->r[rb_i] = F; m->y = av; break;
    case 3: m->r[rb_i] = F; break;
    case 4:
        m->r[rb_i] = (uint16_t)((F >> 1) | (msb << 15));
        m->q0 = (uint8_t)(m->q & 1u);
        m->q = (uint16_t)((m->q >> 1) | ((F & 1u) << 15));
        break;
    case 5:
        m->r[rb_i] = (uint16_t)((F >> 1) | (msb << 15));
        m->q0 = (uint8_t)(m->q & 1u);
        break;
    case 6:
        m->r[rb_i] = (uint16_t)((F << 1) | (m->q >> 15));
        m->q = (uint16_t)(m->q << 1);
        m->q0 = 0;
        break;
    default:
        m->r[rb_i] = (uint16_t)(F << 1);
        m->q0 = 0;
        break;
    }

    if (ldab) m->jt = (uint8_t)((w >> 16) & 0xFF);
    if (jmp && msb == 0) *upc = m->jt;
    else *upc = (uint8_t)(*upc + 1);
    m->steps++;
    return stall;
}

void mb_write(mathbox *m, uint8_t offset, uint8_t data)
{
    uint8_t upc = mb_map[offset & 0x1F];
    uint16_t d = (uint16_t)(data | (data << 8));
    unsigned n;
    m->starts++;
    for (n = 0; n < MB_STEP_CAP; n++)
        if (mb_step(m, d, &upc)) break;
    if (n == MB_STEP_CAP) m->runaway++;
}

uint8_t mb_status(const mathbox *m) { (void)m; return 0x00; }
uint8_t mb_ylow(const mathbox *m)   { return (uint8_t)(m->y & 0xFF); }
uint8_t mb_yhigh(const mathbox *m)  { return (uint8_t)(m->y >> 8); }
