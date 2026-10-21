#!/usr/bin/env python3
"""Repair migrated semantic fields and redirect their internal UUID targets.

Back up the vault, start ATHENA with its normal profile, and edit saved
documents through AUDMAP. No title extraction or body formatting migration is
rerun. Use --apply to write; otherwise report the proposed changes.

SPDX-License-Identifier: GPL-3.0-or-later
"""

from copy import deepcopy
import importlib.util
import os
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location(
    "athena_field_repair_shared", Path(__file__).with_name("migrate-provable-fields.py"))
shared = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = shared
spec.loader.exec_module(shared)

FIELDS = {"name", "attribution"}
UUID = re.compile(r"[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}")
LINK = re.compile(r"^(tmfs://(?:wikilink|transclude)/)([^/?#]+)(.*)$", re.IGNORECASE)


def mentions_alias(path, aliases):
    return any(identity in aliases for identity in UUID.findall(path.read_text(encoding="utf-8")))


def prepare_config(config):
    root = config["_root"]
    aliases, affected, files = dict(config.get("aliases", {})), set(), []
    for parent, dirs, names in os.walk(root):
        dirs[:] = [d for d in dirs if d not in shared.EXCLUDED]
        for name in names:
            path = Path(parent) / name
            if path.suffix not in {".ath", ".ats", ".avd"}:
                continue
            files.append(path)
            if path.suffix == ".avd":
                continue  # AVD is JSON, not a stored document tree.
            tree = ET.parse(path)
            for node in tree.findall('.//n[@t="enunciation"]'):
                for field in node.findall("./x/p"):
                    if field.get("n") not in FIELDS:
                        continue
                    for item in field.iter():
                        identity = item.get("i")
                        if identity:
                            owner = node.get("i")
                            if not owner:
                                raise ValueError(f"Enunciation has no UUID: {path}")
                            if identity in aliases and aliases[identity] != owner:
                                raise ValueError(f"Ambiguous internal UUID: {identity}")
                            aliases[identity] = owner
                        if identity or (item.tag == "n" and item.get("t") == "with"
                                        and len([c for c in item if c.tag != "x"]) == 1):
                            affected.add(str(path.relative_to(root)))
    # Also include documents that only contain incoming references. AVD is
    # checked explicitly rather than silently excluding possible references.
    for path in files:
        if not mentions_alias(path, aliases):
            continue
        if path.suffix != ".ath":
            raise ValueError(f"Reference outside an AUDMAP saved .ath document: {path}")
        affected.add(str(path.relative_to(root)))
    config.update(aliases=aliases, _files=affected, clean_fields=True)
    print(f"Scan: {len(files)} files; {len(affected)} candidate documents; "
          f"{len(aliases)} internal UUIDs to retire", flush=True)


def needs_cleanup(value):
    return any(n.get("id") or (n.get("tag") == "with" and len(shared.children(n)) == 1)
               for n in shared.all_nodes(value))


def rewrite_value(value, aliases, counts, clean=False):
    result = deepcopy(value)
    kind = result.get("type")
    if kind == "reference":
        old = result["value"]
        result["value"] = aliases.get(old, old)
        counts["retargeted"] += int(old != result["value"])
    elif kind == "string":
        result["value"] = rewrite_link(result["value"], aliases, counts)
    elif kind == "list":
        result["value"] = [rewrite_value(v, aliases, counts, clean) for v in result["value"]]
    elif kind == "dictionary":
        result["value"] = [{**p, "value": rewrite_value(p["value"], aliases, counts, clean)}
                           for p in result["value"]]
    elif kind == "rich_tree":
        tree = result["value"]
        if clean and needs_cleanup(tree):
            counts["retired_ids"] += len(shared.identities(tree))
            counts["empty_with"] += sum(n.get("tag") == "with" and len(shared.children(n)) == 1
                                        for n in shared.all_nodes(tree))
            tree = shared.compact(tree)
        result["value"] = rewrite_rich_tree(tree, aliases, counts)
    return result


def rewrite_link(text, aliases, counts):
    match = LINK.fullmatch(text)
    if match and match[2] in aliases:
        counts["retargeted"] += 1
        return match[1] + aliases[match[2]] + match[3]
    return text


def rewrite_rich_tree(node, aliases, counts):
    node = deepcopy(node)
    if "text" in node:
        node["text"] = rewrite_link(node["text"], aliases, counts)
    if "properties" in node:
        node["properties"] = [{**p, "value": rewrite_value(p["value"], aliases, counts)}
                              for p in node["properties"]]
    if "children" in node:
        node["children"] = [rewrite_rich_tree(c, aliases, counts) for c in node["children"]]
    if node.get("tag") == "transclude":
        items = shared.children(node)
        targets = shared.children(items[0]) if len(items) == 1 and items[0].get("tag") == "tuple" else items[:1]
        for target in targets:
            old = target.get("text")
            if old in aliases:
                target["text"] = aliases[old]
                counts["retargeted"] += 1
    return node


def plan_document(document, relative, config):
    aliases = config["aliases"]
    checks, edits, records, expected = [], [], [], {}
    counts = shared.Counter()

    def edit(path, node, command, parameters):
        checks.append({"path": list(path), "tree": node})
        edits.append({"path": list(path), "command": command, "parameters": parameters})

    def walk(node, path, cached=False, target=False):
        tag, items = node.get("tag"), shared.children(node)
        cached = cached or tag == "references"
        if cached and tag == "associate" and items and any(
                identity in aliases for identity in UUID.findall(items[0].get("text", ""))):
            edit(path, node, "erase", {})
            counts["cache_entries"] += 1
            return
        if "text" in node:
            old = node["text"]
            text = aliases.get(old, old) if target else rewrite_link(old, aliases, counts)
            if text != old:
                if target:
                    counts["retargeted"] += 1
                edit(path, node, "set", {"tree": {"text": text}})
        updates = []
        for prop in node.get("properties", []):
            clean = config.get("clean_fields", False) and tag == "enunciation" and prop["name"] in FIELDS
            value = rewrite_value(prop["value"], aliases, counts, clean)
            if value != prop["value"]:
                updates.append({**prop, "value": value})
        if updates:
            edit(path, node, "update_properties", {"set": updates, "remove": []})
            counts["fields"] += len(updates)
            if tag == "enunciation":
                expected[node["id"]] = {p["name"]: p["value"] for p in updates}
        for i in reversed(range(len(items))):
            is_target = (tag == "transclude" and i == 0) or (target and tag == "tuple")
            walk(items[i], path + (i,), cached, is_target)

    walk(document, ())
    if edits:
        records.append({"file": str(relative), "status": "planned", **counts})
    return shared.Plan(checks, edits, records, expected)


def main(argv=None):
    return shared.main(argv, planner=plan_document, migration="enunciation-field-repair",
                       client_name="Definition field migration", description=__doc__,
                       config_file=None, validate_config=prepare_config)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, ValueError, OSError) as error:
        print(f"ERROR: {error}", file=sys.stderr, flush=True)
        sys.exit(1)
