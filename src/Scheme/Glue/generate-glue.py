#!/usr/bin/env python3
"""Generate ATHENA's C++ bindings and Scheme metadata directly from XML.

Copyright (C) 2026 ATHENA contributors
SPDX-License-Identifier: GPL-3.0-or-later
"""

import argparse
import dataclasses
import json
import os
from pathlib import Path
import re
import tempfile
import xml.etree.ElementTree as ET


TYPES = frozenset("""
    array_SI array_array_array_double array_double array_int array_patch
    array_path array_string array_tree array_url array_widget bool command
    content double int list_string list_tree modification object observer
    patch path procedure promise_widget scheme_tree string tmscm tree tree_label uint url
    widget
""".split())
IDENTIFIER = re.compile(r"[a-zA-Z_][a-zA-Z_0-9]*")
SCHEME_NAME = re.compile(r"[a-zA-Z_][a-zA-Z_0-9?!<>*=:+/.-]*")
RECEIVER = re.compile(r"[a-zA-Z_][a-zA-Z_0-9]*\(\)->")


@dataclasses.dataclass(frozen=True)
class Argument:
    type: str
    passing: str = "value"


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
        if root.tag != "glue":
            raise ValueError(f"{path}: expected <glue>")
        checked_attributes(root, ("version", "prefix", "initializer"))
        if root.attrib["version"] != "1":
            raise ValueError(f"{path}: unsupported glue schema version")
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
            checked_attributes(node, ("name", "native", "returns"), ("dispatch",))
            name = node.attrib["name"]
            wrapper = wrapper_name(name)
            if not SCHEME_NAME.fullmatch(name) or not IDENTIFIER.fullmatch(wrapper):
                raise ValueError(f"{path}: invalid Scheme binding name {name!r}")
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
                checked_attributes(arg, ("type",), ("passing",))
                kind = arg.attrib["type"]
                passing = arg.get("passing", "value")
                if kind not in TYPES:
                    raise ValueError(f"{name}: unknown argument type {kind!r}")
                if passing not in ("value", "move"):
                    raise ValueError(f"{name}: unknown passing policy {passing!r}")
                arguments.append(Argument(kind, passing))
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


def scheme_symbols(interfaces):
    names = [binding.name for interface in interfaces for binding in interface.bindings]
    return (";; Generated from src/Scheme/Glue/*.xml. Do not edit.\n"
            ";; SPDX-License-Identifier: GPL-3.0-or-later\n"
            "(texmacs-module (prog glue-symbols))\n\n"
            "(tm-define (all-glued-symbols)\n  '(\n" +
            "".join(f"    {json.dumps(name)}\n" for name in names) + "  ))\n")


def cpp_bindings(interface):
    lines = [f"// Generated from {interface.source}. Do not edit.",
             "// SPDX-License-Identifier: GPL-3.0-or-later", ""]
    for binding in interface.bindings:
        parameters = ", ".join(f"tmscm arg{i}" for i in range(1, len(binding.arguments) + 1))
        lines.extend(["tmscm", f"{wrapper_name(binding.name)} ({parameters}) {{"])
        for i, arg in enumerate(binding.arguments, 1):
            lines.append(f'  TMSCM_ASSERT_{arg.type.upper()} (arg{i}, TMSCM_ARG{i}, "{binding.name}");')
        for i, arg in enumerate(binding.arguments, 1):
            lines.append(f"  {arg.type} in{i}= tmscm_to_{arg.type} (arg{i});")
        native = interface.prefix + binding.native
        if binding.dispatch == "ui":
            lines.append(f"  athena_dispatch_ui ({native});")
        else:
            arguments = ", ".join(f"std::move (in{i})" if arg.passing == "move" else f"in{i}"
                                  for i, arg in enumerate(binding.arguments, 1))
            result = "" if binding.returns == "void" else f"{binding.returns} out= "
            lines.append(f"  {result}{native} ({arguments});")
        result = ("TMSCM_UNSPECIFIED" if binding.returns == "void"
                  else f"{binding.returns}_to_tmscm (out)")
        lines.extend([f"  return {result};", "}", ""])
    lines.extend(["void", f"{interface.initializer} () {{"])
    for binding in interface.bindings:
        lines.append(f'  tmscm_install_procedure ("{binding.name}",  '
                     f'{wrapper_name(binding.name)}, {len(binding.arguments)}, 0, 0);')
    return "\n".join([*lines, "}", ""])


def api_document(interfaces):
    def escape(text):
        return text.translate(str.maketrans({"<": "\\<less\\>", ">": "\\<gtr\\>"}))

    lines = ["<TeXmacs|1.99.4>", "", "<style|<tuple|tmdoc|english>>", "",
             "<\\body>", "<tmdoc-title|All glue functions>", "",
             "Generated from the XML interfaces in <verbatim|src/Scheme/Glue>.", ""]
    for interface in interfaces:
        for binding in interface.bindings:
            arguments = "".join(f" <scm-arg|{arg.type}>" for arg in binding.arguments)
            lines.extend(["  <\\explain>", f"    <scm|({escape(binding.name)}{arguments})>",
                          "    <explain-synopsis|no synopsis>", "  <|explain>",
                          f"    Calls the <c++> function <cpp|{escape(binding.native)}> "
                          f"which returns <scm|{binding.returns}>.", "  </explain>", ""])
    lines.extend(["  <tmdoc-copyright|2016|the TeXmacs team>",
                  "  <tmdoc-license|Permission is granted to copy, distribute and/or modify this",
                  "  document under the terms of the GNU Free Documentation License, Version 1.1",
                  "  or any later version published by the Free Software Foundation; with no",
                  "  Invariant Sections, with no Front-Cover Texts, and with no Back-Cover Texts.",
                  '  A copy of the license is included in the section entitled "GNU Free',
                  '  Documentation License".>', "</body>", ""])
    return "\n".join(lines)


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
        outputs[f"glue_{interface.name}.cpp"] = cpp_bindings(interface)
    outputs["glue-symbols.scm"] = scheme_symbols(interfaces)
    outputs["glue-auto-doc.en.tm"] = api_document(interfaces)
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
        parser.exit(1, f"glue generation failed: {error}\n")


if __name__ == "__main__":
    main()
