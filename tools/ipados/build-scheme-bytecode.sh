#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

build_root="${ATHENA_LINUX_BUILD_ROOT:-${repo_root}/build_qt6}"
jobs="${ATHENA_IPADOS_JOBS:-20}"
runtime_id="athena-guile-3.0.10-ipados-arm64-nojit"
scheme_target="aarch64-apple-darwin"
output="${build_root}/ipados-scheme/${runtime_id}"
compile_home="${build_root}/ipados-scheme-compile-home"
binary="${ATHENA_LINUX_COMPILER_BINARY:-${build_root}/src/ATHENA.bin}"
guile_runtime="${build_root}/athena-guile-runtime"
source_root="${repo_root}/ATHENA/progs"

ssh_target="${ATHENA_IPADOS_SSH_TARGET:-felix@127.0.0.1}"
ssh_port="${ATHENA_IPADOS_SSH_PORT:-2222}"
remote_developer_root="${ATHENA_IPADOS_REMOTE_DEVELOPER_ROOT:-/Users/felix/Developer}"
remote_output="${remote_developer_root}/athena-deps/ipados/prefix/athena-scheme/${runtime_id}"

banner () {
  printf '\n\n======================================================================\n'
  printf '%s\n' "$*"
  printf '======================================================================\n'
}

cache_value () {
  local key=$1
  awk -v key="$key" '
    index($0, key ":") == 1 {
      sub(/^[^=]*=/, "")
      print
      exit
    }
  ' "${build_root}/CMakeCache.txt"
}

for tool in cmake python3 sha256sum rsync ssh file; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "Missing Linux host tool: $tool" >&2
    exit 1
  }
done

test -f "${build_root}/CMakeCache.txt" || {
  echo "Linux ATHENA build tree is not configured: ${build_root}" >&2
  exit 1
}

banner "Building Linux ATHENA bytecode compiler target with ${jobs} jobs"
cmake --build "${build_root}" --target ATHENA.bin -j"${jobs}"

for required in \
    "$binary" \
    "$guile_runtime/bin/guile" \
    "$guile_runtime/lib/libathena-guile.so.1"; do
  test -e "$required" || {
    echo "Missing bytecode compiler input: $required" >&2
    exit 1
  }
done

llama_library=$(cache_value LLAMA_CPP_LIBRARY)
resvg_library=$(cache_value RESVG_LIBRARY)
test -f "$llama_library" || {
  echo "Configured llama.cpp runtime is missing: $llama_library" >&2
  exit 1
}
test -f "$resvg_library" || {
  echo "Configured resvg runtime is missing: $resvg_library" >&2
  exit 1
}
llama_runtime=$(dirname -- "$llama_library")
resvg_runtime=$(dirname -- "$resvg_library")
ads_runtime="${build_root}/x64/lib"
mkdir -p "$ads_runtime" "$output" "$compile_home"

banner "Compiling ATHENA Scheme for ${scheme_target} using the full Linux bootstrap"
ATHENA_SCHEME_TARGET="$scheme_target" \
ATHENA_SCHEME_OPTIMIZATION_LEVEL="${ATHENA_SCHEME_OPTIMIZATION_LEVEL:-1}" \
  /bin/bash "${repo_root}/tools/compile-athena-scheme-bytecode.sh" \
  "$binary" \
  "$output" \
  "${repo_root}/ATHENA" \
  "$compile_home" \
  "$guile_runtime" \
  "$source_root" \
  "$llama_runtime" \
  "$ads_runtime" \
  "$resvg_runtime" \
  "$jobs" \
  "$runtime_id"

expected=$(find "$source_root" -type f -name '*.scm' -printf '.\n' | wc -l)
actual=$(find "$output" -type f -name '*.go' -printf '.\n' | wc -l)
[ "$expected" -eq "$actual" ] || {
  echo "Target Scheme inventory mismatch: expected $expected, got $actual" >&2
  exit 1
}

first_line=$(sed -n '1p' "$output/.complete")
[ "$first_line" = "$runtime_id" ] || {
  echo "Unexpected target Scheme runtime id: $first_line" >&2
  exit 1
}

# The generated tree will ultimately live on a normal macOS/iOS bundle path.
# Fail before transfer if Linux source names would alias on a case-insensitive
# staging filesystem.
python3 - "$output" <<'PY'
import os
import sys

root = sys.argv[1]
seen = {}
for current, dirs, files in os.walk(root):
    for name in dirs + files:
        path = os.path.relpath(os.path.join(current, name), root)
        key = path.casefold()
        previous = seen.get(key)
        if previous is not None and previous != path:
            raise SystemExit(
                f"case-insensitive target Scheme collision: {previous!r} <-> {path!r}")
        seen[key] = path
PY

manifest="$output/.athena-build-manifest"
source_digest=$(
  find "$source_root" -type f -name '*.scm' -print0 \
    | sort -z \
    | xargs -0 sha256sum \
    | sha256sum \
    | awk '{print $1}'
)
compiler_digest=$(
  sha256sum "$binary" "$guile_runtime/bin/guile" \
    "$guile_runtime/lib/libathena-guile.so.1" \
    | sha256sum | awk '{print $1}'
)
cat >"$manifest.tmp" <<EOF
format=athena-ipados-scheme-bytecode-v1
runtime_id=${runtime_id}
guile_target=${scheme_target}
guile_word_size=8
guile_byte_order=little
scheme_source_sha256=${source_digest}
compiler_sha256=${compiler_digest}
scheme_files=${actual}
linux_head=$(git -C "$repo_root" rev-parse HEAD)
EOF
mv -f "$manifest.tmp" "$manifest"

banner "Validating representative target Guile bytecode"
for relative in \
    convert/latex/latex-drd.go \
    kernel/athena/tm-define.go \
    athena/athena/tm-websites.go; do
  test -f "$output/$relative" || {
    echo "Missing representative target bytecode: $relative" >&2
    exit 1
  }
  file "$output/$relative"
done

validation_script="$compile_home/validate-ipados-bytecode.scm"
cat >"$validation_script" <<'SCM'
(use-modules (system vm elf) (rnrs io ports) (ice-9 ftw))
(define root (cadr (command-line)))
(define (validate path)
  (call-with-input-file path
    (lambda (port)
      (let ((elf (parse-elf (get-bytevector-all port))))
        (unless (= (elf-word-size elf) 8)
          (error "unexpected Guile bytecode word size" path (elf-word-size elf)))
        (unless (eq? (elf-byte-order elf) 'little)
          (error "unexpected Guile bytecode byte order" path (elf-byte-order elf)))))))
(ftw root
  (lambda (path statinfo flag)
    (when (and (eq? flag 'regular)
               (string-suffix? ".go" path))
      (validate path))
    #t))
(format #t "Validated target Guile ELF ABI for all packaged .go files.~%")
SCM
GUILE_AUTO_COMPILE=0 \
  "$guile_runtime/bin/guile" --no-auto-compile -s "$validation_script" "$output"

banner "Incrementally publishing target Scheme bytecode to the macOS VM"
"${script_dir}/bootstrap-vm.sh" --quiet
ssh -o BatchMode=yes -o ConnectTimeout=10 -p "$ssh_port" "$ssh_target" \
  "mkdir -p '$remote_output'"

rsync_args=(
  -a --delete --checksum --stats
  -e "ssh -o BatchMode=yes -o ConnectTimeout=10 -p ${ssh_port}"
)
printf '%s\n' 'Transfer preview:'
rsync -n "${rsync_args[@]}" "$output/" "$ssh_target:$remote_output/"
printf '%s\n' 'Transfer:'
rsync "${rsync_args[@]}" "$output/" "$ssh_target:$remote_output/"

local_tree=$(python3 - "$output" <<'PY'
import hashlib
import os
import sys

root = sys.argv[1]
h = hashlib.sha256()
for current, dirs, files in os.walk(root):
    dirs.sort()
    files.sort()
    for name in files:
        path = os.path.join(current, name)
        rel = os.path.relpath(path, root).replace(os.sep, "/")
        h.update(rel.encode("utf-8"))
        h.update(b"\0")
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1024 * 1024), b""):
                h.update(chunk)
        h.update(b"\0")
print(h.hexdigest())
PY
)
remote_tree=$(ssh -o BatchMode=yes -o ConnectTimeout=10 -p "$ssh_port" \
  "$ssh_target" python3 - "$remote_output" <<'PY'
import hashlib
import os
import sys

root = sys.argv[1]
h = hashlib.sha256()
for current, dirs, files in os.walk(root):
    dirs.sort()
    files.sort()
    for name in files:
        path = os.path.join(current, name)
        rel = os.path.relpath(path, root).replace(os.sep, "/")
        h.update(rel.encode("utf-8"))
        h.update(b"\0")
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1024 * 1024), b""):
                h.update(chunk)
        h.update(b"\0")
print(h.hexdigest())
PY
)
[ "$local_tree" = "$remote_tree" ] || {
  echo "Published target Scheme bytecode hash mismatch" >&2
  echo "Linux: $local_tree" >&2
  echo "VM:    $remote_tree" >&2
  exit 1
}

printf '\nTarget Scheme bytecode ready.\n'
printf '  Linux: %s\n' "$output"
printf '  VM:    %s:%s\n' "$ssh_target" "$remote_output"
printf '  Files: %s\n' "$actual"
printf '  SHA:   %s\n' "$local_tree"
