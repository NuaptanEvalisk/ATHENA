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
        self.assertTrue(all(group.bindings for group in interfaces))
        self.assertEqual(bindings["exec-buffer"], glue.Binding(
            "exec-buffer", "exec_buffer", "bool",
            (glue.Argument("url"), glue.Argument("object"))))
        self.assertEqual(bindings["object->command"].native,
                         "as_actor_command")
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

    def test_material_choosers_return_through_actor_callbacks(self):
        interface = glue.read_interfaces([GLUE / "native.xml"])[0]
        bindings = {binding.name: binding for binding in interface.bindings}
        self.assertEqual(bindings["material-choose-citation"], glue.Binding(
            "material-choose-citation", "qtm_material_choose_citation_async", "void",
            (glue.Argument("string"), glue.Argument("procedure"))))
        self.assertEqual(bindings["material-choose-references"], glue.Binding(
            "material-choose-references", "qtm_material_choose_references_async", "void",
            (glue.Argument("procedure"),)))

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

    def test_rest_arguments_are_generic_and_last(self):
        body = ('<binding name="collect-values" native="collect" returns="void">'
                '<arg type="tree"/><arg type="object" rest="true"/></binding>')
        interface = glue.read_interfaces([self.interface(body)])[0]
        code = glue.cpp_bindings(interface)
        self.assertIn('object in2= tmscm_to_object (arg2);', code)
        self.assertIn('collect (in1, in2);', code)
        self.assertIn('tmg_collect_values, 1, 0, 1);', code)
        for args in ('<arg type="object" rest="true"/><arg type="int"/>',
                     '<arg type="string" rest="true"/>',
                     '<arg type="object" rest="yes"/>'):
            with self.subTest(args=args), self.assertRaises(ValueError):
                glue.read_interfaces([self.interface(
                    '<binding name="collect" native="collect" returns="void">'
                    + args + '</binding>')])

    def test_binary_marshalling_is_a_generic_type(self):
        interface = glue.read_interfaces([self.interface(
            '<binding name="arbitrary-bytes" native="arbitrary" returns="bytes">'
            '<arg type="bytes" passing="move"/></binding>')])[0]
        code = glue.cpp_bindings(interface)
        self.assertIn('TMSCM_ASSERT_BYTES (arg1, TMSCM_ARG1, "arbitrary-bytes");', code)
        self.assertIn('bytes in1= tmscm_to_bytes (arg1);', code)
        self.assertIn('bytes out= arbitrary (std::move (in1));', code)
        self.assertIn('return bytes_to_tmscm (out);', code)

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

    def test_exception_policy_validation_and_scope(self):
        body = ('<binding name="arbitrary-failure" native="arbitrary_native" '
                'returns="int" exceptions="scheme">'
                '<arg type="object" passing="move"/></binding>')
        interface = glue.read_interfaces([self.interface(body)])[0]
        self.assertEqual(interface.bindings[0].exceptions, "scheme")
        code = glue.cpp_bindings(interface)
        self.assertIn('arbitrary_native (std::move (in1));', code)
        self.assertLess(code.index('try {'), code.index('object in1='))
        self.assertLess(code.index('catch (...)'), code.index('scm_misc_error ('))
        self.assertIn('scm_misc_error ("arbitrary-failure", "~A",', code)
        self.assertIn('scm_dynwind_unwind_handler (', code)
        self.assertNotIn('std::string', code)
        self.assertNotIn('return TMSCM_FALSE', code)
        self.assertNotIn('scm_', code[code.index('catch (const'):code.index(
            '// Native locals')])
        for policy in ('ignore', 'false', ''):
            with self.subTest(policy=policy), self.assertRaises(ValueError):
                glue.read_interfaces([self.interface(body.replace(
                    'exceptions="scheme"', f'exceptions="{policy}"'))])
        with self.assertRaises(ValueError):
            glue.read_interfaces([self.interface(
                '<binding name="queued" native="queued" returns="void" '
                'dispatch="ui" exceptions="scheme"/>')])
        # Receiver calls and rest/move arguments use the same policy.
        method = body.replace('returns="int"', 'returns="void"').replace(
            'passing="move"', 'passing="move" rest="true"')
        code = glue.cpp_bindings(glue.read_interfaces([
            self.interface(method, prefix="receiver()->")])[0])
        self.assertIn('receiver()->arbitrary_native (std::move (in1));', code)
        self.assertIn('result= TMSCM_UNSPECIFIED;', code)

    def test_native_conversion_bindings_translate_exceptions(self):
        interface = glue.read_interfaces([GLUE / "basic.xml"])[0]
        bindings = {binding.name: binding for binding in interface.bindings}
        for name in ('latex-export-prepare-v3-source', 'latex-export-convert-top',
                     'latex-export-convert-tree', 'latex-export-convert-node',
                     'latex-export-core-dispatch', 'tmtex', 'serialize-latex',
                     'native-mathml->tree-v3'):
            with self.subTest(name=name):
                self.assertEqual(bindings[name].exceptions, "scheme")
        self.assertEqual(bindings['native-mathml->tree'].exceptions, "propagate")
        self.assertEqual(bindings['exec-buffer'].exceptions, "propagate")

    @unittest.skipUnless(shutil.which("c++"), "C++ compiler is required")
    def test_exception_wrappers_unwind_before_scheme_nonlocal_exit(self):
        body = ('<binding name="checked-action" native="checked" returns="string" '
                'exceptions="scheme"><arg type="string" passing="move"/>'
                '<arg type="int"/></binding>'
                '<binding name="checked-void" native="checked_void" returns="void" '
                'exceptions="scheme"/>')
        interface = glue.read_interfaces([self.interface(body)])[0]
        cpp = self.root / "exceptions.cpp"
        cpp.write_text(r'''#include <cassert>
#include <csetjmp>
#include <cstdlib>
#include <exception>
#include <string>
#include <utility>
using tmscm= int;
constexpr int TMSCM_UNSPECIFIED= -1;
#define TMSCM_ASSERT_STRING(value,arg,name) ((void)0)
#define TMSCM_ASSERT_INT(value,arg,name) ((void)0)
int live= 0, exceptions= 0;
struct string {
  int value;
  explicit string(int v): value(v) { ++live; }
  string(const string& s): value(s.value) { ++live; }
  string(string&& s): value(s.value) { ++live; }
  ~string() { --live; }
};
struct native_error: std::exception {
  native_error() { ++exceptions; }
  native_error(const native_error&) { ++exceptions; }
  ~native_error() override { --exceptions; }
  const char* what() const noexcept override { return "unsupported detail ~S"; }
};
string tmscm_to_string(tmscm value) { return string(value); }
int tmscm_to_int(tmscm value) {
  if (value == 4) throw native_error();
  return value;
}
tmscm string_to_tmscm(const string& value) {
  if (value.value == 3) throw native_error();
  return value.value;
}
string checked(string value, int mode) {
  if (mode == 1) throw native_error();
  if (mode == 2) throw 42;
  return string(mode == 3 ? 3 : value.value * 2);
}
bool fail_void= false;
void checked_void() { if (fail_void) throw native_error(); }
enum scm_t_dynwind_flags { no_flags= 0 };
constexpr int SCM_F_WIND_EXPLICITLY= 1;
bool active= false;
void (*cleanup)(void*)= nullptr;
void* cleanup_data= nullptr;
int buffers_freed= 0;
void scm_dynwind_begin(scm_t_dynwind_flags) { assert(!active); active= true; }
void scm_dynwind_unwind_handler(void (*f)(void*), void* data, int flags) {
  assert(active && flags == SCM_F_WIND_EXPLICITLY);
  cleanup= f; cleanup_data= data;
}
void scm_dynwind_end() {
  assert(active && cleanup);
  if (*static_cast<char**>(cleanup_data)) ++buffers_freed;
  cleanup(cleanup_data); cleanup= nullptr; active= false;
}
std::string diagnostic, binding;
tmscm scm_from_utf8_string(const char* text) {
  // Also proves the caught exception is destroyed before any Guile allocation.
  assert(live == 0 && exceptions == 0);
  diagnostic= text;
  return 7;
}
tmscm scm_list_1(tmscm value) { return value; }
std::jmp_buf destination;
[[noreturn]] void scm_misc_error(const char* name, const char* format, tmscm args) {
  assert(live == 0 && exceptions == 0 && args == 7);
  assert(std::string(format) == "~A");
  binding= name;
  scm_dynwind_end();
  std::longjmp(destination, 1);
}
template<class F> void tmscm_install_procedure(const char*, F, int, int, int) {}
''' + glue.cpp_bindings(interface) + r'''
int main() {
  assert(tmg_checked_action(21, 0) == 42);
  assert(!active && live == 0 && exceptions == 0);
  for (volatile int mode= 1; mode <= 4; ++mode) {
    if (setjmp(destination) == 0) {
      tmg_checked_action(21, mode);
      assert(false);
    }
    assert(!active && live == 0 && exceptions == 0);
    assert(binding == "checked-action");
    assert(diagnostic == (mode == 2 ? "Unknown native C++ exception" :
                                     "unsupported detail ~S"));
    assert(tmg_checked_action(21, 0) == 42);
  }
  assert(buffers_freed == 3);
  assert(tmg_checked_void() == TMSCM_UNSPECIFIED);
  fail_void= true;
  if (setjmp(destination) == 0) {
    tmg_checked_void();
    assert(false);
  }
  assert(binding == "checked-void" && buffers_freed == 4);
  assert(!active && live == 0 && exceptions == 0);
}
''')
        executable = self.root / "exceptions"
        subprocess.run(["c++", "-std=c++17", str(cpp), "-o", str(executable)],
                       check=True, capture_output=True, text=True)
        subprocess.run([str(executable)], check=True, capture_output=True, text=True)

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

    def test_xml_change_updates_generated_outputs(self):
        path = self.interface('<binding name="before" native="native" returns="void"/>')
        out = self.root / "output"
        glue.generate([path], out)
        self.interface('<binding name="after" native="native" returns="void"/>')
        glue.generate([path], out)
        for name in ["glue_fixture.cpp", "glue-auto-doc.en.tm"]:
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
        mtimes = {p.name: p.stat().st_mtime_ns for p in outputs.iterdir()}
        rebuild()
        self.assertEqual(mtimes, {p.name: p.stat().st_mtime_ns for p in outputs.iterdir()})

        basic = outputs / "glue_basic.cpp"
        basic.unlink()
        rebuild()
        self.assertTrue(basic.is_file())

        xml = target_glue / "basic.xml"
        tree = glue.ET.parse(xml)
        tree.getroot()[0].set("name", "changed-by-cmake-test")
        tree.write(xml)
        rebuild()
        self.assertIn("changed-by-cmake-test", basic.read_text())
        self.assertIn("changed-by-cmake-test", (outputs / "glue-auto-doc.en.tm").read_text())
        rebuild()
        self.assertNotIn("Generating C++", rebuild().stdout)

        tree.getroot()[0].set("returns", "not-a-type")
        tree.write(xml)
        self.assertNotEqual(rebuild(check=False).returncode, 0)


if __name__ == "__main__":
    unittest.main()
