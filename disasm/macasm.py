"""MAC65-subset assembler for the original Atari Battlezone source (adapted from the Tempest project).

Assembles the linked module set
    ALWELG ALSCO2 ALDIS2 ALEXEC ALSOUN ALVROM ALCOIN ALLANG ALHAR2 ALTES2 ALEARO ALVGUT
(TEMPST.DOC "Version 2" link line) with the common includes (ALCOMN, HLL65, VGMC,
ANVGAN, ASCVG, COIN65), links the .CSECTs by concatenation from $A8B0 (LINKM order),
and compares the result with the rev-3 64K CPU image.

Implemented: hex/decimal/radix switches, MACRO-11 macros (.MACRO with nested
definitions, formal substitution with ' concatenation, \\value arguments, .NARG,
.NCHR, .MEXIT), .REPT/.IRPC (closed by .ENDR or .ENDM), .IF/.IFF/.IFT/.IFTF/.ENDC,
.IIF, local N$ symbols, 6-character symbol significance, location-counter
back-patching (.=), .ASECT/.CSECT, .VCTRS, .ENABL M68 (big-endian .WORD).
Operand sizing follows two-pass rules: zero page only when every symbol in the
operand was defined (and resolved) earlier in the module; forward references
and externals are absolute.

Outputs (disasm/build): symbols.json, srclines.json, macasm_report.md.
Run:  python macasm.py
"""
import os, sys, re, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import m6502
from m6502 import IMP, ACC, IMM, ZP, ZPX, ZPY, ABS, ABX, ABY, IND, IZX, IZY, REL
import macsrc
from macsrc import split_comment, parse_stmt, split_args, substitute, read_source
from rtexpr import Evaluator, ExprError, s16

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(HERE, "build")
ROM64K = os.path.join(HERE, "_survey", "roms_extracted", "bzone_cpu64k.bin")
ROM64K_REV1 = os.path.join(HERE, "_survey", "roms_extracted", "bzone_alt_cpu64k.bin")

# BZONE.COM / BZONE.MAP:  LINKM BZMTNS,BZONE,BZSOUN,VGUT,BZSTST
MODULES = ["BZMTNS", "BZONE", "BZSOUN", "VGUT", "BZSTST"]
MAP_BASES = {"BZSOUN": 0x7864, "VGUT": 0x7A19, "BZSTST": 0x7ACC}     # .CSECT bases (BZONE.MAP)
MAP_BBASES = {"BZONE": 0x0000, "BZSOUN": 0x00EE}                     # .BSECT (base page) bases
CSECT_START = 0x7864              # end of the absolute section = first relocatable byte
BSECT_START = 0x0000
COMMON_FILES = {"VGMC", "VGAN", "ASCVG", "COIN65", "TC65", "COND65", "MBDIAG"}

OPENERS = {".MACRO", ".REPT", ".IRPC", ".IRP"}
CLOSERS = {".ENDM", ".ENDR"}
PREFIX_RE = re.compile(r"^(NY|NX|ZX|ZY|AX|AY|I|X|Y|Z|A|N)\s*,(.*)$")
LOCAL_RE = re.compile(r"^\d+\$$")

def sym6(n):
    return n.upper()[:6]

class AsmError(Exception):
    pass

class Reader:
    def __init__(self, lines, kind, origin=None, fname=None, narg=0, cond_depth=0):
        self.lines, self.kind, self.pos = lines, kind, 0
        self.origin, self.fname, self.narg, self.cond_depth = origin, fname, narg, cond_depth

class Module:
    """Two-pass assembly of one linked module."""
    def __init__(self, name, base, ext, bbase=0):
        self.name, self.base, self.ext, self.bbase = name, base, ext, bbase

    # ------------------------------------------------------------ passes
    def assemble(self):
        self.p1 = {}
        self.p1loc = {}
        self.run(1)
        self.p1, self.p1loc = dict(self.cur), dict(self.locals)
        self.p1labels = dict(self.labelvals)
        self.run(2)
        self.phase = [(k, self.p1labels[k], v) for k, v in self.labelvals.items()
                      if k in self.p1labels and self.p1labels[k] != v]
        return self

    def run(self, pas):
        self.pas = pas
        self.cur, self.resolved, self.locals = {}, set(), {}
        self.labelvals = {}
        self.globl = set()
        self.macros = {}
        self.radix = 8
        self.sect = "REL"           # MACRO-11 default: the unnamed relocatable .CSECT (VGUT)
        self.loc = {"ABS": 0, "REL": self.base, "BSE": self.bbase}
        self.relmax = self.base
        self.bsemax = self.bbase
        self.globb = set()
        self.absmax = 0
        self.block = 0
        self.m68 = False
        self.ama = False
        self.conds = []           # list of [parent_active, cond, active]
        self.sbttl = ""
        self.errors = []
        self.undefined = {}
        self.mem, self.owner, self.events = {}, {}, []
        self.defs = []
        self.pending = []
        self.block_comments = {}
        self.label_blocks = {}
        self.origin = (self.name, 0)
        self.comment = ""
        self.in_exp = False
        self.readers = [Reader(read_source(self.name), "file", fname=self.name)]
        while self.readers:
            r = self.readers[-1]
            if r.pos >= len(r.lines):
                self.pop_reader()
                continue
            text = r.lines[r.pos]
            r.pos += 1
            if r.kind == "file":
                self.origin = (r.fname, r.pos)
                self.in_exp = False
                code, comment = split_comment(text)
                self.comment = comment.strip()
            else:
                code, comment = text, ""
                self.in_exp = True
            try:
                self.statement(code, r, comment_only=(r.kind == "file" and not code.strip()))
            except (AsmError, ExprError, ValueError, KeyError, IndexError) as e:
                self.error("%s: %s" % (type(e).__name__, e), code)

    def pop_reader(self):
        r = self.readers.pop()
        if r.kind != "file" and len(self.conds) > r.cond_depth:
            del self.conds[r.cond_depth:]
        if r.kind == "file" and getattr(r, "saved_sbttl", None) is not None:
            self.sbttl = r.saved_sbttl      # .SBTTL of an include does not leak out

    def error(self, msg, code=""):
        if self.pas == 2:
            self.errors.append("%s:%d: %s  [%s]" % (self.origin[0], self.origin[1], msg, code.strip()))

    def active(self):
        return not self.conds or self.conds[-1][2]

    # ------------------------------------------------------------ symbols
    def resolve(self, name):
        name = name.upper()
        if LOCAL_RE.match(name):
            key = (self.block, name)
            if key in self.locals:
                return self.locals[key], True
            if key in self.p1loc:
                return self.p1loc[key], False
            raise KeyError(name)
        k = sym6(name)
        self.refs.append(k)
        if k in self.cur:
            return self.cur[k], k in self.resolved
        if k in self.p1:
            return self.p1[k], False
        if k in self.ext:
            # .GLOBB declares a base-page external; without .ENABL AMA MAC65 also sizes a
            # plain external by its linked value (VGUT: INC VGLIST+1 is E6 03 in the ROM)
            return self.ext[k], k in self.globb or not self.ama
        raise KeyError(name)

    def ev(self, text, radix=None):
        self.refs = []
        e = Evaluator(self.resolve, self.radix if radix is None else radix, self.loc[self.sect])
        v = e.eval(text)
        if e.undefined and self.pas == 2:
            for u in e.undefined:
                self.undefined.setdefault(u, "%s:%d" % self.origin)
        return v, e.known

    def define(self, name, value, known, kind, glob=False, src_name=None, expr=None):
        k = sym6(name)
        self.cur[k] = value & 0xFFFF
        if known:
            self.resolved.add(k)
        else:
            self.resolved.discard(k)
        if glob:
            self.globl.add(k)
        if self.pas == 2:
            self.defs.append(dict(name=k, src_name=(src_name or name).upper(), value=value & 0xFFFF,
                                  how=kind, file=self.origin[0], line=self.origin[1],
                                  sbttl=self.sbttl, comment=self.comment, in_exp=self.in_exp,
                                  sect=self.sect, glob=glob, refs=list(getattr(self, "refs", []))
                                  if kind == "assign" else [],
                                  block=list(self.pending) if not self.in_exp else []))

    def define_label(self, name, glob):
        v = self.loc[self.sect]
        if LOCAL_RE.match(name):
            self.locals[(self.block, name)] = v
            if self.pas == 2:
                self.defs.append(dict(name=name, src_name=name, value=v, how="local", file=self.origin[0],
                                      line=self.origin[1], sbttl=self.sbttl, comment=self.comment,
                                      in_exp=self.in_exp, sect=self.sect, glob=False, refs=[],
                                      block=[], scope=self.scope_label))
            return
        self.block += 1
        self.scope_label = sym6(name)
        self.labelvals[sym6(name)] = v
        self.refs = []
        self.define(name, v, True, "label", glob)

    # ------------------------------------------------------------ emission
    def emit(self, data, kind, text="", addr=None):
        a = self.loc[self.sect] if addr is None else addr
        if self.pas == 2:
            ev = len(self.events)
            self.events.append(dict(addr=a, len=len(data), origin=self.origin, kind=kind,
                                    text=text.strip(), exp=self.in_exp))
            for i, b in enumerate(data):
                ad = (a + i) & 0xFFFF
                self.mem[ad] = b & 0xFF
                if ad not in self.owner:
                    self.owner[ad] = ev
            if self.pending and not self.in_exp or (self.pending and self.in_exp):
                if a not in self.block_comments:
                    self.block_comments[a] = list(self.pending)
                self.pending = []
        if addr is None:
            self.advance(len(data))

    def advance(self, n):
        self.loc[self.sect] = (self.loc[self.sect] + n) & 0xFFFF
        if self.sect == "REL":
            self.relmax = max(self.relmax, self.loc["REL"])
        elif self.sect == "BSE":
            self.bsemax = max(self.bsemax, self.loc["BSE"])
        else:
            self.absmax = max(self.absmax, self.loc["ABS"])

    # ------------------------------------------------------------ statements
    def statement(self, code, r, comment_only=False):
        if comment_only:
            if self.comment and self.active():
                self.pending.append(self.comment)
            return
        st = parse_stmt(code)
        if not self.active():
            if st.kind == "op":
                if st.op == ".IF":
                    self.conds.append([False, False, False])
                elif st.op in (".IFF", ".IFT", ".IFTF"):
                    self.cond_switch(st.op)
                elif st.op == ".ENDC":
                    if self.conds:
                        self.conds.pop()
            return
        for name, glob in st.labels:
            self.define_label(name, glob)
        if st.kind == "assign":
            self.refs = []
            if st.name == ".":
                v, _ = self.ev(st.operand)
                self.loc[self.sect] = v
                if self.sect == "REL":
                    self.relmax = max(self.relmax, v)
                elif self.sect == "BSE":
                    self.bsemax = max(self.bsemax, v)
                else:
                    self.absmax = max(self.absmax, v)
                return
            v, known = self.ev(st.operand)
            self.define(st.name, v, known, "assign", st.glob)
            if not self.in_exp:
                self.pending = []
            return
        if st.kind != "op":
            return
        op = st.op
        if sym6(op) in self.macros and not op.startswith(".END") or op in self.macros:
            self.expand_macro(self.macros.get(op) or self.macros[sym6(op)], st.operand, r)
            return
        if op in m6502.TAB:
            self.instruction(op, st.operand)
            return
        h = DIRECTIVES.get(op)
        if h is None:
            if op == "." or op[:1].isdigit() or sym6(op) in self.cur:
                # MACRO-11: a bare expression statement is an implicit .WORD
                d_word(self, code.strip(), r, code)
                return
            raise AsmError("unknown op %s" % op)
        h(self, st.operand, r, code)

    def cond_switch(self, op):
        if not self.conds:
            return
        c = self.conds[-1]
        if op == ".IFF":
            c[2] = c[0] and not c[1]
        elif op == ".IFT":
            c[2] = c[0] and c[1]
        else:
            c[2] = c[0]

    def test_cond(self, cond, arg):
        cond = cond.upper()
        if cond in ("B", "NB"):
            a = arg.strip()
            if a.startswith("<") and a.endswith(">"):
                a = a[1:-1].strip()
            blank = (a == "")
            return blank if cond == "B" else not blank
        if cond in ("DF", "NDF"):
            names = re.split(r"[&!]", arg)
            d = all(self.is_defined(n.strip()) for n in names if n.strip())
            return d if cond == "DF" else not d
        v, _ = self.ev(arg)
        v = s16(v)
        return {"EQ": v == 0, "NE": v != 0, "GT": v > 0, "LT": v < 0, "GE": v >= 0,
                "LE": v <= 0, "Z": v == 0, "NZ": v != 0, "G": v > 0, "L": v < 0}[cond]

    def is_defined(self, n):
        n = n.upper()
        if LOCAL_RE.match(n):
            return (self.block, n) in self.locals
        k = sym6(n)
        # pass 2 also sees symbols defined later in pass 1 (ALSOUN OFFSET macro)
        return k in self.cur or k in self.p1 or k in self.ext

    @staticmethod
    def split_cond(operand):
        """'EQ,expr' / 'EQ expr' -> (cond, rest)."""
        m = re.match(r"^\s*([A-Za-z]+)\s*[, \t]\s*(.*)$", operand)
        if not m:
            return operand.strip(), ""
        return m.group(1), m.group(2)

    @staticmethod
    def split_expr(s):
        """First argument of .IIF: ends at a top-level comma or whitespace."""
        depth, i, n = 0, 0, len(s)
        while i < n:
            c = s[i]
            if c == "'" and i + 1 < n:
                i += 2
                continue
            if c == "<":
                depth += 1
            elif c == ">":
                depth -= 1
            elif depth == 0 and (c == "," or c.isspace()):
                rest = s[i + 1:]
                if c.isspace():
                    rest = rest.lstrip()
                    if rest.startswith(","):
                        rest = rest[1:]
                return s[:i], rest
            i += 1
        return s, ""

    # ------------------------------------------------------------ macros
    def collect_body(self, r):
        body, depth = [], 1
        while r.pos < len(r.lines):
            text = r.lines[r.pos]
            r.pos += 1
            code = split_comment(text)[0] if r.kind == "file" else text
            st = parse_stmt(code)
            if st.kind == "op":
                if st.op in OPENERS:
                    depth += 1
                elif st.op in CLOSERS:
                    depth -= 1
                    if depth == 0:
                        return body
            body.append(code.rstrip())
        raise AsmError("unterminated macro/repeat body")

    def fmt(self, v):
        if self.radix == 10:
            return str(v)
        if self.radix == 16:
            return "%X" % v
        if self.radix == 8:
            return "%o" % v
        return str(v)

    def expand_macro(self, mac, operand, r):
        formals, body = mac
        args = split_args(operand) if operand.strip() else []
        table = {}
        for i, f in enumerate(formals):
            a = args[i] if i < len(args) else ""
            if a.startswith("\\"):
                v, _ = self.ev(a[1:])
                a = self.fmt(v)
            table[f] = a
        lines = [substitute(l, table) for l in body]
        self.readers.append(Reader(lines, "macro", narg=len(args), cond_depth=len(self.conds)))

    # ------------------------------------------------------------ 6502
    def instruction(self, mn, operand):
        modes = m6502.TAB[mn]
        o = operand.strip()
        if mn in m6502.BRANCHES:
            v, _ = self.ev(o)
            pc = self.loc[self.sect]
            d = v - (pc + 2)
            if self.pas == 2 and not -128 <= s16(d & 0xFFFF) <= 127:
                self.error("branch out of range to $%04X" % v, mn + " " + o)
            self.emit([modes[REL], d & 0xFF], "insn", "%s %s" % (mn, o))
            return
        if not o:
            mode = ACC if ACC in modes else IMP
            self.emit([modes[mode]], "insn", mn)
            return
        m = PREFIX_RE.match(o.upper())
        pfx, expr = (m.group(1), o[m.start(2):]) if m else ("", o)
        if not m and o.startswith("#"):
            pfx, expr = "I", o[1:]
        if not m and o.upper() == "A" and ACC in modes:
            self.emit([modes[ACC]], "insn", mn + " A")
            return
        v, known = self.ev(expr)
        zp_ok = known and v < 0x100
        if pfx == "I":
            mode = IMM
        elif pfx == "":
            mode = ZP if zp_ok and ZP in modes else ABS
        elif pfx == "X":
            mode = ZPX if zp_ok and ZPX in modes else ABX
        elif pfx == "Y":
            mode = ZPY if zp_ok and ZPY in modes else ABY
        else:
            mode = {"NY": IZY, "NX": IZX, "Z": ZP, "ZX": ZPX, "ZY": ZPY, "A": ABS,
                    "AX": ABX, "AY": ABY, "N": IND}[pfx]
        if mode not in modes:
            alt = {ZP: ABS, ZPX: ABX, ZPY: ABY}.get(mode)
            if alt in modes:
                mode = alt
            else:
                raise AsmError("mode %s not valid for %s" % (m6502.MODE_NAME[mode], mn))
        op = modes[mode]
        if m6502.SIZE[mode] == 2:
            data = [op, v & 0xFF]
        else:
            data = [op, v & 0xFF, (v >> 8) & 0xFF]
        self.emit(data, "insn", "%s %s" % (mn, o))

# ---------------------------------------------------------------- directives
def d_ignore(a, operand, r, code):
    pass

def d_sbttl(a, operand, r, code):
    m = re.match(r"^\s*(?:[A-Za-z0-9$._]+::?\s*)*\.SBTTL\s?(.*)$", code, re.I)
    a.sbttl = (m.group(1) if m else operand).strip()

def d_radix(a, operand, r, code):
    v, _ = a.ev(operand, radix=10)
    a.radix = v

def d_enabl(a, operand, r, code):
    if "M68" in operand.upper():
        a.m68 = True
    if "AMA" in operand.upper():
        a.ama = True

def d_dsabl(a, operand, r, code):
    if "M68" in operand.upper():
        a.m68 = False

def d_include(a, operand, r, code):
    name = operand.strip().split()[0].upper()
    try:
        lines = read_source(name)
    except FileNotFoundError:
        a.error("include file %s not found" % name, code)
        return
    rd = Reader(lines, "file", fname=name)
    rd.saved_sbttl = a.sbttl
    a.readers.append(rd)

def d_asect(a, operand, r, code):
    a.sect = "ABS"

def d_csect(a, operand, r, code):
    a.sect = "REL"
    a.block += 1

def d_bsect(a, operand, r, code):
    a.sect = "BSE"

def d_globb(a, operand, r, code):
    for x in split_args(operand):
        if x.strip():
            a.globl.add(sym6(x.strip()))
            a.globb.add(sym6(x.strip()))

def d_blkb(a, operand, r, code):
    v, _ = a.ev(operand)
    a.advance(v)

def d_byte(a, operand, r, code):
    args = split_args(operand)
    data = []
    for x in args:
        v, _ = a.ev(x) if x.strip() else (0, True)
        data.append(v & 0xFF)
    a.emit(data, "byte", ".BYTE " + operand)

def d_word(a, operand, r, code):
    data = []
    for x in split_args(operand):
        v, _ = a.ev(x) if x.strip() else (0, True)
        data += [(v >> 8) & 0xFF, v & 0xFF] if a.m68 else [v & 0xFF, (v >> 8) & 0xFF]
    a.emit(data, "word", ".WORD " + operand)

def d_vctrs(a, operand, r, code):
    args = split_args(operand)
    addr, _ = a.ev(args[0])
    data = []
    for x in args[1:]:
        v, _ = a.ev(x)
        data += [v & 0xFF, (v >> 8) & 0xFF]
    a.emit(data, "word", ".VCTRS " + operand, addr=addr)

def d_globl(a, operand, r, code):
    for x in split_args(operand):
        if x.strip():
            a.globl.add(sym6(x.strip()))

def d_macro(a, operand, r, code):
    parts = [p for p in re.split(r"[,\s]+", operand.strip()) if p]
    name = parts[0].upper()
    formals = [p.upper() for p in parts[1:]]
    body = a.collect_body(r)
    a.macros[name] = (formals, body)
    if len(name) > 6:
        a.macros[sym6(name)] = (formals, body)

def d_rept(a, operand, r, code):
    body = a.collect_body(r)
    n, _ = a.ev(operand)
    n = s16(n)
    if n > 0:
        a.readers.append(Reader(body * n, "rept", cond_depth=len(a.conds), narg=a.cur_narg()))

def d_irpc(a, operand, r, code):
    body = a.collect_body(r)
    m = re.match(r"^\s*([A-Za-z0-9$._]+)\s*,\s?(.*)$", operand)
    sym, s = m.group(1).upper(), m.group(2)
    s = s.rstrip() if not s.rstrip().endswith(">") else s.rstrip()
    if s.startswith("<"):
        s = s[1:macsrc.matching_close(s, 0)]
    lines = []
    for ch in s:
        lines += [substitute(l, {sym: ch}) for l in body]
    if lines:
        a.readers.append(Reader(lines, "rept", cond_depth=len(a.conds), narg=a.cur_narg()))

def d_irp(a, operand, r, code):
    body = a.collect_body(r)
    m = re.match(r"^\s*([A-Za-z0-9$._]+)\s*,\s?(.*)$", operand)
    sym, s = m.group(1).upper(), m.group(2).strip()
    if s.startswith("<"):
        s = s[1:macsrc.matching_close(s, 0)]
    lines = []
    for item in [x for x in re.split(r"[,\s]+", s.strip()) if x]:
        lines += [substitute(l, {sym: item}) for l in body]
    if lines:
        a.readers.append(Reader(lines, "rept", cond_depth=len(a.conds), narg=a.cur_narg()))

def d_ascii(a, operand, r, code):
    o = operand.strip()
    q = o[0]
    text = o[1:o.index(q, 1)]
    a.emit([ord(c) for c in text], "byte", ".ASCII " + operand)

def d_if(a, operand, r, code):
    cond, rest = Module.split_cond(operand)
    parent = a.active()
    val = a.test_cond(cond, rest)
    a.conds.append([parent, val, parent and val])

def d_iff(a, operand, r, code):
    a.cond_switch(".IFF")

def d_ift(a, operand, r, code):
    a.cond_switch(".IFT")

def d_iftf(a, operand, r, code):
    a.cond_switch(".IFTF")

def d_endc(a, operand, r, code):
    if a.conds:
        a.conds.pop()

def d_iif(a, operand, r, code):
    cond, rest = Module.split_cond(operand)
    arg, stmt = Module.split_expr(rest)
    if a.test_cond(cond, arg):
        a.statement(stmt, r)

def cur_narg(self):
    for rd in reversed(self.readers):
        if rd.kind == "macro":
            return rd.narg
    return 0
Module.cur_narg = cur_narg

def d_narg(a, operand, r, code):
    a.refs = []
    a.define(operand.strip(), a.cur_narg(), True, "assign")

def d_nchr(a, operand, r, code):
    m = re.match(r"^\s*([A-Za-z0-9$._]+)\s*,\s?(.*)$", operand)
    s = m.group(2)
    if s.startswith("<"):
        s = s[1:macsrc.matching_close(s, 0)]
    a.refs = []
    a.define(m.group(1), len(s), True, "assign")

def d_mexit(a, operand, r, code):
    while a.readers:
        rd = a.readers[-1]
        if rd.kind == "file":
            return
        a.pop_reader()
        if rd.kind in ("macro", "rept"):
            return

def d_end(a, operand, r, code):
    a.readers.clear()

def d_error(a, operand, r, code):
    a.error(".ERROR " + operand, code)

DIRECTIVES = {
    ".TITLE": d_ignore, ".SBTTL": d_sbttl, ".PAGE": d_ignore, ".RADIX": d_radix,
    ".ENABL": d_enabl, ".DSABL": d_dsabl, ".NLIST": d_ignore, ".LIST": d_ignore,
    ".INCLUDE": d_include, ".ASECT": d_asect, ".CSECT": d_csect, ".BSECT": d_bsect, ".GLOBB": d_globb, ".ENABLE": d_enabl, ".BLKB": d_blkb,
    ".BYTE": d_byte, ".WORD": d_word, ".VCTRS": d_vctrs, ".GLOBL": d_globl,
    ".MACRO": d_macro, ".ENDM": d_ignore, ".ENDR": d_ignore, ".REPT": d_rept,
    ".IRPC": d_irpc, ".IRP": d_irp, ".ASCII": d_ascii, ".IF": d_if, ".IFF": d_iff, ".IFT": d_ift, ".IFTF": d_iftf,
    ".ENDC": d_endc, ".IIF": d_iif, ".NARG": d_narg, ".NCHR": d_nchr,
    ".MEXIT": d_mexit, ".END": d_end, ".ERROR": d_error, ".PRINT": d_ignore,
}

# ---------------------------------------------------------------- link
def link(verbose=True):
    ext, bases_prev = {}, None
    for rnd in range(1, 6):
        mods, base, bbase, glob = [], CSECT_START, BSECT_START, {}
        bases = {}
        for name in MODULES:
            m = Module(name, base, ext, bbase).assemble()
            bases[name] = base
            m.bbase_used = bbase if m.bsemax != bbase else None
            bbase = m.bsemax
            if m.relmax != base:
                base = m.relmax
            for k in m.globl:
                if k in m.cur:
                    glob[k] = m.cur[k]
            for d in m.defs:
                if d["glob"] and d["name"] in m.cur:
                    glob[d["name"]] = m.cur[d["name"]]
            mods.append(m)
        if verbose:
            print("round %d: %d globals, high %04X" % (rnd, len(glob), base))
        if glob == ext and bases == bases_prev:
            return mods, glob, bases
        ext, bases_prev = glob, bases
    return mods, glob, bases

# ---------------------------------------------------------------- outputs
ROM_RANGES = [(0x3000, 0x4000), (0x5000, 0x8000)]

def in_rom(v):
    return any(lo <= v < hi for lo, hi in ROM_RANGES)

def is_io(v):
    return 0x0800 <= v <= 0x1FFF

def hole_spans(holes):
    sp = []
    for a in holes:
        if sp and sp[-1][1] == a - 1:
            sp[-1][1] = a
        else:
            sp.append([a, a])
    return ", ".join("$%04X-$%04X" % tuple(x) for x in sp)

def main():
    os.makedirs(BUILD, exist_ok=True)
    mods, glob, bases = link()
    rom = open(ROM64K_REV1 if "--alt" in sys.argv else ROM64K, "rb").read()
    lines_cache = {}
    def src_line(f, n):
        if f not in lines_cache:
            lines_cache[f] = read_source(f)
        L = lines_cache[f]
        return L[n - 1] if 0 < n <= len(L) else ""

    # ---- memory image, per-address owner
    mem, owner = {}, {}
    all_events = []
    for m in mods:
        off = len(all_events)
        for e in m.events:
            e["module"] = m.name
            all_events.append(e)
        for a, b in m.mem.items():
            mem[a] = b
        for a, ev in m.owner.items():
            if a not in owner:
                owner[a] = off + ev

    # ---- byte comparison
    per_mod = {}
    mism = []
    for a in sorted(mem):
        mod = all_events[owner[a]]["module"]
        s = per_mod.setdefault(mod, [0, 0])
        if rom[a] == mem[a]:
            s[0] += 1
        else:
            s[1] += 1
            mism.append(a)
    spans = []
    for a in mism:
        if spans and spans[-1][1] == a - 1:
            spans[-1][1] = a
        else:
            spans.append([a, a])
    prog = [a for a in range(0x5000, 0x8000)]
    prog_emit = [a for a in prog if a in mem]
    prog_match = sum(1 for a in prog_emit if mem[a] == rom[a])
    holes = [a for a in prog if a not in mem]
    vrom = [a for a in range(0x3000, 0x4000) if a in mem]
    vrom_match = sum(1 for a in vrom if mem[a] == rom[a])
    lda = None

    # ---- srclines
    block_comments = {}
    for m in mods:
        for a, c in m.block_comments.items():
            block_comments.setdefault(a, c)
    line_labels = {}
    for m in mods:
        for d in m.defs:
            if d["how"] in ("label", "local") and not d["in_exp"]:
                line_labels.setdefault((d["file"], d["line"]), []).append(d["src_name"])
    srclines = []
    run = None
    for a in sorted(owner):
        e = all_events[owner[a]]
        key = (e["module"], e["origin"])
        if run and run["_key"] == key and run["addr"] + run["len"] == a:
            run["len"] += 1
            if owner[a] not in run["_evs"]:
                run["_evs"].append(owner[a])
            continue
        run = {"_key": key, "_evs": [owner[a]], "addr": a, "len": 1}
        srclines.append(run)
    out_lines = []
    for r in srclines:
        mod, (f, n) = r["_key"]
        raw = src_line(f, n)
        code, comment = split_comment(raw)
        st = parse_stmt(code)
        text = code
        for lab, g in st.labels:
            pass
        mt = re.sub(r"^\s*(?:[A-Za-z0-9$._]+::?\s*)*", "", code).strip()
        mt = re.sub(r"\s+", " ", mt)
        evs = [all_events[i] for i in r["_evs"]]
        rec = {"addr": r["addr"], "len": r["len"], "module": mod, "file": f + ".MAC", "line": n,
               "label": (line_labels.get((f, n)) or [None])[0], "mnemonic_text": mt,
               "comment": comment.strip(),
               "kind": "code" if any(e["kind"] == "insn" for e in evs) else "data"}
        if any(e["exp"] for e in evs):
            exp = []
            for e in evs:
                if e["kind"] != "insn":
                    continue
                dec = m6502.decode(mem, e["addr"])      # final bytes (after back-patching)
                if dec and dec[3] > 1:
                    fmt = "$%02X" if dec[3] == 2 and dec[1] != REL else "$%04X"
                    exp.append("%s %s" % (dec[0], m6502.MODE_NAME[dec[1]].replace(
                        "abs", fmt % dec[2]).replace("zp", fmt % dec[2]).replace("imm", "#" + fmt % dec[2])
                        .replace("rel", "$%04X" % dec[2])))
                else:
                    exp.append(e["text"])
            rec["expansion"] = exp or None
            if rec["expansion"] is None:
                del rec["expansion"]
        if r["addr"] in block_comments:
            rec["block_comment"] = block_comments[r["addr"]]
        rec["rom_match"] = all(mem[x] == rom[x] for x in range(r["addr"], r["addr"] + r["len"]))
        out_lines.append(rec)
    with open(os.path.join(BUILD, "srclines.json"), "w") as fp:
        json.dump(out_lines, fp, indent=0)

    # ---- symbols
    first_event_at = {}
    for i, e in enumerate(all_events):
        first_event_at.setdefault(e["addr"], e)
    ramnames = set()
    symbols, seen = [], set()
    counts = {}
    label_checks = [0, 0, []]
    values_by_name = {}
    for m in mods:
        for d in m.defs:
            values_by_name.setdefault((m.name, d["name"]), set()).add(d["value"])
    for m in mods:
        for d in m.defs:
            name = d["src_name"]
            if name.startswith(".") or (d["in_exp"] and re.match(r"^S\d+$", name)):
                continue
            if d["how"] == "assign" and d["in_exp"] and not d["glob"] and d["name"] not in m.globl:
                if len(values_by_name[(m.name, d["name"])]) > 1 or d["name"] in ("CS",):
                    continue
            file = d["file"]
            key = (d["name"], d["value"], file, d["line"])
            if key in seen:
                continue
            seen.add(key)
            v = d["value"]
            if d["how"] == "local":
                kind = "local"
            elif d["how"] == "label":
                if d["sect"] in ("ABS", "BSE") and v < 0x800:
                    kind = "ram"
                elif in_rom(v):
                    e = first_event_at.get(v)
                    kind = "code" if e and e["kind"] == "insn" else "data"
                else:
                    kind = "ram" if v < 0x800 else "equate"
            else:
                refs = d["refs"]
                if is_io(v):
                    kind = "io"
                elif v < 0x800 and any(x in ramnames for x in refs):
                    kind = "ram"
                elif in_rom(v) and v in owner and (v >= 0x5000 or refs):
                    e = first_event_at.get(v)
                    kind = "code" if e and e["kind"] == "insn" else "data"
                else:
                    kind = "equate"
            if kind == "ram":
                ramnames.add(d["name"])
            module = file if file in COMMON_FILES else m.name
            rec = {"name": d["name"], "addr": v, "kind": kind, "module": module, "file": file + ".MAC",
                   "line": d["line"], "sbttl": d["sbttl"], "comment": d["comment"]}
            if name != d["name"]:
                rec["source_name"] = name
            if d["glob"] or d["name"] in m.globl:
                rec["global"] = True
            if d["how"] == "local":
                rec["scope"] = d.get("scope")
            if d["block"]:
                rec["block_comment"] = d["block"]
            if len(values_by_name[(m.name, d["name"])]) > 1:
                rec["redefined"] = True
            if d["in_exp"]:
                rec["from_macro"] = True
            if kind == "code":
                e = first_event_at.get(v)
                dec = m6502.decode({i: rom[i] for i in range(v, min(v + 3, 0x10000))}, v)
                want = e["text"].split()[0] if e else None
                if dec and dec[0] == want:
                    label_checks[0] += 1
                else:
                    label_checks[1] += 1
                    label_checks[2].append("%s $%04X src=%s rom=%s" % (name, v, want, dec and dec[0]))
            counts[kind] = counts.get(kind, 0) + 1
            symbols.append(rec)
    symbols.sort(key=lambda s: (s["addr"], s["name"]))
    with open(os.path.join(BUILD, "symbols.json"), "w") as fp:
        json.dump(symbols, fp, indent=0)

    # ---- MAP globals cross-check
    map_diff = []
    mapf = os.path.join(macsrc.SRC_DIR, "BZONE.MAP")
    if os.path.exists(mapf):
        txt = open(mapf, "rb").read().replace(b"\0", b"").decode("latin-1")
        for nm, val in re.findall(r"([A-Z0-9$.]{1,6})\s+([0-9A-F]{4})\s", txt.split("Global Symbol Summary:")[-1]):
            if nm in ("Name",):
                continue
            if nm.startswith("QCHKS"):
                continue
            if glob.get(nm) != int(val, 16):
                map_diff.append("%s MAP=%s asm=%s" % (nm, val, "%04X" % glob[nm] if nm in glob else "missing"))

    # ---- report
    R = []
    R.append("# macasm report (Battlezone, BZONE.COM module set)\n")
    R.append("Source: `battlezone-main` (%s). Target: `%s`.\n" %
             (", ".join(MODULES), os.path.relpath(ROM64K, HERE)))
    R.append("## Summary\n")
    R.append("- Program ROM $5000-$7FFF: %d/%d bytes emitted by the source, %d match (%.2f%% of emitted, %.2f%% of 12288)."
             % (len(prog_emit), 0x3000, prog_match, 100.0 * prog_match / max(1, len(prog_emit)), 100.0 * prog_match / 0x3000))
    R.append("- Not emitted by any statement in $5000-$7FFF: %s." % (hole_spans(holes) or "none"))
    R.append("- Vector ROM $3000-$3FFF: %d bytes emitted, %d match." % (len(vrom), vrom_match))
    R.append("- The CPU decodes 15 address bits: $8000-$FFFF mirrors $0000-$7FFF, so the 6502 vectors are the "
             "words at $7FFA (NMI, RESET, IRQ).")
    R.append("- Mismatched bytes overall: %d." % len(mism))
    if lda:
        R.append("- ALEXEC.LDA.bin check: %d emitted bytes differ from the LDA image." %
                 sum(1 for a in mem if a < len(lda) and lda[a] != mem[a]))
    R.append("- Code-label decode check (ROM opcode at label == source mnemonic): %d ok, %d bad." %
             (label_checks[0], label_checks[1]))
    R.append("\n## Per module\n")
    R.append("| Module | CSECT base | MAP base | Bytes matched | Mismatched | Errors | Phase errors |")
    R.append("|---|---|---|---|---|---|---|")
    for m in mods:
        s = per_mod.get(m.name, [0, 0])
        R.append("| %s | %s | %s | %d | %d | %d | %d |" % (
            m.name, "$%04X" % bases[m.name] if m.relmax != bases[m.name] else "-",
            "$%04X" % MAP_BASES[m.name] if m.name in MAP_BASES else "-", s[0], s[1], len(m.errors), len(m.phase)))
    R.append("\n## Mismatched spans\n")
    if spans:
        for lo, hi in spans:
            R.append("- $%04X-$%04X src %s rom %s (%s)" % (lo, hi, " ".join("%02X" % mem[x] for x in range(lo, hi + 1))[:60],
                     " ".join("%02X" % rom[x] for x in range(lo, hi + 1))[:60], all_events[owner[lo]]["module"]))
    else:
        R.append("None.")
    R.append("\n## Symbols\n")
    R.append("Total %d: %s." % (len(symbols), ", ".join("%s %d" % kv for kv in sorted(counts.items()))))
    R.append("\nALEXEC.MAP global cross-check (QCHKSx excluded, MAP is the rev-1 link): %d differences." % len(map_diff))
    for x in map_diff:
        R.append("- " + x)
    if label_checks[2]:
        R.append("\nCode labels whose ROM opcode disagrees:")
        for x in label_checks[2][:50]:
            R.append("- " + x)
    R.append("\n## Unresolved / diagnostics\n")
    anyd = False
    for m in mods:
        und = {k: v for k, v in m.undefined.items()}
        if m.errors or m.phase or und:
            anyd = True
            R.append("### %s" % m.name)
            for e in m.errors[:40]:
                R.append("- error: " + e)
            for p in m.phase[:20]:
                R.append("- phase: %s pass1=$%04X pass2=$%04X" % p)
            for k, v in sorted(und.items())[:40]:
                R.append("- undefined `%s` first at %s" % (k, v))
    if not anyd:
        R.append("None.")
    R.append("\n## Notes\n")
    R.append("- Link: .CSECTs concatenated from $A8B0 in LINKM order; globals resolved by iterating the whole link until stable.")
    R.append("- HLL65 structured macros (IFxx/ELSE/ENDIF/BEGIN/xxEND) and the vector macros (VGMC, ALVROM) are expanded, not bypassed; no ROM-byte fallback was needed for matched spans.")
    R.append("- srclines.json groups bytes by the originating source line (macro call site for expansions; `expansion` lists the generated instructions). `block_comment` = comment-only lines directly above.")
    R.append("- symbols.json omits assembler temporaries (names starting with '.', HLL65 S<n> location symbols, redefined macro-internal variables).")
    with open(os.path.join(BUILD, "macasm_report.md"), "w") as fp:
        fp.write("\n".join(R) + "\n")
    print("\n".join(R[:12]))
    print("symbols:", counts)
    for m in mods:
        for e in m.errors[:8]:
            print("  ERR", e)
    print("spans:", len(spans), spans[:10] and ["%04X-%04X" % tuple(s) for s in spans[:15]])

if __name__ == "__main__":
    main()
