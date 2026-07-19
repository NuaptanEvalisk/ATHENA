#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

prefix="${1:-${ATHENA_WIN64_PREFIX:-${repo_root}/build_windows/prefix}}"
version="${LIBSODIUM_VERSION:-1.0.20}"
source_dir="${LIBSODIUM_SOURCE_DIR:-${repo_root}/build_windows/src/libsodium-${version}}"
archive="${repo_root}/build_windows/src/libsodium-${version}.tar.gz"
url="${LIBSODIUM_URL:-https://github.com/jedisct1/libsodium/releases/download/${version}-RELEASE/libsodium-${version}.tar.gz}"
build_dir="${LIBSODIUM_BUILD_DIR:-${repo_root}/build_windows/deps-build/libsodium}"

mkdir -p "$(dirname -- "${archive}")" "${prefix}"
prefix="$(cd -- "${prefix}" && pwd)"

if [[ ! -d "${source_dir}" ]]; then
  if [[ ! -f "${archive}" ]]; then
    curl -fL "${url}" -o "${archive}"
  fi
  tar -xf "${archive}" -C "$(dirname -- "${source_dir}")"
fi

rm -rf "${build_dir}"
mkdir -p "${build_dir}"

pushd "${build_dir}" >/dev/null
"${source_dir}/configure" \
  --host=x86_64-w64-mingw32 \
  --prefix="${prefix}" \
  --enable-shared \
  --enable-static
make -j"$(nproc)"
make install
popd >/dev/null

test -f "${prefix}/include/sodium.h"
test -f "${prefix}/lib/pkgconfig/libsodium.pc" -o \
     -f "${prefix}/lib64/pkgconfig/libsodium.pc"
test -e "${prefix}/lib/libsodium.dll.a" -o \
     -e "${prefix}/lib/libsodium.a" -o \
     -e "${prefix}/lib64/libsodium.dll.a" -o \
     -e "${prefix}/lib64/libsodium.a"
