#!/usr/bin/env python3
"""Build a relocatable standalone archive for an ATHENA Linux service."""

from __future__ import annotations

import argparse
import gzip
import os
import re
import shutil
import stat
import subprocess
import tarfile
import tempfile
from pathlib import Path


GLIBC_EXCLUDE = {
    "ld-linux-x86-64.so.2",
    "libBrokenLocale.so.1",
    "libanl.so.1",
    "libc.so.6",
    "libdl.so.2",
    "libm.so.6",
    "libmvec.so.1",
    "libnsl.so.1",
    "libnss_compat.so.2",
    "libnss_dns.so.2",
    "libnss_files.so.1",
    "libnss_files.so.2",
    "libpthread.so.0",
    "libresolv.so.2",
    "librt.so.1",
    "libthread_db.so.1",
    "libutil.so.1",
}

SERVICE_LAYOUT = {
    "transmitter": {
        "executable": "athena-transmitter",
        "directory": "ATHENA-Transmitter",
        "source": "tools/athena-transmitter",
        "extras": ("README.md", "deploy"),
    },
    "web-server": {
        "executable": "athena-web-server",
        "directory": "ATHENA-Web-Server",
        "source": "tools/athena-web-server",
        "extras": (
            "README.md",
            "Containerfile",
            "build-image.sh",
            "container-entrypoint.sh",
            "session-helper.py",
            "start-athena.sh",
            "deploy",
            "web",
        ),
    },
}


def output(args: list[str]) -> str:
    return subprocess.check_output(
        args, text=True, stderr=subprocess.DEVNULL
    )


def is_elf(path: Path) -> bool:
    with path.open("rb") as stream:
        return stream.read(4) == b"\x7fELF"


def dependencies(path: Path) -> list[Path]:
    result = []
    for line in output(["ldd", str(path)]).splitlines():
        line = line.strip()
        if "not found" in line:
            raise RuntimeError(f"unresolved dependency for {path}: {line}")
        match = re.search(r"=>\s+(/[^ ]+)", line)
        if not match:
            match = re.match(r"(/[^ ]+)", line)
        if match:
            result.append(Path(match.group(1)))
    return result


def soname(path: Path) -> str | None:
    text = output(["readelf", "-d", str(path)])
    match = re.search(r"Library soname: \[(.+?)\]", text)
    return match.group(1) if match else None


def copy_dependency_closure(executable: Path, libdir: Path) -> None:
    queue = [executable]
    copied: set[Path] = set()
    while queue:
        current = queue.pop(0)
        for dependency in dependencies(current):
            real = dependency.resolve()
            if real.name in GLIBC_EXCLUDE or real.name.startswith("libnss_"):
                continue
            if real in copied:
                continue
            copied.add(real)
            destination = libdir / real.name
            shutil.copy2(real, destination)
            name = soname(destination)
            if name and name != destination.name:
                link = libdir / name
                if link.exists() or link.is_symlink():
                    link.unlink()
                link.symlink_to(destination.name)
            queue.append(destination)


def set_relative_rpaths(executable: Path, libdir: Path) -> None:
    subprocess.run(
        [
            "patchelf", "--force-rpath", "--set-rpath",
            "$ORIGIN/../lib", str(executable),
        ],
        check=True,
    )
    for library in sorted(libdir.iterdir()):
        if library.is_file() and not library.is_symlink() and is_elf(library):
            subprocess.run(
                [
                    "patchelf", "--force-rpath", "--set-rpath",
                    "$ORIGIN", str(library),
                ],
                check=True,
            )


def write_launcher(root: Path, executable: str) -> None:
    launcher = root / executable
    launcher.write_text(
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n"
        'root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"\n'
        f'exec "$root/bin/{executable}" "$@"\n'
    )
    launcher.chmod(0o755)


def normalized_tar_info(info: tarfile.TarInfo, epoch: int) -> tarfile.TarInfo:
    info.uid = 0
    info.gid = 0
    info.uname = ""
    info.gname = ""
    info.mtime = epoch
    return info


def create_archive(source: Path, archive: Path, epoch: int) -> None:
    temporary = archive.with_suffix(archive.suffix + ".tmp")
    temporary.parent.mkdir(parents=True, exist_ok=True)
    with temporary.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=epoch,
                           compresslevel=9) as compressed:
            with tarfile.open(fileobj=compressed, mode="w") as tar:
                for path in sorted(
                    [source, *source.rglob("*")],
                    key=lambda item: item.relative_to(source.parent).as_posix(),
                ):
                    arcname = path.relative_to(source.parent)
                    tar.add(
                        path,
                        arcname=arcname,
                        recursive=False,
                        filter=lambda info: normalized_tar_info(info, epoch),
                    )
    temporary.replace(archive)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--kind", choices=SERVICE_LAYOUT, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-epoch", type=int, required=True)
    args = parser.parse_args()

    layout = SERVICE_LAYOUT[args.kind]
    binary = args.binary.resolve()
    repo_root = args.repo_root.resolve()
    if not binary.is_file() or not is_elf(binary):
        raise SystemExit(f"service binary is missing or not ELF: {binary}")

    with tempfile.TemporaryDirectory(prefix="athena-service-package-") as temp:
        package_root = (
            Path(temp) / f"{layout['directory']}-{args.version}"
        )
        bindir = package_root / "bin"
        libdir = package_root / "lib"
        bindir.mkdir(parents=True)
        libdir.mkdir()
        executable = str(layout["executable"])
        installed = bindir / executable
        shutil.copy2(binary, installed)
        installed.chmod(
            installed.stat().st_mode |
            stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH
        )
        copy_dependency_closure(installed, libdir)
        set_relative_rpaths(installed, libdir)
        write_launcher(package_root, executable)

        source = repo_root / str(layout["source"])
        for relative_text in layout["extras"]:
            relative = Path(str(relative_text))
            src = source / relative
            dst = package_root / relative
            if src.is_dir():
                shutil.copytree(src, dst, symlinks=True)
            else:
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dst)

        if args.kind == "web-server":
            installed_web_parent = package_root / "share" / "ATHENA"
            installed_web_parent.mkdir(parents=True)
            (installed_web_parent / "web").symlink_to(
                Path("..") / ".." / "web"
            )

        smoke = subprocess.run(
            [str(package_root / executable), "--help"],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            env={
                **os.environ,
                "LD_LIBRARY_PATH": str(libdir),
            },
        )
        if smoke.returncode not in (0, 2) or "Usage:" not in smoke.stdout:
            raise RuntimeError(
                f"{executable} standalone smoke test failed "
                f"with status {smoke.returncode}:\n{smoke.stdout}"
            )
        create_archive(package_root, args.output.resolve(), args.source_epoch)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
