#!/usr/bin/env python3
"""Atomically deploy a Linux executable, retaining retired builds for core dumps."""
import argparse
import filecmp
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time


def build_id(path, required=True):
    notes = subprocess.check_output(["readelf", "-n", str(path)], text=True)
    match = re.search(r"Build ID: ([0-9a-f]+)", notes)
    if not match:
        if required:
            raise RuntimeError(f"No ELF build ID: {path}")
        stat = path.stat()
        return f"legacy-{stat.st_dev}-{stat.st_ino}"
    return match.group(1)


def running_executables():
    result = {}
    for process in Path("/proc").iterdir():
        if not process.name.isdecimal():
            continue
        try:
            stat = (process / "exe").stat()
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            continue
        result.setdefault((stat.st_dev, stat.st_ino), []).append(int(process.name))
    return result


def write_metadata(path, value):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    temporary.replace(path)


def prune(archive, destination, active):
    # A core is written asynchronously; do not prune a just-retired build before
    # systemd-coredump has had a chance to finish and publish its journal entry.
    try:
        command = subprocess.run(
            ["coredumpctl", "--json=short", "list", f"COREDUMP_EXE={destination}"],
            capture_output=True, text=True, timeout=15, env={**os.environ, "LC_ALL": "C"})
        if command.returncode == 0:
            cores = json.loads(command.stdout)
        elif command.returncode == 1 and "No coredumps found" in command.stderr:
            cores = []
        else:
            return
    except (OSError, ValueError, subprocess.TimeoutExpired):
        return  # Without a core inventory, retain the debugging evidence.
    for metadata in archive.glob("*/build.json"):
        saved = json.loads(metadata.read_text())
        if time.time() - saved["retired_at"] < 86400:
            continue
        live = set()
        for device, inode in saved["instances"]:
            live.update(active.get((device, inode), []))
        archived = (metadata.parent / destination.name).stat()
        live.update(active.get((archived.st_dev, archived.st_ino), []))
        if live:
            saved["pids"] = sorted(set(saved["pids"]) | live)
            write_metadata(metadata, saved)
            continue
        needed = any(
            core["corefile"] not in ("missing", "none")
            and core["time"] / 1e6 >= saved["deployed_at"]
            and (core["time"] / 1e6 <= saved["retired_at"] or core["pid"] in saved["pids"])
            for core in cores)
        if not needed:
            shutil.rmtree(metadata.parent)
            print(f"pruned    {metadata.parent}")


def install(source, destination):
    source = source.resolve()
    destination = destination.absolute()
    destination.parent.mkdir(parents=True, exist_ok=True)
    archive = destination.parent / ".retired" / destination.name
    active = running_executables()
    retired = None
    new_id = build_id(source)
    if destination.exists():
        old_id = build_id(destination, required=False)
        if old_id == new_id and filecmp.cmp(source, destination, shallow=False):
            prune(archive, destination, active)
            print(f"unchanged {destination}")
            return
        stat = destination.stat()
        saved_dir = archive / old_id
        saved_dir.mkdir(parents=True, exist_ok=True)
        executable = saved_dir / destination.name
        if not executable.exists():
            # Independent bytes: a later installer that overwrites the runtime
            # inode must not overwrite the archived image through a hard link.
            shutil.copy2(destination, executable)
        metadata = saved_dir / "build.json"
        saved = json.loads(metadata.read_text()) if metadata.exists() else {
            "build_id": old_id, "deployed_at": stat.st_mtime, "pids": [], "instances": []}
        instance = [stat.st_dev, stat.st_ino]
        if instance not in saved["instances"]:
            saved["instances"].append(instance)
        saved["retired_at"] = time.time()
        saved["pids"] = sorted(set(saved["pids"]) | set(active.get((stat.st_dev, stat.st_ino), [])))
        write_metadata(metadata, saved)
        retired = (metadata, saved, stat)
        print(f"retained  {executable}")
    descriptor, temporary = tempfile.mkstemp(prefix=f".{destination.name}.", dir=destination.parent)
    os.close(descriptor)
    try:
        shutil.copyfile(source, temporary)
        os.chmod(temporary, 0o755)
        os.replace(temporary, destination)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    print(f"deployed  {destination} (build ID {new_id})")
    active = running_executables()
    if retired:
        metadata, saved, stat = retired
        saved["retired_at"] = time.time()
        saved["pids"] = sorted(set(saved["pids"]) | set(active.get((stat.st_dev, stat.st_ino), [])))
        write_metadata(metadata, saved)
    prune(archive, destination, active)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    arguments = parser.parse_args()
    install(arguments.source, arguments.destination)
