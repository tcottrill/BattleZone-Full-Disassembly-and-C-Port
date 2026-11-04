/* Focused ROM-vs-native regression cases, sharing lockstep's real hardware
 * recording/replay and shared ref6502 interpreter. No new CPU model. */
#define main frame_lockstep_main
#include "lockstep.c"
#undef main

static unsigned cases_run, cases_failed;
static uint8_t entry_a, entry_x;  /* the routine's entry registers (ROM side) */

static void reset_case(void)
{
    entry_a = entry_x = 0;
    in_in0 = 0x3F;
    memset(&g, 0, sizeof g);
    memset(R_ram, 0, sizeof R_ram);
    memset(R_vram, 0, sizeof R_vram);
    mb_reset(&mb);
    ad_pokey_init(&pokey, 1512000u, 44100u);
    cyc = pokey_cyc = vg_busy_until = 0;
    vg_running = 0;
    io_n = 0;
    depth = 0;
    in_option = 0x15;
    in_allpot_sw = 0;
    VGLIST = 2;
    VGLIST_AT(1) = 0x20;
    LIVES = 3;
    ATRACT = 0x80;
    S_INTCT = 0x40;
    for (unsigned i = 0; i <= 10; i += 2) EXPOSZ_AT(i + 1) = 0xFF;
}

static void check_case(const char *name, unsigned variant)
{
    printf("Checking %s variant %u\n", name, variant);
    fflush(stdout);
    routine *r = NULL;
    for (unsigned i = 0; i < N_ROUTINES; ++i)
        if (!strcmp(routines[i].name, name)) r = &routines[i];
    if (!r) { fprintf(stderr, "Unknown routine %s\n", name); exit(2); }
    ad_pokey_set_allpot(&pokey, in_allpot_sw);
    memcpy(R_ram, g.ram, sizeof R_ram);
    memcpy(R_vram, g.vram, sizeof R_vram);
    /* Synthetic caller returns to $4000; real ROM handles the entire call. */
    R_ram[0x1FE] = 0xFF;
    R_ram[0x1FF] = 0x3F;
    cpu oracle = {0};
    oracle.read = mem_read;
    oracle.write = mem_write;
    oracle.pc = r->addr;
    oracle.s = 0xFD;
    oracle.p = CPU_C;
    oracle.a = entry_a;
    oracle.x = entry_x;
    open_check(r, &oracle, 0x4000);
    unsigned steps = 0;
    while (oracle.pc != 0x4000 && steps++ < 200000) {
        uint16_t pc = oracle.pc;
        cur_pc = pc;
        uint8_t op = peek(pc);
        cpu_step(&oracle);
        cyc += base_cycles[op] + (is_branch(op) && oracle.pc != (uint16_t)(pc + 2));
        if (oracle.s < stk[0].min_s) stk[0].min_s = oracle.s;
    }
    unsigned long old_cycles_bad = r->cyc_bad;
    int bad = steps >= 200000;
    if (!bad) bad = run_compare(&stk[0], &oracle) || r->cyc_bad != old_cycles_bad;
    ++cases_run;
    if (bad) {
        ++cases_failed;
        printf("Boundary case failed: %s variant %u\n", name, variant);
    }
    depth = 0;
}

int main(void)
{
    /* Every tread command, positive/negative motion components, and firing
     * while driving. Inputs reach the real POKEY pins before ROM execution. */
    static const uint8_t headings[] = {0, 63, 128, 255};
    for (unsigned command = 0; command < 16; ++command)
        for (unsigned heading = 0; heading < sizeof headings; ++heading)
            for (unsigned fire = 0; fire < 2; ++fire) {
                reset_case();
                in_allpot_sw = (uint8_t)((command ^ 15) | (fire ? K_PL1FIR : 0));
                TANGLE = headings[heading];
                LANGLE = 0x80;
                TPOSX = TPOSY = 0xFF;
                TPOSX_AT(1) = 0x20;
                TPOSY_AT(1) = 0x30;
                TPOSX_AT(3) = 0x60;
                check_case("MOTION", command * 8 + heading * 2 + fire);
            }
    /* Player/enemy contact at the normal and bomber distance thresholds.
     * Put the pair away from the first obstacle so both contact kinds run. */
    static const unsigned separations[] = {0, 0x3FF, 0x400, 0x5FF, 0x600, 0xFFFF};
    for (unsigned bomber = 0; bomber < 2; ++bomber)
        for (unsigned separation = 0; separation < sizeof separations / sizeof separations[0]; ++separation) {
            reset_case();
            TPOSX_AT(1) = 0x20;
            TPOSY_AT(1) = TPOSY_AT(3) = 0x30;
            unsigned enemy_x = (0x2000 + separations[separation]) & 0xFFFF;
            TPOSX_AT(2) = (uint8_t)enemy_x;
            TPOSX_AT(3) = (uint8_t)(enemy_x >> 8);
            R2D3FL = bomber ? 0x80 : 0;
            check_case("OBJOBJ", bomber * 6 + separation);
        }
    static const uint8_t letters[] = {0x16, 0x18, 0x48, 0x4A};
    for (unsigned controls = 0; controls < 4; ++controls)
        for (unsigned timer = 0; timer < 3; ++timer)
            for (unsigned letter = 0; letter < sizeof letters; ++letter) {
                reset_case();
                in_allpot_sw = (uint8_t)controls;
                LETR = 1;
                LTIMER = (uint8_t)timer;
                STINTL_AT(1) = letters[letter];
                check_case("LETCHK", controls * 12 + timer * 4 + letter);
            }
    static const uint8_t velocities[] = {0, 1, 3, 4, 127, 128, 132, 133, 252, 255};
    for (unsigned v = 0; v < sizeof velocities; ++v) {
        reset_case();
        for (unsigned i = 0; i <= 10; i += 2) {
            EXPOSZ_AT(i) = (uint8_t)(i & 2 ? 255 : 0);
            EXPOSZ_AT(i + 1) = (uint8_t)(i & 4 ? 127 : 1);
            ZVELOC_AT(i) = velocities[v];
            EXPOSX_AT(i) = EXPOSY_AT(i) = 255;
            EXPOSO_AT(i) = (uint8_t)(i & 2 ? 0 : 255);
        }
        check_case("EXPLDE", v);
    }
    for (unsigned type = 0; type < 3; ++type)
        for (unsigned distance = 0; distance < 3; ++distance) {
            reset_case();
            FIRECT = 0x7F;
            TPOSX_AT(3) = (uint8_t)(distance * 8);
            R2D3FL = type == 2 ? 0x80 : 0;
            NOR2D3 = type == 1 ? 5 : 0;
            HITS = 0x99;
            check_case("QWIKCK", type * 3 + distance);
        }
    for (unsigned bonus = 0; bonus < 4; ++bonus)
        for (unsigned edge = 0; edge < 4; ++edge) {
            reset_case();
            in_option = (uint8_t)(bonus * 16);
            uint8_t threshold = peek((uint16_t)(A_BONTBL + bonus));
            TEMP1 = threshold;
            HITS = (uint8_t)(threshold + (edge & 1));
            TEMP1_AT(1) = (uint8_t)(edge == 3);
            HITS_AT(1) = (uint8_t)(edge >= 2);
            check_case("NEWLIF", bonus * 4 + edge);
        }
    for (unsigned angle = 0; angle < 256; angle += 17) {
        reset_case();
        TANGLE = (uint8_t)angle;
        TPOSX = TPOSY = 255;
        TPOSX_AT(1) = TPOSY_AT(1) = 127;
        check_case("R2D3CK", angle);
        reset_case();
        TANGLE = (uint8_t)angle;
        RGOAL = (uint8_t)(angle + 127);
        FRAME = (uint8_t)angle;
        R2D3FL = 0x80;
        EXPOSZ_AT(0x0D) = 0x20;
        TPOSX_AT(3) = 0x40;
        check_case("BUZBOM", angle);
    }
    /* Coin mechanisms: every coin-switch pattern with the slam switch open
     * and closed, across the debounce, slam-timer, post-coin-timer, coin
     * multiplier, bonus-adder and coin-counter states. */
    static const uint8_t debounce_states[] = {0x00, 0x01, 0x1A, 0x1B, 0x1F, 0xC5, 0xE0, 0xE5};
    static const uint8_t post_coin_timers[] = {0, 1, 5};
    static const uint8_t coin_modes[] = {0x00, 0x01, 0x12, 0x1F, 0xEE, 0x63, 0x7D, 0xA2};
    static const uint8_t counter_pulses[][3] = {{0, 0, 0}, {0x02, 0, 0x31}, {0, 0x05, 0}, {0x10, 0x10, 0x10}};
    unsigned moolah_case = 0;
    for (unsigned switches = 0; switches < 16; ++switches)
        for (unsigned debounce = 0; debounce < sizeof debounce_states; ++debounce)
            for (unsigned post = 0; post < sizeof post_coin_timers; ++post, ++moolah_case) {
                reset_case();
                /* Coin switches (bits 0-2) and the slam switch (bit 3) are active low. */
                in_in0 = (uint8_t)(0x3F & ~((switches & 7) | ((switches & 8) ? 0x08 : 0)));
                for (unsigned mech = 0; mech < 3; ++mech) {
                    S_CNSTT_AT(mech) = debounce_states[(debounce + mech) % sizeof debounce_states];
                    S_PSTSL_AT(mech) = post_coin_timers[(post + mech) % sizeof post_coin_timers];
                    S_CCTIM_AT(mech) = counter_pulses[moolah_case % 4][mech];
                }
                S_LMTIM = (uint8_t)(moolah_case % 5 == 0 ? 3 : 0);
                S_CMODE = coin_modes[moolah_case % sizeof coin_modes];
                S_INTCT = (uint8_t)(0x06 + moolah_case % 2);
                S_BCCNT = (uint8_t)(moolah_case % 7);
                S_CNCT = (uint8_t)(moolah_case % 3);
                S_S_CRDT = (uint8_t)(moolah_case % 4);
                check_case("MOOLAH", moolah_case);
            }
    /* SNDON: every single request bit, several bits at once (highest wins),
     * and the fallback index for every effect. */
    static const uint8_t requests[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x03, 0x81, 0xFF, 0x00};
    for (unsigned request = 0; request < sizeof requests; ++request)
        for (unsigned fallback = 0; fallback < 8; ++fallback) {
            if (requests[request] != 0 && fallback != 0) continue;
            reset_case();
            for (unsigned channel = 0; channel < 4; ++channel)
                POINT_AT(channel) = FRAMES_AT(channel) = COUNT_AT(channel) = (uint8_t)(0x40 + channel);
            entry_a = requests[request];
            entry_x = (uint8_t)fallback;
            check_case("SNDON", request * 8 + fallback);
        }
    printf("Native game boundaries: %u cases, %u failures\n", cases_run, cases_failed);
    return cases_failed != 0;
}
