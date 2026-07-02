#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

prefix="${1:-${ATHENA_WIN64_PREFIX:-${repo_root}/build_windows/prefix}}"
version="${FREETYPE_VERSION:-2.13.3}"
source_dir="${FREETYPE_SOURCE_DIR:-${repo_root}/build_windows/src/freetype-${version}}"
archive="${repo_root}/build_windows/src/freetype-${version}.tar.xz"
url="${FREETYPE_URL:-https://download.savannah.gnu.org/releases/freetype/freetype-${version}.tar.xz}"
build_dir="${FREETYPE_BUILD_DIR:-${repo_root}/build_windows/deps-build/freetype}"

mkdir -p "$(dirname -- "${archive}")" "${build_dir}" "${prefix}"
prefix="$(cd -- "${prefix}" && pwd)"

if [[ ! -d "${source_dir}" ]]; then
  if [[ ! -f "${archive}" ]]; then
    curl -fL "${url}" -o "${archive}"
  fi
  tar -xf "${archive}" -C "$(dirname -- "${source_dir}")"
fi

cmake -S "${source_dir}" -B "${build_dir}" -G Ninja \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
  -DCMAKE_INSTALL_PREFIX="${prefix}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON \
  -DFT_DISABLE_ZLIB=TRUE \
  -DFT_DISABLE_BZIP2=TRUE \
  -DFT_DISABLE_PNG=TRUE \
  -DFT_DISABLE_HARFBUZZ=TRUE \
  -DFT_DISABLE_BROTLI=TRUE

cmake --build "${build_dir}" -j"$(nproc)"
cmake --install "${build_dir}"

test -f "${prefix}/include/freetype2/freetype/freetype.h"
test -f "${prefix}/lib/libfreetype.dll.a"
