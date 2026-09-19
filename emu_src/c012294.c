/* c012294.c - the POKEY core; see c012294.h for the host interface,
 * and for the time model ad_pokey_advance()/ad_pokey_read() share.
 *
 * Translated from the AAE emulator's engine-free POKEY core (Pokey and
 * PokeyHost), minus the AAE adapter (pokey_sh_*, mixer/stream/timer
 * calls, Read_pokey_regs, quad-pokey, MEM callbacks) - that layer is
 * AAE's engine wiring, not chip behaviour.  The timers/IRQ/serial/
 * keyboard/pot pieces are here, driven through ad_pokey_host in place of
 * AAE's virtual PokeyHost.  The audio poly tables and the SKCTL hold on
 * the audio side follow MAME's pokey.cpp (0.286,
 * src/devices/sound/pokey.cpp) - credit to the MAME team for the LFSR
 * arithmetic.  The pot scanner follows the Altirra Hardware Reference
 * (Avery Lee), which measured the real chip.  The RANDOM shift chain -
 * its registers, what SKCTL's init bits do to it clock by clock, and
 * which timers keep counting while it is held - follows a gate-level
 * transcription of Atari's schematics (Nick Mikstas's atari_pokey,
 * poly_core.v, clock_gen_core.v, freq_control.v) - see c012294.h's
 * ad_rng_chain.
 *
 * The parts taken from MAME's pokey.cpp are used under that file's
 * BSD-3-Clause terms, copyright the MAME team and the copyright holders
 * it names (Brad Oliver, Eric Smith, Juergen Buchmueller, and others):
 * redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that redistributions of source
 * code retain this notice, that redistributions in binary form
 * reproduce it in the documentation, and that the names of the
 * copyright holders are not used to endorse derived products without
 * permission; the software is provided "as is" without warranty.  The
 * rest of this file is under the project's licence (see LICENSE).
 */
#include <string.h>
#include <math.h>

#include "c012294.h"

static void notify_pins(ad_pokey *p);
static void notify_clocks(ad_pokey *p, bool force);
static void pot_step(ad_pokey *p);
static void cycle_audio_step(ad_pokey *p, const uint32_t *borrows);
static void cycle_audio_sample(ad_pokey *p);
static int32_t cycle_audio_level(const ad_pokey *p);
static void cycle_audio_integrate(ad_pokey *p, int32_t level, uint32_t halves);
static void audio_queue_clear(ad_pokey *p);
static void audio_playback_config(ad_pokey *p, double dc_hz, double gain);
static void cycle_audio_noise_clock(ad_pokey *p, bool running);

/* ------------------------------------------------------------------ */
/* Poly/RNG tables - built once, on first use                          */
/* ------------------------------------------------------------------ */
/* The real chip's LFSRs, all maximal-length (2^n-1 states; probe_pokey.c
 * check (1) proves it).  poly4/5 are a Fibonacci LFSR with XNOR
 * feedback on bits 2 and size-1, seeded from 0.  poly9/17 feed bit0 XOR
 * bit5 back into bit8 (9-bit), seeded from all ones; the 17-bit case is
 * that 9-bit LFSR extended by an 8-bit shift register, folding bit8 XOR
 * bit13 back into bit7 of the low byte.  The render indexes g_polyN for
 * the audio toggle bit (`& 1`).  RANDOM does not use a table: it reads
 * the shift chain itself (the "RANDOM shift chain" section below) - the
 * g_randN slices (`& 0xff` for 9-bit, `>> 8 & 0xff` for 17-bit) are
 * built only for the probe, which checks the chain against them.
 *
 * Shared tables are initialized on first use and require about 129 KiB
 * for audio alone. Initialize chips serially before starting worker
 * threads; table initialization is not synchronized. */

static uint8_t g_poly4[15];
static uint8_t g_poly5[31];
static uint8_t g_poly9[511];
static uint8_t g_poly17[131071];
#ifdef AD_PROBE
static uint8_t g_rand9[511];
static uint8_t g_rand17[131071];
#endif
static bool    g_tables_built = false;
/* The DAC transfer curve, indexed by the summed channel weights (0..412,
 * units of 0.02 V), PCM units, negative-going: the chip pulls its output
 * DOWN from the pull-up as volume rises.  Built once with the polys. */
static int16_t g_dac_curve[413];

/* Fibonacci LFSR: each step folds bits 2 and (size-1) of the running
 * state through XNOR into a new bit shifted in at position 0; only the
 * table entry is masked down to `size` bits; the running `lfsr` is left
 * to grow (it never affects the tap bits, which stay at fixed low
 * offsets from the shifted-in end). */
static void poly_init_4_5(uint8_t *poly, int size)
{
    uint32_t mask = (1u << size) - 1;
    uint32_t lfsr = 0;
    int xorbit = size - 1;
    for (uint32_t i = 0; i < mask; ++i) {
        uint32_t newbit = (~((lfsr >> 2) ^ (lfsr >> xorbit))) & 1u;
        lfsr = (lfsr << 1) | newbit;
        poly[i] = (uint8_t)((lfsr & mask) & 1u);
    }
}

/* Writes both slices of each state in the same pass (see the table
 * comment above): g_polyN gets the toggle bit, g_randN (probe builds
 * only; NULL otherwise) gets the RANDOM byte.  Seeded from lfsr = mask
 * (all ones): in this register's polarity that is the complement of the
 * chip's all-zero 9-bit register, so the tables line up with the chain
 * (see chain_to_vec()). */
static void poly_init_9_17(uint8_t *poly, uint8_t *rnd, int size)
{
    uint32_t mask = (size == 17) ? 0x1FFFFu : 0x1FFu;
    uint32_t lfsr = mask;
    for (uint32_t i = 0; i < mask; ++i) {
        uint8_t byte;
        if (size == 17) {
            uint32_t in8 = ((lfsr >> 8) & 1u) ^ ((lfsr >> 13) & 1u);
            uint32_t in  = lfsr & 1u;
            lfsr >>= 1;
            lfsr = (lfsr & 0xFF7Fu) | (in8 << 7);
            lfsr = (in << 16) | lfsr;
            byte = (uint8_t)((lfsr >> 8) & 0xFFu);
        } else {
            uint32_t in = (lfsr & 1u) ^ ((lfsr >> 5) & 1u);
            lfsr >>= 1;
            lfsr = (in << 8) | lfsr;
            byte = (uint8_t)(lfsr & 0xFFu);
        }
        if (rnd)
            rnd[i] = byte;
        poly[i] = (uint8_t)(lfsr & 1u);
    }
}

/* ------------------------------------------------------------------ */
/* RANDOM shift chain                                                  */
/* ------------------------------------------------------------------ */
/* The chip's 9/17-bit polynomial, register for register from
 * poly_core.v (see ad_rng_chain in c012294.h for the register names).
 * Hardware polarity: the 9-bit register's bits are the complement of
 * the RANDOM byte, the XNOR of its bits 5 and 0 feeds the 17-bit
 * extension, and the head of the 9-bit register takes the NOR of the
 * three registered switch outputs - or a zero while the SKCTL init
 * bits are clear (Init).  With the 17-bit poly selected the switch
 * routes bit 0 of the extension round to the head (17 stages counting
 * the switch's own flop); with the 9-bit poly it routes the XNOR
 * straight round (9 stages), and the extension keeps shifting unseen.
 * The clock never stops: Init and the select only change the feeds.
 *
 * Holding Init shifts a zero into the head every clock.  After eight
 * the 9-bit register is clear (RANDOM 0xFF), from the ninth the XNOR of
 * two zeros feeds ones into the extension, and after seventeen the
 * whole chain is at rest: it stays there for as long as the hold lasts,
 * and a release inside those seventeen clocks resumes from whatever mix
 * of old and new bits the chain holds at that moment.  The one-clock
 * blank when the select flips (nors[1]) and the clock's delay on the
 * select (swDelay) are in here too, so flipping AUDCTL's poly bit
 * mid-run does what the chip does. */

/* One clock of the chain: the negedge always block of poly_core.v. */
static void chain_step(ad_rng_chain *c, bool init, bool sel9)
{
    uint32_t fb917 = (((c->l9 >> 5) ^ c->l9) & 1u) ^ 1u;        /* ~(l9[5] ^ l9[0]) */
    uint32_t nors0 = ((c->l17 & 1u) | (uint32_t)sel9) ^ 1u;     /* ~(l17[0] | sel9) */
    uint32_t nors1 = ((uint32_t)c->swdelay | (uint32_t)!sel9) ^ 1u; /* ~(swDelay | ~sel9) */
    uint32_t nors2 = ((uint32_t)!sel9 | fb917) ^ 1u;            /* ~(~sel9 | fb917) */
    uint32_t swout = ((uint32_t)init | (uint32_t)(c->nd != 0)) ^ 1u; /* ~(Init | nD[0..2]) */
    c->l9      = (uint8_t)((c->l9 >> 1) | (swout << 7));
    c->l17     = (uint8_t)((c->l17 >> 1) | (fb917 << 7));
    c->swdelay = (uint8_t)sel9;
    c->nd      = (uint8_t)(nors0 | (nors1 << 1) | (nors2 << 2));
}

/* The chain as ad_pokey_reset() leaves it: at rest under a long hold
 * with the 17-bit poly selected (AUDCTL = 0), the state a chip that has
 * seen SKCTL = 0 for seventeen clocks is in. */
static void chain_reset(ad_rng_chain *c)
{
    c->l9 = 0; c->l17 = 0xFF; c->swdelay = 0; c->nd = 0;
}

/* Avery Lee, Altirra Hardware Reference Appendix E.2:
 * measured bit drops .12/.26/.56/1.12V. The exponential is a
 * hand-fitted combined-channel approximation, not resistor values.
 * https://www.virtualdub.org/downloads/Altirra%20Hardware%20Reference%20Manual.pdf
 * 4*(.12+.26+.56+1.12) = 8.24V, or 412 units of .02V. */
static void dac_curve_init(void)
{
    for (int i = 0; i <= 412; ++i) {
        double x = (double)i / 412.0;
        double y = 2.171 * (x <= 0.14 ? x :
            0.14 + (1.0 - exp(-2.85 * (x - 0.14))) / 2.85);
        if (y > 1.0) y = 1.0; /* rounded fit overshoots by ~0.000026 */
        g_dac_curve[i] = (int16_t)-(int32_t)(32767.0 * y + 0.5);
    }
}

static void build_tables(void)
{
    if (g_tables_built)
        return;
    dac_curve_init();
    poly_init_4_5(g_poly4, 4);
    poly_init_4_5(g_poly5, 5);
#ifdef AD_PROBE
    poly_init_9_17(g_poly9, g_rand9, 9);
    poly_init_9_17(g_poly17, g_rand17, 17);
#else
    poly_init_9_17(g_poly9, NULL, 9);
    poly_init_9_17(g_poly17, NULL, 17);
#endif
    g_tables_built = true;
}

#ifdef AD_PROBE
const uint8_t *ad_pokey_dbg_poly4(void)  { build_tables(); return g_poly4;  }
const uint8_t *ad_pokey_dbg_poly5(void)  { build_tables(); return g_poly5;  }
const uint8_t *ad_pokey_dbg_poly9(void)  { build_tables(); return g_poly9;  }
const uint8_t *ad_pokey_dbg_poly17(void) { build_tables(); return g_poly17; }
const uint8_t *ad_pokey_dbg_rand9(void)  { build_tables(); return g_rand9;  }
const uint8_t *ad_pokey_dbg_rand17(void) { build_tables(); return g_rand17; }
#endif

/* ------------------------------------------------------------------ */
/* Channel period, exactly as documented in aae_pokey.cpp               */
/* ------------------------------------------------------------------ */
/* Returns the TRUE half-period in base-clock ticks. */
static uint32_t channel_period(const uint8_t *AUDF, uint8_t AUDCTL, uint32_t base_mult, int ch)
{
    bool hi1 = (AUDCTL & CTL_CH1_HICLK) != 0;
    bool hi3 = (AUDCTL & CTL_CH3_HICLK) != 0;
    uint32_t d;
    switch (ch) {
    case 0: d = hi1 ? (uint32_t)(AUDF[0] + 4) : (uint32_t)((AUDF[0] + 1) * base_mult); break;
    case 1:
        if (AUDCTL & CTL_CH12_JOIN)
            d = hi1 ? (uint32_t)(AUDF[1] * 256 + AUDF[0] + 7)
                    : (uint32_t)((AUDF[1] * 256 + AUDF[0] + 1) * base_mult);
        else
            d = (uint32_t)((AUDF[1] + 1) * base_mult);
        break;
    case 2: d = hi3 ? (uint32_t)(AUDF[2] + 4) : (uint32_t)((AUDF[2] + 1) * base_mult); break;
    case 3:
        if (AUDCTL & CTL_CH34_JOIN)
            d = hi3 ? (uint32_t)(AUDF[3] * 256 + AUDF[2] + 7)
                    : (uint32_t)((AUDF[3] * 256 + AUDF[2] + 1) * base_mult);
        else
            d = (uint32_t)((AUDF[3] + 1) * base_mult);
        break;
    default: return 1;
    }
    return d ? d : 1;
}

static void recompute_channel(ad_pokey *p, int ch)
{
    if (ch < 0 || ch >= 4)
        return;
    p->divisor[ch] = channel_period(p->AUDF, p->AUDCTL, p->base_mult, ch);
}

static void recompute_all(ad_pokey *p)
{
    for (int i = 0; i < 4; ++i)
        recompute_channel(p, i);
}

/* Hardware timer index (0,1,2 = TIMR1/TIMR2/TIMR4) -> the AUDF channel
 * that drives its period, and its IRQEN/IRQST bit.  As AAE's
 * timer_channel()/timer_irq_bit(); the IRQ_TIMR1/2/4 bits are 1<<w by
 * definition. */
static int timer_channel(int which)
{
    return which == 2 ? 3 : which == 3 ? 2 : which;
}

static uint8_t timer_irq_bit(int which)
{
    return (uint8_t)(1u << which); /* bit 3 is internal only, never an IRQ */
}

/* Shared slow-clock phase -------------------------------------------------
 *
 * The 64KHz and 15KHz clocks are common POKEY clock sources, not private
 * divide-by-N delays that restart with each timer write.  Altirra HRM 5.4:
 * their phase is set by leaving initialization mode and remains locked until
 * init is re-entered.  HRM 5.2 gives the directly observable anchor for a
 * fully reset clock with AUDF=0: IRQST asserts 24 cycles after leaving init on
 * the 64KHz clock and 83 cycles after on the 15KHz clock.  The timer IRQ
 * pipeline in this core is independently measured at four cycles. The current
 * source anchors below are 21/80, giving IRQST at 25/84 in this API. Acid800
 * 1.2 brackets reads at 83/84 (15K) and 80/81 (64K, AUDF=2). Altirra 4.40
 * independently schedules IRQST at 25/84 (22/81 source tick + 3 borrow).
 * HRM's 24/83 sentence disagrees with these observable boundaries. Preserve
 * the tested timing; see docs/POKEY-SLOW-CLOCK-EVIDENCE.md. This comparison
 * establishes API deadlines, not the physical chip's internal latch phases.
 * Subsequent pulses are exactly 28 / 114 cycles apart.
 *
 * Keep the timer countdown itself in machine-cycle distance (tcnt[]) because
 * the rest of this core and the serial/two-tone paths already consume it that
 * way.  The only requirement for a slow timer is that tcnt always land on a
 * shared source-clock pulse.  Rearms therefore align to slow_clock_delay(),
 * and an init hold saves the number of remaining source pulses before
 * resetting/rephasing the clocks. */
#define SLOW64_FIRST_DELAY 21u
#define SLOW15_FIRST_DELAY 80u

static bool timer_fast_clock(const ad_pokey *p, int which)
{
    switch (which) {
    case 0:  return (p->AUDCTL & CTL_CH1_HICLK) != 0;
    case 1:  return (p->AUDCTL & (CTL_CH12_JOIN | CTL_CH1_HICLK)) ==
                    (CTL_CH12_JOIN | CTL_CH1_HICLK);
    case 3: return (p->AUDCTL & CTL_CH3_HICLK) != 0;
    default: return (p->AUDCTL & (CTL_CH34_JOIN | CTL_CH3_HICLK)) ==
                    (CTL_CH34_JOIN | CTL_CH3_HICLK);
    }
}

static uint32_t slow_clock_period(const ad_pokey *p)
{
    return (p->AUDCTL & CTL_CLK15) ? DIV_15 : DIV_64;
}

static uint64_t slow_clock_next_abs(const ad_pokey *p)
{
    return (p->AUDCTL & CTL_CLK15) ? p->slow_next_15 : p->slow_next_64;
}

/* Cycles from the current machine time to the NEXT selected slow-clock
 * pulse.  slow_next_* is normally already in the future.  The normalization
 * also makes this safe inside an aggregate advance() before the stored phase
 * has been rolled forward to the end of the slice. */
static uint32_t slow_clock_delay(const ad_pokey *p)
{
    const uint32_t period = slow_clock_period(p);
    uint64_t next = slow_clock_next_abs(p);

    if (!next)
        return (p->AUDCTL & CTL_CLK15) ? SLOW15_FIRST_DELAY : SLOW64_FIRST_DELAY;
    if (next > p->cycles)
        return (uint32_t)(next - p->cycles);

    uint32_t mod = (uint32_t)((p->cycles - next) % period);
    return mod ? (period - mod) : period;
}

static uint32_t slow_divisor_ticks(const ad_pokey *p, int w)
{
    const uint32_t period = slow_clock_period(p);
    const uint32_t div = p->divisor[timer_channel(w)];
    uint32_t ticks = (div + period - 1) / period;
    return ticks ? ticks : 1;
}

/* Convert an aligned machine-cycle countdown back into the number of shared
 * source pulses still required.  This is what init must preserve: HRM 5.2
 * explicitly says the 15/64K clocks reset but the timer counters do not. */
static uint32_t slow_remaining_ticks(const ad_pokey *p, int w)
{
    const uint32_t period = slow_clock_period(p);
    const uint32_t delay = slow_clock_delay(p);
    const uint32_t count = p->tcnt[w];
    if (count <= delay)
        return 1;
    return 1 + (count - delay + period - 1) / period;
}

static void slow_clocks_enter_init(ad_pokey *p)
{
    for (int w = 0; w < 4; ++w)
        if (!timer_fast_clock(p, w))
            p->slow_hold_ticks[w] = slow_remaining_ticks(p, w);

    /* Both polynomial source clocks are held in their reset states in init. */
    p->slow_next_64 = 0;
    p->slow_next_15 = 0;
}

static void slow_clocks_leave_init(ad_pokey *p)
{
    p->slow_next_64 = p->cycles + SLOW64_FIRST_DELAY;
    p->slow_next_15 = p->cycles + SLOW15_FIRST_DELAY;

    const uint32_t period = slow_clock_period(p);
    const uint32_t first = (p->AUDCTL & CTL_CLK15) ?
        SLOW15_FIRST_DELAY : SLOW64_FIRST_DELAY;

    for (int w = 0; w < 4; ++w) {
        if (timer_fast_clock(p, w))
            continue;
        uint32_t ticks = p->slow_hold_ticks[w];
        if (!ticks)
            ticks = slow_divisor_ticks(p, w);
        p->tcnt[w] = first + (ticks - 1) * period;
    }
}

static void slow_clocks_advance_phase(ad_pokey *p)
{
    if (!p->rng_enabled)
        return;

    if (p->slow_next_64 && p->slow_next_64 <= p->cycles) {
        uint64_t n = (p->cycles - p->slow_next_64) / DIV_64 + 1;
        p->slow_next_64 += n * DIV_64;
    }
    if (p->slow_next_15 && p->slow_next_15 <= p->cycles) {
        uint64_t n = (p->cycles - p->slow_next_15) / DIV_15 + 1;
        p->slow_next_15 += n * DIV_15;
    }
}

/* Does timer w count this slice?  Always while the chip runs.  Held
 * (SKCTL init bits clear), the 15 kHz and 64 kHz clocks stop but the
 * 1.79 MHz one does not (clock_gen_core.v holds its two clock LFSRs on
 * Init; freq_control.v's carry for channels 1 and 3 is the fast-clock
 * enable OR the slow clock), so a channel on the fast clock - and the
 * joined partner it clocks - keeps counting. */
static bool timer_runs(const ad_pokey *p, int which)
{
    if (p->rng_enabled) {
        /* Asynchronous receive (SK_ASYNC, SKCTL bit 4) holds timer 4
         * (which == 2, tcnt[2], the same hardware counter as "timer 3+4"
         * when joined) in reset while the receiver is idle, independent
         * of AUDCTL's clock/join bits.  Altirra HRM 5.6, "Asynchronous
         * receive mode": "timers 3 and 4 are held in reset state while
         * POKEY is waiting for a start bit, allowing the timers to run
         * only once a start bit is detected."  sdi_busy is that "waiting
         * for a start bit" flag: false until a frame starts, so this
         * only applies at idle, not mid-reception. */
        if ((which == 2 || which == 3) && (p->SKCTL & SK_ASYNC) && !p->sdi_busy)
            return false;
        return true;
    }
    switch (which) {
    case 0:  return (p->AUDCTL & CTL_CH1_HICLK) != 0;
    case 1:  return (p->AUDCTL & (CTL_CH12_JOIN | CTL_CH1_HICLK)) == (CTL_CH12_JOIN | CTL_CH1_HICLK);
    case 3: return (p->AUDCTL & CTL_CH3_HICLK) != 0;
    default: return (p->AUDCTL & (CTL_CH34_JOIN | CTL_CH3_HICLK)) == (CTL_CH34_JOIN | CTL_CH3_HICLK);
    }
}

/* Latch a fired IRQ into IRQST and tell the host, if either is wired up
 * to hear about it.  IRQST only ever gets bits ORed in here; a write to
 * IRQEN or SKREST is what clears them (see ad_pokey_write()). */
/* Cycles between a timer's borrow and its interrupt appearing in IRQST - the
 * underflow logic's pipeline stages.  See timer_irq_pending in c012294.h. */
#define TIMER_IRQ_STAGE_DELAY 4

static void fire_irq(ad_pokey *p, uint8_t mask)
{
    /* A disabled interrupt's status bit is HELD reset - it is not merely
     * cleared once by the IRQEN write.  Altirra HRM 5.7: "With the exception
     * of bit 3, the status bit for a disabled interrupt is always locked to a
     * 1.  There is no interrupt queuing for a disabled interrupt - any
     * interrupts that would have triggered while an interrupt is disabled are
     * lost."  (IRQST reads active-low, so "locked to a 1" is this latch held
     * at 0.)  The IRQST register reference says the same: "Most bits in IRQST
     * are reset and stay low when the corresponding interrupt is cleared via
     * IRQEN."  atari800 7.1.2 pokey.c:631/642/653 gates each timer the same
     * way, testing POKEY_IRQEN at the moment the bit is asserted.
     *
     * This matters because a timer's borrow reaches IRQST four cycles later
     * (TIMER_IRQ_STAGE_DELAY).  Gating only at the borrow lets an in-flight
     * interrupt land in IRQST after IRQEN has already disabled it, so IRQST
     * comes back set a few cycles after the write that cleared it.  That is
     * exactly what acid800 pokey_addrmirror measures, and whether it hit
     * depended on where the write happened to fall in the timer period.
     * Bit 3 (SEROC) is the documented exception, but it is never latched here
     * at all - ad_pokey_read() derives it live from sdo_idle(). */
    /* Do not apply the CPU interrupt-entry latency to the IRQST latch.
     * HRM 5.7's enable-delay example says "the IRQ handler would trigger";
     * acid800 distinguishes IRQST timing from the CPU's later IRQ entry.
     * pokey_timertiming enables at STIMER+42 and requires the +44 latch to
     * survive (AUDF1=8, two-tone, late AUDF write at +31). A four-cycle gate
     * here loses that event even though the AUDF reload correctly missed the
     * write. CPU recognition latency belongs to the host's sync_irq_line(). */
    mask &= (uint8_t)(p->IRQEN | IRQ_SEROC);
    if (!mask)
        return;

    p->IRQST |= mask;
    if (p->host && p->host->raise_irq)
        p->host->raise_irq(p->host->ctx, mask);
    notify_pins(p);
}

/* The serial port and SKSTAT, defined in their own section below. */
static bool    sdo_idle(const ad_pokey *p);
static bool    sdo_line(const ad_pokey *p);
static uint8_t skstat_read(const ad_pokey *p);
static void    serial_step(ad_pokey *p, const uint32_t *borrows, uint8_t timer_completed);

/* Reload timer w.  Fast timers restart a machine-cycle countdown.  Slow
 * timers reload their counter too, but the shared 64/15KHz clock phase is
 * independent of this write (Altirra HRM 5.3, STIMER): align the resulting
 * underflow to the next source-clock pulse instead of starting DIV_64/DIV_15
 * from the write cycle. */
static void rearm_timer(ad_pokey *p, int w)
{
    p->timer_reload_delay[w] = 3;
    if (timer_fast_clock(p, w)) {
        p->tcnt[w] = p->divisor[timer_channel(w)];
        return;
    }

    const uint32_t period = slow_clock_period(p);
    const uint32_t ticks = slow_divisor_ticks(p, w);
    p->slow_hold_ticks[w] = ticks;

    if (!p->rng_enabled) {
        /* The source clock is frozen in init.  Keep a conventional raw value
         * for diagnostics; slow_clocks_leave_init() will re-anchor it. */
        p->tcnt[w] = p->divisor[timer_channel(w)];
        return;
    }

    p->tcnt[w] = slow_clock_delay(p) + (ticks - 1) * period;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

void ad_pokey_reset(ad_pokey *p)
{
    for (int i = 0; i < 4; ++i) {
        p->AUDF[i] = p->AUDC[i] = 0;
        p->out[i] = 0;
    }
    p->AUDCTL = 0;
    p->base_mult = DIV_64;
    /* The divisors follow the cleared registers: AUDF 0 on the 64 kHz clock
     * is a one-pulse period (28 clocks), which is what the counters start
     * from below.  Seeding them with the whole base clock instead only
     * worked while an AUDCTL write re-armed every timer. */
    recompute_all(p);
    p->p4 = p->p5 = p->p9 = p->p17 = 0;
    p->SKCTL = 0;
    p->pot_scanning = false;
    p->pot_scan_ever = false;
    p->pot_scan_start = 0;
    p->pot_count = p->pot_previous = 0;
    p->pot_clear_delay = p->pot_finish_delay = 0;
    p->pot_transition = false;
    p->pot_next_tick = 0;
    memset(p->pot_latch, 0, sizeof p->pot_latch);
    p->rng_enabled = 0;
    p->rng_init_prev = 1;   /* reset leaves the chip held, so Init is asserted */
    chain_reset(&p->rng);
    /* Each timer starts a full period away - one source pulse, from the
     * cleared registers above - and the held chip remembers that count
     * for the release. */
    p->slow_next_64 = 0;
    p->slow_next_15 = 0;
    for (int i = 0; i < 4; ++i) {
        p->timer_reload_delay[i] = 0;
        p->tcnt[i] = p->divisor[timer_channel(i)];
        p->slow_hold_ticks[i] = (p->tcnt[i] + DIV_64 - 1) / DIV_64;
        if (!p->slow_hold_ticks[i]) p->slow_hold_ticks[i] = 1;
    }
    p->IRQEN = p->IRQST = 0;
    p->timer_irq_pending = 0;
    memset(p->timer_irq_delay, 0, sizeof p->timer_irq_delay);
    p->twotone_delay = 0;
    p->KBCODE = 0;
    p->kb_down = p->kb_shift = false;
    p->SERIN = p->SEROUT = 0;
    p->sdo_pending = p->sdo_busy = false;
    p->sdo_byte = 0; p->sdo_left = 0;
    p->sdi_busy = false;
    p->sdi_byte = 0; p->sdi_left = 0;
    p->sdi_line_in = 1;              /* the line idles at mark */
    p->sdi_host_line = 0;
    p->sdi_src_busy = false;
    p->sdi_src_byte = 0; p->sdi_src_left = 0;
    p->sdi_shift = 0; p->sdi_stop_ok = true;
    p->st_latch = 0;
    p->external_clock = 0;
    p->clock_out_phase = p->clock_bi_phase = 0;
    p->serial_output_delay = 0;
    p->cassette_level = 1;
    audio_queue_clear(p);
    p->highpass_latch[0] = p->highpass_latch[1] = 1;
    memset(p->highpass_delay, 0, sizeof p->highpass_delay);
    p->noise4_history = p->noise917_history = 0;
    p->noise5_history = 0xFF;
    notify_pins(p);
    notify_clocks(p, false);
    /* p->cycles, p->allpot and p->host are this port's stand-ins for
     * host-owned state (the machine clock, the DIP bank, the callback
     * wiring) - a chip reset doesn't touch any of them, same as
     * Pokey::reset() never touches its host. */
}

void ad_pokey_init(ad_pokey *p, uint32_t clock_hz, uint32_t sample_rate)
{
    memset(p, 0, sizeof *p);
    p->base_clock = clock_hz ? clock_hz : 1;
    p->sys_freq = sample_rate ? sample_rate : 1;
    p->quiet_skip = true;
    build_tables();
    /* The measured DAC is the chip; 20 Hz DC removal at unity gain is the
     * default playback stage (ad_pokey_set_measured_audio() changes it). */
    audio_playback_config(p, 20.0, 1.0);
    ad_pokey_reset(p);
}

void ad_pokey_set_quiet_skip(ad_pokey *p, bool enabled)
{
    p->quiet_skip = enabled;
}

void ad_pokey_set_allpot(ad_pokey *p, uint8_t v)
{
    if (p->pot_counter_mode && p->pot_scanning) {
        for (int i = 0; i < 8; ++i)
            if ((p->allpot & (1u << i)) && !(v & (1u << i)))
                p->pot_latch[i] = p->pot_count;
    }
    p->allpot = v;
}

void ad_pokey_set_pot_scan(ad_pokey *p, bool enabled)
{
    p->pot_counter_mode = enabled;
}

static void pot_step(ad_pokey *p)
{
    if (!p->pot_counter_mode || !p->pot_scanning) return;
    p->pot_transition = false;
    if (p->pot_finish_delay && --p->pot_finish_delay == 0) {
        p->pot_scanning = false;
        return;
    }
    if (p->pot_clear_delay) {
        --p->pot_clear_delay;
        p->pot_count = 0;
    } else {
        bool tick = (p->SKCTL & SK_FASTPOT) != 0;
        if (!tick && p->rng_enabled && p->pot_next_tick == p->cycles) tick = true;
        if (tick) {
            p->pot_previous = p->pot_count;
            ++p->pot_count;
            p->pot_transition = true;
            if (p->pot_count == 228) p->pot_finish_delay = 2;
            if (p->pot_count == 229) p->pot_transition = false;
        }
    }
    /* The pot increment follows the shared 15K source pulse by one clock. */
    if (p->rng_enabled && p->slow_next_15 == p->cycles)
        p->pot_next_tick = p->cycles + 1;
    for (int i = 0; i < 8; ++i)
        if (p->allpot & (1u << i)) p->pot_latch[i] = p->pot_count;
}

void ad_pokey_set_host(ad_pokey *p, const ad_pokey_host *h)
{
    p->host = h;
}

bool ad_pokey_irq_asserted(const ad_pokey *p)
{
    uint8_t pending = p->IRQST & (uint8_t)~IRQ_SEROC;
    if (!p->sdo_busy) pending |= IRQ_SEROC;
    return (pending & p->IRQEN) != 0;
}

static void notify_pins(ad_pokey *p)
{
    uint8_t irq = ad_pokey_irq_asserted(p) ? 1 : 0;
    uint8_t level = (p->SKCTL & SK_TWOTONE) ? p->cassette_level : (uint8_t)sdo_line(p);
    if (irq != p->irq_level) {
        p->irq_level = irq;
        if (p->io && p->io->irq_line) p->io->irq_line(p->io->ctx, irq, p->cycles);
    }
    if (level != p->serial_level) {
        p->serial_level = level;
        if (p->io && p->io->serial_output) p->io->serial_output(p->io->ctx, level, p->cycles);
    }
}

void ad_pokey_set_io(ad_pokey *p, const ad_pokey_io *io)
{
    p->io = io;
    p->irq_level = (uint8_t)ad_pokey_irq_asserted(p);
    p->serial_level = (p->SKCTL & SK_TWOTONE) ? p->cassette_level : (uint8_t)sdo_line(p);
    if (io && io->irq_line) io->irq_line(io->ctx, p->irq_level, p->cycles);
    if (io && io->serial_output) io->serial_output(io->ctx, p->serial_level, p->cycles);
}

static void notify_clocks(ad_pokey *p, bool force)
{
    const uint8_t driven = (p->SKCTL & 0x30) == 0x20;
    const uint8_t out = (p->SKCTL & 0x60) ? p->clock_out_phase : p->external_clock;
    const uint8_t bi = driven ? p->clock_bi_phase : p->external_clock;
    if (force || out != p->clock_out_level) {
        p->clock_out_level = out;
        if (p->clocks && p->clocks->output)
            p->clocks->output(p->clocks->ctx, out, p->cycles);
    }
    if (force || bi != p->clock_bi_level || driven != p->clock_bi_driven) {
        p->clock_bi_level = bi;
        p->clock_bi_driven = driven;
        if (p->clocks && p->clocks->bidirectional)
            p->clocks->bidirectional(p->clocks->ctx, bi, driven, p->cycles);
    }
}

void ad_pokey_set_clocks(ad_pokey *p, const ad_pokey_clocks *clocks)
{
    p->clocks = clocks;
    notify_clocks(p, true);
}

/* One machine clock. The RANDOM chain clocks every
 * cycle, held or not (what it takes in differs); the four hardware
 * timers count while the chip runs, and while held only on the fast
 * clock (timer_runs()).  See c012294.h's time-model note for who calls
 * this and with what. */
static void advance_clock(ad_pokey *p)
{
    const uint32_t cycles = 1;
    cycle_audio_sample(p);
    cycle_audio_sample(p);
    p->cycles += cycles;
    build_tables();

    /* The chain sees Init one clock late. SKCTL's register and the chain both
     * move on the same edge, so the edge on which a write lands still uses the
     * pre-write Init; only the following clock sees the new value. When the
     * signal has just changed, this clock uses the saved value and the next
     * uses the new one. See rng_init_prev in c012294.h for the measured
     * datapoint this is pinned to ($1F four clocks after leaving init). */
    {
        chain_step(&p->rng, p->rng_init_prev != 0, (p->AUDCTL & CTL_POLY9) != 0);
        p->rng_init_prev = (uint8_t)!p->rng_enabled;
    }

    /* Retire each timer's IRQ before admitting this clock's new borrows.
     * A later timer event cannot postpone an earlier timer's deadline. */
    uint8_t due = 0;
    for (int w = 0; w < 3; ++w) {
        const uint8_t bit = timer_irq_bit(w);
        if ((p->timer_irq_pending & bit) && --p->timer_irq_delay[w] == 0) {
            p->timer_irq_pending &= (uint8_t)~bit;
            due |= bit;
        }
    }
    if (due) fire_irq(p, due);

    uint32_t borrows[4] = { 0, 0, 0, 0 };
    uint8_t reload_due = 0;
    const bool twotone_reset_due = (p->SKCTL & SK_TWOTONE) && p->twotone_delay == 1;
    for (int w = 0; w < 4; ++w) {
        if (p->timer_reload_delay[w] && --p->timer_reload_delay[w] == 0)
            reload_due |= timer_irq_bit(w);
        if (!timer_runs(p, w)) {
            /* Held for async receive (see timer_runs()): pin tcnt[2] at a
             * full period every slice it is held, rather than merely
             * freezing wherever it happened to be, so release - a start
             * bit (sdi_start()'s rearm_timer()) or SKCTL clearing
             * SK_ASYNC - always begins a fresh period.  Confirmed against
             * acid800 pokey_asyncrecv's timer 3+4 skip-cycles check: a
             * ~two-line async hold started right after one expiry pushes
             * the next one out by a full extra period (four lines), not
             * by the two lines actually held, which only "held in reset
             * state" (not "paused at its current count") explains. */
            if ((w == 2 || w == 3) && p->rng_enabled && (p->SKCTL & SK_ASYNC) && !p->sdi_busy)
                rearm_timer(p, w);
            continue;
        }
        /* A reset reload can straddle a slow source pulse. Do not let
         * the pre-reload counter borrow and overwrite its reload deadline.
         * Normal borrow reloads also wait here; their next period cannot
         * expire within three clocks (the minimum fast period is four). */
        if (p->timer_reload_delay[w] || (reload_due & timer_irq_bit(w)))
            continue;
        /* HRM 5.6: the other timer can fire "up to one cycle later".
         * The reset at borrow+2 wins over a new borrow on this clock.
         * Keep the rearm below, after reload retirement, so its full reload
         * delay and the documented +2 period extension are preserved. */
        if (w < 2 && twotone_reset_due)
            continue;
        if (p->tcnt[w] > 1) {
            --p->tcnt[w];
        } else {
            p->tcnt[w] = p->divisor[timer_channel(w)];
            borrows[w] = 1;
            p->timer_reload_delay[w] = 3;
            /* Every borrow enters the pipeline; IRQEN gates it when it
             * reaches the latch four cycles later. See fire_irq() and the
             * late-enable regressions in test_twotone_resync.c. */
            if (w < 3) {
                p->timer_irq_pending |= timer_irq_bit(w);
                p->timer_irq_delay[w] = TIMER_IRQ_STAGE_DELAY;
            }
        }
    }

    /* Sample AUDF on the actual reload edge, not at its register write or
     * at the earlier borrow stage. For fast timers three clocks of the
     * next period have already elapsed. Slow timers stay on the shared
     * source phase, including when a reload takes place during init. */
    /* Linking changes what a reload MEANS for the low timer of the pair.
     * Altirra HRM 5.3, "16-bit timers": "The automatic reload on underflow is
     * suppressed on the low timer ... When the high timer underflows, both the
     * low and high timer counters are reloaded together."  And "Linked timer
     * fire timing": the low timer "first counts down and underflows from its
     * initial period and then continues to count down and underflow every 256
     * ticks after that until the high timer also underflows and resets both
     * timers ... the low timer in a linked pair will fire AUDF2+1 or AUDF4+1
     * times for each time the high timer fires."
     *
     * Treating timer 1 as an independent countdown of its own period made it
     * fire every 20 cycles for AUDF1=$10 instead of once per 23-cycle linked
     * period, which acid800 pokey_timertiming reads as "1.79MHz 16-bit lo
     * timer triggered too early (loop #2)".  Only the 1+2 pair needs this:
     * timer 3 has no IRQ but uses slot 3 for cycle audio. Slot 2 is TIMR4,
     * the high timer of the 3+4 pair; both pairs use coordinated reloads. */
    const bool pair12_reset = (reload_due & timer_irq_bit(1)) != 0 &&
                              (p->AUDCTL & CTL_CH12_JOIN) != 0;
    const bool pair34_reset = (reload_due & timer_irq_bit(2)) != 0 &&
                              (p->AUDCTL & CTL_CH34_JOIN) != 0;
    for (int w = 0; w < 4; ++w) {
        bool reload = (reload_due & timer_irq_bit(w)) != 0;
        if ((w == 0 && pair12_reset) || (w == 3 && pair34_reset)) reload = true;
        if (!reload) continue;

        if ((w == 0 && (p->AUDCTL & CTL_CH12_JOIN) && !pair12_reset) ||
            (w == 3 && (p->AUDCTL & CTL_CH34_JOIN) && !pair34_reset)) {
            /* Linked low timer with no high-timer underflow: it wraps rather
             * than reloading, so the next underflow is a whole 256 ticks away
             * (the -3 is the borrow-to-reload stage the fast path also pays). */
            uint32_t wrap = timer_fast_clock(p, w) ? 256u : 256u * p->base_mult;
            if (timer_fast_clock(p, w)) wrap -= 3;
            p->tcnt[w] = wrap;
            p->timer_reload_delay[w] = 0;
            continue;
        }

        rearm_timer(p, w);
        if (timer_fast_clock(p, w)) p->tcnt[w] -= 3;
        p->timer_reload_delay[w] = 0;
    }

    /* Roll the two shared slow-clock phase markers past this slice before
     * any post-timer logic (two-tone/serial) can rearm a timer at the
     * current machine time. */
    pot_step(p);
    slow_clocks_advance_phase(p);

    /* Two-tone mode (SKCTL bit 3): the serial output's FSK tone shares
     * timers 1 and 2 with ordinary audio/IRQ duty, switching between them
     * per output data bit (timer 1 = mark/1, timer 2 = space/0), and
     * resyncs both whenever either contributes a pulse to the tone.
     * Altirra HRM 5.6, "Two-tone resync": "whenever the serial output
     * toggles due to one of the timers, both timers are reset ... Timer 1
     * pulses are only used by the serial output for a 1 bit, but timer 2
     * pulses are always used, causing a resync ... regardless of the
     * current data bit."  So timer 2's borrow always resyncs both; timer
     * 1's borrow resyncs both only while the output line is at mark.
     * This is why acid800 pokey_twotone measures far fewer timer 2 IRQs
     * during a continuous mark than during a continuous space: with
     * AUDF1 << AUDF2 (1 hblank vs. 2), timer 1's frequent resyncs starve
     * timer 2 of a full period almost every time, so its IRQ - the same
     * IRQ_TIMR2/tcnt[1] the ordinary audio hardware uses - fires far less
     * often than its free-running rate would give. Resync sends no audio
     * clock pulses: cycle audio retains its flip-flops. The legacy renderer
     * keeps its historical divider/output reset approximation. */
    if (p->SKCTL & SK_TWOTONE) {
        /* The reset is TWO CYCLES LATE.  HRM 5.6, "Two-tone resync timing":
         * "The timer 1+2 reset in two-tone mode occurs two cycles after the
         * timer that triggered the resync reloads ... if timer 1 at 1.79MHz
         * drives the resync, it will have a period of two cycles longer than
         * usual, due to being re-reloaded two cycles after the normal reload.
         * Note that this only affects the second and subsequent periods after
         * an STIMER reset, as there is no timer 1+2 resync at the start."
         * Resyncing on the borrow itself made every period after the first come
         * out two cycles short, which acid800 pokey_timertiming brackets
         * exactly: with AUDF1=8 (a 12-cycle period) the first IRQ is at 16 and
         * the second at 30, not 28. */
        if (p->twotone_delay && --p->twotone_delay == 0) {
            rearm_timer(p, 0);
            rearm_timer(p, 1);
        }
        bool mark = sdo_line(p);
        if (borrows[1] || (borrows[0] && mark)) {
            p->cassette_level ^= 1;
            p->twotone_delay = 2;
        }
    }
    else {
        p->twotone_delay = 0;
    }

    serial_step(p, borrows, due);
    cycle_audio_step(p, borrows);
    notify_pins(p);
}

/* Quiet clocks ---------------------------------------------------------
 *
 * Most clocks change nothing but the RANDOM chain, the poly counters, the
 * timer countdowns and the audio integrator: no timer reaches its borrow,
 * nothing is in the reload or IRQ pipelines, no two-tone resync or
 * high-pass latch is pending, the pot counter is idle and the serial port
 * is unclocked (it polls the host and watches the input line every clock
 * once a receive clock is selected).  quiet_span() says how many such
 * clocks lie ahead, at most `limit`, and advance_quiet() takes them in one
 * step with the same arithmetic advance_clock() would have applied clock
 * by clock - the chain and the noise histories still shift once per
 * clock, the countdowns lose exactly k, the slow-clock markers roll past
 * the same point, and the audio integrator adds the same area at the same
 * sample boundaries, since the level cannot change without a borrow or a
 * latch update.  Everything else stays on the one-clock path, so callback
 * timestamps and register state are unchanged (tests/probe_c012294_golden.c
 * hashes both paths against the one-clock core). */
static uint32_t quiet_span(const ad_pokey *p, uint32_t limit)
{
    if (!p->quiet_skip)
        return 0;
    if (p->timer_irq_pending || p->twotone_delay || p->serial_output_delay ||
        p->highpass_delay[0] || p->highpass_delay[1])
        return 0;
    /* With a filter bit clear, the one-clock path forces that latch to 1
     * at the end of every clock; a latch still 0 from a filtered stretch
     * makes the next clock an event. */
    if ((!(p->AUDCTL & CTL_CH1_FILTER) && !p->highpass_latch[0]) ||
        (!(p->AUDCTL & CTL_CH2_FILTER) && !p->highpass_latch[1]))
        return 0;
    if (p->timer_reload_delay[0] | p->timer_reload_delay[1] |
        p->timer_reload_delay[2] | p->timer_reload_delay[3])
        return 0;
    if (p->pot_counter_mode && p->pot_scanning)
        return 0;
    if (p->SKCTL & 0x30)            /* receive clock selected, or async hold */
        return 0;
    uint32_t k = limit;
    for (int w = 0; w < 4; ++w) {
        if (!timer_runs(p, w))
            continue;
        if (p->tcnt[w] <= 1)        /* borrows on this clock */
            return 0;
        if (p->tcnt[w] - 1 < k)
            k = p->tcnt[w] - 1;     /* the clock it reaches 1 is still quiet */
    }
    return k;
}

static void advance_quiet(ad_pokey *p, uint32_t k)
{
    cycle_audio_integrate(p, cycle_audio_level(p), 2u * k);
    p->cycles += k;

    const bool sel9 = (p->AUDCTL & CTL_POLY9) != 0;
    const bool running = p->rng_enabled != 0;
    for (uint32_t i = 0; i < k; ++i) {
        chain_step(&p->rng, p->rng_init_prev != 0, sel9);
        p->rng_init_prev = (uint8_t)!p->rng_enabled;
        cycle_audio_noise_clock(p, running);
    }

    for (int w = 0; w < 4; ++w)
        if (timer_runs(p, w))
            p->tcnt[w] -= k;

    slow_clocks_advance_phase(p);
}

void ad_pokey_advance(ad_pokey *p, uint32_t cycles)
{
    /* Walk events in machine-clock order, including IRQ
     * pipeline retirement, serial edges and two-tone timer resyncs. This
     * makes callback timestamps and register state independent of batching.
     * A zero-length advance has no side effects. */
    while (cycles) {
        uint32_t k = quiet_span(p, cycles);
        if (k) {
            advance_quiet(p, k);
            cycles -= k;
        } else {
            advance_clock(p);
            cycles--;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Writes                                                              */
/* ------------------------------------------------------------------ */

void ad_pokey_write(ad_pokey *p, uint8_t reg, uint8_t v)
{
    const uint8_t a = reg & 0x0F;
    switch (a) {
    /* AUDF writes update the reload value, not the running counter (HRM 5.3):
     * AUDF1 -> TIMR1 (+TIMR2 when ch1+2 joined), AUDF2 -> TIMR2, AUDF3 ->
     * TIMR4 only when ch3+4 joined, AUDF4 -> TIMR4.  AUDC writes never
     * touch a divisor, so they must not reset a timer's phase. */
    case W_AUDF1:
        p->AUDF[0] = v;
        recompute_channel(p, 0);
        if (p->AUDCTL & CTL_CH12_JOIN) recompute_channel(p, 1);
        break;
    case W_AUDF2:
        p->AUDF[1] = v; recompute_channel(p, 1);
        break;
    case W_AUDF3:
        p->AUDF[2] = v;
        recompute_channel(p, 2);
        if (p->AUDCTL & CTL_CH34_JOIN) recompute_channel(p, 3);
        break;
    case W_AUDF4:
        p->AUDF[3] = v; recompute_channel(p, 3);
        break;

    case W_AUDC1: p->AUDC[0] = v; recompute_channel(p, 0); break;
    case W_AUDC2: p->AUDC[1] = v; recompute_channel(p, 1); break;
    case W_AUDC3: p->AUDC[2] = v; recompute_channel(p, 2); break;
    case W_AUDC4: p->AUDC[3] = v; recompute_channel(p, 3); break;

    case W_AUDCTL: {
        /* A rewrite of the current value is a no-op (MAME pokey.cpp returns
         * early on it), and a timer whose clocking the write leaves alone
         * keeps counting: AUDCTL never reloads a counter on the chip, only
         * STIMER and a borrow do (Altirra HRM 5.3, "Reload timing").  Major
         * Havoc rewrites $78 twice a frame and Battlezone $00 every frame;
         * re-arming every timer on each write restarted every tone once the
         * audio came from the hardware counters.  A timer whose clock source
         * or pair link DOES change is still re-armed here: the count is kept
         * in machine cycles aligned to the selected clock, and the timing
         * regressions (tests/review_pokey_timing.c in the AAE tree) were
         * derived with that re-arm, so a count carried across a clock change
         * is a separate, hardware-measured step. */
        if (v == p->AUDCTL)
            break;
        const uint8_t old = p->AUDCTL;
        bool fast_before[4];
        for (int w = 0; w < 4; ++w)
            fast_before[w] = timer_fast_clock(p, w);
        p->AUDCTL = v;
        p->base_mult = (v & CTL_CLK15) ? DIV_15 : DIV_64;
        recompute_all(p);
        const uint8_t changed = (uint8_t)(old ^ v);
        for (int w = 0; w < 4; ++w) {
            const bool link_changed = (w < 2) ? (changed & CTL_CH12_JOIN) != 0
                                              : (changed & CTL_CH34_JOIN) != 0;
            const bool fast_now = timer_fast_clock(p, w);
            const bool clock_changed = fast_now != fast_before[w] ||
                                       (!fast_now && (changed & CTL_CLK15));
            if (link_changed || clock_changed)
                rearm_timer(p, w);
        }
        break;
    }

    case W_STIMER:
        for (int i = 0; i < 4; ++i) p->out[i] = 1;
        /* HRM 5.3: T4 is the last STIMER strobe that preempts the T8
         * IRQ for fast AUDF=0. Only the just-entered borrow stage is
         * cancelable; preserve interrupts further down the pipeline. */
        for (int w = 0; w < 3; ++w) {
            if (p->timer_irq_delay[w] == TIMER_IRQ_STAGE_DELAY) {
                p->timer_irq_pending &= (uint8_t)~timer_irq_bit(w);
                p->timer_irq_delay[w] = 0;
            }
        }
        rearm_timer(p, 0); rearm_timer(p, 1); rearm_timer(p, 2); rearm_timer(p, 3);
        break;

    case W_SKCTL:
        /* A rewrite of the current value is a no-op.  The init bits
         * change what RANDOM takes from the next clock on, and reset the
         * shared 15/64KHz source-clock phase.  Crucially, init does NOT
         * reset the timer counters themselves (Altirra HRM 5.2): entering
         * init saves how many source pulses each slow timer still needs,
         * and leaving init re-anchors that count to the freshly reset clock
         * phase.  The audio poly phases restart from the seed on entering
         * reset and hold still until release. */
        if (v == p->SKCTL)
            break;
        {
            const bool was_running = p->rng_enabled != 0;
            const bool now_running = (v & SK_INIT) != 0;

            if (was_running && !now_running)
                slow_clocks_enter_init(p);

            p->SKCTL = v;
            /* HRM 5.6: clock select 000 resets both serial clock dividers,
             * independently of the SKCTL initialization bits. */
            if (!(v & 0x70)) {
                p->clock_out_phase = p->clock_bi_phase = 0;
                p->serial_output_delay = 0;
            }
            if (!(v & SK_TWOTONE)) p->cassette_level = 1;
            p->rng_enabled = now_running ? 1 : 0;

            if (!was_running && now_running)
                slow_clocks_leave_init(p);
        }
        if (!p->rng_enabled) {
            p->p4 = p->p5 = p->p9 = p->p17 = 0;
            /* Init also resets both serial state machines (SER_core.v's
             * istate/ostate): a frame in flight is abandoned and the
             * data register counts as taken. */
            p->sdi_busy = false;
            p->sdo_busy = false;
            p->sdo_pending = false;
            p->serial_output_delay = 0;
        }
        break;

    case W_POTGO:
        p->pot_scanning = true;
        p->pot_scan_ever = true;
        p->pot_scan_start = p->cycles;
        p->pot_clear_delay = 1;
        p->pot_finish_delay = 0;
        p->pot_transition = false;
        break;

    case W_SEROUT:
        /* Into the output data register; the shifter takes it at its
         * next bit clock (serial_step()).  A write over a byte the
         * shifter has not taken yet replaces it, as on the chip. */
        p->SEROUT = v;
        p->sdo_pending = true;
        break;

    case W_IRQEN: {
        /* Clear any pending IRQST bits being disabled, then set the
         * mask.  No timer scheduling here - the countdowns in
         * ad_pokey_advance() run unconditionally; IRQEN only gates
         * whether a borrow reaches fire_irq().  Bit 3 is a level, not a
         * latch: enabling it while the transmitter is idle asserts the
         * IRQ at once. */
        const uint8_t was = p->IRQEN;
        if (p->IRQST & (uint8_t)~v)
            p->IRQST &= v;
        p->IRQEN = v;
        if ((v & ~was & IRQ_SEROC) && sdo_idle(p) && p->host && p->host->raise_irq)
            p->host->raise_irq(p->host->ctx, IRQ_SEROC);
        break;
    }

    case W_SKREST:
        p->st_latch = 0;
        break;

    default:
        break;
    }
    notify_pins(p);
    notify_clocks(p, false);
}

/* ------------------------------------------------------------------ */
/* Reads                                                               */
/* ------------------------------------------------------------------ */

/* RANDOM: the complement of the chain's 9-bit register (poly_core.v's
 * rndNum = ~lfsr9bit), which only ad_pokey_advance() moves (see
 * c012294.h's time-model note).  The read charges nothing, so a caller
 * that knows the 6502 cycle distance to its previous read advances by
 * exactly that first, and back-to-back reads with no machine time
 * between them return the same byte.  Whichever poly AUDCTL selects,
 * the byte comes from the same eight flops; the select only changes
 * what feeds them.  A chip held in reset reads 0xFF once eight clocks
 * of zeros have shifted in, and before that the tail of what it was
 * doing. */
static uint8_t read_random(const ad_pokey *p)
{
    return (uint8_t)~p->rng.l9;
}

/* Digital POT model: ALLPOT follows the host-supplied comparator mask
 * until the scan's terminal count, then returns zero until the next POTGO.
 * Before the first POTGO it returns the mask directly, supporting arcade
 * DIP inputs. POT0-7 read host callbacks; independent analog paddle
 * counters and per-input completion are not implemented.
 *
 * Terminal counts follow Altirra HRM 5.9: 228 slow counts or 229 fast
 * counts. The host owns DIP polarity and calls ad_pokey_set_allpot(). */

static uint8_t allpot_read(ad_pokey *p)
{
    if (p->pot_counter_mode)
        return (!p->pot_scan_ever || p->pot_scanning) ? p->allpot : 0;
    if (p->pot_scanning) {
        const uint64_t elapsed = p->cycles - p->pot_scan_start;
        /* The scan ends at the counter's terminal count: 228 in slow
         * mode, one count per 114-cycle scan line; 229 in fast mode, one
         * count per machine cycle (the counter stops one value higher in
         * fast mode - Altirra HRM 5.9). All durations are POKEY cycles. */
        const uint64_t need = (p->SKCTL & SK_FASTPOT) ? 229u : (228u * DIV_15);
        if (elapsed >= need)
            p->pot_scanning = false;
    }
    /* ALLPOT is not a latch during a scan: it follows the pins live, so
     * a line that trips reads 0 and one that drops back below threshold
     * reads 1 again (POT0-7 counters are not modelled).  The host keeps `allpot` at the pins' current
     * grounded-line mask with ad_pokey_set_allpot().  Only the finished
     * scan is a latch, forced to 0 until the next POTGO. */
    if (!p->pot_scan_ever)
        return p->allpot;      /* no POTGO issued yet: the comparators' mask */
    if (!p->pot_scanning)
        return 0x00;           /* scan complete: forced to 0 */
    return p->allpot;          /* mid-scan: the comparators' mask, live */
}

uint8_t ad_pokey_read(ad_pokey *p, uint8_t reg)
{
    const uint8_t a = reg & 0x0F;
    switch (a) {
    case R_RANDOM: return read_random(p);
    case R_ALLPOT: return allpot_read(p);
    case R_IRQST: {
        /* Pending IRQs read as 0.  Bit 3 is not a latch: it is low
         * whenever the transmitter is idle, whatever IRQEN says. */
        uint8_t v = (uint8_t)(p->IRQST ^ 0xFF);
        if (sdo_idle(p)) v &= (uint8_t)~IRQ_SEROC; else v |= IRQ_SEROC;
        return v;
    }
    case R_SKSTAT: return skstat_read(p);
    case R_KBCODE: return p->KBCODE;
    case R_SERIN:  return p->SERIN;
    default:
        /* POT0-7 share offsets 0x00-0x07 with AUDF1-4/AUDC1-4's write
         * side; on the read side they are the only registers there. */
        if (a <= R_POT0 + 7) {
            if (p->pot_counter_mode) {
                if (p->pot_scanning && (p->allpot & (1u << a)) && p->pot_transition)
                    return p->pot_count & p->pot_previous;
                return p->pot_latch[a];
            }
            if (p->host && p->host->pot_read)
                return (uint8_t)p->host->pot_read(p->host->ctx, a);
            return 0xFF;   /* AAE's default with no pot handler wired up */
        }
        return 0xFF;
    }
}

/* ------------------------------------------------------------------ */
/* Keyboard                                                            */
/* ------------------------------------------------------------------ */
/* A key event from the host, standing in for the chip's matrix scan
 * (KEY_core.v): the scanner only runs with SKCTL's scan-enable bit set.
 * A new code latches into KBCODE and raises the keyboard IRQ; if that
 * IRQ was still pending from the previous code, the keyboard overrun
 * latch sets (IRQ_core.v: keyOvrun = setKey & the pending latch) -
 * nothing on the chip knows whether KBCODE was read, only whether its
 * IRQ was cleared.  SKSTAT's key-down and shift-key bits follow the
 * matrix live, so they come from every call, up or down. */
void ad_pokey_keyboard_key(ad_pokey *p, uint8_t code, uint8_t flags, bool down)
{
    if ((p->SKCTL & SK_KEYSCAN) == 0)
        return;
    p->kb_shift = (flags & ST_SHIFT) != 0;
    if (!down) {
        p->kb_down = false;
        return;
    }
    if (p->IRQST & IRQ_KEYBD)
        p->st_latch |= ST_KBERR;
    p->KBCODE = code;
    p->kb_down = true;
    if (p->IRQEN & IRQ_KEYBD)
        fire_irq(p, IRQ_KEYBD);
}

/* ------------------------------------------------------------------ */
/* Serial port                                                         */
/* ------------------------------------------------------------------ */
/* SER_core.v, a byte at a time.  Each direction is a ten-stage shift
 * register (start bit, eight data bits LSB first, stop bit) clocked by
 * a flop that toggles on a timer's borrow, so a bit is two borrows and
 * a frame twenty.  SKCTL bits 4..6 pick the timers: the receiver clocks
 * from timer 4 unless bits 5 and 4 are both clear (the external bit
 * clock), the transmitter from timer 2 with bits 6 and 5 set, timer 4
 * with either alone, the external clock with both clear. External edges
 * are supplied by ad_pokey_serial_clock().
 *
 * Transmit: the shifter takes the data register at its next bit clock
 * and that load is the "output data needed" event (IRQ bit 4); twenty
 * borrows later the stop bit has left the pin and the byte goes to the
 * host.  A second SEROUT during a frame waits in the data register and
 * loads straight after, so a stream is gapless.  "Transmission
 * finished" (IRQ bit 3) is the idle level, see sdo_idle().
 *
 * Receive: a start bit is offered whenever the receiver is idle - the
 * host's serial_in() from ad_pokey_advance(), or ad_pokey_serial_
 * receive() directly.  Outside of that, asynchronous mode (SKCTL bit 4)
 * holds timers 3+4 (tcnt[2]) in reset - see timer_runs() and
 * ad_pokey_advance() - so they only start counting once a start bit
 * arrives, which is when sdi_start() resyncs them (Altirra HRM 5.6,
 * "Asynchronous receive mode").  Twenty borrows later the stop bit is
 * sampled: SERIN takes the byte, the input IRQ (bit 5) fires, and if
 * that IRQ was still pending from the previous byte the overrun latch
 * sets (IRQ_core.v: sdiOvrun = setSdiCompl & the pending latch).
 * SKSTAT's busy bit is low from the start bit to the stop bit, and its
 * serial-data bit shows the raw line. The raw-line interface can supply a
 * zero stop bit to exercise framing errors. Two-tone resync and force break
 * are handled in advance and sdo_line. Optional IO and clock callbacks
 * expose DATA OUT and logical clock-pin transitions to the host. */

enum { SER_FRAME_BORROWS = 20 };

static int sdi_timer(const ad_pokey *p)
{
    return (p->SKCTL & 0x30) ? 2 : -1;
}

static int sdo_timer(const ad_pokey *p)
{
    switch (p->SKCTL & 0x60) {
    case 0x00: return -1;
    case 0x60: return 1;
    default:   return 2;
    }
}

/* IRQST bit 3 (SEROC) follows the SHIFT REGISTER only - a byte merely waiting
 * in SEROUT does not clear it.  HRM 5.6: "The serial output complete IRQ
 * (IRQEN/ST bit 3) is asserted whenever the output shift register is idle",
 * and its warning: "there is a delay from the first write to SEROUT until the
 * serial output ready/complete IRQs update".  HRM 5.7 repeats it: the complete
 * IRQ "deasserts automatically once a new byte is loaded into the output shift
 * register and there is a delay from when SEROUT is written to when this
 * occurs."  Counting sdo_pending here made acid800 pokey_serclock's last check
 * fail - with the external clock selected and nothing driving it, the byte can
 * never load, so IRQST must read $F7 and not $FF. */
static bool sdo_idle(const ad_pokey *p)
{
    return !p->sdo_busy;
}

/* Where a frame's line sits with `left` borrows still to go: two borrows to a
 * bit cell, cell 0 the start bit, 1..8 the data bits LSB first, 9 the stop. */
static bool frame_line_level(uint8_t byte, uint32_t left)
{
    const uint32_t bit = (SER_FRAME_BORROWS - left) / 2;
    if (bit == 0) return false;                      /* start bit */
    if (bit >= 9) return true;                       /* stop bit */
    return ((byte >> (bit - 1)) & 1u) != 0;
}

/* The output data bit currently on the wire - same 20-borrow/10-bit
 * framing as frame_line_level(), for the SEROUT shifter instead of the input
 * one.  Used only by two-tone mode's timer select: mark (true) when idle
 * or on the stop bit, space (false) on the start bit, else the shifting
 * byte's bit, LSB first. */
static bool sdo_line(const ad_pokey *p)
{
    /* HRM 5.6: force break selects the zero-bit tone in two-tone mode.
     * In ordinary serial mode it forces DATA OUT low. The shifter itself
     * keeps running; only its output is overridden. */
    if (p->SKCTL & SK_BREAKEN)
        return false;
    if (!p->sdo_busy)
        return true;
    uint32_t bit = (SER_FRAME_BORROWS - p->sdo_left) / 2;
    if (bit == 0) return false;                       /* start bit */
    if (bit >= 9) return true;                        /* stop bit */
    return ((p->sdo_byte >> (bit - 1)) & 1u) != 0;
}

static uint8_t skstat_read(const ad_pokey *p)
{
    uint8_t v = ST_ALWAYS_ONE;
    if (!(p->st_latch & ST_FRAME))   v |= ST_FRAME;
    if (!(p->st_latch & ST_OVERRUN)) v |= ST_OVERRUN;
    if (!(p->st_latch & ST_KBERR))   v |= ST_KBERR;
    /* Bit 4 is the RAW line, not something derived from the shift register.
     * HRM 5.6, "Direct input": it "bypasses all of the shifting and clocking
     * logic and ignores all serial input settings, working even if all clocks
     * are stopped." */
    if (p->sdi_line_in)              v |= ST_SERIN_DATA;
    if (!p->kb_shift)                v |= ST_SHIFT;
    if (!p->kb_down)                 v |= ST_KEYBD;
    if (!p->sdi_busy)                v |= ST_SERIN_BUSY;
    return v;
}

/* The internal byte -> line source.  A host that only has whole bytes hands
 * one over (serial_in / ad_pokey_serial_receive) and POKEY walks it onto the
 * line itself at the receive clock, so the line always tells the same story as
 * the shift register that is reading it.  A host that shifts its own bits uses
 * ad_pokey_serial_line() instead and this stands aside. */
static void sdi_source_offer(ad_pokey *p, uint8_t data)
{
    p->sdi_src_busy = true;
    p->sdi_src_byte = data;
    p->sdi_src_left = SER_FRAME_BORROWS;
    p->sdi_line_in  = 0;                 /* the start bit, from this cycle on */
}

static void sdi_source_step(ad_pokey *p, uint32_t borrows)
{
    if (p->sdi_host_line) return;        /* the host is driving the line */
    if (!p->sdi_src_busy) { p->sdi_line_in = 1; return; }

    while (borrows && p->sdi_src_left) { --p->sdi_src_left; --borrows; }

    if (p->sdi_src_left == 0) { p->sdi_src_busy = false; p->sdi_line_in = 1; }
    else p->sdi_line_in = frame_line_level(p->sdi_src_byte, p->sdi_src_left) ? 1 : 0;
}

static void sdi_complete(ad_pokey *p)
{
    p->sdi_busy = false;
    p->sdi_byte = p->sdi_shift;
    p->SERIN    = p->sdi_shift;
    /* "A framing error is detected if the stop bit is not high" - the latch
     * a byte interface could never set. */
    if (!p->sdi_stop_ok)
        p->st_latch |= ST_FRAME;
    if (p->IRQST & IRQ_SERIN)
        p->st_latch |= ST_OVERRUN;
    if (p->IRQEN & IRQ_SERIN)
        fire_irq(p, IRQ_SERIN);
}

/* One slice of the input shift register.  The start bit is an EDGE ON THE
 * LINE, not a byte handed over: the receiver watches for the line going low
 * while it is idle and clocked, then samples the middle of each bit cell -
 * borrow 3 for data bit 0, 5 for bit 1 ... 17 for bit 7, 19 for the stop bit,
 * with the frame ending on borrow 20.  Asynchronous mode holds timers 3+4 in
 * reset until that edge (HRM 5.6), which is what re-arms them here. */
static void sdi_recv_step(ad_pokey *p, uint32_t borrows)
{
    if (!p->sdi_busy) {
        if (p->sdi_line_in)
            return;                      /* line at mark: no frame starting */
        p->sdi_busy    = true;
        p->sdi_left    = SER_FRAME_BORROWS;
        p->sdi_shift   = 0;
        p->sdi_stop_ok = true;
        if (p->SKCTL & SK_ASYNC)
            rearm_timer(p, 2);
    }

    while (borrows--) {
        const uint32_t n = SER_FRAME_BORROWS - p->sdi_left + 1;   /* 1-based */
        if ((n & 1u) && n >= 3 && n <= 17) {
            if (p->sdi_line_in)
                p->sdi_shift |= (uint8_t)(1u << ((n - 3) / 2));
        }
        else if (n == 19) {
            p->sdi_stop_ok = p->sdi_line_in != 0;
        }
        if (--p->sdi_left == 0) { sdi_complete(p); break; }
    }
}

/* One slice of serial time: borrows[w] is how many times timer w
 * borrowed in the slice ad_pokey_advance() just walked. */
static void serial_edges(ad_pokey *p, int ti, int to, const uint32_t *borrows, bool external)
{
    if (ti >= 0) {
        /* Ask a byte-only host for the next frame before moving the line, so
         * the start bit is on the wire in the same cycle it is offered. */
        if (!p->sdi_host_line && !p->sdi_src_busy && !p->sdi_busy &&
            p->host && p->host->serial_in) {
            int b = p->host->serial_in(p->host->ctx);
            if (b >= 0)
                sdi_source_offer(p, (uint8_t)b);
        }
        if (external) {
            sdi_recv_step(p, borrows[ti]);
            sdi_source_step(p, borrows[ti]);
        } else {
            sdi_source_step(p, borrows[ti]);
            sdi_recv_step(p, borrows[ti]);
        }
    }

    if (to >= 0) {
        /* One bit-cell edge at a time.  The borrow that empties the shifter
         * is the SAME edge on which a waiting byte loads - the shifter "only
         * attempts to load once every bit cell time on the rising edge of the
         * serial clock" (HRM 5.6), and it is a 10-bit register, so a frame is
         * SER_FRAME_BORROWS edges and back-to-back loads are exactly that far
         * apart.  Giving the load an edge of its own on top of the frame made
         * a byte cost 21 edges, which acid800 pokey_serclock measures as
         * VCOUNT 42 where hardware gives 40.  Sharing the edge is also what
         * keeps bit 3 "inactive continuously while sending back-to-back
         * bytes": sdo_busy never drops between them. */
        for (uint32_t b = borrows[to]; b; --b) {
            bool went_idle = false;

            if (p->sdo_busy && --p->sdo_left == 0) {
                p->sdo_busy = false;
                went_idle = true;
                if (p->host && p->host->serial_out)
                    p->host->serial_out(p->host->ctx, p->sdo_byte);
            }

            /* Internal borrows are half-bit edges. serial_step has already
             * advanced the divider phase; a falling edge cannot load SEROUT.
             * External calls contain only rising bit-cell edges. */
            if (!p->sdo_busy && p->sdo_pending && (external || p->clock_out_phase)) {
                p->sdo_byte = p->SEROUT;
                p->sdo_pending = false;
                p->sdo_busy = true;
                p->sdo_left = SER_FRAME_BORROWS;
                went_idle = false;
                if (p->IRQEN & IRQ_SEROR)
                    fire_irq(p, IRQ_SEROR);
            }

            if (went_idle && (p->IRQEN & IRQ_SEROC) && p->host && p->host->raise_irq)
                p->host->raise_irq(p->host->ctx, IRQ_SEROC);

            if (!p->sdo_busy)
                break;                      /* nothing left to clock this slice */
        }
    }
}

static void serial_step(ad_pokey *p, const uint32_t *borrows, uint8_t timer_completed)
{
    /* HRM table 10: the output divider selects timer 2 or 4. The other
     * divider uses timer 4 and drives the bidirectional pin in modes 010
     * and 110. Clocking is independent of AUDC and of queued serial data. */
    int to = sdo_timer(p);
    /* Our borrows[] are the early counter event, four clocks before the
     * timer IRQ stage. Output clocking uses the completed timer stage,
     * independent of IRQEN, followed by a two-clock serial action pipeline.
     * Acid800 sertiming brackets the resulting first load at STIMER+234
     * for period 228. Altirra FireTimer schedules SerialOutput two clocks
     * after its timer IRQ stage; the schematic transcription likewise has
     * separate divider, edge-detector and output-state stages.
     * Receive timing and external clock calls have separate paths. */
    if (p->serial_output_delay && --p->serial_output_delay == 0 && to >= 0) {
        uint32_t output_edge[4] = {0,0,0,0};
        output_edge[to] = 1;
        serial_edges(p, -1, to, output_edge, false);
    }
    if (to >= 0 && (timer_completed & timer_irq_bit(to))) {
        p->clock_out_phase ^= 1;
        p->serial_output_delay = 2;
    }
    if (p->SKCTL & 0x30) p->clock_bi_phase ^= (uint8_t)(borrows[2] & 1);
    serial_edges(p, sdi_timer(p), -1, borrows, false);
    notify_clocks(p, false);
}

void ad_pokey_serial_clock(ad_pokey *p, int high)
{
    uint8_t level = high ? 1 : 0;
    if (level == p->external_clock) return;
    p->external_clock = level;
    notify_clocks(p, false);
    if (!level || !p->rng_enabled) return;
    /* One rising external edge is a complete bit cell. The internal
     * timer interface uses two half-cell borrows per bit. */
    uint32_t edges[3] = { 2, p->sdo_busy ? 2u : 1u, 0 };
    serial_edges(p, sdi_timer(p) < 0 ? 0 : -1,
                    sdo_timer(p) < 0 ? 1 : -1, edges, true);
    notify_pins(p);
}

/* A byte from the host, outside the serial_in() poll: POKEY shifts it onto
 * the input line itself.  Dropped if a frame is already on the line, if the
 * host has taken the line over with ad_pokey_serial_line(), or if there is no
 * receive clock to shift it with. */
void ad_pokey_serial_receive(ad_pokey *p, uint8_t data)
{
    if (p->sdi_host_line || p->sdi_src_busy || p->sdi_busy || sdi_timer(p) < 0)
        return;
    sdi_source_offer(p, data);
}

/* Drive the serial input line directly, for a host that shifts its own bits.
 * From the first call POKEY stops synthesising the line from whole bytes; the
 * receiver reads what the host puts here, and so does SKSTAT bit 4 - "even if
 * all clocks are stopped" (HRM 5.6, "Direct input"), which is the case acid800
 * pokey_serdirect measures. */
void ad_pokey_serial_line(ad_pokey *p, int mark)
{
    p->sdi_host_line = 1;
    p->sdi_line_in   = mark ? 1u : 0u;
}

/* ------------------------------------------------------------------ */
/* Poll                                                                */
/* ------------------------------------------------------------------ */
/* The host's per-frame poll, standing in for the matrix scan: pulls one
 * keyboard code through host->keyboard_scan() while the scanner is
 * enabled.  (Serial input is not polled here - the receiver asks the
 * host for a start bit itself, from ad_pokey_advance(), whenever it is
 * idle and clocked.) */
void ad_pokey_poll(ad_pokey *p)
{
    if ((p->SKCTL & SK_KEYSCAN) == 0 || !p->host || !p->host->keyboard_scan)
        return;
    uint8_t code = 0, flags = 0;
    if (p->host->keyboard_scan(p->host->ctx, &code, &flags))
        ad_pokey_keyboard_key(p, code, flags, true);
}

/* ------------------------------------------------------------------ */
/* Audio                                                               */
/* ------------------------------------------------------------------ */
/* Cycle audio: ad_pokey_advance() integrates the DAC level over every
 * half clock into a sample queue at sys_freq (cycle_audio_sample() and
 * friends below); ad_pokey_audio_read()/ad_pokey_render() drain it. */

/* Drop queued PCM and the integrator/DC-tracker state; the oscillators,
 * latches and registers are untouched. */
static void audio_queue_clear(ad_pokey *p)
{
    p->audio_phase = p->audio_dropped = 0;
    p->audio_area = 0;
    p->audio_head = p->audio_count = 0;
    p->audio_dc = 0;
}

/* The playback stage after the DAC curve: a one-pole DC block at dc_hz
 * (0 = none, the unipolar DAC signal) and a gain, both at sys_freq. */
static void audio_playback_config(ad_pokey *p, double dc_hz, double gain)
{
    p->audio_gain = isfinite(gain) && gain >= 0 ? gain : 1.0;
    p->audio_dc_decay = isfinite(dc_hz) && dc_hz > 0 ?
        exp(-6.283185307179586 * dc_hz / p->sys_freq) : 1.0;
}

void ad_pokey_audio_clear(ad_pokey *p)
{
    audio_queue_clear(p);
}

void ad_pokey_set_measured_audio(ad_pokey *p, double dc_hz, double gain)
{
    audio_playback_config(p, dc_hz, gain);
    audio_queue_clear(p);
}

uint32_t ad_pokey_audio_available(const ad_pokey *p) { return p->audio_count; }
uint64_t ad_pokey_audio_overruns(const ad_pokey *p) { return p->audio_dropped; }

int ad_pokey_audio_read(ad_pokey *p, int16_t *dst, int n)
{
    if (!dst || n <= 0) return 0;
    if ((uint32_t)n > p->audio_count) n = (int)p->audio_count;
    for (int i = 0; i < n; ++i) {
        dst[i] = p->audio_queue[p->audio_head];
        p->audio_head = (p->audio_head + 1) % AD_POKEY_AUDIO_CAPACITY;
    }
    p->audio_count -= n;
    return n;
}

/* The DAC level for the current channel outputs, latches and volumes:
 * each channel whose output is high adds its AUDC volume's weight (the
 * four measured bit drops .12/.26/.56/1.12 V in .02 V units, summed per
 * nibble), and the shared curve turns the total into PCM. */
static int32_t cycle_audio_level(const ad_pokey *p)
{
    static const uint8_t weights[16] = {
        0,6,13,19,28,34,41,47,56,62,69,75,84,90,97,103
    };
    unsigned sum = 0;
    for (int i = 0; i < 4; ++i) {
        uint8_t bit = p->out[i];
        if (i < 2) bit ^= p->highpass_latch[i];
        if (p->AUDC[i] & AUDC_VOLONLY) bit = 1;
        if (bit) sum += weights[p->AUDC[i] & AUDC_VOLMASK];
    }
    return g_dac_curve[sum];
}

/* Integrate `level` over `halves` half clocks into the sample queue.
 * Half-cycle integration retains the 1.5-cycle high-pass delay. Each
 * half clock contributes sample_rate units to a 2*clock_hz sample. */
static void cycle_audio_integrate(ad_pokey *p, int32_t level, uint32_t halves)
{
    const uint64_t sample_span = (uint64_t)p->base_clock * 2;
    uint64_t remaining = (uint64_t)halves * p->sys_freq;
    while (remaining) {
        uint64_t span = sample_span - p->audio_phase;
        if (span > remaining) span = remaining;
        p->audio_area += (int64_t)level * (int64_t)span;
        p->audio_phase += span;
        remaining -= span;
        if (p->audio_phase == sample_span) {
            /* The bias rides through the nonlinear DAC and the integration;
             * only the playback stage removes DC and scales gain. */
            double raw = (double)p->audio_area / (double)sample_span;
            double output = (raw - p->audio_dc) * p->audio_gain;
            p->audio_dc += (raw - p->audio_dc) * (1.0 - p->audio_dc_decay);
            if (output > 32767.0) output = 32767.0;
            if (output < -32768.0) output = -32768.0;
            int64_t value = (int64_t)output;
            if (p->audio_count == AD_POKEY_AUDIO_CAPACITY) {
                p->audio_head = (p->audio_head + 1) % AD_POKEY_AUDIO_CAPACITY;
                --p->audio_count;
                ++p->audio_dropped;
            }
            p->audio_queue[(p->audio_head + p->audio_count++) % AD_POKEY_AUDIO_CAPACITY] = (int16_t)value;
            p->audio_area = 0;
            p->audio_phase = 0;
        }
    }
}

/* One half clock of audio: integrate the current level, then retire the
 * high-pass delays (three half clocks from the clocking borrow). */
static void cycle_audio_sample(ad_pokey *p)
{
    cycle_audio_integrate(p, cycle_audio_level(p), 1);
    for (int i = 0; i < 2; ++i)
        if (p->highpass_delay[i] && --p->highpass_delay[i] == 0)
            p->highpass_latch[i] = p->highpass_pending[i];
}

/* One clock of the poly counters and the noise histories the channel
 * outputs sample at their borrows. */
static void cycle_audio_noise_clock(ad_pokey *p, bool running)
{
    if (running) {
        if (++p->p4 == 15) p->p4 = 0;
        if (++p->p5 == 31) p->p5 = 0;
        if (++p->p9 == 511) p->p9 = 0;
        if (++p->p17 == 131071) p->p17 = 0;
    }
    p->noise4_history = (uint8_t)((p->noise4_history << 1) | (running ? g_poly4[p->p4] : 0));
    p->noise5_history = (uint8_t)((p->noise5_history << 1) | (running ? g_poly5[p->p5] : 1));
    p->noise917_history = (uint8_t)((p->noise917_history << 1) | (p->rng.l9 & 1));
}

static void cycle_audio_step(ad_pokey *p, const uint32_t *borrows)
{
    cycle_audio_noise_clock(p, p->rng_enabled != 0);
    for (int w = 0; w < 4; ++w) {
        if (!borrows[w]) continue;
        int ch = timer_channel(w);
        uint8_t control = p->AUDC[ch];
        if ((control & AUDC_NOTPOLY5) || ((p->noise5_history >> ch) & 1)) {
            if (control & AUDC_PURE) p->out[ch] ^= 1;
            else if (control & AUDC_POLY4) p->out[ch] = (p->noise4_history >> ch) & 1;
            else p->out[ch] = (p->noise917_history >> ch) & 1;
        }
    }
    for (int i = 0; i < 2; ++i) {
        if (!(p->AUDCTL & (i ? CTL_CH2_FILTER : CTL_CH1_FILTER))) {
            p->highpass_latch[i] = 1;
            p->highpass_delay[i] = 0;
        } else if (borrows[i ? 2 : 3]) {
            p->highpass_pending[i] = p->out[i];
            p->highpass_delay[i] = 3;
        }
    }
}

void ad_pokey_render(ad_pokey *p, int16_t *dst, int n)
{
    if (!dst || n <= 0)
        return;
    int got = ad_pokey_audio_read(p, dst, n);
    memset(dst + got, 0, (size_t)(n - got) * sizeof *dst);
}
