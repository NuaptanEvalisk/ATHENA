#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

ssh_target="${ATHENA_IPADOS_SSH_TARGET:-felix@127.0.0.1}"
ssh_port="${ATHENA_IPADOS_SSH_PORT:-2222}"
remote_developer_root="${ATHENA_IPADOS_REMOTE_DEVELOPER_ROOT:-/Users/felix/Developer}"
remote_source="${ATHENA_IPADOS_REMOTE_SOURCE:-${remote_developer_root}/ATHENA}"
jobs="${ATHENA_IPADOS_JOBS:-8}"

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

source_dir="$source_root/3rdparty/athena-bdwgc"
toolchain="$source_root/tools/ipados/iphoneos.toolchain.cmake"
build_dir="$developer_root/athena-deps/ipados/build/athena-bdwgc-8.2.12"
prefix="$developer_root/athena-deps/ipados/prefix/runtime"
artifact="$prefix/lib/libgc.a"

for tool in cmake ninja xcrun ar file; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "Missing required host tool: $tool" >&2
    exit 1
  }
done

test -f "$toolchain" || {
  echo "Missing iPadOS toolchain: $toolchain" >&2
  exit 1
}

mkdir -p "$build_dir" "$prefix" "$developer_root/athena-artifacts"

echo "======================================================================"
echo "Configuring private ATHENA BDW-GC 8.2.12 for arm64 iPadOS 27"
echo "======================================================================"
cmake -S "$source_dir" -B "$build_dir" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
  -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
  -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DCMAKE_INSTALL_LIBDIR=lib \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_FLAGS='-O3 -g -fPIC -fno-omit-frame-pointer -DGC_USE_ENTIRE_HEAP' \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DBUILD_SHARED_LIBS=OFF \
  -Dbuild_cord=OFF \
  -Dbuild_tests=OFF \
  -Denable_docs=OFF \
  -Denable_threads=ON \
  -Denable_parallel_mark=ON \
  -Denable_thread_local_alloc=ON \
  -Denable_threads_discovery=ON \
  -Denable_cplusplus=OFF \
  -Denable_throw_bad_alloc_library=OFF \
  -Denable_gcj_support=OFF \
  -Denable_large_config=ON \
  -Denable_mmap=ON \
  -Denable_munmap=OFF \
  -Denable_dynamic_loading=OFF \
  -Denable_handle_fork=OFF \
  -Ddisable_handle_fork=ON \
  -Dinstall_headers=ON

echo
echo "======================================================================"
echo "Building private ATHENA BDW-GC with ${jobs} jobs (verbose)"
echo "======================================================================"
cmake --build "$build_dir" --parallel "$jobs" --verbose

echo
echo "======================================================================"
echo "Installing private ATHENA BDW-GC target prefix (verbose)"
echo "======================================================================"
cmake --install "$build_dir" --verbose

test -f "$artifact" || {
  echo "BDW-GC static library was not produced: $artifact" >&2
  exit 1
}

pkgconfig=$(find "$prefix/lib/pkgconfig" -maxdepth 1 -type f \
  \( -name 'bdw-gc.pc' -o -name 'gc.pc' \) -print -quit 2>/dev/null || true)
test -n "$pkgconfig" || {
  echo "BDW-GC pkg-config metadata was not installed" >&2
  exit 1
}

echo
echo "======================================================================"
echo "Validating private ATHENA BDW-GC target artifact"
echo "======================================================================"
ls -lh "$artifact" "$pkgconfig"
file "$artifact"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
first_object=$(ar -t "$artifact" | grep -E '\.(o|obj)$' | head -1 || true)
test -n "$first_object" || {
  echo "No object file found in $artifact" >&2
  exit 1
}
(
  cd "$tmp"
  ar -x "$artifact" "$first_object"
  echo "Representative BDW-GC object: $first_object"
  file "$first_object"
  build_version=$(xcrun vtool -show-build "$first_object")
  printf '%s\n' "$build_version"
  printf '%s\n' "$build_version" | grep -Eq 'platform[[:space:]]+IOS'
  printf '%s\n' "$build_version" | grep -Eq 'minos[[:space:]]+27\.0'
)

printf '%s\n' "$prefix" > \
  "$developer_root/athena-artifacts/bdwgc-ipados-prefix.path"
echo "Private ATHENA BDW-GC iPadOS prefix ready: $prefix"
REMOTE
