/* emu_main.cpp - bz_emu: the emulator seam (DESIGN.md).
 *
 * The emulator TWIN of c_src\app_loop.c.  app_loop.c is the game seam of the
 * Battlezone C port: it implements hw.h for the translated modules and the
 * bz_app_* hooks for a backend.  This file implements the same bz_app_* hooks
 * for the same, unchanged Windows backend (c_src\platform\windows\plat_win.c)
 * - but what runs behind them is the real program ROM on AAE's cpu_6502 core
 * (cpu\, vendored; the ONLY piece taken from AAE), inside the board model the
 * C port proved (EMULATOR_PORT_NOTES.md, app_loop.c, tests\refrun.c), built
 * from the C port's own chip models, compiled in place from ..\c_src:
 * c012294.c (POKEY), mathbox.c (the microcode PROMs), avg.c (the display-list
 * walker with MAME's cycle-true timing), bz_sounds.c (the SOUND latch as
 * recorded samples), state.c (the machine state g: RAM and vector RAM).  The
 * ROM images come from emu_roms.c.
 *
 * Order of authority: the disassembly (disasm\bzone_program_rom.asm, Atari's
 * names) and c_src (app_loop.c, refrun.c, EMULATOR_PORT_NOTES.md) first; MAME
 * to settle doubts; the AAE driver (..\bzone.cpp) only as a cross-reference.
 *
 * The board (EMULATOR_PORT_NOTES.md section 1):
 *  - 6502 at 1.512 MHz; 15-bit address decode: $8000-$FFFF mirrors
 *    $0000-$7FFF (the core's address mask $7FFF - the vectors at $FFFA come
 *    from $7FFA);
 *  - RAM $0000-$03FF -> g.ram; vector RAM $2000-$2FFF -> g.vram; vector ROM
 *    $3000-$3FFF; program ROM $5000-$7FFF;
 *  - IN0 $0800: b0/b1 coins, b3 slam, b4 self test, b5 diag step (all active
 *    low), b6 VG HALT, b7 3 kHz = (cycle >> 8) & 1;
 *  - OPTION $0A00, OPTON2 $0C00 (the DIPs); EMCNT $1000 (coin counters);
 *    GOADD $1200; WTCHDG $1400; VSTOP $1600; SOUND $1840;
 *  - Mathbox: status $1800, result lo/hi $1810/$1818, writes $1860-$187F;
 *  - POKEY $1820-$182F on c012294.c, caught up to the exact bus cycle of every
 *    access (the core's cycle callback), ALLPOT = the tread levers, fire and
 *    start (active high);
 *  - NMI: every 6144 cycles (3 kHz / 12 = 246.09 Hz), edge-triggered, gated
 *    by the self-test switch (MAME bzone_interrupt: only while IN0 b4 is high,
 *    i.e. the switch is OFF);
 *  - the watchdog: 1,134,000 cycles without a WTCHDG strobe pulls RESET (the
 *    self test relies on it: STEST4_10 "IF NO HALT, WTCHDGDOG WILL RESET").
 *
 * The AVG (EMULATOR_PORT_NOTES.md section 2, the traps):
 *  1. GOADD always starts the AVG at $2000 (VECRAM) - the data byte is
 *     ignored; the game's VECRAM holds a JMPL to the live buffer;
 *  2. the scale register persists across GOADDs (the self test's DIATST
 *     patterns set none);
 *  3. the tube is +-256 x +-192 (bz_platform.h BZ_VIEW_*; the backend clips);
 *  4. a list that never HALTs keeps the AVG busy until VSTOP;
 *  5. segments are clipped to the AVG clip window (BIGWND, STAT HI/IN);
 *  6. the overlay gel: red above y = 110, green below, a crossing segment is
 *     split at the edge.
 * The picture is walked and presented at the GOADD write (app_loop.c
 * hw_goadd); the busy time is the list's counted draw time (avg.c, MAME's
 * state-PROM timing) + one lead-in tick.
 *
 * STEP GRANULARITY.  bz_app_step() is one MAIN pass, as in app_loop.c: from
 * one fall-through of MAIN's frame wait (L5004 JSR VOLCNO, after "L5000 LSR
 * SYNC / L5002 BCC MAIN") to the next, ~15.4 per second.  With no MAIN pass
 * (the self test) a step ends after 32 NMI periods, ~7.7 per second.  Inside
 * the step the machine is paced against plat_now_ms() once per NMI period.
 *
 * Command line: plat_win.c looks only for --test.  --roms FILE is taken out
 * of __argv by a static initializer before main / WinMain runs, so a harness
 * with its own parser (c_src\tests\bz_smoke.c) never meets it either.
 */
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#pragma warning(push, 3)
#include "cpu/cpu_6502.h"          /* the vendored AAE core (C++) */
#pragma warning(pop)
#undef LOG_INFO                    /* sys_log.h's; the backend's log.h defines the same names */
#undef LOG_ERROR
#undef LOG_DEBUG

#include "c012294.h"               /* has its own extern "C" guard */
extern "C" {
#include "mathbox.h"
#include "avg.h"
#include "progrom.h"               /* extern const: the storage is emu_roms.c's */
#include "vecrom.h"
#include "bz_sounds.h"
#include "platform/bz_platform.h"
#ifndef EMU_HEADLESS
#include "platform/windows/ini.h"
#include "platform/windows/log.h"
#endif
#include "state.h"                 /* g + state_defs.h; last: it defines many short macros */
}
#include "emu_roms.h"

#ifdef EMU_HEADLESS
#define EMU_LOG(...)  (fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))
#define EMU_WARN(...) EMU_LOG(__VA_ARGS__)
#else
#define EMU_LOG(...)  LOG_INFO(__VA_ARGS__)
#define EMU_WARN(...) LOG_WARN(__VA_ARGS__)
#endif

/* ------------------------------------------------------------------ */
/* constants (app_loop.c's, same names)                                */
/* ------------------------------------------------------------------ */

#define BZ_CPU_HZ        1512000.0              /* 12.096 MHz / 8: CPU = POKEY clock */
#define BZ_NMI_CYCLES    6144u                  /* 3 kHz / 12 = 246.09 Hz            */
#define BZ_AUDIO_RATE    44100
#define BZ_STALL_MS      100.0                  /* catch-up clamp                    */
#define BZ_WD_CYCLES     1134000u               /* hardware watchdog: 0.75 s         */
#define BZ_STEP_CAP_NMIS 32u                    /* a step with no MAIN pass: 32 NMI periods (a pass is 16) */

/* the addresses (disasm\bzone_program_rom.asm / hw.h) */
#define A_IN0_       0x0800u
#define A_OPTION_    0x0A00u
#define A_OPTON2_    0x0C00u
#define A_EMCNT_     0x1000u
#define A_GOADD_     0x1200u
#define A_WTCHDG_    0x1400u
#define A_VSTOP_     0x1600u
#define A_MBSTAT_    0x1800u
#define A_MBYLOW_    0x1810u
#define A_MBYHIGH_   0x1818u
#define A_POKEY_     0x1820u
#define A_SOUND_     0x1840u
#define A_MBWRITE_   0x1860u
#define PC_MAIN_FALL 0x5004u                    /* L5004 JSR VOLCNO: MAIN's frame wait fell through */

/* The cabinet's overlay film (app_loop.c): red above AVG y 110, green below. */
#define BZ_OVERLAY_Y     110.0f
#define BZ_RGB_GREEN     0xFF40FF40u            /* R | G << 8 | B << 16 | A << 24 */
#define BZ_RGB_RED       0xFF3030FFu

/* ------------------------------------------------------------------ */
/* command line: what plat_win.c / bz_smoke.c must not see             */
/* ------------------------------------------------------------------ */

static char cli_roms[1024];                     /* --roms FILE */

static struct cli_strip {
    cli_strip()
    {
        int n = __argc, o = 1;
        char **v = __argv;
        if (!v) return;
        for (int i = 1; i < n; i++) {
            if (!strcmp(v[i], "--roms") && i + 1 < n) {
                snprintf(cli_roms, sizeof cli_roms, "%s", v[++i]);
                continue;
            }
            v[o++] = v[i];
        }
        v[o] = NULL;
        __argc = o;
    }
} cli_strip_once;

/* ------------------------------------------------------------------ */
/* machine state owned by the seam                                     */
/* ------------------------------------------------------------------ */

static cpu_6502   *cpu;
static uint8_t     cpu_mem[0x10000];            /* the core's MEM fallback; never reached (catch-all handlers) */

static plat_inputs cur_in;
static uint8_t     in0_sw = 0x3F;               /* IN0 b0-b5, active low */
static ad_pokey    pokey;
static mathbox     mb;
static uint8_t     sound_latch;

/* the cycle timeline */
static uint64_t mach_cyc;                       /* bus cycles since power-on (cycle_tick) */
static uint64_t pokey_cyc;                      /* the POKEY is clocked to here */
static uint64_t next_nmi;                       /* the grid cycle of the next NMI edge */
static uint64_t next_tick;                      /* the same grid, for the per-NMI housekeeping */
static uint64_t last_kick_cyc;

/* AVG */
static int      vg_running;
static uint64_t vg_busy_until;
static int      avg_have_scale;                 /* the scale register persists across GOADDs */
static int32_t  avg_keep_scale;
static int      avg_keep_bin;

/* clocks (app_loop.c's anchor + stall clamp) */
static int      clk_valid;
static double   clk_base_ms;
static uint64_t clk_base_cyc;
static double   idle_remain_ms;

/* audio */
static int      audio_live;
static uint64_t audio_tick_phase;
static int16_t  audio_buf[512];

/* host edges */
static uint8_t  last_test, last_reset;

/* statistics */
static unsigned long n_nmi, n_nmi_gated, n_pictures, n_passes, n_lit, n_steps_capped;
static unsigned long n_goadd, n_vstop, n_runaway_lists, n_wdog, n_wd_bites, n_resets;
static unsigned long n_unmapped_r, n_unmapped_w, n_rom_writes;
static uint16_t first_unmapped_r[8], first_unmapped_w[8];
static uint64_t max_kick_gap_cyc;
static int      bbox_valid;
static float    bbox_minx, bbox_miny, bbox_maxx, bbox_maxy;

/* ------------------------------------------------------------------ */
/* time passing                                                        */
/* ------------------------------------------------------------------ */

static void pokey_sync(void)
{
    uint64_t n = mach_cyc - pokey_cyc;
    if (n == 0) return;
    ad_pokey_advance(&pokey, (uint32_t)n);
    pokey_cyc = mach_cyc;
}

/* The core's cycle callback: one call per bus cycle, before the access.  The
 * board's NMI clock gives an edge on its grid cycle while the self-test
 * switch is off (IN0 b4 high); the core latches the edge (set_nmi_line). */
static bool cycle_tick(void *)
{
    if (++mach_cyc == next_nmi) {
        next_nmi += BZ_NMI_CYCLES;
        if (in0_sw & K_ST_BIT) {
            cpu->set_nmi_line(true);
            cpu->set_nmi_line(false);
            n_nmi++;
        } else {
            n_nmi_gated++;
        }
    }
    return true;                                /* the bus is always the CPU's */
}

/* ------------------------------------------------------------------ */
/* inputs                                                              */
/* ------------------------------------------------------------------ */

/* app_loop.c inputs_apply: IN0 active low, ALLPOT active high. */
static void inputs_apply(void)
{
    uint8_t in0 = 0x3F, pot = 0;
    if (cur_in.coin1) in0 &= 0xFE;
    if (cur_in.coin2) in0 &= 0xFD;
    if (cur_in.slam)  in0 &= 0xF7;
    if (cur_in.test)  in0 &= (uint8_t)~K_ST_BIT;
    if (cur_in.diag)  in0 &= 0xDF;
    in0_sw = in0;
    if (cur_in.right_rev) pot |= 0x01;
    if (cur_in.right_fwd) pot |= 0x02;
    if (cur_in.left_rev)  pot |= 0x04;
    if (cur_in.left_fwd)  pot |= 0x08;
    if (cur_in.fire)      pot |= K_PL1FIR;
    if (cur_in.start1)    pot |= K_START;
    if (cur_in.start2)    pot |= 0x40;
    pokey_sync();
    ad_pokey_set_allpot(&pokey, pot);
}

static int vg_halted(void) { return !vg_running || mach_cyc >= vg_busy_until; }

static UINT8 rd_in0(UINT32, MemoryReadByte *)
{
    uint8_t v = in0_sw;
    if (vg_halted())          v |= 0x40;        /* b6: VG HALT */
    if ((mach_cyc >> 8) & 1u) v |= 0x80;        /* b7: 3 kHz (refrun.c's model) */
    return v;
}

static UINT8 rd_option(UINT32, MemoryReadByte *) { return plat_dsw_option(); }
static UINT8 rd_opton2(UINT32, MemoryReadByte *) { return plat_dsw_opton2(); }

static UINT8 rd_pokey(UINT32 reg, MemoryReadByte *)
{
    pokey_sync();
    return ad_pokey_read(&pokey, (uint8_t)(reg & 0x0Fu));
}

static void wr_pokey(UINT32 reg, UINT8 v, MemoryWriteByte *)
{
    pokey_sync();
    ad_pokey_write(&pokey, (uint8_t)(reg & 0x0Fu), v);
}

/* ------------------------------------------------------------------ */
/* Mathbox, outputs                                                    */
/* ------------------------------------------------------------------ */

/* $1860-$187F: mathbox.c runs the microcode to its STALL inside the write,
 * so the status at $1800 reads not-busy. */
static void  wr_mb(UINT32 off, UINT8 v, MemoryWriteByte *) { mb_write(&mb, (uint8_t)(off & 0x1F), v); }
static UINT8 rd_mbstat(UINT32, MemoryReadByte *)  { return mb_status(&mb); }
static UINT8 rd_mbylow(UINT32, MemoryReadByte *)  { return mb_ylow(&mb); }
static UINT8 rd_mbyhigh(UINT32, MemoryReadByte *) { return mb_yhigh(&mb); }

static void wr_emcnt(UINT32, UINT8, MemoryWriteByte *) { }                  /* coin counters: no visible effect */

/* SOUND $1840: D0-D4/D7 the discrete sounds (bz_sounds.c), D5 master enable
 * (gates the POKEY too), D6 the start lamp. */
static void wr_sound(UINT32, UINT8 v, MemoryWriteByte *) { sound_latch = v; bz_sounds_w(v); }

static void wr_wtchdg(UINT32, UINT8, MemoryWriteByte *)
{
    n_wdog++;
    if (mach_cyc - last_kick_cyc > max_kick_gap_cyc) max_kick_gap_cyc = mach_cyc - last_kick_cyc;
    last_kick_cyc = mach_cyc;
}

static void wr_vstop(UINT32, UINT8, MemoryWriteByte *) { n_vstop++; vg_running = 0; }

/* ------------------------------------------------------------------ */
/* the picture (app_loop.c hw_goadd / emit_seg)                        */
/* ------------------------------------------------------------------ */

typedef struct { const avg_state *st; } walk_ctx;

/* Liang-Barsky: clip (x0,y0)-(x1,y1) to [xmin,xmax] x [ymin,ymax]. */
static int clip_line(float *x0, float *y0, float *x1, float *y1,
                     float xmin, float ymin, float xmax, float ymax)
{
    float dx = *x1 - *x0, dy = *y1 - *y0, t0 = 0.0f, t1 = 1.0f;
    float p[4] = { -dx, dx, -dy, dy };
    float q[4] = { *x0 - xmin, xmax - *x0, *y0 - ymin, ymax - *y0 };
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0.0f) {
            if (q[i] < 0.0f) return 0;
        } else {
            float t = q[i] / p[i];
            if (p[i] < 0.0f) { if (t > t1) return 0; if (t > t0) t0 = t; }
            else             { if (t < t0) return 0; if (t < t1) t1 = t; }
        }
    }
    float ax = *x0, ay = *y0;
    *x0 = ax + t0 * dx; *y0 = ay + t0 * dy;
    *x1 = ax + t1 * dx; *y1 = ay + t1 * dy;
    return 1;
}

static void bbox_add(float x, float y)
{
    if (!bbox_valid) { bbox_minx = bbox_maxx = x; bbox_miny = bbox_maxy = y; bbox_valid = 1; }
    if (x < bbox_minx) bbox_minx = x;
    if (x > bbox_maxx) bbox_maxx = x;
    if (y < bbox_miny) bbox_miny = y;
    if (y > bbox_maxy) bbox_maxy = y;
}

static void emit_seg(void *ctx, const avg_seg *s)
{
    const walk_ctx *w = (const walk_ctx *)ctx;
    if (s->intensity == 0) return;              /* a dark move: the beam is blanked */
    float x0 = (float)AVG_Q15_TO_F(s->x0), y0 = (float)AVG_Q15_TO_F(s->y0);
    float x1 = (float)AVG_Q15_TO_F(s->x1), y1 = (float)AVG_Q15_TO_F(s->y1);

    /* the AVG's clip window (STAT HI/IN, BIGWND): draw inside only; OUT mode
     * is never used by this ROM */
    if ((s->clip & AVG_CLIP_ARMED) && !(s->clip & AVG_CLIP_OUT)) {
        if (!clip_line(&x0, &y0, &x1, &y1,
                       (float)AVG_Q15_TO_F(w->st->clip_xmin), (float)AVG_Q15_TO_F(w->st->clip_ymin),
                       (float)AVG_Q15_TO_F(w->st->clip_xmax), (float)AVG_Q15_TO_F(w->st->clip_ymax)))
            return;
    }

    /* the overlay gel: split a segment that crosses its edge */
    if ((y0 > BZ_OVERLAY_Y) != (y1 > BZ_OVERLAY_Y)) {
        float t = (BZ_OVERLAY_Y - y0) / (y1 - y0);
        float xm = x0 + t * (x1 - x0);
        plat_video_line(x0, y0, xm, BZ_OVERLAY_Y, y0 > BZ_OVERLAY_Y ? BZ_RGB_RED : BZ_RGB_GREEN, s->intensity);
        plat_video_line(xm, BZ_OVERLAY_Y, x1, y1, y1 > BZ_OVERLAY_Y ? BZ_RGB_RED : BZ_RGB_GREEN, s->intensity);
    } else {
        plat_video_line(x0, y0, x1, y1, y0 > BZ_OVERLAY_Y ? BZ_RGB_RED : BZ_RGB_GREEN, s->intensity);
    }
    n_lit++;
    bbox_add(x0, y0);
    bbox_add(x1, y1);
}

/* GOADD $1200: the AVG starts at $2000 whatever the data byte (trap 1), keeps
 * its scale register (trap 2), draws for the list's counted time, and a list
 * that never reaches HALT keeps it busy until VSTOP (trap 4). */
static void wr_goadd(UINT32, UINT8, MemoryWriteByte *)
{
    avg_mem am;
    avg_state st;
    avg_cfg cfg;
    walk_ctx w;
    avg_result r;

    n_goadd++;
    am.vram = g.vram;
    am.vrom = vecrom;
    avg_state_init(&st);
    if (avg_have_scale) { st.scale_q15 = avg_keep_scale; st.bin_scale = avg_keep_bin; }
    w.st = &st;
    memset(&cfg, 0, sizeof cfg);
    cfg.seg = emit_seg;
    cfg.ctx = &w;
    plat_video_begin();
    r = avg_walk(&am, AVG_VRAM_BASE, &st, &cfg);
    plat_video_present();
    avg_keep_scale = st.scale_q15; avg_keep_bin = st.bin_scale; avg_have_scale = 1;
    n_pictures++;

    vg_running = 1;
    vg_busy_until = mach_cyc + (r.cycles + AVG_VGGO_LEADIN + AVG_CYC_PER_CPU - 1u) / AVG_CYC_PER_CPU;
    if (r.stop != AVG_STOP_HALT) { vg_busy_until = UINT64_MAX; n_runaway_lists++; }
}

/* ------------------------------------------------------------------ */
/* everything else                                                     */
/* ------------------------------------------------------------------ */

/* Reads $00, writes dropped, counted.  DRADAR's deliberate time-waster
 * L6B79 CMP ($00,X) can read almost anything (EMULATOR_PORT_NOTES.md 2.7) -
 * harmless, and it lands here when it reads an unmapped address. */
static UINT8 rd_unmapped(UINT32 a, MemoryReadByte *)
{
    if (n_unmapped_r < 8) first_unmapped_r[n_unmapped_r] = (uint16_t)a;
    n_unmapped_r++;
    return 0x00;
}

/* A READ of the write-only Mathbox range $1860-$187F: not the ROM's doing
 * but the NMOS 6502's - an indexed store (the self test's STA $1860,X loops)
 * spends a bus cycle reading the target before it writes it, and the core
 * runs that cycle.  Nothing answers on the board; counted apart. */
static unsigned long n_dummy_reads;
static UINT8 rd_write_only(UINT32, MemoryReadByte *) { n_dummy_reads++; return 0x00; }

static void wr_rom(UINT32, UINT8, MemoryWriteByte *) { n_rom_writes++; }

static void wr_unmapped(UINT32 a, UINT8, MemoryWriteByte *)
{
    if (n_unmapped_w < 8) first_unmapped_w[n_unmapped_w] = (uint16_t)a;
    n_unmapped_w++;
}

/* ------------------------------------------------------------------ */
/* the memory map ($0000-$7FFF; the core masks A15)                    */
/* ------------------------------------------------------------------ */
/* AAE-style tables: {low, high, handler, user}; a NULL handler = plain memory
 * at `user`; handlers get the offset from `low`.  First match wins, so the
 * program ROM (every opcode fetch) comes first and the catch-all last. */

#define ROM_PTR(p) (const_cast<uint8_t *>(p))
#define MAP_END    { (UINT32)-1, (UINT32)-1, NULL, NULL }

static MemoryReadByte emu_read[] = {
    { 0x5000,     0x7FFF,           NULL,        ROM_PTR(progrom) },
    { 0x0000,     0x03FF,           NULL,        g.ram },
    { 0x2000,     0x2FFF,           NULL,        g.vram },
    { 0x3000,     0x3FFF,           NULL,        ROM_PTR(vecrom) },
    { A_IN0_,     A_IN0_,           rd_in0,      NULL },
    { A_OPTION_,  A_OPTION_,        rd_option,   NULL },
    { A_OPTON2_,  A_OPTON2_,        rd_opton2,   NULL },
    { A_MBSTAT_,  A_MBSTAT_,        rd_mbstat,   NULL },
    { A_MBYLOW_,  A_MBYLOW_,        rd_mbylow,   NULL },
    { A_MBYHIGH_, A_MBYHIGH_,       rd_mbyhigh,  NULL },
    { A_POKEY_,   A_POKEY_ + 0x0F,  rd_pokey,    NULL },
    { A_MBWRITE_, A_MBWRITE_ + 0x1F, rd_write_only, NULL },
    { 0x0000,     0xFFFF,           rd_unmapped, NULL },
    MAP_END
};

static MemoryWriteByte emu_write[] = {
    { 0x0000,     0x03FF,           NULL,        g.ram },
    { 0x2000,     0x2FFF,           NULL,        g.vram },
    { A_EMCNT_,   A_EMCNT_,         wr_emcnt,    NULL },
    { A_GOADD_,   A_GOADD_,         wr_goadd,    NULL },
    { A_WTCHDG_,  A_WTCHDG_,        wr_wtchdg,   NULL },
    { A_VSTOP_,   A_VSTOP_,         wr_vstop,    NULL },
    { A_POKEY_,   A_POKEY_ + 0x0F,  wr_pokey,    NULL },
    { A_SOUND_,   A_SOUND_,         wr_sound,    NULL },
    { A_MBWRITE_, A_MBWRITE_ + 0x1F, wr_mb,      NULL },
    { 0x3000,     0x3FFF,           wr_rom,      NULL },
    { 0x5000,     0x7FFF,           wr_rom,      NULL },
    { 0x0000,     0xFFFF,           wr_unmapped, NULL },
    MAP_END
};

/* ------------------------------------------------------------------ */
/* audio: one NMI tick of POKEY + the discrete sounds (app_loop.c)     */
/* ------------------------------------------------------------------ */

static void audio_tick(void)
{
    int n, got;
    audio_tick_phase += (uint64_t)BZ_AUDIO_RATE * BZ_NMI_CYCLES;
    n = (int)(audio_tick_phase / (uint64_t)BZ_CPU_HZ);
    audio_tick_phase %= (uint64_t)BZ_CPU_HZ;
    if (n > (int)(sizeof audio_buf / sizeof audio_buf[0])) n = (int)(sizeof audio_buf / sizeof audio_buf[0]);
    if (n <= 0) return;
    got = ad_pokey_audio_read(&pokey, audio_buf, n);
    if (got < n) memset(audio_buf + got, 0, (size_t)(n - got) * sizeof audio_buf[0]);
    if (!(sound_latch & 0x20)) memset(audio_buf, 0, (size_t)n * sizeof audio_buf[0]);   /* D5: master enable */
    if (audio_live) plat_audio_push(audio_buf, n);
}

/* ------------------------------------------------------------------ */
/* pacing                                                              */
/* ------------------------------------------------------------------ */

static void machine_idle(double remain)
{
    idle_remain_ms = remain;
    plat_sleep_ms(remain >= 2.0 ? (int)remain - 1 : 0);
}

/* Block until the wall clock reaches machine cycle `ev` (app_loop.c
 * machine_tick's anchor and stall clamp). */
static void wait_until_cyc(uint64_t ev)
{
    for (;;) {
        double now = plat_now_ms(), due;
        if (!clk_valid) {
            clk_base_ms = now;
            clk_base_cyc = ev;
            clk_valid = 1;
        }
        due = clk_base_ms + ((double)ev - (double)clk_base_cyc) * 1000.0 / BZ_CPU_HZ;
        if (now - due > BZ_STALL_MS) {          /* debugger / dragged window: do not dump time */
            clk_base_ms = now;
            clk_base_cyc = ev;
            due = now;
        }
        if (now >= due) return;
        machine_idle(due - now);
    }
}

/* The per-NMI housekeeping, at the first instruction boundary after a grid
 * cycle: clock the POKEY to here, one tick of audio, the engine ramp. */
static void machine_tick(void)
{
    next_tick += BZ_NMI_CYCLES;
    pokey_sync();
    bz_sounds_tick((double)BZ_NMI_CYCLES / BZ_CPU_HZ);
    audio_tick();
}

/* ------------------------------------------------------------------ */
/* running the CPU                                                     */
/* ------------------------------------------------------------------ */

/* The board's RESET line (power-on, watchdog bite, F2 / F3): the CPU restarts
 * at PWRON; the Mathbox and the AVG are reset.  RAM, the POKEY and the SOUND
 * latch keep their state (PWRON sets them itself). */
static void pull_reset(void)
{
    n_resets++;
    cpu->reset6502();
    mb_reset(&mb);
    vg_running = 0;
    last_kick_cyc = mach_cyc;
}

/* Run to the next MAIN fall-through, at most cap_cyc cycles.  1 = a pass. */
static int run_pass(uint64_t cap_cyc)
{
    const uint64_t start = mach_cyc;
    for (;;) {
        while (next_tick <= mach_cyc) { wait_until_cyc(next_tick); machine_tick(); }

        cpu->step6502();

        if (mach_cyc - last_kick_cyc > BZ_WD_CYCLES) {   /* the hardware watchdog bites */
            n_wd_bites++;
            EMU_LOG("watchdog: no WTCHDG for %llu cycles (PC $%04X) - RESET",
                    (unsigned long long)(mach_cyc - last_kick_cyc), cpu->get_pc());
            pull_reset();
        }
        if (cpu->get_pc() == PC_MAIN_FALL) return 1;
        if (mach_cyc - start >= cap_cyc) return 0;
    }
}

/* ------------------------------------------------------------------ */
/* the app hooks (platform\bz_platform.h)                              */
/* ------------------------------------------------------------------ */

extern "C" {

double bz_app_idle_ms(void) { return idle_remain_ms; }

void bz_app_init(void)
{
    const char *roms = cli_roms;
#ifndef EMU_HEADLESS
    char *ini_roms = get_config_string("main", "roms", "");
    if (!roms[0] && ini_roms) roms = ini_roms;
    set_config_string("main", "roms", ini_roms ? ini_roms : "");     /* the key shows up in the ini */
#endif
    EMU_LOG("bz_emu: the real Battlezone ROM on AAE's cpu_6502 core; ROM zip: %s",
            roms[0] ? roms : "(default: roms\\bzone.zip beside the exe)");
    if (emu_roms_load_default(roms, 0) != 0) {
#ifndef EMU_HEADLESS
        free(ini_roms);
#endif
        plat_shutdown();
        exit(3);
    }
#ifndef EMU_HEADLESS
    free(ini_roms);
#endif

    memset(&g, 0, sizeof g);
    memset(&cur_in, 0, sizeof cur_in);
    mach_cyc = pokey_cyc = 0;
    next_nmi = next_tick = BZ_NMI_CYCLES;
    last_kick_cyc = 0;
    vg_running = 0; avg_have_scale = 0;
    sound_latch = 0;
    clk_valid = 0;
    mb_reset(&mb);
    ad_pokey_init(&pokey, 1512000u, BZ_AUDIO_RATE);
    ad_pokey_set_measured_audio(&pokey, 20.0, 1.5);
    plat_input_poll(&cur_in);                   /* the self-test switch as it is at power-on */
    inputs_apply();
    last_test = cur_in.test; last_reset = cur_in.reset;
    audio_live = plat_audio_open(BZ_AUDIO_RATE) == 0;
    if (!audio_live) EMU_WARN("plat_audio_open failed; continuing without sound");
    bz_sounds_init();

    /* address mask $7FFF: the board's 15-bit decode; CPU 0, NMOS */
    cpu = new cpu_6502(cpu_mem, emu_read, emu_write, 0x7FFF, 0, CPU_NMOS_6502);
    cpu->mame_memory_handling(true);
    cpu->set_cycle_callback(cycle_tick, NULL);
    cpu->reset6502();
    EMU_LOG("power-on: RESET $%04X, NMI $%04X; self-test switch %s",
            (unsigned)(cpu_rd(0xFFFC) | (cpu_rd(0xFFFD) << 8)), (unsigned)(cpu_rd(0xFFFA) | (cpu_rd(0xFFFB) << 8)),
            cur_in.test ? "ON" : "off");
}

double bz_app_step(double now_ms)
{
    static double last_status_ms;
    (void)now_ms;
    plat_input_poll(&cur_in);
    inputs_apply();
    /* Host affordances, as bz_win.exe has them.  F2 switching the self test
     * ON resets the board (the game reads the switch only at PWRON - MAME's
     * F2 + reset); switching it OFF needs nothing, the ROM's STEST7 sees it
     * and JMPs PWRON.  F3 = the board's RESET. */
    if ((cur_in.test && !last_test) || (cur_in.reset && !last_reset)) pull_reset();
    last_test = cur_in.test;
    last_reset = cur_in.reset;

    if (run_pass((uint64_t)BZ_STEP_CAP_NMIS * BZ_NMI_CYCLES)) {
        n_passes++;
        g.pass_count++;
    } else {
        n_steps_capped++;
    }
    g.irq_count = (uint32_t)n_nmi;

    double t = plat_now_ms();
    if (t - last_status_ms >= 1000.0) {
        char s[200];
        snprintf(s, sizeof s, "[emu] passes %lu  NMIs %lu  pictures %lu  lit %lu  bbox x %.0f..%.0f y %.0f..%.0f",
                 n_passes, n_nmi, n_pictures, n_lit,
                 (double)bbox_minx, (double)bbox_maxx, (double)bbox_miny, (double)bbox_maxy);
        plat_status_text(s);
        last_status_ms = t;
    }
    return 0.0;
}

void bz_app_exit(void)
{
    if (audio_live) plat_audio_close();
    audio_live = 0;
    EMU_LOG("emu: ROM set '%s'; %llu cycles (%.2f s); MAIN passes %lu (steps with none %lu); NMIs %lu (+ %lu gated by "
            "the self-test switch)", emu_roms_set_name(), (unsigned long long)mach_cyc, (double)mach_cyc / BZ_CPU_HZ,
            n_passes, n_steps_capped, n_nmi, n_nmi_gated);
    EMU_LOG("emu: GOADD %lu (never-halting lists %lu), VSTOP %lu; watchdog kicks %lu (max gap %llu cycles), bites %lu; "
            "RESETs %lu; Mathbox starts %lu, runaway %lu", n_goadd, n_runaway_lists, n_vstop, n_wdog,
            (unsigned long long)max_kick_gap_cyc, n_wd_bites, n_resets, (unsigned long)mb.starts,
            (unsigned long)mb.runaway);
    EMU_LOG("emu: unmapped reads %lu (DRADAR's L6B79 CMP ($00,X) time-waster), writes %lu; ROM writes %lu; "
            "indexed-store dummy reads of $1860-$187F %lu", n_unmapped_r, n_unmapped_w, n_rom_writes, n_dummy_reads);
    for (unsigned long i = 0; i < n_unmapped_r && i < 8; i++) EMU_LOG("emu:   unmapped read  $%04X", first_unmapped_r[i]);
    for (unsigned long i = 0; i < n_unmapped_w && i < 8; i++) EMU_LOG("emu:   unmapped write $%04X", first_unmapped_w[i]);
    delete cpu;
    cpu = NULL;
}

/* Read-only stats for test drivers (c_src\tests\bz_smoke.c; not part of the
 * platform contract) - app_loop.c's signature. */
void bz_app_stats(unsigned long *passes, unsigned long *nmis, unsigned long *pictures,
                  unsigned long *lit, float bbox[4])
{
    if (passes)   *passes = n_passes;
    if (nmis)     *nmis = n_nmi;
    if (pictures) *pictures = n_pictures;
    if (lit)      *lit = n_lit;
    if (bbox) { bbox[0] = bbox_minx; bbox[1] = bbox_maxx; bbox[2] = bbox_miny; bbox[3] = bbox_maxy; }
}

} /* extern "C" */
