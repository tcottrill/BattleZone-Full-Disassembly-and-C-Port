"""Completeness proof for bzone_defines.asm's RAM section.

Independent of emit.py/emit_defines.py's own bookkeeping: it decodes every instruction of
the program ROM directly from the image (one span per build/srclines.json 'code' entry,
so it never mistakes an embedded data table for instructions), using disasm/m6502.py's own
opcode tables - the same ground truth verify.py uses.  For every decoded memory operand
below $0800 (zero page and indexed/absolute addressing alike - the *base* address for
ZPX/ZPY/ABX/ABY/IZX/IZY, since the index register's runtime value isn't known statically;
'forced absolute' operands are included too, since decoding is by opcode, not by the
listing's chosen syntax), it checks the address falls inside the extent of some RAM symbol
from build/symbols.json, extent = 1 byte unless emit_defines.blkb_sizes() gives it a larger
.BLKB count.  Goal: every such operand is covered - 0 left over.

Also reports, for every named RAM cell, which of its bytes are never a literal instruction
operand (only reachable at runtime through X/Y indexing from a named base) - real games
always have plenty of these (table-scanning loops); the interesting case is a byte covered
by one cell's extent that is only ever addressed through a *different* cell's name+offset
(TRDCTR+2 landing inside GARBAG, for instance) - both get pointed out.

Run: python check_ramrefs.py   (after emit.py/emit_defines.py; reads build/symbols.json,
build/srclines.json and the ROM image directly, not the generated .asm files)
Exit status 1 if any operand is left uncovered.
"""
import os, re, sys, json, bisect, collections
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import m6502
from m6502 import ZP, ZPX, ZPY, ABS, ABX, ABY, IND, IZX, IZY
from emit import Names, clean
from emit_defines import blkb_sizes

BUILD = os.path.join(HERE, "build")
ROM64K = os.path.join(HERE, "_survey", "roms_extracted", "bzone_cpu64k.bin")
LISTING = os.path.join(HERE, "bzone_program_rom.asm")
LO, HI = 0x5000, 0x8000
RAM_HI = 0x0800

MEM_MODES = (ZP, ZPX, ZPY, ABS, ABX, ABY, IND, IZX, IZY)

# $0100-$01FF is the 6502 hardware stack (no named cell lives there - see bzone_defines.asm's
# Game RAM banner and PWRON's own 'STA $0100,X' page-clear loop / SNDON's 'TSX; LDA $0103,X'
# stack-peek, both read directly off the ROM below); every other left-over address is
# individually investigated below rather than papered over with an invented name.
STACK_PAGE = range(0x0100, 0x0200)
EXPLAINED = {
    # CKSCOR's high-score shift-down loop (BZONE.MAC:836-852): 'LDA HSCTBL-3,X' / 'LDA
    # HSCTBL-2,X' use a base 3 bytes before HSCTBL only because X is indexed - the loop
    # guards X != SAVX with 'CPX SAVX; BEQ 30$' *before* running the body each time, and X
    # only ever takes multiples of 3 counting down to SAVX (itself a multiple of 3, 0-27),
    # so the smallest X for which the body runs is SAVX+3 >= 3.  The *effective* address
    # (HSCTBL-3+X) is therefore always >= HSCTBL-3+3 = HSCTBL itself: these two decoded
    # base addresses ($02FD/$02FE, the instructions' own compile-time operand before adding
    # X) are never actually read at runtime - they're an artifact of decoding the base of an
    # indexed instruction on its own, not a real memory reference into the unnamed gap
    # documented at bzone_defines.asm's 'Game RAM' banner ($02EF-$02FF).
    0x02FD: "HSCTBL-3,X base (CKSCOR shift loop): X is always >= 3 when this runs, so the effective address is always >= HSCTBL",
    0x02FE: "HSCTBL-2,X base (CKSCOR shift loop): same guarantee as HSCTBL-3,X above",
}

def load():
    rom = open(ROM64K, "rb").read()
    mem = {a: rom[a] for a in range(0x10000)}
    srclines = json.load(open(os.path.join(BUILD, "srclines.json")))
    symbols = json.load(open(os.path.join(BUILD, "symbols.json")))
    return rom, mem, srclines, symbols

def decode_operands(mem, srclines):
    """-> [(insn_addr, mnemonic, mode, val), ...] for every RAM-range (<$0800) memory
    operand in the program ROM, plus a list of any address m6502 couldn't decode.

    Anchors on the union of every code-kind srcline run's full byte range, not on each
    run's own reported start address: the LXL/LAH immediate-load macros (backpatched with
    .=.- and a .WORD - e.g. 'LXL XCROSS' at $50F5 then 'LAH XCROSS' at line 530) leave a
    transient stray byte whose *ownership* (for srclines.json's grouping) macasm.py assigns
    to the first event that touched it, one byte before where the second macro's own
    instruction actually starts in the final image - so LAH's reported run address ($50F8)
    is one byte past its real opcode ($50F7).  The two runs' byte *ranges* are still
    contiguous ($50F5-$50F7, $50F8-$50F9) even though their reported start addresses are
    not perfectly instruction-aligned, so decoding continuously through any contiguous
    stretch of code-owned bytes - resynchronizing on each instruction's own opcode-implied
    length rather than on run boundaries - finds the true instruction starts regardless."""
    codebytes = set()
    for e in srclines:
        if e["kind"] == "code" and LO <= e["addr"] < HI:
            codebytes.update(range(e["addr"], min(e["addr"] + e["len"], HI)))
    ops, bad = [], []
    a = LO
    while a < HI:
        if a not in codebytes:
            a += 1
            continue
        d = m6502.decode(mem, a)
        if d is None or a + d[3] > HI:
            bad.append(a)
            a += 1
            continue
        mn, mode, val, n = d
        if mode in MEM_MODES and val is not None and val < RAM_HI:
            ops.append((a, mn, mode, val))
        a += n
    return ops, bad

def named_extents(symbols):
    """-> sorted [(lo, hi, name, addr, rec), ...] for every RAM symbol (one entry per
    distinct (name, addr) - the identifier emit.py's Names class actually prints)."""
    names = Names(symbols, None, None)
    sizes = blkb_sizes(symbols)
    seen, out = set(), []
    for s in symbols:
        if s["kind"] != "ram":
            continue
        ident = names.ident.get((s["name"], s["addr"]))
        if ident is None or ident in seen:
            continue
        seen.add(ident)
        n = sizes.get((s["name"], s["addr"]), 1)
        out.append((s["addr"], s["addr"] + n, ident, s["addr"], s))
    out.sort()
    return out

def covering(extents, addr):
    """Index into extents of the one covering addr, or None.  extents must be sorted and
    (mostly) non-overlapping; a linear fallback handles the rare TRDCTR/GARBAG-style overlap."""
    los = [e[0] for e in extents]
    i = bisect.bisect_right(los, addr) - 1
    if i >= 0 and extents[i][0] <= addr < extents[i][1]:
        return i
    for j, e in enumerate(extents):
        if e[0] <= addr < e[1]:
            return j
    return None

LINE_RE = re.compile(r"^L([0-9A-F]{4}):\s+(\S+)\s+([^;]*)")
IDENT_RE = re.compile(r"^<?\[?([A-Za-z_][A-Za-z0-9_]*)")

def listing_operand_idents():
    """{insn_addr: leading identifier of its rendered operand}, from bzone_program_rom.asm -
    just for the base name Atari's own source expression starts with (TRDCTR, not GARBAG,
    for 'TRDCTR+2'); used only for the cross-name report, never for the coverage proof
    itself (that comes entirely from the ROM image + symbols.json)."""
    out = {}
    for ln in open(LISTING):
        m = LINE_RE.match(ln)
        if not m:
            continue
        a, mn, rest = int(m.group(1), 16), m.group(2), m.group(3).strip()
        if mn in (".byte", ".word"):
            continue
        opnd = rest.lstrip("#").strip("()").lstrip("<>")
        mi = IDENT_RE.match(opnd)
        if mi and not re.match(r"^L[0-9A-F]{4}$", mi.group(1)):
            out[a] = mi.group(1)
    return out

def main():
    rom, mem, srclines, symbols = load()
    ops, bad = decode_operands(mem, srclines)
    extents = named_extents(symbols)

    uncovered = []
    touched = collections.defaultdict(set)     # extent index -> set of addrs seen literally
    for a, mn, mode, val in ops:
        i = covering(extents, val)
        if i is None:
            uncovered.append((a, mn, mode, val))
        else:
            touched[i].add(val)

    print("program ROM $%04X-$%04X: %d code spans, %d memory operands below $%04X decoded"
          % (LO, HI - 1, sum(1 for e in srclines if e["kind"] == "code" and LO <= e["addr"] < HI),
             len(ops), RAM_HI))
    if bad:
        print("!! %d code address(es) marked as an instruction start did not decode:" % len(bad))
        for a in bad[:20]:
            print("   $%04X: byte $%02X is not a documented opcode" % (a, rom[a]))

    stack_hits = [o for o in uncovered if o[3] in STACK_PAGE]
    explained = [o for o in uncovered if o[3] in EXPLAINED]
    unexplained = [o for o in uncovered if o[3] not in STACK_PAGE and o[3] not in EXPLAINED]

    print("\n== uncovered operands (goal: 0 unexplained) ==")
    if uncovered:
        print("%-6s %-4s %-6s %-6s  %s" % ("insn", "mn", "mode", "operand", ""))
        for a, mn, mode, val in uncovered:
            tag = ("stack page $0100-$01FF, no named cell there by design" if val in STACK_PAGE
                   else EXPLAINED.get(val, "!! UNEXPLAINED"))
            print("$%04X  %-4s %-6s $%04X   %s" % (a, mn, m6502.MODE_NAME[mode], val, tag))
        print("  -> %d in the stack page, %d individually explained above, %d unexplained"
              % (len(stack_hits), len(explained), len(unexplained)))
    else:
        print("none - every RAM-range memory operand in the program ROM lies inside a named cell.")

    print("\n== named RAM cells only ever reached through X/Y indexing (not as a literal operand) ==")
    any_indexed = False
    for i, (lo, hi, name, base, rec) in enumerate(extents):
        hit = touched.get(i, set())
        span = set(range(lo, hi))
        missed = sorted(span - hit)
        if not missed:
            continue
        any_indexed = True
        # group the missed offsets into runs for a compact report
        runs = []
        for a in missed:
            if runs and runs[-1][1] == a - 1:
                runs[-1][1] = a
            else:
                runs.append([a, a])
        where = ", ".join(("$%04X" % r[0]) if r[0] == r[1] else ("$%04X-$%04X" % tuple(r)) for r in runs)
        print("  %-10s $%04X-$%04X (%d bytes): offsets %s never a literal operand - reached "
              "only by indexing from %s" % (name, lo, hi - 1, hi - lo, where, name))
    if not any_indexed:
        print("none - every byte of every named cell is also some instruction's literal operand.")

    print("\n== operand named for one cell but landing inside another cell's extent ==")
    idents = listing_operand_idents()
    cross = collections.OrderedDict()
    for a, mn, mode, val in ops:
        i = covering(extents, val)
        if i is None:
            continue
        lo, hi, name, base, rec = extents[i]
        src_ident = idents.get(a)
        if src_ident is None:
            continue
        src_clean = clean(src_ident)
        # the source expression's own base identifier may itself be a defines alias
        # (e.g. 'TRDCTR' rather than 'TRDCTR_something'); compare against the covering
        # cell's name directly, and also allow the trivial 'name == src_clean' base case
        if src_clean != name:
            key = (src_clean, name)
            cross.setdefault(key, []).append((a, val))
    if cross:
        for (src, covername), hits in cross.items():
            addrs = ", ".join("$%04X" % v for v in sorted(set(v for _, v in hits)))
            insns = ", ".join("$%04X" % a for a, _ in hits[:4]) + (", ..." if len(hits) > 4 else "")
            print("  %s reaches into %s's extent: %s (from instruction(s) %s)" % (src, covername, addrs, insns))
    else:
        print("none - every operand's own source identifier is the name of the cell it lands in.")

    ok = not unexplained and not bad
    print("\n%s" % ("check_ramrefs: OK (%d operands, all covered or individually explained)" % len(ops)
                     if ok else "!! check_ramrefs: FAILED"))
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
