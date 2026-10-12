#!/usr/bin/env python3
"""Focused isolated AUDMAP migration check; production samples are read-only.

SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import uuid
import xml.etree.ElementTree as ET


REPO = Path(__file__).resolve().parents[2]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    sys.modules[name] = result
    spec.loader.exec_module(result)
    return result


def fixture():
    def text(value, identity=False):
        node = ET.Element("text", {"id": str(uuid.uuid4())} if identity else {})
        ET.SubElement(node, "value").text = value
        return node

    def node(tag, *items, identity=True):
        result = ET.Element("node", {"tag": tag})
        if identity:
            result.set("id", str(uuid.uuid4()))
        result.extend(items)
        return result

    def bold(body, tag="with"):
        return node(tag, *( [text("font-series"), text("bold"), body]
                           if tag == "with" else [body]))

    def enunciation(kind, body):
        result = node("enunciation")
        properties = ET.SubElement(result, "properties")
        bindings = ET.SubElement(properties, "property", name="athena:artifact-bindings", type="dictionary")
        ET.SubElement(bindings, "property", name="enunciation", type="string").text = str(uuid.uuid4())
        ET.SubElement(properties, "property", name="kind", type="string").text = kind
        ET.SubElement(properties, "property", name="numbered", type="boolean").text = "true"
        ET.SubElement(properties, "property", name="name", type="rich_tree").append(text(""))
        result.append(body)
        return result

    body = node("document",
        enunciation("theorem", node("document", bold(text("Whole theorem", True)))),
        enunciation("proposition", node("document", bold(text("Whole strong", True), "strong"))),
        enunciation("lemma", node("document", bold(node("document",
            text("First paragraph", True),
            node("concat", text("Keep "), bold(text("local emphasis"))))))),
        enunciation("corollary", node("document", node("with", text("color"), text("red"),
            text("font-series"), text("bold"), text("Keep red", True)))),
        enunciation("theorem", node("document", bold(text("Title only", True)),
                                    text("Not wholly bold", True))),
        enunciation("definition", node("document", bold(text("Keep definition", True)))),
        enunciation("proof", node("document", bold(text("Keep proof", True)))))
    root = ET.Element("athena-document", version="2", **{"text-model": "utf-8"})
    root.append(node("document", node("style", text("generic"), identity=False),
                     node("body", body, identity=False), identity=False))
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def ids_and_properties(data):
    def value(element):
        items = [value(child) for child in element]
        if element.tag == "properties" or element.get("type") == "dictionary":
            items.sort(key=repr)
        return (element.tag, tuple(sorted(element.attrib.items())), element.text or "", tuple(items))

    result = {}
    for node in ET.fromstring(data).iter():
        identity = node.get("id")
        if identity:
            assert identity not in result, f"Duplicate identity: {identity}"
            props = node.find("properties")
            result[identity] = value(props) if props is not None else None
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sample", action="append", type=Path, default=[])
    parser.add_argument("--style", action="append", type=Path, default=[],
                        help="Copy a sample's custom style into the isolated profile")
    args = parser.parse_args()
    helpers = module("audmap_helpers", REPO / "tests/interop/audmap-v3-runtime-test.py")
    migration = module("bold_migration", REPO / "tools/interop/migrate-provable-bold.py")
    root = Path(tempfile.mkdtemp(prefix="athena-provable-bold-", dir="/tmp"))
    vault, profile, runtime = root / "vault", root / "profile", root / "runtime"
    vault.mkdir(); runtime.mkdir(mode=0o700)
    helpers.private_json(profile / "system/sys_state.json",
                         {"format": "athena-system-state", "version": 3})
    (profile / "styles").mkdir()
    for style in args.style:
        (profile / "styles" / style.name).write_bytes(style.read_bytes())
    (vault / "Vaultfile.json").write_text(json.dumps({"name": "Bold migration test",
        "node_model_version": 1, "map_path": "map.sqlite", "namespace_db_path": "ns.sqlite"}))
    (vault / "Synthetic.ath").write_bytes(fixture())
    originals = {}
    for index, sample in enumerate(args.sample):
        content = sample.read_bytes()
        originals[sample] = hashlib.sha256(content).digest()
        (vault / f"Sample-{index}.ath").write_bytes(content)
    before = {file.name: file.read_bytes() for file in vault.glob("*.ath")}
    public, secret = helpers.curve_keypair()
    identity = root / "client/identity.json"
    helpers.private_json(identity, {"version": 1, "public_key": public, "secret_key": secret})
    helpers.private_json(profile / "system/audmap/clients.json",
                         {"version": 1, "clients": {public: {"allow": True, "trust": 0}}})
    resources, guile = REPO / "ATHENA", REPO / "build_qt6/athena-guile-runtime"
    env = dict(os.environ, HOME=str(root), ATHENA_HOME_PATH=str(profile),
        XDG_CONFIG_HOME=str(root / "config"), XDG_CACHE_HOME=str(root / "cache"),
        XDG_DATA_HOME=str(root / "data"), XDG_RUNTIME_DIR=str(runtime),
        ATHENA_PATH=str(resources), QT_QPA_PLATFORM="offscreen", GUILE_AUTO_COMPILE="0",
        GUILE_LOAD_PATH=str(guile / "share/guile/3.0"),
        GUILE_LOAD_COMPILED_PATH=str(guile / "lib/guile/3.0/ccache"),
        LD_LIBRARY_PATH=str(resources / "lib") + ":" + str(guile / "lib"))
    expression = ('(exec-global (lambda () (let ((error (vault-load-with-ns '
        f'(system->url {json.dumps(str(vault))}) "Bold migration test" "map.sqlite" "ns.sqlite"))) '
        '(unless (equal? error "") (display error)))))')
    log = root / "runtime.log"
    with log.open("w") as output:
        process = subprocess.Popen([str(REPO / "build_qt6/src/ATHENA.bin"),
            "-X", "-no-splash-screen", "-x", expression], cwd=root, env=env,
            stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            endpoint = helpers.wait_descriptor(runtime, process, log)
            sys.path.insert(0, str(REPO / "clients/python/src"))
            from athena_audmap import Client
            with Client(endpoint=endpoint, identity=identity) as client:
                session = migration.Session(client)
                deadline = time.monotonic() + 30
                while True:
                    try:
                        info = session.one('@/vaults/"Bold migration test"')
                        assert info["root"] == str(vault)
                        break
                    except RuntimeError:
                        if time.monotonic() >= deadline:
                            raise
                        time.sleep(0.1)
            command = [sys.executable, str(REPO / "tools/interop/migrate-provable-bold.py"),
                "--endpoint", str(endpoint), "--identity", str(identity),
                "--vault", "Bold migration test", "--expect-root", str(vault), "."]
            def run(extra=()):
                completed = subprocess.run(command + list(extra), env=env, text=True,
                    capture_output=True, timeout=180)
                if completed.returncode:
                    raise RuntimeError(completed.stdout + completed.stderr)
                return completed.stdout, json.loads(completed.stdout.splitlines()[-1])["summary"]
            dry, planned = run()
            (root / "dry-run.jsonl").write_text(dry)
            assert all((vault / name).read_bytes() == content for name, content in before.items())
            assert planned["wrappers"] >= 4 and not planned["applied"]
            applied, summary = run(["--apply"])
            (root / "applied.jsonl").write_text(applied)
            assert summary["wrappers"] == planned["wrappers"]
            for name, content in before.items():
                original = ids_and_properties(content)
                after = ids_and_properties((vault / name).read_bytes())
                assert all(key in after and after[key] == value for key, value in original.items()), name
            saved = {file.name: file.read_bytes() for file in vault.glob("*.ath")}
            _, repeated = run(["--apply"])
            assert repeated["wrappers"] == 0
            assert all((vault / name).read_bytes() == content for name, content in saved.items())
            with Client(endpoint=endpoint, identity=identity) as client:
                session = migration.Session(client)
                tree = session.one('@/vaults/"Bold migration test"/filesystem/"Synthetic.ath"/saved')["tree"]
                body = next(n for n in tree["children"] if n.get("tag") == "body")["children"][0]
                cases = body["children"]
                assert cases[4]["children"][0]["children"][0]["tag"] == "with", "Local bold removed"
                for case in cases[5:]:
                    assert case["children"][0]["children"][0]["children"][1]["text"] == "bold"
                assert '"red"' in json.dumps(cases[3]), "Unrelated formatting removed"
            print(json.dumps({"PASS": summary, "artifacts": str(root)}, indent=2))
        finally:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            for sample, digest in originals.items():
                assert hashlib.sha256(sample.read_bytes()).digest() == digest, f"Sample changed: {sample}"


if __name__ == "__main__":
    main()
