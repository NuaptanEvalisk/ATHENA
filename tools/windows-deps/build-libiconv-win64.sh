#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

prefix="${1:-${ATHENA_WIN64_PREFIX:-${repo_root}/build_windows/prefix}}"
version="${LIBICONV_VERSION:-1.18}"
source_dir="${LIBICONV_SOURCE_DIR:-${repo_root}/build_windows/src/libiconv-${version}}"
archive="${repo_root}/build_windows/src/libiconv-${version}.tar.gz"
url="${LIBICONV_URL:-https://ftp.gnu.org/pub/gnu/libiconv/libiconv-${version}.tar.gz}"
build_dir="${LIBICONV_BUILD_DIR:-${repo_root}/build_windows/deps-build/libiconv}"

mkdir -p "$(dirname -- "${archive}")" "${build_dir}" "${prefix}"
prefix="$(cd -- "${prefix}" && pwd)"

if [[ ! -d "${source_dir}" ]]; then
  if [[ ! -f "${archive}" ]]; then
    curl -fL "${url}" -o "${archive}"
  fi
  tar -xf "${archive}" -C "$(dirname -- "${source_dir}")"
fi

cd "${build_dir}"

"${source_dir}/configure" \
  --host=x86_64-w64-mingw32 \
  --build="$("${source_dir}/build-aux/config.guess")" \
  --prefix="${prefix}" \
  --libdir="${prefix}/lib" \
  --enable-shared \
  --enable-static \
  CC=x86_64-w64-mingw32-gcc \
  AR=x86_64-w64-mingw32-ar \
  RANLIB=x86_64-w64-mingw32-ranlib

make -j"$(nproc)"
make install

test -f "${prefix}/include/iconv.h"
test -e "${prefix}/lib/libiconv.dll.a" -o -e "${prefix}/lib/libiconv.a"
