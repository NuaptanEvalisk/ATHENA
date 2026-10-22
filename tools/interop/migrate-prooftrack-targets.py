#!/usr/bin/env python3
"""Persist reviewed ProofTrack associations through saved-document AUDMAP batches.

The Luna mapping is authoritative: do not infer additional edges. Read-only
preflight first checks the XML v3 vault, proof UUIDs, target node types and
paragraph boundaries. The Definition/Provable AUDMAP migration helper owns the
pre-write vault backup, ATHENA lifecycle, atomic per-file batch commits and
post-write verification. Dry-run by default; --apply is explicit.

SPDX-License-Identifier: GPL-3.0-or-later
"""

import argparse
from collections import Counter, defaultdict
import importlib.util
import json
from pathlib import Path, PurePosixPath
import re
import sys
import tempfile
import xml.etree.ElementTree as ET


MODULE = Path(__file__).with_name("migrate-provable-fields.py")
spec = importlib.util.spec_from_file_location("athena_prooftrack_migration_shared", MODULE)
shared = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = shared
spec.loader.exec_module(shared)

KINDS = frozenset({
    "theorem", "proposition", "lemma", "corollary", "question", "example",
    "remark", "law", "warning", "note", "quote-env",
})
HEADINGS = frozenset({
    "part", "chapter", "section", "subsection", "subsubsection",
    "paragraph", "subparagraph",
})
UUID = re.compile(r"[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}\Z")


def require_uuid(identity):
    if not isinstance(identity, str) or UUID.fullmatch(identity) is None:
        raise ValueError(f"Invalid UUID: {identity!r}")
    return identity


def source_name(name):
    result = PurePosixPath(name)
    if (not name or result.is_absolute() or result.suffix != ".ath"
            or any(p in ("", ".", "..") for p in result.parts)
            or result.as_posix() != name
            or any(part in shared.EXCLUDED for part in result.parts)):
        raise ValueError(f"Invalid Notes source filename: {name!r}")
    return name


def kind(node):
    if node.tag != "n" or node.get("t") != "enunciation":
        return ""
    prop = node.find('./x/p[@n="kind"]')
    return "" if prop is None else "".join(prop.itertext())


def tag(node):
    return node.get("t") if node.tag == "n" else node.tag


def children(node):
    return [c for c in node if c.tag != "x"]


def indexed_nodes(path, selected):
    root = ET.parse(path).getroot()
    if root.tag != "a" or root.get("v") != "3":
        raise ValueError(f"Expected native XML v3 document: {path}")
    body = root.find('./n[@t="document"]/n[@t="body"]')
    if body is None:
        raise ValueError(f"Missing document body in {path}")
    located = {}

    def visit(node, parent_tag):
        identity = node.get("i")
        if identity in selected:
            if identity in located:
                raise ValueError(f"Duplicate UUID {identity} within {path}")
            located[identity] = (node, parent_tag)
        for child in children(node):
            visit(child, tag(node))

    visit(body, "")
    missing = selected - located.keys()
    if missing:
        raise ValueError(f"Missing source UUIDs in {path}: {sorted(missing)[:6]}")
    return located


def source_target(node):
    prop = node.find('./x/p[@n="target"]')
    if prop is None:
        return None
    if prop.get("t") != "r":
        raise ValueError("Existing proof.target has a non-reference property type")
    return require_uuid(prop.get("i"))


def valid_target(node, parent):
    label = tag(node)
    if label == "enunciation":
        return kind(node) in KINDS
    return (parent in {"document", "para"} and
            label not in HEADINGS and
            label not in {"proof", "enunciation"})


def proposal_manifest(proposals, index, root):
    if not isinstance(proposals, list) or not isinstance(index, dict):
        raise ValueError("Expected Luna proposal list and UUID source index")
    if not (root / "Vaultfile.json").is_file():
        raise ValueError("Expected a Vaultfile.json in the selected vault")
    proof_to_target, files, needed = {}, defaultdict(dict), defaultdict(set)
    for entry in proposals:
        if not isinstance(entry, dict):
            raise ValueError("Expected a Luna proposal object")
        proof_id = require_uuid(entry.get("proof_id"))
        if proof_id in proof_to_target:
            raise ValueError(f"Duplicate proof in proposals: {proof_id}")
        status = entry.get("status")
        if status not in {"linked", "needs_statement"}:
            raise ValueError(f"Unexpected proposal status: {status}")
        record = index.get(proof_id)
        if not isinstance(record, dict) or record.get("kind") != "proof":
            raise ValueError(f"Proposal does not identify a native Proof: {proof_id}")
        filename = source_name(record["file"])
        if status == "needs_statement":
            if entry.get("target_id") is not None:
                raise ValueError(f"Unresolved proof has a target: {proof_id}")
            proof_to_target[proof_id] = None
            needed[filename].add(proof_id)
            continue
        target = require_uuid(entry.get("target_id"))
        if target == proof_id:
            raise ValueError(f"Proof points to itself: {proof_id}")
        destination = index.get(target)
        if not isinstance(destination, dict):
            raise ValueError(f"Missing target in Luna index: {proof_id} -> {target}")
        targetfile = source_name(destination["file"])
        proof_to_target[proof_id] = target
        files[filename][proof_id] = target
        needed[filename].add(proof_id)
        needed[targetfile].add(target)
    if len(proof_to_target) != len(proposals):
        raise ValueError("Proposal identities are not unique")

    valid, already, absent = Counter(), 0, 0
    all_locations = {}
    for relative, requested in sorted(needed.items()):
        path = root / relative
        if not path.is_file() or path.is_symlink():
            raise ValueError(f"Missing or symbolic-link Notes document: {path}")
        nodes = indexed_nodes(path, requested)
        for identity, (node, parent) in nodes.items():
            if index[identity].get("tag") != tag(node):
                raise ValueError(f"Node tag changed since Luna snapshot: {identity}")
            if index[identity].get("kind", "") != kind(node):
                raise ValueError(f"Node kind changed since Luna snapshot: {identity}")
            all_locations[identity] = (node, parent)

    for proof_id, target in proof_to_target.items():
        node, _ = all_locations[proof_id]
        if tag(node) != "enunciation" or kind(node) != "proof":
            raise ValueError(f"Source proof changed since Luna review: {proof_id}")
        existing = source_target(node)
        if target is None:
            if existing is not None:
                raise ValueError(f"Unresolved proof already has a target; review manually: {proof_id}")
            absent += 1
            continue
        dest, parent = all_locations[target]
        if not valid_target(dest, parent):
            raise ValueError(f"Unsupported target node or non-paragraph fragment: {target}")
        valid[kind(dest) or tag(dest)] += 1
        if existing is None:
            continue
        if existing != target:
            raise ValueError(
                f"Conflicting existing proof.target: {proof_id} ({existing} != {target})")
        already += 1
    print(
        f"Read-only XML v3 preflight: {len(proposals)} proofs, "
        f"{sum(len(x) for x in files.values())} linked, "
        f"{absent} unresolved, {len(needed)} checked documents, "
        f"{len(files)} proof documents, {already} already linked",
        flush=True,
    )
    print("Target types: " + json.dumps(valid, sort_keys=True), flush=True)
    return {
        "proofs_by_file": {file: proofs for file, proofs in sorted(files.items())},
        "_files": sorted(files),
        "expected_total": sum(map(len, files.values())),
    }


def plan_document(document, relative, config):
    expected = config["proofs_by_file"].get(str(relative), {})
    checks, edits, records, matched = [], [], [], set()
    if not expected:
        return shared.Plan(checks, edits, records, {})

    def walk(node, path):
        if node.get("tag") in shared.QUOTED:
            return
        identity = node.get("id")
        if identity in expected:
            if identity in matched:
                raise ValueError(f"Duplicate proof UUID in AUDMAP document: {identity}")
            matched.add(identity)
            if (node.get("tag") != "enunciation" or
                    shared.properties(node).get("kind") !=
                    {"type": "string", "value": "proof"}):
                raise ValueError(f"Luna proof is no longer a Proof: {identity}")
            target = expected[identity]
            previous = shared.properties(node).get("target")
            record = {"file": str(relative), "node_id": identity, "target_id": target}
            value = {"type": "reference", "value": target}
            if previous is not None:
                if previous != value:
                    raise ValueError(
                        f"Conflicting source target for {identity}: {previous!r}")
                records.append({**record, "status": "already_linked"})
            else:
                checks.append({"path": list(path), "tree": node})
                edits.append({
                    "path": list(path),
                    "command": "update_properties",
                    "parameters": {
                        "set": [{"name": "target", "value": value}],
                        "remove": [],
                    },
                })
                records.append({**record, "status": "planned"})
        for number, child in enumerate(shared.children(node)):
            walk(child, path + (number,))

    for index, child in enumerate(shared.children(document)):
        if child.get("tag") == "body":
            walk(child, (index,))
    if matched != expected.keys():
        raise ValueError(
            f"Unmatched Luna Proof IDs in {relative}: {sorted(expected.keys() - matched)[:5]}")
    return shared.Plan(checks, edits, records, {
        proof_id: {"target": {"type": "reference", "value": expected[proof_id]}}
        for proof_id in matched
    })


def validate_manifest(config):
    files = config["proofs_by_file"]
    if (not files or config["_files"] != sorted(files) or
            sum(len(x) for x in files.values()) != config["expected_total"]):
        raise ValueError("ProofTrack migration manifest is incomplete")
    for filename, proofs in files.items():
        source_name(filename)
        for proof_id, target in proofs.items():
            require_uuid(proof_id)
            require_uuid(target)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--proposals", type=Path, required=True)
    parser.add_argument("--index", type=Path, default=None)
    parser.add_argument("--vault", required=True)
    parser.add_argument("--expect-root", type=Path, required=True)
    parser.add_argument("--binary", type=Path, default=Path(__file__).resolve().parents[2] /
                        "ATHENA/bin/ATHENA.bin")
    parser.add_argument("--identity", type=Path, default=None)
    parser.add_argument("--preflight-only", action="store_true",
                        help="Validate all UUIDs against source XML without starting ATHENA or backing up")
    parser.add_argument("--apply", action="store_true",
                        help="Write reviewed links via atomic AUDMAP batches after backing up the vault")
    args = parser.parse_args(argv)

    root = args.expect_root.resolve(strict=True)
    vaultfile = json.loads((root / "Vaultfile.json").read_text())
    if vaultfile.get("name") != args.vault:
        raise ValueError(f"Vault name mismatch: {vaultfile.get('name')!r} != {args.vault!r}")
    proposals = args.proposals.resolve(strict=True)
    indexpath = (args.index or (args.proposals.parent / "index.json")).resolve(strict=True)
    manifest = proposal_manifest(
        json.loads(proposals.read_text()), json.loads(indexpath.read_text()), root)
    if args.preflight_only:
        return 0
    # This guard precedes even the backup, just like the Definition migration.
    shared.require_no_athena(args.binary.resolve(strict=True))
    with tempfile.TemporaryDirectory(prefix="athena-prooftrack-manifest-", dir="/tmp") as place:
        config = Path(place) / "manifest.json"
        config.write_text(json.dumps(manifest, ensure_ascii=False), encoding="utf-8")
        command = [
            "--vault", args.vault, "--expect-root", str(root),
            "--binary", str(args.binary), "--config", str(config),
        ]
        if args.identity is not None:
            command += ["--identity", str(args.identity)]
        if args.apply:
            command += ["--apply"]
        return shared.main(
            command,
            planner=plan_document,
            migration="prooftrack-targets",
            client_name="ProofTrack target migration",
            description=__doc__,
            config_file=None,
            validate_config=validate_manifest,
        )


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("Interrupted; any pre-write backup and migration report were retained.",
              file=sys.stderr, flush=True)
        sys.exit(130)
    except (OSError, ValueError, RuntimeError) as failure:
        print(f"ERROR: {failure}", file=sys.stderr, flush=True)
        sys.exit(1)
