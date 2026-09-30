#!/usr/bin/env python3
"""Run one isolated born-v2 ordinary document integration check."""

import argparse
import json
import os
from pathlib import Path
import re
import signal
import shutil
import subprocess
import tempfile
import uuid
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path)
    parser.add_argument("--maintenance", action="store_true")
    args = parser.parse_args()

    artifacts = args.artifacts.resolve() if args.artifacts else None
    if artifacts:
        artifacts.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="node-new-v2-", dir=artifacts))
    profile = root / "profile"
    system = profile / "system"
    system.mkdir(parents=True)
    (system / "sys_state.json").write_text(json.dumps({
        "format": "athena-system-state",
        "version": 3,
    }))

    runtime = args.runtime.resolve()
    resources = args.resources.resolve()
    env = dict(os.environ)
    env.update({
        "HOME": str(root),
        "ATHENA_HOME_PATH": str(profile),
        "XDG_CONFIG_HOME": str(root / "config"),
        "XDG_CACHE_HOME": str(root / "cache"),
        "XDG_DATA_HOME": str(root / "data"),
        "ATHENA_PATH": str(resources),
        "QT_QPA_PLATFORM": "offscreen",
        "GUILE_AUTO_COMPILE": "0",
        "ATHENA_NODE_NEW_V2_ROOT": str(root),
        "GUILE_LOAD_PATH": str(runtime / "share/guile/3.0"),
        "GUILE_LOAD_COMPILED_PATH": str(runtime / "lib/guile/3.0/ccache"),
        "LD_LIBRARY_PATH": ":".join((
            str(runtime / "lib"), str(resources / "lib"),
            env.get("LD_LIBRARY_PATH", ""),
        )),
    })
    if args.maintenance:
        env["ATHENA_TEST_CONTINUOUS_MAINTENANCE"] = "1"
        vault = root / "vault"
        vault.mkdir()
        env["ATHENA_TEST_MAINTENANCE_ROOT"] = str(vault)
        (vault / "Vaultfile.json").write_text(json.dumps({
            "version": 1, "name": "Maintenance fixture", "node_model_version": 1,
        }))
        def fixture(path):
            document = ET.Element("athena-document", version="2", attrib={"text-model": "utf-8"})
            envelope = ET.SubElement(document, "node", tag="document")
            style = ET.SubElement(envelope, "node", tag="style")
            ET.SubElement(ET.SubElement(style, "text"), "value").text = "generic"
            body = ET.SubElement(envelope, "node", tag="body")
            paragraphs = ET.SubElement(body, "node", tag="document", id=str(uuid.uuid4()))
            statement = ET.SubElement(paragraphs, "node", tag="definition", id=str(uuid.uuid4()))
            content = ET.SubElement(statement, "node", tag="document", id=str(uuid.uuid4()))
            ET.SubElement(ET.SubElement(content, "text", id=str(uuid.uuid4())), "value").text = "Original body"
            path.parent.mkdir(parents=True, exist_ok=True)
            ET.ElementTree(document).write(path, encoding="utf-8", xml_declaration=True)
        for leaf in ("closed.ath", "live.ath", ".backup/skip.ath", ".athena/skip.ath", ".git/skip.ath"):
            fixture(vault / leaf)
        originals = {leaf: (vault / leaf).read_bytes() for leaf in
                     ("closed.ath", "live.ath", ".backup/skip.ath", ".athena/skip.ath", ".git/skip.ath")}
    script = Path(__file__).with_suffix(".scm").resolve()
    expression = (
        '(exec-global (lambda () (primitive-load '
        + json.dumps(str(script)) + ')))'
    )
    log_path = root / "runtime.log"
    with log_path.open("w") as log:
        process = subprocess.Popen(
            [str(args.binary.resolve()), *([] if args.maintenance else ["-H"]), "-X", "-x", expression],
            cwd=root, env=env, start_new_session=True,
            stdout=log, stderr=subprocess.STDOUT,
        )
        try:
            process.wait(timeout=120)
        except subprocess.TimeoutExpired as exc:
            raise RuntimeError(f"born-v2 check timed out; see {log_path}") from exc
        finally:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()

    result_path = root / "result.scm"
    result = result_path.read_text() if result_path.exists() else "No Scheme result"
    output = log_path.read_text(errors="replace")
    if process.returncode or not result.startswith("(#t "):
        raise RuntimeError(
            f"born-v2 check failed ({process.returncode}): {result}\n"
            f"{output[-16000:]}\nArtifacts: {root}"
        )

    match = re.fullmatch(r'\(#t "([0-9a-f-]+)"\)', result.strip())
    if not match:
        raise RuntimeError(f"Unexpected Scheme result: {result}")
    inserted_id = match.group(1)
    document = root / "created.ath"
    xml = ET.parse(document).getroot()
    if xml.tag != "athena-document" or xml.get("version") != "2":
        raise RuntimeError("First save of a born-v2 document did not emit XML v2")
    ids = [node.get("id") for node in xml.iter() if node.get("id")]
    if inserted_id not in ids or len(ids) < 3:
        raise RuntimeError(f"Saved v2 source lost allocated identities: {ids}")
    if args.maintenance:
        before = ET.fromstring(originals["closed.ath"])
        after = ET.parse(vault / "closed.ath").getroot()
        old = next(node for node in before.iter("node") if node.get("tag") == "definition")
        new = next(node for node in after.iter("node") if node.get("tag") == "enunciation")
        assert old.get("id") == new.get("id"), "maintenance changed source UUID"
        assert "Original body" in "".join(after.itertext())
        backups = list((vault / ".athena/maintenance/enunciations").glob("*.ath"))
        assert any(p.read_bytes() == originals["closed.ath"] for p in backups), "missing original backup"
        for leaf in ("live.ath", ".backup/skip.ath", ".athena/skip.ath", ".git/skip.ath"):
            assert (vault / leaf).read_bytes() == originals[leaf], f"unexpected rewrite: {leaf}"
        print("ATHENA-CONTINUOUS-MAINTENANCE-PASS; closed/live/UUID/undo/backup/exclusions")
    print(f"ATHENA-NODE-NEW-V2-PASS; create/edit/first-save/New scratch; {root}")
    if artifacts is None:
        shutil.rmtree(root)


if __name__ == "__main__":
    main()
