#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

ssh_target="${ATHENA_IPADOS_SSH_TARGET:-felix@127.0.0.1}"
ssh_port="${ATHENA_IPADOS_SSH_PORT:-2222}"
remote_developer_root="${ATHENA_IPADOS_REMOTE_DEVELOPER_ROOT:-/Users/felix/Developer}"
remote_source="${ATHENA_IPADOS_REMOTE_SOURCE:-${remote_developer_root}/ATHENA}"
jobs="${ATHENA_IPADOS_JOBS:-20}"

"${script_dir}/bootstrap-vm.sh" --quiet
"${script_dir}/sync-source-to-vm.sh"

ssh -o BatchMode=yes -o ConnectTimeout=10 -p "${ssh_port}" \
  "${ssh_target}" sh -s -- \
  "${remote_developer_root}" "${remote_source}" "${jobs}" <<'REMOTE'
set -eu

developer_root=$1
source_root=$2
jobs=$3

export PATH="/Users/felix/.cargo/bin:/opt/local/bin:/opt/local/sbin:$PATH"
export LC_ALL=en_US.UTF-8
export LANG=en_US.UTF-8
export IPHONEOS_DEPLOYMENT_TARGET=27.0

manifest="$source_root/tools/materials-engine/Cargo.toml"
lockfile="$source_root/tools/materials-engine/Cargo.lock"
target_dir="$developer_root/athena-deps/ipados/build/materials-engine-cargo"
artifact="$target_dir/aarch64-apple-ios/release/libathena_materials.a"

for tool in cargo rustc xcrun ar nm file; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "Missing required host tool: $tool" >&2
    exit 1
  }
done

rustup target list --installed | grep -qx aarch64-apple-ios || {
  echo "Rust target aarch64-apple-ios is not installed" >&2
  exit 1
}

mkdir -p "$target_dir" "$developer_root/athena-artifacts"

# Rust's aarch64-apple-ios target defaults to an old deployment version unless
# IPHONEOS_DEPLOYMENT_TARGET is explicit.  Invalidate only the device-target
# output if this build tree predates ATHENA's iPadOS 27 contract; keep host
# proc-macro/build-script artifacts so subsequent rebuilds stay incremental.
if [ -f "$artifact" ]; then
  tmp_check=$(mktemp -d)
  existing_object=$(ar -t "$artifact" | grep -E '^athena_materials-.*\.o$' | head -1 || true)
  if [ -n "$existing_object" ]; then
    (
      cd "$tmp_check"
      ar -x "$artifact" "$existing_object"
      xcrun vtool -show-build "$existing_object" 2>/dev/null || true
    ) >"$tmp_check/build-version.txt"
  fi
  if ! grep -Eq 'minos[[:space:]]+27\.0' "$tmp_check/build-version.txt" 2>/dev/null; then
    echo "Discarding pre-iPadOS-27 Materials target objects; host Cargo cache is retained."
    rm -rf "$target_dir/aarch64-apple-ios"
  fi
  rm -rf "$tmp_check"
fi

echo "======================================================================"
echo "Building ATHENA Materials for aarch64-apple-ios"
echo "======================================================================"
cargo build \
  --manifest-path "$manifest" \
  --target-dir "$target_dir" \
  --target aarch64-apple-ios \
  --release \
  --locked \
  --lib \
  -j "$jobs" \
  -vv

test -f "$artifact" || {
  echo "Materials static library was not produced: $artifact" >&2
  exit 1
}

echo
echo "======================================================================"
echo "Validating Materials target artifact"
echo "======================================================================"
ls -lh "$artifact"
file "$artifact"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
athena_objects=$(ar -t "$artifact" | grep -E '^athena_materials-.*\.o$' || true)
[ -n "$athena_objects" ] || {
  echo "No ATHENA Materials Rust objects were found in $artifact" >&2
  exit 1
}
(
  cd "$tmp"
  # Archive member names contain no whitespace; extract only ATHENA-owned
  # objects so Apple's older LLVM reader never scans Rust std/compiler objects.
  # shellcheck disable=SC2086
  ar -x "$artifact" $athena_objects

  nm -g athena_materials-*.o | grep -E \
    '(_athena_materials_call|_athena_materials_free)$' \
    | sort -u

  symbols=$(nm -g athena_materials-*.o | grep -E \
    '(_athena_materials_call|_athena_materials_free)$' \
    | sed -E 's/.* (_athena_materials_(call|free))$/\1/' \
    | sort -u | wc -l | tr -d ' ')
  [ "$symbols" = 2 ] || {
    echo "Materials C ABI symbols are incomplete" >&2
    exit 1
  }

  first_object=$(printf '%s\n' $athena_objects | head -1)
  echo "Representative Rust object: $first_object"
  file "$first_object"
  build_version=$(xcrun vtool -show-build "$first_object")
  printf '%s\n' "$build_version"
  printf '%s\n' "$build_version" | grep -Eq 'platform[[:space:]]+IOS'
  printf '%s\n' "$build_version" | grep -Eq 'minos[[:space:]]+27\.0'
)

printf '%s\n' "$artifact" > \
  "$developer_root/athena-artifacts/materials-aarch64-apple-ios.path"
echo "Materials iPadOS library ready: $artifact"
REMOTE
