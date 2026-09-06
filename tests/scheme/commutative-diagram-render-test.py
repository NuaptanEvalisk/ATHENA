#!/usr/bin/env python3
"""Exercise native diagram editing and check label clearance in exported pixels."""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET


def verify(directory):
    from PIL import Image, ImageChops

    trace = directory / "trace.xml"
    subprocess.run(["mutool", "draw", "-F", "trace", "-o", str(trace),
                    str(directory / "evaluation.pdf")], check=True, timeout=30,
                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    strokes = [p for p in ET.parse(trace).iter("stroke_path")
               if p.get("colorspace") == "DeviceRGB" and p.get("color") == "0 0 0"]
    widths = {float(p.get("linewidth")) for p in strokes}
    if len(strokes) < 21 or len(widths) != 1:
        raise AssertionError(f"Arrow stroke widths changed across clipping: {widths}")
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
        import PIL
    except ImportError:
        print("SKIP: Pillow is needed for PDF pixel verification")
        return 77
    if not shutil.which("pdftoppm") or not shutil.which("mutool"):
        print("SKIP: Poppler pdftoppm and MuPDF mutool are needed for PDF verification")
        return 77
    with tempfile.TemporaryDirectory(prefix="athena-diagram-render-") as temporary:
        directory = Path(temporary)
        script = Path(__file__).resolve()
        try:
            subprocess.run([
                sys.executable, str(script.with_name("evaluation-bar-test.py")),
                "--script", str(script.with_name("commutative-diagram-test.scm")),
                "--binary", args.binary, "--runtime", args.runtime,
                "--resources", args.resources, "--artifacts", str(directory)],
                check=True, timeout=75)
            verify(directory)
        finally:
            if args.artifacts:
                args.artifacts.mkdir(parents=True, exist_ok=True)
                for artifact in directory.iterdir():
                    shutil.copy2(artifact, args.artifacts / artifact.name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
