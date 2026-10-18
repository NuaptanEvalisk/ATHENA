#!/usr/bin/env python3
"""Check the private Guile/BDW-GC policy and native plugin unloading."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--compiler', required=True)
parser.add_argument('--runtime', required=True, type=Path)
args = parser.parse_args()
source = Path(__file__).resolve().parent
runtime = args.runtime.resolve()
with tempfile.TemporaryDirectory(prefix='athena-gc-plugin-') as temporary:
    root = Path(temporary)
    plugin = root/'fixture.so'
    probe = root/'probe'
    subprocess.run([args.compiler, '-x', 'c', '-shared', '-fPIC',
                    str(source/'gc-plugin-fixture.c'), '-o', str(plugin)],
                   check=True, timeout=60)
    subprocess.run([args.compiler, '-std=c++17', '-pthread',
                    '-I', str(runtime/'include/guile/3.0'),
                    '-I', str(source.parents[1]/'3rdparty/athena-bdwgc/include'),
                    str(source/'gc-plugin-probe.cc'), '-L', str(runtime/'lib'),
                    '-lathena-guile', '-ldl', '-o', str(probe)],
                   check=True, timeout=60)
    env = dict(os.environ, LD_LIBRARY_PATH=str(runtime/'lib'),
               GUILE_LOAD_PATH=str(runtime/'share/guile/3.0'),
               GUILE_LOAD_COMPILED_PATH=str(runtime/'lib/guile/3.0/ccache'),
               GUILE_AUTO_COMPILE='0', GC_ENABLE_INCREMENTAL='1')
    env.pop('GC_DISABLE_INCREMENTAL', None)
    subprocess.run([str(probe), str(plugin)], env=env, check=True, timeout=90)
