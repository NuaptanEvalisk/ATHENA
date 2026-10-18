#!/usr/bin/env python3
"""Exercise offline v3 history and index verifiers on disposable SQLite trees.

This tests the independent *verifiers*, not ATHENA's native migrator. Never
uses a user Vault or publishes a detached candidate.
"""

import base64
import hashlib
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile


def sha(data):
    return hashlib.sha256(data).hexdigest()


def digits(number):
    alphabet = b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz~"
    if not number:
        return b"0"
    result = bytearray()
    while number:
        result.append(alphabet[number & 63])
        number >>= 6
    return bytes(reversed(result))


def checksum(data):
    return sum(int.from_bytes(data[i:i + 4].ljust(4, b"\0"), "big")
               for i in range(0, len(data), 4)) & 0xFFFFFFFF


def delta(data):
    return digits(len(data)) + b"\n" + digits(len(data)) + b":" + data + digits(checksum(data)) + b";"


def history(path, texts, protected):
    path.parent.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect(path)
    with db:
        db.executescript("""
            CREATE TABLE versions(id INTEGER PRIMARY KEY,base_id INTEGER,
                encoding INTEGER,payload BLOB,content_size INTEGER,
                content_hash TEXT,path TEXT,created_at_ms INTEGER,trigger TEXT);
            CREATE TABLE protected_versions(operation TEXT,source_path TEXT,version_id INTEGER);
        """)
        if protected:
            db.execute("CREATE TABLE xml_v3_protected_evidence("
                       "version_id INTEGER PRIMARY KEY,original_payload BLOB,"
                       "original_content_hash TEXT,original_size INTEGER)")
        for ident, content in enumerate(texts, 1):
            payload = content if ident == 1 else delta(content)
            db.execute("INSERT INTO versions VALUES (?,?,?,?,?,?,?,?,?)", (
                ident, None if ident == 1 else 1, 0 if ident == 1 else 1,
                payload, len(content), sha(content), "Example.ath", ident * 1000,
                "Manual save"))
        db.execute("INSERT INTO protected_versions VALUES (?,?,?)",
                   ("fixture-operation", "Example.ath", 2))
    db.close()


def indexed(root, old, name_fragment, mode):
    artifacts = sqlite3.connect(root / ".athena/artifacts.sqlite")
    with artifacts:
        artifacts.executescript("""
            CREATE TABLE artifact_names(name_tree TEXT, extra TEXT);
            CREATE TABLE artifact_metadata(key TEXT PRIMARY KEY,value TEXT);
            CREATE TABLE documents(semantic_hash TEXT,storage_hash TEXT,content_hash TEXT,locator_contract TEXT);
            CREATE TABLE artifacts(input_hash TEXT);
            CREATE TABLE artifact_range_cache(path TEXT);
        """)
        artifacts.execute("INSERT INTO artifact_names VALUES (?,?)",
                          (name_fragment, "unchanged name index"))
        artifacts.execute("INSERT INTO artifact_metadata VALUES ('tree-format',?)",
                          (mode,))
        artifacts.execute("INSERT INTO documents VALUES (?,?,?,?)",
                          tuple("h" + str(i) if old else "" for i in range(4)))
        artifacts.execute("INSERT INTO artifacts VALUES (?)", ("cache" if old else "",))
        if old:
            artifacts.execute("INSERT INTO artifact_range_cache VALUES ('Example.ath')")
    artifacts.close()

    bold = sqlite3.connect(root / ".athena/bold-text.sqlite")
    with bold:
        bold.executescript("""
            CREATE TABLE entries(keyword_tree TEXT, extra TEXT);
            CREATE TABLE bold_text_metadata(key TEXT PRIMARY KEY,value TEXT);
        """)
        prefix = "base64-v1:" if old else "base64-v3:"
        bold.execute("INSERT INTO entries VALUES (?,?)",
                     (prefix + base64.b64encode(name_fragment.encode()).decode(),
                      "unchanged bold index"))
        bold.execute("INSERT INTO bold_text_metadata VALUES ('tree-format',?)", (mode,))
    bold.close()


def verifier(script, source, candidate, expected_failure=None):
    path = Path(__file__).with_name(script)
    result = subprocess.run((sys.executable, str(path), "--source", str(source),
                             "--candidate", str(candidate)), capture_output=True,
                            text=True, timeout=30, check=False)
    output = result.stdout + result.stderr
    if expected_failure is None:
        if result.returncode or "VERIFIED:" not in output:
            raise AssertionError(f"Valid offline verifier fixture rejected: {output}")
    elif result.returncode == 0 or expected_failure not in output:
        raise AssertionError(f"Corrupt fixture did not fail with {expected_failure!r}: {output}")


def main():
    with tempfile.TemporaryDirectory(prefix="athena-v3-verifiers-selftest-") as temp:
        source, target = Path(temp) / "v2", Path(temp) / "v3"
        for root, version in ((source, 1), (target, 2)):
            (root / ".athena").mkdir(parents=True)
            (root / "Vaultfile.json").write_text(json.dumps({
                "name": "isolated verifier test", "node_model_version": version}))
        old1 = (b'<athena-document version="2" text-model="utf-8">'
                b'<node tag="document"><text>first</text></node></athena-document>')
        old2 = old1.replace(b"first", b"second")
        new1 = b'<a v="3" k="d"><n t="document"><t><v>first</v></t></n></a>'
        new2 = new1.replace(b"first", b"second")
        (source / "Example.ath").write_bytes(old2)
        (target / "Example.ath").write_bytes(new2)
        history(source / ".athena/document-history.sqlite", (old1, old2), False)
        history(target / ".athena/document-history.sqlite", (new1, new2), True)
        with sqlite3.connect(target / ".athena/document-history.sqlite") as db:
            db.execute("INSERT INTO xml_v3_protected_evidence VALUES (?,?,?,?)",
                       (2, old2, sha(old2), len(old2)))
        old_name = '<athena-tree version="1"><text>Title</text></athena-tree>'
        new_name = '<a v="3" k="t"><t><v>Title</v></t></a>'
        indexed(source, True, old_name, "utf8-xml-v1")
        indexed(target, False, new_name, "utf8-xml-v3")
        manifest = {
            "documents": 1, "history_versions": 2,
            "protected_history_versions": 1,
            "index_trees": 2, "invalidated_cache_hashes": 5,
            "invalidated_range_cache_rows": 1,
            "hodarium_protocol_cutover_verified": False,
            "eligible_for_atomic_publication": False,
        }
        (target / ".athena/xml-v3-candidate-manifest.json").write_text(json.dumps(manifest))
        verifier("native-v3-staged-history-verify.py", source, target)
        verifier("native-v3-staged-index-verify.py", source, target)

        history_file = target / ".athena/document-history.sqlite"
        with sqlite3.connect(history_file) as db:
            db.execute("UPDATE versions SET payload=? WHERE id=2", (b"bad fossil delta",))
        verifier("native-v3-staged-history-verify.py", source, target,
                 "Fossil")
        with sqlite3.connect(history_file) as db:
            db.execute("UPDATE versions SET payload=? WHERE id=2", (delta(new2),))
        verifier("native-v3-staged-history-verify.py", source, target)

        bold_file = target / ".athena/bold-text.sqlite"
        with sqlite3.connect(bold_file) as db:
            db.execute("UPDATE entries SET keyword_tree='base64-v3:invalid!!'")
        verifier("native-v3-staged-index-verify.py", source, target, "Base64")
        with sqlite3.connect(bold_file) as db:
            db.execute("UPDATE entries SET keyword_tree=?",
                       ("base64-v3:" + base64.b64encode(new_name.encode()).decode(),))
        with sqlite3.connect(target / ".athena/artifacts.sqlite") as db:
            db.execute("UPDATE artifact_names SET name_tree=?",
                       (new_name.replace("Title", "Tampered"),))
        verifier("native-v3-staged-index-verify.py", source, target,
                 "Non-math indexed text changed")

    print("ATHENA-V3-STAGED-VERIFIER-SELFTEST-PASS: valid history/index, "
          "corrupt Fossil delta, invalid Base64, and changed source text")


if __name__ == "__main__":
    main()
