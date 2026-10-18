#!/usr/bin/env python3
"""Stage genuine v2 math alphabets and verify typed v3 letters and identities.

Both the input Vault and the disconnected v3 candidate are disposable. This
is a functional v2-to-v3 conversion test, with no Hodarium service or account.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET


ALPHABETS = (
    ("math-alpha-bold", "mbf"),
    ("math-alpha-bold-up", "mbfu"),
    ("math-alpha-frak", "frak"),
    ("math-alpha-cal", "cal"),
    ("math-alpha-bbb", "bb"),
    ("math-bf", "bf"),
)


def environment(home, runtime, resources):
    result = dict(os.environ)
    result.update({
        "HOME": str(home), "ATHENA_HOME_PATH": str(home / "profile"),
        "XDG_CONFIG_HOME": str(home / "config"),
        "XDG_CACHE_HOME": str(home / "cache"),
        "XDG_DATA_HOME": str(home / "data"),
        "ATHENA_PATH": str(resources), "QT_QPA_PLATFORM": "offscreen",
        "GUILE_AUTO_COMPILE": "0",
        "LD_LIBRARY_PATH": ":".join((str(runtime / "lib"),
                                     str(resources / "lib"),
                                     result.get("LD_LIBRARY_PATH", ""))),
    })
    return result


def run(binary, home, env, *args):
    proc = subprocess.run((str(binary), "--upgrade-vault-xml-v3", *map(str, args)),
                          cwd=home, env=env, capture_output=True,
                          text=True, timeout=120, check=False)
    if proc.returncode:
        raise RuntimeError(f"ATHENA XML v3 offline stage failed ({proc.returncode}):\n"
                           + proc.stdout + proc.stderr)
    return proc.stdout + proc.stderr


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    args = parser.parse_args()
    binary, runtime, resources = (
        args.binary.resolve(), args.runtime.resolve(), args.resources.resolve())
    with tempfile.TemporaryDirectory(prefix="athena-v3-math-alphabets-") as path:
        home = Path(path)
        system = home / "profile/system"
        system.mkdir(parents=True)
        (system / "sys_state.json").write_text(json.dumps({
            "format": "athena-system-state", "version": 3}))
        vault = home / "source"
        candidate = home / "candidate"
        (vault / ".athena").mkdir(parents=True)
        (vault / "Vaultfile.json").write_text(json.dumps({
            "name": "Private mathematical alphabet conversion fixture",
            "node_model_version": 1}))
        body_id = "01010101-0101-4101-8101-010101010101"
        root = ET.Element("athena-document", {"version": "2", "text-model": "utf-8"})
        document = ET.SubElement(root, "node", {"tag": "document"})
        body = ET.SubElement(document, "node", {"tag": "body"})
        source_document = ET.SubElement(body, "node", {"tag": "document", "id": body_id})
        math = ET.SubElement(source_document, "node", {"tag": "math"})
        concat = ET.SubElement(math, "node", {"tag": "concat"})
        original_ids = []
        for index, (old, _) in enumerate(ALPHABETS, 1):
            identity = f"{index:08x}-1111-4111-8111-{index:012x}"
            original_ids.append(identity)
            variant = ET.SubElement(concat, "node", {"tag": old, "id": identity})
            ET.SubElement(ET.SubElement(variant, "text"), "value").text = "A"
        data = ET.tostring(root, encoding="utf-8")
        source = vault / "Math.ath"
        source.write_bytes(data)
        original = (hashlib.sha256(data).hexdigest(), source.stat().st_mtime_ns)

        env = environment(home, runtime, resources)
        preflight = run(binary, home, env, "--check", vault)
        if "Original Vault unchanged" not in preflight:
            raise AssertionError("Mathematical alphabet preflight did not remain read-only")
        staged = run(binary, home, env, "--stage", candidate, vault)
        if "Private v3 candidate" not in staged and not candidate.is_dir():
            raise AssertionError("XML v3 candidate was not staged")
        if (hashlib.sha256(source.read_bytes()).hexdigest(), source.stat().st_mtime_ns) != original:
            raise AssertionError("Offline mathematical alphabet stage modified its v2 input")
        result = ET.parse(candidate / "Math.ath").getroot()
        assert result.tag == "a" and result.attrib == {"v": "3", "k": "d"}
        values = [node for node in result.iter("n") if node.attrib.get("t") == "math-variant"]
        if len(values) != len(ALPHABETS):
            raise AssertionError(f"Not all mathematical alphabet variants survived: {len(values)}")
        for index, (item, (_, expected)) in enumerate(zip(values, ALPHABETS)):
            slots = list(item)
            if (len(slots) != 2 or slots[0].findtext("v") != expected or
                    slots[1].findtext("v") != "A"):
                raise AssertionError(f"Incorrect native mathematical alphabet at {index}: "
                                     + ET.tostring(item, encoding="unicode"))
            # Compact XML v3 writes the original persistent identity as `i`;
            # v2 spells the same UUID `id`. Never mistake those for losses.
            if item.attrib.get("i") != original_ids[index]:
                raise AssertionError(f"Original mathematical alphabet UUID changed at {index}")
        manifest = json.loads((candidate / ".athena" /
                              "xml-v3-candidate-manifest.json").read_text())
        if (manifest["documents"] != 1 or
                manifest["hodarium_protocol_cutover_verified"] is not False or
                manifest["eligible_for_atomic_publication"] is not False):
            raise AssertionError("Mathematical alphabet candidate safety flags are incorrect")
        print("ATHENA-V3-MATH-ALPHABET-STAGE-PASS: 6 original mathematical "
              "variants migrated with their typed kinds/UUIDs; v2 source intact; "
              "v3 candidate disconnected")


if __name__ == "__main__":
    main()
