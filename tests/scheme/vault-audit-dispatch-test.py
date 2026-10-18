#!/usr/bin/env python3
"""Verify retries, exactly-once execution and bounded audit dispatch failure."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--runtime', required=True, type=Path)
args = parser.parse_args()
source = Path(__file__).resolve().parent
runtime = args.runtime.resolve()
with tempfile.TemporaryDirectory(prefix='athena-audit-dispatch-') as temporary:
    root = Path(temporary)
    for scenario in ('ready', 'busy', 'closed'):
        env = dict(os.environ, LD_LIBRARY_PATH=str(runtime/'lib'),
                   GUILE_LOAD_PATH=str(runtime/'share/guile/3.0'),
                   GUILE_LOAD_COMPILED_PATH=str(runtime/'lib/guile/3.0/ccache'),
                   GUILE_AUTO_COMPILE='0',
                   ATHENA_AUDIT_DISPATCH_SCENARIO=scenario,
                   ATHENA_AUDIT_DISPATCH_SCRIPT=str(source.parents[1]/
                       'tools/stress/random-vault-load-audit.scm'),
                   ATHENA_LOAD_AUDIT_RESULT=str(root/'result.scm'),
                   ATHENA_LOAD_AUDIT_MESSAGES=str(root/'messages.txt'),
                   ATHENA_LOAD_AUDIT_SOURCE='test.ath',
                   ATHENA_LOAD_AUDIT_VAULT=str(root),
                   ATHENA_LOAD_AUDIT_MAP='map', ATHENA_LOAD_AUDIT_NAMESPACES='namespaces',
                   ATHENA_LOAD_AUDIT_SETTLE_MS='0')
        subprocess.run([str(runtime/'bin/guile'), '--no-auto-compile',
                        str(source/'vault-audit-dispatch-test.scm')],
                       env=env, check=True, timeout=20)
        print(f'PASS: {scenario}')
