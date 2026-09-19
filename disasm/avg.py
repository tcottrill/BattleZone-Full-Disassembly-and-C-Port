"""Atari Analog Vector Generator (AVG) decoder/encoder for Battlezone.

Adapted from the Tempest disassembly's avg.py. Battlezone's AVG is
monochrome (intensity only, no colour RAM), and adds a windowed-clipping
mode to STAT that Tempest's chip does not use. Semantics were checked
against:

  * battlezone-main/VGMC.MAC (Ed Logg's macros: VCTR (auto long/short),
    STAT, SCAL, CNTR, HALT, JSRL, RTSL, JMPL)
  * the AAE/MAME AVG core (avg_common in mame_late_avgdvg.cpp, used
    unmodified - not the *coloured* variants tempest/mhavoc use)
  * ../bzone.cpp (the AAE Battlezone driver) for the VECRAM/VECROM map

Opcode = word >> 13 (the first word, little-endian in the ROM):

  0 VCTR  2 words: w1 = DY & $1FFF, w2 = Z<<13 | DX & $1FFF  (13-bit two's
          complement deltas). Long form.
  1 HALT  $2000  (not used anywhere in the $3000-$3FFF ROM image - every
          AVG subroutine here ends with RTSL; kept for completeness/decode
          safety).
  2 SVEC  short vector, 1 word: $4000 | Z<<5 | (DX/2)&$1F | ((DY/2)&$1F)<<8
          VGMC's VCTR macro picks this automatically when |DX| and |DY|
          are both even and <= 30 (mask ^H0FFE1 test on |DX|!|DY|).
  3 STAT  $6xxx when bit 12 ($1000) clear.
          STAT Z            -> one=1,hi=0,in=0 -> $6400 | Z<<4
          STAT Z,HI,IN      -> one=0            -> $6000 | HI<<9 | IN<<8 | Z<<4
          (VGMC.MAC: "...1=1" when HI/IN blank; HI/LOW picks the corner of
          a clip rectangle from the current beam position, IN/OUT picks
          inside/outside; Z loads the INTENSITY latch 0-7/0-15. No colour
          latch bit exists on this board - Battlezone's AVG is monochrome.)
      SCAL  $7000 | b<<8 | linear  (bit 12 set): binary scale b (0..7),
          linear scale l (factor (255-l)/256 ... $00 = full, $80 = 1/2)
  4 CNTR  $8040 - centre the beam (VGMC's CNTR macro takes no operand, so
          this is the only value the ROM ever holds)
  5 JSRL  $A000 | target/2  (VG address space: CPU $2000 + 2*(w & $1FFF);
          vector RAM $2000-$2FFF, vector ROM $3000-$3FFF share this space)
  6 RTSL  $C000  (VGMC's RTSL macro takes no operand: the ROM only ever
          holds this exact value)
  7 JMPL  $E000 | target/2
"""
import os

VG_BASE = 0x2000
HERE = os.path.dirname(os.path.abspath(__file__))
ROM64K = os.path.join(HERE, "_survey", "roms_extracted", "bzone_cpu64k.bin")
VROM_LO, VROM_HI = 0x3000, 0x3FFF


def load_image(path=ROM64K):
    """64K CPU image as bytes (index = CPU address)."""
    data = open(path, "rb").read()
    assert len(data) == 0x10000, path
    return data


def s13(v):
    """13-bit two's complement delta (VGMC: .WORD DY&^H1FFF)."""
    v &= 0x1FFF
    return v - 0x2000 if v & 0x1000 else v


def s5(v):
    v &= 0x1F
    return v - 0x20 if v & 0x10 else v


def word(mem, a):
    return mem[a] | (mem[a + 1] << 8)


def vg_target(w):
    return VG_BASE + ((w & 0x1FFF) * 2)


def decode_raw(mem, a):
    """-> dict(op, len, fields...) without text formatting."""
    w = word(mem, a)
    top = w >> 13
    if top == 0:
        w2 = word(mem, a + 2)
        return dict(op="VCTR", len=4, dx=s13(w2), dy=s13(w), z=(w2 >> 13) & 7,
                    words=[w, w2])
    if top == 1:
        return dict(op="HALT", len=2, words=[w])
    if top == 2:
        return dict(op="SVEC", len=2, dx=s5(w) * 2, dy=s5(w >> 8) * 2,
                    z=(w >> 5) & 7, words=[w])
    if top == 3:
        if w & 0x1000:
            return dict(op="SCAL", len=2, b=(w >> 8) & 0xF, l=w & 0xFF, words=[w])
        one = (w >> 10) & 1
        return dict(op="STAT", len=2, words=[w], z=(w >> 4) & 0xF,
                    one=one, hi=(w >> 9) & 1, inn=(w >> 8) & 1)
    if top == 4:
        return dict(op="CNTR", len=2, words=[w])
    if top == 5:
        return dict(op="JSRL", len=2, target=vg_target(w), words=[w])
    if top == 6:
        return dict(op="RTSL", len=2, words=[w])
    return dict(op="JMPL", len=2, target=vg_target(w), words=[w])


def decode(mem, a):
    """-> (mnemonic, operand_text, length) in Space Duel listing style."""
    d = decode_raw(mem, a)
    op = d["op"]
    if op in ("VCTR", "SVEC"):
        return op, "%d, %d, %d" % (d["dx"], d["dy"], d["z"]), d["len"]
    if op == "SCAL":
        return op, "%d, $%02X" % (d["b"], d["l"]), 2
    if op == "STAT":
        if d["one"]:
            return op, "%d" % d["z"], 2
        return op, "%d, %d, %d" % (d["z"], d["hi"], d["inn"]), 2
    if op in ("JSRL", "JMPL"):
        return op, "$%04X" % d["target"], 2
    if op == "CNTR" and d["words"][0] != 0x8040:
        return op, "$%04X" % d["words"][0], 2
    if op == "RTSL" and d["words"][0] != 0xC000:
        return op, "$%04X" % d["words"][0], 2
    return op, "", d["len"]


def _ints(s):
    out = []
    for x in s.split(","):
        x = x.strip()
        out.append(int(x[1:], 16) if x.startswith("$") else int(x))
    return out


def encode(mn, oper):
    """Inverse of decode(): (mnemonic, operand text) -> list of 16-bit words,
    or None when the text cannot be encoded. JSRL/JMPL operands must be $hex
    (resolve labels before calling)."""
    oper = (oper or "").strip()
    if mn == "HALT":
        return [0x2000]
    if mn == "RTSL":
        return [_ints(oper)[0]] if oper else [0xC000]
    if mn == "CNTR":
        return [_ints(oper)[0]] if oper else [0x8040]
    if mn == "VCTR":
        dx, dy, z = _ints(oper)
        return [dy & 0x1FFF, ((z & 7) << 13) | (dx & 0x1FFF)]
    if mn == "SVEC":
        dx, dy, z = _ints(oper)
        if dx & 1 or dy & 1:
            return None
        return [0x4000 | ((z & 7) << 5) | ((dx >> 1) & 0x1F) | (((dy >> 1) & 0x1F) << 8)]
    if mn == "SCAL":
        b, l = _ints(oper)
        return [0x7000 | ((b & 0xF) << 8) | (l & 0xFF)]
    if mn == "STAT":
        vals = _ints(oper)
        if len(vals) == 1:
            z = vals[0]
            return [0x6000 | 0x400 | ((z & 0xF) << 4)]
        z, hi, inn = vals
        return [0x6000 | ((hi & 1) << 9) | ((inn & 1) << 8) | ((z & 0xF) << 4)]
    if mn in ("JSRL", "JMPL"):
        t = _ints(oper)[0]
        if t & 1 or not (VG_BASE <= t < VG_BASE + 0x4000):
            return None
        return [(0xA000 if mn == "JSRL" else 0xE000) | ((t - VG_BASE) >> 1)]
    return None


def scal_factor(b, lin):
    """aae_avg: timer shifted b times, linear scale complemented:
    factor = ((~lin)&0xFF)/256 / 2^b  (SCAL 0,$00 ~ full size)."""
    return ((~lin) & 0xFF) / 256.0 / (1 << (b & 7))


def targets(mem, lo=VROM_LO, hi=VROM_HI):
    out, a = set(), lo
    while a < hi:
        d = decode_raw(mem, a)
        if d["op"] in ("JSRL", "JMPL") and lo <= d["target"] <= hi:
            out.add(d["target"])
        a += d["len"]
    return out


if __name__ == "__main__":
    mem = load_image()
    print("Battlezone AVG decode self-check (STAT/CNTR/RTSL occurrences in $3000-$3FFF):")
    a = VROM_LO
    counts = {}
    # NOTE: unlike Tempest, large parts of $3000-$3FFF are NOT an AVG opcode
    # stream (they are 3D point / edge-list tables read directly by the
    # 6502), so a blind linear sweep is not meaningful here. vromsrc.py
    # decodes only the addresses that build/srclines.json says are AVG
    # statements.
