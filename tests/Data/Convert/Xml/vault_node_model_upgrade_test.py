#!/usr/bin/env python3
"""One isolated CLI regression for UTF-8 XML-v1 -> node-model XML-v2 vault migration."""

import argparse
import json
import os
from pathlib import Path
import re
import sqlite3
import subprocess
import tempfile
import xml.etree.ElementTree as ET


PRIMARY = "11111111-1111-4111-8111-111111111111"
ALIAS = "22222222-2222-4222-8222-222222222222"
ARTIFACT = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"
CONTENT = "legacy-content"


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
        '<node tag="label"><text>Theorem {</text></node>'
        '<node tag="theorem"><node tag="document">'
        '<text>Every compact source survives migration.</text>'
        '</node></node>'
        '<node tag="label"><text>Theorem }</text></node>'
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
    (root / "Source.ath").write_text(source_document())
    create_map(root / "map.sqlite")
    create_artifacts(root)


def run(binary, root, env):
    return subprocess.run(
        [str(binary), "--upgrade-vault-node-model", str(root)],
        env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=120,
    )


def node(root, tag):
    return next(item for item in root.iter("node") if item.get("tag") == tag)


def check_migrated(root):
    vaultfile = json.loads((root / "Vaultfile.json").read_text())
    assert vaultfile["node_model_version"] == 1

    target = ET.parse(root / "Target.ath").getroot()
    source = ET.parse(root / "Source.ath").getroot()
    assert target.get("version") == "2"
    assert source.get("version") == "2"

    enunciation = node(target, "enunciation")
    assert enunciation.get("id") == PRIMARY
    props = enunciation.find("properties")
    assert props is not None
    kind = props.find("./property[@name='kind']")
    assert kind is not None and kind.text == "theorem"
    binding = props.find(
        "./property[@name='athena:artifact-bindings']"
        "/property[@name='enunciation']")
    assert binding is not None and binding.text == ARTIFACT

    hlink = node(source, "hlink")
    assert hlink[1].find("value").text.startswith(
        f"tmfs://wikilink/{PRIMARY}/")
    transclude = node(source, "transclude")
    assert len(transclude) == 1 and transclude[0].get("tag") == "tuple"
    ids = [item.find("value").text for item in transclude[0] if item.tag == "text"]
    assert ids == [PRIMARY]

    db = sqlite3.connect(root / "map.sqlite")
    rows = db.execute(
        "SELECT uuid,path,anchor_begin,anchor_end FROM map_nodes ORDER BY uuid"
    ).fetchall()
    db.close()
    assert rows == [(PRIMARY, "Target.ath", "Theorem {", "Theorem }")]

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
    parser.add_argument("--artifacts", type=Path, required=True)
    args = parser.parse_args()

    artifacts = args.artifacts.resolve()
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
    check_migrated(vault)
    workspaces = sorted(temporary.glob(".vault.node-model-upgrade-*"))
    assert len(workspaces) == 1
    backup = workspaces[0] / "vault"
    assert backup.exists()
    assert (backup / "Target.ath").read_bytes() == before_target
    assert json.loads((backup / "Vaultfile.json").read_text()).get(
        "node_model_version", 0) == 0
    assert (workspaces[0] / "manifest.json").exists()

    live_before_repeat = {
        path.name: path.read_bytes()
        for path in (vault / "Target.ath", vault / "Source.ath", vault / "Vaultfile.json")
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


if __name__ == "__main__":
    main()
