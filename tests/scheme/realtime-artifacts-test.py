#!/usr/bin/env python3
"""One isolated end-to-end realtime artifact check using the normal binary."""
import argparse
import json
import os
from pathlib import Path
import signal
import sqlite3
import subprocess
import tempfile
import uuid
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="athena-live-artifacts-") as temporary:
        root = Path(temporary)
        vault = root / "vault"
        vault.mkdir()
        (vault / "Vaultfile.json").write_text(json.dumps({
            "version": 1, "name": "Realtime fixture", "node_model_version": 1}))
        document = ET.Element("athena-document", version="2", attrib={"text-model": "utf-8"})
        envelope = ET.SubElement(document, "node", tag="document")
        body = ET.SubElement(ET.SubElement(envelope, "node", tag="body"), "node",
                             tag="document", id=str(uuid.uuid4()))
        paragraph = ET.SubElement(body, "node", tag="concat", id=str(uuid.uuid4()))
        keyword = ET.SubElement(paragraph, "node", tag="strong")
        ET.SubElement(ET.SubElement(keyword, "text"), "value").text = "nebular space"
        original = ET.tostring(document, encoding="utf-8", xml_declaration=True)
        (vault / "closed.ath").write_bytes(original)
        for directory in (".athena", ".backup", ".git"):
            (vault / directory).mkdir()
            (vault / directory / "skip.ath").write_bytes(original)
        profile = root / "profile"
        (profile / "system").mkdir(parents=True)
        (profile / "system/sys_state.json").write_text(json.dumps({
            "format": "athena-system-state", "version": 3}))
        env = dict(os.environ, HOME=str(root), ATHENA_HOME_PATH=str(profile),
                   ATHENA_PATH=str(args.resources.resolve()), QT_QPA_PLATFORM="offscreen",
                   GUILE_AUTO_COMPILE="0", ATHENA_REALTIME_ARTIFACT_TEST_ROOT=str(root),
                   XDG_CONFIG_HOME=str(root / "config"), XDG_CACHE_HOME=str(root / "cache"),
                   XDG_DATA_HOME=str(root / "data"),
                   GUILE_LOAD_PATH=str(args.runtime.resolve() / "share/guile/3.0"),
                   GUILE_LOAD_COMPILED_PATH=str(args.runtime.resolve() / "lib/guile/3.0/ccache"))
        env["LD_LIBRARY_PATH"] = ":".join((str(args.runtime.resolve() / "lib"),
            str(args.resources.resolve() / "lib"), env.get("LD_LIBRARY_PATH", "")))
        script = Path(__file__).with_suffix(".scm").resolve()
        expression = '(exec-global (lambda () (primitive-load ' + json.dumps(str(script)) + ')))'
        log_path = root / "runtime.log"
        with log_path.open("w") as log:
            process = subprocess.Popen([str(args.binary.resolve()), "-X", "-x", expression],
                cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                process.wait(timeout=90)
            except subprocess.TimeoutExpired:
                raise RuntimeError("Runtime timed out:\n" + log_path.read_text(errors="replace")[-16000:])
            finally:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
        result = root / "result.scm"
        if process.returncode or not result.exists() or result.read_text().strip() != "(#t)":
            raise RuntimeError(f"exit={process.returncode}; result={result.read_text() if result.exists() else 'missing'}\n"
                               + log_path.read_text(errors="replace")[-16000:])
        with sqlite3.connect(vault / "artifacts.db") as db:
            rows = db.execute("SELECT path,display_text,range_state,source_uuid,uuid FROM artifacts ORDER BY path").fetchall()
        assert len(rows) == 3, (rows, log_path.read_text(errors="replace")[-12000:])
        assert {row[1] for row in rows} == {"quasar operator", "quasar field", "nebular space"}, rows
        for directory in (".athena", ".backup", ".git"):
            assert (vault / directory / "skip.ath").read_bytes() == original
        for path, _, state, source_id, artifact_id in rows:
            assert state == "pending", rows
            xml = ET.parse(vault / path).getroot()
            node = next(value for value in xml.iter("node") if value.get("id") == source_id)
            assert artifact_id in ET.tostring(node, encoding="unicode"), "binding not persisted in source"
        print("PASS: cross-buffer overlay, masking, discard, source bindings, exit flush, pending ranges, closed worker, exclusions")


if __name__ == "__main__":
    main()
