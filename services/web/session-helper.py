#!/usr/bin/env python3
"""Constrained file operations inside one Web-Accessible ATHENA sandbox."""

from __future__ import annotations

import json
import os
from pathlib import Path
import stat
import sys
import tarfile


HOME = Path("/home/ATHENA-User")
UPLOAD = HOME / "Desktop" / "Upload"
DOWNLOAD = HOME / "Desktop" / "Download"


def valid_component(component: str) -> bool:
    return (
        component not in {"", ".", ".."}
        and "/" not in component
        and "\x00" not in component
        and len(component.encode("utf-8")) <= 240
    )


def regular_download(relative: str) -> Path:
    parts = relative.split("/")
    if not parts or not all(valid_component(part) for part in parts):
        raise ValueError("invalid path")
    current = DOWNLOAD
    for index, part in enumerate(parts):
        current = current / part
        info = current.lstat()
        if stat.S_ISLNK(info.st_mode):
            raise ValueError("symbolic links are not downloadable")
        if index + 1 < len(parts) and not stat.S_ISDIR(info.st_mode):
            raise ValueError("non-directory path component")
    if not stat.S_ISREG(current.lstat().st_mode):
        raise ValueError("not a regular file")
    return current


def download_entries() -> list[dict[str, object]]:
    result: list[dict[str, object]] = []
    for root, directories, files in os.walk(DOWNLOAD, followlinks=False):
        root_path = Path(root)
        directories[:] = [
            name
            for name in directories
            if valid_component(name)
            and not (root_path / name).is_symlink()
        ]
        for name in files:
            if not valid_component(name):
                continue
            path = root_path / name
            info = path.lstat()
            if stat.S_ISREG(info.st_mode):
                result.append(
                    {
                        "path": path.relative_to(DOWNLOAD).as_posix(),
                        "size": info.st_size,
                        "modified": int(info.st_mtime),
                    }
                )
    result.sort(key=lambda item: str(item["path"]).casefold())
    return result


def list_downloads() -> None:
    print(json.dumps(download_entries(), ensure_ascii=False))


def usage() -> None:
    total = 0
    for root, directories, files in os.walk(HOME, followlinks=False):
        root_path = Path(root)
        directories[:] = [
            name for name in directories if not (root_path / name).is_symlink()
        ]
        for name in files:
            path = root_path / name
            try:
                info = path.lstat()
            except FileNotFoundError:
                continue
            if stat.S_ISREG(info.st_mode):
                total += info.st_size
    print(total)


def finish_upload(incoming: str, destination: str) -> None:
    if not valid_component(incoming) or not valid_component(destination):
        raise ValueError("invalid upload filename")
    source = UPLOAD / incoming
    target = UPLOAD / destination
    info = source.lstat()
    if not stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode):
        raise ValueError("upload staging object is not a regular file")
    os.replace(source, target)


def archive_downloads(destination: str) -> None:
    output = Path(destination)
    if output.parent != Path("/tmp") or not valid_component(output.name):
        raise ValueError("invalid archive destination")
    with tarfile.open(output, "w:gz", format=tarfile.PAX_FORMAT) as archive:
        for item in download_entries():
            source = regular_download(str(item["path"]))
            archive.add(
                source,
                arcname=str(item["path"]),
                recursive=False,
            )


def main() -> int:
    try:
        command = sys.argv[1]
        if command == "list" and len(sys.argv) == 2:
            list_downloads()
        elif command == "usage" and len(sys.argv) == 2:
            usage()
        elif command == "check" and len(sys.argv) == 3:
            regular_download(sys.argv[2])
        elif command == "finish-upload" and len(sys.argv) == 4:
            finish_upload(sys.argv[2], sys.argv[3])
        elif command == "archive" and len(sys.argv) == 3:
            archive_downloads(sys.argv[2])
        else:
            raise ValueError("invalid helper invocation")
    except (IndexError, OSError, ValueError) as error:
        print(f"athena-web-session-helper: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
