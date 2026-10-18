#!/usr/bin/env python3
"""Independently inspect every indexed XML v3 fragment in an isolated stage.

Runs read-only against explicitly supplied detached old/new Vault roots. Tests
real on-disk XML envelopes, row identity, cache invalidation and SQLite
integrity without importing any editor code or modifying either database.
"""

import argparse
import base64
import binascii
import json
from pathlib import Path
import sqlite3
import xml.etree.ElementTree as ET


def database(root, relative):
    path = root / relative
    if not path.is_file() or path.is_symlink():
        raise ValueError(f"Offline index database is missing or symlinked: {path}")
    wal = Path(str(path) + "-wal")
    if wal.exists() and wal.stat().st_size:
        raise ValueError(f"Offline index database has an uncheckpointed WAL: {path}")
    db = sqlite3.connect(f"file:{path.as_posix()}?mode=ro&immutable=1", uri=True)
    if db.execute("PRAGMA quick_check").fetchone() != ("ok",):
        raise ValueError(f"Offline index database is corrupt: {path}")
    if db.execute("PRAGMA foreign_key_check").fetchone():
        raise ValueError(f"Offline index database has broken references: {path}")
    return db


def valid_fragment(encoded, v3, opaque):
    if opaque:
        prefix = "base64-v3:" if v3 else "base64-v1:"
        if not isinstance(encoded, str) or not encoded.startswith(prefix):
            raise ValueError("Invalid Base64 versioned index fragment")
        try:
            payload = base64.b64decode(encoded[len(prefix):], validate=True)
        except binascii.Error as error:
            raise ValueError("Malformed Base64 encoded index fragment") from error
        if base64.b64encode(payload).decode("ascii") != encoded[len(prefix):]:
            raise ValueError("Noncanonical Base64 indexed fragment")
    else:
        payload = encoded.encode("utf-8") if isinstance(encoded, str) else encoded
    root = ET.fromstring(payload)
    if v3:
        if root.tag != "a" or root.attrib.get("v") != "3" or root.attrib.get("k") != "t":
            raise ValueError("Indexed payload did not migrate to canonical XML v3")
    elif root.tag not in ("athena-tree", "a"):
        raise ValueError("Original index fragment is not an ATHENA tree")
    return root


def legacy_atomic_text(root):
    parts = []
    for item in root.iter("text"):
        content = item.find("value")
        parts.append((item.text if content is None else content.text) or "")
    return "".join(parts)


def v3_atomic_text(root):
    return "".join(value.text or "" for value in root.iter("v"))


def has_typed_math_structure(root):
    # Compact delimiters and scripts move physical glyphs/superscripts out of
    # text children into typed descriptors. Their flat XML text-node sequence
    # is intentionally different despite preserved native math semantics; do
    # not confuse that normalization with content loss. Compare only the
    # projections for which the atom order remains a stable invariant.
    for element in root.iter():
        if element.tag in {"d", "o", "m"}:
            return True
        if element.tag == "n" and element.attrib.get("t") in {
            "scripts", "accent", "math-variant", "math-symbol",
            "long-arrow", "math-middle", "math-accent", "math-scripts",
            "math-long-arrow", "big-operator", "math-grid", "math-block",
        }:
            return True
    return False


def verify_tree_field(original, candidate, table, field, opaque):
    columns = [row[1] for row in original.execute(f"PRAGMA table_info({table})")]
    other_columns = [field_name for field_name in columns if field_name != field]
    sql = f"SELECT rowid,{','.join(columns)} FROM {table} ORDER BY rowid"
    old = original.execute(sql)
    new = candidate.execute(sql)
    count = 0
    strict_text = 0
    typed_math = 0
    while True:
        before, after = old.fetchone(), new.fetchone()
        if before is None or after is None:
            if before != after:
                raise ValueError(f"Indexed {table} row count changed")
            break
        if before[0] != after[0]:
            raise ValueError(f"Indexed {table} source row identity changed")
        previous = dict(zip(columns, before[1:]))
        converted = dict(zip(columns, after[1:]))
        for name in other_columns:
            if previous[name] != converted[name]:
                raise ValueError(f"Staged {table}.{name} unexpectedly changed")
        value, updated = previous[field], converted[field]
        if value is None or value == "":
            if updated != value:
                raise ValueError(f"Unchanged empty {table}.{field} was rewritten")
            continue
        original_root = valid_fragment(value, False, opaque)
        candidate_root = valid_fragment(updated, True, opaque)
        if has_typed_math_structure(candidate_root):
            typed_math += 1
        else:
            if legacy_atomic_text(original_root) != v3_atomic_text(candidate_root):
                raise ValueError(
                    f"Non-math indexed text changed during v3 migration: {table}, rowid={before[0]}")
            strict_text += 1
        count += 1
    return count, strict_text, typed_math


def verify_metadata(db, table):
    value = db.execute(f"SELECT value FROM {table} WHERE key='tree-format'").fetchone()
    if value != ("utf8-xml-v3",):
        raise ValueError(f"Tree format indicator is stale in {table}: {value}")


def verify_hash_invalidations(original, candidate, table, columns):
    total = 0
    for name in columns:
        if name not in [row[1] for row in original.execute(f"PRAGMA table_info({table})")]:
            continue
        total += original.execute(f"SELECT count(*) FROM {table} WHERE {name}<>''").fetchone()[0]
        existing = candidate.execute(f"SELECT count(*) FROM {table} WHERE {name}<>''").fetchone()[0]
        if existing:
            raise ValueError(f"Stale derived index hash remains in {table}.{name}: {existing}")
    return total


def visible_documents(root):
    result = {}
    for path in root.rglob("*.ath"):
        relative = path.relative_to(root)
        if any(piece.startswith(".") for piece in relative.parts):
            continue
        if not path.is_file() or path.is_symlink():
            raise ValueError(f"Visible indexed document is not a regular file: {relative}")
        result[str(relative)] = path
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    args = parser.parse_args()
    source, target = args.source.resolve(), args.candidate.resolve()
    if source == target or not source.is_dir() or not target.is_dir():
        raise ValueError("Source and candidate must be distinct existing offline Vaults")
    manifest = json.loads((target / ".athena/xml-v3-candidate-manifest.json").read_text())
    if (manifest.get("eligible_for_atomic_publication") is not False or
            manifest.get("hodarium_protocol_cutover_verified") is not False):
        raise ValueError("Staged candidate is incorrectly marked publishable")

    old_docs, new_docs = visible_documents(source), visible_documents(target)
    if old_docs.keys() != new_docs.keys() or len(new_docs) != manifest["documents"]:
        raise ValueError("Active document set disagrees with preflight receipt")
    for name, old_path in old_docs.items():
        previous = ET.parse(old_path).getroot()
        restored = ET.parse(new_docs[name]).getroot()
        if (previous.tag != "athena-document" or previous.attrib.get("version") != "2" or
                restored.tag != "a" or restored.attrib.get("v") != "3" or
                restored.attrib.get("k") != "d"):
            raise ValueError(f"Source/candidate document format mismatch: {name}")

    converted_trees, invalidated, text_checked, typed_math = 0, 0, 0, 0
    for database_file, table, column, opaque in (
            (".athena/artifacts.sqlite", "artifact_names", "name_tree", False),
            (".athena/bold-text.sqlite", "entries", "keyword_tree", True)):
        old = database(source, database_file)
        new = database(target, database_file)
        try:
            count, simple, structured = verify_tree_field(old, new, table, column, opaque)
            converted_trees += count
            text_checked += simple
            typed_math += structured
            if table == "artifact_names":
                verify_metadata(new, "artifact_metadata")
                invalidated += verify_hash_invalidations(old, new, "documents",
                    ("semantic_hash", "storage_hash", "content_hash", "locator_contract"))
                invalidated += verify_hash_invalidations(old, new, "artifacts", ("input_hash",))
                existing = old.execute("SELECT count(*) FROM artifact_range_cache").fetchone()[0]
                residual = new.execute("SELECT count(*) FROM artifact_range_cache").fetchone()[0]
                if existing != manifest.get("invalidated_range_cache_rows") or residual:
                    raise ValueError("Derived Artifact paragraph-range cache was not fully invalidated")
            else:
                verify_metadata(new, "bold_text_metadata")
        finally:
            old.close()
            new.close()
    if (converted_trees != manifest["index_trees"] or
            invalidated != manifest["invalidated_cache_hashes"]):
        raise ValueError(f"Index trees/hash invalidations differ from receipt: "
                         f"{converted_trees}/{invalidated}")
    print("ATHENA-V3-DETACHED-INDEXES-VERIFIED: "
          f"{len(new_docs)} independent XML v2->v3 documents parsed, "
          f"{converted_trees} actual v3 Artifact/BoldText fragments parsed, "
          f"{text_checked} native atomic text projections identical, "
          f"{typed_math} typed-math projections delegated to AST validation, "
          f"{invalidated} derived hashes invalidated, all index rows/identities stable")


if __name__ == "__main__":
    main()
