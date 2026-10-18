#!/usr/bin/env python3
"""Exercise v3 editing and fresh-process recovery; never edit supplied sources."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import xml.etree.ElementTree as ET


def inspect(path):
    root = ET.parse(path).getroot()
    if root.tag != "a" or root.attrib != {"v": "3", "k": "d"}:
        raise RuntimeError(f"Not a native v3 document: {path}")
    nodes = list(root.iter())
    ids = [node.get("i") for node in nodes if node.get("i")]
    if len(ids) != len(set(ids)):
        raise RuntimeError(f"Duplicate source UUIDs: {path}")
    return root, set(ids)


def source_shape(node):
    return (node.tag, sorted((k, v) for k, v in node.attrib.items() if k != "i"),
            node.text or "", [source_shape(child) for child in node if child.tag != "x"])


def check_original_body(original, saved):
    original_body = original.find("./n/n[@t='body']/n[@t='document']")
    saved_body = saved.find("./n/n[@t='body']/n[@t='document']")
    if original_body is None or saved_body is None:
        raise RuntimeError("Missing structural document body")
    before = [source_shape(child) for child in original_body if child.tag != "x"]
    after = [source_shape(child) for child in saved_body if child.tag != "x"]
    if after[:len(before)] != before:
        raise RuntimeError("Editing appended paragraphs changed existing body structure")
    saved_nodes = {node.get("i"): node for node in saved.iter() if node.get("i")}
    for node in original.iter():
        if node.get("i") and node.find("x") is not None:
            properties = saved_nodes[node.get("i")].find("x")
            if properties is None or any(
                    source_shape(prop) not in [source_shape(p) for p in properties]
                    for prop in node.find("x")):
                raise RuntimeError(f"Original typed property changed: {node.get('i')}")


def run(args, home, phase, label=None):
    runtime, resources = args.runtime.resolve(), args.resources.resolve()
    env = dict(os.environ)
    env.update({
        "HOME": str(home), "ATHENA_HOME_PATH": str(home / "profile"),
        "XDG_CONFIG_HOME": str(home / "config"),
        "XDG_CACHE_HOME": str(home / "cache"), "XDG_DATA_HOME": str(home / "data"),
        "ATHENA_PATH": str(resources), "QT_QPA_PLATFORM": "offscreen",
        "GUILE_AUTO_COMPILE": "0", "ATHENA_V3_PERSISTENCE_ROOT": str(home),
        "ATHENA_V3_PERSISTENCE_PHASE": phase,
        "ATHENA_V3_PERSISTENCE_VAULT": "yes" if args.vault else "no",
        "GUILE_LOAD_PATH": str(runtime / "share/guile/3.0"),
        "GUILE_LOAD_COMPILED_PATH": str(runtime / "lib/guile/3.0/ccache"),
        "LD_LIBRARY_PATH": ":".join((str(runtime / "lib"), str(resources / "lib"),
                                     env.get("LD_LIBRARY_PATH", ""))),
    })
    script = Path(__file__).with_suffix(".scm").resolve()
    expression = f'(exec-global (lambda () (primitive-load {json.dumps(str(script))})))'
    log_path = home / f"{label or phase}.log"
    report = home / f"result-{phase}.scm"
    report.unlink(missing_ok=True)
    with log_path.open("w") as log:
        process = subprocess.Popen(
            [str(args.binary.resolve()), "-H", "-X", "-x", expression],
            cwd=home, env=env, start_new_session=True,
            stdout=log, stderr=subprocess.STDOUT)
        try:
            process.wait(timeout=1800 if phase == "audit" else 120)
        finally:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
    result = report.read_text() if report.exists() else "No Scheme result"
    if process.returncode or result != "#t":
        raise RuntimeError(f"{phase} failed ({process.returncode}): {result}\n"
                           f"{log_path.read_text(errors='replace')[-12000:]}\n{home}")
    if label:
        report.rename(home / f"result-{label}.scm")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "runtime", "resources"):
        parser.add_argument("--" + name, type=Path, required=True)
    inputs = parser.add_mutually_exclusive_group()
    inputs.add_argument("--document", type=Path)
    inputs.add_argument("--corpus", type=Path)
    parser.add_argument("--styles", type=Path)
    parser.add_argument("--vault", action="store_true")
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()
    if args.artifacts:
        args.artifacts.mkdir(parents=True, exist_ok=True)
    home = Path(tempfile.mkdtemp(prefix="native-v3-persistence-", dir=args.artifacts))
    print(f"Artifacts: {home}", flush=True)
    system = home / "profile/system"
    system.mkdir(parents=True)
    (system / "sys_state.json").write_text(json.dumps({"format": "athena-system-state", "version": 3}))
    if args.styles:
        for style in args.styles.rglob("*"):
            if style.is_file() and style.suffix in (".ats", ".ts"):
                target = home / "profile/styles" / style.relative_to(args.styles)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(style, target)
    if args.corpus:
        originals = {}
        for index, document in enumerate(sorted(args.corpus.rglob("*.ath"))):
            target = home / f"corpus-{index}.ath"
            contents = document.read_bytes()
            target.write_bytes(contents)
            inspect(target)
            originals[target] = (document, contents)
        if not originals:
            raise RuntimeError("Empty v3 corpus")
        (home / "sources.json").write_text(json.dumps(
            {str(target): str(document) for target, (document, _) in originals.items()}, indent=2))
        entries = list(originals.items())
        for offset in range(0, len(entries), 64):
            batch = entries[offset:offset + 64]
            (home / "files.scm").write_text(
                "(" + " ".join(json.dumps(str(path), ensure_ascii=False) for path, _ in batch) + ")")
            run(args, home, "audit", f"audit-{offset}")
            for target, (document, contents) in batch:
                old = ET.fromstring(contents)
                saved, ids = inspect(target)
                if not {node.get("i") for node in old.iter() if node.get("i")} <= ids:
                    raise RuntimeError(f"Original IDs lost: {document}")
                try:
                    check_original_body(old, saved)
                except RuntimeError as error:
                    raise RuntimeError(f"{document}: {error}") from error
                if document.read_bytes() != contents:
                    raise RuntimeError(f"Input document changed: {document}")
            print(f"Corpus verified: {offset + len(batch)}/{len(entries)}", flush=True)
        print(f"NATIVE-V3-CORPUS-PASS: {len(originals)} normal loads and saves")
        if not args.artifacts:
            shutil.rmtree(home)
        return
    source = home / "source.ath"
    if args.vault:
        (home / "Vaultfile.json").write_text(json.dumps({
            "name": "Isolated v3 persistence", "node_model_version": 2,
        }))
    original = args.document.read_bytes() if args.document else b'''<a v="3" k="d"><n t="document"><n t="style"><t><v>generic</v></t></n><n t="body"><n t="document" i="11111111-1111-4111-8111-111111111111"><t i="22222222-2222-4222-8222-222222222222"><v>Original paragraph</v></t></n></n></n></a>'''
    source.write_bytes(original)
    original_tree, original_ids = inspect(source)
    if args.vault:
        candidate = home / ".athena/xml-v3-candidate-manifest.json"
        candidate.parent.mkdir(parents=True)
        candidate.write_text(json.dumps({"kind": "xml-v3-disconnected-candidate"}))
        before = {path: (path.read_bytes(), path.stat().st_mtime_ns)
                  for path in (source, home / "Vaultfile.json", candidate)}
        run(args, home, "candidate")
        if any((path.read_bytes(), path.stat().st_mtime_ns) != saved
               for path, saved in before.items()):
            raise RuntimeError("Candidate refusal modified source or receipt")
        if (home / "ns.sqlite").exists() or (home / "map.sqlite").exists():
            raise RuntimeError("Candidate refusal initialized Vault databases")
        candidate.unlink()
    run(args, home, "edit")
    if args.vault:
        inspect(home / "created.ath")
    saved, saved_ids = inspect(source)
    if not original_ids <= saved_ids:
        raise RuntimeError("Save lost original source identities")
    check_original_body(original_tree, saved)
    if not saved.findall(".//d") or not saved.findall(".//n[@t='scripts']"):
        raise RuntimeError("Native keyboard math did not survive XML serialization")
    run(args, home, "reopen")
    reopened, reopened_ids = inspect(source)
    if saved_ids != reopened_ids:
        raise RuntimeError("Fresh-process load/resave changed source identities")
    if source_shape(saved) != source_shape(reopened):
        raise RuntimeError("Fresh-process load/resave changed document structure")
    run(args, home, "recover")
    autosaved, autosaved_ids = inspect(home / "source.ath~")
    recovered, recovered_ids = inspect(home / "recovered.ath")
    if autosaved_ids != recovered_ids or not saved_ids <= autosaved_ids:
        raise RuntimeError("Autosave recovery changed source identities")
    check_original_body(autosaved, recovered)
    if args.document and hashlib.sha256(args.document.read_bytes()).digest() != hashlib.sha256(original).digest():
        raise RuntimeError("Supplied source changed during the test")
    print("NATIVE-V3-PERSISTENCE-PASS: keyboard, save, restart, autosave, recovery")
    if not args.artifacts:
        shutil.rmtree(home)


if __name__ == "__main__":
    main()
