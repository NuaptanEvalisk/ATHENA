#!/usr/bin/env python3
"""Exercise model-free artifact extraction through the normal ATHENA binary."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import uuid
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="athena-structure-") as temporary:
        root = Path(temporary)
        doc = ET.Element("athena-document", version="2", attrib={"text-model": "utf-8"})
        envelope = ET.SubElement(doc, "node", tag="document")
        body = ET.SubElement(ET.SubElement(envelope, "node", tag="body"), "node", tag="document")
        paragraph = ET.SubElement(body, "node", tag="concat")
        source_id = str(uuid.uuid4())
        bold = ET.SubElement(paragraph, "node", tag="strong", id=source_id)
        ET.SubElement(ET.SubElement(bold, "text"), "value").text = "compact operator"
        ET.SubElement(ET.SubElement(paragraph, "text"), "value").text = " has compact image."
        source = root / "source.ath"
        ET.ElementTree(doc).write(source, encoding="utf-8", xml_declaration=True)
        original = source.read_bytes()
        env = dict(os.environ, ATHENA_PATH=str(args.resources.resolve()),
                   ATHENA_HOME_PATH=str(root / "profile"), HOME=str(root),
                   QT_QPA_PLATFORM="offscreen", GUILE_AUTO_COMPILE="0")
        env["LD_LIBRARY_PATH"] = str(args.resources.resolve() / "lib") + ":" + env.get("LD_LIBRARY_PATH", "")
        outputs = []
        for structural in (True, False):
            request = root / "request.json"
            result = root / "result.json"
            request.write_text(json.dumps({"structural_only": structural,
                "title_filter": [], "documents": [{"file": str(source), "path": "source.ath"}]}))
            process = subprocess.run([str(args.binary.resolve()), "-artifact-extract-worker",
                str(request), str(result)], env=env, cwd=root, capture_output=True, timeout=60)
            assert process.returncode == 0, process.stderr.decode(errors="replace")
            response = json.loads(result.read_text())
            assert not response["error"], response
            records = response["documents"][0]["records"]
            assert len(records) == 1, records
            assert records[0]["source_uuid"] == source_id
            assert records[0]["range_state"] == "pending"
            outputs.append(records[0])
        assert outputs[0]["candidates"] == []
        assert len(outputs[1]["candidates"]) == 1
        assert outputs[0]["semantic_names"] == outputs[1]["semantic_names"]
        assert source.read_bytes() == original, "read-only extraction rewrote source"
        print("PASS: model-free structural extraction, source UUID, pending range, no source writes")


if __name__ == "__main__":
    main()
