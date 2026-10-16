#!/usr/bin/env python3
"""Catalog literal macro declarations in the shipped XML v1 style sources.

This is declaration discovery, not style evaluation. Computed assignment names,
aliases and evaluated macro values remain outside the catalog.
"""

import argparse
from collections import Counter
from pathlib import Path
import sys
import xml.etree.ElementTree as ET


class DeclarationError(ValueError):
    pass


class NoDoctypeBuilder(ET.TreeBuilder):
    def doctype(self, name, public_id, system_id):
        raise DeclarationError("DOCTYPE is not allowed in shipped style sources")


def declarations(path):
    """Return declared names and the count of unevaluated computed names."""
    try:
        root = ET.parse(path, parser=ET.XMLParser(target=NoDoctypeBuilder())).getroot()
    except (ET.ParseError, DeclarationError) as error:
        raise DeclarationError(f"{path}: {error}") from error
    if root.tag != "athena-document" or root.attrib != {
        "version": "1", "text-model": "utf-8"
    }:
        raise DeclarationError(f"{path}: expected shipped XML v1 UTF-8 style source")
    names = set()
    computed = 0
    for assignment in root.iter("node"):
        if assignment.get("tag") != "assign" or len(assignment) != 2:
            continue
        name, value = assignment
        if value.tag != "node" or value.get("tag") not in {"macro", "xmacro"}:
            continue
        if name.tag != "text" or len(name):
            computed += 1
            continue
        # Names in today's shipped v1 sources are plain UTF-8 text. Fail rather
        # than misinterpret a newly introduced encoded/metadata-bearing form.
        if name.attrib:
            raise DeclarationError(f"{path}: encoded macro name needs explicit decoder support")
        literal = name.text or ""
        if not literal or "\0" in literal:
            raise DeclarationError(f"{path}: empty or NUL-containing macro name")
        names.add(literal)
    return names, computed


def catalog(resource_root):
    names = set()
    skipped = Counter()
    files = []
    for directory in ("packages", "styles"):
        base = resource_root / directory
        if not base.is_dir():
            raise DeclarationError(f"Missing shipped resource directory: {base}")
        files.extend(base.rglob("*.ats"))
    for path in sorted(files):
        declared, computed = declarations(path)
        names.update(declared)
        if computed:
            skipped[path.relative_to(resource_root).as_posix()] = computed
    return sorted(names), skipped, len(files)


def cpp_string(value):
    result = []
    for byte in value.encode("utf-8"):
        if byte in (34, 92):
            result.append("\\" + chr(byte))
        elif 32 <= byte < 127:
            result.append(chr(byte))
        else:
            result.append(f"\\{byte:03o}")
    return '"' + "".join(result) + '"'


def render(names):
    lines = ["// Generated from shipped .ats declarations; do not edit."]
    lines.extend(f"make_tree_label ({cpp_string(name)});" for name in sorted(set(names)))
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resources", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        names, skipped, files = catalog(args.resources)
        output = render(names)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        if not args.output.exists() or args.output.read_text(encoding="ascii") != output:
            args.output.write_text(output, encoding="ascii")
    except (DeclarationError, OSError) as error:
        parser.exit(1, f"Shipped macro label generation failed: {error}\n")
    print(f"Shipped macro labels: {len(names)} names from {files} files; "
          f"{sum(skipped.values())} computed declarations require runtime declarations")
    for path, count in sorted(skipped.items()):
        print(f"  computed names omitted: {path}: {count}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
