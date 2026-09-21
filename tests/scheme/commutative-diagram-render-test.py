#!/usr/bin/env python3
"""Verify exported geometry from the fully native commutative-diagram renderer."""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET


def trace_geometry(directory):
    trace = directory / "trace.xml"
    subprocess.run(["mutool", "draw", "-F", "trace", "-o", str(trace),
                    str(directory / "evaluation.pdf")], check=True, timeout=30,
                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    root = ET.parse(trace)
    strokes = [p for p in root.iter("stroke_path")
               if p.get("colorspace") == "DeviceRGB" and p.get("color") == "0 0 0"]
    widths = {float(p.get("linewidth")) for p in strokes}
    if len(strokes) < 21 or len(widths) != 1:
        raise AssertionError(f"Arrow stroke widths changed across clipping: {widths}")

    page = root.findall("page")[-1]
    clips = []
    for path in page.iter("clip_path"):
        points = [(float(item.get("x")), float(item.get("y"))) for item in path
                  if item.tag in ("moveto", "lineto")
                  and item.get("x") and item.get("y")]
        if points:
            xs = [point[0] for point in points]
            ys = [point[1] for point in points]
            clips.append((min(xs), min(ys), max(xs), max(ys)))
    if len(clips) < 2:
        raise AssertionError("Long-label diagram clip was not emitted")
    clip = clips[-1]

    grid_points = []
    black_lines = []
    for path in page.iter("stroke_path"):
        points = [(float(item.get("x")), float(item.get("y"))) for item in path
                  if item.tag in ("moveto", "lineto")
                  and item.get("x") and item.get("y")]
        if not points:
            continue
        if path.get("color") == ".847 .866 1":
            if any(clip[1] <= y <= clip[3] for _, y in points):
                grid_points.extend(points)
        elif path.get("color") == "0 0 0" and len(points) == 2:
            (x1, y1), (x2, y2) = points
            if (clip[1] <= y1 <= clip[3] and clip[1] <= y2 <= clip[3]
                    and abs(y1 - y2) <= 2):
                black_lines.append((abs(x2 - x1), x1, y1, x2, y2))
    if not grid_points or not black_lines:
        raise AssertionError("Long-label diagram grid or horizontal arrow is missing")
    grid_left = min(x for x, _ in grid_points)
    grid_right = max(x for x, _ in grid_points)
    if not (clip[0] < grid_left and clip[2] > grid_right):
        raise AssertionError(
            f"Long vertex labels did not expand diagram clip {clip} beyond grid "
            f"[{grid_left}, {grid_right}]")

    _, arrow_left, arrow_y, arrow_right, _ = max(black_lines)
    if arrow_left > arrow_right:
        arrow_left, arrow_right = arrow_right, arrow_left
    glyphs = []
    for fill in page.iter("fill_text"):
        if fill.get("color") != "0 0 0":
            continue
        for span in fill.iter("span"):
            scale = float(span.get("trm", "100 0 0 100").split()[0])
            for glyph in span.iter("g"):
                if not glyph.get("x") or not glyph.get("y"):
                    continue
                x = float(glyph.get("x"))
                y = float(glyph.get("y"))
                if abs(y - arrow_y) > 100:
                    continue
                end = x + float(glyph.get("adv", "0")) * scale
                glyphs.append((x, end))
    left_ends = [end for x, end in glyphs if x < arrow_left]
    right_starts = [x for x, end in glyphs if x > arrow_right]
    if not left_ends or not right_starts:
        raise AssertionError("Could not locate long vertex formula extents")
    left_end = max(left_ends)
    right_start = min(right_starts)
    if not (left_end < arrow_left < arrow_right < right_start):
        raise AssertionError(
            "Long-label arrow overlaps a vertex formula: "
            f"left end={left_end}, arrow=[{arrow_left}, {arrow_right}], "
            f"right start={right_start}")
    print("DIAGRAM-LONG-LABEL-PASS: "
          f"clip={clip}, grid=[{grid_left}, {grid_right}], "
          f"arrow=[{arrow_left}, {arrow_right}], "
          f"labels=[{left_end}, {right_start}]")


def verify_pixels(directory):
    from PIL import Image, ImageChops

    subprocess.run(["pdftoppm", "-r", "180", "-png",
                    str(directory / "evaluation.pdf"), str(directory / "page")],
                   check=True, timeout=30)
    labels = []
    for page in sorted(directory.glob("page-*.png")):
        with Image.open(page) as image:
            r, g, b = image.convert("RGB").split()
            red = ImageChops.multiply(
                r.point(lambda x: 255 if x > 150 else 0),
                ImageChops.lighter(g, b).point(lambda x: 255 if x < 120 else 0))
            black = ImageChops.lighter(ImageChops.lighter(r, g), b).point(
                lambda x: 255 if x < 90 else 0)
            rows = [y for y in range(image.height)
                    if red.crop((0, y, image.width, y + 1)).getbbox()]
            bands = []
            for row in rows:
                if not bands or row - bands[-1][-1] > 20:
                    bands.append([])
                bands[-1].append(row)
            for band in bands:
                bounds = red.crop((0, band[0], image.width, band[-1] + 1)).getbbox()
                x1, y1, x2, y2 = bounds
                rect = (x1, band[0] + y1, x2, band[0] + y2)
                count = black.crop(rect).histogram()[255]
                labels.append(count)
    if len(labels) != 7:
        raise AssertionError(f"Expected seven rendered red labels, got {len(labels)}")
    for index, count in enumerate(labels):
        if index != 4 and count:
            raise AssertionError(f"Arrow intersects label {index + 1}: {count} black pixels")
    if not labels[4]:
        raise AssertionError("Over alignment unexpectedly masks or displaces the arrow")
    print(f"DIAGRAM-PIXELS-PASS: seven labels, black overlap counts {labels}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "runtime", "resources"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()
    try:
        import PIL  # noqa: F401
        have_pillow = True
    except ImportError:
        have_pillow = False
    if not shutil.which("mutool"):
        print("SKIP: MuPDF mutool is needed for diagram geometry verification")
        return 77
    with tempfile.TemporaryDirectory(prefix="athena-diagram-render-") as temporary:
        directory = Path(temporary)
        script = Path(__file__).resolve()
        try:
            subprocess.run([
                sys.executable, str(script.with_name("evaluation-bar-test.py")),
                "--setup-script", str(script.with_name("editor-owner-setup.scm")),
                "--script", str(script.with_name("commutative-diagram-test.scm")),
                "--binary", args.binary, "--runtime", args.runtime,
                "--resources", args.resources, "--artifacts", str(directory)],
                check=True, timeout=75)
            trace_geometry(directory)
            if have_pillow and shutil.which("pdftoppm"):
                verify_pixels(directory)
            else:
                print("DIAGRAM-PIXELS-SKIP: Pillow and pdftoppm are optional")
        finally:
            if args.artifacts:
                args.artifacts.mkdir(parents=True, exist_ok=True)
                for artifact in directory.iterdir():
                    shutil.copy2(artifact, args.artifacts / artifact.name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
