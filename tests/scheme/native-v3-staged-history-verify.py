#!/usr/bin/env python3
"""Independently verify all Fossil delta history in an offline XML v3 candidate.

Both databases are opened SQLite immutable/read-only, with no checkpoint or
write. This verifier is intentionally independent of ATHENA's C++ delta reader
and is for explicitly supplied detached Vault copies; it is NOT a migration or
publication command.
"""

import argparse
import hashlib
import json
from pathlib import Path
import sqlite3


DIGITS = b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz~"
POSITIONS = {digit: index for index, digit in enumerate(DIGITS)}
MAX_OUTPUT = 64 * 1024 * 1024


def read_integer(payload, offset):
    start = offset
    result = 0
    while offset < len(payload) and payload[offset] in POSITIONS:
        result = (result << 6) | POSITIONS[payload[offset]]
        if result > (1 << 32) - 1:
            raise ValueError("Fossil integer exceeds uint32")
        offset += 1
    if offset == start:
        raise ValueError("Fossil delta has no numeric argument")
    return result, offset


def checksum(payload):
    total = 0
    for offset in range(0, len(payload), 4):
        fragment = payload[offset:offset + 4].ljust(4, b"\0")
        total = (total + int.from_bytes(fragment, "big")) & 0xFFFFFFFF
    return total


def fossil_apply(source, delta):
    expected, offset = read_integer(delta, 0)
    if expected > MAX_OUTPUT or delta[offset:offset + 1] != b"\n":
        raise ValueError("Fossil delta header is malformed or exceeds budget")
    offset += 1
    output = bytearray()
    while offset < len(delta):
        count, offset = read_integer(delta, offset)
        if offset >= len(delta):
            raise ValueError("Fossil delta ended inside an opcode")
        opcode = delta[offset]
        offset += 1
        if opcode == ord(";"):
            if offset != len(delta) or len(output) != expected or checksum(output) != count:
                raise ValueError("Fossil final length/checksum mismatch")
            return bytes(output)
        if opcode == ord(":"):
            if count > len(delta) - offset or count > expected - len(output):
                raise ValueError("Fossil literal exceeds input/output")
            output.extend(delta[offset:offset + count])
            offset += count
            continue
        if opcode == ord("@"):
            source_offset, offset = read_integer(delta, offset)
            if delta[offset:offset + 1] != b",":
                raise ValueError("Fossil copy misses comma")
            offset += 1
            if source_offset > len(source):
                raise ValueError("Fossil copy starts outside source")
            actual = count if count else len(source) - source_offset
            if actual > len(source) - source_offset or actual > expected - len(output):
                raise ValueError("Fossil copy exceeds source/output")
            output.extend(source[source_offset:source_offset + actual])
            continue
        raise ValueError("Unknown Fossil delta opcode")
    raise ValueError("Fossil delta omitted final checksum")


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def readonly_db(path):
    if not path.is_file() or path.is_symlink():
        raise ValueError(f"Offline history database missing or symlinked: {path}")
    wal = Path(str(path) + "-wal")
    if wal.exists() and wal.stat().st_size:
        raise ValueError(f"Offline history has an uncheckpointed WAL: {path}")
    db = sqlite3.connect(f"file:{path.as_posix()}?mode=ro&immutable=1", uri=True)
    if db.execute("PRAGMA quick_check").fetchone() != ("ok",):
        raise ValueError(f"Corrupt offline history SQLite: {path}")
    if db.execute("PRAGMA foreign_key_check").fetchone():
        raise ValueError(f"Broken offline history references: {path}")
    return db


def recover_all(db):
    rows = {row[0]: row for row in db.execute(
        "SELECT id, COALESCE(base_id,0), encoding, payload, content_size, "
        "content_hash, path, created_at_ms, trigger FROM versions")}
    recovered = {}
    visited = set()

    def decode(version_id, depth=0):
        if version_id in recovered:
            return recovered[version_id]
        if depth > 64 or version_id in visited:
            raise ValueError(f"History delta cycle/depth exceeded at version {version_id}")
        if version_id not in rows:
            raise ValueError(f"Missing history base version {version_id}")
        visited.add(version_id)
        _, base, encoding, payload, size, digest, *_ = rows[version_id]
        if size < 0 or size > MAX_OUTPUT:
            raise ValueError(f"History version {version_id} exceeds decoder budget")
        if encoding == 0:
            if base != 0:
                raise ValueError(f"Full checkpoint {version_id} has an unexpected base")
            restored = payload
        elif encoding == 1 and base > 0:
            restored = fossil_apply(decode(base, depth + 1), payload)
        else:
            raise ValueError(f"Invalid history encoding/base for version {version_id}")
        if len(restored) != size or sha256(restored) != digest:
            raise ValueError(f"History SHA256/length mismatch at version {version_id}")
        recovered[version_id] = restored
        visited.remove(version_id)
        return restored

    for version_id in rows:
        decode(version_id)
    return rows, recovered


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True,
                        help="Explicitly authorized, separate offline v2 Vault copy")
    parser.add_argument("--candidate", type=Path, required=True,
                        help="Disconnected XML v3 staged Vault candidate")
    args = parser.parse_args()
    source, target = args.source.resolve(), args.candidate.resolve()
    if not source.is_dir() or not target.is_dir() or source == target:
        raise ValueError("Source and candidate must be different existing offline Vaults")
    original_version = json.loads((source / "Vaultfile.json").read_text())
    new_version = json.loads((target / "Vaultfile.json").read_text())
    receipt = json.loads((target / ".athena/xml-v3-candidate-manifest.json").read_text())
    if (original_version.get("node_model_version") != 1 or
            new_version.get("node_model_version") != 2 or
            receipt.get("eligible_for_atomic_publication") is not False or
            receipt.get("hodarium_protocol_cutover_verified") is not False):
        raise ValueError("Detached candidate version or production safety gates invalid")

    original_db = readonly_db(source / ".athena/document-history.sqlite")
    candidate_db = readonly_db(target / ".athena/document-history.sqlite")
    try:
        old_rows, old_bytes = recover_all(original_db)
        new_rows, new_bytes = recover_all(candidate_db)
        if set(old_rows) != set(new_rows):
            raise ValueError("Candidate discarded or manufactured history IDs")
        source_versions = {1: 0, 2: 0}
        for version_id in old_rows:
            old, new = old_rows[version_id], new_rows[version_id]
            if old[6:9] != new[6:9]:
                raise ValueError(f"History path/time/trigger changed for {version_id}")
            if old[6].lower().endswith(".ath"):
                # Historical source versions predate the current v2 Vaultfile
                # and legitimately include both XML v1 and XML v2 snapshots.
                # The offline upgrader uses the explicit v2-compatible reader,
                # never the regular v3 reader, to preserve both lineages.
                version = next((v for v in (1, 2) if
                                f'<athena-document version="{v}"'.encode()
                                in old_bytes[version_id][:220]), None)
                if version is None:
                    raise ValueError(f"Historical v1/v2 XML envelope missing in {version_id}")
                source_versions[version] += 1
                if b'<a v="3" k="d">' not in new_bytes[version_id][:220]:
                    raise ValueError(f"Historical canonical v3 XML missing in {version_id}")
            elif old_bytes[version_id] != new_bytes[version_id]:
                raise ValueError(f"Non-document protected history changed at {version_id}")
        old_protected = original_db.execute(
            "SELECT operation,source_path,version_id FROM protected_versions ORDER BY operation").fetchall()
        new_protected = candidate_db.execute(
            "SELECT operation,source_path,version_id FROM protected_versions ORDER BY operation").fetchall()
        if old_protected != new_protected:
            raise ValueError("Protected history membership changed")
        evidence = candidate_db.execute(
            "SELECT version_id,original_payload,original_content_hash,original_size "
            "FROM xml_v3_protected_evidence").fetchall()
        for version_id, original, digest, size in evidence:
            if original != old_bytes[version_id] or len(original) != size or sha256(original) != digest:
                raise ValueError(f"Protected v2 source evidence differs for version {version_id}")
        document_count = sum(row[6].lower().endswith(".ath") for row in new_rows.values())
        if (document_count != receipt.get("history_versions") or
                len(evidence) != receipt.get("protected_history_versions")):
            raise ValueError("History rows disagree with candidate receipt")
        print("ATHENA-V3-DETACHED-HISTORY-VERIFIED: "
              f"{len(new_rows)} independent histories reconstructed, "
              f"{document_count} v2->v3 document versions, "
              f"legacy XML v1/v2 sources {source_versions}, "
              f"{len(old_protected)} protected references and "
              f"{len(evidence)} original v2 evidence payloads retained")
    finally:
        original_db.close()
        candidate_db.close()


if __name__ == "__main__":
    main()
