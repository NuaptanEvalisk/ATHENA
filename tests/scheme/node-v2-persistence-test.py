#!/usr/bin/env python3
"""Run one isolated normal-load/edit/save/autosave/recovery XML v2 check."""

import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import xml.etree.ElementTree as ET


ROOT_ID = "11111111-1111-4111-8111-111111111111"
FIRST_ID = "22222222-2222-4222-8222-222222222222"
SECOND_ID = "33333333-3333-4333-8333-333333333333"


def fixture():
    return f'''<?xml version="1.0" encoding="UTF-8"?>\n<athena-document version="2" text-model="utf-8"><node tag="document"><node tag="style"><text><value>generic</value></text></node><node tag="body"><node tag="document" id="{ROOT_ID}"><text id="{FIRST_ID}"><properties><property name="test:marker" type="string">kept</property></properties><value>First paragraph</value></text><text id="{SECOND_ID}"><value>Second paragraph</value></text></node></node></node></athena-document>\n'''.encode()


def inspect(path, expected_texts):
    root = ET.parse(path).getroot()
    if root.tag != "athena-document" or root.get("version") != "2":
        raise RuntimeError(f"{path.name} is not ATHENA XML v2")
    texts = {}
    for node in root.iter("text"):
        value = node.find("value")
        if value is not None and value.text in expected_texts:
            texts[value.text] = node.get("id")
    missing = set(expected_texts) - texts.keys()
    if missing or any(not texts[text] for text in expected_texts):
        raise RuntimeError(f"{path.name} lost identified paragraphs: {missing} {texts}")
    return texts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    args = parser.parse_args()
    artifacts = args.artifacts.resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    home = Path(tempfile.mkdtemp(prefix="node-v2-persistence-", dir=artifacts))
    system = home / "profile/system"
    system.mkdir(parents=True)
    (system / "sys_state.json").write_text(json.dumps({
        "format": "athena-system-state", "version": 2,
        "compatibility_version": "2.1.4",
    }))
    source = home / "source.ath"
    source.write_bytes(fixture())
    runtime = args.runtime.resolve()
    resources = args.resources.resolve()
    env = dict(os.environ)
    env.update({
        "HOME": str(home), "ATHENA_HOME_PATH": str(home / "profile"),
        "XDG_CONFIG_HOME": str(home / "config"),
        "XDG_CACHE_HOME": str(home / "cache"),
        "XDG_DATA_HOME": str(home / "data"),
        "ATHENA_PATH": str(resources), "QT_QPA_PLATFORM": "offscreen",
        "GUILE_AUTO_COMPILE": "0", "ATHENA_NODE_V2_ROOT": str(home),
        "GUILE_LOAD_PATH": str(runtime / "share/guile/3.0"),
        "GUILE_LOAD_COMPILED_PATH": str(runtime / "lib/guile/3.0/ccache"),
        "LD_LIBRARY_PATH": ":".join((str(runtime / "lib"),
                                     str(resources / "lib"),
                                     env.get("LD_LIBRARY_PATH", ""))),
    })
    script = Path(__file__).with_suffix(".scm").resolve()
    expression = '(exec-global (lambda () (primitive-load ' + json.dumps(str(script)) + ')))'
    log_path = home / "runtime.log"
    with log_path.open("w") as log:
        process = subprocess.Popen(
            [str(args.binary.resolve()), "-H", "-X", "-x", expression],
            cwd=home, env=env, start_new_session=True,
            stdout=log, stderr=subprocess.STDOUT)
        try:
            process.wait(timeout=120)
        except subprocess.TimeoutExpired as exc:
            raise RuntimeError(f"v2 persistence check timed out; see {log_path}") from exc
        finally:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
    result_path = home / "result.scm"
    result = result_path.read_text() if result_path.exists() else "No Scheme result"
    output = log_path.read_text(errors="replace")
    if process.returncode or result != "#t" or "ATHENA-NODE-V2-PERSISTENCE-PASS" not in output:
        raise RuntimeError(f"v2 persistence failed ({process.returncode}): {result}\n"
                           f"{output[-16000:]}\nArtifacts: {home}")

    saved = inspect(source, ["First paragraph", "Second paragraph", "Saved paragraph"])
    if saved["First paragraph"] != FIRST_ID:
        raise RuntimeError("normal save changed the original paragraph UUID")
    auto = inspect(home / "source.ath~",
                   ["First paragraph", "Second paragraph", "Saved paragraph",
                    "Autosave paragraph"])
    recovered = inspect(home / "recovered.ath",
                        ["First paragraph", "Second paragraph", "Saved paragraph",
                         "Autosave paragraph"])
    if auto != recovered:
        raise RuntimeError("autosave recovery changed persisted paragraph identities")
    print(f"ATHENA-NODE-V2-PERSISTENCE-PASS; normal save/autosave/recovery/reopen; {home}")


if __name__ == "__main__":
    main()
