"""Line-level parser for the Atari MAC65 (RT-11, MACRO-11 lookalike) Tempest sources.

Adapted from the Space Duel macsrc.py.  The .MAC files are NUL padded to
512-byte blocks; read_source() strips NULs and form feeds and splits on LF.
"""
import os, re

_HERE = os.path.dirname(os.path.abspath(__file__))
SRC_DIR = os.path.normpath(os.path.join(_HERE, "..", "battlezone-main"))
# Includes the Battlezone archive does not carry.  TC65 is COIN65 under its older
# name (BZONE.DOC lists COIN65).  VGAN and ASCVG are read from Atari's other released
# archives; the ROM comparison is the proof that they are the same text.  COND65
# (BZSOUN) is the older name of HLL65 (IFxx/BEGIN/THEN/xxEND), read the same way.
ALIASES = {"TC65": "COIN65", "COND65": "HLL65"}
_S = os.path.normpath(os.path.join(_HERE, "..", ".."))
ALT_DIRS = [os.path.join(_HERE, "..", "red-baron-main"),
            os.path.join(_S, "Gravitar Dissasembly", "gravitar-main"),
            os.path.join(_S, "AAE_publish", "AAE_working_stuff", "space-duel-main"),
            os.path.join(_S, "Tempest Dissasembly", "tempest-main", "tempest-main"),
            os.path.join(_HERE, "reconstructed")]

SYMCH = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789$._"

def read_source(name):
    """Return list of raw text lines (1-based index = list index + 1)."""
    name = ALIASES.get(name.upper().replace(".MAC", ""), name)
    fn = name if name.upper().endswith(".MAC") else name + ".MAC"
    path = os.path.join(SRC_DIR, fn.upper())
    for d in ALT_DIRS:
        if not os.path.exists(path):
            path = os.path.join(d, fn.upper())
    with open(path, "rb") as f:
        data = f.read()
    text = data.replace(b"\0", b"").decode("latin-1")
    text = text.replace("\r\n", "\n").replace("\r", "\n").replace("\f", "")
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    return lines

def split_comment(s):
    """Split 'code ; comment'.  A ' char constant may quote a ';'."""
    i, n = 0, len(s)
    while i < n:
        c = s[i]
        if c == ";":
            return s[:i], s[i + 1:]
        if c == "'":
            i += 2
            continue
        i += 1
    return s, ""

LABEL_RE = re.compile(r"^\s*([A-Za-z0-9$._]+)(::?)(?!=)")
ASSIGN_RE = re.compile(r"^\s*([A-Za-z$._][A-Za-z0-9$._]*|\.)\s*(==?)\s*(.*)$")

class Stmt:
    __slots__ = ("labels", "kind", "op", "operand", "name", "glob")
    def __init__(self):
        self.labels = []          # [(name, is_global)]
        self.kind = "blank"       # blank | label | assign | op
        self.op = ""
        self.operand = ""
        self.name = ""            # assignment target
        self.glob = False         # '==' assignment
    def __repr__(self):
        return "<%s %s %s %r>" % (self.kind, self.labels, self.op or self.name, self.operand)

def parse_stmt(code):
    """Parse the code part (comment already removed) of one statement."""
    st = Stmt()
    s = code
    while True:
        m = LABEL_RE.match(s)
        if not m:
            break
        name = m.group(1)
        if name[0].isdigit() and not (name.endswith("$") and name[:-1].isdigit()):
            break
        st.labels.append((name.upper(), len(m.group(2)) == 2))
        s = s[m.end():]
    if not s.strip():
        st.kind = "label" if st.labels else "blank"
        return st
    m = ASSIGN_RE.match(s)
    if m:
        st.kind, st.name, st.glob = "assign", m.group(1).upper(), m.group(2) == "=="
        st.operand = m.group(3).strip()
        return st
    t = s.strip()
    j = 0
    while j < len(t) and t[j] in SYMCH:
        j += 1
    if j == 0:
        j = 1
    st.kind, st.op = "op", t[:j].upper()
    rest = t[j:]
    if rest.startswith(","):
        rest = rest[1:]
    st.operand = rest.strip()
    return st

def split_args(s):
    """Split macro/directive arguments at top-level commas.  <...> is stripped
    from a whole argument; a leading backslash is kept for the caller."""
    args, cur, depth, i = [], [], 0, 0
    n = len(s)
    while i < n:
        c = s[i]
        if c == "'" and depth == 0 and i + 1 < n:
            cur.append(s[i:i + 2]); i += 2; continue
        if c == "<":
            depth += 1
        elif c == ">" and depth > 0:
            depth -= 1
        if c == "," and depth == 0:
            args.append("".join(cur)); cur = []
        else:
            cur.append(c)
        i += 1
    args.append("".join(cur))
    out = []
    for a in args:
        a2 = a.strip()
        if a2.startswith("<") and a2.endswith(">") and matching_close(a2, 0) == len(a2) - 1:
            a2 = a2[1:-1]
        out.append(a2)
    if len(out) == 1 and out[0] == "" and not s.strip():
        return []
    return out

def matching_close(s, i):
    depth = 0
    for j in range(i, len(s)):
        if s[j] == "<":
            depth += 1
        elif s[j] == ">":
            depth -= 1
            if depth == 0:
                return j
    return -1

def substitute(line, table):
    """MACRO-11 formal substitution: replace whole symbol tokens found in
    `table`; an apostrophe immediately before/after a substituted formal is
    the concatenation operator and is removed."""
    out = []
    i, n = 0, len(line)
    while i < n:
        c = line[i]
        if c in SYMCH:
            j = i
            while j < n and line[j] in SYMCH:
                j += 1
            tok = line[i:j]
            key = tok.upper()
            if key in table:
                if out and out[-1] == "'":
                    out.pop()
                out.append(table[key])
                if j < n and line[j] == "'":
                    j += 1
            else:
                out.append(tok)
            i = j
            continue
        out.append(c)
        i += 1
    return "".join(out)

if __name__ == "__main__":
    import sys
    for name in sys.argv[1:]:
        lines = read_source(name)
        kinds = {}
        for l in lines:
            code, _ = split_comment(l)
            k = parse_stmt(code).kind
            kinds[k] = kinds.get(k, 0) + 1
        print(name, len(lines), kinds)
