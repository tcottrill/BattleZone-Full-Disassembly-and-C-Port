/* bzsoun.c - BZSOUN.MAC ($7864-$7A18): the POKEY sound-effect sequencer.
 *
 * Each effect owns one 4-byte row of PNTRS: one envelope-record offset per
 * POKEY channel (0 = leave that channel alone).  An envelope record is
 * STVAL/FRCNT/CHANGE/NUMBER: a start value, frames per step, the amount to
 * add each step, and how many steps before moving to the next record.
 *
 * PNTRS ($7864), AUDCV ($7884), IDLEV ($7885) and the envelope records
 * (from $7886) are program-ROM bytes, read through cpu_rd() rather than
 * retyped.
 *
 * Cycle charges reproduce the ROM's total for every path through each
 * routine (lockstep --cycles-strict checks them). */
#include "state.h"
#include "hw.h"
#include "bzsoun.h"


#define A_PNTRS       0x7864  /* 8 effects x 4 channel-pointer bytes, offset from SOUND_7886 */
#define A_AUDCV       0x7884  /* audio control channel value (1 byte) */
#define A_IDLEV       0x7885  /* idle value (1 byte) */
#define A_SOUND_7886  0x7886  /* envelope-record base: 4-byte records, STVAL/FRCNT/CHANGE/NUMBER */

/* SNDON ($7985): see bzsoun.h. */
void sndon(uint8_t request)
{
    unsigned effect;

    if (request == 0) {
        /* The ROM then indexes PNTRS by its own stack pointer, which always
         * lands past the end of the table: nothing starts. */
        CYC(59);
        return;
    }

    /* The highest requested bit wins; the scan costs 7 cycles a bit. */
    effect = 7;
    while (!(request & (1u << effect))) --effect;
    CYC(19 + 4 + 7 * (7 - effect) + 6 + 2 + 12 + 2);

    for (int channel = 3; channel >= 0; --channel) {
        uint8_t record = cpu_rd((uint16_t)(A_PNTRS + effect * 4u + (unsigned)channel));
        if (record != 0) {
            POINT_AT(channel) = record;
            FRAMES_AT(channel) = 1;  /* first envelope step on the next MODSND */
            COUNT_AT(channel) = 1;
            CYC(4 + 16);
        } else {
            CYC(4 + 3);
        }
        CYC(channel > 0 ? 7 : 6);
    }
    CYC(22);
}

/* Advance one envelope channel.  The result selects the idle-output path,
 * which intentionally omits the shared audio-control write. */
static int advance_envelope(unsigned channel)
{
    uint8_t next_record;
    CYC(4);                                      /* $79BB */
    if (!FRAMES_AT(channel)) { CYC(3); return 1; }
    CYC(2);
    --FRAMES_AT(channel);
    CYC(6);                                      /* $79BF */
    if (FRAMES_AT(channel)) { CYC(3); return 0; }
    CYC(2);

    next_record = POINT_AT(channel);
    CYC(4);                                      /* $79C3 */
    if (!next_record) { CYC(3); return 0; }
    CYC(2);
    --COUNT_AT(channel);
    CYC(6);                                      /* $79C7 */
    if (COUNT_AT(channel)) {
        /* Continue the current ramp, using the preceding record. */
        CYC(3);
        FRAMES_AT(channel) = cpu_rd((uint16_t)(A_SOUND_7886 + K_FRCNT - 4 + next_record));
        CYC(8);                                  /* $79FD-$7A00 */
        CURRENT_AT(channel) = (uint8_t)(CURRENT_AT(channel) +
            cpu_rd((uint16_t)(A_SOUND_7886 + K_CHANGE - 4 + next_record)));
        CYC(14);                                 /* $7A02-$7A08 */
        return 0;
    }

    CYC(2);
    CURRENT_AT(channel) = cpu_rd((uint16_t)(A_SOUND_7886 + K_STVAL + next_record));
    CYC(8);                                      /* $79CB-$79CE */
    FRAMES_AT(channel) = cpu_rd((uint16_t)(A_SOUND_7886 + K_FRCNT + next_record));
    CYC(8);                                      /* $79D0-$79D3 */
    if (FRAMES_AT(channel)) {
        CYC(3);
        COUNT_AT(channel) = cpu_rd((uint16_t)(A_SOUND_7886 + K_NUMBER + next_record));
        CYC(8);                                  /* $79EE-$79F1 */
        POINT_AT(channel) = (uint8_t)(next_record + 4);
        CYC(12 + 3);                             /* $79F3-$79FA */
        return 0;
    }

    CYC(2);
    POINT_AT(channel) = 0;
    CYC(4 + 2);                                  /* $79D7-$79D9 */
    CYC(channel ? 3 : 2);
    /* Ending channel zero stops the entire effect. Other channels still
     * publish the terminating record's value on this tick. */
    return channel == 0;
}

void modsnd(void)
{
    int channel;
    CYC(2);                                      /* $79B9 */
    for (channel = 3; channel >= 0; --channel) {
        if (advance_envelope((unsigned)channel)) {
            CURRENT_AT(channel) = cpu_rd(A_IDLEV);
            CYC(8);                              /* $79DC-$79DF */
            hw_pokey_write((uint8_t)channel, CURRENT_AT(channel));
            CYC(5);                              /* $79E1 */
            POINT_AT(channel) = 0;
            CYC(6);                              /* $79E4-$79E6 */
            CYC(channel > 0 ? 5 : 7);             /* $79E8-$79EB */
        } else {
            hw_pokey_write((uint8_t)channel, CURRENT_AT(channel));
            CYC(9);                              /* $7A0A-$7A0C */
            hw_pokey_write(0x08, cpu_rd(A_AUDCV));
            CYC(8);                              /* $7A0F-$7A12 */
            CYC(channel > 0 ? 5 : 4);             /* $7A15-$7A16 */
        }
    }
    CYC(6);                                      /* $7A18 */
}
