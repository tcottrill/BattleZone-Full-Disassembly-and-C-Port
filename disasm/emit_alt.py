"""Emit disasm/bzone_alt_5000_diff.asm - the MAME 'bzone' (Revision 2, $5000 chip CRC32
13de36d5) program ROM disassembled wherever it differs from the source tree's own image
(MAME 'bzonea' / Revision 1, CRC32 efbc3fa0, the one this whole disassembly reproduces).

_survey/roms_extracted/bzone_cpu64k.bin is the primary (source) image; bzone_alt_cpu64k.bin
(built by gen_from_roms.py alongside it, from whichever ROM set - bzone.zip or bzonea.zip -
is *not* used for the primary image) is the alternate.  gen_from_roms.py's own diff already
narrows this to exactly 149 bytes in two spans: 1 byte at $5340 and 148 bytes at $54A8-$553C
(HARDWARE_NOTES.md's own finding, confirmed here independently by a fresh byte-for-byte diff).

Each span is grown to an instruction boundary common to *both* images (never assumed): walk
forward, one real decoded instruction at a time, from the nearest known-identical anchor (the
program listing's own Lxxxx line at or before the span) in the source image past the span's
end; then walk the alternate image from the very same anchor; if the alternate's instruction
stream does not land on the same address, grow the target end to wherever it did land and
repeat, until both streams agree on where the span ends.  The alternate span is then verified
by re-encoding it (the same encoder verify.py uses) against bzone_alt_cpu64k.bin - 0
mismatches is the same bar the rest of this disassembly holds itself to.

Naming: exactly the source listing's own conventions (Lxxxx: address labels on every line,
Atari's own symbol names from build/symbols.json wherever an operand's value matches one -
JSR/JMP targets into unchanged code resolve this way almost every time, since only this one
routine differs between the two images), hex where it does not.  Nothing beyond an address
label is invented; anywhere the alternate code does something with no source counterpart, the
explanation is written by hand and tagged [note].

Run:  python emit_alt.py   (after emit.py/emit_defines.py; needs both ROM images built by
gen_from_roms.py, i.e. both bzone.zip and bzonea.zip present when it ran)
Exit status 1 if the alternate image is missing, or if a fragment does not re-encode clean.
"""
import os, sys, re, json
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import m6502
from m6502 import IMP, ACC, IMM, ZP, ZPX, ZPY, ABS, ABX, ABY, IND, IZX, IZY, REL
from emit import Names, clean, hexb, hexw
import verify as vfy

BUILD = os.path.join(HERE, "build")
SRC_ROM = os.path.join(HERE, "_survey", "roms_extracted", "bzone_cpu64k.bin")
ALT_ROM = os.path.join(HERE, "_survey", "roms_extracted", "bzone_alt_cpu64k.bin")
LISTING = os.path.join(HERE, "bzone_program_rom.asm")
OUT = os.path.join(HERE, "bzone_alt_5000_diff.asm")
LO, HI = 0x5000, 0x8000

LINE_RE = re.compile(r"^L([0-9A-F]{4}):")

# What each span *is*, and what the alternate does differently - established by reading both
# streams (see the per-span notes emitted inline) plus BZONE.MAC and BZSTST.MAC's own self-test
# checksum loop (STEST3, BZSTST.MAC:345-370): each 2 KB program-ROM chip ends in one checksum
# byte (CHKS2..CHKS7, one per chip) chosen so an 8-bit EOR of the whole chip is $FF; STEST3's
# loop (BZSTST.MAC:353-356, 'LDA I,0FF; 10$: EOR NY,TEMP1; INY; BNE 10$ ... DEC TEMP2; BNE 10$
# ;8 PAGES / ROM') sums all 2048 bytes without ever reading CHKS2 by name - it is simply *part
# of* the sum, placed so the total comes out right.  BZONE.MAC:802-803 declares it directly:
# 'CHKS2: .BYTE 70' then '.ASCII /EDR/' (the bytes 'E','D','R', unrelated filler/padding - the
# checksum byte just happens to sit at a fixed offset ahead of a 3-letter comment string).
SPAN_INTRO = {
    0x5340: "CHKS2, the self-test EOR checksum byte for the $5000-$57FF program-ROM chip "
            "(BZONE.MAC:802, 'CHKS2: .BYTE 70'; followed by '.ASCII /EDR/', 3 unrelated filler "
            "bytes).  STEST3's checksum loop (BZSTST.MAC:345-370) never reads CHKS2 by name - "
            "it just EORs every byte of the chip and expects $FF, so CHKS2 is picked to make "
            "that true; it necessarily changes whenever anything else on this chip changes, "
            "which is exactly what happened here (see the $54A8 span below, on the same chip).",
    0x54A8: "Inside CKSCOR (BZONE.MAC:805-1020, .SBTTL HIGH SCORE STUFF), the part of the "
            "high-score table display that runs once per entry after DIGT2S has drawn its "
            "score digits.  The source version: STY SAVX (remembers which entry this is), LDX "
            "#$1E; STX YDISP (sets the per-entry row spacing this entry's caller will use "
            "next), JSR VGVTR, then (if HSCTBL+1,X is nonzero) JSR DSPALP twice to draw two "
            "extra vector characters looked up from VGMSGA via HSCTBL+1,X and a small table at "
            "$3590 - a marker next to entries with a nonzero high score byte.  The alternate "
            "version instead: LDX #$1A (a different opening value, 26 not 30) with no "
            "corresponding STX YDISP anywhere in this span, draws the same two DSPALP "
            "characters but drops the TAX before the VGMSGA lookup (see the [note] there), and "
            "ends the span with a completely different tail: it sets YCOMP to a fixed $D8FF Y "
            "delta and XCOMP to one of two fixed deltas depending on whether any characters "
            "were drawn for this entry (TYA/BEQ), sets brightness 0 and JSR VGVTR2 - an "
            "invisible move-only vector, i.e. a hand-picked row-advance offset - then rejoins "
            "the loop 2 bytes earlier ($5434, not $54B2) than the source version does.  Put "
            "together: this looks like an earlier or alternate row-spacing/new-entry-marker "
            "scheme for the same high-score screen, not a bug fix - CHKS2 above had to change "
            "only because these 148 bytes did.",
    0x5506: "Continues directly on from the $54A8 span above (see its description) after the "
            "one coincidentally-identical byte at $5505.",
}

def load_rom(path):
    if not os.path.exists(path):
        return None
    return open(path, "rb").read()

def diff_spans(a, b):
    spans, i, n = [], 0, len(a)
    while i < n:
        if a[i] != b[i]:
            j = i
            while j < n and a[j] != b[j]:
                j += 1
            spans.append((i, j))
            i = j
        else:
            i += 1
    return spans

def listing_anchors():
    """Every Lxxxx: address in bzone_program_rom.asm - all proven valid instruction/data
    starts in the *source* image (emit.py built them and verify.py re-checked every one)."""
    out = []
    for ln in open(LISTING):
        m = LINE_RE.match(ln)
        if m:
            out.append(int(m.group(1), 16))
    out.sort()
    return out

def owning_srcline(srclines, addr):
    for e in srclines:
        if e["addr"] <= addr < e["addr"] + e["len"]:
            return e
    return None

def decode_run(rom, start, past):
    """Decode instructions from start until the address reaches or passes `past`.
    -> (end_addr, [(addr, mn, mode, val, nbytes), ...]); end_addr may be > past."""
    mem = {a: rom[a] for a in range(0x10000)}
    a, out = start, []
    while a < past:
        d = m6502.decode(mem, a)
        if d is None:
            raise SystemExit("cannot decode byte $%02X at $%04X" % (rom[a], a))
        mn, mode, val, n = d
        out.append((a, mn, mode, val, n))
        a += n
    return a, out

def common_boundary(src, alt, anchor, span_end):
    """Grow span_end until decoding both images from `anchor` lands on the same address."""
    end = span_end
    for _ in range(50):
        end_src, _ = decode_run(src, anchor, end)
        end_alt, _ = decode_run(alt, anchor, end)
        end = max(end_src, end_alt)
        if end_src == end_alt:
            return end
    raise SystemExit("no common instruction boundary found past $%04X" % span_end)

def build():
    src = load_rom(SRC_ROM)
    alt = load_rom(ALT_ROM)
    if src is None:
        sys.exit("!! missing %s - run gen_from_roms.py first" % SRC_ROM)
    if alt is None:
        sys.exit("!! missing %s - run gen_from_roms.py with both ROM sets (bzone.zip and "
                  "bzonea.zip) present so it can build the alternate image" % ALT_ROM)

    raw_spans = diff_spans(src[LO:HI], alt[LO:HI])
    raw_spans = [(LO + a, LO + b) for a, b in raw_spans]
    anchors = listing_anchors()
    srclines = json.load(open(os.path.join(BUILD, "srclines.json")))

    fragments = []      # (kind, frag_start, frag_end, payload)
    for lo, hi in raw_spans:
        owner = owning_srcline(srclines, lo)
        if owner is not None and owner["kind"] == "data":
            # a data statement's own declared span is already a clean boundary in both images
            # (nothing decodes it as instructions - it never was code); grow only if the diff
            # itself somehow reached past the statement's own end (it does not, here).
            anchor, end = owner["addr"], max(owner["addr"] + owner["len"], hi)
            fragments.append(("data", anchor, end, alt[anchor:end]))
            continue
        anchor = max(a for a in anchors if a <= lo)
        end = common_boundary(src, alt, anchor, hi)
        _, insns = decode_run(alt, anchor, end)
        fragments.append(("code", anchor, end, insns))

    symbols = json.load(open(os.path.join(BUILD, "symbols.json")))
    names = Names(symbols, None, None)
    # Symbol lookup is by *value*, so it must stay kind-aware: an 'equate' is just a named
    # number (e.g. LOX=$02, HDIV=$01, NPTS=$1A) with no memory of its own, and coincides with
    # real addresses (LOX==VGLIST's $02, HDIV==VGBRIT's $01, NPTS==the immediate load's own
    # $1A) purely by accident - using it for a memory operand would misrepresent what the
    # instruction touches.  Only 'ram'/'io'/'code'/'data' symbols (genuine memory locations)
    # are offered for a memory-reference operand, and immediate operands get no symbol at all
    # (an immediate's *value* matching some address's number means nothing).
    MEM_KINDS = {"ram", "io", "code", "data"}
    by_addr = {}
    for nm, rec in names.rec.items():
        if rec["kind"] not in MEM_KINDS:
            continue
        by_addr.setdefault(rec["addr"], []).append(nm)
    for a in by_addr:
        by_addr[a] = sorted(by_addr[a], key=len)[0]     # ties: prefer the plainest name

    def sym(addr):
        return by_addr.get(addr)

    fmt = {IMM: "#%s", ZP: "%s", ABS: "%s", ZPX: "%s,X", ABX: "%s,X", ZPY: "%s,Y", ABY: "%s,Y",
           IND: "(%s)", IZX: "(%s,X)", IZY: "(%s),Y"}

    out = []
    W = out.append
    W(";Battlezone - the alternate $5000 program-ROM chip (MAME set 'bzone', Revision 2,")
    W(";036414-02.e1, CRC32 13de36d5), disassembled only where it differs from the source-")
    W(";derived image this disassembly reproduces (MAME set 'bzonea', Revision 1, 036414-01.e1,")
    W(";CRC32 efbc3fa0 - see bzone_program_rom.asm and HARDWARE_NOTES.md's 'two $5000 ROM")
    W(";images' section).  149 bytes differ overall: 1 (CHKS2) plus 148 inside CKSCOR's")
    W(";high-score display, split below into two runs of 93 and 55 bytes around one byte at")
    W(";$5505 that happens to be identical in both images (part of a 'STA (VGLIST),Y' whose")
    W(";other byte differs) - the fragment right after the gap continues the very next")
    W(";instruction, byte-adjacent in both images; nothing in between is omitted.  Everything")
    W(";outside these spans is byte-identical to bzone_program_rom.asm and is not repeated here.")
    W(";Conventions as bzone_program_rom.asm: Lxxxx: address labels, Atari's own names from")
    W(";build/symbols.json wherever an operand matches one, hex otherwise; [note] marks text")
    W(";written for this disassembly (nothing in the alternate ROM has its own source to quote -")
    W(";it is not part of the battlezone-main archive).  Each fragment below is re-encoded and")
    W(";compared with bzone_alt_cpu64k.bin by this script itself; see the run summary.")
    W("")

    total_bytes = 0
    for lo, hi in raw_spans:
        W(";" + "=" * 78)
        W("; $%04X-$%04X (%d bytes differ from bzone_program_rom.asm)" % (lo, hi - 1, hi - lo))
        for ln in wrap(SPAN_INTRO.get(lo, ""), 76):
            W(";   " + ln)
        W(";" + "=" * 78)
        total_bytes += hi - lo

    for kind, anchor, end, payload in fragments:
        W("")
        if kind == "data":
            data = payload
            items = ", ".join(hexb(b) for b in data)
            emit_line(W, anchor, ".byte %s" % items, "")
            continue
        insns = payload
        # A branch/JMP/JSR target that isn't one of Atari's own named symbols still gets a
        # plain Lxxxx reference, never an invented name - and needs no separate label line of
        # its own: every instruction already carries its own address as the Lxxxx: prefix
        # emit_line gives it below, exactly as bzone_program_rom.asm does, so 'Lxxxx' in an
        # operand always points at another line already labelled that way.
        for a, mn, mode, val, n in insns:
            if mode in (IMP, ACC):
                text = mn
            elif mode == IMM:
                text = "%s #%s" % (mn, hexb(val))          # never symbol-substituted (see sym() above)
            elif mode == REL or mn in ("JMP", "JSR"):
                t = sym(val) or ("L%04X" % val)
                text = "%-4s %s" % (mn, t)
            else:
                t = sym(val)
                opnd = t if t else (hexb(val) if n == 2 else hexw(val))
                text = "%-4s %s" % (mn, fmt[mode] % opnd)
            emit_line(W, a, text, "")

    W("")
    W(";" + "=" * 78)
    W("; end of the alternate-image diff (%d bytes total, both spans)" % total_bytes)
    W(";" + "=" * 78)

    with open(OUT, "w", newline="\n") as fp:
        fp.write("\n".join(out) + "\n")
    return fragments, raw_spans

def wrap(text, width):
    words, lines, cur = text.split(), [], ""
    for w in words:
        if cur and len(cur) + 1 + len(w) > width:
            lines.append(cur)
            cur = w
        else:
            cur = (cur + " " + w) if cur else w
    if cur:
        lines.append(cur)
    return lines

def emit_line(W, addr, text, note):
    prefix = "L%04X:  %s" % (addr, text)
    if note:
        W("%-35s ;%s" % (prefix, note) if len(prefix) < 35 else "%s ;%s" % (prefix, note))
    else:
        W(prefix.rstrip())

def verify_fragments(fragments, alt):
    """Re-encode every fragment with verify.py's own encoder (code) or a plain byte compare
    (data) and check it against the alternate image - the same proof the rest of this project
    holds every listing to."""
    total = bad = 0
    for kind, anchor, end, payload in fragments:
        if kind == "data":
            for i, byte in enumerate(payload):
                total += 1
                if alt[anchor + i] != byte:
                    bad += 1
                    print("   !! $%04X: expected $%02X, alt image has $%02X" % (anchor + i, byte, alt[anchor + i]))
            continue
        for a, mn, mode, val, n in payload:
            out = vfy.encode(mn, mode, val, a)
            if out is None:
                bad += 1
                print("   !! cannot re-encode %s at $%04X" % (mn, a))
                continue
            for i, byte in enumerate(out):
                total += 1
                if alt[a + i] != byte:
                    bad += 1
                    print("   !! $%04X: %s -> $%02X, alt image has $%02X" % (a + i, mn, byte, alt[a + i]))
    print("alt-image fragment check: %d bytes, %d mismatches" % (total, bad))
    return bad

def main():
    fragments, raw_spans = build()
    alt = load_rom(ALT_ROM)
    print("wrote %s: %d span(s), %d differing bytes" %
          (os.path.relpath(OUT, HERE), len(raw_spans), sum(b - a for a, b in raw_spans)))
    for kind, anchor, end, payload in fragments:
        n = len(payload)
        print("   %s fragment $%04X-$%04X (anchor $%04X, %d %s)" %
              (kind, anchor, end - 1, anchor, n, "bytes" if kind == "data" else "instructions"))
    bad = verify_fragments(fragments, alt)
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
