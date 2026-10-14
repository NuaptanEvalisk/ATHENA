#!/usr/bin/env python3
"""Regenerate ATHENA SVG artwork from the original Beuron Condensed A.

Requires fontTools; --raster additionally requires rsvg-convert.
The font is a build-time input only, never required by the application.
"""

import argparse
from pathlib import Path
import subprocess

from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont


ROOT = Path(__file__).resolve().parents[2]
IMAGES = ROOT / "ATHENA/misc/images"
PURPLE = "#c45aec"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font", type=Path)
    parser.add_argument("--raster", action="store_true")
    args = parser.parse_args()
    font = TTFont(args.font)
    glyphs = font.getGlyphSet()
    glyph = glyphs[font.getBestCmap()[ord("A")]]
    bounds = BoundsPen(glyphs)
    glyph.draw(bounds)
    x0, y0, x1, y1 = bounds.bounds

    def letter(cx, cy, height, color="#fff"):
        scale = height / (y1 - y0)
        pen = SVGPathPen(glyphs)
        glyph.draw(TransformPen(pen, (scale, 0, 0, -scale,
                                     cx - (x0 + x1) * scale / 2,
                                     cy + (y0 + y1) * scale / 2)))
        return f'<path fill="{color}" d="{pen.getCommands()}"/>'

    def svg(body, size=64):
        return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" '
                f'height="{size}" viewBox="0 0 {size} {size}">\n'
                '<!-- Beuron Condensed A: Keno Wehr, SIL OFL 1.1. -->\n'
                f'{body}\n</svg>\n')

    logo = svg(f'<path fill="{PURPLE}" d="M0 0h512v512H0z"/>\n'
               + letter(256, 256, 241), 512)
    for path in (IMAGES / "ATHENA.svg",
                 ROOT / "ATHENA/misc/pixmaps/light/ATHENA.svg",
                 ROOT / "ATHENA/misc/pixmaps/dark/ATHENA.svg"):
        path.write_text(logo)

    paper = ('<path fill="#fff" stroke="#707078" stroke-width="2" '
             'stroke-linejoin="round" d="M12 3h27l13 13v45H12z"/>\n'
             '<path fill="#e8e8ed" stroke="#707078" stroke-width="2" '
             'stroke-linejoin="round" d="M39 3v13h13"/>\n')
    badge = letter(32, 38, 30, PURPLE)
    document = svg(paper + badge)
    style = svg(paper + badge + '\n'
                '<path fill="#364954" stroke="#fff" stroke-width="2" '
                'stroke-linejoin="round" d="M41 48l13-18 6 6-16 15z"/>\n'
                '<path fill="#138b84" stroke="#fff" stroke-width="2" '
                'd="M41 47c-7 0-3 9-10 10 9 4 17-1 14-7z"/>')
    compound = svg('<path fill="#ddd2e5" stroke="#707078" stroke-width="2" '
                   'd="M6 11h39v50H6z"/>\n'
                   '<path fill="#f0e9f4" stroke="#707078" stroke-width="2" '
                   'd="M9 7h39v54H9z"/>\n' + paper + badge)
    for name, content in (("text-x-athena", document),
                          ("text-x-athena-style", style),
                          ("application-x-athena-avd", compound)):
        (IMAGES / f"{name}.svg").write_text(content)

    if args.raster:
        for path in IMAGES.glob("ATHENA-*.png"):
            size = int(path.stem.split("-")[-1])
            subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size),
                            "-o", str(path), str(IMAGES / "ATHENA.svg")], check=True)


if __name__ == "__main__":
    main()
