#!/usr/bin/env python3
"""One isolated CLI regression for UTF-8 XML-v1 -> node-model XML-v2 vault migration."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import sqlite3
import subprocess
import tempfile
import xml.etree.ElementTree as ET


PRIMARY = "11111111-1111-4111-8111-111111111111"
ALIAS = "22222222-2222-4222-8222-222222222222"
ARTIFACT = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"
CONTENT = "legacy-content"
MISSING = "33333333-3333-4333-8333-333333333333"
RECOVERABLE = "44444444-4444-4444-8444-444444444444"
BRACE_MATE = "55555555-5555-4555-8555-555555555555"
UNICODE_HEADING = "66666666-6666-4666-8666-666666666666"
CORK_HEADING = "77777777-7777-4777-8777-777777777777"
OPEN_RANGE = "88888888-8888-4888-8888-888888888888"
CONTENT_RECOVERY = "99999999-9999-4999-8999-999999999999"
SYMBOL_RECOVERY = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"
RAW_TOKEN_RECOVERY = "bbbbbbbb-cccc-4ddd-8eee-ffffffffffff"
MAPPED_LABEL = "ffffffff-ffff-4fff-8fff-ffffffffffff"


def xml_v1(body):
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<athena-document version="1" text-model="utf-8">'
        '<node tag="document">'
        '<node tag="style"><text>generic</text></node>'
        f'<node tag="body">{body}</node>'
        '</node></athena-document>\n'
    )


def target_document():
    return xml_v1(
        '<node tag="document">'
        '<node tag="label"><text>proof:Unmapped proof {</text></node>'
        '<node tag="proof"><node tag="document">'
        '<text>This proof has no map.sqlite row.</text>'
        '</node></node>'
        '<node tag="label"><text>proof:Unmapped proof }</text></node>'
        '<node tag="label"><text>note:Unmapped note {</text></node>'
        '<node tag="note"><node tag="document">'
        '<text>This note has no map.sqlite row.</text>'
        '</node></node>'
        '<node tag="label"><text>note:Unmapped note }</text></node>'
        '<node tag="label"><text>H2 §34.2 Arzelà–Ascoli Theorem</text></node>'
        '<node tag="subsection"><text>§34.2 Arzelà–Ascoli Theorem</text></node>'
        '<node tag="label"><text>Theorem {</text></node>'
        '<node tag="theorem"><node tag="document">'
        '<text>Every compact source survives migration.</text>'
        '</node></node>'
        '<node tag="label"><text>Theorem }</text></node>'
        '<node tag="label"><text>proof:Recovered only }</text></node>'
        '<node tag="label"><text>User mapped</text></node>'
        '<node tag="label"><text>User-unmapped</text></node>'
        '</node>'
    )


def source_document():
    return xml_v1(
        '<node tag="document">'
        '<node tag="hlink">'
        '<text>Jump</text>'
        f'<text>tmfs://wikilink/{ALIAS}/Target.ath/Theorem</text>'
        '</node>'
        '<node tag="transclude">'
        f'<text>{ALIAS}</text>'
        '<text>Target.ath</text>'
        '<text>Theorem {</text>'
        '<text>Theorem }</text>'
        '</node>'
        '<node tag="hlink">'
        '<text>Recovered Jump</text>'
        f'<text>tmfs://wikilink/{RECOVERABLE}/Target.ath/User%20mapped</text>'
        '</node>'
        '<node tag="hlink">'
        '<text>Whole File Jump</text>'
        '<text>tmfs://wikilink//Target/</text>'
        '</node>'
        '<node tag="hlink">'
        '<text>Unicode Heading Jump</text>'
        f'<text>tmfs://wikilink/{UNICODE_HEADING}/Target.ath/'
        'H2%20%C2%A734.2%20Arzel%C3%A0Ascoli%20Theorem</text>'
        '</node>'
        '<node tag="hlink">'
        '<text>Cork Map Heading Jump</text>'
        f'<text>tmfs://wikilink/{CORK_HEADING}/Target.ath/'
        'H2%20%C2%A734.2%20Arzel%C3%A0%E2%80%93Ascoli%20Theorem</text>'
        '</node>'
        '<node tag="hlink">'
        '<text>Brace Mate Jump</text>'
        f'<text>tmfs://wikilink/{BRACE_MATE}/Target.ath/proof%3ARecovered%20only%20%7B</text>'
        '</node>'
        '<node tag="hlink">'
        '<text>Paragraph Recovery Jump</text>'
        f'<text>tmfs://wikilink/{CONTENT_RECOVERY}/Nested%2FParagraph/'
        'This%20is%20essentially%20a%20Dedekind%20cut%20of%20a%20line.%20We%20g%20%7B</text>'
        '</node>'
        '<node tag="hlink">'
        '<text>Symbol Anchor Jump</text>'
        f'<text>tmfs://wikilink/{SYMBOL_RECOVERY}/Nested%2FSymbol%20Lemma/'
        'lemma%3ALet%20fAlphalongrightarrowBeta%20be%20a%20homomorphism%20If%20U%20is%20a%20subuniverse%20of%20Alpha%20then%20f%20rightarrow%20U%20is%20%7B</text>'
        '</node>'
        '<node tag="hlink">'
        '<text>Raw Token Heading Jump</text>'
        f'<text>tmfs://wikilink/{RAW_TOKEN_RECOVERY}/Raw%20Heading/'
        'H2%20&lt;#300A&gt;&lt;#5353&gt;%20§13.3%20&lt;#4E60&gt;&lt;#9898&gt;%2014</text>'
        '</node>'
        '<node tag="transclude">'
        f'<text>{RECOVERABLE}</text>'
        '<text>Target.ath</text>'
        '<text>Theorem%20%7B</text>'
        '<text>Theorem%20%7D</text>'
        '</node>'
        '<node tag="transclude">'
        '<text></text>'
        '<text>Representation Theory.Aspect</text>'
        '<text></text>'
        '<text>Aspect Linear Algebra → Representation Theory</text>'
        '</node>'
        '<node tag="transclude">'
        f'<text>{OPEN_RANGE}</text>'
        '<text>Dynamic Focus Frame</text>'
        '<text></text>'
        '<text>H1 Current Focus</text>'
        '</node>'
        '<node tag="transclude">'
        '<text></text>'
        '<text>Nested/Paragraph</text>'
        '<text>This is essentially a Dedekind cut of a line. We g {</text>'
        '<text>This is essentially a Dedekind cut of a line. We g }</text>'
        '</node>'
        '<node tag="hlink">'
        '<text>Lost Jump</text>'
        f'<text>tmfs://wikilink/{MISSING}/Missing.ath/Nowhere</text>'
        '</node>'
        '<node tag="transclude">'
        '<text></text>'
        '<text>Missing.ath</text>'
        '<text>Missing {</text>'
        '<text>Missing }</text>'
        '</node>'
        '</node>'
    )


def metadata_document():
    return xml_v1(
        '<node tag="document">'
        '<node tag="doc-data">'
        '<node tag="doc-title"><text>Abstract Algebra.Scope</text></node>'
        '</node>'
        '<text>Body paragraph</text>'
        '</node>'
    )


def aspect_document():
    return xml_v1(
        '<node tag="document">'
        '<node tag="section"><text>Aspect: Linear Algebra → Representation Theory</text></node>'
        '<node tag="subsection"><text>Knowledge</text></node>'
        '<node tag="section"><text>Next Top Level</text></node>'
        '</node>'
    )


def paragraph_document():
    return xml_v1(
        '<node tag="document">'
        '<node tag="concat"><text>This is essentially a Dedekind cut of a line. '
        'We graciously recall its definition here.</text></node>'
        '</node>'
    )


def open_range_document():
    return xml_v1(
        '<node tag="document">'
        '<node tag="label"><text>H1 Current Focus</text></node>'
        '<node tag="section"><text>Current Focus</text></node>'
        '<node tag="concat"><text>Focus details.</text></node>'
        '</node>'
    )


def symbol_lemma_document():
    return xml_v1(
        '<node tag="document">'
        '<node tag="lemma"><node tag="document">'
        '<node tag="with"><text>font-series</text><text>bold</text>'
        '<node tag="concat">'
        '<text>Let </text><node tag="math"><text>f:Α⟶Β</text></node>'
        '<text> be a homomorphism. If </text><node tag="math"><text>U</text></node>'
        '<text> is a subuniverse of </text><node tag="math"><text>Α</text></node>'
        '<text> then </text><node tag="math"><node tag="concat"><text>f</text>'
        '<node tag="rsup"><text>→</text></node><node tag="around*">'
        '<text>(</text><text>U</text><text>)</text></node></node></node>'
        '<text> is a subuniverse of </text><node tag="math"><text>Β</text></node>'
        '<text>.</text></node></node>'
        '</node></node>'
        '</node>'
    )


def raw_heading_document():
    return xml_v1(
        '<node tag="document">'
        '<node tag="subsection"><text>&lt;#300A&gt;&lt;#5353&gt; §13.3 '
        '&lt;#4E60&gt;&lt;#9898&gt; 14</text></node>'
        '</node>'
    )


def create_map(path):
    db = sqlite3.connect(path)
    db.executescript("""
      CREATE TABLE map_nodes(
        sequence INTEGER PRIMARY KEY AUTOINCREMENT,
        uuid TEXT NOT NULL UNIQUE,
        path TEXT NOT NULL,
        anchor_begin TEXT NOT NULL DEFAULT '',
        anchor_end TEXT NOT NULL DEFAULT '');
      CREATE TABLE map_metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);
      INSERT INTO map_metadata VALUES('format','athena-vault-map');
      CREATE TABLE rename_operations(
        operation_id TEXT PRIMARY KEY,old_path TEXT NOT NULL,new_path TEXT NOT NULL,
        is_directory INTEGER NOT NULL,phase TEXT NOT NULL);
      CREATE INDEX map_nodes_location_idx
        ON map_nodes(path,anchor_begin,anchor_end);
      PRAGMA user_version=2;
    """)
    for uuid in (PRIMARY, ALIAS):
        db.execute(
            "INSERT INTO map_nodes(uuid,path,anchor_begin,anchor_end) VALUES(?,?,?,?)",
            (uuid, "Target.ath", "Theorem {", "Theorem }"),
        )
    db.execute(
        "INSERT INTO map_nodes(uuid,path,anchor_begin,anchor_end) VALUES(?,?,?,?)",
        (MAPPED_LABEL, "Target.ath", "", "User mapped"),
    )
    db.execute(
        "INSERT INTO map_nodes(uuid,path,anchor_begin,anchor_end) VALUES(?,?,?,?)",
        (BRACE_MATE, "Target.ath", "", "proof:Recovered only {"),
    )
    # Legacy map.sqlite was written from TeXmacs' internal Cork strings.  Keep
    # one realistic non-UTF-8 TEXT payload: 0x9f=§, 0xe0=à, 0x15=EN DASH.
    db.execute(
        "INSERT INTO map_nodes(uuid,path,anchor_begin,anchor_end) VALUES(?,?,?,?)",
        (CORK_HEADING, "Target.ath", "",
         sqlite3.Binary(b"H2 \x9f34.2 Arzel\xe0\x15Ascoli Theorem")),
    )
    db.execute(
        "INSERT INTO map_nodes(uuid,path,anchor_begin,anchor_end) VALUES(?,?,?,?)",
        (OPEN_RANGE, "Dynamic Focus Frame.ath", "H1 Current Focus", ""),
    )
    db.commit()
    db.close()


def create_artifacts(root):
    db = sqlite3.connect(root / "artifacts.db")
    db.executescript("""
      CREATE TABLE artifacts(
        uuid TEXT PRIMARY KEY,type TEXT NOT NULL,origin TEXT NOT NULL,
        content_uuid TEXT NOT NULL,proof_uuid TEXT,path TEXT NOT NULL,
        anchor_stem TEXT NOT NULL,display_text TEXT NOT NULL,
        document_order INTEGER NOT NULL,
        identity_decision TEXT NOT NULL DEFAULT 'new',
        identity_evidence TEXT NOT NULL DEFAULT '');
      CREATE TABLE artifact_names(
        artifact_uuid TEXT NOT NULL,name TEXT NOT NULL,ordinal INTEGER NOT NULL,
        name_tree TEXT NOT NULL,PRIMARY KEY(artifact_uuid,ordinal));
    """)
    db.execute(
        """INSERT INTO artifacts(
           uuid,type,origin,content_uuid,proof_uuid,path,anchor_stem,display_text,
           document_order,identity_decision,identity_evidence)
           VALUES(?,?,?,?,?,?,?,?,?,?,?)""",
        (ARTIFACT, "provable", "enunciation", CONTENT, None, "Target.ath",
         "Theorem", "Every compact source survives migration.", 0, "new", ""),
    )
    db.commit()
    db.close()

    db = sqlite3.connect(root / "enunciations.db")
    db.execute(
        """CREATE TABLE entries(
           uuid TEXT PRIMARY KEY,path TEXT NOT NULL,anchor_stem TEXT NOT NULL,
           tag TEXT NOT NULL,display_text TEXT NOT NULL,document_order INTEGER NOT NULL,
           identity_focus TEXT NOT NULL DEFAULT '',identity_host TEXT NOT NULL DEFAULT '',
           identity_before TEXT NOT NULL DEFAULT '',identity_after TEXT NOT NULL DEFAULT '')"""
    )
    db.execute(
        "INSERT INTO entries(uuid,path,anchor_stem,tag,display_text,document_order) "
        "VALUES(?,?,?,?,?,?)",
        (CONTENT, "Target.ath", "Theorem", "theorem",
         "Every compact source survives migration.", 0),
    )
    db.commit()
    db.close()

    db = sqlite3.connect(root / "bold-text.db")
    db.execute(
        """CREATE TABLE entries(
           uuid TEXT PRIMARY KEY,path TEXT NOT NULL,keyword_tree TEXT NOT NULL,
           keyword_display TEXT NOT NULL,occurrence INTEGER NOT NULL,
           paragraph_offsets TEXT NOT NULL,document_order INTEGER NOT NULL,
           identity_focus TEXT NOT NULL DEFAULT '',identity_host TEXT NOT NULL DEFAULT '',
           identity_before TEXT NOT NULL DEFAULT '',identity_after TEXT NOT NULL DEFAULT '')"""
    )
    db.commit()
    db.close()


def create_vault(root):
    root.mkdir()
    (root / "Vaultfile.json").write_text(json.dumps({
        "version": 1,
        "name": "Node migration fixture",
        "map_path": "map.sqlite",
        "artifacts_path": "artifacts.db",
        "enunciations_path": "enunciations.db",
        "bold_text_path": "bold-text.db",
    }))
    (root / "Target.ath").write_text(target_document())
    (root / "Representation Theory.Aspect.ath").write_text(aspect_document())
    (root / "Dynamic Focus Frame.ath").write_text(open_range_document())
    (root / "Nested").mkdir()
    (root / "Nested" / "Paragraph.ath").write_text(paragraph_document())
    (root / "Nested" / "Symbol Lemma.ath").write_text(symbol_lemma_document())
    (root / "Raw Heading.ath").write_text(raw_heading_document())
    (root / "Source.ath").write_text(source_document())
    (root / "Metadata.ath").write_text(metadata_document())
    create_map(root / "map.sqlite")
    create_artifacts(root)


def run(binary, root, env, *options):
    return subprocess.run(
        [str(binary), "--upgrade-vault-node-model", *options, str(root)],
        env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=120,
    )


def node(root, tag):
    return next(item for item in root.iter("node") if item.get("tag") == tag)


def check_migrated(root):
    vaultfile = json.loads((root / "Vaultfile.json").read_text())
    assert vaultfile["node_model_version"] == 1

    target = ET.parse(root / "Target.ath").getroot()
    aspect = ET.parse(root / "Representation Theory.Aspect.ath").getroot()
    focus = ET.parse(root / "Dynamic Focus Frame.ath").getroot()
    paragraph = ET.parse(root / "Nested" / "Paragraph.ath").getroot()
    symbol_lemma = ET.parse(root / "Nested" / "Symbol Lemma.ath").getroot()
    raw_heading = ET.parse(root / "Raw Heading.ath").getroot()
    source = ET.parse(root / "Source.ath").getroot()
    metadata = ET.parse(root / "Metadata.ath").getroot()
    assert target.get("version") == "2"
    assert aspect.get("version") == "2"
    assert focus.get("version") == "2"
    assert paragraph.get("version") == "2"
    assert symbol_lemma.get("version") == "2"
    assert raw_heading.get("version") == "2"
    assert source.get("version") == "2"
    assert metadata.get("version") == "2"

    doc_data = node(metadata, "doc-data")
    doc_title = node(metadata, "doc-title")
    assert doc_data is not None
    assert doc_title is not None
    assert "".join(doc_title.itertext()) == "Abstract Algebra.Scope"

    enunciation = next(
        item for item in target.iter("node")
        if item.get("tag") == "enunciation" and item.get("id") == PRIMARY
    )
    assert enunciation.get("id") == PRIMARY
    props = enunciation.find("properties")
    assert props is not None
    kind = props.find("./property[@name='kind']")
    assert kind is not None and kind.text == "theorem"
    binding = props.find(
        "./property[@name='athena:artifact-bindings']"
        "/property[@name='enunciation']")
    assert binding is not None and binding.text == ARTIFACT
    labels = ["".join(item.itertext()) for item in target.iter("node")
              if item.get("tag") == "label"]
    assert labels == ["proof:Recovered only }", "User-unmapped"]

    hlinks = [item for item in source.iter("node") if item.get("tag") == "hlink"]
    assert len(hlinks) == 9
    destinations = {
        "".join(item[0].itertext()): item[1].find("value").text
        for item in hlinks
    }
    assert destinations["Jump"].startswith(f"tmfs://wikilink/{PRIMARY}/")
    assert destinations["Recovered Jump"].startswith(
        f"tmfs://wikilink/{PRIMARY}/Target.ath/User%20mapped"
    )
    target_body = next(
        item[0] for item in target.iter("node")
        if item.get("tag") == "body"
    )
    target_body_id = target_body.get("id")
    assert target_body_id
    assert destinations["Whole File Jump"].startswith(
        f"tmfs://wikilink/{target_body_id}/Target/"
    )
    heading = node(target, "subsection")
    heading_id = heading.get("id")
    assert heading_id
    assert destinations["Unicode Heading Jump"].startswith(
        f"tmfs://wikilink/{heading_id}/Target.ath/"
        "H2%20%C2%A734.2%20Arzel%C3%A0Ascoli%20Theorem"
    )
    assert destinations["Cork Map Heading Jump"].startswith(
        f"tmfs://wikilink/{heading_id}/Target.ath/"
        "H2%20%C2%A734.2%20Arzel%C3%A0%E2%80%93Ascoli%20Theorem"
    )
    assert destinations["Brace Mate Jump"].startswith(
        f"tmfs://wikilink/{PRIMARY}/Target.ath/proof%3ARecovered%20only%20%7B"
    )
    paragraph_id = node(paragraph, "concat").get("id")
    assert paragraph_id
    assert destinations["Paragraph Recovery Jump"].startswith(
        f"tmfs://wikilink/{paragraph_id}/Nested%2FParagraph/"
    )
    symbol_enunciation = node(symbol_lemma, "enunciation")
    symbol_id = symbol_enunciation.get("id")
    assert symbol_id
    assert destinations["Symbol Anchor Jump"].startswith(
        f"tmfs://wikilink/{symbol_id}/Nested%2FSymbol%20Lemma/"
    )
    raw_heading_id = node(raw_heading, "subsection").get("id")
    assert raw_heading_id
    assert destinations["Raw Token Heading Jump"].startswith(
        f"tmfs://wikilink/{raw_heading_id}/Raw%20Heading/"
    )
    transcludes = [item for item in source.iter("node")
                   if item.get("tag") == "transclude"]
    assert len(transcludes) == 5
    for transclude in transcludes[:2]:
        assert len(transclude) == 1 and transclude[0].get("tag") == "tuple"
        ids = [item.find("value").text for item in transclude[0]
               if item.tag == "text"]
        assert ids == [PRIMARY]
    aspect_sections = [item for item in aspect.iter("node")
                       if item.get("tag") == "section"]
    aspect_subsection = node(aspect, "subsection")
    assert len(aspect_sections) == 2
    aspect_range_ids = [aspect_sections[0].get("id"), aspect_subsection.get("id")]
    assert all(aspect_range_ids)
    assert aspect_sections[1].get("id") not in aspect_range_ids
    aspect_transclude = transcludes[2]
    assert len(aspect_transclude) == 1 and aspect_transclude[0].get("tag") == "tuple"
    ids = [item.find("value").text for item in aspect_transclude[0]
           if item.tag == "text"]
    assert ids == aspect_range_ids
    focus_section = node(focus, "section")
    focus_concat = node(focus, "concat")
    focus_ids = [focus_section.get("id"), focus_concat.get("id")]
    assert all(focus_ids)
    focus_transclude = transcludes[3]
    assert len(focus_transclude) == 1 and focus_transclude[0].get("tag") == "tuple"
    ids = [item.find("value").text for item in focus_transclude[0]
           if item.tag == "text"]
    assert ids == focus_ids
    all_transclusion_ids = [
        [item.find("value").text for item in transclude[0]
         if item.tag == "text"]
        for transclude in transcludes
        if len(transclude) == 1 and transclude[0].get("tag") == "tuple"
    ]
    assert [paragraph_id] in all_transclusion_ids
    source_text = "".join(source.itertext())
    assert "Lost Jump" in source_text
    assert "tmfs://wikilink/33333333-3333-4333-8333-333333333333" not in source_text
    assert ("a transclusion was once here but is already lost "
            "(original link: tmfs://transclude/; file hint: Missing.ath; "
            "begin anchor: Missing {; end anchor: Missing })") in source_text

    assert not (root / "map.sqlite").exists()

    db = sqlite3.connect(root / "artifacts.db")
    artifact = db.execute(
        "SELECT uuid,source_uuid,source_role FROM artifacts WHERE uuid=?",
        (ARTIFACT,),
    ).fetchone()
    db.close()
    assert artifact == (ARTIFACT, PRIMARY, "enunciation")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()

    artifacts = args.artifacts.resolve() if args.artifacts else None
    if artifacts:
        artifacts.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix="node-model-upgrade-", dir=artifacts))
    vault = temporary / "vault"
    create_vault(vault)
    before_target = (vault / "Target.ath").read_bytes()

    runtime = args.runtime.resolve()
    resources = args.resources.resolve()
    env = dict(os.environ)
    env.update({
        "ATHENA_PATH": str(resources),
        "QT_QPA_PLATFORM": "offscreen",
        "GUILE_AUTO_COMPILE": "0",
        "GUILE_LOAD_PATH": str(runtime / "share/guile/3.0"),
        "GUILE_LOAD_COMPILED_PATH": str(runtime / "lib/guile/3.0/ccache"),
        "LD_LIBRARY_PATH": ":".join((
            str(runtime / "lib"), str(resources / "lib"),
            env.get("LD_LIBRARY_PATH", ""),
        )),
    })

    first = run(args.binary.resolve(), vault, env)
    if first.returncode:
        raise RuntimeError(
            f"node-model migration failed ({first.returncode}):\n{first.stderr}"
        )
    assert RECOVERABLE not in first.stderr
    assert BRACE_MATE not in first.stderr
    assert UNICODE_HEADING not in first.stderr
    assert CORK_HEADING not in first.stderr
    assert SYMBOL_RECOVERY not in first.stderr
    assert RAW_TOKEN_RECOVERY not in first.stderr
    assert "Reference resolution: direct=4, hint=10, failed=2." in first.stderr
    check_migrated(vault)
    workspaces = sorted(temporary.glob(".vault.node-model-upgrade-*"))
    assert len(workspaces) == 1
    backup = workspaces[0] / "vault"
    assert backup.exists()
    assert (backup / "Target.ath").read_bytes() == before_target
    assert (backup / "map.sqlite").exists()
    assert json.loads((backup / "Vaultfile.json").read_text()).get(
        "node_model_version", 0) == 0
    manifest_path = workspaces[0] / "manifest.json"
    assert manifest_path.exists()
    manifest = json.loads(manifest_path.read_text())
    assert manifest["reference_resolution"] == {
        "direct": 4, "hint": 10, "failed": 2,
    }

    live_before_repeat = {
        path.name: path.read_bytes()
        for path in (
            vault / "Target.ath", vault / "Source.ath", vault / "Metadata.ath",
            vault / "Vaultfile.json",
        )
    }
    second = run(args.binary.resolve(), vault, env)
    if second.returncode:
        raise RuntimeError(
            f"idempotent migration failed ({second.returncode}):\n{second.stderr}"
        )
    assert "already uses node model v1" in second.stderr
    assert len(list(temporary.glob(".vault.node-model-upgrade-*"))) == 1
    for name, content in live_before_repeat.items():
        assert (vault / name).read_bytes() == content

    cleanup = temporary / "drop-all-labels-vault"
    create_vault(cleanup)
    cleanup_migration = run(args.binary.resolve(), cleanup, env)
    if cleanup_migration.returncode:
        raise RuntimeError(
            "cleanup fixture migration failed "
            f"({cleanup_migration.returncode}):\n{cleanup_migration.stderr}"
        )
    assert any(
        item.get("tag") == "label"
        for item in ET.parse(cleanup / "Target.ath").getroot().iter("node")
    )
    aggressive_result = run(
        args.binary.resolve(), cleanup, env, "--drop-all-labels"
    )
    if aggressive_result.returncode:
        raise RuntimeError(
            "drop-all-labels migration failed "
            f"({aggressive_result.returncode}):\n{aggressive_result.stderr}"
        )
    aggressive_tree = ET.parse(cleanup / "Target.ath").getroot()
    assert not any(
        item.get("tag") == "label" for item in aggressive_tree.iter("node")
    )

    legacy = temporary / "legacy-vault"
    legacy.mkdir()
    (legacy / "Vaultfile.json").write_text('{"name":"Legacy rejection"}')
    legacy_source = b"<TeXmacs|2.1.4>\n\n<\\body>\ncaf\xe9\n</body>\n"
    (legacy / "legacy.ath").write_bytes(legacy_source)
    rejected = run(args.binary.resolve(), legacy, env)
    assert rejected.returncode != 0
    assert "--upgrade-vault-format first" in rejected.stderr
    assert (legacy / "legacy.ath").read_bytes() == legacy_source
    assert not list(temporary.glob(".legacy-vault.node-model-upgrade-*"))

    print(
        "ATHENA-NODE-MODEL-UPGRADE-PASS; xml-v1->v2/reference/artifact/"
        f"idempotence/legacy-rejection; {temporary}"
    )
    if artifacts is None:
        shutil.rmtree(temporary)


if __name__ == "__main__":
    main()
