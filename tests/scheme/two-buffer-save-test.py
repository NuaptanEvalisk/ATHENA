#!/usr/bin/env python3
"""Exercise two real BufferActors saving concurrently with isolated profiles."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import tempfile


def run_case(args, mode):
    with tempfile.TemporaryDirectory(prefix=f"athena-save-{mode}-") as temporary:
        home = Path(temporary)
        system = home / "profile/system"
        system.mkdir(parents=True)
        (system / "sys_state.json").write_text(json.dumps({
            "format": "athena-system-state", "version": 1,
            "compatibility_version": "2.1.4",
            "tex": {"design_dpi": 600, "kpsepath": False, "kpsewhich": False,
                    "make_pk": False, "make_tfm": False}}))
        env = dict(os.environ)
        env.update({
            "HOME": str(home), "ATHENA_HOME_PATH": str(home / "profile"),
            "XDG_CONFIG_HOME": str(home / "config"),
            "XDG_CACHE_HOME": str(home / "cache"),
            "XDG_DATA_HOME": str(home / "data"),
            "ATHENA_PATH": str(args.resources.resolve()),
            "QT_QPA_PLATFORM": "offscreen", "GUILE_AUTO_COMPILE": "0",
            "ATHENA_GUILE_CACHE_PATH": str(home / "scheme-cache"),
            "ATHENA_SAVE_TEST_ROOT": str(home), "ATHENA_SAVE_TEST_MODE": mode,
            "GUILE_LOAD_PATH": str(args.runtime / "share/guile/3.0"),
            "GUILE_LOAD_COMPILED_PATH": str(args.runtime / "lib/guile/3.0/ccache"),
            "LD_LIBRARY_PATH": ":".join([str(args.runtime / "lib"),
                                          str(args.resources / "lib"),
                                          env.get("LD_LIBRARY_PATH", "")])})
        script = args.script.resolve()
        expression = '(exec-global (lambda () (primitive-load ' + json.dumps(str(script)) + ')))'
        timed_out = False
        with (home / "output.log").open("w") as log:
            command = [str(args.binary.resolve()), "-H", "-X", "-x", expression]
            if args.gdb:
                command = ["gdb", "-nx", "-batch", "-iex", "set auto-load off",
                           "-iex", "set debuginfod enabled off",
                           "-ex", "handle SIGPWR nostop noprint pass",
                           "-ex", "handle SIGXCPU nostop noprint pass",
                           "-ex", "run", "-ex", "thread apply all bt",
                           "-ex", "quit", "--args"] + command
            process = subprocess.Popen(
                command,
                cwd=home, env=env, start_new_session=True,
                stdout=log, stderr=subprocess.STDOUT)
            try:
                process.wait(timeout=35)
            except subprocess.TimeoutExpired:
                timed_out = True
                if args.capture_stacks and not args.gdb:
                    with (home / "stacks.log").open("w") as stacks:
                        try:
                            subprocess.run(
                                ["gdb", "-nx", "-batch", "-iex", "set auto-load off",
                                 "-iex", "set debuginfod enabled off", "-p", str(process.pid),
                                 "-ex", "thread apply all bt", "-ex", "detach"],
                                stdout=stacks, stderr=subprocess.STDOUT, timeout=20)
                        except subprocess.TimeoutExpired:
                            stacks.write("\nDebugger capture timed out.\n")
            finally:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
        errors = []
        for tag in ("first", "second"):
            result = home / f"{tag}.result"
            if not result.exists() or result.read_text() != "#t":
                errors.append(f"{tag}: {result.read_text() if result.exists() else 'no completion'}")
            document = home / f"{tag}.ath"
            text = document.read_text() if document.exists() else ""
            if f"{tag.upper()} BUFFER" not in text or "<hlink|" not in text:
                errors.append(f"{tag}: missing or incorrect saved document")
            if mode == "manual-approve":
                labels = re.findall(r"<label\|([^<>]+)>", text)
                upper = {label[:-2] for label in labels
                         if label.startswith("definition:") and label.endswith(" {")}
                lower = {label[:-2] for label in labels
                         if label.startswith("definition:") and label.endswith(" }")}
                if not upper or upper != lower:
                    errors.append(f"{tag}: approved definition anchor pair not saved")
        if args.artifacts:
            shutil.copytree(home, args.artifacts / mode, dirs_exist_ok=True)
        if timed_out or process.returncode or errors:
            raise RuntimeError(f"{mode}: timeout={timed_out}, exit={process.returncode}\n"
                               + "\n".join(errors) + "\n" + (home / "output.log").read_text())
        print(f"PASS: {mode}, two actors, six saves, correct contents and source ownership",
              flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path)
    parser.add_argument("--script", type=Path,
                        default=Path(__file__).with_suffix(".scm"))
    parser.add_argument("--gdb", action="store_true")
    parser.add_argument("--capture-stacks", action="store_true")
    parser.add_argument("--mode", choices=("plain", "manual-decline", "manual-approve"))
    args = parser.parse_args()
    args.runtime = args.runtime.resolve()
    args.resources = args.resources.resolve()
    for mode in ([args.mode] if args.mode else ("plain", "manual-decline", "manual-approve")):
        run_case(args, mode)


if __name__ == "__main__":
    main()
