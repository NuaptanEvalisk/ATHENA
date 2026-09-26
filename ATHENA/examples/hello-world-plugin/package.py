#!/usr/bin/env python3
"""Build a self-contained Hello World plugin with an uv-managed Python runtime.

Copyright (C) 2026 Nuaptan Felix Evalisk.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

PYTHON_VERSION = "3.13.15"
MSGPACK_VERSION = "1.1.2"
PYZMQ_VERSION = "27.1.0"


def run(*args):
    subprocess.run(args, check=True)


def copy_runtime(uv, staging):
    download = staging / ".python-download"
    run(uv, "python", "install", PYTHON_VERSION,
        "--install-dir", str(download), "--no-bin", "--reinstall", "--quiet")
    candidates = [p for p in download.glob(f"cpython-{PYTHON_VERSION}-*")
                  if p.is_dir() and not p.is_symlink()]
    if len(candidates) != 1:
        raise RuntimeError("uv did not produce exactly one standalone Python runtime")
    source = candidates[0]
    runtime = staging / "runtime"
    (runtime / "bin").mkdir(parents=True)
    (runtime / "lib").mkdir()
    shutil.copy2(source / "bin" / "python3.13", runtime / "bin" / "python3.13")
    shutil.copy2(source / "lib" / "libpython3.13.so.1.0",
                 runtime / "lib" / "libpython3.13.so.1.0")
    shutil.copytree(source / "lib" / "python3.13", runtime / "lib" / "python3.13",
                    ignore=shutil.ignore_patterns("site-packages", "__pycache__", "*.pyc"))
    site = runtime / "site-packages"
    site.mkdir()
    run(uv, "pip", "install", "--target", str(site),
        "--python", str(runtime / "bin" / "python3.13"),
        "--only-binary", ":all:", "--link-mode", "copy",
        f"msgpack=={MSGPACK_VERSION}", f"pyzmq=={PYZMQ_VERSION}", "--quiet")
    (site / ".lock").unlink(missing_ok=True)
    shutil.rmtree(download)


def copy_sources(example, staging):
    for name in ("manifest.json", "plugin_exec", "README.md", "LICENSE.txt"):
        shutil.copy2(example / name, staging / name)
    sdk = staging / "sdk" / "athena_audmap"
    sdk.mkdir(parents=True)
    for source in sorted((example / "sdk" / "athena_audmap").glob("*.py")):
        shutil.copy2(source, sdk / source.name)


def validate_regular_tree(staging):
    files = 0
    for path in staging.rglob("*"):
        if path.is_symlink():
            raise RuntimeError(f"Self-contained package contains a symlink: {path}")
        if path.is_file():
            files += 1
    if files > 10000:
        raise RuntimeError("Self-contained package exceeds ATHENA's file-count limit")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="Destination ZIP (replaced if it exists)")
    args = parser.parse_args()
    example = Path(__file__).resolve().parent
    uv = shutil.which("uv")
    if uv is None:
        raise SystemExit("uv is required to build the self-contained Hello World plugin")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.unlink(missing_ok=True)
    with tempfile.TemporaryDirectory(prefix="athena-hello-world-") as temporary:
        staging = Path(temporary) / "package"
        staging.mkdir()
        copy_sources(example, staging)
        copy_runtime(uv, staging)
        validate_regular_tree(staging)
        with ZipFile(args.output, "w", compression=ZIP_DEFLATED) as archive:
            for source in sorted(p for p in staging.rglob("*") if p.is_file()):
                archive.write(source, source.relative_to(staging))
    print(args.output.resolve())


if __name__ == "__main__":
    main()
