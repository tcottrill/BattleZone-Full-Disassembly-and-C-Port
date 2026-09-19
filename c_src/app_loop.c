/* app_loop.c - Battlezone C port: the game seam and the application loop (M7).
 *
 * Linked ONLY into the game binaries (bz_win.exe, tests\bz_smoke.exe).  The
 * verification harnesses have their own hw.h seams (tests\lockstep.c replays
 * the ROM's I/O); never link them with this.  Modelled on the Tempest port's
 * app_loop.c (its NOTES_m8.md has the design this follows), cut down to what
 * Battlezone's board needs:
 *
 *  (a) every hw.h function over the platform contract (platform/
 *      bz_platform.h): IN0 (coins/slam/test active low, VG HALT b6, 3 kHz
 *      clock b7), the two DIP banks, one POKEY (c012294.c: sound, PRAND, the
 *      ALLPOT switch byte carrying the tread levers/fire/start), the Mathbox
 *      (mathbox.c), the AVG run/HALT state, the SOUND latch;
 *  (b) machine time: ONE thread, a cycle timeline at 1.512 MHz.  Translated
 *      code charges the ROM's own cycles as it runs (hw.h CYC() ->
 *      hw_cycles(), TRANSLATING.md "Cycle counting"), and the NMI ($558F,
 *      every 6144 cycles = 3 kHz / 12 = 246.09 Hz) is taken at the first
 *      charge after it falls due - mid-pass, as on the board, where the game
 *      reads the NMI's counters (S_INTCT at ROBOT) and POKEY RANDOM moves
 *      with the cycle count.  Every NMI, mid-pass or in MAIN's spin loops,
 *      waits for the wall clock (machine_tick), so they also land at a
 *      steady 246 Hz of host time.  Only the self test (bzstst.c, not
 *      cycle-counted) still charges its hardware accesses a flat few cycles
 *      (Tempest's 4/8) to pace its busy-waits;
 *  (c) video: every GOADD the NMI strobes (every 6 NMIs, ~41 Hz) walks the
 *      list it starts (avg.c) and presents the lit segments, clipped to the
 *      AVG's clip window, coloured by the cabinet's overlay;
 *  (d) audio: per NMI one tick of POKEY samples plus the discrete sounds the
 *      SOUND latch drives, as recorded samples on the platform's mixer
 *      (bz_sounds.c; the engine's 1.0 <-> 1.66 rate change ramps like the
 *      PCB's);
 *  (e) bz_app_init / bz_app_step / bz_app_exit.
 *
 * MAIN's two spin loops.  "L5000 LSR SYNC / BCC MAIN" is hw_wait_frame()
 * (the NMI bumps SYNC once per 16 NMIs); MAIN_5's "LDX BUFRDY / BEQ MAIN_5",
 * after JSR VOLCNO, is hw_wait_bufrdy() - both spin a RAM cell only the NMI
 * sets, so they service NMIs until it changes, charging the loop's cycles.
 */
#include <math.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c012294.h"
#include "state.h"
#include "hw.h"
#include "mathbox.h"
#include "avg.h"
#include "vecrom.h"
#include "bzone_game.h"
#include "bzone_nmi.h"
#include "bzstst.h"
#include "bz_sounds.h"
#include "platform/bz_platform.h"

/* ------------------------------------------------------------------ */
/* constants                                                           */
/* ------------------------------------------------------------------ */

#define BZ_CPU_HZ        1512000.0  /* 12.096 MHz / 8: CPU = POKEY clock */
#define BZ_NMI_CYCLES    6144u      /* 3 kHz / 12 = 246.09 Hz            */
#define BZ_AUDIO_RATE    44100
#define BZ_STALL_MS      100.0  /* catch-up clamp                    */
#define BZ_COST_READ     4u     /* seam access costs (Tempest's)     */
#define BZ_COST_WRITE    8u
#define BZ_WD_CYCLES     1134000u  /* hardware watchdog: 0.75 s (refrun/AAE/Tempest) */

/* The cabinet's overlay film (upright): a red strip across the top of the
 * tube over the radar and the score/messages, green below.  AVG y at which
 * the colour changes (measured from the attract/play pictures: the radar
 * (y 125..192), score, lives and "ENEMY IN RANGE" sit above it; the moon
 * (y ~65), mountains and everything 3-D below). */
#define BZ_OVERLAY_Y     110.0f
#define BZ_RGB_GREEN     0xFF40FF40u  /* R | G << 8 | B << 16 | A << 24 */
#define BZ_RGB_RED       0xFF3030FFu

/* ------------------------------------------------------------------ */
/* machine state owned by the seam                                     */
/* ------------------------------------------------------------------ */

static plat_inputs cur_in;
static uint8_t     in0_sw = 0x3F;  /* IN0 b0-b5, active low */
static uint8_t     allpot_sw;      /* ALLPOT, active high   */
static ad_pokey    pokey;
static mathbox     mb;
static uint8_t     sound_latch;

static uint64_t mach_cyc;
static uint64_t next_nmi;
static int      in_nmi;
static int      in_selftest;    /* BZSTST (bzstst.c) is driving, not main_pass() */
static uint64_t last_kick_cyc;  /* the last WTCHDG strobe */
static jmp_buf  wd_jmp;         /* the watchdog's board reset (bz_app_step) */
static int      wd_armed;       /* inside bz_app_step's guarded region */
static unsigned long n_wd_resets;

static int      vg_running;
static uint64_t vg_busy_until;

/* wall clock anchor */
static int      clk_valid;
static double   clk_base_ms;
static uint64_t clk_base_cyc;
static double   idle_remain_ms;

/* audio */
static int      audio_live;
static uint64_t audio_tick_phase;
static int16_t  audio_buf[512];

/* stats */
static unsigned long n_nmi, n_pictures, n_passes, n_lit;
static int      bbox_valid;
static float    bbox_minx, bbox_miny, bbox_maxx, bbox_maxy;

/* ------------------------------------------------------------------ */
/* time passing                                                        */
/* ------------------------------------------------------------------ */

static void nmi_service(void);  /* forward decl: defined below; hw_in0() needs it for BZSTST */
static void machine_tick(void);

/* Machine time.  The board's watchdog is a hardware counter on this clock:
 * if WTCHDG ($1400) has not been strobed for BZ_WD_CYCLES it pulls RESET.
 * Modelled here, below all translated code: bz_app_step() arms it and
 * catches the bite (wd_jmp) - a board reset through PWRON. */
static void spend(uint32_t n)
{
    if (n == 0) return;
    mach_cyc += n;
    ad_pokey_advance(&pokey, n);
    if (wd_armed && mach_cyc - last_kick_cyc > BZ_WD_CYCLES) {
        wd_armed = 0;
        longjmp(wd_jmp, 1);
    }
}

static void io_read(void)  { if (in_selftest) spend(BZ_COST_READ); }   /* game code: the CYC() charges include the access */
static void io_write(void) { if (in_selftest) spend(BZ_COST_WRITE); }

/* ------------------------------------------------------------------ */
/* inputs                                                              */
/* ------------------------------------------------------------------ */

static void inputs_apply(void)
{
    uint8_t in0 = 0x3F, pot = 0;
    if (cur_in.coin1) in0 &= 0xFE;
    if (cur_in.coin2) in0 &= 0xFD;
    if (cur_in.slam)  in0 &= 0xF7;
    if (cur_in.test)  in0 &= 0xEF;
    if (cur_in.diag)  in0 &= 0xDF;
    in0_sw = in0;
    if (cur_in.right_rev) pot |= 0x01;
    if (cur_in.right_fwd) pot |= 0x02;
    if (cur_in.left_rev)  pot |= 0x04;
    if (cur_in.left_fwd)  pot |= 0x08;
    if (cur_in.fire)      pot |= K_PL1FIR;
    if (cur_in.start1)    pot |= K_START;
    if (cur_in.start2)    pot |= 0x40;
    allpot_sw = pot;
    ad_pokey_set_allpot(&pokey, allpot_sw);
}

static int vg_halted(void)
{
    return !vg_running || mach_cyc >= vg_busy_until;
}

uint8_t hw_in0(void)
{
    uint8_t v;
    /* BZSTST's busy-wait loops (RAMTST_7, STEST4_5/7/10, MBDIAG_1, ...) spin
     * on this register with no frame-wait of their own to service the NMI
     * (unlike the game path's hw_wait_frame()) - the NMI is non-maskable, so
     * service any that machine time has already passed here instead. */
    if (in_selftest) while (next_nmi <= mach_cyc) nmi_service();
    v = in0_sw;
    if (vg_halted())             v |= 0x40;  /* b6: VG HALT */
    if ((mach_cyc >> 8) & 1u)    v |= 0x80;  /* b7: 3 kHz (refrun.c's model) */
    io_read();
    return v;
}

uint8_t hw_option(void) { uint8_t v = plat_dsw_option(); io_read(); return v; }
uint8_t hw_opton2(void) { uint8_t v = plat_dsw_opton2(); io_read(); return v; }

uint8_t hw_pokey_read(uint8_t reg)
{
    uint8_t v = ad_pokey_read(&pokey, (uint8_t)(reg & 0x0F));
    io_read();
    return v;
}

void hw_pokey_write(uint8_t reg, uint8_t val)
{
    ad_pokey_write(&pokey, (uint8_t)(reg & 0x0F), val);
    io_write();
}

uint8_t hw_mb_status(void) { uint8_t v = mb_status(&mb); io_read(); return v; }
uint8_t hw_mb_ylow(void)   { uint8_t v = mb_ylow(&mb);   io_read(); return v; }
uint8_t hw_mb_yhigh(void)  { uint8_t v = mb_yhigh(&mb);  io_read(); return v; }
void    hw_mb_write(uint8_t offset, uint8_t val) { mb_write(&mb, (uint8_t)(offset & 0x1F), val); io_write(); }

void hw_emcnt(uint8_t v) { (void)v; io_write(); }  /* coin counters: no visible effect */
void hw_wtchdg(void)     { last_kick_cyc = mach_cyc; io_write(); }
void hw_vstop(void)      { vg_running = 0; io_write(); }
void hw_sound(uint8_t v) { sound_latch = v; bz_sounds_w(v); io_write(); }

/* ------------------------------------------------------------------ */
/* video                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    const avg_state *st;
} walk_ctx;

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
    {
        float ax = *x0, ay = *y0;
        *x0 = ax + t0 * dx; *y0 = ay + t0 * dy;
        *x1 = ax + t1 * dx; *y1 = ay + t1 * dy;
    }
    return 1;
}

static void emit_seg(void *ctx, const avg_seg *s)
{
    const walk_ctx *w = (const walk_ctx *)ctx;
    float x0, y0, x1, y1;
    uint32_t rgb;

    if (s->intensity == 0) return;
    x0 = (float)AVG_Q15_TO_F(s->x0); y0 = (float)AVG_Q15_TO_F(s->y0);
    x1 = (float)AVG_Q15_TO_F(s->x1); y1 = (float)AVG_Q15_TO_F(s->y1);

    /* The AVG's clip window (STAT HI/IN, BIGWND): draw inside only.  The
     * OUT mode is never used by this ROM (avg.h "CLIP WINDOW"). */
    if ((s->clip & AVG_CLIP_ARMED) && !(s->clip & AVG_CLIP_OUT)) {
        if (!clip_line(&x0, &y0, &x1, &y1,
                       (float)AVG_Q15_TO_F(w->st->clip_xmin), (float)AVG_Q15_TO_F(w->st->clip_ymin),
                       (float)AVG_Q15_TO_F(w->st->clip_xmax), (float)AVG_Q15_TO_F(w->st->clip_ymax)))
            return;
    }

    /* The overlay gel tints everything above its edge: split a segment that
     * crosses BZ_OVERLAY_Y there (red above, green below). */
    if ((y0 > BZ_OVERLAY_Y) != (y1 > BZ_OVERLAY_Y)) {
        float t = (BZ_OVERLAY_Y - y0) / (y1 - y0);
        float xm = x0 + t * (x1 - x0);
        plat_video_line(x0, y0, xm, BZ_OVERLAY_Y, y0 > BZ_OVERLAY_Y ? BZ_RGB_RED : BZ_RGB_GREEN, s->intensity);
        plat_video_line(xm, BZ_OVERLAY_Y, x1, y1, y1 > BZ_OVERLAY_Y ? BZ_RGB_RED : BZ_RGB_GREEN, s->intensity);
    } else {
        rgb = (y0 > BZ_OVERLAY_Y) ? BZ_RGB_RED : BZ_RGB_GREEN;
        plat_video_line(x0, y0, x1, y1, rgb, s->intensity);
    }

    n_lit++;
    if (!bbox_valid) { bbox_minx = bbox_maxx = x0; bbox_miny = bbox_maxy = y0; bbox_valid = 1; }
    if (x0 < bbox_minx) bbox_minx = x0; if (x0 > bbox_maxx) bbox_maxx = x0;
    if (x1 < bbox_minx) bbox_minx = x1; if (x1 > bbox_maxx) bbox_maxx = x1;
    if (y0 < bbox_miny) bbox_miny = y0; if (y0 > bbox_maxy) bbox_maxy = y0;
    if (y1 < bbox_miny) bbox_miny = y1; if (y1 > bbox_maxy) bbox_maxy = y1;
}

/* GOADD ($1200): start the AVG at the buffer page's list ($2002/$2802 -
 * VECRAM's own first word JMPLs there; tests\refrun.c's model), note how
 * long it will be busy (IN0 b6), and present the picture. */
void hw_goadd(uint8_t page)
{
    avg_mem am;
    avg_state st;
    avg_cfg cfg;
    walk_ctx w;
    avg_result r;
    /* The AVG always starts at $2000 (VECRAM); the data byte is ignored by the
     * hardware.  In the game VECRAM's first word is a JMPL to the live buffer
     * ($2002/$2802, the NMI flips it) - the same list tests\refrun.c's
     * "page<<8 + 2" shortcut walks; the self test (STEST4/6) builds its
     * picture AT $2000, which that shortcut skipped the first word of (the
     * HATCH JSRL - user report: "just a few ones and zeros"). */
    uint16_t start = AVG_VRAM_BASE;
    (void)page;

    am.vram = g.vram;
    am.vrom = vecrom;
    avg_state_init(&st);
    /* The AVG's scale register is not reset by GOADD: it keeps the last
     * SCAL's value.  Every game list sets it first (MAIN's VGSCA1); the self
     * test's DIATST patterns set none and rely on it (their corner-to-corner
     * diagonal, -512,-384 to +511,+383, fills the tube only at the scale the
     * preceding list left). */
    {
        static int     have_scale;
        static int32_t keep_scale;
        static int     keep_bin;
        if (have_scale) { st.scale_q15 = keep_scale; st.bin_scale = keep_bin; }
        w.st = &st;
        memset(&cfg, 0, sizeof cfg);
        cfg.seg = emit_seg;
        cfg.ctx = &w;
        plat_video_begin();
        r = avg_walk(&am, start, &st, &cfg);
        plat_video_present();
        keep_scale = st.scale_q15; keep_bin = st.bin_scale; have_scale = 1;
    }
    n_pictures++;
    vg_running = 1;
    vg_busy_until = mach_cyc + (r.cycles + AVG_VGGO_LEADIN + AVG_CYC_PER_CPU - 1u) / AVG_CYC_PER_CPU;
    /* A list that never reaches HALT (a runaway walk - e.g. a self-test
     * DIATST pattern jumping into cleared vector RAM) keeps the AVG busy until
     * VSTOP; BZSTST's STEST4_10 then waits for HALT without kicking the dog
     * ("IF NO HALT, WTCHDGDOG WILL RESET") - see bz_watchdog_check(). */
    if (r.stop != AVG_STOP_HALT) vg_busy_until = UINT64_MAX;
    io_write();
}

/* ------------------------------------------------------------------ */
/* audio: POKEY + the discrete sounds on the SOUND latch               */
/* ------------------------------------------------------------------ */

/* The POKEY's output is gated by the SOUND latch's D5 (master enable:
 * bzone.cpp forwards POKEY writes only while it is set).  The latch's
 * discrete sounds (explosion, shell, engine) are recorded samples played by
 * the platform's mixer - bz_sounds.c. */
static void pokey_gate(int16_t *buf, int n)
{
    if (!(sound_latch & 0x20)) memset(buf, 0, (size_t)n * sizeof buf[0]);
}

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
    pokey_gate(audio_buf, n);
    if (audio_live) plat_audio_push(audio_buf, n);
}

/* ------------------------------------------------------------------ */
/* the NMI and the wall clock                                          */
/* ------------------------------------------------------------------ */

static void nmi_service(void)
{
    if (mach_cyc < next_nmi) spend((uint32_t)(next_nmi - mach_cyc));
    next_nmi += BZ_NMI_CYCLES;
    /* The board gates the NMI with the self-test switch (MAME bzone.cpp
     * bzone_interrupt: NMI only while IN0 b4 is high, i.e. the switch is
     * OFF).  With it on the game's NMI would swap display buffers (EOR
     * VECRAM+1) and restart the AVG over the self test's own list at $2000 -
     * user report: "correct screen for a second, then a scribble" (the
     * first swap comes 256 NMIs after PWRON clears VTIMER).  The grid and
     * the audio tick keep running. */
    if (in0_sw & 0x10) {
        in_nmi = 1;
        spend(7);  /* the 6502's interrupt entry */
        nmi();
        in_nmi = 0;
        n_nmi++;
    }
    bz_sounds_tick((double)BZ_NMI_CYCLES / BZ_CPU_HZ);
    audio_tick();
}

/* hw.h CYC(): translated code's own time.  An NMI that falls due is taken
 * at once, at this charge point - on the board it would have interrupted
 * the instructions just charged (at most a few cycles earlier).  Not from
 * inside the NMI itself (the 6502 does not nest an edge it is serving).
 * It waits for the wall clock like the spin loops' NMIs (machine_tick): the
 * host runs a pass's 9-18 NMIs of machine time in microseconds, and taking
 * them back to back pushed their audio blocks (and GOADD pictures) in one
 * burst - the stream ring overflowed and flushed, then starved in the frame
 * wait (user report: garbled POKEY sounds). */
void hw_cycles(unsigned n)
{
    spend(n);
    while (!in_nmi && next_nmi <= mach_cyc) machine_tick();
}

static void machine_idle(double remain)
{
    idle_remain_ms = remain;
    plat_sleep_ms(remain >= 2.0 ? (int)remain - 1 : 0);
}

/* Service the next NMI once the wall clock says it is due. */
static void machine_tick(void)
{
    for (;;) {
        double now = plat_now_ms(), due;
        if (!clk_valid) {
            clk_base_ms = now;
            clk_base_cyc = mach_cyc;
            clk_valid = 1;
        }
        due = clk_base_ms + ((double)next_nmi - (double)clk_base_cyc) * 1000.0 / BZ_CPU_HZ;
        if (now - due > BZ_STALL_MS) {  /* debugger / dragged window: do not dump NMIs */
            clk_base_ms = now;
            clk_base_cyc = next_nmi;
            due = now;
        }
        if (now >= due) { nmi_service(); return; }
        machine_idle(due - now);
    }
}

/* MAIN's frame wait ($5000 LSR SYNC / L5002 BCC MAIN): each spin is the ROM's
 * own LSR (5) + BCC (3 taken, 2 on the way out); between spins the NMI the
 * wall clock says is due is serviced (machine_tick). */
void hw_wait_frame(void)
{
    for (;;) {
        unsigned c = SYNC & 1u;  /* L5000 LSR SYNC */
        SYNC = (uint8_t)(SYNC >> 1);
        spend(5);
        if (c) {
            spend(2);  /* L5002 BCC MAIN, not taken */
            break;
        }
        spend(3);                    /* BCC taken */
        machine_tick();
    }
}

/* MAIN_5's BUFRDY spin ($5007 LDX BUFRDY / $5009 BEQ MAIN_5), after JSR
 * VOLCNO as on the board: LDX (3) + BEQ (3 taken, 2 on the way out). */
void hw_wait_bufrdy(void)
{
    for (;;) {
        spend(3);                           /* L5007 LDX BUFRDY */
        if (BUFRDY != 0) { spend(2); break; }  /* L5009 BEQ, not taken */
        spend(3);                           /* BEQ taken */
        machine_tick();
    }
}
/* BZSTST pacing: the self test's MBDIAG/STEST4/STEST7 loop (bzstst.c
 * selftest_pass()) has no frame wait of its own (unlike MAIN's
 * hw_wait_frame()) - it just runs as fast as the busy-wait loops (serviced
 * via hw_in0(), above) let it.  Called once per selftest_pass() so the self
 * test also runs at real time; uses the same wall-clock anchor as
 * machine_tick(), advanced to mach_cyc instead of next_nmi. */
static void machine_wait_cyc(void)
{
    for (;;) {
        double now = plat_now_ms(), due;
        if (!clk_valid) {
            clk_base_ms = now;
            clk_base_cyc = mach_cyc;
            clk_valid = 1;
            return;
        }
        due = clk_base_ms + ((double)mach_cyc - (double)clk_base_cyc) * 1000.0 / BZ_CPU_HZ;
        if (now - due > BZ_STALL_MS) {  /* debugger / dragged window */
            clk_base_ms = now;
            clk_base_cyc = mach_cyc;
            return;
        }
        if (now >= due) return;
        machine_idle(due - now);
    }
}

/* ------------------------------------------------------------------ */
/* app hooks                                                           */
/* ------------------------------------------------------------------ */

double bz_app_idle_ms(void) { return idle_remain_ms; }

void bz_app_init(void)
{
    memset(&g, 0, sizeof g);
    memset(&cur_in, 0, sizeof cur_in);
    mach_cyc = 0;
    next_nmi = BZ_NMI_CYCLES;
    in_selftest = 0;
    vg_running = 0;
    sound_latch = 0;
    clk_valid = 0;
    mb_reset(&mb);
    ad_pokey_init(&pokey, 1512000u, BZ_AUDIO_RATE);
    ad_pokey_set_measured_audio(&pokey, 20.0, 1.5);
    plat_input_poll(&cur_in);
    inputs_apply();
    audio_live = plat_audio_open(BZ_AUDIO_RATE) == 0;
    if (!audio_live) fprintf(stderr, "plat_audio_open failed; continuing without sound\n");
    bz_sounds_init();
    if (pwron() != 0)
        in_selftest = 1;  /* self-test switch is on (bzstst.c) */
    else
        main_init();
}

/* The board's RESET line (watchdog bite, or F3): the CPU restarts at PWRON,
 * the Mathbox and the AVG are reset. */
static void board_reset(void)
{
    mb_reset(&mb);
    vg_running = 0;
    last_kick_cyc = mach_cyc;
    in_selftest = (pwron() != 0);
    if (!in_selftest) main_init();
}

double bz_app_step(double now_ms)
{
    static double last_status_ms;
    (void)now_ms;
    static uint8_t last_test, last_reset;
    plat_input_poll(&cur_in);
    inputs_apply();
    /* The game never reads the self-test switch - only PWRON does - so on
     * the board you flip it and power-cycle.  Host affordance (MAME's F2 +
     * reset): switching it ON during the game reboots through PWRON, which
     * then enters the self test.  Switching it OFF in the self test needs
     * nothing here: the ROM's own STEST7 sees it and does JMP PWRON. */
    if (cur_in.test && !last_test && !in_selftest) {
        in_selftest = (pwron() != 0);
        if (!in_selftest) main_init();
    }
    /* F3: the board's RESET (host affordance, MAME's F3): restart at PWRON,
     * which starts the game or the self test by the switch - the way out of
     * the diag-step screens, which only listen to the diag button. */
    if (cur_in.reset && !last_reset) board_reset();
    last_reset = cur_in.reset;
    last_test = cur_in.test;
    if (setjmp(wd_jmp)) {
        /* The watchdog bit (spend()): the board's RESET line. */
        n_wd_resets++;
        board_reset();
    } else {
        wd_armed = 1;
        if (in_selftest) {
            if (selftest_pass()) {  /* 1 = the ROM did "JMP PWRON" */
                in_selftest = (pwron() != 0);
                if (!in_selftest) main_init();
            }
        } else {
            main_pass();
        }
        wd_armed = 0;
    }
    if (in_selftest) machine_wait_cyc();
    n_passes++;
    g.pass_count++;
    {
        double t = plat_now_ms();
        if (t - last_status_ms >= 1000.0) {
            char s[160];
            snprintf(s, sizeof s, "passes %lu  NMIs %lu  pictures %lu  lit %lu  bbox x %.0f..%.0f y %.0f..%.0f",
                     n_passes, n_nmi, n_pictures, n_lit,
                     (double)bbox_minx, (double)bbox_maxx, (double)bbox_miny, (double)bbox_maxy);
            plat_status_text(s);
            last_status_ms = t;
        }
    }
    return 0.0;
}

void bz_app_exit(void)
{
    if (audio_live) plat_audio_close();
    audio_live = 0;
}

/* Read-only stats for test drivers (not part of the platform contract). */
void bz_app_stats(unsigned long *passes, unsigned long *nmis, unsigned long *pictures,
                  unsigned long *lit, float bbox[4])
{
    if (passes)   *passes = n_passes;
    if (nmis)     *nmis = n_nmi;
    if (pictures) *pictures = n_pictures;
    if (lit)      *lit = n_lit;
    if (bbox) { bbox[0] = bbox_minx; bbox[1] = bbox_maxx; bbox[2] = bbox_miny; bbox[3] = bbox_maxy; }
}
