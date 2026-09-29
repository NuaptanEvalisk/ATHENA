#!/usr/bin/env python3
"""Run the isolated one-use cut/move credential lifecycle regression."""

import argparse
import json
import os
from pathlib import Path
import signal
import shutil
import subprocess
import tempfile


def document(root_id, blocks):
    children = "".join(
        f'<node tag="section" id="{node_id}"><text><value>{text}</value></text></node>'
        for node_id, text in blocks
    )
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<athena-document version="2" text-model="utf-8">'
        '<node tag="document">'
        '<node tag="style"><text><value>generic</value></text></node>'
        f'<node tag="body"><node tag="document" id="{root_id}">'
        f'{children}</node></node></node></athena-document>\n'
    ).encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()

    artifacts = args.artifacts.resolve() if args.artifacts else None
    if artifacts:
        artifacts.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="node-move-", dir=artifacts))
    vault1 = root / "vault1"
    vault2 = root / "vault2"
    vault1.mkdir()
    vault2.mkdir()
    (vault1 / "Vaultfile.json").write_text('{"name":"Move vault 1"}')
    (vault2 / "Vaultfile.json").write_text('{"name":"Move vault 2"}')

    (vault1 / "A.ath").write_bytes(document(
        "11111111-1111-4111-8111-111111111111",
        [
            ("22222222-2222-4222-8222-222222222222", "Move me"),
            ("33333333-3333-4333-8333-333333333333", "Stay A"),
        ],
    ))
    (vault1 / "B.ath").write_bytes(document(
        "44444444-4444-4444-8444-444444444444",
        [("55555555-5555-4555-8555-555555555555", "Stay B")],
    ))
    (vault2 / "C.ath").write_bytes(document(
        "66666666-6666-4666-8666-666666666666",
        [("77777777-7777-4777-8777-777777777777", "Stay C")],
    ))

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
        "ATHENA_NODE_MOVE_VAULT1": str(vault1),
        "ATHENA_NODE_MOVE_VAULT2": str(vault2),
        "GUILE_LOAD_PATH": str(runtime / "share/guile/3.0"),
        "GUILE_LOAD_COMPILED_PATH": str(runtime / "lib/guile/3.0/ccache"),
        "LD_LIBRARY_PATH": ":".join((
            str(runtime / "lib"), str(resources / "lib"),
            env.get("LD_LIBRARY_PATH", ""),
        )),
    })
    script = Path(__file__).with_suffix(".scm").resolve()
    expression = (
        '(exec-global (lambda () (primitive-load '
        + json.dumps(str(script)) + ')))'
    )
    log_path = root / "runtime.log"
    with log_path.open("w") as log:
        process = subprocess.Popen(
            [str(args.binary.resolve()), "-H", "-X", "-x", expression],
            cwd=root, env=env, start_new_session=True,
            stdout=log, stderr=subprocess.STDOUT,
        )
        try:
            process.wait(timeout=120)
        except subprocess.TimeoutExpired as exc:
            raise RuntimeError(
                f"node move lifecycle timed out; see {log_path}") from exc
        finally:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()

    result_path = vault1 / "result.scm"
    result = result_path.read_text() if result_path.exists() else "No Scheme result"
    output = log_path.read_text(errors="replace")
    if (process.returncode or result != "#t" or
            "ATHENA-NODE-MOVE-LIFECYCLE-PASS" not in output):
        raise RuntimeError(
            f"node move lifecycle failed ({process.returncode}): {result}\n"
            f"{output[-18000:]}\nArtifacts: {root}"
        )
    print(f"ATHENA-NODE-MOVE-LIFECYCLE-PASS; same-vault move/repeat/"
          f"cross-actor undo-redo/cross-vault copy; {root}")
    if artifacts is None:
        shutil.rmtree(root)


if __name__ == "__main__":
    main()
