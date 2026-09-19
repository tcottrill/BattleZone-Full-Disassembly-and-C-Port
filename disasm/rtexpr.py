"""RT-11 MAC65 (MACRO-11 style) expression evaluator for the Tempest sources.

Adapted from the Space Duel rtexpr.py.  Semantics:
  * no operator precedence, strict left to right; <...> groups
  * binary  + - * / & !    unary + - and ^C (one's complement of the next term)
  * ^H ^D ^O ^B radix override for the next number, trailing '.' = decimal
  * 'c  = ASCII value of one character, "cc = two characters (low byte first)
  * '.' = current location counter, N$ = local symbol
  * 16-bit arithmetic; '/' is signed (truncates toward zero)
  * symbols are significant to 6 characters

The resolver callback returns (value, known).  `known` is False for forward
references, externals and undefined symbols; the assembler uses it to decide
zero-page vs absolute operand size exactly the way a two-pass assembler does.
"""
import re

SYMCH = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$._"

def sym6(name):
    return name.upper()[:6]

class ExprError(Exception):
    pass

def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v

def parse_number(tok, radix):
    t = tok.upper()
    if t.endswith("."):
        body = t[:-1]
        try:
            return int(body or "0", 10)
        except ValueError:
            v = 0                            # "6C." in ALVROM: MAC65 accumulates
            for ch in body:                  # each digit *10 regardless (= 72.)
                v = v * 10 + int(ch, 36)
            return v
    return int(t, radix)

class Evaluator:
    """resolve(name) -> (value, known) or raises KeyError for undefined."""
    def __init__(self, resolve, radix=16, dot=0):
        self.resolve, self.radix, self.dot = resolve, radix, dot
        self.known = True
        self.undefined = []

    def eval(self, s):
        self.known = True
        self.undefined = []
        s = s.strip()
        if not s:
            return 0
        v, i = self._expr(s, 0)
        while i < len(s) and s[i].isspace():
            i += 1
        if i < len(s):
            raise ExprError("junk after expression: %r" % s[i:])
        return v & 0xFFFF

    def _term(self, s, i):
        n = len(s)
        while i < n and s[i].isspace():
            i += 1
        if i >= n:
            raise ExprError("missing term in %r" % s)
        c = s[i]
        if c == "<":
            v, i = self._expr(s, i + 1)
            if i < n and s[i] == ">":
                i += 1
            return v, i
        if c == "-":
            v, i = self._term(s, i + 1)
            return (-v) & 0xFFFF, i
        if c == "+":
            return self._term(s, i + 1)
        if c == "'":
            if i + 1 >= n:
                return 0x20, n
            return ord(s[i + 1]), i + 2
        if c == '"':
            a = ord(s[i + 1]) if i + 1 < n else 0x20
            b = ord(s[i + 2]) if i + 2 < n else 0x20
            return a | (b << 8), i + 3
        if c == "^":
            k = s[i + 1].upper() if i + 1 < n else ""
            if k == "C":
                v, i = self._term(s, i + 2)
                return (~v) & 0xFFFF, i
            base = {"H": 16, "D": 10, "O": 8, "B": 2}.get(k)
            if base is None:
                raise ExprError("bad ^ operator in %r" % s)
            j = i + 2
            while j < n and s[j].isspace():
                j += 1
            if j < n and s[j] == "<":
                save = self.radix
                self.radix = base
                try:
                    v, j = self._term(s, j)
                finally:
                    self.radix = save
                return v, j
            k2 = j
            while k2 < n and s[k2].upper() in "0123456789ABCDEF":
                k2 += 1
            return int(s[j:k2], base) & 0xFFFF, k2
        j = i
        while j < n and s[j].upper() in SYMCH:
            j += 1
        tok = s[i:j]
        if not tok:
            raise ExprError("bad character %r in %r" % (c, s))
        up = tok.upper()
        if up[0].isdigit():
            if up.endswith("$") and up[:-1].isdigit():
                return self._sym(up), j
            try:
                return parse_number(up, self.radix) & 0xFFFF, j
            except ValueError:
                raise ExprError("bad number %r" % tok)
        if up == ".":
            return self.dot & 0xFFFF, j
        return self._sym(up), j

    def _sym(self, name):
        try:
            v, k = self.resolve(name)
        except KeyError:
            self.known = False
            self.undefined.append(name)
            return 0
        if not k:
            self.known = False
        return v & 0xFFFF

    def _expr(self, s, i):
        acc, i = self._term(s, i)
        n = len(s)
        while True:
            while i < n and s[i].isspace():
                i += 1
            if i >= n or s[i] not in "+-*/&!":
                return acc, i
            op = s[i]
            b, i = self._term(s, i + 1)
            acc = self.apply(acc, op, b)

    @staticmethod
    def apply(a, op, b):
        if op == "+": return (a + b) & 0xFFFF
        if op == "-": return (a - b) & 0xFFFF
        if op == "*": return (a * b) & 0xFFFF
        if op == "/":
            x, y = s16(a), s16(b)
            if y == 0:
                return 0
            q = abs(x) // abs(y)
            return (q if (x < 0) == (y < 0) else -q) & 0xFFFF
        if op == "&": return a & b
        if op == "!": return a | b
        raise ExprError(op)

if __name__ == "__main__":
    tab = {"VECRAM": 0x2000, "MLED1": 2, "MLED2": 1}
    ev = Evaluator(lambda n: (tab[sym6(n)], True), radix=16)
    for t, want in [("100", 0x100), ("12.", 12), ("^H1F", 0x1F), ("^C<MLED1!MLED2>", 0xFFFC),
                    ("VECRAM/100", 0x20), ("-2*3", 0xFFFA), ("<2+1>*2", 6), ("' ", 0x20),
                    ("-8/2&^H1F", 0x1C), ("-7/2&^H1F", 0x1D)]:
        got = ev.eval(t)
        print("  %-18s = $%04X  %s" % (t, got, "ok" if got == want else "EXPECTED $%04X" % want))
