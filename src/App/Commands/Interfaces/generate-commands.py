#!/usr/bin/env python3
"""Generate ATHENA's native command bindings directly from XML.

Copyright (C) 2026 ATHENA contributors
SPDX-License-Identifier: GPL-3.0-or-later
"""

import argparse
import dataclasses
import os
from pathlib import Path
import re
import tempfile
import xml.etree.ElementTree as ET


TYPES = frozenset("""
    array_SI array_double array_int array_patch
    array_path array_string array_tree array_url bool bytes
    content double int list_string list_tree modification
    patch path scheme_tree string tree tree_label url value
""".split())
IDENTIFIER = re.compile(r"[a-zA-Z_][a-zA-Z_0-9]*")
COMMAND_NAME = re.compile(r"[a-zA-Z_][a-zA-Z_0-9?!<>*=:+/.-]*")
RECEIVER = re.compile(r"[a-zA-Z_][a-zA-Z_0-9]*\(\)->")


@dataclasses.dataclass(frozen=True)
class Argument:
    type: str
    passing: str = "value"
    default: str | None = None


@dataclasses.dataclass(frozen=True)
class Binding:
    name: str
    native: str
    returns: str
    arguments: tuple[Argument, ...]
    dispatch: str = "direct"


@dataclasses.dataclass(frozen=True)
class Interface:
    name: str
    source: str
    prefix: str
    initializer: str
    bindings: tuple[Binding, ...]


def checked_attributes(node, required, optional=()):
    missing = set(required) - node.attrib.keys()
    unknown = node.attrib.keys() - set(required) - set(optional)
    if missing or unknown:
        raise ValueError(f"<{node.tag}>: missing attributes {sorted(missing)}; "
                         f"unknown attributes {sorted(unknown)}")
    if (node.text or "").strip() or (node.tail or "").strip():
        raise ValueError(f"<{node.tag}>: unexpected text")


def checked_identifier(value, kind):
    if not IDENTIFIER.fullmatch(value):
        raise ValueError(f"invalid {kind}: {value!r}")
    return value


def wrapper_name(name):
    if name.endswith("*"):
        name = name[:-1] + "_dot"
    return "tmg_" + name.translate(str.maketrans("?!<>-=", "PSF2_Q"))


def read_interfaces(paths):
    interfaces = []
    names, wrappers, groups, initializers = set(), set(), set(), set()
    for path in paths:
        root = ET.parse(path).getroot()
        if root.tag != "commands":
            raise ValueError(f"{path}: expected <commands>")
        checked_attributes(root, ("version", "prefix", "initializer"))
        if root.attrib["version"] != "1":
            raise ValueError(f"{path}: unsupported command schema version")
        prefix = root.attrib["prefix"]
        if prefix and not RECEIVER.fullmatch(prefix):
            raise ValueError(f"{path}: unsupported native receiver {prefix!r}")
        group = checked_identifier(path.stem, "interface name")
        initializer = checked_identifier(root.attrib["initializer"], "initializer")
        if group in groups or initializer in initializers:
            raise ValueError(f"{path}: duplicate interface or initializer")
        groups.add(group)
        initializers.add(initializer)
        bindings = []
        for node in root:
            if node.tag != "binding":
                raise ValueError(f"{path}: unexpected <{node.tag}>")
            checked_attributes(node, ("name", "native", "returns"),
                               ("dispatch",))
            name = node.attrib["name"]
            wrapper = wrapper_name(name)
            if not COMMAND_NAME.fullmatch(name) or not IDENTIFIER.fullmatch(wrapper):
                raise ValueError(f"{path}: invalid command name {name!r}")
            if name in names or wrapper in wrappers:
                raise ValueError(f"{path}: duplicate binding or wrapper: {name}")
            names.add(name)
            wrappers.add(wrapper)
            native = checked_identifier(node.attrib["native"], "native function")
            returns = node.attrib["returns"]
            if returns not in TYPES | {"void"}:
                raise ValueError(f"{name}: unknown return type {returns!r}")
            arguments = []
            for arg in node:
                if arg.tag != "arg" or len(arg):
                    raise ValueError(f"{name}: expected empty <arg>")
                checked_attributes(arg, ("type",), ("passing", "default"))
                kind = arg.attrib["type"]
                passing = arg.get("passing", "value")
                default = arg.get("default")
                if kind not in TYPES:
                    raise ValueError(f"{name}: unknown argument type {kind!r}")
                if passing not in ("value", "move"):
                    raise ValueError(f"{name}: unknown passing policy {passing!r}")
                if default is not None and (kind != "bool" or default not in ("true", "false")):
                    raise ValueError(f"{name}: only boolean command defaults are supported")
                if arguments and arguments[-1].default is not None and default is None:
                    raise ValueError(f"{name}: required argument after optional argument")
                arguments.append(Argument(kind, passing, default))
            dispatch = node.get("dispatch", "direct")
            if dispatch not in ("direct", "ui"):
                raise ValueError(f"{name}: unknown dispatch policy {dispatch!r}")
            if dispatch == "ui" and (prefix or returns != "void" or arguments):
                raise ValueError(f"{name}: UI dispatch requires a free nullary void function")
            bindings.append(Binding(name, native, returns, tuple(arguments), dispatch))
        if not bindings:
            raise ValueError(f"{path}: empty interface")
        interfaces.append(Interface(group, path.name, prefix, initializer,
                                    tuple(bindings)))
    return interfaces


CPP_TYPES = {
    "array_SI": "array<SI>", "array_int": "array<int>",
    "array_double": "array<double>", "array_string": "array<string>",
    "array_tree": "array<tree>", "array_url": "array<url>",
    "array_patch": "array<patch>", "array_path": "array<path>",
    "list_string": "list<string>", "list_tree": "list<tree>",
}


def argument_expression(kind, value, diagnostic):
    if kind == "value":
        return value
    if kind == "content":
        return f'command_content ({value}, "{diagnostic}")'
    if kind in ("bytes", "scheme_tree"):
        wrapper = "command_bytes" if kind == "bytes" else "command_syntax"
        return f'command_argument<{wrapper}> ({value}, "{diagnostic}").data'
    native_type = CPP_TYPES.get(kind, kind)
    return f'command_argument<{native_type}> ({value}, "{diagnostic}")'


def cpp_binding(interface, binding):
    wrapper = wrapper_name(binding.name).replace("tmg_", "tmn_", 1)
    required = sum(arg.default is None for arg in binding.arguments)
    maximum = len(binding.arguments)
    arity = (f"N(arguments) != {required}" if required == maximum
             else f"N(arguments) < {required} || N(arguments) > {maximum}")
    expected = str(required) if required == maximum else f"{required}..{maximum}"
    lines = ["static command_value",
             f"{wrapper} (const command_arguments& arguments) {{",
             f"  if ({arity})",
             f'    throw std::invalid_argument ("{binding.name}: expected {expected} arguments");']
    for i, arg in enumerate(binding.arguments):
        diagnostic = f"{binding.name}: argument {i+1} must be {arg.type}"
        expression = argument_expression(arg.type, f"arguments[{i}]", diagnostic)
        if arg.default is not None:
            expression = f"N(arguments) > {i} ? {expression} : {arg.default}"
        lines.append(f"  auto in{i+1}= {expression};")
    native = interface.prefix + binding.native
    if binding.dispatch == "ui":
        lines.append(f"  athena_dispatch_ui ({native});")
    else:
        arguments = ", ".join(f"std::move (in{i})" if arg.passing == "move" else f"in{i}"
                              for i, arg in enumerate(binding.arguments, 1))
        result = "" if binding.returns == "void" else "auto out= "
        lines.append(f"  {result}{native} ({arguments});")
    if binding.returns == "void":
        lines.append("  return std::monostate {};")
    elif binding.returns == "bytes":
        lines.append("  return command_bytes {std::move (out)};")
    elif binding.returns == "scheme_tree":
        lines.append("  return command_syntax {std::move (out)};")
    else:
        native_type = CPP_TYPES.get(binding.returns, binding.returns)
        if native_type == "content":
            native_type = "tree"
        if native_type == "value":
            native_type = "command_value"
        lines.append(f"  return command_value ({native_type} (std::move (out)));")
    return [*lines, "}", ""]


def cpp_bindings(interface):
    lines = [f"// Generated from {interface.source}. Do not edit.",
             "// SPDX-License-Identifier: GPL-3.0-or-later",
             '#include "native_dispatch.hpp"', "#include <stdexcept>",
             "#include <utility>", ""]
    for binding in interface.bindings:
        lines.extend(cpp_binding(interface, binding))
    lines.extend(["void", f"{interface.initializer} () {{"])
    for binding in interface.bindings:
        wrapper = wrapper_name(binding.name).replace("tmg_", "tmn_", 1)
        lines.append(f'  native_dispatch_register ("{binding.name}", {wrapper});')
    return "\n".join([*lines, "}", ""])


def replace_if_changed(path, content):
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as output:
            output.write(content)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def generate(paths, output_dir):
    interfaces = read_interfaces(paths)
    output_dir.mkdir(parents=True, exist_ok=True)
    outputs = {}
    # Validate and finish every group before publishing any build output.
    for interface in interfaces:
        outputs[f"commands_{interface.name}.cpp"] = cpp_bindings(interface)
    for name, content in outputs.items():
        replace_if_changed(output_dir / name, content)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("interfaces", nargs="+", type=Path)
    args = parser.parse_args()
    try:
        generate(args.interfaces, args.output_dir)
    except (ValueError, OSError, ET.ParseError) as error:
        parser.exit(1, f"command generation failed: {error}\n")


if __name__ == "__main__":
    main()
