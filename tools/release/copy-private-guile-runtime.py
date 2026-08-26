#!/usr/bin/env python3

import hashlib
import json
import os
import shutil
import sys
import tempfile
from pathlib import Path


def copy_tree(source: Path, destination: Path) -> None:
    if not source.is_dir():
        raise SystemExit(f"missing ATHENA Guile runtime directory: {source}")
    shutil.copytree(source, destination, dirs_exist_ok=True, symlinks=True)


def runtime_signature(source: Path) -> str:
    digest = hashlib.sha256()
    roots = [
        source / "bin/guile",
        source / "share/guile/3.0",
        source / "lib/guile/3.0",
    ]
    roots.extend(sorted((source / "lib").glob("libathena-guile.so*")))
    for root in roots:
        paths = [root]
        if root.is_dir():
            paths.extend(sorted(root.rglob("*")))
        for path in paths:
            relative = path.relative_to(source)
            stat = path.lstat()
            digest.update(os.fsencode(str(relative)))
            digest.update(f"\0{stat.st_mode}\0{stat.st_size}\0{stat.st_mtime_ns}\0".encode())
            if path.is_symlink():
                digest.update(os.fsencode(os.readlink(path)))
    return digest.hexdigest()


def runtime_is_current(destination: Path, signature: str) -> bool:
    marker = destination / ".athena-runtime.json"
    required = [
        destination / "bin/guile",
        destination / "lib/libathena-guile.so.1",
        destination / "share/guile/3.0/ice-9/boot-9.scm",
        destination / "lib/guile/3.0/ccache/ice-9/boot-9.go",
    ]
    if not marker.is_file() or not all(path.exists() for path in required):
        return False
    try:
        data = json.loads(marker.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False
    return data.get("signature") == signature


def validate_bytecode(athena: Path, runtime_id: str) -> None:
    bytecode_root = athena / "lib/athena-scheme"
    bytecode = bytecode_root / runtime_id
    marker = bytecode / ".complete"
    try:
        marker_lines = marker.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise SystemExit(f"missing ATHENA Scheme bytecode marker: {marker}: {error}")
    if len(marker_lines) != 2 or marker_lines[0] != runtime_id:
        raise SystemExit(f"invalid ATHENA Scheme bytecode marker: {marker}")
    try:
        expected = int(marker_lines[1])
    except ValueError as error:
        raise SystemExit(f"invalid ATHENA Scheme bytecode count: {marker}") from error
    actual = sum(1 for path in bytecode.rglob("*.go") if path.is_file())
    if actual != expected:
        raise SystemExit(
            f"incomplete ATHENA Scheme bytecode: expected {expected}, got {actual}")

    for candidate in bytecode_root.iterdir():
        if candidate.is_dir() and candidate.name != runtime_id:
            shutil.rmtree(candidate)


def main() -> int:
    if len(sys.argv) not in (3, 4):
        print("usage: copy-private-guile-runtime.py BUILD_RUNTIME ATHENA_DIR "
              "[RUNTIME_ID]",
              file=sys.stderr)
        return 2

    source = Path(sys.argv[1]).resolve()
    athena = Path(sys.argv[2]).resolve()
    destination = athena / "lib/athena-guile"
    runtime_id = sys.argv[3] if len(sys.argv) == 4 else None
    if runtime_id is not None:
        validate_bytecode(athena, runtime_id)

    signature = runtime_signature(source)
    if runtime_is_current(destination, signature):
        destination.chmod(0o755)
        return 0

    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(
        prefix=".athena-guile-", dir=destination.parent))
    # mkdtemp deliberately creates mode 0700 directories.  The assembled
    # runtime is later executed by unprivileged package/container users, so its
    # root must remain traversable after staging is atomically renamed.
    staging.chmod(0o755)
    backup = destination.parent / f".athena-guile-backup-{os.getpid()}"
    try:
        copy_tree(source / "share/guile/3.0",
                  staging / "share/guile/3.0")
        copy_tree(source / "lib/guile/3.0",
                  staging / "lib/guile/3.0")
        (staging / "bin").mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / "bin/guile", staging / "bin/guile")
        (staging / "lib").mkdir(parents=True, exist_ok=True)
        libraries = sorted((source / "lib").glob("libathena-guile.so*"))
        if not libraries:
            raise SystemExit(
                f"missing private Guile library under {source / 'lib'}")
        for library in libraries:
            target = staging / "lib" / library.name
            if library.is_symlink():
                target.symlink_to(library.readlink())
            else:
                shutil.copy2(library, target)

        required = [
            staging / "bin/guile",
            staging / "lib/libathena-guile.so.1",
            staging / "share/guile/3.0/ice-9/boot-9.scm",
            staging / "lib/guile/3.0/ccache/ice-9/boot-9.go",
        ]
        missing = [str(path) for path in required if not path.exists()]
        if missing:
            raise SystemExit("incomplete ATHENA Guile runtime:\n" +
                             "\n".join(missing))
        (staging / ".athena-runtime.json").write_text(
            json.dumps({"signature": signature}, sort_keys=True) + "\n",
            encoding="utf-8")

        if backup.exists():
            shutil.rmtree(backup)
        if destination.exists():
            destination.rename(backup)
        staging.rename(destination)
        if backup.exists():
            shutil.rmtree(backup)
    finally:
        if staging.exists():
            shutil.rmtree(staging)
        if backup.exists() and not destination.exists():
            backup.rename(destination)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
