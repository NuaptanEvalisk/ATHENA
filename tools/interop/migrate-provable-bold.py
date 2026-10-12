#!/usr/bin/env python3
"""Remove redundant whole-body bold from four provable kinds over AUDMAP.

Copyright (C) 2026 Nuaptan Felix Evalisk
SPDX-License-Identifier: GPL-3.0-or-later

Dry-run by default. This script never reads or writes document files directly.
Use an explicit instance endpoint and expected vault root, not active selection.
"""

import argparse
from contextlib import contextmanager
from dataclasses import dataclass
import json
from pathlib import Path, PurePosixPath
import sys

PROVABLES = {"theorem", "proposition", "corollary", "lemma"}
QUOTED = {"macro", "xmacro", "quote", "quasiquote", "quasi"}
INTERNAL = {".athena", ".backup", ".git"}


def children(node):
    return node.get("children", [])


def properties(node):
    return {entry["name"]: entry["value"] for entry in node.get("properties", [])}


@dataclass
class Edit:
    path: tuple
    command: str
    parameters: dict
    kind: str
    source_id: str
    header: dict


def header(node):
    return {key: value for key, value in node.items() if key != "children"}


def plan(document):
    """Only follow singleton envelopes covering the entire provable body.

    Never descend through multiple paragraphs looking for formatting to remove:
    a bold title/phrase inside an otherwise ordinary body is local emphasis.
    """
    edits = []

    def envelope(node, path, kind, source_id):
        tag, items = node.get("tag"), children(node)
        if tag in {"document", "concat"} and len(items) == 1:
            envelope(items[0], path + (0,), kind, source_id)
        elif tag == "with" and items and len(items) % 2 == 1:
            bindings = list(zip(items[:-1:2], items[1:-1:2]))
            series = [(key, value) for key, value in bindings
                      if key.get("text") == "font-series"]
            # Retain ambiguous/overriding assignments rather than changing them.
            if series and all(value.get("text") == "bold" for _, value in series):
                if any(set(x) != {"text"} for pair in series for x in pair):
                    raise ValueError("Formatting binding itself carries metadata")
                edits.append(Edit(path, "clear_formatting",
                                  {"expected_tag": "with", "bindings": [
                                      {"name": "font-series", "value": {"text": "bold"}}]}, kind, source_id,
                                  header(node)))
            envelope(items[-1], path + (len(items) - 1,), kind, source_id)
        elif tag == "strong" and len(items) == 1:
            edits.append(Edit(path, "clear_formatting",
                              {"expected_tag": "strong", "bindings": []},
                              kind, source_id, header(node)))
            envelope(items[0], path + (0,), kind, source_id)

    def walk(node, path):
        if node.get("tag") in QUOTED:
            return
        if node.get("tag") == "enunciation":
            kind = properties(node).get("kind", {}).get("value")
            items = children(node)
            if kind in PROVABLES:
                if len(items) != 1 or items[0].get("tag") != "document":
                    raise ValueError("Provable has no canonical document body")
                envelope(items[0], path + (0,), kind, node.get("id", ""))
        for index, child in enumerate(children(node)):
            walk(child, path + (index,))

    # Only source body, never initial settings, style definitions or caches.
    for index, child in enumerate(children(document)):
        if child.get("tag") == "body":
            walk(child, (index,))
    return sorted(edits, key=lambda edit: edit.path, reverse=True)


class Session:
    def __init__(self, client):
        self.client = client

    @staticmethod
    def await_result(request):
        result = request.result(timeout=300)
        if result.status != "OK":
            raise RuntimeError(f"AUDMAP {result.status}: {result.data}")
        return result.data

    @contextmanager
    def select(self, selector):
        request = self.client.resolve(selector, leaves=True)
        handles, diagnostics = self.await_result(request)
        try:
            if diagnostics:
                raise RuntimeError(f"Incomplete resolution: {diagnostics}")
            yield request.ticket, handles
        finally:
            self.await_result(self.client.finish(request.ticket))

    def call(self, ticket, handle, command, parameters=None):
        request = self.client.operate(ticket, handle, command, parameters)
        try:
            return self.await_result(request)
        finally:
            if request.future.done():
                self.await_result(self.client.release(ticket, request.operation))

    def one(self, selector, command="get", parameters=None):
        with self.select(selector) as (ticket, handles):
            if len(handles) != 1:
                raise RuntimeError(f"Expected one resource: {selector} ({len(handles)})")
            return self.call(ticket, handles[0], command, parameters)


def relative_path(value):
    path = PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts or "\x00" in value:
        raise ValueError(f"Not a vault-relative path: {value}")
    return path


def selector_for(vault, path):
    return vault + "/filesystem" + "".join(
        "/" + json.dumps(part, ensure_ascii=False) for part in path.parts)


def discover(session, vault, scopes, excluded):
    files, seen = set(), set()

    def ignored(path):
        return any(p in INTERNAL for p in path.parts) or any(
            path == prefix or prefix in path.parents for prefix in excluded)

    def visit(path):
        if ignored(path):
            return
        selector = selector_for(vault, path)
        info = session.one(selector)
        absolute = info["absolute_path"]
        if absolute in seen:
            return
        seen.add(absolute)
        if info["type"] == "file":
            if path.suffix == ".ath":
                files.add(path)
            return
        with session.select(selector + '/?($type = "file" OR $type = "directory")') as (ticket, handles):
            entries = [session.call(ticket, h, "get") for h in handles]
        for entry in sorted(entries, key=lambda x: x["name"]):
            visit(path / entry["name"])

    for scope in scopes:
        visit(scope)
    return sorted(files)


def migrate_file(session, vault, path, apply):
    selector = selector_for(vault, path)
    if session.one(selector, "buffers"):
        raise RuntimeError(f"Close the source buffer before migration: {path}")
    document = selector + "/saved"
    with session.select(document) as (ticket, handles):
        if len(handles) != 1:
            raise RuntimeError(f"Cannot resolve saved document: {path}")
        snapshot = session.call(ticket, handles[0], "get")["tree"]
        edits = plan(snapshot)
        for edit in edits:
            print(json.dumps({"file": str(path), "kind": edit.kind,
                              "enunciation_id": edit.source_id, "path": edit.path,
                              "command": edit.command, "apply": apply}, ensure_ascii=False),
                  flush=True)
            if not apply:
                continue
            # Deepest-first replacement changes no later sibling offsets.
            node_selector = document + "".join(f"/[{i}]" for i in edit.path)
            with session.select(node_selector) as (node_ticket, nodes):
                if len(nodes) != 1:
                    raise RuntimeError(f"Migration target disappeared: {path} {edit.path}")
                current = session.call(node_ticket, nodes[0], "get")["tree"]
                if header(current) != edit.header:
                    raise RuntimeError(f"Migration target changed: {path} {edit.path}")
                if session.one(selector, "buffers"):
                    raise RuntimeError(f"Source was opened during migration: {path}")
                result = session.call(node_ticket, nodes[0], edit.command, edit.parameters)
                if not result.get("committed"):
                    raise RuntimeError(f"Edit did not commit to disk: {result}")
                if not result.get("directory_synced") or not result.get("revision_available"):
                    raise RuntimeError(f"Edit committed, but durability/revision is uncertain: {result}")
        if apply and plan(session.call(ticket, handles[0], "get")["tree"]):
            raise RuntimeError(f"Unexpected remaining whole-body bold: {path}")
    return len(edits)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--endpoint", required=True, help="Explicit instance connection.json")
    parser.add_argument("--identity", required=True, help="Private AUDMAP client identity")
    parser.add_argument("--vault", required=True, help="Exact loaded vault name")
    parser.add_argument("--expect-root", required=True, type=Path, help="Require this physical vault root")
    parser.add_argument("--exclude", action="append", default=[], type=relative_path)
    parser.add_argument("--apply", action="store_true", help="Commit edits; default only reports")
    parser.add_argument("paths", nargs="+", type=relative_path, help="Vault-relative files/directories")
    args = parser.parse_args()
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "clients/python/src"))
    from athena_audmap import Client

    vault = "@/vaults/" + json.dumps(args.vault, ensure_ascii=False)
    with Client(endpoint=args.endpoint, identity=args.identity,
                name="Provable bold migration") as client:
        session = Session(client)
        info = session.one(vault)
        if Path(info["root"]).resolve() != args.expect_root.resolve():
            raise RuntimeError(f"Wrong vault root: {info['root']}")
        files = discover(session, vault, args.paths, args.exclude)
        changed = wrappers = 0
        for path in files:
            count = migrate_file(session, vault, path, args.apply)
            changed += bool(count)
            wrappers += count
        print(json.dumps({"summary": {"files": len(files), "changed_files": changed,
                                       "wrappers": wrappers, "applied": args.apply}}))


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, ValueError, OSError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
