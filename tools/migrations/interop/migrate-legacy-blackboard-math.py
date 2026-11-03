#!/usr/bin/env python3
"""One-time XML v3 Vault upgrade of historical blackboard alphabet nodes.

The existing legacy-display migration supplies the offline backup, historical
Fossil reconstruction, index handling, native codec verification and atomic
whole-Vault publication. This script only defines the narrower source rewrite.
No normal ATHENA reader or editor compatibility is added.
"""

from collections import Counter
import importlib.util
from pathlib import Path
import sys
import xml.etree.ElementTree as ET


_helper_path = Path(__file__).with_name("migrate-legacy-display-math.py")
_spec = importlib.util.spec_from_file_location("athena_display_vault_upgrade", _helper_path)
_migration = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_migration)


def migrate_xml(data, location):
    if b"math-alpha-bbb" not in data:
        return data, Counter()
    root = ET.fromstring(data)
    _migration.require(root.tag == "a" and root.get("v") == "3",
                       f"{location}: expected XML v3 document")
    before = _migration.identity_inventory(root)
    count = 0
    for node in root.iter("n"):
        if node.get("t") != "math-alpha-bbb":
            continue
        children = _migration.children(node)
        _migration.require(len(children) == 1 and
                           children[0].tag in ("n", "t"),
                           f"{location}: unsupported blackboard alphabet child")
        _migration.require(set(node.attrib).issubset({"t", "i"}),
                           f"{location}: unsupported blackboard alphabet attributes")
        # The new kind is an atomic first child. Leave the former body node,
        # persistent IDs and any metadata extensions exactly where they are.
        kind = ET.Element("t")
        ET.SubElement(kind, "v").text = "bb"
        node.set("t", "math-variant")
        node.insert(sum(child.tag == "x" for child in node if child.tag == "x"), kind)
        count += 1
    if count == 0:
        return data, Counter()
    _migration.require(_migration.identity_inventory(root) == before,
                       f"{location}: persistent node identities changed")
    _migration.require(not any(n.get("t") == "math-alpha-bbb" for n in root.iter("n")),
                       f"{location}: legacy blackboard alphabet survived")
    result = ET.tostring(root, encoding="utf-8", xml_declaration=True)
    reread = ET.fromstring(result)
    _migration.require(_migration.identity_inventory(reread) == before,
                       f"{location}: XML/UUID round-trip mismatch")
    return result, Counter({"math-alpha-bbb": count})


# Existing Vault routines resolve migrate_xml in their own module. Inject the
# one-shot conversion, not a runtime format decoder.
_migration.migrate_xml = migrate_xml


if __name__ == "__main__":
    sys.exit(_migration.main())
