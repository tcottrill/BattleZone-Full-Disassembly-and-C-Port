/* emu_roms.c - bz_emu: the ROM images, loaded from a MAME Battlezone zip.
 *
 * bz_win.exe links the generated arrays progrom.c / vecrom.c / mbprom.c
 * (c_src\tools\gen_roms.py, from the source-exact image = MAME's bzonea).
 * The emulator links THIS file instead: the same four symbols, as writable
 * storage, filled at start-up from a zip with the Windows backend's miniz
 * (c_src\platform\windows\miniz.c).  The shared files that read them - state.c
 * (cpu_rd), avg.c (the vector ROM), mathbox.c (the mapping PROM and the
 * microcode) - are compiled unchanged from ..\c_src and see `extern const`
 * declarations; the definitions below have no `const`, which is why this file
 * must not include progrom.h / vecrom.h / mbprom.h (C linkage: the symbol
 * names are identical, the linker binds them).
 *
 * Layouts (file name -> CPU address; MAME's bzone.cpp ROM table, also the
 * project's bzone.cpp):
 *   bzone    036414-02.e1 $5000 (CRC 13de36d5)  036413-01.h1 $5800
 *            036412-01.j1 $6000  036411-01.k1 $6800  036410-01.lm1 $7000
 *            036409-01.n1 $7800;  vector 036422-01.bc3 $3000, 036421-01.a3 $3800
 *   bzonea   bzone with 036414-01.e1 (CRC efbc3fa0) - Atari's own source
 *            assembles to exactly this image (disasm\, memory note
 *            bzone-disasm-approach); the C port's progrom.c is this set.
 *   old MAME names (036414a.01 ... with dots): "bzone2" = 036414a.01 (the
 *            13de36d5 part), "bzone" = 036414.01 (efbc3fa0).  A merged old
 *            zip holding both loads the 036414a.01 layout (listed first).
 *   Mathbox, every set: 036174-01.b1 (32 bytes) = mb_map; 036180-01.f1,
 *            036179-01.h1, 036178-01.j1, 036177-01.k1, 036176-01.l1,
 *            036175-01.m1 = microcode nibbles 0..5 (bits 3-0 .. 23-20),
 *            assembled exactly as gen_roms.py does (MB_UCODE there: the
 *            REVERSE of the part-number order; = Tempest's 127.e1..132.l1).
 *   036408-01.k7 (the AVG state PROM) is not needed: avg.c counts the state
 *   machine's cycles itself.
 * The board decodes 15 address bits: $8000-$FFFF mirrors $0000-$7FFF (the
 * 6502 vectors at $FFFA come from $7FFA) - the memory map's business
 * (emu_main.cpp: the core's address mask), not the image's.
 *
 * The set is recognised by the file names in the zip (any directory prefix
 * ignored, case-insensitive): the first layout whose every file is present.
 * The zip directory's CRC32 of each file is compared with the table; a
 * mismatch is a warning - a hacked or patched set still runs.
 *
 * tests\romcheck.c (/DEMU_ROMS_NO_BACKEND, the four symbols renamed emu_*)
 * proves roms\bzonea.zip byte-identical to c_src's generated arrays.
 */
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "platform/windows/miniz.h"
#include "emu_roms.h"

#if !defined(EMU_ROMS_NO_BACKEND) && !defined(EMU_HEADLESS)
#include <windows.h>
#include "platform/windows/log.h"
#define ROMS_INFO(...)  LOG_INFO(__VA_ARGS__)
#define ROMS_WARN(...)  LOG_WARN(__VA_ARGS__)
#define ROMS_ERROR(...) LOG_ERROR(__VA_ARGS__)
#else
#include <windows.h>
#define ROMS_INFO(...)  (printf("roms: " __VA_ARGS__), printf("\n"))
#define ROMS_WARN(...)  (printf("roms: WARNING: " __VA_ARGS__), printf("\n"))
#define ROMS_ERROR(...) (printf("roms: ERROR: " __VA_ARGS__), printf("\n"))
#endif

/* ------------------------------------------------------------------ */
/* the images (what progrom.h / vecrom.h / mbprom.h declare)           */
/* ------------------------------------------------------------------ */

uint8_t  progrom[0x3000];      /* program ROM, CPU $5000-$7FFF: progrom[addr - $5000] */
uint8_t  vecrom[0x1000];       /* vector ROM,  CPU $3000-$3FFF: vecrom[addr - $3000]  */
uint8_t  mb_map[32];           /* 036174-01.b1: Mathbox write offset -> uPC           */
uint32_t mb_ucode[256];        /* 036175..036180-01: 24-bit microcode words           */

/* ------------------------------------------------------------------ */
/* the sets                                                            */
/* ------------------------------------------------------------------ */

enum { R_PROG, R_VEC, R_MAP, R_NIB };

typedef struct {
    const char *name;          /* file name in the zip                         */
    uint8_t     region;        /* R_*                                          */
    uint16_t    addr;          /* CPU address (R_PROG / R_VEC); nibble no. k (R_NIB) */
    uint16_t    size;
    uint32_t    crc;           /* CRC32 of the good dump                       */
} rom_file;

/* the parts every set shares, by name style (dash = current MAME, dot = old) */
#define SET_COMMON(sep) \
    { "036413" sep "01" SUFFIX_H1,  R_PROG, 0x5800, 0x0800, 0x5d9d9111 }, \
    { "036412" sep "01" SUFFIX_J1,  R_PROG, 0x6000, 0x0800, 0xab55cbd2 }, \
    { "036411" sep "01" SUFFIX_K1,  R_PROG, 0x6800, 0x0800, 0xad281297 }, \
    { "036410" sep "01" SUFFIX_LM1, R_PROG, 0x7000, 0x0800, 0x0b7bfaa4 }, \
    { "036409" sep "01" SUFFIX_N1,  R_PROG, 0x7800, 0x0800, 0x1e14e919 }, \
    { "036422" sep "01" SUFFIX_BC3, R_VEC,  0x3000, 0x0800, 0x7414177b }, \
    { "036421" sep "01" SUFFIX_A3,  R_VEC,  0x3800, 0x0800, 0x8ea8f939 }, \
    { "036174" sep "01" SUFFIX_B1,  R_MAP,  0,      0x0020, 0x8b04f921 }, \
    { "036180" sep "01" SUFFIX_F1,  R_NIB,  0,      0x0100, 0x276eadd5 }, \
    { "036179" sep "01" SUFFIX_H1,  R_NIB,  1,      0x0100, 0x823b61ae }, \
    { "036178" sep "01" SUFFIX_J1,  R_NIB,  2,      0x0100, 0x09f5a4d5 }, \
    { "036177" sep "01" SUFFIX_K1,  R_NIB,  3,      0x0100, 0x8119b847 }, \
    { "036176" sep "01" SUFFIX_L1,  R_NIB,  4,      0x0100, 0xb31f6e24 }, \
    { "036175" sep "01" SUFFIX_M1,  R_NIB,  5,      0x0100, 0x2af82e87 }, \
    { NULL, 0, 0, 0, 0 }

#define SUFFIX_H1  ".h1"
#define SUFFIX_J1  ".j1"
#define SUFFIX_K1  ".k1"
#define SUFFIX_LM1 ".lm1"
#define SUFFIX_N1  ".n1"
#define SUFFIX_BC3 ".bc3"
#define SUFFIX_A3  ".a3"
#define SUFFIX_B1  ".b1"
#define SUFFIX_F1  ".f1"
#define SUFFIX_L1  ".l1"
#define SUFFIX_M1  ".m1"

static const rom_file set_bzone[] = {
    { "036414-02.e1", R_PROG, 0x5000, 0x0800, 0x13de36d5 },
    SET_COMMON("-")
};
static const rom_file set_bzonea[] = {
    { "036414-01.e1", R_PROG, 0x5000, 0x0800, 0xefbc3fa0 },
    SET_COMMON("-")
};

#undef SUFFIX_H1
#undef SUFFIX_J1
#undef SUFFIX_K1
#undef SUFFIX_LM1
#undef SUFFIX_N1
#undef SUFFIX_BC3
#undef SUFFIX_A3
#undef SUFFIX_B1
#undef SUFFIX_F1
#undef SUFFIX_L1
#undef SUFFIX_M1
#define SUFFIX_H1  ""
#define SUFFIX_J1  ""
#define SUFFIX_K1  ""
#define SUFFIX_LM1 ""
#define SUFFIX_N1  ""
#define SUFFIX_BC3 ""
#define SUFFIX_A3  ""
#define SUFFIX_B1  ""
#define SUFFIX_F1  ""
#define SUFFIX_L1  ""
#define SUFFIX_M1  ""

static const rom_file set_old_bzone2[] = {
    { "036414a.01", R_PROG, 0x5000, 0x0800, 0x13de36d5 },
    SET_COMMON(".")
};
static const rom_file set_old_bzone[] = {
    { "036414.01", R_PROG, 0x5000, 0x0800, 0xefbc3fa0 },
    SET_COMMON(".")
};

static const struct { const char *name; const char *what; const rom_file *files; } sets[] = {
    { "bzone",              "MAME parent, 036414-02",               set_bzone      },
    { "bzonea",             "036414-01, the source-exact image",    set_bzonea     },
    { "bzone2 (old names)", "036414a.01 = MAME bzone's program",    set_old_bzone2 },
    { "bzone (old names)",  "036414.01 = MAME bzonea's program",    set_old_bzone  },
};
#define N_SETS ((int)(sizeof sets / sizeof sets[0]))

static const char *loaded_set = "";
static char        loaded_zip[1024];
static unsigned    crc_mismatches;

const char *emu_roms_set_name(void)       { return loaded_set; }
const char *emu_roms_zip_path(void)       { return loaded_zip; }
unsigned    emu_roms_crc_mismatches(void) { return crc_mismatches; }

/* ------------------------------------------------------------------ */
/* loading                                                             */
/* ------------------------------------------------------------------ */

static int locate(mz_zip_archive *zip, const char *name)
{
    return mz_zip_reader_locate_file(zip, name, NULL, MZ_ZIP_FLAG_IGNORE_PATH);   /* case-insensitive by default */
}

/* How many of a set's files the zip holds; *first_missing = the first absent one. */
static int count_present(mz_zip_archive *zip, const rom_file *f, int *total, const char **first_missing)
{
    int have = 0;
    *total = 0;
    *first_missing = NULL;
    for (; f->name; f++) {
        (*total)++;
        if (locate(zip, f->name) >= 0) have++;
        else if (!*first_missing) *first_missing = f->name;
    }
    return have;
}

int emu_roms_load(const char *zip_path, char *err, size_t err_len)
{
    mz_zip_archive zip;
    const rom_file *f;
    uint8_t nib[6][256];
    uint8_t buf[0x0800];
    int set = -1, best = -1, best_have = -1, i;
    const char *best_missing = NULL;

    if (err_len) err[0] = '\0';
    memset(&zip, 0, sizeof zip);
    if (!mz_zip_reader_init_file(&zip, zip_path, 0)) {
        snprintf(err, err_len, "cannot open ROM zip %s", zip_path);
        return 1;
    }
    for (i = 0; i < N_SETS; i++) {
        int total;
        const char *missing;
        int have = count_present(&zip, sets[i].files, &total, &missing);
        if (have == total) { set = i; break; }
        if (have > best_have) { best_have = have; best = i; best_missing = missing; }
    }
    if (set < 0) {
        snprintf(err, err_len, "%s is not a Battlezone set: closest layout '%s' lacks %s (and maybe more)",
                 zip_path, sets[best].name, best_missing ? best_missing : "?");
        mz_zip_reader_end(&zip);
        return 1;
    }

    crc_mismatches = 0;
    memset(nib, 0, sizeof nib);
    for (f = sets[set].files; f->name; f++) {
        mz_zip_archive_file_stat st;
        int idx = locate(&zip, f->name);
        if (idx < 0 || !mz_zip_reader_file_stat(&zip, (mz_uint)idx, &st)) {
            snprintf(err, err_len, "%s: cannot stat %s", zip_path, f->name);
            mz_zip_reader_end(&zip);
            return 1;
        }
        if (st.m_uncomp_size != f->size) {
            snprintf(err, err_len, "%s: %s is %u bytes, expected %u", zip_path, f->name,
                     (unsigned)st.m_uncomp_size, (unsigned)f->size);
            mz_zip_reader_end(&zip);
            return 1;
        }
        if (!mz_zip_reader_extract_to_mem(&zip, (mz_uint)idx, buf, f->size, 0)) {
            snprintf(err, err_len, "%s: cannot extract %s (corrupt zip?)", zip_path, f->name);
            mz_zip_reader_end(&zip);
            return 1;
        }
        if ((uint32_t)st.m_crc32 != f->crc) {
            crc_mismatches++;
            ROMS_WARN("%s: CRC32 %08x, expected %08x (set '%s') - not the known good dump, loading it anyway",
                      f->name, (unsigned)st.m_crc32, (unsigned)f->crc, sets[set].name);
        }
        switch (f->region) {
        case R_PROG: memcpy(progrom + (f->addr - 0x5000), buf, f->size); break;
        case R_VEC:  memcpy(vecrom  + (f->addr - 0x3000), buf, f->size); break;
        case R_MAP:  memcpy(mb_map, buf, f->size); break;
        default:     memcpy(nib[f->addr], buf, f->size); break;
        }
    }
    mz_zip_reader_end(&zip);

    /* the microcode words, low nibble first (gen_roms.py) */
    for (i = 0; i < 256; i++) {
        uint32_t w = 0;
        int k;
        for (k = 0; k < 6; k++) w |= (uint32_t)(nib[k][i] & 0x0Fu) << (4 * k);
        mb_ucode[i] = w;
    }
    /* gen_roms.py's order check against MBUCOD.V05: RSTO's "LDL 0 LDAB 16 JMP"
     * (write AL, uPC $20) = $1073B4. */
    if (mb_ucode[0x20] != 0x1073B4u)
        ROMS_WARN("Mathbox microcode fails gen_roms.py's nibble-order check (uPC $20 = %06X, expected 1073B4)",
                  (unsigned)mb_ucode[0x20]);

    loaded_set = sets[set].name;
    snprintf(loaded_zip, sizeof loaded_zip, "%s", zip_path);
    ROMS_INFO("ROM set '%s' (%s) from %s; vectors NMI $%02X%02X RESET $%02X%02X IRQ $%02X%02X; %u CRC mismatch(es)",
              sets[set].name, sets[set].what, zip_path,
              progrom[0x2FFB], progrom[0x2FFA], progrom[0x2FFD], progrom[0x2FFC], progrom[0x2FFF], progrom[0x2FFE],
              crc_mismatches);
    return 0;
}

#ifndef EMU_ROMS_NO_BACKEND
static int file_exists(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

int emu_roms_load_default(const char *override_path, int quiet)
{
    /* emu_src\roms\: beside bz_emu.exe, and one up from tests\bz_emu_smoke.exe */
    static const char *const tries[] = { "roms\\bzone.zip", "..\\roms\\bzone.zip" };
    char exe_dir[MAX_PATH], path[MAX_PATH + 32], err[1400];
    char *slash;
    DWORD n = GetModuleFileNameA(NULL, exe_dir, (DWORD)sizeof exe_dir);
    if (n == 0 || n >= sizeof exe_dir) exe_dir[0] = '\0';
    slash = strrchr(exe_dir, '\\');
    if (slash) slash[1] = '\0'; else exe_dir[0] = '\0';

    if (override_path && override_path[0]) {
        snprintf(path, sizeof path, "%s", override_path);
    } else {
        size_t t;
        for (t = 0; t < sizeof tries / sizeof tries[0]; t++) {
            snprintf(path, sizeof path, "%s%s", exe_dir, tries[t]);
            if (file_exists(path)) break;
        }
    }
    if (!file_exists(path))
        snprintf(err, sizeof err, "ROM zip not found: %s%s", path,
                 override_path && override_path[0] ? "" : " (looked for roms\\bzone.zip beside the exe, then in ..\\roms\\)");
    else if (emu_roms_load(path, err, sizeof err) == 0)
        return 0;
    ROMS_ERROR("%s", err);
#ifndef EMU_HEADLESS
    if (!quiet) MessageBoxA(NULL, err, "Battlezone emulator - ROMs", MB_OK | MB_ICONERROR);
#else
    (void)quiet;
#endif
    return 1;
}
#endif /* EMU_ROMS_NO_BACKEND */
