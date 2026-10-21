#!/usr/bin/env python3
"""Migrate leading provable headings to structured fields over AUDMAP.

Copyright (C) 2026 Nuaptan Felix Evalisk
SPDX-License-Identifier: GPL-3.0-or-later

Require ATHENA to be closed, back up the vault to /tmp without runtime/cache
directories or hashing, then launch ATHENA using its normal configuration.
Dry-run unless --apply is supplied. Document edits use AUDMAP only.
"""

import argparse
from collections import Counter
from contextlib import contextmanager
from copy import deepcopy
from dataclasses import dataclass
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import threading
import time

KINDS = {"theorem", "proposition", "corollary", "lemma"}
EXCLUDED = {".athena", ".backup", ".cache", ".git"}
BACKUP_EXCLUDED = {".athena", ".cache"}
QUOTED = {"macro", "xmacro", "quote", "quasiquote", "quasi"}
UNARY = {"strong", "em", "emphasis", "underline", "small", "large"}
WITH = {"with", "style-with", "style-with*"}
YEAR = re.compile(r"[0-9]{4}(?:s)?\Z")


def children(node):
    return node.get("children", [])


def properties(node):
    return {p["name"]: p["value"] for p in node.get("properties", [])}


def prop(name, kind, value):
    return {"name": name, "value": {"type": kind, "value": value}}


def concat(items):
    if not items:
        return {"text": ""}
    return items[0] if len(items) == 1 else {"tag": "concat", "children": items}


def all_nodes(value):
    if isinstance(value, dict):
        if "text" in value or "tag" in value:
            yield value
        for item in value.values():
            yield from all_nodes(item)
    elif isinstance(value, list):
        for item in value:
            yield from all_nodes(item)


def identities(value):
    return [node["id"] for node in all_nodes(value) if node.get("id")]


def visible_children(node):
    tag, items = node.get("tag"), children(node)
    if tag in {"document", "concat"}:
        return list(range(len(items)))
    if tag in WITH and items and len(items) % 2:
        return [len(items) - 1]
    if tag in UNARY and len(items) == 1:
        return [0]
    if tag in {"hlink", "action"} and items:
        return [0]
    return None


class Review(Exception):
    """Leave an ambiguous node unchanged and include the reason in the report."""


class Layout:
    """Text offsets ignore formatting; mathematical trees occupy one opaque slot."""

    def __init__(self, node):
        self.node = node
        self.spans = {}
        self.objects = {}
        pieces = []
        offset = 0

        def walk(current, path):
            nonlocal offset
            start = offset
            if "text" in current:
                pieces.append(current["text"])
                offset += len(current["text"])
            elif current.get("tag") not in {"label", "anchor"}:
                indices = visible_children(current)
                if indices is None:
                    self.objects[offset] = current
                    pieces.append("\ufffc")
                    offset += 1
                else:
                    for n, i in enumerate(indices):
                        if n and current.get("tag") == "document":
                            pieces.append("\n")
                            offset += 1
                        walk(children(current)[i], path + (i,))
            self.spans[path] = (start, offset)

        walk(node, ())
        self.text = "".join(pieces)

    def slice(self, left, right):
        def visit(node, path):
            start, end = self.spans[path]
            if start >= right or end <= left:
                return None
            result = deepcopy(node)
            if "text" in node:
                result["text"] = node["text"][max(0, left - start):right - start]
                return result
            indices = visible_children(node)
            if indices is None:
                return result
            parts = [visit(children(node)[i], path + (i,)) for i in indices]
            parts = [part for part in parts if part is not None]
            if not parts:
                return None
            tag = node["tag"]
            if tag in {"document", "concat"}:
                result.update(tag="concat", children=parts)
            else:
                result["children"][indices[0]] = parts[0]
            # Historical bold is not title evidence, nor a desired title style.
            if tag == "strong":
                result["tag"] = "concat"
            elif tag in WITH:
                bindings = result["children"][:-1]
                kept = []
                for i in range(0, len(bindings), 2):
                    if (bindings[i].get("text") in {"font-series", "math-font-series"}
                            and bindings[i + 1].get("text") == "bold"
                            and not identities(bindings[i:i + 2])
                            and not any(x.get("properties") for x in bindings[i:i + 2])):
                        continue
                    kept.extend(bindings[i:i + 2])
                result["children"] = kept + [result["children"][-1]]
            return result

        return visit(self.node, ())

    def cut_prefix(self, end, base):
        """Produce primitive edits without structural-set of annotated subtrees."""
        def edit(path, command, parameters):
            return {"path": list(base + path), "command": command,
                    "parameters": parameters}

        def visit(node, path):
            start, stop = self.spans[path]
            if start == stop or start >= end:
                return deepcopy(node), []
            if stop <= end:
                return None, []
            if "text" in node:
                text = node["text"][end - start:]
                return {**deepcopy(node), "text": text}, [
                    edit(path, "set", {"tree": {"text": text}})]
            indices = visible_children(node)
            if indices is None:
                raise Review("Prefix would cut through an opaque mathematical tree")
            result, operations = deepcopy(node), []
            for i in reversed(indices):
                kept, changes = visit(children(node)[i], path + (i,))
                if kept is None:
                    del result["children"][i]
                    operations.append(edit(path + (i,), "erase", {}))
                else:
                    result["children"][i] = kept
                    operations.extend(changes)
            return result, operations

        result, edits = visit(self.node, ())
        if result is None:
            result = {**deepcopy(self.node), "children": [{"text": ""}]}
            edits = [edit((), "set", {"tree": {
                "tag": "document", "children": [{"text": ""}]}})]
        return result, edits


def trim(text, start, end):
    while start < end and text[start].isspace():
        start += 1
    while end > start and text[end - 1].isspace():
        end -= 1
    return start, end


def is_dt_derivative(node):
    def terms(n):
        if n.get("tag") in {"math", "concat"}:
            return [term for child in children(n) for term in terms(child)]
        if "text" in n and not n["text"].strip():
            return []
        return [n]

    parts = terms(node)
    if len(parts) != 1 or parts[0].get("tag") != "frac" or len(children(parts[0])) != 2:
        return False
    numerator, denominator = [terms(c) for c in children(parts[0])]

    def differential(n):
        return (n.get("tag") == "math-symbol" and len(children(n)) == 1
                and children(n)[0].get("text") == "differential-d")

    return (len(numerator) == 1 and differential(numerator[0])
            and len(denominator) == 2 and differential(denominator[0])
            and denominator[1].get("text") == "t")


def leading_fields(layout, exception=None):
    text = layout.text
    start = len(text) - len(text.lstrip())
    if exception:
        prefix = exception["expected_text_prefix"]
        position = start + len(prefix)
        math = layout.objects.get(position)
        following = exception["expected_following_text_prefix"]
        if (not text.startswith(prefix, start) or not math
                or not is_dt_derivative(math)
                or not text.startswith(following, position + 1)):
            raise Review("The UUID-specific missing-parenthesis exception no longer matches")
        return [(start + 1, position + 1)], position + 3  # consume ". ", retain "If "
    if start == len(text) or text[start] != "(" or start in layout.objects:
        return None
    stack, ranges, field = [")"], [], start + 1
    pairs = {"(": ")", "[": "]", "{": "}"}
    for i in range(start + 1, len(text)):
        if i in layout.objects:
            continue
        char = text[i]
        if char in pairs:
            stack.append(pairs[char])
        elif char in ")]}":
            if char != stack[-1]:
                raise Review("Mismatched textual brackets in leading parenthesis")
            stack.pop()
            if not stack:
                ranges.append(trim(text, field, i))
                end = i + 1
                while end < len(text) and text[end].isspace():
                    end += 1
                return ranges, end
        elif char in ",;" and len(stack) == 1:
            ranges.append(trim(text, field, i))
            field = i + 1
    raise Review("Unclosed leading parenthesis (not a configured exception)")


def remove_duplicate_metadata(node, seen):
    if isinstance(node, dict):
        if node.get("id"):
            if node["id"] in seen:
                del node["id"]
                if "properties" in node:
                    node["properties"] = [
                        p for p in node["properties"] if p["name"] != "athena:artifact-bindings"]
                    if not node["properties"]:
                        del node["properties"]
            else:
                seen.add(node["id"])
        for value in node.values():
            remove_duplicate_metadata(value, seen)
    elif isinstance(node, list):
        for value in node:
            remove_duplicate_metadata(value, seen)


def compact(node):
    """Semantic fields belong to their enunciation, not to title formatting IDs."""
    node = deepcopy(node)
    for item in all_nodes(node):
        item.pop("id", None)
        if "properties" in item:
            item["properties"] = [p for p in item["properties"]
                                  if p["name"] != "athena:artifact-bindings"]
            if not item["properties"]:
                del item["properties"]
    return compact_envelopes(node)


def compact_envelopes(node):
    indices = visible_children(node)
    if indices is None:
        return node
    node = {**node, "children": list(children(node))}
    for i in indices:
        node["children"][i] = compact_envelopes(node["children"][i])
    if (set(node) == {"tag", "children"} and len(children(node)) == 1
            and node["tag"] in {"concat", "with"}):
        return children(node)[0]
    return node


def nonempty_field(value):
    if value.get("type") == "rich_tree":
        return bool(Layout(value["value"]).text.strip())
    if value.get("type") == "list":
        return any(nonempty_field(v) for v in value["value"])
    return bool(value.get("value"))


@dataclass
class Plan:
    checks: list
    edits: list
    records: list
    migrated: dict


def plan_document(document, relative, config):
    attribution = set(config["attribution_fields"])
    exceptions = {case["node_id"]: case for case in config["leading_parenthesis_exceptions"]}
    reviews = {case["node_id"]: case for case in config["review_cases"]}
    checks, edits, records, expected = [], [], [], {}
    targets = []

    def walk(node, path):
        if node.get("tag") in QUOTED:
            return
        if node.get("tag") == "enunciation":
            kind = properties(node).get("kind", {}).get("value")
            if kind in KINDS:
                targets.append((path, node, kind))
        for i, child in enumerate(children(node)):
            walk(child, path + (i,))

    for i, child in enumerate(children(document)):
        if child.get("tag") == "body":
            walk(child, (i,))
    for path, node, kind in sorted(targets, key=lambda item: item[0], reverse=True):
        record = {"file": str(relative), "node_id": node.get("id"), "kind": kind}
        fields = properties(node)
        if any(nonempty_field(fields.get(k, {})) for k in ("name", "attribution", "year")):
            records.append({**record, "status": "already_structured"})
            continue
        try:
            if not node.get("id"):
                raise Review("Provable has no persistent UUID")
            if len(children(node)) != 1 or children(node)[0].get("tag") != "document":
                raise Review("Provable does not have a canonical document body")
            body = children(node)[0]
            layout = Layout(body)
            parsed = leading_fields(layout, exceptions.get(node["id"]))
            if parsed is None:
                records.append({**record, "status": "no_header"})
                continue
            ranges, end = parsed
            labels = [layout.text[a:b] for a, b in ranges]
            record["original_fields"] = labels
            if any(a == b for a, b in ranges):
                raise Review("Empty field in leading parenthesis")
            if node["id"] in reviews:
                raise Review(reviews[node["id"]]["reason"])
            names, authors, years = [], [], []
            for a, b in ranges:
                label = layout.text[a:b]
                plain = not any(a <= pos < b for pos in layout.objects)
                combined = re.fullmatch(r"(.+\S)\s+([0-9]{4}(?:s)?)", label) if plain else None
                if plain and YEAR.fullmatch(label):
                    years.append(label)
                elif plain and label in attribution:
                    authors.append(layout.slice(a, b))
                elif combined and combined[1] in attribution:
                    authors.append(layout.slice(a, a + len(combined[1])))
                    years.append(combined[2])
                else:
                    names.append(layout.slice(a, b))
            if len(years) > 1:
                raise Review("Multiple years require explicit attribution/year pairing")
            remaining, changes = layout.cut_prefix(end, path + (0,))
            seen = set(identities(remaining))
            for item in names + authors:
                remove_duplicate_metadata(item, seen)
            names = [compact(item) for item in names]
            authors = [compact(item) for item in authors]
            joined = []
            for item in names:
                if joined:
                    joined.append({"text": ", "})
                joined.append(item)
            updates = [prop("name", "rich_tree", concat(joined))]
            if authors:
                updates.append(prop("attribution", "list", [
                    {"type": "rich_tree", "value": author} for author in authors]))
            if years:
                updates.append(prop("year", "string", years[0]))
            after_ids = identities(remaining) + identities(updates)
            retired = set(identities(body)) - set(after_ids)
            if len(after_ids) != len(set(after_ids)):
                raise Review("Extraction would duplicate a persistent identity")
            changes.append({"path": list(path), "command": "update_properties",
                            "parameters": {"set": updates, "remove": []}})
            checks.append({"path": list(path), "tree": node})
            edits.extend(changes)
            expected[node["id"]] = {p["name"]: p["value"] for p in updates}
            config.setdefault("_redirects", {}).update({old: node["id"] for old in retired})
            records.append({**record, "status": "planned",
                            "name": Layout(concat(joined)).text,
                            "attribution": [Layout(a).text for a in authors],
                            "year": years[0] if years else None,
                            "exception": node["id"] in exceptions})
        except Review as error:
            records.append({**record, "status": "review", "reason": str(error)})
    return Plan(checks, edits, records, expected)


class Progress:
    def __init__(self):
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.state = ("Starting", 0, None, "")
        self.started = time.monotonic()
        self.thread = threading.Thread(target=self.render, daemon=True)

    def __enter__(self):
        self.thread.start()
        return self

    def update(self, phase, done=0, total=None, detail=""):
        with self.lock:
            self.state = (phase, done, total, detail)

    def render(self):
        while not self.stop.is_set():
            self.draw()
            self.stop.wait(0.15 if sys.stderr.isatty() else 2)

    def draw(self):
        with self.lock:
            phase, done, total, detail = self.state
        width = shutil.get_terminal_size((110, 24)).columns
        ratio = min(1, done / total) if total else 0
        bar = ("#" * int(ratio * 20)).ljust(20, "-")
        count = f"{done}/{total} {ratio:5.1%}" if total else str(done)
        elapsed = int(time.monotonic() - self.started)
        line = f"{phase} [{bar}] {count} ({elapsed}s) {detail}"
        line = line.replace("\n", " ").replace("\r", " ").replace("\x1b", "?")
        if sys.stderr.isatty():
            print("\r" + line[:max(1, width - 1)] + "\x1b[K",
                  end="", file=sys.stderr, flush=True)
        else:
            print(line, file=sys.stderr, flush=True)

    def __exit__(self, *args):
        self.stop.set()
        self.thread.join()
        self.draw()
        if sys.stderr.isatty():
            print(file=sys.stderr, flush=True)


def backup_vault(root, progress, prefix="athena-provable-fields-"):
    destination = Path(tempfile.mkdtemp(prefix=prefix, dir="/tmp"))
    backup = destination / "vault"
    print(f"Backup directory: {backup}", flush=True)
    print("Backup excludes directories: " + ", ".join(sorted(BACKUP_EXCLUDED)), flush=True)
    if destination.is_relative_to(root):
        raise ValueError("The backup directory must be outside the source vault")
    entries, directories = [], []
    progress.update("Counting backup files")

    def failed_walk(error):
        raise error

    for parent, dirs, files in os.walk(root, followlinks=False, onerror=failed_walk):
        parent = Path(parent)
        dirs[:] = [name for name in dirs if name not in BACKUP_EXCLUDED]
        directories.append(parent)
        for name in sorted(dirs + files):
            path = parent / name
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode):
                entries.append((path, 0, True))
                if name in dirs:
                    dirs.remove(name)
            elif stat.S_ISREG(mode):
                entries.append((path, path.stat().st_size, False))
            elif not stat.S_ISDIR(mode):
                raise RuntimeError(f"Cannot back up special filesystem entry: {path}")
            progress.update("Counting backup files", len(entries), detail=str(path.relative_to(root)))
    total, copied = sum(size for _, size, _ in entries), 0
    for directory in directories:
        (backup / directory.relative_to(root)).mkdir(parents=True, exist_ok=True)
    source_files = []
    for number, (source, _, link) in enumerate(entries, 1):
        relative = source.relative_to(root)
        target = backup / relative
        detail = f"{number}/{len(entries)} files: {relative}"
        progress.update("Backing up bytes", copied, total, detail)
        if link:
            target.symlink_to(os.readlink(source))
            shutil.copystat(source, target, follow_symlinks=False)
        else:
            with source.open("rb") as reader, target.open("wb") as writer:
                while chunk := reader.read(1024 * 1024):
                    writer.write(chunk)
                    copied += len(chunk)
                    progress.update("Backing up bytes", copied, total, detail)
            shutil.copystat(source, target)
            if source.suffix == ".ath" and not any(p in EXCLUDED for p in relative.parts):
                source_files.append(PurePosixPath(relative.as_posix()))
    for directory in reversed(directories):
        shutil.copystat(directory, backup / directory.relative_to(root))
    print(f"Backup complete: {len(entries)} entries, {copied} bytes (no hashes)", flush=True)
    return destination, sorted(source_files)


def require_no_athena(binary):
    processes = subprocess.run(
        ["ps", "-eo", "pid=,comm=,args="], check=True, capture_output=True, text=True)
    matches = []
    names = {"ATHENA", "ATHENA.bin", binary.name}
    for line in processes.stdout.splitlines():
        fields = line.strip().split(None, 2)
        if len(fields) >= 2 and fields[1] in names:
            matches.append(line.strip())
    if matches:
        raise RuntimeError("Close ATHENA before migration. These processes are still running:\n"
                           + "\n".join(matches) + "\nNo backup or migration was started.")


@contextmanager
def running_athena(binary, root, vault_name, destination, progress):
    """Own only the child we launch; inherit the user's normal configuration."""
    vaultfile = json.loads((root / "Vaultfile.json").read_text(encoding="utf-8"))
    arguments = [str(root), vault_name, vaultfile.get("map_path", "map.sqlite"),
                 vaultfile.get("namespace_db_path", "ns.sqlite")]
    quoted = [json.dumps(value, ensure_ascii=False) for value in arguments]
    expression = (
        "(exec-global (lambda () (let ((failure (vault-load-with-ns "
        f"(system->url {quoted[0]}) {quoted[1]} {quoted[2]} {quoted[3]}))) "
        '(unless (equal? failure "") (error failure)))))')
    log_path = destination / "athena.log"
    progress.update("Starting ATHENA")
    with log_path.open("w", encoding="utf-8") as log:
        process = subprocess.Popen(
            [str(binary), "-X", "-no-splash-screen", "-x", expression],
            cwd=Path(__file__).resolve().parents[2] / "ATHENA",
            stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        print(f"Started ATHENA PID: {process.pid}\nATHENA log: {log_path}", flush=True)
        try:
            runtime = Path(os.environ.get("XDG_RUNTIME_DIR") or "/tmp")
            deadline = time.monotonic() + 90
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError(f"ATHENA exited with code {process.returncode}; see {log_path}")
                for endpoint in runtime.glob(f"athena-audmap-{process.pid}-*/connection.json"):
                    try:
                        descriptor = json.loads(endpoint.read_text(encoding="utf-8"))
                    except (OSError, ValueError):
                        continue
                    if descriptor.get("pid") == process.pid:
                        yield endpoint
                        return
                time.sleep(0.1)
            raise RuntimeError(f"Timed out waiting for ATHENA AUDMAP startup; see {log_path}")
        finally:
            progress.update("Stopping migration ATHENA", detail=f"PID {process.pid}")
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            print(f"Stopped migration ATHENA PID: {process.pid}", flush=True)


class Session:
    def __init__(self, client):
        self.client = client

    @staticmethod
    def result(request):
        answer = request.result(timeout=300)
        if answer.status != "OK":
            raise RuntimeError(f"AUDMAP {answer.status}: {answer.data}")
        return answer.data

    @contextmanager
    def select(self, selector):
        request = self.client.resolve(selector, leaves=True)
        handles, diagnostics = self.result(request)
        try:
            if diagnostics or len(handles) != 1:
                raise RuntimeError(f"Cannot resolve {selector}: {diagnostics}, {len(handles)} handles")
            yield request.ticket, handles[0]
        finally:
            self.result(self.client.finish(request.ticket))

    def call(self, ticket, handle, command="get", parameters=None):
        request = self.client.operate(ticket, handle, command, parameters)
        try:
            return self.result(request)
        finally:
            if request.future.done():
                self.result(self.client.release(ticket, request.operation))

    def one(self, selector, command="get", parameters=None):
        with self.select(selector) as (ticket, handle):
            return self.call(ticket, handle, command, parameters)


def migrate_file(session, vault, relative, config, apply, planner=plan_document):
    selector = vault + "/filesystem" + "".join(
        "/" + json.dumps(part, ensure_ascii=False) for part in relative.parts)
    if session.one(selector, "buffers"):
        return [{"file": str(relative), "status": "open_buffer",
                 "reason": "Skipped open document; close it and rerun"}]
    with session.select(selector + "/saved") as (ticket, handle):
        snapshot = session.call(ticket, handle)["tree"]
        plan = planner(snapshot, relative, config)
        if plan.edits and apply:
            if config.get("_redirects"):
                config["_redirect_path"].write_text(
                    json.dumps({"aliases": config["_redirects"]}, indent=2) + "\n",
                    encoding="utf-8")
            if "batch" not in session.call(ticket, handle, "inspect"):
                raise RuntimeError("This ATHENA binary lacks saved-document batch editing; "
                                   "restart using the updated binary")
            if session.one(selector, "buffers"):
                return [{"file": str(relative), "status": "open_buffer",
                         "reason": "Document opened before commit; skipped"}]
            result = session.call(ticket, handle, "batch",
                                  {"checks": plan.checks, "edits": plan.edits})
            if not all(result.get(key) for key in (
                    "committed", "directory_synced", "revision_available")):
                raise RuntimeError(f"Commit or durability uncertain; do not retry blindly: {result}")
            after = session.call(ticket, handle)["tree"]
            actual = {n.get("id"): properties(n) for n in all_nodes(after)
                      if n.get("tag") == "enunciation"}
            for node_id, fields in plan.migrated.items():
                if any(actual.get(node_id, {}).get(k) != v for k, v in fields.items()):
                    raise RuntimeError(f"Saved structured fields differ: {relative}, {node_id}")
        for record in plan.records:
            if record["status"] == "planned":
                record["status"] = "migrated" if apply else "would_migrate"
        return plan.records


def validate_provable_config(config):
    if set(config["scope"]) != KINDS:
        raise ValueError("Configuration scope must be the four provable kinds")


def main(argv=None, *, planner=plan_document, migration="provable-fields",
         client_name="Provable field migration", description=__doc__,
         config_file="provable-attribution-fields.json",
         validate_config=validate_provable_config):
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--binary", type=Path, default=Path(__file__).resolve().parents[2] /
                        "ATHENA/bin/ATHENA.bin", help="ATHENA.bin to launch after backup")
    parser.add_argument("--vault", required=True, help="Vault name to load")
    parser.add_argument("--expect-root", required=True, type=Path, help="Physical vault directory to back up")
    parser.add_argument("--identity", type=Path, default=Path.home() /
                        f".config/athena-audmap/{migration}-migration.json")
    parser.add_argument("--config", type=Path,
                        default=Path(__file__).with_name(config_file) if config_file else None)
    parser.add_argument("--apply", action="store_true", help="Commit; otherwise only report proposed changes")
    args = parser.parse_args(argv)
    root = args.expect_root.expanduser().resolve(strict=True)
    if not root.is_dir():
        parser.error("--expect-root must be a vault directory")
    binary = args.binary.expanduser().resolve(strict=True)
    require_no_athena(binary)
    config = json.loads(args.config.read_text(encoding="utf-8")) if args.config else {}
    config["_root"] = root
    if validate_config:
        validate_config(config)
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "clients/python/src"))
    try:
        from athena_audmap import Client
    except ImportError as error:
        raise RuntimeError("Use a Python interpreter with the AUDMAP dependencies "
                           f"msgpack and pyzmq installed: {error}") from error

    summary = Counter()
    with Progress() as progress:
        destination, files = backup_vault(root, progress, prefix=f"athena-{migration}-")
        config["_redirect_path"] = destination / "redirects.json"
        if config.get("aliases"):
            config["_redirect_path"].write_text(
                json.dumps({"aliases": config["aliases"]}, indent=2) + "\n", encoding="utf-8")
        if "_files" in config:
            files = [f for f in files if str(f) in config["_files"]]
        report_path = destination / "migration.jsonl"
        print(f"Report: {report_path}\nMode: {'APPLY' if args.apply else 'DRY RUN'}", flush=True)
        vault = "@/vaults/" + json.dumps(args.vault, ensure_ascii=False)
        with report_path.open("w", encoding="utf-8") as report, running_athena(
                binary, root, args.vault, destination, progress) as endpoint:
            print("AUDMAP: choose Full access, then Allow (this time only) or Allow (always).",
                  flush=True)
            progress.update("Connecting / waiting for authorization")
            with Client(endpoint=endpoint, identity=str(args.identity),
                name=client_name) as client:
                session = Session(client)
                progress.update("Waiting for vault to load")
                deadline = time.monotonic() + 60
                while True:
                    try:
                        actual = Path(session.one(vault)["root"]).resolve()
                        break
                    except RuntimeError:
                        if time.monotonic() >= deadline:
                            raise RuntimeError(f"Vault did not load; see {destination / 'athena.log'}")
                        time.sleep(0.1)
                if actual != root:
                    raise RuntimeError(f"Wrong vault: requested {root}, AUDMAP returned {actual}")
                for number, relative in enumerate(files):
                    progress.update("Migrating" if args.apply else "Scanning", number, len(files), str(relative))
                    try:
                        records = migrate_file(session, vault, relative, config, args.apply, planner)
                    except Exception as error:
                        record = {"file": str(relative), "status": "error", "reason": str(error)}
                        report.write(json.dumps(record, ensure_ascii=False) + "\n")
                        report.flush()
                        raise RuntimeError(f"{relative}: {error}\nBackup: {destination / 'vault'}\n"
                                           f"Report: {report_path}") from error
                    for record in records:
                        summary[record["status"]] += 1
                        report.write(json.dumps(record, ensure_ascii=False) + "\n")
                        if record["status"] in {"review", "open_buffer"}:
                            print(f"\nREVIEW {relative} [{record.get('node_id', '-')}] "
                                  f"{record['reason']}", file=sys.stderr, flush=True)
                    report.flush()
                    summary["files"] += 1
                if apply_redirects := config.get("_redirects"):
                    # Run after extraction so references in any document can be
                    # redirected even when their target was migrated later.
                    import runpy
                    repair = runpy.run_path(str(Path(__file__).with_name("repair-enunciation-fields.py")))
                    redirect_config = {"aliases": apply_redirects, "clean_fields": False}
                    for number, relative in enumerate(files):
                        progress.update("Retargeting references", number, len(files), str(relative))
                        if not repair["mentions_alias"](root / relative, apply_redirects):
                            continue
                        records = migrate_file(session, vault, relative, redirect_config,
                                               args.apply, repair["plan_document"])
                        for record in records:
                            summary["reference_" + record["status"]] += 1
                            report.write(json.dumps(record, ensure_ascii=False) + "\n")
                        report.flush()
        progress.update("Complete", len(files), len(files))
        summary_path = destination / "summary.json"
        summary_path.write_text(json.dumps(dict(summary), indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"summary": dict(summary), "backup": str(destination / "vault"),
                      "report": str(report_path)}, ensure_ascii=False, indent=2), flush=True)
    return 2 if summary["review"] or summary["open_buffer"] else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\nInterrupted. The launched ATHENA has been stopped. Backup and the "
              "per-file report are retained. Reruns skip already structured nodes.",
              file=sys.stderr, flush=True)
        sys.exit(130)
    except (RuntimeError, ValueError, OSError) as error:
        print(f"\nERROR: {error}", file=sys.stderr, flush=True)
        sys.exit(1)
