#!/usr/bin/env python3
"""Check the installed procedures in an isolated, headless ATHENA process."""

import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("interfaces", nargs="+", type=Path)
    args = parser.parse_args()
    signatures = [(node.attrib["name"], len(node))
                  for path in args.interfaces for node in ET.parse(path).getroot()]
    expected = "\n".join(f"({json.dumps(name)} {arity})" for name, arity in signatures)
    with tempfile.TemporaryDirectory(prefix="athena-glue-runtime-") as temporary:
        home = Path(temporary)
        system = home / "profile/system"
        system.mkdir(parents=True)
        # Match the bytecode builder's isolated TeX setup. Interface tests do
        # not need first-install welcome documents or external font builders.
        (system / "sys_state.json").write_text(json.dumps({
            "format": "athena-system-state", "version": 1,
            "compatibility_version": "2.1.4",
            "tex": {"design_dpi": 600, "kpsepath": False,
                    "kpsewhich": False, "make_pk": False, "make_tfm": False},
        }))
        script = home / "check.scm"
        script.write_text(
            '(use-modules (ice-9 format))\n'
            '(for-each\n'
            '  (lambda (entry)\n'
            '    (let* ((name (car entry))\n'
            '           (symbol (string->symbol name)))\n'
            '      (unless (defined? symbol)\n'
            '        (error "Missing generated binding" name))\n'
            '      (let ((proc (eval symbol (current-module))))\n'
            '        (unless (and (procedure? proc)\n'
            '                     (equal? (procedure-minimum-arity proc)\n'
            '                             (list (cadr entry) 0 #f)))\n'
            '          (error "Wrong generated arity" name proc)))))\n'
            f"  '({expected}))\n"
            '(define (check condition label)\n'
            '  (unless condition (error "Native glue regression" label)))\n'
            '(check (and (tree? (string->tree "x")) (not (tree? "x"))\n'
            '            (tm? "x") (url? "file.ath")\n'
            '            (modification? "legacy") (patch? "legacy")\n'
            '            (not (blackbox? #f))) "raw predicates")\n'
            '(check (equal? (native-font-selector "" "" "" "" "") (quote ()))\n'
            '       "headless font selector")\n'
            '(let ((answer (quote pending)))\n'
            '  (native-anchor-enunciations-confirm "" "" "" ""\n'
            '    (lambda (accepted?) (set! answer accepted?)))\n'
            '  (check (eq? answer #f) "headless confirmation continuation"))\n'
            '(check (equal? (escape-symbol-picker) "") "headless symbol picker")\n'
            '(check (equal? (namespace-new-file-wizard) "") "headless namespace wizard")\n'
            '(check (equal? (image-remove-background #f)\n'
            '               "Remove background expects an image path.")\n'
            '       "image adapter error contract")\n'
            '(check (catch (quote wrong-type-arg)\n'
            '         (lambda () (global-transformation-run #f "") #f)\n'
            '         (lambda ignored #t)) "procedure argument validation")\n'
            f'(when (exec-buffer (string->url {json.dumps(str(home / "absent.ath"))})\n'
            '                   (lambda () (error "Absent buffer callback ran")))\n'
            '  (error "exec-buffer accepted a missing buffer"))\n'
            f'(format #t "ATHENA-GLUE-PASS: {len(signatures)} signatures, native contracts and exec-buffer call~%")\n',
            encoding="utf-8")
        environment = dict(os.environ)
        environment.update({
            "HOME": str(home),
            "XDG_CONFIG_HOME": str(home / "config"),
            "XDG_CACHE_HOME": str(home / "cache"),
            "XDG_DATA_HOME": str(home / "data"),
            "ATHENA_HOME_PATH": str(home / "profile"),
            "ATHENA_PATH": str(args.resources.resolve()),
            "QT_QPA_PLATFORM": "offscreen",
            "GUILE_AUTO_COMPILE": "0",
            "GUILE_LOAD_PATH": str(args.runtime / "share/guile/3.0"),
            "GUILE_LOAD_COMPILED_PATH": str(args.runtime / "lib/guile/3.0/ccache"),
            "LD_LIBRARY_PATH": ":".join([str(args.runtime / "lib"),
                                          str(args.resources / "lib"),
                                          os.environ.get("LD_LIBRARY_PATH", "")]),
        })
        process = subprocess.Popen(
            [str(args.binary.resolve()), "-H", "-X", "-x",
             '(exec-global (lambda () (exit (catch #t '
             f'(lambda () (primitive-load {json.dumps(str(script))}) 0) '
             '(lambda args (write args) (newline) 1)))))'],
            cwd=home, env=environment, start_new_session=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            output, _ = process.communicate(timeout=90)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            output, _ = process.communicate()
            raise RuntimeError(f"ATHENA glue test timed out:\n{output}")
        if process.returncode != 0 or "ATHENA-GLUE-PASS:" not in output:
            raise RuntimeError(f"ATHENA glue test failed ({process.returncode}):\n{output}")
        print(output[output.index("ATHENA-GLUE-PASS:"):].splitlines()[0])


if __name__ == "__main__":
    main()
