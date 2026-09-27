#!/usr/bin/env python3
"""Run the source-lifecycle integration block in one isolated owner runtime."""

import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    args = parser.parse_args()
    artifacts = args.artifacts.resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    home = Path(tempfile.mkdtemp(prefix="source-lifecycle-", dir=artifacts))
    system = home / "profile/system"
    system.mkdir(parents=True)
    (system / "sys_state.json").write_text(json.dumps({
        "format": "athena-system-state", "version": 2,
        "compatibility_version": "2.1.4",
    }))
    source = home / "source.ath"
    original = ("<TeXmacs|2.1.4>\n<style|generic>\n"
                "<\\body>\nUnmodified disk document.\n</body>\n").encode()
    source.write_bytes(original)
    runtime = args.runtime.resolve()
    resources = args.resources.resolve()
    env = dict(os.environ)
    env.update({
        "HOME": str(home), "ATHENA_HOME_PATH": str(home / "profile"),
        "XDG_CONFIG_HOME": str(home / "config"),
        "XDG_CACHE_HOME": str(home / "cache"),
        "XDG_DATA_HOME": str(home / "data"),
        "ATHENA_PATH": str(resources), "QT_QPA_PLATFORM": "offscreen",
        "GUILE_AUTO_COMPILE": "0", "ATHENA_NODE_LIFECYCLE_ROOT": str(home),
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
            raise RuntimeError(f"Owner integration check timed out; see {log_path}") from exc
        finally:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
    result_path = home / "result.scm"
    result = result_path.read_text() if result_path.exists() else "No Scheme result"
    output = log_path.read_text(errors="replace")
    if process.returncode or result != "#t" or "ATHENA-NODE-SOURCE-LIFECYCLE-PASS" not in output:
        raise RuntimeError(f"Source lifecycle failed ({process.returncode}): {result}\n"
                           f"{output[-16000:]}\nArtifacts: {home}")
    if source.read_bytes() != original:
        raise RuntimeError(f"Rejected v1 save changed the original file: {source}")
    print(f"ATHENA-NODE-SOURCE-LIFECYCLE-PASS; original disk bytes preserved; {home}")


if __name__ == "__main__":
    main()
