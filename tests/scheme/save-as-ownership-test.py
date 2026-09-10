#!/usr/bin/env python3
"""Verify actor-originated Save As publishes the rename to the GUI owner."""

import argparse
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--script", type=Path,
                        default=Path(__file__).with_suffix(".scm"))
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="athena-save-as-") as temporary:
        root = Path(temporary)
        (root / "profile/fonts").mkdir(parents=True)
        (root / "profile/system").mkdir(parents=True)
        env = dict(os.environ)
        env.update({
            "HOME": str(root),
            "ATHENA_HOME_PATH": str(root / "profile"),
            "ATHENA_PATH": str(args.resources.resolve()),
            "XDG_CONFIG_HOME": str(root / "config"),
            "XDG_CACHE_HOME": str(root / "cache"),
            "XDG_DATA_HOME": str(root / "data"),
            "ATHENA_GUILE_CACHE_PATH": str(root / "scheme-cache"),
            "ATHENA_SAVE_AS_TEST_ROOT": str(root),
            "GUILE_AUTO_COMPILE": "0",
            "QT_QPA_PLATFORM": "offscreen",
            "GUILE_LOAD_PATH": str(args.runtime / "share/guile/3.0"),
            "GUILE_LOAD_COMPILED_PATH": str(args.runtime / "lib/guile/3.0/ccache"),
            "LD_LIBRARY_PATH": ":".join([
                str(args.runtime / "lib"),
                str(args.resources / "lib"),
                env.get("LD_LIBRARY_PATH", ""),
            ]),
        })
        expression = (
            "(exec-global (lambda () (primitive-load "
            + repr(str(args.script.resolve())).replace("'", '"')
            + ")))"
        )
        log_path = root / "output.log"
        with log_path.open("w") as log:
            process = subprocess.Popen(
                [str(args.binary.resolve()), "-X", "-x", expression],
                cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT,
                start_new_session=True)
            try:
                result_path = root / "result.scm"
                deadline = time.monotonic() + 35
                while time.monotonic() < deadline and not result_path.exists():
                    if process.poll() is not None:
                        break
                    time.sleep(0.1)
                output = log_path.read_text()
                if not result_path.exists():
                    raise RuntimeError(
                        f"Save As verification did not complete; exit={process.poll()}\n{output}")
                result = result_path.read_text()
                expected = '(old-exists #f new-exists #t saved #t title "new.ath")'
                if result != expected:
                    raise RuntimeError(f"Unexpected Save As state: {result}\n{output}")
                if "GUI buffer registry accessed from a BufferActor" in output:
                    raise RuntimeError(output)
                print("PASS: Save As renames actor state, GUI registry, title, and file")
            finally:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()


if __name__ == "__main__":
    main()
