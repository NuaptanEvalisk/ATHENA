#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

prefix="${1:-${ATHENA_WIN64_PREFIX:-${repo_root}/build_windows/prefix}}"
version="${GMP_VERSION:-6.3.0}"
source_dir="${GMP_SOURCE_DIR:-${repo_root}/build_windows/src/gmp-${version}}"
archive="${repo_root}/build_windows/src/gmp-${version}.tar.xz"
url="${GMP_URL:-https://gmplib.org/download/gmp/gmp-${version}.tar.xz}"
build_dir="${GMP_BUILD_DIR:-${repo_root}/build_windows/deps-build/gmp}"

mkdir -p "$(dirname -- "${archive}")" "${build_dir}" "${prefix}"
prefix="$(cd -- "${prefix}" && pwd)"

if [[ ! -d "${source_dir}" ]]; then
  if [[ ! -f "${archive}" ]]; then
    curl -fL "${url}" -o "${archive}"
  fi
  tar -xf "${archive}" -C "$(dirname -- "${source_dir}")"
fi

cd "${build_dir}"

rm -rf "${build_dir}"
mkdir -p "${build_dir}"
cd "${build_dir}"

"${source_dir}/configure" \
  --host=x86_64-w64-mingw32 \
  --build="$("${source_dir}/config.guess")" \
  --prefix="${prefix}" \
  --libdir="${prefix}/lib" \
  --disable-shared \
  --enable-static \
  ABI=64 \
  CC=x86_64-w64-mingw32-gcc \
  CXX=x86_64-w64-mingw32-g++ \
  CC_FOR_BUILD=gcc \
  CPP_FOR_BUILD="gcc -E"

make -j"$(nproc)"
make install

test -f "${prefix}/include/gmp.h"
test -f "${prefix}/lib/libgmp.a"
