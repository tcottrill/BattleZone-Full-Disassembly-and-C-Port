/* emu_roms.h - bz_emu: the ROM loader (emu_roms.c).
 *
 * emu_roms.c DEFINES the four images the shared c_src files link against -
 * progrom[] (state.c cpu_rd), vecrom[] (avg.c, state.c), mb_map[] / mb_ucode[]
 * (mathbox.c) - as writable storage, and fills them from a MAME Battlezone
 * zip.  c_src declares them `extern const` in progrom.h / vecrom.h / mbprom.h;
 * this header deliberately does NOT declare them (a C file that needs the
 * bytes includes those headers, as the emulator seam does).
 */
#ifndef EMU_ROMS_H
#define EMU_ROMS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Load one set from `zip_path`.  0 = ok; nonzero = failed, `err` holds one
 * line saying what is missing.  CRC mismatches are warnings (logged), not
 * errors. */
int emu_roms_load(const char *zip_path, char *err, size_t err_len);

/* Pick the zip and load it: `override_path` (--roms / [main] roms) if given,
 * else roms\bzone.zip beside the exe, then ..\roms\bzone.zip (emu_src\roms\
 * seen from tests\).  On failure the error is logged and, unless `quiet`
 * (or a headless build), shown in a message box.  0 = ok. */
int emu_roms_load_default(const char *override_path, int quiet);

/* After a successful load. */
const char *emu_roms_set_name(void);      /* "bzone", "bzonea", "bzone2 (old names)", "bzone (old names)" */
const char *emu_roms_zip_path(void);
unsigned    emu_roms_crc_mismatches(void);

#ifdef __cplusplus
}
#endif

#endif /* EMU_ROMS_H */
