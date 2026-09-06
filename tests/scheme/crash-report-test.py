#!/usr/bin/env python3
"""Exercise the production signal reporter in disposable, core-disabled processes."""
import argparse
import json
import os
from pathlib import Path
import re
import resource
import signal
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", required=True)
parser.add_argument("--source-dir", required=True, type=Path)
parser.add_argument("--binary", type=Path)
parser.add_argument("--runtime", type=Path)
parser.add_argument("--resources", type=Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="athena-crash-report-") as temporary:
    root = Path(temporary)
    binary = root / "probe"
    subprocess.run([
        args.compiler, "-std=c++17", "-O1", "-pthread", "-I", str(args.source_dir / "src"),
        str(args.source_dir / "tests/scheme/crash-report-probe.cc"),
        str(args.source_dir / "src/System/Misc/crash_report.cpp"), "-o", str(binary),
    ], check=True, timeout=60)
    cases = [
        ("main", signal.SIGSEGV, "Main"),
        ("actor", signal.SIGSEGV, "BufferActor"),
        ("render", signal.SIGSEGV, "RenderService"),
        ("unregistered", signal.SIGSEGV, "Unregistered"),
        ("stack", signal.SIGSEGV, "BufferActor"),
        ("allocator", signal.SIGSEGV, "Main"),
        ("closed-stderr", signal.SIGSEGV, "Main"),
        ("full-stderr", signal.SIGSEGV, "Main"),
        ("uncaught", signal.SIGABRT, "Main"),
        ("fpe", signal.SIGFPE, "Main"),
        ("ill", signal.SIGILL, "Main"),
        ("bus", signal.SIGBUS, "Main"),
    ]
    for mode, expected_signal, role in cases:
        directory = root / mode
        process = subprocess.run([str(binary), str(directory), mode],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 timeout=5)
        assert process.returncode == -expected_signal, (mode, process.returncode, process.stderr)
        reports = list(directory.glob("fatal-*.log"))
        assert len(reports) == 1, (mode, reports)
        report = reports[0].read_text()
        assert f"fatal signal={expected_signal.value} " in report, (mode, report)
        assert f"role={role} " in report, (mode, report)
        assert re.search(r"tid=[1-9][0-9]* ", report), (mode, report)
        assert re.search(r"pc=0x[1-9a-f][0-9a-f]*", report), (mode, report)
        assert "Root path" not in report and "Physical selection" not in report
        if mode == "actor":
            assert "owner-actor=71 actor=71 view=91 command=111" in report, report
        if mode == "uncaught":
            assert "ATHENA fatal error: C++ termination" in report, report
        assert reports[0].stat().st_mode & 0o077 == 0
        print(f"PASS {mode}: preserved signal, thread identity and private crash record", flush=True)

    if args.binary:
        assert args.runtime and args.resources
        home = root / "runtime-home"
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
                str(args.resources / "lib"), os.environ.get("LD_LIBRARY_PATH", "")]),
        })
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        process = subprocess.Popen(
            [str(args.binary.resolve()), "-H", "-X", "-x", "(kill (getpid) 11)"],
            cwd=home, env=environment, start_new_session=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            output, _ = process.communicate(timeout=40)
        finally:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
        assert process.returncode == -signal.SIGSEGV, (process.returncode, output[-8000:])
        report = (system / "crash" / f"fatal-{process.pid}.log").read_text()
        assert "ATHENA fatal signal=11" in report, report
        assert "Editor state not inspected" in report, report
        assert "Root path" not in report and "Physical selection" not in report
        assert b"scm_dynstack_unwind_fluid" not in output, output[-8000:]
        print("PASS runtime: installed ATHENA handler terminates without Scheme unwinding", flush=True)
