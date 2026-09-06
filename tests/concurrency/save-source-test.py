#!/usr/bin/env python3
"""Compile selected production bodies with ownership-checking test doubles.

This is NOT an ATHENA/Qt/Guile integration test. It makes the isolated C++
ownership and failure contracts executable even in a review-only environment.
Use --source-root to verify that the same tests fail on the pre-fix checkpoint.
"""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def function(text: str, name: str) -> str:
    """Extract a full free-function definition, ignoring braces in C++ tokens."""
    match = re.search(r"(?m)^(?:[\w:<>,]+(?:\s*\*)?\n)+" + re.escape(name)
                      + r"\s*\([^;]*?\)\s*\{", text)
    if not match:
        raise ValueError(f"Definition not found: {name}")
    opening = match.end() - 1
    tokens = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|'
                        r"'(?:\\.|[^'\\])*'|[{}]")
    depth = 0
    for token in tokens.finditer(text, opening):
        value = token.group()
        if value == "{":
            depth += 1
        elif value == "}":
            depth -= 1
            if depth == 0:
                return text[match.start():token.end()]
    raise ValueError(f"Unterminated definition: {name}")


def case(text: str, name: str, following: str) -> str:
    start = text.index(f"  case actor_command_kind::{name}:")
    end = text.index(f"  case actor_command_kind::{following}:", start)
    return text[start:end]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    own_root = Path(__file__).resolve().parents[2]
    parser.add_argument("--source-root", type=Path, default=own_root)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--only", choices=("keyboard_cache", "buffer_save", "actor_callback"))
    args = parser.parse_args()
    root = args.source_root.resolve()
    config = (root / "src/ATHENA/Server/tm_config.cpp").read_text()
    buffers = (root / "src/ATHENA/Data/new_buffer.cpp").read_text()
    actors = (root / "src/ATHENA/Server/buffer_actor.cpp").read_text()
    objects = (root / "src/Scheme/Scheme/object.cpp").read_text()
    command_start = objects.index("class object_command_rep:")
    command_end = objects.index("\ncommand\nas_command", command_start)
    sections = {
        "keyboard_cache": {"SOURCE": config[config.index("static tree\nkeyboard_label"): ]},
        "buffer_save": {
            "SOURCE": "\n\n".join(function(buffers, name) for name in (
                "export_tree", "set_last_save_buffer", "get_last_save_buffer",
                "pretend_buffer_saved", "get_all_buffers")),
            "MARK_SAVED": case(actors, "mark_saved", "mark_autosaved")},
        "actor_callback": {
            "COMMAND": objects[command_start:command_end],
            "DISPATCH": case(actors, "invoke_scheme_handle", "invoke_scheme_handle_tree")}}
    compiler = shlex.split(os.environ.get("CXX", "g++"))
    flags = ["-std=c++17", "-O1", "-g", "-pthread", "-Wall", "-Wextra",
             "-Wno-unused-function", "-I", str(own_root / "src/ATHENA")]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                  "-fno-pie", "-no-pie"]
    failed = False
    with tempfile.TemporaryDirectory(prefix="athena-save-source-") as directory:
        work = Path(directory)
        for name, replacements in sections.items():
            if args.only and args.only != name:
                continue
            template = Path(__file__).with_name(f"{name}_test.cpp.in").read_text()
            for key, body in replacements.items():
                marker = f"\n@@{key}@@\n"
                if template.count(marker) != 1:
                    raise ValueError(f"Missing/duplicate template marker: {key}")
                template = template.replace(marker, "\n" + body + "\n")
            cpp = work / f"{name}.cpp"
            binary = work / name
            cpp.write_text(template)
            built = subprocess.run(compiler + flags + [str(cpp), "-o", str(binary)],
                                   check=False)
            if built.returncode:
                print(f"BUILD FAILED: {name}", flush=True)
                failed = True
                continue
            ran = subprocess.run([str(binary)], timeout=30, check=False)
            failed |= ran.returncode != 0
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
