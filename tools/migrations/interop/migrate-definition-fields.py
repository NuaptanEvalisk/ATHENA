#!/usr/bin/env python3
"""Migrate Definition title paragraphs to semantic fields using AUDMAP.

Close ATHENA first. Back up the vault to /tmp without hashes, then start ATHENA
with its normal profile. Dry-run unless --apply is supplied. Body paragraphs
and their formatting are not rewritten.

SPDX-License-Identifier: GPL-3.0-or-later
"""

import importlib.util
from pathlib import Path
import sys


spec = importlib.util.spec_from_file_location(
    "athena_provable_migration", Path(__file__).with_name("migrate-provable-fields.py"))
shared = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = shared
spec.loader.exec_module(shared)

children = shared.children
properties = shared.properties
Layout = shared.Layout
Review = shared.Review

GROUPS = (
    "attribution_cases", "parenthetical_alias_cases", "keep_parentheses_in_name",
    "keep_comma_in_single_name", "shared_noun_cases_for_review", "other_cases")


def prepare_config(config):
    config["_cases"] = {
        case["node_id"]: (group, case)
        for group in GROUPS for case in config[group]}


def title_shape(node, bold=False):
    """Accept title formatting, not a prose paragraph containing a bold term."""
    tag, items = node.get("tag"), children(node)
    if "text" in node:
        return bold or not node["text"].strip(" \t\r\n,\uff0c")
    if tag in shared.WITH and items and len(items) % 2:
        for key, value in zip(items[:-1:2], items[1:-1:2]):
            if key.get("text") == "font-series":
                bold = value.get("text") == "bold"
        return title_shape(items[-1], bold)
    if tag == "strong":
        bold = True
    if tag == "math":
        return True
    if tag == "document" and len(items) != 1:
        return False
    indices = shared.visible_children(node)
    return indices is not None and all(title_shape(items[i], bold) for i in indices)


def name_ranges(layout, start, end):
    stack, ranges, field = [], [], start
    pairs = {"(": ")", "[": "]", "{": "}"}
    for i in range(start, end):
        if i in layout.objects:
            continue
        char = layout.text[i]
        if char in pairs:
            stack.append(pairs[char])
        elif char in ")]}":
            if not stack or stack.pop() != char:
                raise Review("Unbalanced textual brackets in title")
        elif char in ",\uff0c" and not stack:
            ranges.append(shared.trim(layout.text, field, i))
            field = i + 1
    if stack:
        raise Review("Unclosed textual brackets in title")
    ranges.append(shared.trim(layout.text, field, end))
    if any(a == b for a, b in ranges):
        raise Review("Empty name between title separators")
    return ranges


def extract_title(title, node_id, config):
    layout = Layout(title)
    start, end = shared.trim(layout.text, 0, len(layout.text))
    group, case = config["_cases"].get(node_id, (None, None))
    if case and layout.text != case["expected_layout_text"]:
        raise Review("The UUID-specific title case no longer matches")
    if group == "shared_noun_cases_for_review":
        raise Review("Shared noun needs an explicit decision. Proposed names: "
                     + "; ".join(case["proposed_names"]))
    if case and case.get("rule") == "review":
        raise Review(case["action"])
    if start == end:
        raise Review("Empty title paragraph; body not inspected for a replacement")
    if not case and not title_shape(title):
        raise Review("First paragraph is not a recognized title; body left untouched")

    authors, year = [], ""
    if group == "attribution_cases":
        caption = ", ".join(case["attribution"] + ([case["year"]] if case["year"] else []))
        token = "(" + caption + ")"
        if layout.text.startswith(token, start):
            author_start = start + 1
            start, end = shared.trim(layout.text, start + len(token), end)
        elif layout.text[start:end].endswith(token):
            author_start = end - len(token) + 1
            start, end = shared.trim(layout.text, start, end - len(token))
        else:
            raise Review("Configured attribution/year parentheses no longer match")
        for author in case["attribution"]:
            authors.append(layout.slice(author_start, author_start + len(author)))
            author_start += len(author) + 2
        year = case["year"]
    if group == "parenthetical_alias_cases":
        opening = layout.text.rfind("(", start, end)
        ranges = [shared.trim(layout.text, start, opening),
                  shared.trim(layout.text, opening + 1, end - 1)]
        if ([layout.text[a:b] for a, b in ranges] != case["names"]
                or not layout.text[start:end].endswith(")")):
            raise Review("Configured parenthetical aliases no longer match")
    elif group == "keep_comma_in_single_name":
        ranges = [(start, end)]
    else:
        if case and case.get("rule") == "strip_final_stop":
            end -= 1
        ranges = name_ranges(layout, start, end)
    if any(a >= b for a, b in ranges):
        raise Review("Title extraction would produce an empty name")
    return layout, [layout.slice(a, b) for a, b in ranges], authors, year


def plan_document(document, relative, config):
    if "_cases" not in config:
        prepare_config(config)
    checks, edits, records, expected = [], [], [], {}
    targets = []

    def walk(node, path):
        if node.get("tag") in shared.QUOTED:
            return
        if (node.get("tag") == "enunciation"
                and properties(node).get("kind", {}).get("value") == "definition"):
            targets.append((path, node))
        for i, child in enumerate(children(node)):
            walk(child, path + (i,))

    for i, child in enumerate(children(document)):
        if child.get("tag") == "body":
            walk(child, (i,))
    for path, node in sorted(targets, reverse=True, key=lambda item: item[0]):
        record = {"file": str(relative), "node_id": node.get("id"), "kind": "definition"}
        fields = properties(node)
        if any(shared.nonempty_field(fields.get(key, {})) for key in ("name", "year", "attribution")):
            records.append({**record, "status": "already_structured"})
            continue
        try:
            if not node.get("id"):
                raise Review("Definition has no persistent UUID")
            if len(children(node)) != 1 or children(node)[0].get("tag") != "document":
                raise Review("Definition does not have a canonical document body")
            body = children(node)[0]
            if not children(body):
                raise Review("Definition body is empty")
            title = children(body)[0]
            record["original_title"] = Layout(title).text
            _, names, authors, year = extract_title(title, node["id"], config)

            # Only the first paragraph moves. Remaining body trees are never
            # serialized into structural set operations or stripped of bold.
            remaining = {**body, "children": children(body)[1:] or [{"text": ""}]}
            seen = set(shared.identities(remaining))
            for item in names + authors:
                shared.remove_duplicate_metadata(item, seen)
            names = [shared.compact(item) for item in names]
            authors = [shared.compact(item) for item in authors]
            updates = [shared.prop("name", "list", [
                {"type": "rich_tree", "value": item} for item in names])]
            if authors:
                updates.append(shared.prop("attribution", "list", [
                    {"type": "rich_tree", "value": item} for item in authors]))
            if year:
                updates.append(shared.prop("year", "string", year))

            after_ids = shared.identities(remaining) + shared.identities(updates)
            retired = set(shared.identities(body)) - set(after_ids)
            if len(after_ids) != len(set(after_ids)):
                raise Review("Extraction would duplicate a persistent identity")
            changes = []
            if len(children(body)) == 1:
                changes.append({"path": list(path + (0,)), "command": "insert",
                                "parameters": {"index": 1, "children": [{"text": ""}]}})
            changes.append({"path": list(path + (0, 0)), "command": "erase", "parameters": {}})
            changes.append({"path": list(path), "command": "update_properties",
                            "parameters": {"set": updates, "remove": []}})
            checks.append({"path": list(path), "tree": node})
            edits.extend(changes)
            expected[node["id"]] = {p["name"]: p["value"] for p in updates}
            config.setdefault("_redirects", {}).update({old: node["id"] for old in retired})
            records.append({**record, "status": "planned",
                            "names": [Layout(item).text for item in names],
                            "attribution": [Layout(item).text for item in authors],
                            "year": year or None,
                            "exception": node["id"] in config["_cases"]})
        except Review as error:
            records.append({**record, "status": "review", "reason": str(error)})
    return shared.Plan(checks, edits, records, expected)


def main(argv=None):
    return shared.main(argv, planner=plan_document, migration="definition-fields",
                       client_name="Definition field migration", description=__doc__,
                       config_file="definition-title-cases.json", validate_config=prepare_config)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\nInterrupted. The migration ATHENA has been stopped; backup and report retained.",
              file=sys.stderr, flush=True)
        sys.exit(130)
    except (RuntimeError, ValueError, OSError) as error:
        print(f"\nERROR: {error}", file=sys.stderr, flush=True)
        sys.exit(1)
