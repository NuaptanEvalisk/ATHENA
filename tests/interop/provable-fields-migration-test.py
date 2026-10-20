#!/usr/bin/env python3
"""Synthetic-only tests for the one-off provable field migration.

SPDX-License-Identifier: GPL-3.0-or-later
"""
from copy import deepcopy
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock
import uuid
import xml.etree.ElementTree as ET
from contextlib import redirect_stdout, redirect_stderr

REPO = Path(__file__).resolve().parents[2]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    sys.modules[name] = result
    spec.loader.exec_module(result)
    return result


m = module("provable_fields_migration", REPO / "tools/interop/migrate-provable-fields.py")
CONFIG = json.loads((REPO / "tools/interop/provable-attribution-fields.json").read_text())


def text(value, identified=False):
    return {"text": value, **({"id": str(uuid.uuid4())} if identified else {})}


def node(tag, *items, identified=False):
    return {"tag": tag, "children": list(items),
            **({"id": str(uuid.uuid4())} if identified else {})}


def provable(body, kind="theorem", identity=None):
    result = node("enunciation", node("document", body, identified=True), identified=True)
    result["properties"] = [
        m.prop("kind", "string", kind), m.prop("name", "rich_tree", text("")),
        m.prop("numbered", "boolean", True),
        m.prop("athena:artifact-bindings", "dictionary",
               [m.prop("enunciation", "string", str(uuid.uuid4()))])]
    if identity:
        result["id"] = identity
    return result


def document(*items):
    return node("document", node("style", text("generic")),
                node("body", node("document", *items, identified=True)))


def apply_edits(source, edits):
    result = deepcopy(source)
    for edit in edits:
        path = edit["path"]
        parent = result
        for index in path[:-1]:
            parent = parent["children"][index]
        target = parent["children"][path[-1]]
        command, args = edit["command"], edit["parameters"]
        if command == "set":
            replacement = deepcopy(args["tree"])
            for key in ("id", "properties"):
                if key in target:
                    replacement[key] = deepcopy(target[key])
            parent["children"][path[-1]] = replacement
        elif command == "erase":
            del parent["children"][path[-1]]
        elif command == "update_properties":
            properties = m.properties(target)
            properties.update({p["name"]: p["value"] for p in args["set"]})
            for name in args["remove"]:
                properties.pop(name, None)
            target["properties"] = [{"name": k, "value": v} for k, v in sorted(properties.items())]
        else:
            raise AssertionError(command)
    return result


class MigrationTests(unittest.TestCase):
    def migrate(self, content, kind="theorem", identity=None):
        source = document(provable(content, kind, identity))
        plan = m.plan_document(source, "Synthetic.ath", CONFIG)
        self.assertEqual([r["status"] for r in plan.records], ["planned"], plan.records)
        result = apply_edits(source, plan.edits)
        self.assertEqual(set(m.identities(source)), set(m.identities(result)))
        self.assertEqual(len(m.identities(result)), len(set(m.identities(result))))
        again = m.plan_document(result, "Synthetic.ath", CONFIG)
        self.assertEqual(again.records[0]["status"], "already_structured")
        self.assertFalse(again.edits)
        enunciation = next(n for n in m.all_nodes(result) if n.get("tag") == "enunciation")
        return enunciation, m.properties(enunciation)

    def test_names_do_not_depend_on_keywords_or_bold(self):
        for kind in m.KINDS:
            value, props = self.migrate(text("(Absorption, An arbitrary alias) Body", True), kind)
            self.assertEqual(m.Layout(props["name"]["value"]).text, "Absorption, An arbitrary alias")
            self.assertEqual(m.Layout(value["children"][0]).text, "Body")
        _, props = self.migrate(node("strong", text("(UnknownSurname) Body"), identified=True))
        self.assertEqual(m.Layout(props["name"]["value"]).text, "UnknownSurname")
        self.assertNotIn("attribution", props)

    def test_attribution_order_year_and_compound_fields(self):
        for prefix in ("Hopf, Structure Theorem, Characteristic 0",
                       "Structure Theorem, Hopf, Characteristic 0"):
            _, props = self.migrate(text(f"({prefix}) Body", True))
            self.assertEqual(m.Layout(props["name"]["value"]).text,
                             "Structure Theorem, Characteristic 0")
            self.assertEqual(m.Layout(props["attribution"]["value"][0]["value"]).text, "Hopf")
        for prefix, author, year in [
                ("Hurwitz, 1898", "Hurwitz", "1898"),
                ("Universal Chord Theorem, Levy 1934", "Levy", "1934"),
                ("Thompson-Feit, 1960s", "Thompson-Feit", "1960s"),
                ("Convex Four Vertex Theorem, Syamadas Mukhopadhyaya, 1909",
                 "Syamadas Mukhopadhyaya", "1909")]:
            _, props = self.migrate(text(f"({prefix}) Body", True))
            self.assertEqual(props["year"]["value"], year)
            self.assertEqual(m.Layout(props["attribution"]["value"][0]["value"]).text, author)
        _, props = self.migrate(text("(Frobenius' 1912 Theorem) Body", True))
        self.assertNotIn("year", props)
        self.assertNotIn("attribution", props)

    def test_math_structure_and_formatting(self):
        math = node("math", node("concat", text("a"), text(","), text("b")), identified=True)
        content = node("with", text("color"), text("red"), text("font-series"), text("bold"),
                       node("concat", text("(First (nested, comma), "), math,
                            text(" Name, Dynkin) Let "), node("math", text("x"))),
                       identified=True)
        value, props = self.migrate(content)
        name = props["name"]["value"]
        self.assertIn(math, list(m.all_nodes(name)))
        self.assertIn('"red"', json.dumps(name))
        self.assertNotIn('"bold"', json.dumps(name))
        self.assertIn('"bold"', json.dumps(value["children"]))
        self.assertEqual(m.Layout(value["children"][0]).text, "Let \ufffc")

    def test_whole_header_paragraph_and_inline_links(self):
        link = node("hlink", text("Name"), text("https://example.invalid/target"))
        body = node("document", node("concat", text("("), link, text(")"), identified=True),
                    text("Body", True), identified=True)
        value, props = self.migrate(body)
        self.assertEqual(m.Layout(value["children"][0]).text, "Body")
        self.assertIn(link, list(m.all_nodes(props["name"]["value"])))

    def test_explicit_missing_parenthesis_exception(self):
        d = node("math-symbol", text("differential-d"))
        math = node("math", node("frac", node("concat", d),
                                node("concat", deepcopy(d), text(" "), text("t"))))
        identity = CONFIG["leading_parenthesis_exceptions"][0]["node_id"]
        content = node("strong", node("concat", text("(Product Rule for "), math,
                                     text(". If "), node("math", text("x")),
                                     text(" is smooth, then ...")), identified=True)
        value, props = self.migrate(content, identity=identity)
        self.assertEqual(m.Layout(value["children"][0]).text, "If \ufffc is smooth, then ...")
        self.assertIn(math, list(m.all_nodes(props["name"]["value"])))
        source = document(provable(text("(Product Rule for x. If ...", True), identity=identity))
        result = m.plan_document(source, "Synthetic.ath", CONFIG)
        self.assertEqual(result.records[0]["status"], "review")

    def test_review_cases_and_nonprovables_are_unchanged(self):
        for prefix in ("(Kato, 2004, Skinner-Urban, 2013) Body",
                       "(Missing closer", "(Name, ) Body"):
            source = document(provable(text(prefix, True)))
            plan = m.plan_document(source, "Synthetic.ath", CONFIG)
            self.assertFalse(plan.edits)
            self.assertEqual(plan.records[0]["status"], "review")
        for kind in ("definition", "proof", "example"):
            plan = m.plan_document(document(provable(text("(Name) Body"), kind)),
                                   "Synthetic.ath", CONFIG)
            self.assertFalse(plan.edits)
            self.assertFalse(plan.records)

    def test_backup_excludes_runtime_directories_and_does_not_follow_links(self):
        class Progress:
            def update(self, *args, **kwargs):
                pass
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "vault"
            root.mkdir()
            (root / ".athena").mkdir()
            (root / ".athena/state").write_bytes(b"database")
            (root / "nested/.cache").mkdir(parents=True)
            (root / "nested/.cache/busy").write_bytes(b"temporary")
            (root / "Synthetic.ath").write_bytes(b"source")
            (root / "external").symlink_to("/unreadable-outside-vault")
            output = io.StringIO()
            with redirect_stdout(output):
                destination, files = m.backup_vault(root, Progress())
            try:
                self.assertEqual(files, [Path("Synthetic.ath")])
                self.assertFalse((destination / "vault/.athena").exists())
                self.assertFalse((destination / "vault/nested/.cache").exists())
                self.assertEqual((destination / "vault/Synthetic.ath").read_bytes(), b"source")
                self.assertTrue((destination / "vault/external").is_symlink())
                self.assertIn(f"Backup directory: {destination / 'vault'}", output.getvalue())
            finally:
                import shutil
                shutil.rmtree(destination)

    def test_process_check_does_not_match_command_arguments(self):
        binary = REPO / "ATHENA/bin/ATHENA.bin"
        result = subprocess.CompletedProcess([], 0,
            "123 python3 migrate.py --binary /somewhere/ATHENA.bin\n"
            "456 bash echo ATHENA.bin\n", "")
        with mock.patch.object(m.subprocess, "run", return_value=result):
            m.require_no_athena(binary)
        result.stdout += "789 ATHENA.bin ./bin/ATHENA.bin\n"
        with mock.patch.object(m.subprocess, "run", return_value=result):
            with self.assertRaisesRegex(RuntimeError, "789 ATHENA.bin"):
                m.require_no_athena(binary)

    def test_existing_process_aborts_before_backup(self):
        with tempfile.TemporaryDirectory() as root:
            with mock.patch.object(m, "require_no_athena",
                                   side_effect=RuntimeError("ATHENA running")), \
                    mock.patch.object(m, "backup_vault") as backup, \
                    mock.patch.object(m, "running_athena") as launch:
                with self.assertRaisesRegex(RuntimeError, "ATHENA running"):
                    m.main(["--vault", "test", "--expect-root", root,
                            "--binary", sys.executable])
                backup.assert_not_called()
                launch.assert_not_called()


def fixture():
    values = [
        provable(text("(Hurwitz, 1898) A statement.", True), "lemma"),
        provable(node("strong", node("concat", text("(Monotone Class Lemma, "),
            node("math", text("\u03c0-\u03bb"), identified=True),
            text(" Theorem, Dynkin) A statement.")), identified=True)),
        provable(node("with", text("font-series"), text("bold"),
                      text("(Banach-Steinhaus, Uniform Boundedness Principle) Body."),
                      identified=True), "proposition"),
        provable(text("(Untouched definition) Body.", True), "definition"),
        provable(text("(First alias, Second alias) Body.", True), "corollary"),
    ]
    return document(*values)


def xml_fixture(source):
    # This test fixture contains only generic v3 nodes and primitive properties.
    def write_property(prop):
        kinds = {"string": "s", "boolean": "b", "rich_tree": "h", "dictionary": "d"}
        kind, value = prop["value"]["type"], prop["value"]["value"]
        result = ET.Element("p", n=prop["name"], t=kinds[kind])
        if kind == "rich_tree":
            result.append(write_node(value))
        elif kind == "dictionary":
            result.extend(write_property(item) for item in value)
        else:
            result.text = str(value).lower() if kind == "boolean" else value
        return result

    def write_node(node):
        attrs = {"i": node["id"]} if "id" in node else {}
        result = ET.Element("t" if "text" in node else "n", attrs)
        if "properties" in node:
            props = ET.SubElement(result, "x")
            props.extend(write_property(p) for p in node["properties"])
        if "text" in node:
            ET.SubElement(result, "v").text = node["text"]
        else:
            result.set("t", node["tag"])
            result.extend(write_node(child) for child in node["children"])
        return result

    root = ET.Element("a", v="3", k="d")
    root.append(write_node(source))
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def runtime_test():
    helpers = module("audmap_runtime_helpers", REPO / "tests/interop/audmap-v3-runtime-test.py")
    root = Path(tempfile.mkdtemp(prefix="provable-fields-test-", dir="/tmp"))
    vault, profile = root / "vault", root / "profile"
    runtime = Path(tempfile.mkdtemp(prefix="pf-", dir="/tmp"))
    vault.mkdir()
    os.chmod(runtime, 0o700)
    helpers.private_json(profile / "system/sys_state.json",
                         {"format": "athena-system-state", "version": 3})
    (vault / "Vaultfile.json").write_text(json.dumps({"name": "Provable fields test",
        "node_model_version": 2, "map_path": "map.sqlite", "namespace_db_path": "ns.sqlite"}))
    source = fixture()
    path = vault / "Synthetic.ath"
    path.write_bytes(xml_fixture(source))
    (vault / ".backup").mkdir()
    (vault / ".backup/Ignore.ath").write_bytes(b"not a document; must be ignored")
    before = path.read_bytes()
    public, secret = helpers.curve_keypair()
    identity = root / "client/identity.json"
    helpers.private_json(identity, {"version": 1, "public_key": public, "secret_key": secret})
    helpers.private_json(profile / "system/audmap/clients.json",
                         {"version": 1, "clients": {public: {"allow": True, "trust": 0}}})
    resources, guile = REPO / "ATHENA", REPO / "build_qt6/athena-guile-runtime"
    import msgpack
    import zmq
    env = dict(os.environ, HOME=str(root), ATHENA_HOME_PATH=str(profile),
        XDG_CONFIG_HOME=str(root / "config"), XDG_CACHE_HOME=str(root / "cache"),
        XDG_DATA_HOME=str(root / "data"), XDG_RUNTIME_DIR=str(runtime),
        ATHENA_PATH=str(resources), QT_QPA_PLATFORM="offscreen", GUILE_AUTO_COMPILE="0",
        GUILE_LOAD_PATH=str(guile / "share/guile/3.0"),
        GUILE_LOAD_COMPILED_PATH=str(guile / "lib/guile/3.0/ccache"),
        PYTHONPATH=os.pathsep.join({str(Path(package.__file__).parent.parent)
                                   for package in (msgpack, zmq)}),
        LD_LIBRARY_PATH=str(resources / "lib") + ":" + str(guile / "lib"))
    binary = REPO / "ATHENA/bin/ATHENA.bin"

    class Progress:
        def update(self, *args, **kwargs):
            pass

    sys.path.insert(0, str(REPO / "clients/python/src"))
    from athena_audmap import Client
    try:
        with mock.patch.dict(os.environ, env, clear=True), m.running_athena(
                binary, vault, "Provable fields test", root, Progress()) as endpoint:
            try:
                m.require_no_athena(binary)
                raise AssertionError("Process guard missed the running test ATHENA")
            except RuntimeError as error:
                assert "still running" in str(error)
            with Client(endpoint=endpoint, identity=identity) as client:
                session = m.Session(client)
                deadline = time.monotonic() + 60
                while True:
                    try:
                        assert session.one('@/vaults/"Provable fields test"')["root"] == str(vault)
                        break
                    except RuntimeError:
                        if time.monotonic() >= deadline:
                            raise
                        time.sleep(0.1)
                selector = '@/vaults/"Provable fields test"/filesystem/"Synthetic.ath"/saved'
                tree = session.one(selector)["tree"]
                saved = path.read_bytes()
                bad = {"checks": [], "edits": [
                    {"path": [1, 0, 0, 0, 0], "command": "set",
                     "parameters": {"tree": {"text": "Must be rolled back"}}},
                    {"path": [-1], "command": "erase", "parameters": {}}]}
                try:
                    session.one(selector, "batch", bad)
                    raise AssertionError("Invalid batch unexpectedly succeeded")
                except RuntimeError as error:
                    assert "nonnegative" in str(error), str(error)
                assert path.read_bytes() == saved
                assert session.one(selector)["tree"] == tree
                bad = {"checks": [{"path": [], "tree": text("wrong precondition")}], "edits": []}
                try:
                    session.one(selector, "batch", bad)
                    raise AssertionError("Stale precondition unexpectedly succeeded")
                except RuntimeError as error:
                    assert "precondition" in str(error), str(error)
                assert path.read_bytes() == saved
        arguments = ["--binary", str(binary), "--identity", str(identity),
                     "--vault", "Provable fields test", "--expect-root", str(vault)]

        def run(name, apply=False):
            output, errors = io.StringIO(), io.StringIO()
            # Only the test harness supplies a test environment. Production
            # main inherits the caller's profile. Ignore unrelated desktop
            # processes here so testing a /tmp fixture never interrupts them.
            with mock.patch.dict(os.environ, env, clear=True), \
                    mock.patch.object(m, "require_no_athena"), \
                    redirect_stdout(output), redirect_stderr(errors):
                try:
                    code = m.main(arguments + (["--apply"] if apply else []))
                finally:
                    (root / f"{name}.stdout").write_text(output.getvalue())
                    (root / f"{name}.stderr").write_text(errors.getvalue())
            if code:
                raise RuntimeError(output.getvalue() + errors.getvalue())
            stdout = output.getvalue()
            result = json.loads(stdout[stdout.index('{\n  "summary":'):])
            assert Path(result["backup"], "Synthetic.ath").exists()
            assert (Path(result["backup"]) / ".backup/Ignore.ath").exists()
            assert not (Path(result["backup"]) / ".athena").exists()
            assert stdout.index("Backup complete:") < stdout.index("Started ATHENA PID:")
            assert "Stopped migration ATHENA PID:" in stdout
            assert "[" in errors.getvalue(), "Missing progress output"
            pid = int(stdout.split("Started ATHENA PID: ", 1)[1].splitlines()[0])
            try:
                os.kill(pid, 0)
                raise AssertionError("Launched ATHENA was left running")
            except ProcessLookupError:
                pass
            return result

        planned = run("dry-run")
        assert path.read_bytes() == before
        assert planned["summary"]["would_migrate"] == 4
        applied = run("apply", True)
        assert applied["summary"]["migrated"] == 4
        assert Path(applied["backup"], "Synthetic.ath").read_bytes() == before
        current = path.read_bytes()
        repeated = run("repeat", True)
        assert repeated["summary"]["already_structured"] == 4
        assert not repeated["summary"].get("migrated")
        assert path.read_bytes() == current
        after_ids = [element.get("i") for element in ET.fromstring(current).iter() if element.get("i")]
        assert set(m.identities(source)).issubset(set(after_ids))
        assert len(after_ids) == len(set(after_ids))
        print(f"PASS managed startup / backup exclusions / migration / idempotence / shutdown: {root}")
    finally:
        import shutil
        shutil.rmtree(runtime, ignore_errors=True)


if __name__ == "__main__":
    if "--runtime" in sys.argv:
        runtime_test()
    else:
        unittest.main()
