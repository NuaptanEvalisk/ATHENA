#!/usr/bin/env python3
"""Opt-in isolated ATHENA binding/ADS smoke; requires Xvfb and xdotool."""
import argparse
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path, default=ROOT / "build_qt6")
args = parser.parse_args()
home = Path(tempfile.mkdtemp(prefix="athena-audmap-pane-smoke-"))
system = home / "profile/system"
system.mkdir(parents=True)
(system / "sys_state.json").write_text(json.dumps({
    "format": "athena-system-state", "version": 1, "compatibility_version": "2.1.4",
    "tex": {"design_dpi": 600, "kpsepath": False, "kpsewhich": False,
            "make_pk": False, "make_tfm": False}}))
read_fd, write_fd = os.pipe()
xvfb = subprocess.Popen(["Xvfb", "-displayfd", str(write_fd), "-screen", "0",
                         "1200x850x24", "-nolisten", "tcp"], pass_fds=[write_fd],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
os.close(write_fd)
app = None
children = []
try:
    assert select.select([read_fd], [], [], 10)[0], "Xvfb startup timed out"
    display = ":" + os.read(read_fd, 80).decode().strip()
    env = dict(os.environ, DISPLAY=display, QT_QPA_PLATFORM="xcb", HOME=str(home),
               ATHENA_HOME_PATH=str(home / "profile"), ATHENA_PATH=str(ROOT / "ATHENA"),
               XDG_CONFIG_HOME=str(home / "config"), XDG_DATA_HOME=str(home / "data"),
               XDG_CACHE_HOME=str(home / "cache"), GUILE_AUTO_COMPILE="0",
               ATHENA_GUILE_CACHE_PATH=str(home / "scheme-cache"))
    env["LD_LIBRARY_PATH"] = ":".join([
        str(ROOT / "ATHENA/lib"), str(ROOT / "ATHENA/lib/athena-guile/lib"),
        os.environ.get("LD_LIBRARY_PATH", "")])
    expr = '(delayed (:pause 1500) (audmap-repl-show) (display "AUDMAP-PANE-BINDING-OK\\n"))'
    with (home / "app.log").open("w") as log:
        app = subprocess.Popen([str(args.build / "src/ATHENA.bin"), "-X", "-x", expr],
            cwd=ROOT / "ATHENA/bin", env=env, stdout=log, stderr=subprocess.STDOUT,
            start_new_session=True)
        deadline = time.monotonic() + 60
        while True:
            assert app.poll() is None, f"ATHENA exited {app.returncode}: {home / 'app.log'}"
            result = subprocess.run(["xdotool", "search", "--onlyvisible", "--name",
                                     "^ATHENA Interop Connection$"], env=env,
                                    capture_output=True, text=True)
            if result.returncode == 0:
                break
            if time.monotonic() >= deadline:
                subprocess.run(["import", "-window", "root", str(home / "failure.png")], env=env)
                raise AssertionError(f"REPL authorization did not appear: {home}")
            time.sleep(.2)
        assert "AUDMAP-PANE-BINDING-OK" in (home / "app.log").read_text()
        for child in Path(f"/proc/{app.pid}/task/{app.pid}/children").read_text().split():
            cmd = Path(f"/proc/{child}/cmdline").read_bytes().split(b"\0")
            if cmd and Path(os.fsdecode(cmd[0])).name == "athena-audmap-repl":
                children.append(int(child))
        assert len(children) == 1, "Expected exactly one REPL PTY child"
        subprocess.run(["import", "-window", "root", str(home / "pane.png")],
                       env=env, check=True)
        print(f"PASS: generated binding, ADS pane, PTY child, current-instance authorization. Artifacts: {home}")
finally:
    os.close(read_fd)
    for pid in children:
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
    if app and app.poll() is None:
        os.killpg(app.pid, signal.SIGTERM)
        try:
            app.wait(timeout=10)
        except subprocess.TimeoutExpired:
            os.killpg(app.pid, signal.SIGKILL)
            app.wait()
    xvfb.terminate()
    xvfb.wait(timeout=10)
