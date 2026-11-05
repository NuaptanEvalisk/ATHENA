#!/usr/bin/env python3
"""Exercise native v3 through real website and headless BufferActor exports."""

import argparse
from html import unescape
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
from xml.etree import ElementTree as ET


ROW_ID = "12345678-1234-4234-8234-123456789abc"


def text(value):
    node = ET.Element("t")
    ET.SubElement(node, "v").text = value
    return node


def node(tag, *children, **attributes):
    result = ET.Element("n", {"t": tag, **attributes})
    result.extend(children)
    return result


def property_value(name, kind, value):
    result = ET.Element("p", {"n": name, "t": kind})
    if isinstance(value, ET.Element):
        result.append(value)
    else:
        result.text = value
    return result


def metadata(*properties):
    result = ET.Element("x")
    result.extend(properties)
    return result


def fixture():
    scripts = node("scripts", text("x"), text(""), text(""), text("i"),
                   text("2"), p="r")
    tag = node("concat", node("strong", text("Case A")),
               node("hlink", text("Link"), text("target.ath")))
    first = node("row", metadata(property_value("math:row-tag", "h", tag)),
                 node("cell", node("concat", scripts, node("label", text("eq:first")))),
                 i=ROW_ID)
    second = node("row", metadata(property_value("math:row-numbering", "s", "unnumbered")),
                  node("cell", text("y")))
    empty = node("row", metadata(property_value("math:row-tag", "h", text(""))),
                 node("cell", text("z")))
    block = node("math-block", node("math-grid", first, second, empty, k="stack"), b="1")
    enunciation = node("enunciation", metadata(
        property_value("kind", "s", "definition"),
        property_value("numbered", "b", "true"),
        property_value("name", "h", text("Native name"))),
        node("document", text("Enunciation remains visible."), block))
    document = node("document", node("style", node("tuple", text("generic"))),
                    node("body", node("document", text("Native website source"), enunciation)))
    envelope = ET.Element("a", {"v": "3", "k": "d"})
    envelope.append(document)
    return ET.tostring(envelope, encoding="utf-8", xml_declaration=True)


def run(binary, home, environment, arguments, success=True):
    log_path = home / "runtime.log"
    with log_path.open("w+") as log:
        process = subprocess.Popen([str(binary), "-H", "-X", *arguments],
                                   cwd=home, env=environment, start_new_session=True,
                                   stdout=log, stderr=subprocess.STDOUT)
        try:
            process.wait(timeout=120)
        finally:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
        log.seek(0)
        output = log.read()
    if success and process.returncode:
        raise AssertionError(output[-24000:])
    if not success and not process.returncode:
        raise AssertionError("Malformed website source was accepted\n" + output[-12000:])
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "resources"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="athena-native-v3-website-") as temporary:
        home = Path(temporary)
        vault, site = home / "vault", home / "site"
        vault.mkdir()
        # Exercise file/actor codecs without opening the closed model-v3 Vault gate.
        (vault / "Vaultfile.json").write_text(json.dumps({"name": "Export fixture"}))
        source = vault / "source.ath"
        source.write_bytes(fixture())
        legacy = vault / "target.ath"
        legacy.write_text('<athena-document version="2" text-model="utf-8">'
                          '<node tag="document"><node tag="style"><node tag="tuple">'
                          '<text><value>generic</value></text></node></node>'
                          '<node tag="body"><node tag="document"><text><value>'
                          'Legacy baseline &lt;literal&gt; stays text.'
                          '</value></text></node></node></node></athena-document>')
        plain = vault / "plain.ath"
        plain.write_bytes(b'<a v="3" k="d"><n t="document"><n t="style">'
                          b'<n t="tuple"><t><v>generic</v></t></n></n><n t="body">'
                          b'<n t="document"><t><v>Plain v3</v></t></n></n></n></a>')
        originals = {p: p.read_bytes() for p in (source, legacy, plain)}
        (vault / "websites.json").write_text(json.dumps({"version": 1, "websites": [{
            "id": "test", "name": "Native site", "selector": {"kind": "path", "path": ""},
            "destination": str(site), "generatePdfs": False,
            "entrypoint": {"kind": "file", "path": "source.ath"}}]}))
        system = home / "profile/system"
        system.mkdir(parents=True)
        (system / "sys_state.json").write_text(json.dumps({"format": "athena-system-state", "version": 3}))
        resources = args.resources.resolve()
        environment = dict(os.environ)
        environment.update({
            "HOME": str(home), "ATHENA_HOME_PATH": str(home / "profile"),
            "XDG_CONFIG_HOME": str(home / "config"), "XDG_CACHE_HOME": str(home / "cache"),
            "XDG_DATA_HOME": str(home / "data"), "ATHENA_PATH": str(resources),
            "QT_QPA_PLATFORM": "offscreen",
            "LD_LIBRARY_PATH": ":".join((str(resources / "lib"),
                                          environment.get("LD_LIBRARY_PATH", ""))),
        })
        run(args.binary.resolve(), home, environment, ["--generate-website", str(vault), "test"])
        html = unescape((site / "source.html").read_text())
        normalized_html = " ".join(html.split())
        for expected in ("Native name", "Enunciation remains visible.", "Case A", "Link",
                         r"\begin{gather", r"\tag{", "athena-node-" + ROW_ID,
                         "eq:first", "target.html"):
            assert expected in normalized_html, f"Missing {expected!r} from actual website output\n{html[-16000:]}"
        assert r"\tag{}" in html, "Explicit empty tag became absent"
        assert "nonconverted" not in html
        assert "Legacy baseline <literal> stays text." in unescape((site / "target.html").read_text())
        assert "Plain v3" in (site / "plain.html").read_text()

        # The native conversion CLI loads/exports through BufferActor.
        arguments = []
        for name in ("source", "plain", "target"):
            arguments.extend(("-c", str(vault / (name + ".ath")),
                              str(home / ("actor-" + name + ".html"))))
            arguments.extend(("-c", str(vault / (name + ".ath")),
                              str(home / ("copy-" + name + ".ath"))))
        arguments.extend(("-c", str(plain), str(home / "snapshot.pdf"), "-q"))
        run(args.binary.resolve(), home, environment, arguments)
        assert (home / "snapshot.pdf").read_bytes().startswith(b"%PDF-")
        for name, version in (("source", "3"), ("plain", "3"), ("target", "2")):
            envelope = ET.fromstring((home / ("copy-" + name + ".ath")).read_bytes())
            assert envelope.get("v", envelope.get("version")) == version
            if name == "source":
                grid = envelope.find(".//n[@t='math-grid']")
                assert grid is not None and grid.get("k") == "stack"
                rows = grid.findall("n[@t='row']")
                assert len(rows) == 3 and rows[0].get("i") == ROW_ID
                tag = rows[0].find("x/p[@n='math:row-tag']")
                assert tag is not None and tag.get("t") == "h"
                assert tag.find(".//n[@t='strong']/t/v").text == "Case A"
                numbering = rows[1].find("x/p[@n='math:row-numbering']")
                assert numbering is not None and numbering.text == "unnumbered"
                empty_tag = rows[2].find("x/p[@n='math:row-tag']")
                assert empty_tag is not None and empty_tag.get("t") == "h"
                assert empty_tag.find("t/v") is not None
                assert not empty_tag.find("t/v").text
        actor_html = unescape((home / "actor-source.html").read_text())
        actor_html = " ".join(actor_html.split())
        for expected in ("Case A", r"\tag{}", "athena-node-" + ROW_ID):
            assert expected in actor_html
        for path, before in originals.items():
            assert path.read_bytes() == before, "Export changed an authoritative source"

        unsupported = ET.fromstring(fixture())
        grid = unsupported.find(".//n[@t='math-grid']")
        grid.set("k", "eqnarray")
        for row in grid.findall("n[@t='row']"):
            row.extend((node("cell", text("=")), node("cell", text("q"))))
        source.write_bytes(ET.tostring(unsupported, encoding="utf-8"))
        unsupported_bytes = source.read_bytes()
        (site / "source.html").unlink()
        failure = run(args.binary.resolve(), home, environment,
                      ["--generate-website", str(vault), "test"], success=False)
        assert "Custom MathGrid row tags" in failure, failure[-12000:]
        assert not (site / "source.html").exists(), "Unsupported math became a website page"
        assert source.read_bytes() == unsupported_bytes, "Failed export changed its source"

        source.write_bytes(b'<a v="3" k="d"><n')
        run(args.binary.resolve(), home, environment,
            ["--generate-website", str(vault), "test"], success=False)
        assert not (site / "source.html").exists(), "Import error became a website page"
        print("ATHENA-NATIVE-V3-WEBSITE-EXPORT-PASS")


if __name__ == "__main__":
    main()
