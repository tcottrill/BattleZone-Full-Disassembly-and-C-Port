"""Vector-ROM ($3000-$3FFF) statement model for Battlezone, built on top of
Track A's build/srclines.json + build/symbols.json (macasm.py's byte-exact
re-assembly of BZMTNS.MAC, which .INCLUDEs VGMC/VGAN) rather than re-parsing
MAC65 ourselves (macasm.py/macsrc.py are owned by Track A and are not
modified here).

Each build/srclines.json record already carries: addr, len (bytes), the
decoded macro/mnemonic text (VCTR/GVCTR/JSRL/.../.NWORD/.MWORD/.BYTE/.WORD
with its literal operands, already macro-expanded and radix-normalised) and
Atari's own end-of-line comment, one record per source statement - exactly
the granularity the emitted listing wants. This module:

  * classifies each statement as an AVG instruction (decoded independently
    from the ROM bytes via avg.py - NOT trusted from the source text, so a
    macro alias like GVCTR still comes out as the real VCTR/SVEC the AVG
    chip executes) or raw data (.byte/.word groups taken from the ROM);
  * resolves JSRL/JMPL targets and table labels to Atari's names (mangled:
    '.' -> '_', '$' -> 'S_', local 'NN$' -> 'SCOPE_NN'), synthesising a
    positional V<addr> label where the target address has no Atari name;
  * groups the consecutive .NWORD/.MWORD statements under each OBJPNT
    target into a 3D point table, and the consecutive TLABS/TVCTR/BVCTR/
    TDOT/SBRITE/TSPCL/TSLIM statements under each OBJTBL target (that table
    itself lives in the *program* ROM - BZONE.MAC - not in $3000-$3FFF, but
    build/srclines.json covers the whole program so it is read the same way)
    into a per-object edge list. See vtables.py / vrender.py.

No 6502 code executes out of $3000-$3FFF: every byte there is either an AVG
instruction stream (read by the AVG chip) or a data table the 6502 reads as
plain bytes (PNTPUT/DRAW in BZONE.MAC); vromsrc confirms this by requiring
every statement's mnemonic to be an AVG op or .BYTE/.WORD/.NWORD/.MWORD.
"""
import json, os, re
from collections import defaultdict

import avg

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(HERE, "build")

VROM_LO, VROM_HI = 0x3000, 0x4000
FULL_LO, FULL_HI = 0x0000, 0x10000

AVG_HEADS = {"VCTR", "GVCTR", "SVEC", "STAT", "SCAL", "CNTR", "JSRL", "JMPL", "RTSL", "HALT"}
DATA_HEADS = {".BYTE", ".WORD", ".NWORD", ".MWORD"}


def load_json(name):
    return json.load(open(os.path.join(BUILD, name), encoding="utf-8"))


def mangle(name):
    """Atari name -> listing label: '.' -> '_', '$' -> 'S_', local 'NN$' -> 'SCOPE_NN'."""
    if name is None:
        return None
    m = re.match(r"^(\d+)\$$", name)
    if m:
        return "SCOPE_%s" % m.group(1)
    return name.replace(".", "_").replace("$", "S_")


class Model:
    """Loads symbols/srclines once and answers name/address questions for
    both the vector ROM statements and (for vtables/vrender only) the
    program-ROM object/edge-list tables they point at."""

    def __init__(self):
        self.mem = avg.load_image()
        self.symbols = load_json("symbols.json")
        self.srclines = load_json("srclines.json")
        self._index()

    def _index(self):
        names_by_addr = defaultdict(list)
        for e in self.symbols:
            a = e.get("addr")
            if a is None:
                continue
            names_by_addr[a].append(e["name"])
        # Two Atari names can share one address: an 'end of the previous
        # table' bare marker label (e.g. ETKTBL) immediately followed by the
        # next table's own label (TREAD4) on the very next source line, or a
        # later 'CHAR.0 = CHAR.O' digit/letter alias. macasm.py (Track A)
        # already picked, for each *statement*, the one label it considers
        # that statement's own - trust that choice as primary; any other
        # name(s) sharing the address are secondary (still listed, for
        # cross-reference, but never used to name a table/target).
        primary = {}
        for r in self.srclines:
            if r.get("label"):
                primary[r["addr"]] = r["label"]
        for a in names_by_addr:
            p = primary.get(a)
            names_by_addr[a].sort(key=lambda n: (
                0 if n == p else (1 if self._kind_of(a, n) != "equate" else 2)))
        self.names_by_addr = dict(names_by_addr)
        self.name_to_addr = {}
        for a, ns in self.names_by_addr.items():
            for n in ns:
                self.name_to_addr.setdefault(n, a)

        by_addr = {}
        for r in self.srclines:
            by_addr.setdefault(r["addr"], r)
        self.stmt_by_addr = by_addr
        self.stmts_sorted = sorted(self.srclines, key=lambda r: r["addr"])

        self.vrom_stmts = [r for r in self.stmts_sorted if VROM_LO <= r["addr"] < VROM_HI]

    def _kind_of(self, addr, name):
        for e in self.symbols:
            if e.get("addr") == addr and e["name"] == name:
                return e.get("kind")
        return None

    # ---------------------------------------------------------------- names
    def names_at(self, addr):
        return self.names_by_addr.get(addr, [])

    def label(self, addr):
        """Primary Atari name at addr (mangled), or None."""
        ns = self.names_at(addr)
        return mangle(ns[0]) if ns else None

    def target_name(self, addr):
        """Label used when addr is the target of a JSRL/JMPL/.WORD pointer:
        Atari's name if one exists there, else a positional V<addr> label
        (every statement gets one of those on its own listing line anyway)."""
        return self.label(addr) or ("V%04X" % addr)

    def addr_of(self, name):
        return self.name_to_addr.get(name)

    # ------------------------------------------------------------ walking
    def stmt_at(self, addr):
        return self.stmt_by_addr.get(addr)

    def next_stmt_addr(self, addr):
        """Address of the statement immediately following addr, scanning
        the full statement list (used to walk data tables such as OBJTBL's
        per-object edge lists, which live in the program ROM)."""
        st = self.stmt_by_addr.get(addr)
        if st is None:
            return None
        return addr + st["len"]

    def walk_from(self, addr, stop_labels=None):
        """Yield srclines records starting at addr until (but not including)
        the next statement that has its own Atari label (table boundary),
        or forever if stop_labels is given and hit."""
        a = addr
        first = True
        while a in self.stmt_by_addr:
            st = self.stmt_by_addr[a]
            if not first and st.get("label"):
                return
            first = False
            yield st
            a += st["len"]


def head_of(mnemonic_text):
    return (mnemonic_text or "").split(None, 1)[0].upper()


def classify(mnemonic_text):
    h = head_of(mnemonic_text)
    if h in AVG_HEADS:
        return "avg"
    if h in DATA_HEADS:
        return "data"
    return "unknown"


def args_of(mnemonic_text):
    rest = mnemonic_text.split(None, 1)[1] if " " in mnemonic_text or "\t" in mnemonic_text else ""
    return [a.strip() for a in rest.split(",")] if rest.strip() else []


class VStmt:
    """One emitted listing line: either an AVG op (decoded independently
    from the ROM, not trusted from source text) or a raw .byte/.word group
    taken verbatim from the ROM at (addr, len)."""
    __slots__ = ("addr", "len", "kind", "mnemonic", "operand", "words", "src",
                 "src_head", "comment", "file", "line", "label")

    def __repr__(self):
        return "<%04X %s %s %s>" % (self.addr, self.kind, self.mnemonic, self.operand)


def build_vstmt(M, rec):
    addr, ln = rec["addr"], rec["len"]
    v = VStmt()
    v.addr, v.len = addr, ln
    v.src, v.comment = rec["mnemonic_text"], rec.get("comment") or ""
    v.file, v.line, v.label = rec["file"], rec["line"], rec.get("label")
    v.src_head = head_of(v.src)
    kind = classify(v.src)
    if kind == "avg":
        mn, oper, declen = avg.decode(M.mem, addr)
        assert declen == ln, "%s:%d $%04X length mismatch: src %s(%d) ROM decodes %s(%d)" % (
            v.file, v.line, addr, v.src_head, ln, mn, declen)
        v.kind = "avg"
        v.mnemonic, v.operand = mn, oper
        if mn in ("JSRL", "JMPL"):
            target = avg.decode_raw(M.mem, addr)["target"]
            v.operand = M.target_name(target)
        v.words = [avg.word(M.mem, addr + 2 * k) for k in range(ln // 2)]
    else:
        v.kind = "data"
        if v.src_head == ".BYTE":
            v.mnemonic = ".byte"
            v.words = list(M.mem[addr:addr + ln])
        else:
            v.mnemonic = ".word"
            v.words = [avg.word(M.mem, addr + 2 * k) for k in range(ln // 2)]
        v.operand = None
    return v


def all_vstmts(M=None):
    M = M or Model()
    return M, [build_vstmt(M, r) for r in M.vrom_stmts]


if __name__ == "__main__":
    M, vs = all_vstmts()
    kinds = defaultdict(int)
    for v in vs:
        kinds[v.kind] += 1
    covered = sum(v.len for v in vs)
    print("vector ROM $3000-$3FFF: %d statements (%s), %d/%d bytes covered"
          % (len(vs), dict(kinds), covered, VROM_HI - VROM_LO))
    lo, hi = VROM_HI, VROM_LO
    gaps = []
    pc = VROM_LO
    for v in vs:
        if v.addr != pc:
            gaps.append((pc, v.addr))
        pc = v.addr + v.len
    if pc != VROM_HI:
        gaps.append((pc, VROM_HI))
    print("gaps:", [("$%04X" % a, "$%04X" % b) for a, b in gaps])
