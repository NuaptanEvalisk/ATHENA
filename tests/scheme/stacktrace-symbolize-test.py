#!/usr/bin/env python3
"""Exercise actual addr2line resolution and bounded failure without an editor."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", required=True)
parser.add_argument("--source-dir", required=True, type=Path)
args = parser.parse_args()
assert shutil.which("addr2line"), "addr2line is required for this regression"
source = args.source_dir / "src"
probe = args.source_dir / "tests/scheme/stacktrace-symbolize-probe.cc"
with tempfile.TemporaryDirectory(prefix="athena-stacktrace-") as temporary:
    root = Path(temporary)
    library = root / "library with spaces.so"
    common = [args.compiler, "-std=c++17", "-g", "-O0", "-I", str(source)]
    subprocess.run(common + ["-fPIC", "-shared", "-DPROBE_LIBRARY", str(probe),
        str(source / "System/Misc/stacktrace_symbolize.cpp"),
        "-ldl", "-o", str(library)], check=True, timeout=60)
    for kind, flags in [("pie", ["-fPIE", "-pie"]),
                        ("exec", ["-fno-pie", "-no-pie"])]:
        binary = root / kind
        subprocess.run(common + flags + [str(probe), str(library),
            "-o", str(binary)], check=True, timeout=60)
        result = subprocess.run([str(binary)], check=True, capture_output=True,
                                text=True, timeout=8)
        assert "library_frame at " in result.stdout, result.stdout
        assert "main at " in result.stdout, result.stdout
        assert "stacktrace-symbolize-probe.cc:" in result.stdout, result.stdout
        environment = dict(os.environ, PATH=str(root))
        missing = subprocess.run([str(binary)], env=environment, check=True,
                                 capture_output=True, timeout=8)
        assert not missing.stdout, missing.stdout
        fake = root / "addr2line"
        fake.write_text(f"#!{sys.executable}\nimport time\ntime.sleep(30)\n")
        fake.chmod(0o700)
        before = time.monotonic()
        hung = subprocess.run([str(binary)], env=environment, check=True,
                              capture_output=True, timeout=8)
        assert time.monotonic() - before < 5, "resolver exceeded its time budget"
        assert not hung.stdout, hung.stdout
        fake.unlink()
        print(f"PASS {kind}: executable/shared-library locations, missing tool, timeout")
