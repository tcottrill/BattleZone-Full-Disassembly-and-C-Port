#!/usr/bin/env python3
"""align_comments.py - normalise the gap between code and trailing comments.

Rule:
  * Only lines of the form  CODE <ws> /* comment */  (comment closed on the
    same line, nothing after it) or  CODE <ws> // comment  are touched.
    Full-line comments, comment blocks, and lines whose trailing comment
    continues onto the next line keep their layout.
  * Consecutive such lines with the same leading indentation form a run
    (a blank line, a line without a trailing comment, or an indentation
    change ends the run).
  * In a run every comment goes to one column: longest code part + 2.
    If that exceeds 80, lines whose code fits within 78 are aligned at the
    longest such line + 2, and longer lines get exactly 2 spaces.
  * Only the whitespace between code and comment ever changes.  Lines whose
    code part contains tabs, non-ASCII, or anything the lexer is unsure of
    are skipped.

Usage:
  python align_comments.py [--dry-run] [--backup DIR] [files...]
With no files, processes every hand-written .c/.h under c_src (generated
ROM/state files, third-party glew/miniz, tests/ref6502 and obj excluded).
"""
import os
import sys

CSRC = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

EXCLUDE_FILES = {
    # generated
    "progrom.c", "progrom.h", "vecrom.c", "vecrom.h", "mbprom.c", "mbprom.h",
    "state_defs.h",
    # third-party libraries (not hand-written for this port)
    os.path.join("platform", "windows", "glew.c"),
    os.path.join("platform", "windows", "glew.h"),
    os.path.join("platform", "windows", "wglew.h"),
    os.path.join("platform", "windows", "miniz.c"),
    os.path.join("platform", "windows", "miniz.h"),
}
EXCLUDE_DIRS = {os.path.join("tests", "ref6502"), "obj"}

MAX_COL = 80
FIT = 78


def default_files():
    out = []
    for root, dirs, files in os.walk(CSRC):
        rel_root = os.path.relpath(root, CSRC)
        dirs[:] = [d for d in dirs
                   if os.path.normpath(os.path.join(rel_root, d)) not in EXCLUDE_DIRS
                   and d not in ("obj", "__pycache__", ".git")]
        for f in files:
            if not f.endswith((".c", ".h")):
                continue
            rel = os.path.normpath(os.path.join(rel_root, f))
            if rel in EXCLUDE_FILES:
                continue
            if "GENERATED" in first_lines(os.path.join(root, f)):
                continue
            out.append(os.path.join(root, f))
    return sorted(out)


def first_lines(path, n=3):
    with open(path, "rb") as fh:
        return b"".join(fh.readline() for _ in range(n)).decode("latin-1")


def split_eol(line):
    if line.endswith("\r\n"):
        return line[:-2], "\r\n"
    if line.endswith("\n") or line.endswith("\r"):
        return line[:-1], line[-1]
    return line, ""


def lex_line(text, in_block):
    """Lex one line.

    Returns (in_block_after, info) where info is None if the line is not a
    candidate, else (code, gap, comment).
    """
    starts_in_block = in_block
    i, n = 0, len(text)
    first_comment = None     # index of first comment start outside literals
    uncertain = False
    comment_closed_at = None  # index just after the */ of the first comment
    while i < n:
        c = text[i]
        if in_block:
            j = text.find("*/", i)
            if j < 0:
                i = n
                break
            in_block = False
            i = j + 2
            if first_comment is not None and comment_closed_at is None:
                comment_closed_at = i
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            if first_comment is None:
                first_comment = i
            in_block = True
            i += 2
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            if first_comment is None:
                first_comment = i
                comment_closed_at = n
            i = n
            break
        if c in "\"'":
            q = c
            i += 1
            closed = False
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == q:
                    closed = True
                    i += 1
                    break
                i += 1
            if not closed:
                uncertain = True   # unterminated literal (or #if 0 prose)
                i = n
            continue
        i += 1

    if starts_in_block or uncertain or first_comment is None:
        return in_block, None
    code_full = text[:first_comment]
    code = code_full.rstrip(" \t")
    if not code.strip():
        return in_block, None       # full-line comment
    if in_block or comment_closed_at is None:
        return in_block, None       # comment continues onto next line
    if text[comment_closed_at:].strip(" \t") != "":
        return in_block, None       # something follows the comment
    if text.rstrip(" \t").endswith("\\"):
        return in_block, None       # macro / line continuation
    if "\t" in code or any(ord(ch) > 126 for ch in code):
        return in_block, None
    gap = code_full[len(code):]
    comment = text[first_comment:]
    return in_block, (code, gap, comment)


def indent_of(code):
    return code[:len(code) - len(code.lstrip(" "))]


def process_text(src):
    lines = src.splitlines(keepends=True)
    parsed = []
    in_block = False
    for ln in lines:
        body, eol = split_eol(ln)
        in_block, info = lex_line(body, in_block)
        parsed.append((body, eol, info))

    out = [None] * len(lines)
    changed = 0
    i = 0
    while i < len(parsed):
        body, eol, info = parsed[i]
        if info is None:
            out[i] = body + eol
            i += 1
            continue
        ind = indent_of(info[0])
        j = i
        while j < len(parsed) and parsed[j][2] is not None and indent_of(parsed[j][2][0]) == ind:
            j += 1
        run = parsed[i:j]
        lens = [len(p[2][0]) for p in run]
        col = max(lens) + 2
        if col > MAX_COL:
            fits = [L for L in lens if L <= FIT]
            col = (max(fits) + 2) if fits else None
        for k, (b, e, (code, gap, comment)) in enumerate(run):
            L = len(code)
            if col is not None and L + 2 <= col:
                target = col
            else:
                target = L + 2
            new = code + " " * (target - L) + comment
            if new != b:
                changed += 1
            out[i + k] = new + e
        i = j
    return "".join(out), changed


def main(argv):
    dry = "--dry-run" in argv
    backup = None
    args = [a for a in argv if a != "--dry-run"]
    if "--backup" in args:
        k = args.index("--backup")
        backup = args[k + 1]
        del args[k:k + 2]
    files = args or default_files()
    total = 0
    for path in files:
        for attempt in range(5):
            with open(path, "rb") as fh:
                raw = fh.read()
            src = raw.decode("latin-1")
            new, changed = process_text(src)
            if new == src:
                break
            if dry:
                print("%5d  %s" % (changed, os.path.relpath(path, CSRC)))
                total += changed
                break
            with open(path, "rb") as fh:
                if fh.read() != raw:
                    continue        # changed on disk meanwhile: redo
            if backup:
                dst = os.path.join(backup, os.path.relpath(path, CSRC))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with open(dst, "wb") as fh:
                    fh.write(raw)
            with open(path, "wb") as fh:
                fh.write(new.encode("latin-1"))
            print("%5d  %s" % (changed, os.path.relpath(path, CSRC)))
            total += changed
            break
        else:
            print("GAVE UP (file kept changing): %s" % path)
    print("total lines changed: %d" % total)


if __name__ == "__main__":
    main(sys.argv[1:])
