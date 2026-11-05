#!/usr/bin/env python3
"""Validate XML command contracts and execute compiled generated adapters."""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[4]
INTERFACES = ROOT / "src/App/Commands/Interfaces"
spec = importlib.util.spec_from_file_location("native_commands", INTERFACES / "generate-commands.py")
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)

def interface(body):
    return '<commands version="1" prefix="" initializer="initialize_fixture">' + body + '</commands>'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="c++")
    args = parser.parse_args()
    groups = module.read_interfaces(sorted(INTERFACES.glob("*.xml")))
    assert len(groups) == 4
    with tempfile.TemporaryDirectory(prefix="athena-command-contract-") as temporary:
        root = Path(temporary)
        xml = root / "fixture.xml"
        valid = interface('''
          <binding name="increment" native="increment" returns="int">
            <arg type="int"/><arg type="bool" default="false"/>
          </binding>
          <binding name="bytes" native="echo_bytes" returns="bytes"><arg type="bytes"/></binding>
          <binding name="name" native="name" returns="string"/>
          <binding name="fails" native="fails" returns="void"/>
          <binding name="notify" native="notify" returns="void" dispatch="ui"/>
        ''')
        xml.write_text(valid)
        module.generate([xml], root)
        generated = (root / "commands_fixture.cpp").read_bytes()
        invalid = [
            valid.replace('type="int"', 'type="object"', 1),
            valid.replace('type="int"', 'type="procedure"', 1),
            valid.replace('default="false"', 'default="0"'),
            valid.replace('type="int"', 'type="int" default="false"', 1),
            valid.replace('name="name"', 'name="increment"'),
            valid.replace('native="name"', 'native="run();abort"'),
            valid.replace('dispatch="ui"', 'dispatch="scheme"'),
            valid.replace('name="notify" native="notify" returns="void"',
                          'name="notify" native="notify" returns="int"'),
            valid.replace('type="bool" default="false"', 'type="bool" rest="true"'),
        ]
        for content in invalid:
            xml.write_text(content)
            try:
                module.generate([xml], root)
            except ValueError:
                pass
            else:
                raise AssertionError("Invalid command contract accepted")
            assert (root / "commands_fixture.cpp").read_bytes() == generated
        (root / "native_dispatch.hpp").write_text(r'''
#pragma once
#include <cassert>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>
using string = std::string;
struct command_bytes { string data; };
using command_value = std::variant<std::monostate, int, bool, string, command_bytes>;
using command_arguments = std::vector<command_value>;
int N(const command_arguments& a) { return int(a.size()); }
template<class T> T command_argument(const command_value& v, const char* why) {
  if (const auto* p = std::get_if<T>(&v)) return *p;
  throw std::invalid_argument(why);
}
using adapter = command_value (*)(const command_arguments&);
std::map<string, adapter> commands;
void native_dispatch_register(const char* name, adapter f) { commands.emplace(name, f); }
int notifications = 0;
void athena_dispatch_ui(void (*f)()) { f(); }
''')
        (root / "probe.cpp").write_text(r'''
#include "native_dispatch.hpp"
unsigned increment(int x, bool double_it) { return x + (double_it ? 2 : 1); }
string echo_bytes(string bytes) { return bytes; }
const char* name() { return "native"; }
int live = 0;
void fails() {
  struct guard { guard() { ++live; } ~guard() { --live; } } scope;
  throw std::runtime_error("native failure");
}
void notify() { ++notifications; }
#include "commands_fixture.cpp"
int main() {
  initialize_fixture();
  assert(std::get<int>(commands.at("increment")({3})) == 4);
  assert(std::get<int>(commands.at("increment")({3, true})) == 5);
  assert(std::get<string>(commands.at("name")({})) == "native");
  const string binary("\0\x80\xff", 3);
  assert(std::get<command_bytes>(commands.at("bytes")({command_bytes{binary}})).data == binary);
  for (const command_arguments values: {command_arguments{}, {string("wrong")}, {3, false, true}}) {
    try { commands.at("increment")(values); assert(false); }
    catch (const std::invalid_argument&) {}
  }
  try { commands.at("bytes")({string("text")}); assert(false); }
  catch (const std::invalid_argument&) {}
  try { commands.at("fails")({}); assert(false); }
  catch (const std::runtime_error&) { assert(live == 0); }
  commands.at("notify")({});
  assert(notifications == 1);
  assert(std::get<int>(commands.at("increment")({8})) == 9);
}
''')
        binary = root / "probe"
        subprocess.run([args.compiler, "-std=c++17", "-I", str(root),
                        str(root / "probe.cpp"), "-o", str(binary)], check=True, timeout=25)
        subprocess.run([str(binary)], check=True, timeout=5)
    print("PASS native command schema, arity/types, defaults, bytes and exception unwinding")

if __name__ == "__main__":
    main()
