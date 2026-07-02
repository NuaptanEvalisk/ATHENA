#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

prefix="${1:-${ATHENA_WIN64_PREFIX:-${repo_root}/build_windows/prefix}}"
version="${LIBTASN1_VERSION:-4.20.0}"
source_dir="${LIBTASN1_SOURCE_DIR:-${repo_root}/build_windows/src/libtasn1-${version}}"
archive="${repo_root}/build_windows/src/libtasn1-${version}.tar.gz"
url="${LIBTASN1_URL:-https://ftp.gnu.org/gnu/libtasn1/libtasn1-${version}.tar.gz}"
build_dir="${LIBTASN1_BUILD_DIR:-${repo_root}/build_windows/deps-build/libtasn1}"

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
  --disable-shared \
  --enable-static \
  --disable-doc \
  CC=x86_64-w64-mingw32-gcc \
  AR=x86_64-w64-mingw32-ar \
  RANLIB=x86_64-w64-mingw32-ranlib

make -j"$(nproc)"
make install

test -f "${prefix}/include/libtasn1.h"
test -f "${prefix}/lib/libtasn1.a"
