#!/usr/bin/env python3
"""Exercise confined vault enumeration and Random document in the real runtime."""

import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    args = parser.parse_args()
    resources = args.resources.resolve()
    runtime = args.runtime.resolve()
    with tempfile.TemporaryDirectory(prefix="athena-random-document-") as temporary:
        home = Path(temporary).resolve()
        root = home / "vault"
        expected = ["top.ath", "nested/deep/note.ath", "folder.ath/child.ath"]
        excluded = [".athena/internal.ath", ".backup/old.ath", ".git/object.ath",
                    "nested/.athena/internal.ath", "nested/.backup/old.ath",
                    "nested/.git/object.ath", ".hidden/hidden.ath", ".hidden.ath",
                    "notes.txt"]
        shared_only = ["legacy.tm", "upper.ATH", "backup.ath~", "auto.ath#"]
        document = ('<athena-document version="2" text-model="utf-8">'
                    '<node tag="document"><node tag="body"><node tag="document">'
                    '<text><value>Fixture</value></text></node></node></node>'
                    '</athena-document>')
        for name in expected + excluded + shared_only:
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(document)
        (root / "empty.ath").mkdir()
        (root / "Vaultfile.json").write_text('{"name":"Random document test"}')
        # A sibling whose name shares the vault prefix catches string-prefix
        # containment checks. File symlinks must be excluded too.
        outside = home / "vault-outside"
        outside.mkdir()
        (outside / "escape.ath").write_text(document)
        links = [
            (outside, root / "escape", True),
            (root, root / "nested/cycle", True),
            (root / "nested", root / "alias.ath", True),
            (outside / "escape.ath", root / "escape.ath", False),
            (root / "top.ath", root / "alias-file.ath", False),
            (home / "missing", root / "dangling.ath", False),
            (home, home / "parent-alias", True),
        ]
        symlinks = 0
        for target, link, directory in links:
            try:
                link.symlink_to(target, target_is_directory=directory)
                symlinks += 1
            except (OSError, NotImplementedError) as error:
                print(f"SKIP symlink fixture {link.name}: {error}", flush=True)
        if hasattr(os, "mkfifo"):
            os.mkfifo(root / "pipe.ath")
        system = home / "profile/system"
        system.mkdir(parents=True)
        (system / "sys_state.json").write_text(
            '{"format":"athena-system-state","version":3}')
        quoted = lambda path: json.dumps(str(path))
        all_files = expected + shared_only
        script = home / "check.scm"
        script.write_text(f'''
(define (check ok label) (unless ok (error "Random document regression" label)))
(define expected '({" ".join(quoted(root / name) for name in expected)}))
(define all-files '({" ".join(quoted(root / name) for name in all_files)}))
;; Read the shipped command and replace only its UI effects and RNG. Native
;; vault enumeration and generated glue remain real; every candidate is tried.
(define command
  (call-with-input-file {quoted(resources / "progs/athena/menus/file-menu.scm")}
    (lambda (port)
      (let loop ((form (read port)))
        (cond ((eof-object? form) (error "Random document command missing"))
              ((and (pair? form) (eq? (car form) 'tm-define)
                    (equal? (cadr form) '(go-to-random-vault-document))) form)
              (else (loop (read port))))))))
(eval `(define (select-document index)
         (let ((load-buffer (lambda (u) (list 'load (url->system u))))
               (set-message (lambda (text title) (list 'message text title)))
               (random (lambda (n)
                         (check (= n (length expected)) "candidate count") index)))
           ,(cons 'define (cdr command))
           (go-to-random-vault-document)))
      (current-module))
(check (null? (vault-get-all-files)) "inactive vault enumeration")
(check (equal? (select-document 0)
               '(message "No Vault is open" "Random document")) "inactive feedback")
(define (load-vault path)
  (let ((result (vault-load-with-ns (string->url path) "Scan test"
                                   "map.sqlite" "ns.sqlite")))
    (check (equal? result "") (list "vault load" path result))))
(define (check-documents)
  (check (equal? (sort (map url->system (vault-get-all-files)) string<?)
                 (sort all-files string<?)) "canonical shared enumeration")
  (check (equal? (sort (map (lambda (i)
                             (let ((result (select-document i)))
                               (check (eq? (car result) 'load) "load effect")
                               (cadr result))) '(0 1 2)) string<?)
                 (sort expected string<?)) "exact random candidates"))
(load-vault {quoted(root)})
(check-documents)
(vault-close)
;; The vault lease rejects a symlink as the final root component, but accepts
;; symlinked parents. Returned candidates must still use the canonical root.
{f'(load-vault {quoted(home / "parent-alias/vault")}) (check-documents) (vault-close)' if (home / "parent-alias").is_symlink() else ''}
;; Retain legacy/backup suffixes and excluded fixtures: none satisfy Random.
(for-each delete-file expected)
(load-vault {quoted(root)})
(check (equal? (sort (map url->system (vault-get-all-files)) string<?)
               (sort '({" ".join(quoted(root / name) for name in shared_only)}) string<?))
       "legacy shared behavior")
(check (equal? (select-document 0)
               '(message "No .ath documents found in this Vault" "Random document"))
       "empty feedback")
(vault-close)
(display "ATHENA-RANDOM-DOCUMENT-PASS\\n")
''')
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
        expression = ('(exec-global (lambda () (exit (catch #t '
                      f'(lambda () (primitive-load {quoted(script)}) 0) '
                      '(lambda args (write args) (newline) 1)))))')
        process = subprocess.Popen(
            [str(args.binary.resolve()), "-H", "-X", "-x", expression],
            cwd=home, env=environment, start_new_session=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            output, _ = process.communicate(timeout=35)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            output, _ = process.communicate()
            raise RuntimeError(f"Vault enumeration timed out (possible cycle):\n{output}")
        if process.returncode or "ATHENA-RANDOM-DOCUMENT-PASS" not in output:
            raise RuntimeError(f"Random document failed ({process.returncode}):\n{output}")
        print(f"ATHENA-RANDOM-DOCUMENT-PASS: nested, exclusions, regular files, "
              f"canonical root, menu selection/feedback; {symlinks} symlink fixtures")


if __name__ == "__main__":
    main()
