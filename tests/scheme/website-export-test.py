#!/usr/bin/env python3
"""Exercise the website CLI, including PDFs for buffers with no GUI views."""

import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--document", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="athena-website-export-") as temporary:
        home = Path(temporary)
        vault = home / "vault"
        vault.mkdir()
        (vault / "Vaultfile.json").write_text(json.dumps({"name": "Export test"}))
        source = vault / "source.ath"
        if args.document:
            shutil.copyfile(args.document, source)
        else:
            source.write_text(
                '<athena-document version="2" text-model="utf-8">'
                '<node tag="document"><node tag="style"><text><value>article</value>'
                '</text></node><node tag="body"><node tag="document">'
                '<text><value>Website export: a &lt; b &amp; c &gt; d; '
                '\u03c0 \u2208 \u211a without a GUI view.</value></text>'
                '<node tag="math"><text><value>p &lt; q</value></text></node>'
                '</node></node></node></athena-document>')
        destination = home / "site"
        (vault / "websites.json").write_text(json.dumps({
            "version": 1, "websites": [{
                "id": "test", "name": "Test website",
                "selector": {"kind": "path", "path": "source.ath"},
                "destination": str(destination), "generatePdfs": True,
                "entrypoint": {"kind": "file", "path": "source.ath"},
            }],
        }))
        system = home / "profile/system"
        system.mkdir(parents=True)
        (system / "sys_state.json").write_text(json.dumps({
            "format": "athena-system-state", "version": 3,
        }))
        runtime = args.runtime.resolve()
        resources = args.resources.resolve()
        environment = dict(os.environ)
        environment.update({
            "HOME": str(home), "ATHENA_HOME_PATH": str(home / "profile"),
            "XDG_CONFIG_HOME": str(home / "config"),
            "XDG_CACHE_HOME": str(home / "cache"),
            "XDG_DATA_HOME": str(home / "data"),
            "ATHENA_PATH": str(resources), "QT_QPA_PLATFORM": "offscreen",
            "GUILE_AUTO_COMPILE": "0",
            "GUILE_LOAD_PATH": str(runtime / "share/guile/3.0"),
            "GUILE_LOAD_COMPILED_PATH": str(runtime / "lib/guile/3.0/ccache"),
            "LD_LIBRARY_PATH": ":".join((str(runtime / "lib"),
                str(resources / "lib"), environment.get("LD_LIBRARY_PATH", ""))),
        })
        with (home / "runtime.log").open("w+") as log:
            process = subprocess.Popen([
                str(args.binary.resolve()), "-H", "-X", "--generate-website",
                str(vault), "test"], cwd=home, env=environment,
                start_new_session=True, stdout=log, stderr=subprocess.STDOUT)
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
        if process.returncode:
            raise RuntimeError(output[-20000:])
        pdfs = list(destination.rglob("*.pdf"))
        assert pdfs, "Website has no PDF"
        assert all(path.read_bytes().startswith(b"%PDF-") for path in pdfs)
        assert list(destination.rglob("*.html")), "Website has no HTML"
        print("ATHENA-WEBSITE-EXPORT-PASS")


if __name__ == "__main__":
    main()
