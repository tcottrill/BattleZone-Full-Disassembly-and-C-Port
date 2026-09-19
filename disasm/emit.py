"""Emit disasm/bzone_program_rom.asm - annotated listing of the program ROM $5000-$7FFF.

Format copied from the Tempest / Space Duel projects: Ophis-style syntax, every
code/data line carries an address label Lxxxx, named labels on their own line,
`.byte`/`.word` data, raw bytes where an assembler would pick another encoding,
a comment header before every code routine.

Where everything comes from
  * macasm.py re-assembles Atari's source (BZMTNS BZONE BZSOUN VGUT BZSTST link
    order, BZONE.COM module set) byte-exact; each assembler event (one instruction /
    one .BYTE / .WORD directive, macro expansions included) becomes one listing
    unit, so the listing follows the source statement for statement.
  * Operands: the source operand expression is translated to listing syntax
    (RT-11 left-to-right evaluation is made explicit with [ ] grouping), with the
    source's own symbol names.  It is only used if it evaluates to exactly the
    operand byte(s) in the ROM; otherwise the operand is printed in hex (and the
    source text is kept in the comment).  Branch targets are always labels.
  * Names: build/symbols.json (Atari names, `.`->`_`, `$`->`S_`; local N$ labels
    -> SCOPE_N; a name defined twice with different values gets _ADDR).
  * Comments: Atari's inline comments verbatim (upper case), macro call-site text
    for expanded statements, Atari comment blocks.
  * Routine headers: .SBTTL / label comment / comment block above the label
    (source), else routine_docs.DOCS (written for this disassembly from reading
    the code).

Run:  python emit.py      (needs ../battlezone-main and the 64K image)
Also writes build/program_refs.json (identifiers the listing uses) for emit_defines.py.
"""
import os, sys, re, json, collections
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import m6502
from m6502 import IMP, ACC, IMM, ZP, ZPX, ZPY, ABS, ABX, ABY, IND, IZX, IZY, REL
import macsrc
from macsrc import split_comment, parse_stmt, split_args, read_source
from rtexpr import sym6, parse_number
import macasm

BUILD = os.path.join(HERE, "build")
OUT = os.path.join(HERE, "bzone_program_rom.asm")
DEFINES_NAME = "bzone_defines.asm"
ROM64K = os.path.join(HERE, "_survey", "roms_extracted", "bzone_cpu64k.bin")
CS_FILE = os.path.join(HERE, "Battlezone Commented Source.txt")   # none exists; load_cs() is a no-op
LO, HI = 0x5000, 0x8000

MODULE_INFO = {
    "BZMTNS": "Vector ROM mountain/terrain and object shape data ($3000-$3FFF, via the VGMC/VGAN "
              "macros); contributes no bytes to the program ROM listing (see the note at the end "
              "of this file) - present here only so module banners have a description.",
    "BZONE":  "Main game module: page-0 variables, the main (vector-sync) loop, the logo, high "
              "score table, IRQ handler, mountain/volcano drawing, 3-D translate/rotate, "
              "explosions, object projection, collision checks, tank/robot/buzz-bomber/saucer "
              "motion and AI, radar, messages and information display; the coin routine (TC65, "
              "Atari's generic COIN65/MOOLAH) and the alphanumeric vector-character macros "
              "(ASCVG) are included here.",
    "BZSOUN": "Sound driver: POKEY channel tables (start/frequency/change-rate/count per effect) "
              "and the per-frame sound routine that steps them; the HLL65 structured-if macros "
              "(COND65) are included here.",
    "VGUT":   "Vector generator utility subroutines: VGRTSL, VGHALT, VGHEX(Z), VGJMPL/VGJSRL, "
              "VGSTAT, VGCNTR, VGADD/VGADD2, VGSCAL, VGDOT, VGVTR/VGVCTR.",
    "BZSTST": "Power-on / self-test: RESET entry, zero-page/VG-RAM/ROM/EAROM tests, the "
              "bookkeeping and switch-test displays, VG diagnostic pictures; the alphanumeric "
              "(ASCVG), vector shape (VGMC) and Mathbox diagnostic (MBDIAG) macros are included "
              "here.  Ends with the 6502 vectors at $7FFA.",
}

TAG_CS = "[CS] "
COMMON_FILES = macasm.COMMON_FILES

# ---------------------------------------------------------------- helpers
def clean(name):
    """Legal assembler identifier: CHAR.A -> CHAR_A, $CCTIM -> S_CCTIM."""
    return name.replace("$", "S_").replace(".", "_")

def tidy(s):
    return re.sub(r"\s+", " ", s.replace("\t", " ")).strip()

def hexb(v):
    return "$%02X" % (v & 0xFF)

def hexw(v):
    return "$%04X" % (v & 0xFFFF)

def load_cs():
    """Commented Source: (inline{addr:text}, block{addr:[lines]})."""
    LINE_RE = re.compile(r"^([0-9A-F]{4}) ([0-9A-F]{2})([: ][0-9A-F]{2})?([: ][0-9A-F]{2})?:?\s+(\S+)(.*)$")
    inline, block = {}, {}
    if not os.path.exists(CS_FILE):
        return inline, block
    text = open(CS_FILE, "rb").read().decode("latin-1").replace("\r\n", "\n").split("\n")
    pending, last, started = [], None, False
    for n, raw in enumerate(text, 1):
        m = LINE_RE.match(raw)
        if m:
            addr = m.group(1)
            if n == 4504 and addr == "BC1E":
                addr = "B21E"
            a = int(addr, 16)
            started = True
            rest = m.group(6)
            c = rest.split(";", 1)[1].strip() if ";" in rest else ""
            if pending:
                block.setdefault(a, []).extend(pending)
                pending = []
            if c:
                inline[a] = (inline[a] + " " + c) if a in inline else c
            last = a
            continue
        s = raw.strip()
        if not s.startswith(";") or not started:
            continue
        c = s[1:].strip()
        if raw[:1] == ";":
            if c:
                pending.append(c)
        elif last is not None and c:
            inline[last] = (inline[last] + " " + c) if last in inline else c
    return inline, block

# ---------------------------------------------------------------- expressions
class Fail(Exception):
    pass

NUM_RE = re.compile(r"^[-+]?\s*(\^[HDOB])?[0-9][0-9A-Fa-f]*\.?$")

def parse_rt(s, radix=16):
    """RT-11 expression -> AST (strict left to right, <> groups)."""
    s = s.strip()
    n = len(s)

    def skip(i):
        while i < n and s[i].isspace():
            i += 1
        return i

    def term(i, rdx):
        i = skip(i)
        if i >= n:
            raise Fail("missing term")
        c = s[i]
        if c == "<":
            node, i = expr(i + 1, rdx)
            i = skip(i)
            if i < n and s[i] == ">":
                i += 1
            return ("grp", node), i
        if c == "-":
            node, i = term(i + 1, rdx)
            return ("neg", node), i
        if c == "+":
            return term(i + 1, rdx)
        if c == "'":
            if i + 1 >= n:
                raise Fail("char")
            return ("num", ord(s[i + 1]), False), i + 2
        if c == '"':
            raise Fail("dchar")
        if c == "^":
            k = s[i + 1].upper() if i + 1 < n else ""
            if k == "C":
                node, i = term(i + 2, rdx)
                return ("com", node), i
            base = {"H": 16, "D": 10, "O": 8, "B": 2}.get(k)
            if base is None:
                raise Fail("^")
            j = skip(i + 2)
            if j < n and s[j] == "<":
                return term(j, base)
            k2 = j
            while k2 < n and s[k2].upper() in "0123456789ABCDEF":
                k2 += 1
            return ("num", int(s[j:k2], base), base == 10), k2
        j = i
        while j < n and s[j].upper() in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$._":
            j += 1
        tok = s[i:j]
        if not tok:
            raise Fail("bad char %r" % c)
        up = tok.upper()
        if up[0].isdigit():
            if up.endswith("$"):
                raise Fail("local")
            try:
                return ("num", parse_number(up, rdx), up.endswith(".") or rdx == 10), j
            except ValueError:
                raise Fail("number")
        if up.startswith(".") or up == ".":
            raise Fail("dot/temp")
        return ("sym", up), j

    def expr(i, rdx):
        acc, i = term(i, rdx)
        while True:
            i = skip(i)
            if i >= n or s[i] not in "+-*/&!":
                return acc, i
            op = s[i]
            b, i = term(i + 1, rdx)
            acc = ("bin", op, acc, b)

    node, i = expr(0, radix)
    if skip(i) != n:
        raise Fail("junk")
    return node

FAMILY = {"+": "add", "-": "add", "*": "mul", "/": "mul", "&": "and", "!": "or"}
OUTOP = {"+": "+", "-": "-", "*": "*", "/": "/", "&": "&", "!": "|"}

def tdiv(a, b):
    if b == 0:
        raise Fail("div0")
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q

class Renderer:
    def __init__(self, names):
        self.names = names

    def render(self, text, module):
        """-> (listing text, value as unbounded int, used idents) or raise Fail."""
        node = parse_rt(text)
        used = []
        s, v, _ = self._r(node, module, used)
        return s, v, used

    def _r(self, node, module, used):
        t = node[0]
        if t == "num":
            v = node[1]
            return (str(v) if node[2] or v < 10 else ("$%X" % v)), v, "atom"
        if t == "sym":
            ident, v = self.names.resolve(module, node[1])
            used.append(ident)
            return ident, v, "atom"
        if t == "grp":
            s, v, k = self._r(node[1], module, used)
            if k == "bin":
                return "[" + s + "]", v, "atom"
            return s, v, k
        if t in ("neg", "com"):
            s, v, k = self._r(node[1], module, used)
            if k == "bin":
                s = "[" + s + "]"
            return ("-" if t == "neg" else "~") + s, (-v if t == "neg" else ~v), "atom"
        if t == "bin":
            op = node[1]
            ls, lv, lk = self._r(node[2], module, used)
            rs, rv, rk = self._r(node[3], module, used)
            if lk == "bin" and FAMILY[node[2][1]] != FAMILY[op]:
                ls = "[" + ls + "]"
            if rk == "bin":
                rs = "[" + rs + "]"
            if op == "+": v = lv + rv
            elif op == "-": v = lv - rv
            elif op == "*": v = lv * rv
            elif op == "/": v = tdiv(lv, rv)
            elif op == "&": v = lv & rv
            else: v = lv | rv
            node_bin = ls + OUTOP[op] + rs
            return node_bin, v, "bin"
        raise Fail(t)

# ---------------------------------------------------------------- names
class Names:
    """Identifier registry built from symbols.json."""
    def __init__(self, symbols, mods, glob):
        self.mods = {m.name: m for m in (mods or [])}
        self.glob = glob or {}
        self.by_key = collections.defaultdict(list)
        for s in symbols:
            if s["kind"] == "local":
                continue
            self.by_key[s["name"]].append(s)
        self.ident = {}          # (key, value) -> ident
        self.rec = {}            # ident -> representative record
        used = set()

        def rank(s):
            a = s["addr"]
            if s.get("redefined") or s.get("from_macro"):
                r = 6
            elif s["file"] == "ALCOMN.MAC":
                r = 0
            elif s["kind"] == "io":
                # a hardware register keeps its bare name even if a same-spelled ROM data
                # label (accessed only with an offset, e.g. Y,SOUND+STVAL) collides with it
                r = 1
            elif s["kind"] in ("code", "data") and LO <= a < HI:
                r = 1 if s.get("global") else 2
            elif s["kind"] == "ram":
                r = 3
            elif s["kind"] == "equate":
                r = 4
            else:
                r = 5
            return (r, s["file"], s["line"])

        order = sorted(self.by_key.items(), key=lambda kv: kv[0])
        for key, recs in order:
            recs = sorted(recs, key=rank)
            prim = recs[0]
            disp = prim.get("source_name", key)
            for s in recs:
                k = (key, s["addr"])
                if k in self.ident:
                    continue
                if s["addr"] == prim["addr"]:
                    nm = clean(disp)
                elif s["kind"] in ("code", "data", "ram", "io", "equate"):
                    nm = clean(s.get("source_name", key)) + "_%04X" % s["addr"]
                else:
                    continue            # a second value of an equate: not used symbolically
                base, i = nm, 2
                while nm in used:
                    nm = "%s_%d" % (base, i)
                    i += 1
                used.add(nm)
                self.ident[k] = nm
                self.rec[nm] = s
        self.used = used

    def resolve(self, module, token):
        k = sym6(token)
        m = self.mods[module]
        if k in m.cur:
            v = m.cur[k]
        elif k in self.glob:
            v = self.glob[k]
        else:
            raise Fail("undefined %s" % token)
        ident = self.ident.get((k, v))
        if ident is None:
            raise Fail("no ident %s=%04X" % (k, v))
        return ident, v

    def new_unique(self, nm):
        base, i = nm, 2
        while nm in self.used:
            nm = "%s_%d" % (base, i)
            i += 1
        self.used.add(nm)
        return nm

# ---------------------------------------------------------------- source helpers
_src_cache = {}

def src_line(f, n):
    if f not in _src_cache:
        _src_cache[f] = read_source(f)
    L = _src_cache[f]
    return L[n - 1] if 0 < n <= len(L) else ""

_sbttl_cache = {}

def sbttl_at(f, n):
    """.SBTTL text in effect at line n of file f (decorative banners ignored)."""
    if f not in _sbttl_cache:
        cur, arr = "", []
        for raw in read_source(f):
            code, _ = split_comment(raw)
            m = re.match(r"^\s*\.SBTTL\s*(.*)$", code, re.I)
            if m:
                t = tidy(m.group(1))
                if t and not t.startswith("*"):
                    cur = t
            arr.append(cur)
        _sbttl_cache[f] = arr
    arr = _sbttl_cache[f]
    return arr[n - 1] if 0 < n <= len(arr) else ""

def callsite(f, n):
    code, comment = split_comment(src_line(f, n))
    mt = re.sub(r"^\s*(?:[A-Za-z0-9$._]+::?\s*)*", "", code)
    return tidy(mt), tidy(comment)

def label_only_comment(f, n):
    code, comment = split_comment(src_line(f, n))
    st = parse_stmt(code)
    if st.kind == "label":
        return tidy(comment)
    return ""

# ---------------------------------------------------------------- units
class Unit:
    __slots__ = ("addr", "len", "kind", "items", "module", "origins", "exp", "text", "lohi")
    def __init__(self, addr, ln, kind, module, origin, exp, text, items=None):
        self.lohi = False
        self.addr, self.len, self.kind, self.module = addr, ln, kind, module
        self.origins, self.exp, self.text, self.items = [origin], exp, text, items or []

def data_items(e):
    """(arg_text, size) list for a .BYTE/.WORD/.VCTRS event, or None."""
    t = e["text"]
    if e["kind"] == "byte":
        m = re.match(r"^\.BYTE\s*(.*)$", t, re.I)
        args = split_args(m.group(1)) if m else None
        if not args or len(args) != e["len"]:
            return None
        return [(a, 1) for a in args]
    m = re.match(r"^\.(WORD|VCTRS)\s*(.*)$", t, re.I)
    if m:
        args = split_args(m.group(2))
        if m.group(1).upper() == "VCTRS":
            args = args[1:]
    else:
        args = split_args(t)
    if not args or 2 * len(args) != e["len"]:
        return None
    return [(a, 2) for a in args]

def build(verbose=True):
    import macasm
    mods, glob, bases = macasm.link(verbose=False)
    rom = open(ROM64K, "rb").read()
    symbols = json.load(open(os.path.join(BUILD, "symbols.json")))
    names = Names(symbols, mods, glob)
    R = Renderer(names)
    cs_inline, cs_block = load_cs()

    # ---- events -> units (owner of every byte)
    events = []
    block_comments = {}
    for m in mods:
        for e in m.events:
            if LO <= e["addr"] < HI:
                e = dict(e)
                e["module"] = m.name
                events.append(e)
        for a, c in m.block_comments.items():
            if LO <= a < HI:
                block_comments.setdefault(a, c)
    # HLL65/ALCOMN load-byte macros (LDAL, LDAH, LXL ...): an opcode byte or a
    # placeholder immediate instruction, then '.WORD value' and '.=.-1'.  The pair is
    # really 'LDA #<value' / '#>value'; the .WORD's second byte is overwritten.
    kept = []
    for e in events:
        p = kept[-1] if kept else None
        if (e["kind"] == "word" and e["len"] == 2 and p is not None and p["origin"] == e["origin"]
                and p["module"] == e["module"] and p["addr"] == e["addr"] - 1
                and ((p["kind"] == "byte" and p["len"] == 1 and rom[p["addr"]] in (0xA9, 0xA2, 0xA0))
                     or (p["kind"] == "insn" and p["len"] == 2 and rom[p["addr"]] in (0xA9, 0xA2, 0xA0)))):
            mn = {0xA9: "LDA", 0xA2: "LDX", 0xA0: "LDY"}[rom[p["addr"]]]
            arg = split_args(re.sub(r"^\.WORD\s*", "", e["text"], flags=re.I))[0]
            q = dict(p)
            q.update(kind="insn", len=2, text="%s I,%s" % (mn, arg), lohi=True)
            kept[-1] = q
            continue
        kept.append(e)
    events = kept
    owner = {}
    for i, e in enumerate(events):
        for k in range(e["len"]):
            owner.setdefault(e["addr"] + k, i)
    units = []
    for i, e in enumerate(sorted(range(len(events)), key=lambda j: (events[j]["addr"], j))):
        ev = events[e]
        own = [owner.get(ev["addr"] + k) == e for k in range(ev["len"])]
        if not any(own):
            continue                      # HLL65 back-patch of an earlier branch byte
        if not all(own):
            raise SystemExit("partially overwritten statement at $%04X: %s" % (ev["addr"], ev["text"]))
        if ev["kind"] == "insn":
            u = Unit(ev["addr"], ev["len"], "insn", ev["module"], ev["origin"], ev["exp"], ev["text"])
            u.lohi = bool(ev.get("lohi"))
            units.append(u)
        else:
            items = data_items(ev)
            u = Unit(ev["addr"], ev["len"], "data", ev["module"], ev["origin"], ev["exp"], ev["text"],
                     items if items is not None else [("", 1)] * ev["len"])
            if items is None:
                u.items = [(None, 1)] * ev["len"]
            units.append(u)
    # fill holes (bytes no statement emits)
    covered = set(owner)
    holes = [a for a in range(LO, HI) if a not in covered]
    for a in holes:
        units.append(Unit(a, 1, "hole", None, ("", 0), False, "", [(None, 1)]))
    units.sort(key=lambda u: u.addr)

    # ---- labels by address
    labels = collections.defaultdict(list)     # addr -> [(ident, rec)]
    for s in symbols:
        a = s["addr"]
        if not (LO <= a < HI) or s["kind"] not in ("code", "data", "local"):
            continue
        if s["kind"] == "local":
            nm = names.new_unique(clean(s.get("scope") or "L") + "_" + s["name"].rstrip("$"))
        else:
            nm = names.ident.get((s["name"], a))
            if nm is None:
                continue
        if nm not in [x[0] for x in labels[a]]:
            labels[a].append((nm, s))
    for a in labels:
        labels[a].sort(key=lambda x: (x[1]["kind"] == "local", not x[1].get("global"), x[1]["line"]))

    # ---- merge consecutive data events from the same source line
    merged = []
    for u in units:
        p = merged[-1] if merged else None
        if (p and u.kind in ("data", "hole") and p.kind == u.kind and p.origins[-1] == u.origins[0]
                and p.addr + p.len == u.addr and u.addr not in labels
                and all(sz == 1 for _, sz in p.items + u.items)):
            p.len += u.len
            p.items = p.items + u.items
            continue
        merged.append(u)
    units = merged

    # ---- code references (split points and routine entries)
    jsr_targets, jmp_targets, word_vals, br_targets = set(), set(), set(), set()
    caller_comments = collections.defaultdict(collections.Counter)
    memd = {a: rom[a] for a in range(0x10000)}
    for u in units:
        if u.kind == "insn":
            d = m6502.decode(memd, u.addr)
            if d and d[3] == u.len:
                if d[1] == REL:
                    br_targets.add(d[2])
                elif d[0] == "JSR":
                    jsr_targets.add(d[2])
                elif d[0] == "JMP" and d[1] == ABS:
                    jmp_targets.add(d[2])
                if d[0] in ("JSR", "JMP") and d[1] == ABS and not u.exp:
                    c = callsite(*u.origins[0])[1]
                    if c:
                        caller_comments[d[2]][c] += 1
        elif u.kind == "data":
            k = u.addr
            for _, sz in u.items:
                if sz == 2:
                    w = rom[k] | (rom[k + 1] << 8)
                    word_vals.add(w)
                    word_vals.add(w + 1)
                k += sz
    splits = set(labels) | br_targets | jsr_targets | jmp_targets

    # ---- split data units into lines (8 bytes / 4 words, at split points)
    lines = []
    for u in units:
        if u.kind == "insn":
            lines.append(u)
            continue
        cur, a = None, u.addr
        for it in u.items:
            sz = it[1]
            if cur is None or a in splits or len(cur.items) >= (8 if sz == 1 else 4) or cur.items[0][1] != sz:
                cur = Unit(a, 0, u.kind, u.module, u.origins[0], u.exp, u.text)
                cur.items = []
                lines.append(cur)
            cur.items.append(it)
            cur.len += sz
            a += sz
    starts = {u.addr: u for u in lines}

    def label_for(addr):
        if addr in labels:
            return labels[addr][0][0]
        if addr in starts:
            return "L%04X" % addr
        return None

    refs = set()
    for labs in labels.values():
        for nm, _ in labs:
            LABEL_IDENTS[nm] = True

    # ---- routine entries
    stop_mn = {"RTS", "RTI", "JMP", "BRK"}
    entries = set()
    prev = None
    for u in lines:
        if u.kind == "insn" and u.addr in labels and any(r["kind"] == "code" for _, r in labels[u.addr]):
            pd = m6502.decode(memd, prev.addr) if prev is not None and prev.kind == "insn" else None
            # a label reached only by falling through (or by a branch) is part of the
            # routine above - even when it is global (the ZATCxS/ZATCxE checksum-range
            # markers, for example) - and gets no header of its own
            if (u.addr in jsr_targets or u.addr in word_vals or prev is None or prev.kind != "insn"
                    or (pd and pd[0] in stop_mn) or u.module != prev.module):
                entries.add(u.addr)
        prev = u

    import routine_docs
    DOCS = routine_docs.DOCS
    notes = table_notes()

    # ---- render
    out = []
    W = out.append
    W(";Battlezone (Atari, 1980) - annotated disassembly of the program ROM $5000-$7FFF.")
    W(";MAME 'bzone'/'bzonea' (136014-01/-02 + 136013..136009).  Reconstructed by")
    W(";re-assembling Atari's own source (the BZONE.COM link: BZMTNS BZONE BZSOUN VGUT")
    W(";BZSTST, with TC65/COIN65, VGMC, VGAN, ASCVG and COND65/HLL65 included) byte-exact")
    W(";with disasm/macasm.py, so names and upper-case comments are Atari's.  Routine")
    W(";header descriptions come from the source (.SBTTL / label comment / comment block),")
    W(";else were written for this disassembly (tagged [note]).")
    W(";Every line below was re-encoded and byte-compared with the ROM (verify.py);")
    W(";gen_from_roms.py --check round-trips it with ca65/ld65.")
    W(";Syntax: Ophis style (.alias, .byte, .word); [ ] groups expressions, which Atari's")
    W(";MAC65 evaluated strictly left to right; ~ is one's complement, | is OR.")
    W(";The 6502 only decodes 15 address bits: $8000-$FFFF mirrors $0000-$7FFF exactly")
    W(";(RAM, hardware registers, vector RAM/ROM and this program ROM alike), so the CPU")
    W(";vectors read at $FFFA-$FFFF are the .VCTRS words at $7FFA below (see the end of")
    W(";the file).")
    W("")
    W(".org $5000")
    W("")
    W('.include "%s"' % DEFINES_NAME)
    W("")
    stats = collections.Counter()
    desc_src = collections.Counter()
    desc_list = {}
    seen_mod = set()
    last_mod = None
    last_origin = None
    last_sbttl = None
    header_used_block = set()
    foreign_idents = set()   # names given a ".alias NAME $addr" line directly above, in the listing
    aliases_after = collections.defaultdict(list)
    # labels that do not start a line (inside an instruction or word)
    for a, labs in labels.items():
        if a not in starts:
            host = max(x for x in starts if x < a)
            for nm, r in labs:
                aliases_after[host].append((nm, a))
            del_lab = True
    for a in list(labels):
        if a not in starts:
            del labels[a]

    def comment_join(parts):
        parts = [p for p in parts if p]
        return "  ".join(parts)

    def emit_line(prefix, comment):
        if comment:
            W("%-35s ;%s" % (prefix, comment) if len(prefix) < 35 else "%s ;%s" % (prefix, comment))
        else:
            W(prefix.rstrip())

    for u in lines:
        a = u.addr
        mod = u.module or last_mod
        # labels another module defines at this address (the end address of the
        # previous module: empty tables of ALVROM's disabled SPACG code) stay with
        # their own module, as equates
        foreign = [(nm, r) for nm, r in labels.get(a, [])
                   if u.module and r["module"] != u.module and r["file"][:-4] not in COMMON_FILES]
        if foreign:
            labels[a] = [x for x in labels[a] if x not in foreign]
            if not labels[a]:
                del labels[a]
            W("")
            W(";%s labels at its end address $%04X (no data of their own):" % (foreign[0][1]["module"], a))
            for nm, r in foreign:
                W(".alias %-16s $%04X" % (nm, a))
                stats["aliases"] += 1
                foreign_idents.add(nm)
        # module banner
        if mod != last_mod:
            W("")
            W(";" + "=" * 78)
            cont = " (continued)" if mod in seen_mod else ""
            W("; MODULE %s%s   %s.MAC" % (mod, cont, mod))
            for ln in wrap(MODULE_INFO.get(mod, ""), 74):
                W(";   " + ln)
            W(";" + "=" * 78)
            seen_mod.add(mod)
            last_mod = mod
        f, n = u.origins[0]
        sb = sbttl_at(f, n) if f else last_sbttl
        sb_changed = sb and sb != last_sbttl
        last_sbttl = sb
        is_entry = a in entries
        labs = labels.get(a, [])
        cs_blk = cs_block.get(a, [])
        if is_entry:
            name = labs[0][0]
            src = []
            if sb_changed:
                src.append(sb)
            for nm, r in labs:
                c = label_only_comment(r["file"][:-4], r["line"]) if r["file"].endswith(".MAC") else ""
                if c and c not in src:
                    src.append(c)
            blk = [tidy(x) for x in block_comments.get(a, []) if tidy(x)]
            if blk:
                header_used_block.add(a)
            if not src and not blk and caller_comments.get(a):
                cc = [c for c, _ in caller_comments[a].most_common(3)]
                src = ["%s  (comment at the call%s)" % (cc[0], "s" if caller_comments[a][cc[0]] > 1 else "")]
                src += ["%s  (another call)" % c for c in cc[1:]]
                desc_src["source: call-site comment"] += 1
            if src or blk:
                kind = "source"
                first = (src or blk)[0]
                rest = (src[1:] + blk) if src else blk[1:]
            elif cs_blk:
                kind = "commented"
                first, rest = TAG_CS + cs_blk[0], cs_blk[1:]
                cs_blk = []
            elif name in DOCS:
                kind = "written"
                d = DOCS[name].split("\n")
                first, rest = "[note] " + d[0], d[1:]
            else:
                kind = "none"
                first, rest = "", []
            desc_src[kind] += 1
            desc_list[name] = (a, kind, u.module)
            W("")
            W(";" + "-" * 78)
            W("; %s%s" % (name, (" - " + first) if first else ""))
            for x in rest:
                W(";   " + x)
            for x in cs_blk:
                W(";   " + TAG_CS + x)
            cs_blk = []
            aka = [nm for nm, _ in labs[1:]]
            if aka:
                W(";   (also: %s)" % ", ".join(aka))
            W(";" + "-" * 78)
            for nm, r in labs:
                W("%s:" % nm)
        else:
            if sb_changed and u.kind != "hole":
                W("")
                W(";.SBTTL " + sb)
            if labs:
                W("")
                for nm, r in labs:
                    c = label_only_comment(r["file"][:-4], r["line"]) if r["file"].endswith(".MAC") else ""
                    W("%s:%s" % (nm, (" " * max(1, 16 - len(nm)) + ";" + c) if c else ""))
        for x in notes.get(a, []):
            W(";  " + x)
        if a in block_comments and a not in header_used_block:
            for x in block_comments[a]:
                if tidy(x):
                    W(";" + tidy(x))
        for x in cs_blk:
            W(";" + TAG_CS + x)

        # comments for this line
        parts = []
        new_origin = u.origins[0] != last_origin
        if f and new_origin:
            cst, ccm = callsite(f, n)
            if u.exp:
                parts.append(cst)
            parts.append(ccm)
        last_origin = u.origins[0]
        cs = [cs_inline[x] for x in range(a, a + max(1, u.len)) if x in cs_inline]
        csc = (TAG_CS + " ".join(cs)) if cs else ""

        if u.kind == "insn":
            txt, extra = render_insn(u, memd, R, names, label_for, refs)
            stats["insn"] += 1
            emit_line("L%04X:  %s" % (a, txt), comment_join([extra] + parts + [csc]))
        else:
            items, extra = render_data(u, rom, R, refs, label_for)
            stats["data_bytes"] += u.len
            d = ".byte" if u.items[0][1] == 1 else ".word"
            if u.kind == "hole":
                parts = ["not assembled by any source statement (ROM fill)"]
            emit_line("L%04X:  %s %s" % (a, d, ", ".join(items)), comment_join(parts + [extra, csc]))
        for nm, la in aliases_after.get(a, []):
            W(".alias %-16s $%04X    ;inside the line at L%04X (L%04X+%d)" % (nm, la, a, a, la - a))
            stats["aliases"] += 1
        stats["lines"] += 1

    W("")
    W(";" + "=" * 78)
    W("; $8000-$FFFF: no memory of its own anywhere in the machine.  The 6502 only")
    W("; decodes address lines A0-A14 (15 bits): every device - this program ROM, the")
    W("; vector ROM/RAM, the hardware registers and the RAM - repeats with A15 ignored,")
    W("; so $8000-$FFFF is an exact mirror of $0000-$7FFF.  The 6502 vectors read at")
    W("; $FFFA-$FFFF are therefore the .VCTRS words at $7FFA above:")
    W(";   NMI $FFFA -> NMI ($558F)   RESET $FFFC -> PWRON ($7ACC)   IRQ $FFFE -> PWRON")
    W("; (BZSTST's .VCTRS statement gives the IRQ vector the same target as RESET; the")
    W("; game does not otherwise enable IRQs - NMI is the vector-generator interrupt).")
    W(";" + "=" * 78)
    with open(OUT, "w", newline="\n") as fp:
        fp.write("\n".join(out) + "\n")

    # identifiers needed by the defines file (everything that is not a listing label)
    label_idents = set(nm for labs in labels.values() for nm, _ in labs)
    label_idents |= set(nm for v in aliases_after.values() for nm, _ in v)
    label_idents |= foreign_idents
    need = {}
    for nm in sorted(refs):
        if nm in label_idents or re.match(r"^L[0-9A-F]{4}$", nm):
            continue
        r = names.rec.get(nm)
        if r is None:
            continue
        need[nm] = {"addr": r["addr"], "kind": r["kind"], "name": r["name"], "module": r["module"],
                    "file": r["file"], "line": r["line"], "comment": r["comment"]}
    with open(os.path.join(BUILD, "program_refs.json"), "w") as fp:
        json.dump({"refs": need, "routines": {k: {"addr": "$%04X" % v[0], "desc": v[1], "module": v[2]}
                                             for k, v in sorted(desc_list.items(), key=lambda kv: kv[1][0])}},
                  fp, indent=1)
    if verbose:
        print("wrote %s: %d lines, %d instructions, %d data bytes, %d inner aliases" %
              (os.path.relpath(OUT, HERE), stats["lines"], stats["insn"], stats["data_bytes"], stats["aliases"]))
        print("routines with headers: %d  (descriptions: source %d [of which %d from call-site comments], "
              "commented %d, written %d, none %d)" %
              (len(entries), desc_src["source"], desc_src["source: call-site comment"], desc_src["commented"],
               desc_src["written"], desc_src["none"]))
        print("symbolic operands: %d, hex fallbacks with symbols in source: %d, forced absolute: %d" %
              (RSTAT["sym"], RSTAT["fallback"], RSTAT["forced"]))
        if desc_src["none"]:
            print("routines without description:", " ".join(k for k, v in desc_list.items() if v[1] == "none"))
    return desc_src

RSTAT = collections.Counter()
LABEL_IDENTS = {}        # ident -> True for program-ROM labels (filled by build)

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

def has_symbol(expr):
    return bool(re.search(r"[G-Zg-z$._]", expr)) or bool(re.search(r"(^|[^0-9A-Fa-f])[A-Fa-f][0-9A-Fa-f]*", expr))

def is_number(expr):
    return bool(NUM_RE.match(expr.strip()))

def render_insn(u, mem, R, names, label_for, refs):
    """-> (instruction text, extra comment)."""
    d = m6502.decode(mem, u.addr)
    if d is None or d[3] != u.len:
        raise SystemExit("cannot decode source instruction at $%04X: %s" % (u.addr, u.text))
    mn, mode, val, ln = d
    parts = u.text.split(None, 1)
    srcop = parts[1].strip() if len(parts) > 1 else ""
    if parts[0].upper() != mn:
        raise SystemExit("mnemonic mismatch at $%04X: %s vs %s" % (u.addr, u.text, mn))
    if mode in (IMP, ACC):
        return mn, ""
    if mode == REL:
        t = label_for(val)
        if t is None:
            t = "$%04X" % val
        refs.add(t)
        return "%-4s %s" % (mn, t), ""
    import macasm
    mm = macasm.PREFIX_RE.match(srcop.upper())
    expr = srcop[mm.start(2):] if mm else (srcop[1:] if srcop.startswith("#") else srcop)
    sym = None
    if expr and not is_number(expr):
        try:
            s, v, used = R.render(expr, u.module)
            if not used:
                raise Fail("numbers only")           # plain hex reads better
            if mode == IMM and u.lohi:
                lo = "<" if (v & 0xFF) == val else (">" if ((v >> 8) & 0xFF) == val else None)
                ok = lo is not None
                if ok:
                    s = lo + (s if re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", s) else "[" + s + "]")
            elif mode == IMM:
                ok = (v & 0xFF) == val
                # ca65 gives a code label an address size of 2: take the low byte explicitly
                if ok and (not 0 <= v <= 0xFF or any(LABEL_IDENTS.get(x) for x in used)):
                    s = "<" + (s if re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", s) else "[" + s + "]")
            else:
                ok = v == val and not s.startswith("[")
            if ok:
                sym = s
                for x in used:
                    refs.add(x)
        except Fail:
            pass
    if sym is None and mn in ("JSR", "JMP") and mode == ABS:
        t = label_for(val)
        if t:
            sym = t
            refs.add(t)
    extra = ""
    if sym is None:
        if expr and not is_number(expr):
            RSTAT["fallback"] += 1
            extra = "(src: %s %s)" % (mn, tidy(srcop))
        sym = hexb(val) if ln == 2 else hexw(val)
    else:
        if not is_number(expr or "0"):
            RSTAT["sym"] += 1
    if mode in (ABS, ABX, ABY) and val < 0x100:
        RSTAT["forced"] += 1
        opnd = {ABS: "%s", ABX: "%s,X", ABY: "%s,Y"}[mode] % sym
        raw = ", ".join("$%02X" % mem[u.addr + i] for i in range(ln))
        return ".byte %s" % raw, "%s %s (forced absolute)" % (mn, opnd) + ((" " + extra) if extra else "")
    fmt = {IMM: "#%s", ZP: "%s", ABS: "%s", ZPX: "%s,X", ABX: "%s,X", ZPY: "%s,Y", ABY: "%s,Y",
           IND: "(%s)", IZX: "(%s,X)", IZY: "(%s),Y"}[mode]
    return "%-4s %s" % (mn, fmt % sym), extra

def render_data(u, rom, R, refs, label_for):
    out = []
    fallback = False
    a = u.addr
    for arg, sz in u.items:
        v = rom[a] if sz == 1 else rom[a] | (rom[a + 1] << 8)
        s = None
        if arg is not None and arg.strip() == "." and sz == 2 and v == a and label_for(a):
            s = label_for(a)
        elif arg and not is_number(arg) and u.module:
            try:
                t, tv, used = R.render(arg, u.module)
                if not used:
                    raise Fail("numbers only")
                if sz == 1:
                    if (tv & 0xFF) == v:
                        s = t if 0 <= tv <= 0xFF else "<" + (t if re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", t) else "[" + t + "]")
                else:
                    if (tv & 0xFFFF) == v:
                        s = t if 0 <= tv <= 0xFFFF else "[" + t + "]&$FFFF"
                if s is not None:
                    for x in used:
                        refs.add(x)
                    RSTAT["sym"] += 1
            except Fail:
                pass
            if s is None:
                fallback = True
        if s is None:
            s = hexb(v) if sz == 1 else hexw(v)
        out.append(s)
        a += sz
    extra = ""
    if fallback and not u.exp:
        RSTAT["fallback"] += 1
        extra = "(src: %s)" % tidy(u.text)
    return out, extra

# ---------------------------------------------------------------- table notes (Track B JSON)
def table_notes():
    notes = collections.defaultdict(list)
    def A(x):
        return int(x.strip("$"), 16)
    p = os.path.join(BUILD, "cam_scripts.json")
    if os.path.exists(p):
        cam = json.load(open(p))
        notes[A(cam["cam_addr"])].append("CAM enemy-motion scripts $%s-$%s: one opcode byte (+ operand), interpreted by"
                                         % (cam["cam_addr"][1:], cam["cam_end"][1:]))
        notes[A(cam["cam_addr"])].append("JSRCAM through TABJSR; branch operands are target-CAM-1. " + cam.get("interpreter", ""))
        for s in cam.get("scripts", []):
            if s.get("doc"):
                notes[A(s["addr"])].append("script %s (%d bytes): %s" % (s["name"], s["size"], s["doc"]))
        notes[A(cam["tabjsr_addr"])].append("TABJSR: CAM opcode handlers, .WORD handler-1 (dispatched with RTS), index = opcode/2:")
        ops = cam.get("opcodes", [])
        for o in ops:
            notes[A(cam["tabjsr_addr"])].append("  $%02X %-7s %s" % (o["code"], o["name"], o.get("doc", "")))
        for nm, uu in cam.get("users", {}).items():
            if isinstance(uu, dict) and "addr" in uu:
                notes[A(uu["addr"])].append("%s: %s" % (nm, uu.get("format", "")))
                if uu.get("entries"):
                    notes[A(uu["addr"])].append("  entries: " + ", ".join(uu["entries"]))
    p = os.path.join(BUILD, "vector_tables.json")
    if os.path.exists(p):
        vt = json.load(open(p))
        msg = vt.get("messages", {})
        if msg:
            notes[A(msg["ENGMSG"])].append("Message tables: " + msg.get("format", ""))
            for mm in msg.get("messages", []):
                for lang, t in mm.get("texts", {}).items():
                    notes[A(t["addr"])].append('%s message %s (%s): "%s"  x=%d' %
                                               (lang, mm["name"], mm.get("comment", ""), t["text"], t["x"]))
        bp = vt.get("between_points", {})
        if bp:
            notes[A(bp["PCOUNT"])].append("PCOUNT/PINDEX/VBASE: 'between points' pictures. " + bp.get("format", ""))
            for pic in bp.get("pictures", []):
                notes[A(pic["addr"])].append("picture %s (%d vectors): %s" % (pic["name"], pic["count"], pic.get("desc", "")))
        ct = vt.get("COLTAB", {})
        if ct:
            notes[A(ct["addr"])].append("COLTAB: " + ct.get("format", ""))
    return notes

if __name__ == "__main__":
    build()
