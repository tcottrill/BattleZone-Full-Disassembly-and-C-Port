/* AVG command encoding. Public entry points retain the original call
 * contracts; internal emitters work with commands, glyphs and coordinates.
 * Byte order and cycle charges match the board's display-list writer. */
#include "state.h"
#include "vgut.h"
#include "hw.h"

static void put_vector_byte(uint8_t offset, uint8_t value)
{
    cpu_wr((uint16_t)(VGLIST_ADDR + offset), value);
}

void vgadd(uint8_t last_offset)
{
#ifdef LK_MUTATE_VGADD
    unsigned count = last_offset + 2u;
#else
    unsigned count = last_offset + 1u;
#endif
    unsigned next = VGLIST_ADDR + count;
    int crosses_page = VGLIST + count >= 256u;
    CYC(12);                                     /* $7A76-$7A7C */
    VGLIST = (uint8_t)next;
    if (crosses_page) {
        CYC(5);
        VGLIST_AT(1) = (uint8_t)(next / 256u);
    } else CYC(1);
    CYC(6);                                      /* $7A80 */
}

static void finish_vector_command(uint8_t last_offset, uint8_t last_byte)
{
    CYC(11);                                     /* $7AC7-$7ACA */
    put_vector_byte(last_offset, last_byte);
    vgadd(last_offset);
}

static void append_control_word(uint8_t opcode)
{
    CYC(11);                                     /* $7A22-$7A26 */
    put_vector_byte(0, opcode);
    finish_vector_command(1, opcode);
}

void vgrtsl(void)
{
    CYC(5);                                      /* $7A19-$7A1B */
    append_control_word(0xC0);
}

void vghalt(void)
{
    CYC(6); vgcntr();                            /* $7A1D */
    CYC(2);
    append_control_word(0x20);
}

/* Character zero is blank; hexadecimal digits start at character one. */
static void append_glyph(uint8_t character)
{
    uint8_t table_offset = (uint8_t)(character * 2u);
    CYC(19);                                     /* $7A34-$7A3C */
    put_vector_byte(0, cpu_rd((uint16_t)(A_VGMSGA + table_offset)));
    CYC(12);                                     /* $7A3E-$7A42 */
    put_vector_byte(1, cpu_rd((uint16_t)(A_VGMSGA + table_offset + 1)));
    CYC(6); vgadd(1);                            /* $7A44 */
    CYC(10);                                     /* $7A47-$7A48 */
}

int vghex(uint8_t value)
{
    CYC(6);                                      /* $7A2F-$7A32 */
    append_glyph((uint8_t)((value & 15) + 1));
    return 0;
}

int vghexz(uint8_t value, int suppress_zero)
{
    uint8_t digit;
    if (!suppress_zero) { CYC(3); return vghex(value); }
    CYC(4);                                      /* $7A29-$7A2B */
    digit = value & 15;
    if (!digit) {
        CYC(3);
        append_glyph(0);
        return 1;
    }
    CYC(2);
    return vghex(digit);
}

/* AVG jumps address words, with a 12-bit destination field. */
static void append_vector_jump(uint16_t address, uint16_t opcode)
{
    uint16_t instruction = (uint16_t)(opcode | ((address / 2) & 0x0FFF));
    CYC(25);                                     /* $7A4D-$7A5F */
    put_vector_byte(1, (uint8_t)(instruction / 256u));
    put_vector_byte(0, (uint8_t)instruction);
    vgadd(1);
}

void vgjmpl(uint8_t address_high, uint8_t address_low)
{
    CYC(6);
    append_vector_jump((uint16_t)(address_high * 256u + address_low), 0xE000);
}

void vgjsrl(uint8_t address_high, uint8_t address_low)
{
    CYC(9);
    append_vector_jump((uint16_t)(address_high * 256u + address_low), 0xA000);
}

void vgsta1(uint8_t flags)
{
    CYC(3);                                      /* $7A61 */
    vgstat(flags, VGBRIT);
}

void vgstat(uint8_t flags, uint8_t brightness)
{
    CYC(9);                                      /* $7A63-$7A67 */
    vgadd2(brightness, (uint8_t)(flags | 0x60));
}

void vgcntr(void)
{
    CYC(4);                                      /* $7A6A-$7A6C */
    vgadd2(0x40, 0x80);
}

void vgadd2(uint8_t low, uint8_t high)
{
    CYC(18);                                     /* $7A6E-$7A74 */
    put_vector_byte(0, low);
    put_vector_byte(1, high);
    vgadd(1);
}

void vgsca1(uint8_t binary_scale)
{
    CYC(2);                                      /* $7A81 */
    vgscal(binary_scale, 0);
}

void vgscal(uint8_t binary_scale, uint8_t linear_scale)
{
    CYC(9);                                      /* $7A83-$7A87 */
    vgadd2(linear_scale, (uint8_t)(binary_scale | 0x70));
}

void vgdot(uint8_t brightness)
{
    CYC(6);                                      /* $7A8A-$7A8D */
    vgvtr(0, 0, brightness);
}

void vgvtr(uint8_t dx, uint8_t dy, uint8_t brightness)
{
    CYC(3);                                      /* $7A8E */
    VGBRIT = brightness;
    vgvtr1(dx, dy);
}

void vgvtr1(uint8_t dx, uint8_t dy)
{
    uint16_t scaled_x = (uint16_t)((int8_t)dx * 4);
    uint16_t scaled_y = (uint16_t)((int8_t)dy * 4);
    CYC((dx & 0x80) ? 8 : 7);
    CYC(13);
    XCOMP = (uint8_t)scaled_x;
    XCOMP_AT(1) = (uint8_t)(scaled_x / 256u);
    CYC((dy & 0x80) ? 10 : 9);
    CYC(13);
    XCOMP_AT(2) = (uint8_t)scaled_y;
    XCOMP_AT(3) = (uint8_t)(scaled_y / 256u);
#ifdef LK_MUTATE_VGVTR1
    XCOMP_AT(3) |= 1;
#endif
    vgvtr2();
}

void vgvtr2(void)
{
    CYC(2);                                      /* $7AAB */
    vgvctr((uint8_t)A_XCOMP);
}

void vgvctr(uint8_t coordinates)
{
    uint8_t x_high;
    CYC(50);                                     /* $7AAD-$7AC5 */
    put_vector_byte(0, g.ram[(uint8_t)(coordinates + 2)]);
    put_vector_byte(1, (uint8_t)(g.ram[(uint8_t)(coordinates + 3)] & 0x1F));
    put_vector_byte(2, g.ram[coordinates]);
    x_high = (uint8_t)((g.ram[(uint8_t)(coordinates + 1)] & 0x1F) | (VGBRIT & 0xE0));
    finish_vector_command(3, x_high);
}
