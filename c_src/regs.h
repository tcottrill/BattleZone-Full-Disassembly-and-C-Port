/* regs.h - register/flag bundle returned by translated routines whose callers
 * consume more than one output (see SIGNATURES.md).  Shared by every module;
 * frozen: do not add fields.
 */
#ifndef REGS_H
#define REGS_H

#include <stdint.h>

typedef struct regs {
    uint8_t a, x, y;     /* 6502 registers at the RTS                     */
    uint8_t c, z, n, v;  /* flags at the RTS, 0 or 1 (only those callers use are valid) */
} regs;

#endif  /* REGS_H */
