#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

prefix="${1:-${ATHENA_WIN64_PREFIX:-${repo_root}/build_windows/prefix}}"
version="${LIBPNG_VERSION:-1.6.50}"
source_dir="${LIBPNG_SOURCE_DIR:-${repo_root}/build_windows/src/libpng-${version}}"
archive="${repo_root}/build_windows/src/libpng-${version}.tar.xz"
url="${LIBPNG_URL:-https://download.sourceforge.net/libpng/libpng-${version}.tar.xz}"
build_dir="${LIBPNG_BUILD_DIR:-${repo_root}/build_windows/deps-build/libpng}"

mkdir -p "$(dirname -- "${archive}")" "${prefix}"
prefix="$(cd -- "${prefix}" && pwd)"

if [[ ! -d "${source_dir}" ]]; then
  if [[ ! -f "${archive}" ]]; then
    curl -fL "${url}" -o "${archive}"
  fi
  tar -xf "${archive}" -C "$(dirname -- "${source_dir}")"
fi

rm -rf "${build_dir}"

cmake -S "${source_dir}" -B "${build_dir}" -G Ninja \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
  -DCMAKE_FIND_ROOT_PATH="${prefix}" \
  -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
  -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
  -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
  -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
  -DCMAKE_PREFIX_PATH="${prefix}" \
  -DCMAKE_INSTALL_PREFIX="${prefix}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  -DPNG_SHARED=ON \
  -DPNG_STATIC=ON \
  -DPNG_TESTS=OFF

cmake --build "${build_dir}" -j"$(nproc)"
cmake --install "${build_dir}"

test -f "${prefix}/include/png.h"
test -e "${prefix}/lib/libpng.dll.a" -o -e "${prefix}/lib/libpng.a" -o -e "${prefix}/lib/libpng16.dll.a"
