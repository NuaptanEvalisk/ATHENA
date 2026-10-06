#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

guile_prefix="${ATHENA_BOOTSTRAP_GUILE_PREFIX:-${repo_root}/build_qt6/athena-guile-runtime}"
guile="${guile_prefix}/bin/guile"
guild="${guile_prefix}/bin/guild"
source_root="${repo_root}/3rdparty/athena-guile"
module_root="${source_root}/module"
target="x86_64-unknown-linux-gnu"
output_root="${repo_root}/build_qt6/ipados-guile-bootstrap/${target}"
stamp="${output_root}/.athena-bootstrap-source-sha256"

for tool in python3 sha256sum file; do
  command -v "${tool}" >/dev/null 2>&1 || {
    printf 'prepare-guile-bootstrap: missing Linux host tool: %s\n' "${tool}" >&2
    exit 1
  }
done

[[ -x "${guile}" && -x "${guild}" ]] || {
  printf 'prepare-guile-bootstrap: private Linux Guile compiler is required at %s\n' \
    "${guile_prefix}" >&2
  exit 1
}

version="$("${guile}" -c \
  '(format #t "~a.~a.~a" (major-version) (minor-version) (micro-version))')"
[[ "${version}" == "3.0.10" ]] || {
  printf 'prepare-guile-bootstrap: expected private Guile 3.0.10, got %s\n' \
    "${version}" >&2
  exit 1
}

# Hash the complete Scheme module corpus plus the compiler executable.  The
# bootstrap objects are regenerated whenever either the source language/runtime
# surface or the compiler changes.  They are build artifacts, never source.
source_digest="$(python3 - "${module_root}" "${guile}" <<'PY'
import hashlib
import os
import sys

module_root, compiler = sys.argv[1:]
h = hashlib.sha256()
for current, dirs, files in os.walk(module_root):
    dirs.sort()
    files.sort()
    for name in files:
        if not name.endswith('.scm'):
            continue
        path = os.path.join(current, name)
        rel = os.path.relpath(path, module_root)
        h.update(rel.encode('utf-8'))
        h.update(b'\0')
        with open(path, 'rb') as f:
            for chunk in iter(lambda: f.read(1024 * 1024), b''):
                h.update(chunk)
h.update(b'\0compiler\0')
with open(compiler, 'rb') as f:
    for chunk in iter(lambda: f.read(1024 * 1024), b''):
        h.update(chunk)
print(h.hexdigest())
PY
)"

if [[ -f "${stamp}" ]] && [[ "$(cat "${stamp}")" == "${source_digest}" ]] && \
   [[ -f "${output_root}/ice-9/eval.go" ]] && \
   [[ -f "${output_root}/ice-9/boot-9.go" ]] && \
   [[ -f "${output_root}/ice-9/psyntax-pp.go" ]]; then
  printf 'Guile bootstrap objects are current: %s\n' "${output_root}"
  exit 0
fi

rm -rf "${output_root}"
mkdir -p "${output_root}/ice-9"

export GUILE_AUTO_COMPILE=0

compile_bootstrap () {
  source_name=$1
  output_name=$2
  printf '\n===== PREBUILD %s -> %s =====\n' "${source_name}" "${output_name}"
  "${guild}" compile \
    --target="${target}" \
    -W1 -O2 \
    -L "${module_root}" \
    -o "${output_root}/${output_name}" \
    "${module_root}/${source_name}"
}

compile_bootstrap ice-9/eval.scm ice-9/eval.go
compile_bootstrap ice-9/psyntax.scm ice-9/psyntax-pp.go
compile_bootstrap ice-9/boot-9.scm ice-9/boot-9.go

printf '\n===== Bootstrap object validation =====\n'
for object in \
  "${output_root}/ice-9/eval.go" \
  "${output_root}/ice-9/psyntax-pp.go" \
  "${output_root}/ice-9/boot-9.go"
do
  file "${object}"
done

cat >"${output_root}/ATHENA-BOOTSTRAP-MANIFEST.txt" <<EOF
generator=ATHENA private Guile ${version}
generator_path=${guile}
target=${target}
bytecode_variant=64-bit-little-endian
module_source_sha256=${source_digest}
EOF
sha256sum \
  "${output_root}/ice-9/eval.go" \
  "${output_root}/ice-9/psyntax-pp.go" \
  "${output_root}/ice-9/boot-9.go" \
  >>"${output_root}/ATHENA-BOOTSTRAP-MANIFEST.txt"

printf '%s\n' "${source_digest}" >"${stamp}"
printf 'Guile bootstrap objects ready: %s\n' "${output_root}"

