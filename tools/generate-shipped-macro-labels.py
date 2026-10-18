#!/usr/bin/env python3
"""Catalog macro declarations in trusted shipped XML style sources.

Discover literal definitions, proven macro aliases, and declaration templates
at static factory calls. This does not evaluate styles or execute macro bodies.
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


def read_style(path):
    try:
        root = ET.parse(path, parser=ET.XMLParser(target=NoDoctypeBuilder())).getroot()
    except (ET.ParseError, DeclarationError) as error:
        raise DeclarationError(f"{path}: {error}") from error
    if root.tag != "athena-document" or root.attrib != {
        "version": "1", "text-model": "utf-8"
    }:
        raise DeclarationError(f"{path}: expected shipped XML v1 UTF-8 style source")
    return root


def tag(node):
    return node.get("tag") if node.tag == "node" else None


def unwrap_style(node):
    while tag(node) == "style-with" and len(node) % 2 == 1:
        node = node[-1]
    return node


def static_text(node, arguments):
    """Resolve only literal name templates, never general style expressions."""
    if node.tag == "text" and not len(node):
        if node.attrib:
            raise DeclarationError("Encoded declaration name needs explicit decoder support")
        return node.text or ""
    kind = tag(node)
    if kind == "arg" and len(node) == 1:
        return arguments.get(static_text(node[0], {}))
    if kind == "unquote" and len(node) == 1:
        return static_text(node[0], arguments)
    if kind == "merge":
        parts = [static_text(part, arguments) for part in node]
        if all(part is not None for part in parts):
            return "".join(parts)
    return None


def discover(roots):
    names, aliases, factories = set(), set(), {}
    unresolved = set()
    queue, visited = [], set()

    def declare(name):
        if not name or "\0" in name:
            raise DeclarationError("Empty or NUL-containing macro name")
        names.add(name)

    # Literal declarations remain valid even inside an unevaluated macro.
    # Factory parameter scopes, however, must never leak into nested macros.
    for root in roots:
        for assignment in root.iter("node"):
            if tag(assignment) != "assign" or len(assignment) != 2:
                continue
            name = static_text(assignment[0], {})
            value = unwrap_style(assignment[1])
            if tag(value) in {"macro", "xmacro"}:
                if name is None:
                    unresolved.add(id(assignment))
                else:
                    declare(name)
                    if tag(value) == "macro" and len(value):
                        params = [static_text(param, {}) for param in list(value)[:-1]]
                        if all(param is not None for param in params) and len(set(params)) == len(params):
                            factories.setdefault(name, []).append((params, value[-1]))
            elif name is not None and tag(value) == "value" and len(value) == 1:
                target = static_text(value[0], {})
                if target is not None:
                    aliases.add((name, target))

    def scan(node, arguments):
        kind = tag(node)
        if kind in {"macro", "xmacro", "quote"}:
            return
        if kind == "assign" and len(node) == 2:
            value = unwrap_style(node[1])
            name = static_text(node[0], arguments)
            if name is not None:
                if tag(value) in {"macro", "xmacro"}:
                    declare(name)
                elif tag(value) == "value" and len(value) == 1:
                    target = static_text(value[0], arguments)
                    if target is not None:
                        aliases.add((name, target))
            return
        if kind in factories:
            for params, body in factories[kind]:
                if len(params) != len(node):
                    continue
                bound = tuple(static_text(arg, arguments) for arg in node)
                key = (id(body), bound)
                if key not in visited:
                    if len(visited) >= 10000:
                        raise DeclarationError("Declaration factory expansion limit exceeded")
                    visited.add(key)
                    queue.append((body, dict(zip(params, bound))))
            return
        for child in node:
            scan(child, arguments)

    for root in roots:
        scan(root, {})
    while queue:
        body, arguments = queue.pop()
        scan(body, arguments)
    while True:
        additions = {name for name, target in aliases if target in names} - names
        if not additions:
            break
        for name in additions:
            declare(name)
    return names, len(unresolved)


def declarations(path):
    """Inspect one trusted style; cross-file factories resolve in catalog()."""
    return discover([read_style(path)])


def catalog(resource_root):
    names = set()
    skipped = Counter()
    files = []
    for directory in ("packages", "styles"):
        base = resource_root / directory
        if not base.is_dir():
            raise DeclarationError(f"Missing shipped resource directory: {base}")
        files.extend(base.rglob("*.ats"))
    roots = []
    for path in sorted(files):
        root = read_style(path)
        roots.append(root)
        _, computed = discover([root])
        if computed:
            skipped[path.relative_to(resource_root).as_posix()] = computed
    names, _ = discover(roots)
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
          f"{sum(skipped.values())} computed declaration templates inspected")
    for path, count in sorted(skipped.items()):
        print(f"  computed declaration templates: {path}: {count}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
