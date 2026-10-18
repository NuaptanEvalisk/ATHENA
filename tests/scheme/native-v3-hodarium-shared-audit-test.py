#!/usr/bin/env python3
"""Exercise disconnected v3/Hodarium CLI on isolated two-Vault shared state.

Two different local Vault roots share one group revision database. This is a real
ATHENA.bin --check subprocess, not a mock and not the user's live Notes Vault.
The CLI must count physical journals once, never write either input, and keep
cross-device publication explicitly blocked.
"""

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import select
import shutil
import sqlite3
import subprocess
import sys
import tempfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def cbor_integer(major, number):
    """RFC 8949 definite-length unsigned header used by v1 revision IDs."""
    if number < 24:
        return bytes([(major << 5) | number])
    for marker, size in ((24, 1), (25, 2), (26, 4), (27, 8)):
        if number < 1 << (8 * size):
            return bytes([(major << 5) | marker]) + number.to_bytes(size, "big")
    raise ValueError("CBOR integer exceeds the fixture limit")


def cbor(value):
    # Match nlohmann::json::to_cbor for these deliberately narrow descriptor
    # primitives. The object implementation uses std::map (sorted keys).
    if value is False:
        return b"\xf4"
    if value is True:
        return b"\xf5"
    if isinstance(value, int) and value >= 0:
        return cbor_integer(0, value)
    if isinstance(value, str):
        data = value.encode("utf-8")
        return cbor_integer(3, len(data)) + data
    if isinstance(value, (list, tuple)):
        return cbor_integer(4, len(value)) + b"".join(map(cbor, value))
    if isinstance(value, dict):
        return (cbor_integer(5, len(value)) + b"".join(
            cbor(key) + cbor(value[key]) for key in sorted(value)))
    raise TypeError(f"Unsupported offline signed revision fixture value: {value!r}")


def sealed_revision(vault, object_id, payload, parents=(), **updates):
    header = {
        "protocol": 1, "vault": vault, "object": object_id,
        "origin_member": "isolated-audit-fixture", "format": "ath-xml-v2",
        "semantic_version": 3, "path": "History.ath", "deleted": False,
        "parents": sorted(set(parents)),
    }
    header.update(updates)
    seal = hashlib.sha256(
        b"ATHENA-HODARIUM-REVISION-v1" + cbor(header) + payload
    ).hexdigest()
    return seal, header, payload


def add_revision(db, revision):
    revision_id, header, payload = revision
    db.execute("INSERT INTO revisions VALUES (?,?,?,?,?)", (
        revision_id, header["vault"], header["object"],
        json.dumps(header, sort_keys=True, separators=(",", ":")), payload))
    for parent in header["parents"]:
        db.execute("INSERT INTO parents VALUES (?,?)", (revision_id, parent))
    return revision_id


def run(binary, environment, vault, state):
    result = subprocess.run(
        [str(binary), "--upgrade-vault-xml-v3", "--check", "--hodarium-state",
         str(state), str(vault)],
        cwd=vault.parent, env=environment, capture_output=True, text=True,
        timeout=90, check=False)
    output = result.stdout + "\n" + result.stderr
    if result.returncode:
        raise AssertionError(f"XML v3 audit failed ({result.returncode}):\n{output}")
    if "Original Vault unchanged. This is a check, not an upgrade." not in output:
        raise AssertionError(f"CLI did not acknowledge a read-only preflight:\n{output}")
    if ("CROSS-DEVICE CUTOVER REQUIRED: content-addressed legacy" not in output or
            "causal evidence cannot be rewritten" not in output):
        raise AssertionError(f"Shared Hodarium group was incorrectly considered releasable:\n{output}")
    return output


def rejects(binary, environment, vault, state, reason):
    result = subprocess.run(
        [str(binary), "--upgrade-vault-xml-v3", "--check", "--hodarium-state",
         str(state), str(vault)],
        cwd=vault.parent, env=environment, capture_output=True, text=True,
        timeout=90, check=False)
    output = result.stdout + "\n" + result.stderr
    if result.returncode == 0 or reason not in output:
        raise AssertionError(
            f"Expected isolated preflight rejection {reason!r}, "
            f"got exit {result.returncode}:\n{output}")


def rejects_changed_group_journal_during_audit(binary, environment, vault,
                                                state, journal, group,
                                                remove_preexisting=False):
    """Inject an atomic directory-entry change after the audit starts.

    Linux inotify IN_OPEN on device.sqlite is a deterministic boundary: the
    C++ preflight inventories the directory before opening device.sqlite.
    This test does not need a production writer or a timing sleep and keeps
    every modification inside the disposable temporary external-state folder.
    """
    if not sys.platform.startswith("linux"):
        return
    libc = ctypes.CDLL(None, use_errno=True)
    libc.inotify_init1.restype = ctypes.c_int
    libc.inotify_add_watch.argtypes = (ctypes.c_int, ctypes.c_char_p,
                                       ctypes.c_uint32)
    fd = libc.inotify_init1(os.O_NONBLOCK | os.O_CLOEXEC)
    if fd < 0:
        raise OSError(ctypes.get_errno(), "Cannot set up isolated inotify race")
    marker = state / f"revisions-{group}-{'K' * 43}.sqlite"
    process = None
    try:
        if remove_preexisting:
            os.link(journal, marker)
        if libc.inotify_add_watch(fd, os.fsencode(state / "device.sqlite"),
                                  0x20) < 0:  # IN_OPEN
            raise OSError(ctypes.get_errno(), "Cannot watch private settings file")
        process = subprocess.Popen(
            [str(binary), "--upgrade-vault-xml-v3", "--check",
             "--hodarium-state", str(state), str(vault)],
            cwd=vault.parent, env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if not select.select([fd], [], [], 45)[0]:
            raise AssertionError("No external SQLite open observed in isolated preflight")
        os.read(fd, 8192)
        # Hardlink creation publishes one complete valid journal atomically;
        # its bytes are unchanged, so a byte-hash-only audit would wrongly
        # accept the extra recovery generation unless it inventories names.
        if remove_preexisting:
            marker.unlink()
        else:
            os.link(journal, marker)
        output, error = process.communicate(timeout=90)
        if (process.returncode == 0 or
                "External Hodarium database directory changed during immutable audit"
                not in output + error):
            raise AssertionError(
                "Changed generation journal set during scan was not blocked: "
                f"exit={process.returncode}\n{output}\n{error}")
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.communicate()
        if marker.exists():
            marker.unlink()
        os.close(fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    args = parser.parse_args()

    binary, runtime, resources = (args.binary.resolve(), args.runtime.resolve(),
                                  args.resources.resolve())
    with tempfile.TemporaryDirectory(prefix="athena-v3-hodarium-shared-audit-") as temp:
        home = Path(temp)
        vault, state = home / "vault", home / "external"
        vault.mkdir()
        state.mkdir()
        (vault / ".athena").mkdir()
        (vault / "Vaultfile.json").write_text(json.dumps({
            "name": "Disconnected shared journal fixture", "node_model_version": 1,
        }))
        (vault / "Example.ath").write_text(
            '<athena-document version="2" text-model="utf-8">'
            '<node tag="document"><node tag="body"><node tag="document" '
            'id="11111111-1111-4111-8111-111111111111">'
            '<text><value>Isolated source</value></text>'
            '</node></node></node></athena-document>')
        other_root = home / "other-vault"
        other_root.mkdir()
        (other_root / ".athena").mkdir()
        (other_root / "Vaultfile.json").write_text(json.dumps({
            "name": "Second shared group source", "node_model_version": 1,
        }))
        (other_root / "Example.ath").write_bytes((vault / "Example.ath").read_bytes())
        group = "G" * 43
        journal = state / f"revisions-{group}.sqlite"
        vault_ids = ("11111111-1111-4111-8111-111111111111",
                     "22222222-2222-4222-8222-222222222222")
        with sqlite3.connect(state / "device.sqlite") as database:
            database.execute(
                "CREATE TABLE vault_bindings("
                "hodarium TEXT,vault TEXT,root TEXT,enabled INTEGER)")
            for vault_id, bound_root in zip(vault_ids, (vault, other_root)):
                database.execute("INSERT INTO vault_bindings VALUES (?,?,?,1)",
                                 (group, vault_id, str(bound_root.resolve())))
        with sqlite3.connect(journal) as database:
            database.execute("PRAGMA foreign_keys=ON")
            database.executescript("""
                CREATE TABLE revisions(id TEXT PRIMARY KEY, vault TEXT NOT NULL,
                                       object TEXT NOT NULL, descriptor TEXT NOT NULL,
                                       payload BLOB NOT NULL);
                CREATE TABLE parents(child TEXT NOT NULL REFERENCES revisions(id),
                                     parent TEXT NOT NULL REFERENCES revisions(id),
                                     PRIMARY KEY(child,parent));
                CREATE TABLE applied(vault TEXT NOT NULL, object TEXT NOT NULL,
                                     revision TEXT NOT NULL REFERENCES revisions(id),
                                     PRIMARY KEY(vault,object));
                CREATE TABLE apply_intents(
                    operation TEXT PRIMARY KEY, vault TEXT NOT NULL,
                    object TEXT NOT NULL, target TEXT NOT NULL REFERENCES revisions(id),
                    descriptor TEXT NOT NULL, completed INTEGER NOT NULL);
                CREATE TABLE incoming(id TEXT);
                CREATE TABLE resolution_approvals(
                    revision TEXT PRIMARY KEY REFERENCES revisions(id),
                    envelope TEXT NOT NULL);
                CREATE TABLE eligible(id TEXT PRIMARY KEY REFERENCES revisions(id));
                CREATE TABLE payload_fingerprints(
                    revision TEXT PRIMARY KEY REFERENCES revisions(id),
                    hash TEXT NOT NULL);
            """)

            # Real content-addressed immutable revisions (not just empty
            # tables) cover two Vaults in one group and a child causal edge.
            source_xml = (vault / "Example.ath").read_bytes()
            object_id = "11111111-1111-4111-8111-111111111111"
            a = sealed_revision(vault_ids[0], object_id, source_xml)
            b = sealed_revision(vault_ids[0], object_id, source_xml,
                                parents=(a[0],), origin_member="second-device")
            c = sealed_revision(vault_ids[1], object_id, source_xml)
            for item in (a, b, c):
                add_revision(database, item)
                database.execute("INSERT INTO eligible VALUES (?)", (item[0],))
                database.execute("INSERT INTO payload_fingerprints VALUES (?,?)",
                                 (item[0], hashlib.sha256(item[2]).hexdigest()))
            database.execute("INSERT INTO applied VALUES (?,?,?)",
                             (vault_ids[0], object_id, b[0]))
            database.execute("INSERT INTO applied VALUES (?,?,?)",
                             (vault_ids[1], object_id, c[0]))

        # Group-level trust/decision records are stored outside revision
        # journals. A preflight receipt must cover them too: otherwise a
        # membership or approval change after stage would be invisible to a
        # revision-only fingerprint.
        generation = "H" * 43
        sidecars = [state / f"{kind}-{group}-{generation}.sqlite"
                    for kind in ("membership", "conflicts", "decisions")]
        for source in sidecars:
            with sqlite3.connect(source) as database:
                database.execute("CREATE TABLE audit_fixture(value TEXT NOT NULL)")
                database.execute("INSERT INTO audit_fixture VALUES (?)", (source.name,))

        profile = home / "profile"
        (profile / "system").mkdir(parents=True)
        (profile / "system" / "sys_state.json").write_text(json.dumps({
            "format": "athena-system-state", "version": 3,
        }))
        environment = dict(os.environ)
        environment.update({
            "HOME": str(home), "ATHENA_HOME_PATH": str(profile),
            "XDG_CONFIG_HOME": str(home / "config"),
            "XDG_CACHE_HOME": str(home / "cache"),
            "XDG_DATA_HOME": str(home / "data"),
            "ATHENA_PATH": str(resources), "QT_QPA_PLATFORM": "offscreen",
            "GUILE_AUTO_COMPILE": "0",
            "LD_LIBRARY_PATH": ":".join((str(runtime / "lib"),
                                         str(resources / "lib"),
                                         environment.get("LD_LIBRARY_PATH", ""))),
        })

        inputs = [vault / "Vaultfile.json", vault / "Example.ath",
                  state / "device.sqlite", journal, *sidecars]
        before = {path: (digest(path), path.stat().st_mtime_ns) for path in inputs}
        output = run(binary, environment, vault, state)
        if not all(marker in output for marker in (
                "1 Vault bindings", "1 enabled", "1 revision journals",
                "3 sealed revisions verified", "2 v2 document revisions",
                "1 applied", "0 pending applications")):
            raise AssertionError(f"Shared journal or binding was counted incorrectly:\n{output}")
        if {path: (digest(path), path.stat().st_mtime_ns) for path in inputs} != before:
            raise AssertionError("Read-only XML v3 preflight changed an input file")
        if any((state / (name + suffix)).exists()
               for name in ("device.sqlite", journal.name)
               for suffix in ("-wal", "-shm")):
            raise AssertionError("Read-only audit created an external SQLite journal")

        # Disabled bindings still matter to causality. Turning off one of two
        # devices may change the active count, not the set of archived Vaults.
        with sqlite3.connect(state / "device.sqlite") as database:
            database.execute("UPDATE vault_bindings SET enabled=0 WHERE vault=?",
                             (vault_ids[1],))
        output = run(binary, environment, vault, state)
        if not all(marker in output for marker in (
                "1 Vault bindings", "1 enabled", "1 revision journals")):
            raise AssertionError(f"Disabled binding was dropped or journal duplicated:\n{output}")
        second_disabled = run(binary, environment, other_root, state)
        if not all(marker in second_disabled for marker in (
                "1 Vault bindings", "0 enabled", "1 revision journals",
                "1 v2 document revisions")):
            raise AssertionError("Disabled Vault binding lost its signed history")

        # Exercise the actual second phase as well. A disconnected candidate
        # may be staged from this private source, but the shared group must
        # survive as an explicit cutover blocker in the signed-off manifest.
        after_disable = {path: (digest(path), path.stat().st_mtime_ns)
                         for path in inputs}
        candidate = home / "v3-candidate"
        staged = subprocess.run(
            [str(binary), "--upgrade-vault-xml-v3", "--stage", str(candidate),
             "--hodarium-state", str(state), str(vault)],
            cwd=home, env=environment, capture_output=True, text=True,
            timeout=90, check=False)
        if staged.returncode:
            raise AssertionError(
                f"Disconnected v3 candidate failed ({staged.returncode}):\n"
                + staged.stdout + "\n" + staged.stderr)
        manifest_path = candidate / ".athena" / "xml-v3-candidate-manifest.json"
        manifest = json.loads(manifest_path.read_text())
        if (manifest["source_node_model_version"] != 1 or
                manifest["candidate_node_model_version"] != 2 or
                manifest["hodarium_existing_bindings"] != 1 or
                not manifest["hodarium_coordinated_cutover_required"] or
                not manifest["hodarium_external_state_audited"] or
                manifest["hodarium_protocol_cutover_verified"] or
                manifest["eligible_for_atomic_publication"]):
            raise AssertionError(f"Candidate publication safety gates changed: {manifest}")
        if json.loads((candidate / "Vaultfile.json").read_text())["node_model_version"] != 2:
            raise AssertionError("Staged candidate has the wrong node model")
        if {path: (digest(path), path.stat().st_mtime_ns)
                for path in inputs} != after_disable:
            raise AssertionError("Staging changed source Vault or external Hodarium input")

        # A valid detached migration result is NOT an authorized runtime
        # Vault. Exercise the real Scheme->C++ vault_load boundary against
        # this very staged fixture. This must not create a normal workspace or
        # rewrite its version marker; use a second isolated application HOME.
        before_candidate = {
            path: (digest(path), path.stat().st_mtime_ns)
            for path in (candidate / "Vaultfile.json", manifest_path,
                         candidate / "Example.ath")
        }
        gate_environment = dict(environment)
        gate_environment["ATHENA_V3_CANDIDATE_ROOT"] = str(candidate)
        gate = subprocess.run(
            [sys.executable, str(Path(__file__).with_name("runtime-test.py")),
             "--binary", str(binary), "--runtime", str(runtime),
             "--resources", str(resources), "--timeout", "90", "--script",
             str(Path(__file__).with_name(
                 "native-v3-disconnected-candidate-runtime-test.scm"))],
            cwd=home, env=gate_environment, capture_output=True, text=True,
            timeout=105, check=False)
        if gate.returncode or "ATHENA-SCHEME-RUNTIME-PASS:" not in gate.stdout:
            raise AssertionError(
                "Disconnected XML v3 candidate was not safely rejected by the "
                "real runtime:\n" + gate.stdout + gate.stderr)
        if {path: (digest(path), path.stat().st_mtime_ns)
                for path in before_candidate} != before_candidate:
            raise AssertionError("Normal Vault open refusal modified staged source")

        previous_external_digest = manifest["hodarium_external_state_fingerprint"]
        membership_stat = sidecars[0].stat()
        membership_hash = digest(sidecars[0])
        with sqlite3.connect(sidecars[0]) as database:
            database.execute("UPDATE audit_fixture SET value='changed trust generation'")
        os.utime(sidecars[0], ns=(membership_stat.st_atime_ns,
                                 membership_stat.st_mtime_ns))
        if (sidecars[0].stat().st_size != membership_stat.st_size or
                sidecars[0].stat().st_mtime_ns != membership_stat.st_mtime_ns or
                digest(sidecars[0]) == membership_hash):
            raise AssertionError("The same-size, restored-mtime audit mutation was invalid")
        new_candidate = home / "v3-sidecar-changed"
        second_stage = subprocess.run(
            [str(binary), "--upgrade-vault-xml-v3", "--stage", str(new_candidate),
             "--hodarium-state", str(state), str(vault)],
            cwd=home, env=environment, capture_output=True, text=True,
            timeout=90, check=False)
        if second_stage.returncode:
            raise AssertionError("Changed external membership fixture failed stage:\n"
                                 + second_stage.stdout + second_stage.stderr)
        new_manifest = json.loads((new_candidate / ".athena" /
                                  "xml-v3-candidate-manifest.json").read_text())
        if (new_manifest["hodarium_external_state_fingerprint"] == previous_external_digest or
                new_manifest["eligible_for_atomic_publication"] or
                new_manifest["hodarium_protocol_cutover_verified"]):
            raise AssertionError("Membership change escaped external snapshot fingerprint")

        member_writer = sqlite3.connect(sidecars[0])
        try:
            member_writer.execute("PRAGMA journal_mode=WAL")
            member_writer.execute("INSERT INTO audit_fixture VALUES ('uncheckpointed')")
            member_writer.commit()
            rejects(binary, environment, vault, state, "uncheckpointed WAL")
            member_writer.execute("DELETE FROM audit_fixture WHERE value='uncheckpointed'")
            member_writer.commit()
        finally:
            member_writer.close()
        assert "3 sealed revisions verified" in run(binary, environment, vault, state)

        # client_settings stores canonical paths, never symlink aliases.
        # A fabricated alias in the external SQLite database must not cause
        # this very same bound source to be missed by a lexical comparison.
        alias = home / "vault-alias"
        alias.symlink_to(vault, target_is_directory=True)
        with sqlite3.connect(state / "device.sqlite") as database:
            database.execute("UPDATE vault_bindings SET root=? WHERE vault=?",
                             (str(alias), vault_ids[0]))
        rejects(binary, environment, vault, state,
                "Hodarium Vault binding aliases this source root")
        with sqlite3.connect(state / "device.sqlite") as database:
            database.execute("UPDATE vault_bindings SET root=? WHERE vault=?",
                             (str(vault.resolve()), vault_ids[0]))

        # Re-enable the second binding and verify the real client_settings
        # topology: one distinct local root per Vault, same group journal.
        with sqlite3.connect(state / "device.sqlite") as database:
            database.execute("UPDATE vault_bindings SET enabled=1 WHERE vault=?",
                             (vault_ids[1],))
        first = run(binary, environment, vault, state)
        second = run(binary, environment, other_root, state)
        if not all(part in first for part in (
                "1 Vault bindings", "1 enabled", "1 revision journals",
                "3 sealed revisions verified", "2 v2 document revisions", "1 applied")):
            raise AssertionError(f"First Vault attribution was incorrect:\n{first}")
        if not all(part in second for part in (
                "1 Vault bindings", "1 enabled", "1 revision journals",
                "3 sealed revisions verified", "1 v2 document revisions", "1 applied")):
            raise AssertionError(f"Second Vault attribution was incorrect:\n{second}")

        rejects_changed_group_journal_during_audit(
            binary, environment, vault, state, journal, group)
        rejects_changed_group_journal_during_audit(
            binary, environment, vault, state, journal, group,
            remove_preexisting=True)
        assert "3 sealed revisions verified" in run(binary, environment, vault, state)

        # Physical revision proof is group-scoped. While only one Vault is
        # being staged, a malformed old-document revision belonging to its
        # sibling Vault must still block the shared group's cutover audit.
        # A mere digest/parent check is insufficient if the other payload's
        # path or document model is not applicable by the old runtime.
        sibling_unsafe = sealed_revision(
            vault_ids[1], object_id, source_xml, path="../SiblingOutside.ath")
        with sqlite3.connect(journal) as database:
            add_revision(database, sibling_unsafe)
        rejects(binary, environment, vault, state,
                "Invalid Hodarium document application path")
        with sqlite3.connect(journal) as database:
            database.execute("DELETE FROM revisions WHERE id=?", (sibling_unsafe[0],))

        # Membership recovery retains the old generation's immutable journal.
        # Its filename has an exact 43-character generation ID. Both physical
        # journals must be audited, not reduced to one by logical Vault UUID.
        old_generation = state / f"revisions-{group}-{'J' * 43}.sqlite"
        shutil.copy2(journal, old_generation)
        retained = run(binary, environment, vault, state)
        if not all(marker in retained for marker in (
                "1 Vault bindings", "2 revision journals",
                "6 sealed revisions verified", "4 v2 document revisions")):
            raise AssertionError(f"Old recovery-generation revision DAG was skipped:\n{retained}")
        old_generation.unlink()

        malformed_generation = state / f"revisions-{group}-short.sqlite"
        shutil.copy2(journal, malformed_generation)
        rejects(binary, environment, vault, state,
                "Malformed external Hodarium revision generation journal name")
        malformed_generation.unlink()

        malformed_sidecar = state / f"conflicts-{group}-short.sqlite"
        shutil.copy2(sidecars[1], malformed_sidecar)
        rejects(binary, environment, vault, state,
                "Malformed external Hodarium group sidecar generation")
        malformed_sidecar.unlink()

        # device.sqlite's production schema forbids one root under two Vault
        # IDs, because that would let two distinct causal identities describe
        # the same on-disk object. The audit must enforce the invariant even
        # against an externally modified SQLite schema lacking UNIQUE(root).
        with sqlite3.connect(state / "device.sqlite") as database:
            database.execute("UPDATE vault_bindings SET root=? WHERE vault=?",
                             (str(vault.resolve()), vault_ids[1]))
        rejects(binary, environment, vault, state,
                "Duplicate external Hodarium Vault root binding")
        with sqlite3.connect(state / "device.sqlite") as database:
            database.execute("UPDATE vault_bindings SET root=? WHERE vault=?",
                             (str(other_root.resolve()), vault_ids[1]))

        # A single Hodarium journal also stores *resource* and logical-object
        # apply intents. A valid resource style path ends in .ts, not .ath;
        # imposing the document-only validator on all intent source paths
        # would falsely block a legitimate old-epoch recovery.
        style = sealed_revision(
            vault_ids[0], "99999999-9999-4999-8999-999999999999",
            b"isolated style fixture", format="ath-resource-style-v1",
            semantic_version=1, path="styles/fixture.ts")
        style_intent = {"revision": style[0], "expected": None,
                        "source_path": "styles/fixture.ts", "source_fingerprint": None,
                        "protected_history_version": 0}
        with sqlite3.connect(journal) as database:
            add_revision(database, style)
            database.execute("INSERT INTO eligible VALUES (?)", (style[0],))
            database.execute("INSERT INTO apply_intents VALUES (?,?,?,?,?,0)",
                             ("resource-style-apply", vault_ids[0], style[1]["object"],
                              style[0], json.dumps(style_intent, sort_keys=True)))
        resource_result = run(binary, environment, vault, state)
        if not all(part in resource_result for part in (
                "4 sealed revisions verified", "2 v2 document revisions",
                "1 pending applications")):
            raise AssertionError("Legitimate resource intent was misclassified as a document")
        with sqlite3.connect(journal) as database:
            database.execute("DELETE FROM apply_intents WHERE operation='resource-style-apply'")
            database.execute("DELETE FROM eligible WHERE id=?", (style[0],))
            database.execute("DELETE FROM revisions WHERE id=?", (style[0],))

        # Valid hashes and format labels are not sufficient for resource
        # revisions: a .ts style must not carry an .ath document path or live
        # under a hidden/internal directory, nor use an arbitrary identity.
        for bad_path, bad_object in (
                ("styles/invalid.ath", style[1]["object"]),
                (".athena/Secret.ts", style[1]["object"]),
                ("styles/valid.ts", "not-a-canonical-uuid")):
            invalid_resource = sealed_revision(
                vault_ids[0], bad_object, b"isolated style fixture",
                format="ath-resource-style-v1", semantic_version=1,
                path=bad_path)
            with sqlite3.connect(journal) as database:
                add_revision(database, invalid_resource)
            rejects(binary, environment, vault, state,
                    "Invalid Hodarium resource revision path, format or identity")
            with sqlite3.connect(journal) as database:
                database.execute("DELETE FROM revisions WHERE id=?",
                                 (invalid_resource[0],))

        # Logical namespace revisions do not reference arbitrary .json paths:
        # the old runtime derives a stable, object-addressed history path.
        # The same invariant must hold for offline replays and recovered
        # pending intents before any new format epoch could be negotiated.
        logical_object = "namespace-fixture-object"
        logical_path = ("Hodarium logical records/" +
                        hashlib.sha256(logical_object.encode()).hexdigest() + ".json")
        logical = sealed_revision(
            vault_ids[0], logical_object, b"{}", format="athena-namespace-v1",
            semantic_version=1, path=logical_path)
        with sqlite3.connect(journal) as database:
            add_revision(database, logical)
        assert "4 sealed revisions verified" in run(binary, environment, vault, state)
        with sqlite3.connect(journal) as database:
            database.execute("DELETE FROM revisions WHERE id=?", (logical[0],))

        for bad_path in ("../Namespace.json", "Other Directory/Namespace.json"):
            wrong_logical = sealed_revision(
                vault_ids[0], logical_object, b"{}",
                format="athena-namespace-v1", semantic_version=1, path=bad_path)
            with sqlite3.connect(journal) as database:
                add_revision(database, wrong_logical)
            rejects(binary, environment, vault, state,
                    "Invalid Hodarium logical revision model or path")
            with sqlite3.connect(journal) as database:
                database.execute("DELETE FROM revisions WHERE id=?", (wrong_logical[0],))

        derived_payload = b"isolated derived artifact range payload"
        derived_object = "range-fixture/" + hashlib.sha256(derived_payload).hexdigest()
        derived_path = ("Hodarium logical records/" +
                        hashlib.sha256(derived_object.encode()).hexdigest() + ".json")
        derived = sealed_revision(
            vault_ids[0], derived_object, derived_payload,
            format="athena-artifact-range-v1", semantic_version=1,
            path=derived_path)
        with sqlite3.connect(journal) as database:
            add_revision(database, derived)
        assert "4 sealed revisions verified" in run(binary, environment, vault, state)
        with sqlite3.connect(journal) as database:
            database.execute("DELETE FROM revisions WHERE id=?", (derived[0],))

        for wrong_object, deleted, actual_payload in (
                ("range-fixture/" + "0" * 64, False, derived_payload),
                ("range-fixture/" + hashlib.sha256(b"").hexdigest(), True, b"")):
            name = ("Hodarium logical records/" +
                    hashlib.sha256(wrong_object.encode()).hexdigest() + ".json")
            invalid_derived = sealed_revision(
                vault_ids[0], wrong_object, actual_payload,
                format="athena-artifact-range-v1", semantic_version=1,
                path=name, deleted=deleted)
            with sqlite3.connect(journal) as database:
                add_revision(database, invalid_derived)
            rejects(binary, environment, vault, state,
                    "Derived Hodarium result")
            with sqlite3.connect(journal) as database:
                database.execute("DELETE FROM revisions WHERE id=?", (invalid_derived[0],))

        bad_rejections_object = "wrong-rejections-object"
        rejection_path = ("Hodarium logical records/" +
                          hashlib.sha256(bad_rejections_object.encode()).hexdigest() + ".json")
        wrong_rejection = sealed_revision(
            vault_ids[0], bad_rejections_object, b"{}",
            format="athena-artifact-rejections-v1", semantic_version=1,
            path=rejection_path)
        with sqlite3.connect(journal) as database:
            add_revision(database, wrong_rejection)
        rejects(binary, environment, vault, state,
                "Invalid Hodarium logical rejection object identity")
        with sqlite3.connect(journal) as database:
            database.execute("DELETE FROM revisions WHERE id=?", (wrong_rejection[0],))

        # Old v1 journals only understand documented document, resource and
        # logical-revision formats. A valid hash for an unknown *non-document*
        # format does not make a future producer's bytes safely replayable.
        for alien_format, alien_version in (("athena-future-plugin-v9", 1),
                                             ("ath-resource-style-v1", 2)):
            alien = sealed_revision(
                vault_ids[0], "77777777-7777-4777-8777-777777777777",
                b"opaque future-format bytes", format=alien_format,
                semantic_version=alien_version, path="styles/old.ts")
            with sqlite3.connect(journal) as database:
                add_revision(database, alien)
            rejects(binary, environment, vault, state,
                    "Unsupported Hodarium revision format in legacy epoch"
                    if alien_format == "athena-future-plugin-v9" else
                    "Unsupported Hodarium non-document semantic version")
            with sqlite3.connect(journal) as database:
                database.execute("DELETE FROM revisions WHERE id=?", (alien[0],))

        # Every resource format has an established path-kind and canonical
        # UUID contract in the runtime. A style .ts revision cannot safely
        # claim it owns a .png or `.ath` source even if its SHA-256 matches.
        for wrong_format, wrong_path, wrong_object in (
                ("ath-resource-style-v1", "assets/Incorrect.png",
                 "99999999-9999-4999-8999-999999999999"),
                ("ath-resource-style-v1", "styles/fixture.ts", "not-a-uuid"),
                ("ath-resource-blob-v1", "Documents/Source.ath",
                 "99999999-9999-4999-8999-999999999999")):
            resource_bad = sealed_revision(
                vault_ids[0], wrong_object, b"fixture", format=wrong_format,
                semantic_version=1, path=wrong_path)
            with sqlite3.connect(journal) as database:
                add_revision(database, resource_bad)
            rejects(binary, environment, vault, state,
                    "Invalid Hodarium resource revision path, format or identity")
            with sqlite3.connect(journal) as database:
                database.execute("DELETE FROM revisions WHERE id=?", (resource_bad[0],))

        # The fixture's revision IDs were recomputed using the real signed v1
        # domain/CBOR contract and accepted above. Now alter only *mutable*
        # SQLite index state without changing a valid signed revision. FK
        # integrity still succeeds, but immutable migration must reject these
        # different forms of broken causal history.
        with sqlite3.connect(journal) as database:
            database.execute("DELETE FROM parents WHERE child=? AND parent=?",
                             (b[0], a[0]))
        rejects(binary, environment, vault, state, "causal parent index disagrees")
        with sqlite3.connect(journal) as database:
            database.execute("INSERT INTO parents VALUES (?,?)", (b[0], a[0]))
        assert "3 sealed revisions verified" in run(binary, environment, vault, state)

        with sqlite3.connect(journal) as database:
            database.execute("INSERT INTO parents VALUES (?,?)", (a[0], c[0]))
        rejects(binary, environment, vault, state, "causal parent index disagrees")
        with sqlite3.connect(journal) as database:
            database.execute("DELETE FROM parents WHERE child=? AND parent=?",
                             (a[0], c[0]))

        # Hashes and the stored parent index can *both* agree while the DAG
        # connects an entirely different Vault/object. A genuine revision
        # store rejects such a parent at ingestion; offline audit must do so
        # as well before accepting old causal evidence for a new epoch.
        cross_scope = sealed_revision(
            vault_ids[0], object_id, source_xml,
            parents=(c[0],), origin_member="invalid-cross-vault-parent")
        with sqlite3.connect(journal) as database:
            add_revision(database, cross_scope)
        rejects(binary, environment, vault, state,
                "Hodarium causal parent crosses a Vault/object boundary")
        with sqlite3.connect(journal) as database:
            database.execute("DELETE FROM parents WHERE child=?", (cross_scope[0],))
            database.execute("DELETE FROM revisions WHERE id=?", (cross_scope[0],))

        with sqlite3.connect(journal) as database:
            database.execute("UPDATE applied SET revision=? WHERE vault=?",
                             (c[0], vault_ids[0]))
        rejects(binary, environment, vault, state, "Applied Hodarium revision belongs")
        with sqlite3.connect(journal) as database:
            database.execute("UPDATE applied SET revision=? WHERE vault=?",
                             (b[0], vault_ids[0]))
        assert "3 sealed revisions verified" in run(binary, environment, vault, state)

        # The descriptor's signed vault/object also has a separate mutable
        # copy in the revisions table for SQL indexes. FK checks do not protect
        # either column, and a valid digest must not hide index corruption.
        with sqlite3.connect(journal) as database:
            database.execute("UPDATE revisions SET vault=? WHERE id=?",
                             (vault_ids[1], b[0]))
        rejects(binary, environment, vault, state,
                "revision Vault/object index disagrees with sealed descriptor")
        with sqlite3.connect(journal) as database:
            database.execute("UPDATE revisions SET vault=? WHERE id=?",
                             (vault_ids[0], b[0]))
            database.execute("UPDATE revisions SET object=? WHERE id=?",
                             ("30000000-0000-4000-8000-000000000003", b[0]))
        rejects(binary, environment, vault, state,
                "revision Vault/object index disagrees with sealed descriptor")
        with sqlite3.connect(journal) as database:
            database.execute("UPDATE revisions SET object=? WHERE id=?",
                             (object_id, b[0]))
        assert "3 sealed revisions verified" in run(binary, environment, vault, state)

        # A valid signed revision can carry an invalid separately cached SHA256.
        # Runtime source-preimage checks use that cache; corrupt it without
        # touching the sealed revision, then insist offline audit detects it.
        with sqlite3.connect(journal) as database:
            database.execute("UPDATE payload_fingerprints SET hash=? WHERE revision=?",
                             ("0" * 64, b[0]))
        rejects(binary, environment, vault, state,
                "cached Hodarium revision payload fingerprint disagrees")
        with sqlite3.connect(journal) as database:
            database.execute("UPDATE payload_fingerprints SET hash=? WHERE revision=?",
                             (hashlib.sha256(b[2]).hexdigest(), b[0]))
        assert "3 sealed revisions verified" in run(binary, environment, vault, state)

        # Eligibility is separate from the signed DAG. A descendant cannot
        # be eligible if its ancestor is not eligible: violating the gate is
        # invisible to SQLite FK checks and to digest verification.
        with sqlite3.connect(journal) as database:
            database.execute("DELETE FROM eligible WHERE id=?", (a[0],))
        rejects(binary, environment, vault, state,
                "eligible revision bypasses its causal or conflict approval gate")
        with sqlite3.connect(journal) as database:
            database.execute("INSERT INTO eligible VALUES (?)", (a[0],))

        # A forged deletion of an applied head's eligibility is invisible to
        # both the content hash and the parents-index comparison. Replaying
        # the same old head after cutover cannot rely on such an incomplete
        # conflict/causality record.
        with sqlite3.connect(journal) as database:
            database.execute("DELETE FROM eligible WHERE id=?", (c[0],))
        rejects(binary, environment, vault, state,
                "Applied Hodarium revision has lost its eligibility record")
        with sqlite3.connect(journal) as database:
            database.execute("INSERT INTO eligible VALUES (?)", (c[0],))

        # Approval records are only meaningful for >=2-parent conflict merge
        # revisions. Attaching one to a linear revision misrepresents a remote
        # conflict decision and must be rejected rather than merely counted.
        with sqlite3.connect(journal) as database:
            database.execute("INSERT INTO resolution_approvals VALUES (?,?)",
                             (b[0], "fabricated"))
        rejects(binary, environment, vault, state,
                "Hodarium conflict approval has no merge revision")
        with sqlite3.connect(journal) as database:
            database.execute("DELETE FROM resolution_approvals WHERE revision=?",
                             (b[0],))

        # Apply intents carry both an index target and a descriptor target,
        # and both must refer to the same Vault/object. A target revision
        # cannot be silently installed into a different source object.
        intent = {"revision": b[0], "expected": a[0],
                  "source_path": "History.ath", "source_fingerprint": None,
                  "protected_history_version": 0}
        with sqlite3.connect(journal) as database:
            database.execute("INSERT INTO apply_intents VALUES (?,?,?,?,?,0)",
                             ("fixture-apply", vault_ids[1], object_id, b[0],
                              json.dumps(intent, sort_keys=True)))
        rejects(binary, environment, vault, state,
                "application intent does not belong to its target revision")
        with sqlite3.connect(journal) as database:
            database.execute("UPDATE apply_intents SET vault=? WHERE operation=?",
                             (vault_ids[0], "fixture-apply"))
            bad_descriptor = dict(intent, revision=c[0])
            database.execute("UPDATE apply_intents SET descriptor=? WHERE operation=?",
                             (json.dumps(bad_descriptor, sort_keys=True), "fixture-apply"))
        rejects(binary, environment, vault, state,
                "application intent does not belong to its target revision")
        with sqlite3.connect(journal) as database:
            database.execute("DELETE FROM apply_intents WHERE operation=?", ("fixture-apply",))
        assert "3 sealed revisions verified" in run(binary, environment, vault, state)

        # A pending apply target is not necessarily an already-applied head.
        # Model a separate valid, un-applied root to prove that losing *only*
        # the intent target's eligibility is caught by the intent-specific
        # invariant rather than by the applied-head check.
        pending_root = sealed_revision(
            vault_ids[0], object_id, source_xml,
            parents=(b[0],),
            origin_member="unapplied-target-fixture")
        pending_intent = dict(intent, revision=pending_root[0], expected=b[0])
        with sqlite3.connect(journal) as database:
            add_revision(database, pending_root)
            database.execute("INSERT INTO eligible VALUES (?)", (pending_root[0],))
            database.execute("INSERT INTO apply_intents VALUES (?,?,?,?,?,0)",
                             ("pending-unapplied", vault_ids[0], object_id,
                              pending_root[0], json.dumps(pending_intent, sort_keys=True)))
            database.execute("DELETE FROM eligible WHERE id=?", (pending_root[0],))
        rejects(binary, environment, vault, state,
                "Hodarium application intent target has lost its eligibility record")
        with sqlite3.connect(journal) as database:
            database.execute("INSERT INTO eligible VALUES (?)", (pending_root[0],))
            misleading = dict(pending_intent, revision=c[0])
            encoded = json.dumps(misleading, sort_keys=True, separators=(",", ":"))
            # SQL reads the first key (the correct target), while a generic
            # JSON parser reads the final one (a different revision).
            duplicate = ("{\"revision\":" + json.dumps(pending_root[0]) +
                         "," + encoded[1:])
            database.execute("UPDATE apply_intents SET descriptor=? WHERE operation=?",
                             (duplicate, "pending-unapplied"))
            assert database.execute(
                "SELECT json_extract(descriptor,'$.revision') FROM apply_intents "
                "WHERE operation='pending-unapplied'").fetchone()[0] == pending_root[0]
        rejects(binary, environment, vault, state,
                "Duplicate Hodarium revision descriptor field")
        with sqlite3.connect(journal) as database:
            malicious_source = dict(pending_intent, source_path="../Outside.ath")
            database.execute("UPDATE apply_intents SET descriptor=? WHERE operation=?",
                             (json.dumps(malicious_source, sort_keys=True),
                              "pending-unapplied"))
        rejects(binary, environment, vault, state,
                "Invalid Hodarium document application path")
        with sqlite3.connect(journal) as database:
            impossible_base = dict(pending_intent, expected=c[0])
            database.execute("UPDATE apply_intents SET descriptor=? WHERE operation=?",
                             (json.dumps(impossible_base, sort_keys=True), "pending-unapplied"))
        rejects(binary, environment, vault, state,
                "Hodarium application intent base does not match applied causal state")
        with sqlite3.connect(journal) as database:
            database.execute("UPDATE apply_intents SET descriptor=? WHERE operation=?",
                             (json.dumps(pending_intent, sort_keys=True), "pending-unapplied"))
        assert "4 sealed revisions verified" in run(binary, environment, vault, state)
        with sqlite3.connect(journal) as database:
            database.execute("DELETE FROM apply_intents WHERE operation=?", ("pending-unapplied",))
            database.execute("DELETE FROM eligible WHERE id=?", (pending_root[0],))
            database.execute("DELETE FROM parents WHERE child=?", (pending_root[0],))
            database.execute("DELETE FROM revisions WHERE id=?", (pending_root[0],))
        assert "3 sealed revisions verified" in run(binary, environment, vault, state)

        # Signed hash remains valid even if the declared `ath-xml-v2` format
        # contains canonical XML v3 bytes. Strict document codec must reject.
        labelled_wrong = sealed_revision(
            vault_ids[0], object_id,
            b'<a v="3" k="d"><n t="document"><t><v>v3</v></t></n></a>',
            path="WrongEnvelope.ath")
        with sqlite3.connect(journal) as database:
            add_revision(database, labelled_wrong)
        rejects(binary, environment, vault, state, "requires an XML v2 envelope")
        with sqlite3.connect(journal) as database:
            database.execute("DELETE FROM revisions WHERE id=?", (labelled_wrong[0],))

        # A separately signed 'ath-xml-v3' revision also cannot be ignored
        # during the old-epoch inventory: it means another peer already used
        # an unnegotiated document format, and the group is no longer a pure
        # legacy-v2 revision DAG ready for offline migration.
        premature_v3 = sealed_revision(
            vault_ids[0], object_id,
            b'<a v="3" k="d"><n t="document"><t><v>v3</v></t></n></a>',
            format="ath-xml-v3", semantic_version=4,
            path="PrematureV3.ath")
        with sqlite3.connect(journal) as database:
            add_revision(database, premature_v3)
        rejects(binary, environment, vault, state,
                "unsupported document format inside the legacy Hodarium revision epoch")
        with sqlite3.connect(journal) as database:
            database.execute("DELETE FROM revisions WHERE id=?", (premature_v3[0],))

        # A valid v2 envelope can contain the source UUID of a different
        # document. Its signed Hodarium object ID must still be checked.
        wrong_object = sealed_revision(
            vault_ids[0], "44444444-4444-4444-8444-444444444444",
            source_xml, path="DifferentBody.ath")
        with sqlite3.connect(journal) as database:
            add_revision(database, wrong_object)
        rejects(binary, environment, vault, state,
                "Hodarium payload differs from its source identity")
        with sqlite3.connect(journal) as database:
            database.execute("DELETE FROM revisions WHERE id=?", (wrong_object[0],))

        # A self-consistently hashed old v2 document may still describe a
        # traversal path, an internal Vault directory or a non-ATH document.
        # Actual application rejects all three via its confined-path rule;
        # migration preflight must not count those as recoverable revisions.
        for malformed_path in ("../Outside.ath", ".athena/Secret.ath",
                               "Notes/Unexpected.txt", "folder/../Alias.ath",
                               "folder/./Alias.ath", "folder//Alias.ath",
                               "folder\\Alias.ath"):
            unsafe = sealed_revision(
                vault_ids[0], object_id, source_xml, path=malformed_path)
            with sqlite3.connect(journal) as database:
                add_revision(database, unsafe)
            rejects(binary, environment, vault, state,
                    "Invalid Hodarium document application path")
            with sqlite3.connect(journal) as database:
                database.execute("DELETE FROM revisions WHERE id=?", (unsafe[0],))

        unsafe_deletion = sealed_revision(
            vault_ids[0], object_id, b"", path="../Deleted.ath", deleted=True)
        with sqlite3.connect(journal) as database:
            add_revision(database, unsafe_deletion)
        rejects(binary, environment, vault, state,
                "Invalid Hodarium document application path")
        with sqlite3.connect(journal) as database:
            database.execute("DELETE FROM revisions WHERE id=?", (unsafe_deletion[0],))

        # An XML document tombstone has no payload to extract a body UUID
        # from. Its signed object's persistent UUID must still be canonical;
        # otherwise the old peer journal can delete a path under an identity
        # that no legitimate source document could ever own.
        invalid_identity_tombstone = sealed_revision(
            vault_ids[0], "not-a-source-uuid", b"", path="Deleted.ath",
            deleted=True)
        with sqlite3.connect(journal) as database:
            add_revision(database, invalid_identity_tombstone)
        rejects(binary, environment, vault, state,
                "Invalid Hodarium document source identity")
        with sqlite3.connect(journal) as database:
            database.execute("DELETE FROM revisions WHERE id=?",
                             (invalid_identity_tombstone[0],))

        # SQLite json_extract() takes the *first* duplicate object key, but
        # common C++/Python JSON parsers keep the *last*. An attacker can
        # preserve a valid content-addressed revision ID while making SQL
        # interpret its document format, scope or parents differently.
        ordinary = sealed_revision(
            vault_ids[0], object_id, source_xml, origin_member="duplicate-key-case")
        for key, forged_value in (("format", "ath-xml-v3"),
                                  ("vault", vault_ids[1]),
                                  ("object", "88888888-8888-4888-8888-888888888888")):
            with sqlite3.connect(journal) as database:
                add_revision(database, ordinary)
                canonical = json.dumps(ordinary[1], sort_keys=True, separators=(",", ":"))
                forged = "{" + json.dumps(key) + ":" + json.dumps(forged_value) + "," + canonical[1:]
                database.execute("UPDATE revisions SET descriptor=? WHERE id=?",
                                 (forged, ordinary[0]))
                assert database.execute(
                    "SELECT json_extract(descriptor,'$." + key + "') FROM revisions "
                    "WHERE id=?", (ordinary[0],)).fetchone()[0] == forged_value
            rejects(binary, environment, vault, state,
                    "Duplicate Hodarium revision descriptor field")
            with sqlite3.connect(journal) as database:
                database.execute("DELETE FROM revisions WHERE id=?", (ordinary[0],))

        # An immutable read deliberately ignores WAL pages. A second SQLite
        # connection with a pending nonempty WAL must block the entire audit
        # instead of producing a plausible but incomplete snapshot.
        writer = sqlite3.connect(journal)
        try:
            writer.execute("PRAGMA journal_mode=WAL")
            writer.execute("INSERT INTO incoming VALUES (?)", ("incomplete-remote",))
            writer.commit()
            rejects(binary, environment, vault, state, "uncheckpointed WAL")
            writer.execute("DELETE FROM incoming")
            writer.commit()
        finally:
            writer.close()
        assert "0 receiving" in run(binary, environment, vault, state)

        # Each existing Hodarium component has an explicit schema version
        # ceiling. Keeping the familiar table names in a newer database does
        # not mean its semantics are safe under the old migration preflight.
        for database_path in (state / "device.sqlite", journal, *sidecars):
            with sqlite3.connect(database_path) as database:
                previous = database.execute("PRAGMA user_version").fetchone()[0]
                database.execute("PRAGMA user_version=99")
            # sqlite3's context manager commits but does NOT close. Leaving
            # the connection open would keep WAL pages live, so the existing
            # WAL safety gate (correctly) rejects before the schema gate can
            # be exercised. Close explicitly to isolate these two policies.
            database.close()
            rejects(binary, environment, vault, state,
                    "Unsupported external Hodarium SQLite schema version")
            with sqlite3.connect(database_path) as database:
                database.execute(f"PRAGMA user_version={previous}")
            database.close()
        assert "3 sealed revisions verified" in run(binary, environment, vault, state)

        # Group handles are 43-character base64url identifiers. A forged
        # prefix must not select another group's journal through a fuzzy
        # filename match, even when both exist in an external state folder.
        with sqlite3.connect(state / "device.sqlite") as database:
            database.execute("INSERT INTO vault_bindings VALUES (?,?,?,1)",
                             (group + "-shadow", vault_ids[0], str(vault.resolve())))
        rejects(binary, environment, vault, state, "Malformed external Hodarium Vault binding")

        print("ATHENA-V3-HODARIUM-SHARED-AUDIT-PASS: "
              "two bindings, one group journal, disabled binding preserved, "
              "signed DAG/index corruption rejected, WAL blocked, "
              "read-only source and isolated staged candidate, publication blocked")


if __name__ == "__main__":
    main()
