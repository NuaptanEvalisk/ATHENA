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


def _normalized_relative(path: Path) -> PurePosixPath:
    return PurePosixPath(path.as_posix())


def distribution_forbidden_reason(relative: Path, is_directory: bool) -> str | None:
    rel = _normalized_relative(relative)
    if any(part in CACHE_DIRECTORY_NAMES for part in rel.parts):
        return "generated Python environment or cache"
    if not is_directory and rel.suffix.lower() in MODEL_SUFFIXES:
        return "model weight"
    if (not is_directory and rel.suffix.lower() == ".bin" and
            rel.name != "ATHENA.bin"):
        return "model weight"
    return None


def source_only_reason(
    relative: Path, is_directory: bool, keep_source_libraries: bool
) -> str | None:
    if is_directory:
        return None
    rel = _normalized_relative(relative)
    if rel == PurePosixPath("bin/ATHENA.bin"):
        return "development executable"
    if (len(rel.parts) == 2 and rel.parts[0] == "bin" and
            rel.name.startswith("ATHENA.bin.before-")):
        return "historical development executable"
    if (not keep_source_libraries and rel.parts and rel.parts[0] == "lib"):
        return "development runtime library"
    return None


def _copy_ignore(source: Path, keep_source_libraries: bool):
    def ignore(directory: str, names: list[str]) -> set[str]:
        base = Path(directory)
        ignored: set[str] = set()
        for name in names:
            candidate = base / name
            relative = candidate.relative_to(source)
            is_directory = candidate.is_dir() and not candidate.is_symlink()
            if (distribution_forbidden_reason(relative, is_directory) or
                    source_only_reason(
                        relative, is_directory, keep_source_libraries
                    )):
                ignored.add(name)
        return ignored

    return ignore


def copy_runtime(
    source: Path, destination: Path, keep_source_libraries: bool = False
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
            ignore=_copy_ignore(source, keep_source_libraries),
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


def verify_runtime(root: Path) -> None:
    bad = forbidden_entries(root)
    if not bad:
        return
    details = "\n".join(
        f"  {path}: {reason}" for path, reason in bad
    )
    raise RuntimeError(
        "runtime contains files forbidden from ATHENA release packages:\n" +
        details
    )


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

    verify_parser = subparsers.add_parser(
        "verify", help="reject model weights and generated Python state"
    )
    verify_parser.add_argument("runtime", type=Path)

    args = parser.parse_args(argv)
    try:
        if args.command == "copy":
            copy_runtime(
                args.source,
                args.destination,
                keep_source_libraries=args.keep_source_libraries,
            )
            verify_runtime(args.destination)
        else:
            verify_runtime(args.runtime)
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
