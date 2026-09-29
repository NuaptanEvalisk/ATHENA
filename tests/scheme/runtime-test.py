#!/usr/bin/env python3
"""Run one Scheme regression in an isolated ATHENA runtime."""

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
    parser.add_argument("--script", type=Path, required=True)
    parser.add_argument("--setup-script", type=Path)
    parser.add_argument("--root-env", action="append", default=[])
    parser.add_argument("--timeout", type=int, default=90)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="athena-scheme-runtime-") as temporary:
        home = Path(temporary)
        system = home / "profile/system"
        system.mkdir(parents=True)
        (system / "sys_state.json").write_text(json.dumps({
            "format": "athena-system-state", "version": 3,
        }))
        runtime = args.runtime.resolve()
        resources = args.resources.resolve()
        environment = dict(os.environ)
        environment.update({
            "HOME": str(home), "ATHENA_HOME_PATH": str(home / "profile"),
            "XDG_CONFIG_HOME": str(home / "config"),
            "XDG_CACHE_HOME": str(home / "cache"),
            "XDG_DATA_HOME": str(home / "data"),
            "ATHENA_PATH": str(resources), "QT_QPA_PLATFORM": "offscreen",
            "GUILE_AUTO_COMPILE": "0",
            "GUILE_LOAD_PATH": str(runtime / "share/guile/3.0"),
            "GUILE_LOAD_COMPILED_PATH": str(runtime / "lib/guile/3.0/ccache"),
            "LD_LIBRARY_PATH": ":".join((
                str(runtime / "lib"), str(resources / "lib"),
                environment.get("LD_LIBRARY_PATH", ""),
            )),
        })
        for name in args.root_env:
            environment[name] = str(home)

        report = home / "result.scm"
        script = args.script.resolve()
        expression = (
            '(let ((result (catch #t '
            f'(lambda () (primitive-load {json.dumps(str(script))}) #t) '
            '(lambda args args)))) '
            f'(call-with-output-file {json.dumps(str(report))} '
            '(lambda (port) (write result port))) '
            '(exec-global (lambda () (quit-TeXmacs))))')
        if args.setup_script:
            expression = (
                '(exec-global (lambda () '
                f'(primitive-load {json.dumps(str(args.setup_script.resolve()))}) '
                f'(exec-buffer (current-buffer) (lambda () {expression}))))')

        log_path = home / "runtime.log"
        with log_path.open("w") as log:
            process = subprocess.Popen(
                [str(args.binary.resolve()), "-H", "-X", "-x", expression],
                cwd=home, env=environment, start_new_session=True,
                stdout=log, stderr=subprocess.STDOUT)
            timed_out = False
            try:
                process.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
            finally:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()

        result = report.read_text() if report.exists() else "No Scheme result"
        output = log_path.read_text(errors="replace")
        if timed_out or process.returncode or result != "#t":
            status = "timeout" if timed_out else str(process.returncode)
            raise RuntimeError(
                f"{script.name} failed ({status}): {result}\n{output[-20000:]}"
            )
        print(f"ATHENA-SCHEME-RUNTIME-PASS: {script.name}")


if __name__ == "__main__":
    main()
