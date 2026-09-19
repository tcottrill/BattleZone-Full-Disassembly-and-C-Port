/* avg.c - Battlezone C port: the Analog Vector Generator display-list walker.
 *
 * Contract and semantics, with full evidence: avg.h, NOTES_avg.md.  Base:
 * C:\Source2026\Tempest Dissasembly\c_src\avg.c (colour AVG); the
 * differences are the monochrome Z-direct intensity rule, the STAT
 * clip-window tracking, and dropping avg_mem_g()/avg_run_frame() (no
 * state.h/vecrom.c to bind to yet - see avg.h's "ACCESSOR").
 */
#include <stddef.h>
#include <stdint.h>
#include "avg.h"

#define OP_VCTR 0
#define OP_HALT 1
#define OP_SVEC 2
#define OP_STAT 3
#define OP_CNTR 4
#define OP_JSRL 5
#define OP_RTSL 6
#define OP_JMPL 7

/* 13-bit two's complement delta (VGMC.MAC: .WORD DY&^H1FFF). */
static int s13(unsigned v)
{
    v &= 0x1FFFu;
    return (v & 0x1000u) ? (int)v - 0x2000 : (int)v;
}

/* 5-bit two's complement half delta of SVEC. */
static int s5(unsigned v)
{
    v &= 0x1Fu;
    return (v & 0x10u) ? (int)v - 0x20 : (int)v;
}

/* ---- draw time (avg.h TIMING; MAME's avgdvg state machine, copied from
 * Tempest's avg.c - avg_strobe0/avg_strobe1/avg_common_strobe3, unchanged by
 * AAE's avg_bzone; see NOTES_avg.md) ------------------------------------- */

/* Normalizer shift count (avg_strobe0): both 13-bit delta registers are
 * shifted up until either has its bit 12 differing from bit 11; dvx == dvy
 * == 0 cuts off at 16. */
static int norm_shifts(unsigned dvy, unsigned dvx)
{
    int i = 0;

    while (((dvy ^ (dvy << 1)) & 0x1000u) == 0 &&
           ((dvx ^ (dvx << 1)) & 0x1000u) == 0 && i < 16) {
        i++;
        dvy = (dvy & 0x1000u) | ((dvy << 1) & 0x1FFFu);
        dvx = (dvx & 0x1000u) | ((dvx << 1) & 0x1FFFu);
    }
    return i;
}

/* The timer shift register after nnorm normalizer shifts (avg_strobe0) and
 * nbin binary-scale shifts (avg_strobe1).  op1 = the SVEC path: each shift
 * also ORs $80 in and the register is masked to its low byte. */
static unsigned timer_reg(int nnorm, int nbin, int op1)
{
    unsigned t = 0;
    int      i;

    for (i = 0; i < nnorm; i++)
        t = (t >> 1) | (op1 ? 0x4080u : 0x4000u);
    if (op1)
        t &= 0xFFu;
    for (i = 0; i < nbin; i++)
        t = (t >> 1) | (op1 ? 0x4080u : 0x4000u);
    if (op1)
        t &= 0xFFu;
    return t;
}

#define TICKS(n) ((uint32_t)(n) * AVG_CYC_PER_CPU)

/* Word at CPU address a in the AVG map; 0 if outside $2000-$3FFF. */
static int fetch(const avg_mem *m, uint32_t a, unsigned *w)
{
    const uint8_t *p;

    if (a < AVG_VRAM_BASE || a + 1u >= AVG_SPACE_END)
        return 0;
    if (a < AVG_VROM_BASE) {
        if (a + 1u >= AVG_VROM_BASE) {         /* word straddles $2FFF/$3000 */
            *w = (unsigned)m->vram[a - AVG_VRAM_BASE]
               | ((unsigned)m->vrom[0] << 8);
            return 1;
        }
        p = m->vram + (a - AVG_VRAM_BASE);
        *w = (unsigned)p[0] | ((unsigned)p[1] << 8);
        return 1;
    }
    p = m->vrom + (a - AVG_VROM_BASE);
    *w = (unsigned)p[0] | ((unsigned)p[1] << 8);
    return 1;
}

static uint16_t target(unsigned w)
{
    return (uint16_t)(AVG_VRAM_BASE + ((w & 0x1FFFu) << 1));
}

int32_t avg_scal_q15(int bin, int lin)
{
    return (int32_t)((255 - (lin & 0xFF)) << (7 - (bin & 7)));
}

void avg_state_init(avg_state *st)
{
    int i;

    st->x = 0;
    st->y = 0;
    st->scale_q15 = AVG_Q15_ONE;      /* see avg.h "COORDINATES"             */
    st->bin_scale = 0;
    st->intensity = 0;
    for (i = 0; i < AVG_STACK_SLOTS; i++)
        st->stack[i] = 0;
    st->sp = 0;
    st->depth = 0;
    st->clip_xmin = st->clip_ymin = st->clip_xmax = st->clip_ymax = 0;
    st->clip_have_lo = 0;
    st->clip_have_hi = 0;
    st->clip_out = 0;
}

static int32_t clamp32(int64_t v)
{
    if (v > INT32_MAX) return INT32_MAX;
    if (v < INT32_MIN) return INT32_MIN;
    return (int32_t)v;
}

/* JSRL push on the 4-slot / 4-bit-counter stack (avg.h "STACK"). */
static void push(avg_state *st, avg_result *r, uint16_t ret)
{
    st->stack[st->sp & 3u] = ret;
    st->sp = (uint8_t)((st->sp + 1u) & 0xFu);
    st->depth++;
    if (st->depth > AVG_STACK_SLOTS)
        r->flags |= AVG_FLAG_STACK_OVER;
    if (st->depth > r->max_depth)
        r->max_depth = st->depth;
}

/* RTSL pop; returns 0 at depth 0 (the walk ends there). */
static int pop(avg_state *st, uint16_t *pc)
{
    if (st->depth <= 0)
        return 0;
    st->depth--;
    st->sp = (uint8_t)((st->sp - 1u) & 0xFu);
    *pc = st->stack[st->sp & 3u];
    return 1;
}

/* STAT, general form (word bit $0400 clear): arm one corner of the clip
 * window at the CURRENT beam position (avg.h "CLIP WINDOW"; VGMC.MAC's STAT
 * macro comment; AAE's bzone_strobe2/bzone_latch1). */
static void stat_window(avg_state *st, unsigned w)
{
    int hi = (int)((w >> 9) & 1u);
    int inn = (int)((w >> 8) & 1u);

    st->clip_out = inn;
    if (hi) {
        st->clip_xmax = clamp32(st->x);
        st->clip_ymax = clamp32(st->y);
        st->clip_have_hi = 1;
    } else {
        st->clip_xmin = clamp32(st->x);
        st->clip_ymin = clamp32(st->y);
        st->clip_have_lo = 1;
    }
}

static uint8_t clip_flags(const avg_state *st)
{
    uint8_t f = 0;
    if (st->clip_have_lo && st->clip_have_hi) {
        f |= AVG_CLIP_ARMED;
        if (st->clip_out)
            f |= AVG_CLIP_OUT;
    }
    return f;
}

avg_result avg_walk(const avg_mem *m, uint16_t start, avg_state *st_in,
                    const avg_cfg *cfg)
{
    avg_state   local;
    avg_state  *st = st_in;
    avg_result  r;
    uint32_t    budget = (cfg && cfg->max_ops) ? cfg->max_ops : AVG_FRAME_BUDGET;
    uint16_t    pc = start;

    r.stop = AVG_STOP_BUDGET;
    r.flags = 0;
    r.stop_pc = pc;
    r.ops = 0;
    r.cycles = 0;
    r.nseg = 0;
    r.nseg_stored = 0;
    r.nlit = 0;
    r.max_depth = 0;

    if (st == NULL) {
        avg_state_init(&local);
        st = &local;
    }

    for (;;) {
        unsigned w, w2 = 0;
        int      op, dx = 0, dy = 0, z = 0, is_svec = 0;
        uint16_t here = pc;

        if (r.ops >= budget) {
            r.stop = AVG_STOP_BUDGET;
            r.stop_pc = here;
            break;
        }
        if (!fetch(m, pc, &w)) {
            r.stop = AVG_STOP_BADADDR;
            r.stop_pc = here;
            break;
        }
        r.ops++;
        op = (int)(w >> 13);

        switch (op) {
        case OP_VCTR:
            if (!fetch(m, (uint32_t)pc + 2u, &w2)) {
                r.stop = AVG_STOP_BADADDR;
                r.stop_pc = here;
                return r;
            }
            dy = s13(w);
            dx = s13(w2);
            z = (int)((w2 >> 13) & 7u);
            pc = (uint16_t)(pc + 4u);
            r.cycles += TICKS(8) + (0x8000u - timer_reg(
                norm_shifts(w & 0x1FFFu, w2 & 0x1FFFu), st->bin_scale, 0));
            goto draw;

        case OP_SVEC:
            dx = s5(w) * 2;
            dy = s5(w >> 8) * 2;
            z = (int)((w >> 5) & 7u);
            is_svec = 1;
            pc = (uint16_t)(pc + 2u);
            r.cycles += TICKS(6) + (0x100u - (timer_reg(
                norm_shifts(((w >> 8) & 0x1Fu) << 8, (w & 0x1Fu) << 8),
                st->bin_scale, 1) & 0xFFu));
        draw: {
                avg_seg s;
                int64_t nx = st->x + (int64_t)dx * st->scale_q15;
                int64_t ny = st->y + (int64_t)dy * st->scale_q15;

                s.x0 = clamp32(st->x);
                s.y0 = clamp32(st->y);
                s.x1 = clamp32(nx);
                s.y1 = clamp32(ny);
                s.intensity = (uint8_t)z;      /* direct - avg.h "INTENSITY" */
                s.svec = (uint8_t)is_svec;
                s.clip = clip_flags(st);
                s.pc = here;
                st->x = nx;
                st->y = ny;

                r.nseg++;
                if (z > 0)
                    r.nlit++;
                if (cfg != NULL) {
                    if (cfg->segs != NULL && r.nseg_stored < cfg->seg_cap)
                        cfg->segs[r.nseg_stored++] = s;
                    if (cfg->seg != NULL)
                        cfg->seg(cfg->ctx, &s);
                }
            }
            continue;

        case OP_STAT:
            r.cycles += TICKS(7);
            if (w & 0x1000u) {                     /* SCAL */
                st->scale_q15 = avg_scal_q15((int)((w >> 8) & 0xFu), (int)(w & 0xFFu));
                st->bin_scale = (int)((w >> 8) & 7u);
            } else {
                st->intensity = (int)((w >> 4) & 0xFu);   /* tracked only     */
                if (!((w >> 10) & 1u))                     /* HI/IN given      */
                    stat_window(st, w);
            }
            pc = (uint16_t)(pc + 2u);
            continue;

        case OP_CNTR:
            r.cycles += TICKS(5) + (0x8000u - timer_reg(norm_shifts(w & 0xFFu, 0), 0, 0));
            st->x = 0;
            st->y = 0;
            pc = (uint16_t)(pc + 2u);
            continue;

        case OP_JSRL:
            r.cycles += TICKS(5);
            push(st, &r, (uint16_t)(pc + 2u));
            pc = target(w);
            continue;

        case OP_RTSL:
            r.cycles += TICKS(4);
            if (!pop(st, &pc)) {
                r.stop = AVG_STOP_RTSL;
                r.stop_pc = here;
                return r;
            }
            continue;

        case OP_JMPL: {
            uint16_t t = target(w);
            r.cycles += TICKS(3);
            if (t == start && st->depth == 0) {
                r.stop = AVG_STOP_LOOP;
                r.stop_pc = here;
                return r;
            }
            pc = t;
            continue;
        }

        case OP_HALT:
        default:
            r.cycles += TICKS(2);
            r.stop = AVG_STOP_HALT;
            r.stop_pc = here;
            return r;
        }
    }
    return r;
}

const char *avg_stop_name(avg_stop s)
{
    switch (s) {
    case AVG_STOP_HALT:    return "HALT";
    case AVG_STOP_LOOP:    return "LOOP";
    case AVG_STOP_RTSL:    return "RTSL";
    case AVG_STOP_BUDGET:  return "BUDGET";
    case AVG_STOP_BADADDR: return "BADADDR";
    }
    return "?";
}
