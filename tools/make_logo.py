#!/usr/bin/env python3
"""Convert a small 2-colour image or ASCII pattern into a top-metal art macro (GDS + LEF + SVG).

    .venv-art/bin/python tools/make_logo.py art/unisc_icon.txt --name unisc_logo --pdk gf180mcuD --pixel 1.4
    .venv-art/bin/python tools/make_logo.py art/unisc_icon.txt --name unisc_logo --pdk sky130A --pixel 0.7
    .venv-art/bin/python tools/make_logo.py logo.png --name x --crop 0 6 71 19 --threshold 160

Opaque pixels (alpha > threshold), or '#' in a .txt pattern, become metal 4 (the top routing layer of a Tiny Tapeout project on both processes). The script makes the pixel grid
DRC-friendly: it fills one pixel wherever two pixels touch only at a corner
(zero spacing) and drops lone pixels (below the minimum metal area at small pitches).
"""
import argparse
import os

import gdstk
from PIL import Image

# per process: metal 4 GDS layer, its LEF name, and the place-and-route boundary layer
PDKS = {
    "sky130A":   {"metal": (71, 20), "lef": "met4",   "boundary": (235, 4)},
    "gf180mcuD": {"metal": (46, 0),  "lef": "Metal4", "boundary": (0, 0)},
}
MARGIN = 0.5             # um of empty border inside the macro


def load_bitmap(path, crop, threshold):
    if path.endswith(".txt"):
        rows = [ln.rstrip("\n") for ln in open(path) if ln.strip() and not ln.startswith("//")]
        w = max(len(r) for r in rows)
        return [[c == "#" for c in r.ljust(w, ".")] for r in rows]
    im = Image.open(path).convert("RGBA")
    if crop:
        im = im.crop(crop)
    w, h = im.size
    return [[im.getpixel((x, y))[3] > threshold for x in range(w)] for y in range(h)]


def fix_diagonals(bm):
    """Fill a pixel wherever two set pixels meet only at a corner."""
    h, w = len(bm), len(bm[0])
    changed = True
    while changed:
        changed = False
        for y in range(h - 1):
            for x in range(w - 1):
                a, b = bm[y][x], bm[y][x + 1]
                c, d = bm[y + 1][x], bm[y + 1][x + 1]
                if a and d and not b and not c:
                    bm[y][x + 1] = changed = True
                elif b and c and not a and not d:
                    bm[y][x] = changed = True
    return bm


def drop_lone_pixels(bm):
    h, w = len(bm), len(bm[0])
    out = [row[:] for row in bm]
    for y in range(h):
        for x in range(w):
            if bm[y][x] and not any(
                0 <= y + dy < h and 0 <= x + dx < w and bm[y + dy][x + dx]
                for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1))
            ):
                out[y][x] = False
    return out


def write_lef(path, name, width, height, layer):
    with open(path, "w") as f:
        f.write(f"""VERSION 5.7 ;
BUSBITCHARS "[]" ;
DIVIDERCHAR "/" ;
MACRO {name}
  CLASS BLOCK ;
  FOREIGN {name} 0 0 ;
  ORIGIN 0 0 ;
  SIZE {width:.3f} BY {height:.3f} ;
  SYMMETRY X Y ;
  OBS
    LAYER {layer} ;
      RECT 0.000 0.000 {width:.3f} {height:.3f} ;
  END
END {name}
END LIBRARY
""")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("image")
    ap.add_argument("--name", required=True, help="cell/module name")
    ap.add_argument("--crop", type=int, nargs=4, metavar=("L", "T", "R", "B"))
    ap.add_argument("--threshold", type=int, default=128)
    ap.add_argument("--pixel", type=float, default=0.42, help="pixel pitch in um")
    ap.add_argument("--pdk", choices=sorted(PDKS), default="gf180mcuD")
    ap.add_argument("--outdir", default="macros")
    args = ap.parse_args()

    bm = drop_lone_pixels(fix_diagonals(load_bitmap(args.image, args.crop, args.threshold)))
    h, w = len(bm), len(bm[0])
    p = args.pixel
    pdk = PDKS[args.pdk]

    cell = gdstk.Cell(args.name)
    rects = []
    for y in range(h):          # one rectangle per horizontal run of pixels
        x = 0
        gy = (h - 1 - y) * p + MARGIN   # image rows go down, GDS y goes up
        while x < w:
            if bm[y][x]:
                x0 = x
                while x < w and bm[y][x]:
                    x += 1
                rects.append(gdstk.rectangle((MARGIN + x0 * p, gy), (MARGIN + x * p, gy + p)))
            else:
                x += 1
    for poly in gdstk.boolean(rects, [], "or", layer=pdk["metal"][0], datatype=pdk["metal"][1]):
        cell.add(poly)

    width, height = round(w * p + 2 * MARGIN, 3), round(h * p + 2 * MARGIN, 3)
    cell.add(gdstk.rectangle((0, 0), (width, height), layer=pdk["boundary"][0], datatype=pdk["boundary"][1]))

    os.makedirs(args.outdir, exist_ok=True)
    base = os.path.join(args.outdir, args.name)
    lib = gdstk.Library(unit=1e-6, precision=1e-9)
    lib.add(cell)
    lib.write_gds(base + ".gds")
    cell.write_svg(base + ".svg", background="#203060")
    write_lef(base + ".lef", args.name, width, height, pdk["lef"])
    print("\n".join("".join("#" if v else "." for v in row) for row in bm))
    print(f"{args.name}: {w}x{h} px, {width} x {height} um, {len(cell.polygons) - 1} {pdk['lef']} polygons ({args.pdk})")


if __name__ == "__main__":
    main()
