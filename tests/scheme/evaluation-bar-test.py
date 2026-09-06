#!/usr/bin/env python3
"""Run evaluation-bar editor checks with an isolated ATHENA profile."""

import argparse
import json
import os
from pathlib import Path
import signal
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path)
    parser.add_argument("--script", type=Path,
                        default=Path(__file__).with_suffix(".scm"))
    args = parser.parse_args()
    script = args.script.resolve()
    with tempfile.TemporaryDirectory(prefix="athena-evaluation-bar-") as temporary:
        home = Path(temporary)
        system = home / "profile/system"
        system.mkdir(parents=True)
        (system / "sys_state.json").write_text(json.dumps({
            "format": "athena-system-state", "version": 1,
            "compatibility_version": "2.1.4",
            "tex": {"design_dpi": 600, "kpsepath": False,
                    "kpsewhich": False, "make_pk": False, "make_tfm": False},
        }))
        environment = dict(os.environ)
        environment.update({
            "HOME": str(home), "ATHENA_HOME_PATH": str(home / "profile"),
            "XDG_CONFIG_HOME": str(home / "config"),
            "XDG_CACHE_HOME": str(home / "cache"),
            "XDG_DATA_HOME": str(home / "data"),
            "ATHENA_PATH": str(args.resources.resolve()),
            "QT_QPA_PLATFORM": "offscreen", "GUILE_AUTO_COMPILE": "0",
            "ATHENA_GUILE_CACHE_PATH": str(home / "scheme-cache"),
            "GUILE_LOAD_PATH": str(args.runtime / "share/guile/3.0"),
            "GUILE_LOAD_COMPILED_PATH": str(args.runtime / "lib/guile/3.0/ccache"),
            "LD_LIBRARY_PATH": ":".join([str(args.runtime / "lib"),
                                          str(args.resources / "lib"),
                                          os.environ.get("LD_LIBRARY_PATH", "")]),
        })
        report = home / "result.scm"
        expression = (
            '(let ((result (catch #t '
            f'(lambda () (primitive-load {json.dumps(str(script))}) #t) '
            '(lambda args args)))) '
            f'(call-with-output-file {json.dumps(str(report))} '
            '(lambda (port) (write result port))) '
            '(exec-global (lambda () (exit (if (eq? result #t) 0 1)))))')
        with tempfile.TemporaryFile(mode="w+t") as log:
            process = subprocess.Popen(
                [str(args.binary.resolve()), "-H", "-X", "-x",
                 expression],
                cwd=home, env=environment, start_new_session=True,
                stdout=log, stderr=subprocess.STDOUT, text=True)
            try:
                process.wait(timeout=60)
            finally:
                # A crash reporter can outlive the test process.
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
            log.seek(0)
            output = log.read()
        result = report.read_text() if report.exists() else "No Scheme result"
        if process.returncode or result != "#t":
            output += "\n" + result
            trace = home / "trace.scm"
            if trace.exists():
                output += "\n" + trace.read_text()
            raise RuntimeError(f"{script.name} failed ({process.returncode}):\n{output}")
        pdf = home / "evaluation.pdf"
        if not pdf.exists() or not pdf.read_bytes().startswith(b"%PDF-"):
            raise RuntimeError(f"Evaluation PDF export failed:\n{output}")
        if args.artifacts:
            args.artifacts.mkdir(parents=True, exist_ok=True)
            shutil.copy2(pdf, args.artifacts / pdf.name)
        print(f"ATHENA-PASS: {script.name} assertions and PDF export")


if __name__ == "__main__":
    main()
