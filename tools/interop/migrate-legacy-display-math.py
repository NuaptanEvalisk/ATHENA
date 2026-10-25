#!/usr/bin/env python3
"""One-time, offline XML v3 display-math Vault migration.

No legacy reader is installed in ATHENA. This script runs independently of the
application, makes a full /tmp backup (including .athena), rewrites a private
same-filesystem Vault candidate, validates it, then atomically exchanges roots.
Without --apply it only reports the current live-document inventory.
"""

import argparse
from collections import Counter
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import sqlite3
import stat
import subprocess
import sys
import tempfile
import uuid
import xml.etree.ElementTree as ET


OLD = frozenset(("equation", "equation*", "eqnarray", "eqnarray*"))
DIGITS = {value: index for index, value in
          enumerate(b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz~")}
SKIP_LIVE = frozenset((".backup", ".cache", ".git", ".hg", ".svn"))


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def fingerprint(value):
    return hashlib.sha256(value).hexdigest()


def children(node):
    return [item for item in node if item.tag != "x"]


def identity_inventory(root):
    return Counter((node.tag, node.get("i")) for node in root.iter()
                   if node.get("i") and node.tag in ("n", "d", "t"))


def only_child(node, tag):
    descendants = children(node)
    require(len(descendants) == 1 and descendants[0].tag == "n"
            and descendants[0].get("t") == tag,
            f"Unsupported historical {node.get('t')} shape: expected one {tag}")
    return descendants[0]


def convert_display(node):
    label = node.get("t")
    document = only_child(node, "document")
    if label in ("equation", "equation*"):
        require(len(children(document)) == 1,
                "Multi-paragraph historical equation needs separate review")
        # The DOCUMENT wrapper is not valid directly under MathBlock. Its UUID
        # and typed properties belong to a transparent CONCAT holding the same
        # sole expression; its expression subtree is never reconstructed.
        document.set("t", "concat")
        node.set("t", "math-block")
        node.set("b", "1" if label == "equation" else "0")
        return

    format_node = only_child(document, "tformat")
    table = only_child(format_node, "table")
    require(not any(x.tag == "x" for x in table),
            "Historical table carries metadata that cannot be merged")
    rows = children(table)
    require(rows and all(row.tag == "n" and row.get("t") == "row"
                         for row in rows),
            "Historical equation array has invalid rows")
    for row in rows:
        require(children(row) and all(item.get("t") == "cell" and item.tag == "n"
                                      for item in children(row)),
                "Historical array contains a non-cell member")
    # The equation environment, document wrapper and format wrapper each own
    # a persistent identity. They become MathBlock, a transparent CONCAT and
    # MathGrid respectively; this preserves the meaning of the whole-document
    # wrapper and the formatting grid for multirow arrays. An unidentifiable
    # TABLE is the only erased object; ROW/CELL identities remain untouched.
    require(not table.get("i") and not any(k not in ("t",) for k in table.attrib),
            "Historical TABLE owns independent metadata")
    format_node.remove(table)
    for row in rows:
        table.remove(row)
        format_node.append(row)
    format_node.set("t", "math-grid")
    format_node.set("k", "eqnarray")
    document.set("t", "concat")
    node.set("t", "math-block")
    node.set("b", "0")


def migrate_xml(data, location):
    if b"equation" not in data and b"eqnarray" not in data:
        return data, Counter()
    root = ET.fromstring(data)
    require(root.tag == "a" and root.get("v") == "3",
            f"{location}: expected XML v3 envelope")
    identities = identity_inventory(root)
    counted = Counter()
    for node in root.iter("n"):
        label = node.get("t")
        if label in OLD:
            try:
                convert_display(node)
            except (ValueError, RuntimeError) as error:
                raise RuntimeError(f"{location}: {label}: {error}") from error
            counted[label] += 1
    if not counted:
        return data, counted
    require(identity_inventory(root) == identities,
            f"{location}: persistent node identities changed")
    require(not any(node.get("t") in OLD for node in root.iter("n")),
            f"{location}: a legacy display structure survived")
    result = ET.tostring(root, encoding="utf-8", xml_declaration=True)
    reread = ET.fromstring(result)
    require(ET.tostring(reread, encoding="utf-8") ==
            ET.tostring(root, encoding="utf-8") and
            identity_inventory(reread) == identities,
            f"{location}: XML/UUID round-trip mismatch")
    return result, counted


def read_uint(delta, cursor):
    result, start = 0, cursor
    while cursor < len(delta) and delta[cursor] in DIGITS:
        result = (result << 6) + DIGITS[delta[cursor]]
        require(result <= 0xffffffff, "Invalid Fossil integer")
        cursor += 1
    require(cursor != start, "Invalid Fossil integer")
    return result, cursor


def fossil_checksum(data):
    total = 0
    for offset in range(0, len(data), 4):
        total = (total + int.from_bytes(
            data[offset:offset + 4].ljust(4, b"\x00"), "big")) & 0xffffffff
    return total


def fossil_apply(source, delta):
    length, cursor = read_uint(delta, 0)
    require(delta[cursor:cursor + 1] == b"\n", "Invalid Fossil header")
    cursor += 1
    output = bytearray()
    while cursor < len(delta):
        count, cursor = read_uint(delta, cursor)
        operation = delta[cursor]
        cursor += 1
        if operation == ord(";"):
            require(len(output) == length and fossil_checksum(output) == count
                    and cursor == len(delta), "Invalid Fossil checksum")
            return bytes(output)
        if operation == ord(":"):
            require(count <= len(delta) - cursor, "Truncated Fossil literal")
            output.extend(delta[cursor:cursor + count])
            cursor += count
        elif operation == ord("@"):
            offset, cursor = read_uint(delta, cursor)
            require(delta[cursor:cursor + 1] == b",", "Invalid Fossil offset")
            cursor += 1
            size = count or len(source) - offset
            require(offset <= len(source) and
                    0 <= size <= len(source) - offset, "Fossil source overflow")
            output.extend(source[offset:offset + size])
        else:
            raise RuntimeError("Unrecognized Fossil operation")
        require(len(output) <= length, "Fossil output overflow")
    raise RuntimeError("Unterminated Fossil delta")


def inspect_history(db, apply):
    require(db.execute("PRAGMA quick_check").fetchone() == ("ok",),
            "History SQLite integrity failed")
    rows = {row[0]: row for row in db.execute(
        "SELECT id,coalesce(base_id,0),encoding,payload,content_size,content_hash,path "
        "FROM versions ORDER BY id")}
    cache, changes, revisions = {}, Counter(), {}

    def reconstruct(version, seen=()):
        if version not in cache:
            require(version in rows and version not in seen and len(seen) < 64,
                    f"Invalid historical reference {version}")
            identity, base, encoding, payload, length, digest, path = rows[version]
            require(0 <= length <= 64 * 1024 * 1024, "Oversize history record")
            if encoding == 0:
                require(base == 0, "Full checkpoint has a delta parent")
                data = bytes(payload)
            else:
                require(encoding == 1 and base > 0, "Unknown history encoding")
                data = fossil_apply(reconstruct(base, seen + (version,)), bytes(payload))
            require(len(data) == length and fingerprint(data) == digest,
                    f"Invalid history checksum: {identity} {path}")
            cache[version] = data
        return cache[version]

    for version, row in rows.items():
        data = reconstruct(version)
        if not row[-1].endswith(".ath"):
            continue
        updated, found = migrate_xml(data, f"history version {version} {row[-1]}")
        changes.update(found)
        revisions[version] = updated
    if apply and changes:
        # Rebase all historical .ath versions to verified full checkpoints. The
        # bounded Vault stores hundreds of revisions, not gigabytes of history.
        # This avoids keeping Fossil deltas tied to superseded source bytes.
        with db:
            for version, updated in revisions.items():
                db.execute(
                    "UPDATE versions SET base_id=NULL,encoding=0,payload=?,"
                    "content_size=?,content_hash=?,chain_depth=0 WHERE id=?",
                    (updated, len(updated), fingerprint(updated), version))
        reread = {row[0]: row for row in db.execute(
            "SELECT id,payload,content_size,content_hash,base_id,encoding FROM versions "
            "WHERE path LIKE '%.ath'")}
        for identity, (vid, blob, size, digest, base, encoding) in reread.items():
            require(base is None and encoding == 0 and
                    len(blob) == size and fingerprint(blob) == digest and
                    blob == revisions[identity], f"History write mismatch: {identity}")
    return changes, len(revisions)


def candidate_documents(root, apply):
    matches, affected = Counter(), []
    for directory, dirs, files in os.walk(root, followlinks=False):
        dirs[:] = sorted(x for x in dirs if x not in SKIP_LIVE and x != ".athena")
        for name in sorted(files):
            if not name.endswith(".ath"):
                continue
            path = Path(directory) / name
            require(path.is_file() and not path.is_symlink(), f"Invalid source {path}")
            data = path.read_bytes()
            transformed, found = migrate_xml(data, str(path.relative_to(root)))
            matches.update(found)
            if found:
                affected.append(str(path.relative_to(root)))
                if apply:
                    temp = path.with_name(path.name + ".math-migration-tmp")
                    require(not temp.exists(), f"Unexpected temporary path: {temp}")
                    try:
                        with temp.open("xb") as writer:
                            writer.write(transformed)
                            writer.flush()
                            os.fsync(writer.fileno())
                        shutil.copystat(path, temp)
                        os.replace(temp, path)
                    finally:
                        if temp.exists():
                            temp.unlink()
    return matches, affected


def candidate_indexes(root, apply, invalidate_source_hashes=False):
    config = json.loads((root / "Vaultfile.json").read_text())
    tables = (("artifacts_path", "artifact_names", "name_tree"),
              ("enunciations_path", "entries", "keyword_tree"),
              ("bold_text_path", "entries", "keyword_tree"))
    changes = Counter()
    for field, table, column in tables:
        if not config.get(field):
            continue
        path = root / config[field]
        require(path.resolve().is_relative_to(root.resolve()), "Index escaped Vault")
        if not path.exists():
            continue
        with sqlite3.connect(path) as db:
            require(db.execute("PRAGMA quick_check").fetchone() == ("ok",),
                    f"Invalid index: {path}")
            exists = db.execute(
                "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?",
                (table,)).fetchone()
            if not exists:
                continue
            schema = {item[1] for item in db.execute(f'PRAGMA table_info("{table}")')}
            if column not in schema:
                continue
            for rowid, encoded in list(db.execute(
                    f'SELECT rowid,"{column}" FROM "{table}"')):
                if encoded is None:
                    continue
                raw = encoded.encode("utf-8") if isinstance(encoded, str) else bytes(encoded)
                if raw.startswith((b"base64-v1:", b"base64-v3:")):
                    import base64
                    payload = base64.b64decode(raw[10:], validate=True)
                    header = raw[:10]
                    wrap = lambda data: header + base64.b64encode(data)
                else:
                    payload, wrap = raw, lambda data: data
                if not payload.lstrip().startswith(b"<?xml") and not payload.lstrip().startswith(b"<a "):
                    continue
                updated, found = migrate_xml(payload, f"{field} row {rowid}")
                changes.update(found)
                if apply and found:
                    value = wrap(updated)
                    db.execute(f'UPDATE "{table}" SET "{column}"=? WHERE rowid=?',
                               (value.decode("ascii") if isinstance(encoded, str) else value, rowid))
            if apply:
                if field == "artifacts_path" and (invalidate_source_hashes or changes):
                    # The native v3 index upgrader invalidates these exact
                    # derived hashes/offsets. Reuse the same policy because
                    # bytes and storage hashes have changed in source files.
                    for column in ("semantic_hash", "storage_hash", "content_hash",
                                   "locator_contract"):
                        db.execute(f"UPDATE documents SET {column}='' WHERE {column}<>''")
                    db.execute("DELETE FROM artifact_range_cache")
                    db.execute("UPDATE artifacts SET input_hash='' WHERE input_hash<>''")
                db.commit()
                require(db.execute("PRAGMA quick_check").fetchone() == ("ok",),
                        f"Index integrity failed after modification: {path}")
    return changes


def running_athena():
    output = subprocess.check_output(["ps", "-eo", "pid=,comm="], text=True)
    return [line.strip() for line in output.splitlines()
            if len(line.split()) >= 2 and
            line.split()[1] in ("ATHENA", "ATHENA.bin")]


def file_inventory(root):
    entries = {}
    for directory, dirs, files in os.walk(root, followlinks=False):
        for name in list(dirs) + files:
            path = Path(directory) / name
            relative = path.relative_to(root).as_posix()
            info = path.lstat()
            # Directory st_size is a filesystem allocation detail; the backup
            # lives on /tmp whereas the source Vault may be on Btrfs or ext4.
            # Preserve file sizes, modes, mtimes and symlink targets, but never
            # compare directory allocation sizes across filesystems.
            entries[relative] = (info.st_mode,
                                 0 if stat.S_ISDIR(info.st_mode) else info.st_size,
                                 info.st_mtime_ns,
                                 os.readlink(path) if path.is_symlink() else "")
            if stat.S_ISLNK(info.st_mode) and name in dirs:
                dirs.remove(name)
    return entries


def copy_full(source, target):
    require(not target.exists(), f"Existing backup or candidate: {target}")
    subprocess.run(["cp", "-a", "--reflink=auto", "--", str(source), str(target)],
                   check=True)
    require(file_inventory(source) == file_inventory(target),
            "Full backup inventory differs from original")


def exchange(left, right):
    libc = ctypes.CDLL(None, use_errno=True)
    require(hasattr(libc, "renameat2"), "No Linux renameat2 available")
    result = libc.renameat2(ctypes.c_int(-100), os.fsencode(left),
                            ctypes.c_int(-100), os.fsencode(right),
                            ctypes.c_uint(2))
    if result != 0:
        raise OSError(ctypes.get_errno(), "Atomic Vault directory exchange failed")


def checkpoint_database(path):
    if not path.exists():
        return
    with sqlite3.connect(path) as connection:
        require(connection.execute("PRAGMA quick_check").fetchone() == ("ok",),
                f"Invalid copied SQLite database: {path}")
        require(connection.execute("PRAGMA wal_checkpoint(TRUNCATE)").fetchone()[0] == 0,
                f"Failed copied WAL checkpoint: {path}")


def validate_native_candidate(staged, workspace, binary):
    """Require ATHENA's strict native XML v3 codec to accept every source."""
    validation = workspace / "native-validation"
    validation.mkdir()
    version_dir = validation / "history"
    version_dir.mkdir()
    paths = []
    for directory, dirs, files in os.walk(staged, followlinks=False):
        dirs[:] = sorted(x for x in dirs if x not in SKIP_LIVE and x != ".athena")
        for name in sorted(files):
            if name.endswith(".ath"):
                paths.append(str(Path(directory) / name))
    with sqlite3.connect(staged / ".athena/document-history.sqlite") as db:
        for identity, payload in db.execute(
                "SELECT id,payload FROM versions WHERE path LIKE '%.ath'"):
            # History has been rebased to verified full snapshots; no decoder
            # for legacy Fossil content is part of the normal runtime.
            path = version_dir / f"{identity}.ath"
            path.write_bytes(payload)
            paths.append(str(path))
    manifest = validation / "paths.scm"
    manifest.write_text("(" + " ".join(json.dumps(p, ensure_ascii=False)
                            for p in paths) + ")\n")
    script = validation / "check.scm"
    script.write_text(
        '(define (verify path)\n'
        '  (let* ((bytes (string-load (string->url path)))\n'
        '         (source (native-conversion-read-document bytes))\n'
        '         (written (native-conversion-write-document source)))\n'
        '    (unless (tree? (native-conversion-read-document written))\n'
        '      (error "Native XML v3 verification failed" path))))\n'
        '(for-each verify (call-with-input-file '
        + json.dumps(str(manifest), ensure_ascii=False) + ' read))\n'
    )
    repo = Path(__file__).resolve().parents[2]
    command = [sys.executable, str(repo / "tests/scheme/runtime-test.py"),
               "--binary", str(binary),
               "--runtime", str(repo / "build_qt6/athena-guile-runtime"),
               "--resources", str(repo / "ATHENA"),
               "--script", str(script), "--timeout", "600"]
    with (validation / "runtime.log").open("w") as log:
        execution = subprocess.run(command, stdout=log,
                                   stderr=subprocess.STDOUT, timeout=660)
    require(execution.returncode == 0,
            "Native XML v3 verification failed; see " + str(validation / "runtime.log"))
    print(f"Native XML v3 verified {len(paths)} candidate documents and revisions",
          flush=True)


def run(root, apply, binary):
    require(not root.is_symlink() and (root / "Vaultfile.json").is_file(),
            "Expected a real Vault root with Vaultfile.json")
    require(not running_athena(), "Close all ATHENA processes before migration")
    require(json.loads((root / "Vaultfile.json").read_text()).get("node_model_version") == 2,
            "Only an existing XML v3 Vault can be migrated")
    if not apply:
        current, affected = candidate_documents(root, False)
        print(json.dumps({"mode": "check", "current": current, "files": affected},
                         indent=2, ensure_ascii=False, default=dict))
        return

    require(binary.is_file() and
            (Path(__file__).resolve().parents[2] /
             "build_qt6/athena-guile-runtime").is_dir(),
            "Native XML v3 verifier binary/runtime is unavailable")

    source = file_inventory(root)
    backup_dir = Path(tempfile.mkdtemp(prefix="athena-legacy-display-math-", dir="/tmp"))
    backup = backup_dir / "vault"
    print(f"Backing up entire Vault, including .athena, to {backup}", flush=True)
    copy_full(root, backup)
    require(source == file_inventory(root), "Vault changed during /tmp backup")
    print(f"Backup complete: {backup}", flush=True)
    workspace = root.parent / (f".{root.name}-legacy-display-{uuid.uuid4().hex}")
    workspace.mkdir(mode=0o700)
    staged = workspace / "vault"
    published = False
    already_migrated = False
    try:
        copy_full(root, staged)
        require(source == file_inventory(root), "Vault changed during candidate copy")
        history_file = staged / ".athena/document-history.sqlite"
        checkpoint_database(history_file)
        # No live-Vault database is ever opened in write mode.
        current, paths = candidate_documents(staged, True)
        with sqlite3.connect(history_file) as db:
            old_history, versions = inspect_history(db, True)
        indexes = candidate_indexes(staged, True, bool(current))
        if not current and not old_history and not indexes:
            print("Vault already migrated; original unchanged. Backup: " + str(backup),
                  flush=True)
            already_migrated = True
            shutil.rmtree(workspace)
            return
        # The schema is now strict: no current or historical source may retain
        # a legacy display node. A failed check leaves only candidate/backup.
        remaining, _ = candidate_documents(staged, False)
        with sqlite3.connect(history_file) as db:
            historical_remaining, _ = inspect_history(db, False)
        require(not remaining and not historical_remaining,
                "Legacy display nodes remain in staged Vault")
        validate_native_candidate(staged, workspace, binary)
        require(source == file_inventory(root),
                "Live Vault changed after staging; refusing publication")
        require(not running_athena(),
                "ATHENA started during migration; refusing publication")
        report = {"mode": "applied", "current": dict(current), "files": paths,
                  "history": dict(old_history), "history_versions": versions,
                  "indexes": dict(indexes), "backup": str(backup),
                  "previous_vault": str(staged)}
        report_path = backup_dir / "report.json"
        report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
        os.sync()
        exchange(root, staged)
        published = True
        for path in (root.parent, workspace):
            fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
        print(json.dumps(report, indent=2, ensure_ascii=False), flush=True)
    finally:
        if not published and not already_migrated:
            print(f"NOT PUBLISHED; source intact. Backup: {backup}", file=sys.stderr)
            print(f"Disconnected candidate retained at: {staged}", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expect-root", type=Path, required=True)
    parser.add_argument("--binary", type=Path,
                        default=Path(__file__).resolve().parents[2] /
                        "build_qt6/src/ATHENA.bin",
                        help="Strict native XML v3 binary for candidate verification")
    parser.add_argument("--apply", action="store_true",
                        help="Back up to /tmp and atomically publish verified conversion")
    args = parser.parse_args()
    root = args.expect_root.expanduser().resolve(strict=True)
    try:
        run(root, args.apply, args.binary.resolve())
    except (RuntimeError, OSError, ValueError, sqlite3.Error, ET.ParseError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
