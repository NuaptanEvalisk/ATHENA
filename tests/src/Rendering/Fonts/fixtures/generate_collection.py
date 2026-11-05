#!/usr/bin/env python3
"""Regenerate the tiny original TTC fixture, not part of build/test execution.

Copyright (C) 2026 Nuaptan Felix Evalisk
SPDX-License-Identifier: GPL-3.0-or-later
Requires fontTools (MIT); all outlines here are original test artwork.
"""

from pathlib import Path
from tempfile import TemporaryDirectory

from fontTools.designspaceLib import AxisDescriptor, DesignSpaceDocument, InstanceDescriptor, SourceDescriptor
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTCollection
from fontTools.ttLib.tables._c_m_a_p import CmapSubtable
from fontTools.varLib import build


def face(second, rectangle_width=None):
    builder = FontBuilder(1000, isTTF=True)
    order = [".notdef", "alpha", "space", "A"] if second else [".notdef", "space", "A", "alpha"]
    builder.setupGlyphOrder(order)
    cmap = {32: "space", 65: "A", 0x3B1: "alpha"}
    # Disjoint coverage forces real fallback inside Greek and Hebrew items.
    cmap[0x5D1 if second else 0x5D0] = "alpha"
    if second:
        cmap[0x3B2] = "alpha"
    builder.setupCharacterMap(cmap)
    roman = CmapSubtable.newSubtable(0)
    roman.platformID, roman.platEncID, roman.language = 1, 0, 0
    roman.cmap = {32: "space", 65: "A"}
    builder.font["cmap"].tables.append(roman)
    glyphs = {}
    for name in order:
        pen = TTGlyphPen(None)
        if name != "space":
            pen.moveTo((0, 0))
            if second:
                pen.lineTo((700, 0))
                pen.lineTo((350, 400))
            else:
                pen.lineTo((rectangle_width or 300, 0))
                pen.lineTo((rectangle_width or 300, 700))
                pen.lineTo((0, 700))
            pen.closePath()
        glyphs[name] = pen.glyph()
    builder.setupGlyf(glyphs)
    advance = rectangle_width + 200 if rectangle_width else (900 if second else 500)
    builder.setupHorizontalMetrics({name: (advance, 0) for name in order})
    builder.setupHorizontalHeader(ascent=800, descent=-200)
    suffix = "Two" if second else "One"
    builder.setupNameTable({
        "familyName": "ATHENA Collection Fixture " + suffix,
        "styleName": "Regular",
        "uniqueFontIdentifier": "ATHENA-UTF8-Collection-" + suffix,
        "fullName": "ATHENA Collection Fixture " + suffix,
        "psName": "ATHENAFixture" + suffix,
        "version": "Version 1.0",
        "copyright": "Copyright (C) 2026 Nuaptan Felix Evalisk",
        "licenseDescription": "GNU General Public License version 3 or later",
        "licenseInfoURL": "https://www.gnu.org/licenses/gpl-3.0.html",
    })
    builder.setupOS2(sTypoAscender=800, sTypoDescender=-200, usWinAscent=800, usWinDescent=200)
    builder.setupPost()
    builder.setupMaxp()
    builder.font["head"].created = builder.font["head"].modified = 3786912000
    builder.font.recalcTimestamp = False
    return builder.font


if __name__ == "__main__":
    collection = TTCollection()
    collection.fonts = [face(False), face(True)]
    collection.save(Path(__file__).with_name("two-faces.ttc"))
    with TemporaryDirectory() as temporary:
        design = DesignSpaceDocument()
        axis = AxisDescriptor()
        axis.name, axis.tag = "Weight", "wght"
        axis.minimum, axis.default, axis.maximum = 400, 400, 900
        design.addAxis(axis)
        for weight, width in [(400, 300), (900, 700)]:
            master_path = Path(temporary) / f"master-{weight}.ttf"
            master = face(False, width)
            master["OS/2"].usWeightClass = weight
            master.save(master_path)
            source = SourceDescriptor()
            source.name, source.path = str(weight), str(master_path)
            source.familyName, source.styleName = "ATHENA Variable Fixture", str(weight)
            source.location = {"Weight": weight}
            source.copyInfo = source.copyLib = source.copyFeatures = weight == 400
            design.addSource(source)
        instance = InstanceDescriptor()
        instance.familyName, instance.styleName = "ATHENA Variable Fixture", "Heavy"
        instance.location = {"Weight": 900}
        design.addInstance(instance)
        variable, _, _ = build(design)
        variable["head"].created = variable["head"].modified = 3786912000
        variable.recalcTimestamp = False
        variable.save(Path(__file__).with_name("named-instance.ttf"))
