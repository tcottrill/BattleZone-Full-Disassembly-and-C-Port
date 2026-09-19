/* avgtime.c - Gate T, C side: avg_walk()'s cycle-true draw time (avg.h
 * TIMING) for every AVG picture/character, for a handful of synthetic lists,
 * and (once the oracle exists) for captured game frames.
 * tools\avg_gate_v.py's sibling, tools\avg_prom_sim.py, runs an independent
 * Python transcription of MAME's real AVG state machine on the actual
 * 036408-01.k7 state PROM and compares its cycle count against this
 * program's, instruction stream for instruction stream - Gate T.
 *
 *   tests\avgtime.exe pictures [INDEX] [ROM] [OUT]
 *     Times every ROM picture/character in INDEX (tests\avg_shapes_index.txt,
 *     written by tools\avg_gate_v.py from ../disasm/build/vector_tables.json)
 *     the same way the real display list does: writes JSRL <picture>; HALT
 *     at CPU $2000/$2002 (into a copy of ROM's own vram region - the vrom
 *     region $3000+ is untouched, the real picture bytes), and walks from
 *     $2000.  This "cycles from JSRL to the matching RTSL, then the HALT"
 *     is exactly what BZONE.MAC's own display-list builder pays every time
 *     it calls one of these pictures, and it gives a clean, hardware-real
 *     stopping condition (a genuine pushed return address, not a
 *     depth-zero special case) - see NOTES_avg.md "Timing".
 *     Default INDEX tests\avg_shapes_index.txt, ROM
 *     ..\disasm\_survey\roms_extracted\bzone_cpu64k.bin, OUT
 *     tests\avg_out\avg_time_pictures.txt.
 *
 *   tests\avgtime.exe synth [OUT]
 *     Times 5 synthetic lists exercising SCAL binary+linear, CNTR, JSRL
 *     nesting (depth 3) and a lone SVEC - the exact word sequences are
 *     documented below and mirrored byte-for-byte in
 *     tools\avg_prom_sim.py's SYNTH table.  Default OUT
 *     tests\avg_out\avg_time_synth.txt.
 *
 *   tests\avgtime.exe frames DIR [DIR ...]
 *     Tempest-style: for each DIR\frame_NNNN.vram (not produced until the
 *     oracle milestone), walks from $2000 with a fresh state and prints
 *     stop/cycles/ms to stdout.  No Battlezone captures exist yet; kept so
 *     tools\avg_prom_sim.py's frame mode has a C-side counterpart ready.
 *
 * Output format for pictures/synth (one line per test):
 *   TIME NAME ADDR STOP CYCLES
 */
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "..\avg.h"

static uint8_t rom64k[0x10000];

/* Synthetic subroutines, laid out from CPU $2010 in a scratch vram buffer -
 * word offsets from $2000 (index*2 + $2000 = CPU address).  Byte-identical
 * to tools\avg_prom_sim.py's SYNTH table; each word's own encoding is
 * ../disasm/avg.py's avg.encode() (see the comment on each line).
 *
 *   idx  0- 1  (JSRL target; HALT)     - the wrapper, written per test
 *   idx  2- 7  unused (never reached)
 *   A  idx  8-11  $2010  SCAL 3,$40 ; VCTR 500,-300,5 ; RTSL
 *   B  idx 12-15  $2018  CNTR ; VCTR -200,700,2 ; RTSL
 *   C  idx 16-17  $2020  JSRL $2024 ; RTSL
 *   C1 idx 18-20  $2024  SVEC 10,-10,3 ; JSRL $202A ; RTSL
 *   C2 idx 21-23  $202A  SVEC -6,4,7 ; JSRL $2030 ; RTSL
 *   C3 idx 24-25  $2030  SVEC 2,2,1 ; RTSL
 *   D  idx 26-27  $2034  SVEC -30,30,6 ; RTSL
 *   E  idx 28-30  $2038  VCTR 0,0,0 ; RTSL
 */
static const uint16_t SYNTH_WORDS[31] = {
    0x0000, 0x0000,                  /* 0,1: wrapper (overwritten) */
    0, 0, 0, 0, 0, 0,                /* 2-7: unused                */
    0x7340, 0x1ED4, 0xA1F4, 0xC000,  /* 8-11: A  ($2010)           */
    0x8040, 0x02BC, 0x5F38, 0xC000,  /* 12-15: B ($2018)           */
    0xA012, 0xC000,                  /* 16-17: C ($2020)           */
    0x5B65, 0xA015, 0xC000,          /* 18-20: C1 ($2024)          */
    0x42FD, 0xA018, 0xC000,          /* 21-23: C2 ($202A)          */
    0x4121, 0xC000,                  /* 24-25: C3 ($2030)          */
    0x4FD1, 0xC000,                  /* 26-27: D  ($2034)          */
    0x0000, 0x0000, 0xC000           /* 28-30: E  ($2038)          */
};

typedef struct { const char *name; uint16_t addr; } synth_entry;
static const synth_entry SYNTH_ENTRIES[] = {
    { "SYN_A", 0x2010 }, { "SYN_B", 0x2018 }, { "SYN_C", 0x2020 },
    { "SYN_D", 0x2034 }, { "SYN_E", 0x2038 },
};

static void poke_words(uint8_t *buf, const uint16_t *words, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        buf[2 * i]     = (uint8_t)(words[i] & 0xFF);
        buf[2 * i + 1] = (uint8_t)((words[i] >> 8) & 0xFF);
    }
}

/* Wrap `entry` in JSRL <entry>; HALT at $2000/$2002 and time it; returns the
 * avg_result of walking from $2000 (stop should be AVG_STOP_HALT). */
static avg_result time_wrapped(uint8_t *vram4k, const uint8_t *vrom4k, uint16_t entry)
{
    avg_mem m;
    uint16_t jsrl_word = (uint16_t)(0xA000u | (((entry - AVG_VRAM_BASE) >> 1) & 0x1FFFu));

    vram4k[0] = (uint8_t)(jsrl_word & 0xFF);
    vram4k[1] = (uint8_t)((jsrl_word >> 8) & 0xFF);
    vram4k[2] = 0x00;  /* HALT = $2000 */
    vram4k[3] = 0x20;
    m.vram = vram4k;
    m.vrom = vrom4k;
    return avg_walk(&m, AVG_VRAM_BASE, NULL, NULL);
}

static int do_pictures(int argc, char **argv)
{
    const char *idxp = argc > 2 ? argv[2] : "tests\\avg_shapes_index.txt";
    const char *romp = argc > 3 ? argv[3] : "..\\disasm\\_survey\\roms_extracted\\bzone_cpu64k.bin";
    const char *outp = argc > 4 ? argv[4] : "tests\\avg_out\\avg_time_pictures.txt";
    FILE *fi, *fr, *fo;
    uint8_t vram[0x1000];
    char line[256];
    int n = 0;
    size_t nread;

    fr = fopen(romp, "rb");
    if (!fr) { fprintf(stderr, "cannot open ROM image %s\n", romp); return 2; }
    nread = fread(rom64k, 1, sizeof rom64k, fr);
    fclose(fr);
    if (nread != sizeof rom64k) {
        fprintf(stderr, "%s: expected 65536 bytes, got %u\n", romp, (unsigned)nread);
        return 2;
    }
    fi = fopen(idxp, "r");
    if (!fi) { fprintf(stderr, "cannot open index %s\n", idxp); return 2; }
    fo = fopen(outp, "w");
    if (!fo) { fprintf(stderr, "cannot write %s (create tests\\avg_out)\n", outp); fclose(fi); return 2; }

    memcpy(vram, rom64k + 0x2000, sizeof vram);
    while (fgets(line, sizeof line, fi)) {
        char name[64];
        unsigned addr;
        avg_result r;

        if (line[0] == '#' || line[0] == '\r' || line[0] == '\n' || line[0] == 0)
            continue;
        if (sscanf(line, "%63s %x", name, &addr) != 2)
            continue;
        if (addr < AVG_VROM_BASE || addr > AVG_SPACE_END - 2u)
            continue;

        r = time_wrapped(vram, rom64k + 0x3000, (uint16_t)addr);
        fprintf(fo, "TIME %s %04X %s %u\n", name, addr, avg_stop_name(r.stop),
                (unsigned)r.cycles);
        n++;
    }
    fclose(fi);
    fclose(fo);
    printf("avgtime pictures: %d timed -> %s\n", n, outp);
    return 0;
}

static int do_synth(int argc, char **argv)
{
    const char *outp = argc > 2 ? argv[2] : "tests\\avg_out\\avg_time_synth.txt";
    uint8_t vram[0x1000];
    uint8_t vrom[0x1000];
    FILE *fo;
    size_t i;

    memset(vram, 0, sizeof vram);
    memset(vrom, 0, sizeof vrom);
    poke_words(vram, SYNTH_WORDS, (int)(sizeof SYNTH_WORDS / sizeof SYNTH_WORDS[0]));

    fo = fopen(outp, "w");
    if (!fo) { fprintf(stderr, "cannot write %s (create tests\\avg_out)\n", outp); return 2; }
    for (i = 0; i < sizeof SYNTH_ENTRIES / sizeof SYNTH_ENTRIES[0]; i++) {
        avg_result r = time_wrapped(vram, vrom, SYNTH_ENTRIES[i].addr);
        fprintf(fo, "TIME %s %04X %s %u\n", SYNTH_ENTRIES[i].name, SYNTH_ENTRIES[i].addr,
                avg_stop_name(r.stop), (unsigned)r.cycles);
    }
    fclose(fo);
    printf("avgtime synth: %u timed -> %s\n",
           (unsigned)(sizeof SYNTH_ENTRIES / sizeof SYNTH_ENTRIES[0]), outp);
    return 0;
}

/* Tempest-style: not exercised until the oracle produces frame_NNNN.vram
 * captures (M1+); kept so tools\avg_prom_sim.py's frame mode has a C-side
 * counterpart ready without another pass over this file. */
static int do_frames(int argc, char **argv)
{
    int a;
    FILE *fr;
    size_t nread;
    const char *romp = "..\\disasm\\_survey\\roms_extracted\\bzone_cpu64k.bin";

    if (argc < 3) {
        fprintf(stderr, "usage: avgtime frames DIR [DIR ...]\n");
        return 2;
    }
    fr = fopen(romp, "rb");
    if (!fr) { fprintf(stderr, "cannot open ROM image %s\n", romp); return 2; }
    nread = fread(rom64k, 1, sizeof rom64k, fr);
    fclose(fr);
    if (nread != sizeof rom64k) {
        fprintf(stderr, "%s: expected 65536 bytes, got %u\n", romp, (unsigned)nread);
        return 2;
    }
    for (a = 2; a < argc; a++) {
        int n;
        for (n = 0; n <= 9999; n++) {
            char path[512];
            FILE *f;
            uint8_t vram[0x1000];
            avg_mem m;
            avg_result r;

            sprintf(path, "%s\\frame_%04d.vram", argv[a], n);
            f = fopen(path, "rb");
            if (!f) continue;
            if (fread(vram, 1, sizeof vram, f) != sizeof vram) { fclose(f); continue; }
            fclose(f);
            m.vram = vram;
            m.vrom = rom64k + 0x3000;  /* caller must have loaded the ROM */
            r = avg_walk(&m, AVG_VRAM_BASE, NULL, NULL);
            printf("frame_%04d %-6s ops=%u cycles=%u ms=%.4f\n", n, avg_stop_name(r.stop),
                   (unsigned)r.ops, (unsigned)r.cycles, avg_cycles_ms(r.cycles));
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: avgtime pictures|synth|frames ...\n");
        return 2;
    }
    if (strcmp(argv[1], "pictures") == 0) return do_pictures(argc, argv);
    if (strcmp(argv[1], "synth") == 0)    return do_synth(argc, argv);
    if (strcmp(argv[1], "frames") == 0)   return do_frames(argc, argv);
    fprintf(stderr, "unknown mode %s\n", argv[1]);
    return 2;
}
