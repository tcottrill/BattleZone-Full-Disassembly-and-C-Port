"""svg2png.py - rasterize tests\\bz_smoke.exe's --svg picture (lines/circles only) to a PNG.

usage: python tools\\svg2png.py IN.svg OUT.png [scale]
"""
import re
import sys

from PIL import Image, ImageDraw


def main():
    src, dst = sys.argv[1], sys.argv[2]
    scale = float(sys.argv[3]) if len(sys.argv) > 3 else 1.5
    text = open(src, encoding="utf-8").read()
    vb = [float(v) for v in re.search(r'viewBox="([^"]+)"', text).group(1).split()]
    x0, y0, w, h = vb
    img = Image.new("RGB", (int(w * scale), int(h * scale)), "black")
    d = ImageDraw.Draw(img)

    def px(x, y):
        return ((x - x0) * scale, (y - y0) * scale)

    def col(m, key):
        r, g, b = (int(v) for v in re.search(r"rgb\((\d+),(\d+),(\d+)\)", m).groups())
        op = float(re.search(key + r'-opacity="([\d.]+)"', m).group(1))
        return (int(r * op), int(g * op), int(b * op))

    for m in re.findall(r"<line [^>]+>", text):
        a = dict(re.findall(r'(\w+)="([-\d.e]+)"', m))
        d.line([px(float(a["x1"]), float(a["y1"])), px(float(a["x2"]), float(a["y2"]))],
               fill=col(m, "stroke"), width=max(1, int(scale)))
    for m in re.findall(r"<circle [^>]+>", text):
        a = dict(re.findall(r'(\w+)="([-\d.e]+)"', m))
        cx, cy = px(float(a["cx"]), float(a["cy"]))
        d.ellipse([cx - 1.5, cy - 1.5, cx + 1.5, cy + 1.5], fill=col(m, "fill"))
    img.save(dst)


if __name__ == "__main__":
    main()
