#!/usr/bin/env python3
"""Copy and validate ATHENA runtime trees for redistribution."""

from __future__ import annotations

import argparse
import os
import shutil
import sys
from pathlib import Path, PurePosixPath


MODEL_SUFFIXES = {
    ".ckpt",
    ".gguf",
    ".onnx",
    ".pt",
    ".pth",
    ".safetensors",
}

CACHE_DIRECTORY_NAMES = {
    ".uv-cache",
    ".venv",
    "__pycache__",
}

LINUX_SERVICE_EXECUTABLES = (
    PurePosixPath("bin/athena-materials-engine"),
    PurePosixPath("bin/athena-transmitter"),
    PurePosixPath("bin/athena-web-server"),
)

LINUX_WEB_ASSETS = (
    PurePosixPath("share/ATHENA/web/index.html"),
    PurePosixPath("share/ATHENA/web/app.js"),
)

PACKAGED_MODEL_FILES = {
    PurePosixPath("misc/models/handwriting/handtex.ncnn.bin"),
}


def _normalized_relative(path: Path) -> PurePosixPath:
    return PurePosixPath(path.as_posix())


def distribution_forbidden_reason(relative: Path, is_directory: bool) -> str | None:
    rel = _normalized_relative(relative)
    if any(part in CACHE_DIRECTORY_NAMES for part in rel.parts):
        return "generated Python environment or cache"
    if not is_directory and rel.suffix.lower() in MODEL_SUFFIXES:
        return "model weight"
    if (not is_directory and rel.suffix.lower() == ".bin" and
            rel.name != "ATHENA.bin" and rel not in PACKAGED_MODEL_FILES):
        return "model weight"
    return None


def source_only_reason(
    relative: Path,
    is_directory: bool,
    keep_source_libraries: bool,
    keep_athena_binary: bool,
) -> str | None:
    if is_directory:
        return None
    rel = _normalized_relative(relative)
    if rel == PurePosixPath("bin/ATHENA.bin") and not keep_athena_binary:
        return "development executable"
    if rel in LINUX_SERVICE_EXECUTABLES:
        return "development service executable"
    if (len(rel.parts) == 2 and rel.parts[0] == "bin" and
            rel.name.startswith("ATHENA.bin.before-")):
        return "historical development executable"
    if (not keep_source_libraries and rel.parts and rel.parts[0] == "lib"):
        return "development runtime library"
    return None


def _copy_ignore(
    source: Path, keep_source_libraries: bool, keep_athena_binary: bool
):
    def ignore(directory: str, names: list[str]) -> set[str]:
        base = Path(directory)
        ignored: set[str] = set()
        for name in names:
            candidate = base / name
            relative = candidate.relative_to(source)
            is_directory = candidate.is_dir() and not candidate.is_symlink()
            if (distribution_forbidden_reason(relative, is_directory) or
                    source_only_reason(
                        relative,
                        is_directory,
                        keep_source_libraries,
                        keep_athena_binary,
                    )):
                ignored.add(name)
        return ignored

    return ignore


def copy_runtime(
    source: Path,
    destination: Path,
    keep_source_libraries: bool = False,
    keep_athena_binary: bool = False,
) -> None:
    source = source.resolve()
    destination = destination.resolve()
    if not source.is_dir():
        raise RuntimeError(f"runtime source does not exist: {source}")
    if destination == source or source in destination.parents:
        raise RuntimeError("runtime destination must not be inside its source")

    temporary = destination.with_name(f".{destination.name}.tmp-{os.getpid()}")
    if temporary.exists():
        shutil.rmtree(temporary)
    temporary.parent.mkdir(parents=True, exist_ok=True)

    try:
        shutil.copytree(
            source,
            temporary,
            symlinks=True,
            ignore=_copy_ignore(
                source, keep_source_libraries, keep_athena_binary
            ),
        )
        if destination.exists():
            shutil.rmtree(destination)
        temporary.replace(destination)
    except BaseException:
        if temporary.exists():
            shutil.rmtree(temporary)
        raise


def forbidden_entries(root: Path) -> list[tuple[Path, str]]:
    root = root.resolve()
    if not root.is_dir():
        raise RuntimeError(f"runtime tree does not exist: {root}")

    result: list[tuple[Path, str]] = []
    for directory, dirnames, filenames in os.walk(root, followlinks=False):
        base = Path(directory)
        for name in dirnames:
            candidate = base / name
            reason = distribution_forbidden_reason(
                candidate.relative_to(root), is_directory=True
            )
            if reason:
                result.append((candidate, reason))
        for name in filenames:
            candidate = base / name
            reason = distribution_forbidden_reason(
                candidate.relative_to(root), is_directory=False
            )
            if reason:
                result.append((candidate, reason))
    return sorted(result)


def verify_linux_services(root: Path) -> None:
    root = root.resolve()
    missing = [
        relative for relative in (*LINUX_SERVICE_EXECUTABLES, *LINUX_WEB_ASSETS)
        if not (root / relative).is_file()
    ]
    non_executable = [
        relative for relative in LINUX_SERVICE_EXECUTABLES
        if (root / relative).is_file() and
        not os.access(root / relative, os.X_OK)
    ]
    if not missing and not non_executable:
        return
    details = []
    details.extend(f"  missing: {relative}" for relative in missing)
    details.extend(
        f"  not executable: {relative}" for relative in non_executable
    )
    raise RuntimeError(
        "Linux runtime does not contain complete ATHENA service tools:\n" +
        "\n".join(details)
    )


def verify_private_guile(root: Path) -> None:
    candidates = (root.resolve(), root.resolve() / "usr/share/ATHENA")
    for runtime in candidates:
        if not (runtime / "bin/ATHENA.bin").is_file():
            continue
        required = (
            PurePosixPath("lib/athena-guile/lib/libathena-guile.so.1"),
            PurePosixPath(
                "lib/athena-guile/share/guile/3.0/ice-9/boot-9.scm"
            ),
            PurePosixPath(
                "lib/athena-guile/lib/guile/3.0/ccache/ice-9/boot-9.go"
            ),
        )
        missing = [path for path in required if not (runtime / path).exists()]
        if missing:
            details = "\n".join(f"  missing: {path}" for path in missing)
            raise RuntimeError(
                "Linux runtime does not contain ATHENA's private Guile 3 "
                "runtime:\n" + details
            )


def verify_runtime(root: Path, require_linux_services: bool = False) -> None:
    bad = forbidden_entries(root)
    if bad:
        details = "\n".join(
            f"  {path}: {reason}" for path, reason in bad
        )
        raise RuntimeError(
            "runtime contains files forbidden from ATHENA release packages:\n" +
            details
        )
    if require_linux_services:
        verify_linux_services(root)
    verify_private_guile(root)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    copy_parser = subparsers.add_parser(
        "copy", help="copy a filtered ATHENA runtime tree"
    )
    copy_parser.add_argument("source", type=Path)
    copy_parser.add_argument("destination", type=Path)
    copy_parser.add_argument(
        "--keep-source-libraries",
        action="store_true",
        help="retain ATHENA/lib for the host-native release tree",
    )
    copy_parser.add_argument(
        "--keep-athena-binary",
        action="store_true",
        help="retain bin/ATHENA.bin for an executable runtime image",
    )

    verify_parser = subparsers.add_parser(
        "verify", help="reject model weights and generated Python state"
    )
    verify_parser.add_argument("runtime", type=Path)
    verify_parser.add_argument(
        "--require-linux-services",
        action="store_true",
        help="also require the Linux service binaries and Web Server assets",
    )

    args = parser.parse_args(argv)
    try:
        if args.command == "copy":
            copy_runtime(
                args.source,
                args.destination,
                keep_source_libraries=args.keep_source_libraries,
                keep_athena_binary=args.keep_athena_binary,
            )
            verify_runtime(args.destination)
        else:
            verify_runtime(
                args.runtime,
                require_linux_services=args.require_linux_services,
            )
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
