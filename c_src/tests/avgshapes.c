/* avgshapes.c - Gate V, C side: walk every AVG picture / VGAN character in
 * the Battlezone vector ROM with avg_walk() and dump the segments to a
 * plain-text file.  tools\avg_gate_v.py does the actual comparison against
 * ../disasm/vrender.py (this program has no Python dependency and no
 * knowledge of what "correct" looks like - it just runs the walker and
 * writes down what happened, so it stays pure C11 / no platform includes).
 *
 *   tests\avgshapes.exe [INDEX] [ROM] [OUT]
 *     INDEX  default tests\avg_shapes_index.txt
 *            "# comment" or blank lines ignored; otherwise "NAME ADDRHEX"
 *            per line (ADDRHEX = 4 hex digits, no $) - written by
 *            tools\avg_gate_v.py from ../disasm/build/vector_tables.json,
 *            so the address list always comes from the generated symbol
 *            data, never hand-transcribed here.
 *     ROM    default ..\disasm\_survey\roms_extracted\bzone_cpu64k.bin
 *            the same 64K CPU image ../disasm/avg.py loads; vram = bytes
 *            $2000-$2FFF, vrom = bytes $3000-$3FFF of that one image, so
 *            both sides of Gate V start from byte-identical memory (no
 *            "zero the vram" special-casing - see avg.h's "ACCESSOR").
 *     OUT    default tests\avg_out\avg_shapes.txt
 *
 * Each picture/character is walked from a fresh avg_state_init() (beam at
 * (0,0), scale Q15 1.0, no clip window armed - see avg.h "COORDINATES"),
 * budget 4000 ops (../disasm/vrender.py's MAX_STEPS), matching exactly how
 * ../disasm/emit_shapes.py calls vrender.walk() for the same pictures.
 *
 * Output format, one block per INDEX line:
 *   SHAPE NAME ADDR NSEG STOP
 *   x0 y0 x1 y1 intensity clip      (repeated NSEG times, Q15 integers)
 *   END
 */
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "..\avg.h"

#define MAX_SEGS   4096

static uint8_t rom64k[0x10000];

int main(int argc, char **argv)
{
    const char *idxp = argc > 1 ? argv[1] : "tests\\avg_shapes_index.txt";
    const char *romp = argc > 2 ? argv[2] : "..\\disasm\\_survey\\roms_extracted\\bzone_cpu64k.bin";
    const char *outp = argc > 3 ? argv[3] : "tests\\avg_out\\avg_shapes.txt";
    FILE *fi, *fr, *fo;
    avg_mem m;
    static avg_seg segs[MAX_SEGS];
    char line[256];
    int nshapes = 0, nsegs_total = 0, overflow = 0;
    size_t nread;

    fr = fopen(romp, "rb");
    if (!fr) { fprintf(stderr, "cannot open ROM image %s\n", romp); return 2; }
    nread = fread(rom64k, 1, sizeof rom64k, fr);
    fclose(fr);
    if (nread != sizeof rom64k) {
        fprintf(stderr, "%s: expected 65536 bytes, got %u\n", romp, (unsigned)nread);
        return 2;
    }
    m.vram = rom64k + 0x2000;
    m.vrom = rom64k + 0x3000;

    fi = fopen(idxp, "r");
    if (!fi) { fprintf(stderr, "cannot open index %s\n", idxp); return 2; }
    fo = fopen(outp, "w");
    if (!fo) { fprintf(stderr, "cannot write %s (create tests\\avg_out)\n", outp); fclose(fi); return 2; }

    while (fgets(line, sizeof line, fi)) {
        char name[64];
        unsigned addr;
        avg_state st;
        avg_cfg cfg;
        avg_result r;
        uint32_t i;

        if (line[0] == '#' || line[0] == '\r' || line[0] == '\n' || line[0] == 0)
            continue;
        if (sscanf(line, "%63s %x", name, &addr) != 2)
            continue;
        if (addr < AVG_VROM_BASE || addr > AVG_SPACE_END - 2u) {
            fprintf(stderr, "%s: address $%04X out of range, skipped\n", name, addr);
            continue;
        }

        avg_state_init(&st);
        memset(&cfg, 0, sizeof cfg);
        cfg.segs = segs;
        cfg.seg_cap = MAX_SEGS;
        cfg.max_ops = 4000;  /* ../disasm/vrender.py MAX_STEPS    */
        r = avg_walk(&m, (uint16_t)addr, &st, &cfg);

        fprintf(fo, "SHAPE %s %04X %u %s\n", name, addr,
                (unsigned)r.nseg_stored, avg_stop_name(r.stop));
        for (i = 0; i < r.nseg_stored; i++) {
            const avg_seg *s = &segs[i];
            fprintf(fo, "%ld %ld %ld %ld %u %u\n",
                    (long)s->x0, (long)s->y0, (long)s->x1, (long)s->y1,
                    (unsigned)s->intensity, (unsigned)s->clip);
        }
        fprintf(fo, "END\n");

        nshapes++;
        nsegs_total += (int)r.nseg_stored;
        if (r.flags & AVG_FLAG_STACK_OVER) overflow++;
        if (r.nseg > r.nseg_stored)
            fprintf(stderr, "%s: %u segments overflowed the %d-segment buffer\n",
                    name, r.nseg - r.nseg_stored, MAX_SEGS);
    }
    fclose(fi);
    fclose(fo);

    printf("avgshapes: %d shapes, %d segments, %d with stack overflow -> %s\n",
           nshapes, nsegs_total, overflow, outp);
    return nshapes > 0 ? 0 : 1;
}
