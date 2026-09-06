#!/usr/bin/env python3
"""Contract tests for the XML glue preprocessor, without an editor startup."""

import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
GLUE = ROOT / "src/Scheme/Glue"
SPEC = importlib.util.spec_from_file_location("athena_glue", GLUE / "generate-glue.py")
glue = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = glue
SPEC.loader.exec_module(glue)


class GlueGeneratorTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="athena-glue-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def interface(self, body, name="fixture", **attributes):
        attrs = {"version": "1", "prefix": "", "initializer": "initialize_" + name}
        attrs.update(attributes)
        node = glue.ET.Element("glue", attrs)
        for binding in glue.ET.fromstring("<bindings>" + body + "</bindings>"):
            node.append(binding)
        path = self.root / (name + ".xml")
        glue.ET.ElementTree(node).write(path)
        return path

    def test_complete_migrated_signatures(self):
        interfaces = glue.read_interfaces([GLUE / (name + ".xml")
                                           for name in ("basic", "editor", "server", "native")])
        bindings = {binding.name: binding for group in interfaces for binding in group.bindings}
        self.assertEqual([len(group.bindings) for group in interfaces], [748, 320, 56, 87])
        self.assertEqual(bindings["exec-buffer"], glue.Binding(
            "exec-buffer", "exec_buffer", "bool",
            (glue.Argument("url"), glue.Argument("object"))))
        self.assertEqual(bindings["init-default-one"].arguments,
                         (glue.Argument("string", "move"),))
        # The native entry consumes the widget smob and binds the close thunk
        # on its source actor before transferring the request to the GUI.
        self.assertEqual(bindings["ads-show-tool-pane"].arguments,
                         (glue.Argument("object"), glue.Argument("string"),
                          glue.Argument("string"), glue.Argument("object"),
                          glue.Argument("bool")))
        self.assertEqual(interfaces[1].prefix, "get_current_editor()->")
        self.assertEqual(interfaces[2].prefix, "get_server()->")

    def test_reject_invalid_schema(self):
        cases = [
            '<binding name="test" native="native" returns="unknown"/>',
            '<binding name="test" native="native" returns="void"><arg type="void"/></binding>',
            '<binding name="test" native="native" returns="void"><arg type="string" passing="copy"/></binding>',
            '<binding name="test" native="native" returns="void"><arg type="string" optional="yes"/></binding>',
            '<binding name="test" native="native" returns="void"><arg type="string">code</arg></binding>',
            '<binding name="test" native="native" returns="void"><unexpected/></binding>',
            '<binding name="test" native="native();bad" returns="void"/>',
            '<binding name="test" native="native" returns="void" ignored="true"/>',
            '<binding name="test" native="native" returns="bool" dispatch="ui"/>',
            '<binding name="test" native="native" returns="void" dispatch="ui"><arg type="string"/></binding>',
            '<binding name="test" native="native" returns="void" dispatch="magic"/>',
            '<binding name="test" native="native"/>',
            '<unexpected/>',
            '',
        ]
        for body in cases:
            with self.subTest(body=body), self.assertRaises(ValueError):
                glue.read_interfaces([self.interface(body)])

    def test_reject_duplicates_and_cpp_symbol_collisions(self):
        for names in [("test", "test"), ("test?", "testP"), ("test*", "test_dot")]:
            body = "".join(f'<binding name="{name}" native="native" returns="void"/>'
                           for name in names)
            with self.subTest(names=names), self.assertRaises(ValueError):
                glue.read_interfaces([self.interface(body)])
        body = '<binding name="test" native="native" returns="void"/>'
        with self.assertRaises(ValueError):
            glue.read_interfaces([self.interface(body, "first"),
                                  self.interface(body, "second")])

    def test_reject_wrong_version_and_receiver(self):
        body = '<binding name="test" native="native" returns="void"/>'
        for attrs in [{"version": "2"}, {"prefix": "bad();receiver()->"}, {"initializer": "x();bad"}]:
            with self.subTest(attrs=attrs), self.assertRaises(ValueError):
                glue.read_interfaces([self.interface(body, **attrs)])

    def test_policies_are_independent_of_function_names(self):
        body = ('<binding name="arbitrary-action" native="arbitrary_native_action" '
                'returns="void" dispatch="ui"/>')
        interface = glue.read_interfaces([self.interface(body)])[0]
        code = glue.cpp_bindings(interface)
        self.assertIn('athena_dispatch_ui (arbitrary_native_action);', code)
        self.assertNotIn('arbitrary_native_action ();', code)
        with self.assertRaises(ValueError):
            glue.read_interfaces([self.interface(body, prefix="arbitrary_receiver()->")])
        body = ('<binding name="arbitrary-method" native="method" returns="int">'
                '<arg type="int"/></binding>')
        interface = glue.read_interfaces([
            self.interface(body, prefix="arbitrary_receiver()->")])[0]
        self.assertIn('int out= arbitrary_receiver()->method (in1);',
                      glue.cpp_bindings(interface))

    @unittest.skipUnless(shutil.which("c++"), "C++ compiler is required")
    def test_compile_and_invoke_generated_wrappers(self):
        body = ('<binding name="double-value" native="twice" returns="int">'
                '<arg type="int"/></binding>'
                '<binding name="defer-action" native="action" returns="void" dispatch="ui"/>')
        interface = glue.read_interfaces([self.interface(body)])[0]
        cpp = self.root / "bindings.cpp"
        cpp.write_text('''#include <cassert>
#include <stdexcept>
using tmscm= int;
constexpr int TMSCM_ARG1= 1;
constexpr int TMSCM_UNSPECIFIED= -1;
#define TMSCM_ASSERT_INT(value,arg,name) if (value < 0) throw std::invalid_argument(name)
int tmscm_to_int(int v) { return v; }
int int_to_tmscm(int v) { return v; }
int twice(int v) { return v * 2; }
int called= 0;
int registered= 0;
void action() { ++called; }
void (*pending)()= nullptr;
void athena_dispatch_ui(void (*fn)()) { pending= fn; }
template<class F> void tmscm_install_procedure(const char*, F, int, int, int) {
  ++registered;
}
''' + glue.cpp_bindings(interface) + '''
int main() {
  initialize_fixture();
  assert(registered == 2);
  assert(tmg_double_value(21) == 42);
  bool rejected= false;
  try { tmg_double_value(-1); }
  catch (const std::invalid_argument&) { rejected= true; }
  assert(rejected);
  assert(tmg_defer_action() == TMSCM_UNSPECIFIED);
  assert(called == 0 && pending == action);
  pending();
  assert(called == 1);
}
''')
        executable = self.root / "bindings"
        subprocess.run(["c++", "-std=c++17", str(cpp), "-o", str(executable)],
                       check=True, capture_output=True, text=True)
        subprocess.run([str(executable)], check=True, capture_output=True, text=True)

    def test_no_legacy_generator_or_embedded_implementations(self):
        self.assertFalse((GLUE / "build-glue.scm").exists())
        source = (ROOT / "src/Scheme/Scheme/glue.cpp").read_text()
        self.assertNotIn('tmscm_install_procedure (', source)
        self.assertNotIn('QDialog ', source)
        self.assertNotIn('QMessageBox ', source)
        native = (ROOT / "src/Scheme/Scheme/native_interfaces.cpp").read_text()
        self.assertNotIn('tmscm', native)
        self.assertNotIn('TMSCM', native)

    def test_generate_deterministic_outputs_and_preserve_mtime(self):
        body = ('<binding name="test?" native="native" returns="bool">'
                '<arg type="string" passing="move"/></binding>')
        path = self.interface(body)
        out = self.root / "output"
        glue.generate([path], out)
        contents = {p.name: p.read_bytes() for p in out.iterdir()}
        mtimes = {p.name: p.stat().st_mtime_ns for p in out.iterdir()}
        cpp = (out / "glue_fixture.cpp").read_text()
        self.assertIn('TMSCM_ASSERT_STRING (arg1, TMSCM_ARG1, "test?");', cpp)
        self.assertIn('bool out= native (std::move (in1));', cpp)
        self.assertIn('tmscm_install_procedure ("test?",  tmg_testP, 1, 0, 0);', cpp)
        glue.generate([path], out)
        self.assertEqual(contents, {p.name: p.read_bytes() for p in out.iterdir()})
        self.assertEqual(mtimes, {p.name: p.stat().st_mtime_ns for p in out.iterdir()})

    def test_xml_change_updates_both_languages(self):
        path = self.interface('<binding name="before" native="native" returns="void"/>')
        out = self.root / "output"
        glue.generate([path], out)
        self.interface('<binding name="after" native="native" returns="void"/>')
        glue.generate([path], out)
        for name in ["glue_fixture.cpp", "glue-symbols.scm", "glue-auto-doc.en.tm"]:
            content = (out / name).read_text()
            self.assertIn("after", content)
            self.assertNotIn("before", content)

    def test_failed_generation_does_not_publish_partial_outputs(self):
        path = self.interface('<binding name="test" native="native" returns="void"/>')
        out = self.root / "output"
        out.mkdir()
        previous = out / "glue_fixture.cpp"
        previous.write_text("previous output")
        with patch.object(glue, "cpp_bindings", side_effect=ValueError("invalid emission")):
            with self.assertRaises(ValueError):
                glue.generate([path], out)
        self.assertEqual(previous.read_text(), "previous output")
        self.assertEqual(list(out.iterdir()), [previous])

    @unittest.skipUnless(shutil.which("cmake"), "CMake is required")
    def test_cmake_regenerates_changed_and_missing_outputs(self):
        source = self.root / "source"
        target_glue = source / "src/Scheme/Glue"
        target_glue.mkdir(parents=True)
        for name in ["basic.xml", "editor.xml", "server.xml", "native.xml",
                     "generate-glue.py"]:
            shutil.copy2(GLUE / name, target_glue / name)
        (source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.20)\n'
            'project(GlueBuildContract NONE)\n'
            'find_package(Python3 REQUIRED COMPONENTS Interpreter)\n'
            'set(ATHENA_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")\n'
            'set(ATHENA_BINARY_DIR "${CMAKE_BINARY_DIR}")\n'
            f'include("{ROOT}/cmake/AthenaGlue.cmake")\n')
        build = self.root / "build"
        subprocess.run(["cmake", "-S", str(source), "-B", str(build)],
                       check=True, capture_output=True, text=True)

        def rebuild(check=True):
            return subprocess.run(["cmake", "--build", str(build), "--target", "athena_glue", "-j4"],
                                  check=check, capture_output=True, text=True)

        rebuild()
        outputs = build / "generated/athena-glue"
        resource = source / "ATHENA/progs/prog/glue-symbols.scm"
        self.assertEqual(resource.read_bytes(), (outputs / "glue-symbols.scm").read_bytes())
        mtimes = {p.name: p.stat().st_mtime_ns for p in outputs.iterdir()}
        rebuild()
        self.assertEqual(mtimes, {p.name: p.stat().st_mtime_ns for p in outputs.iterdir()})

        basic = outputs / "glue_basic.cpp"
        basic.unlink()
        rebuild()
        self.assertTrue(basic.is_file())

        resource.unlink()
        rebuild()
        self.assertEqual(resource.read_bytes(), (outputs / "glue-symbols.scm").read_bytes())

        xml = target_glue / "basic.xml"
        tree = glue.ET.parse(xml)
        tree.getroot()[0].set("name", "changed-by-cmake-test")
        tree.write(xml)
        rebuild()
        self.assertIn("changed-by-cmake-test", basic.read_text())
        self.assertIn("changed-by-cmake-test", (outputs / "glue-symbols.scm").read_text())
        self.assertIn("changed-by-cmake-test", resource.read_text())
        rebuild()
        self.assertNotIn("Generating C++", rebuild().stdout)

        tree.getroot()[0].set("returns", "not-a-type")
        tree.write(xml)
        self.assertNotEqual(rebuild(check=False).returncode, 0)


if __name__ == "__main__":
    unittest.main()
