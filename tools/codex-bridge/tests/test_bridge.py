#!/usr/bin/env python3
import os
import pathlib
import subprocess
import sys
import tempfile


bridge = pathlib.Path(sys.argv[1])
fake_codex = pathlib.Path(sys.argv[2])
fake_codex.chmod(fake_codex.stat().st_mode | 0o111)

with tempfile.TemporaryDirectory(prefix="athena-codex-test-") as directory:
    root = pathlib.Path(directory)
    prompt = root / "prompt.txt"
    output = root / "output.txt"
    prompt.write_text("Continue $x$", encoding="utf-8")
    result = subprocess.run(
        [str(bridge), "--codex", str(fake_codex),
         "--codex-home", str(root / "home"), "--cwd", str(root / "work"),
         "--one-shot", "--input", str(prompt), "--output", str(output)],
        text=True, capture_output=True, timeout=10, check=False)
    if result.returncode != 0:
        raise SystemExit(
            f"bridge failed ({result.returncode}): {result.stderr}")
    if output.read_text(encoding="utf-8") != "continued formula":
        raise SystemExit("bridge did not assemble streamed agent deltas")

    session = subprocess.run(
        [str(bridge), "--codex", str(fake_codex),
         "--codex-home", str(root / "home"), "--cwd", str(root / "work")],
        input="Continue $x$\n", text=True, capture_output=True, timeout=10,
        check=False)
    if session.returncode != 0:
        raise SystemExit(
            f"session bridge failed ({session.returncode}): {session.stderr}")
    if "verbatim:continued formula" not in session.stdout:
        raise SystemExit("session bridge did not return a TeXmacs data frame")
