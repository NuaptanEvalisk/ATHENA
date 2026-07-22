#!/usr/bin/env python3
import base64
import json
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
    image = root / "codex-fig-12345678-1234-1234-1234-123456789abc-1.png"
    prompt.write_text("Continue $x$", encoding="utf-8")
    image.write_bytes(base64.b64decode(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk"
        "/w8AAusB9Wl2nJ8AAAAASUVORK5CYII="))
    image_env = os.environ.copy()
    image_env["ATHENA_FAKE_EXPECT_IMAGE"] = str(image.resolve())
    result = subprocess.run(
        [str(bridge), "--codex", str(fake_codex),
         "--codex-home", str(root / "home"), "--cwd", str(root / "work"),
         "--one-shot", "--input", str(prompt), "--output", str(output),
         "--image", str(image)],
        text=True, capture_output=True, timeout=10, check=False,
        env=image_env)
    if result.returncode != 0:
        raise SystemExit(
            f"bridge failed ({result.returncode}): {result.stderr}")
    if output.read_text(encoding="utf-8") != "continued formula":
        raise SystemExit("bridge did not assemble streamed agent deltas")

    models = subprocess.run(
        [str(bridge), "--codex", str(fake_codex),
         "--codex-home", str(root / "home"), "--list-models"],
        text=True, capture_output=True, timeout=10, check=False)
    if models.returncode != 0:
        raise SystemExit(
            f"model listing failed ({models.returncode}): {models.stderr}")
    catalog = json.loads(models.stdout)
    if [model["model"] for model in catalog] != ["gpt-test"]:
        raise SystemExit("bridge returned an unexpected model catalog")

    custom_output = root / "custom-output.txt"
    custom_env = os.environ.copy()
    custom_env["ATHENA_FAKE_EXPECT_CUSTOM"] = "1"
    custom = subprocess.run(
        [str(bridge), "--codex", str(fake_codex),
         "--codex-home", str(root / "home"), "--cwd", str(root / "work"),
         "--one-shot", "--input", str(prompt), "--output", str(custom_output),
         "--model", "gpt-test", "--effort", "low",
         "--service-tier", "priority",
         "--web-search"],
        text=True, capture_output=True, timeout=10, check=False,
        env=custom_env)
    if custom.returncode != 0:
        raise SystemExit(
            f"custom bridge failed ({custom.returncode}): {custom.stderr}")
    if custom_output.read_text(encoding="utf-8") != "continued formula":
        raise SystemExit("custom bridge did not return its completion")

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
