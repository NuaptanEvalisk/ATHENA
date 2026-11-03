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
parser.add_argument("--build-dir", required=True, type=Path)
parser.add_argument("--binary", type=Path)
parser.add_argument("--resources", type=Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="athena-crash-report-") as temporary:
    root = Path(temporary)
    binary = root / "probe"
    subprocess.run([
        args.compiler, "-std=c++17", "-O1", "-pthread", "-I", str(args.source_dir / "src"),
        "-I", str(args.build_dir / "src/System"),
        str(args.source_dir / "tests/runtime/crash-report-probe.cc"),
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
        ("terminate", signal.SIGABRT, "Main"),
        ("non-standard", signal.SIGABRT, "Main"),
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
            assert "type=" in report and "what=probe" in report, report
        if mode == "terminate":
            assert "no current exception" in report, report
        if mode == "non-standard":
            assert "non-standard exception; type=" in report, report
        assert reports[0].stat().st_mode & 0o077 == 0
        print(f"PASS {mode}: preserved signal, thread identity and private crash record", flush=True)
