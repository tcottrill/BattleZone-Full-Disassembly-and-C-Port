/* bzone_nmi.h - the Battlezone NMI handler (BZONE.MAC $558F-$56C2). */
#ifndef BZONE_NMI_H
#define BZONE_NMI_H

/* NMI ($558F): the 6502 NMI vector (.VCTRS at $7FFA). Fires every 6144 CPU
 * cycles (246.09 Hz) while IN0 bit4 (self test) is clear - PLAN.md. No
 * inputs/outputs (interrupt); every RAM/hardware access happens in ROM
 * order, register save/restore modelled as nothing (see bzone_nmi.c). */
void nmi(void);

#endif  /* BZONE_NMI_H */
