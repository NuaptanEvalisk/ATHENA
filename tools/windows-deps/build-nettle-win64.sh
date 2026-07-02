#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

prefix="${1:-${ATHENA_WIN64_PREFIX:-${repo_root}/build_windows/prefix}}"
version="${NETTLE_VERSION:-3.10.2}"
source_dir="${NETTLE_SOURCE_DIR:-${repo_root}/build_windows/src/nettle-${version}}"
archive="${repo_root}/build_windows/src/nettle-${version}.tar.gz"
url="${NETTLE_URL:-https://ftp.gnu.org/gnu/nettle/nettle-${version}.tar.gz}"
build_dir="${NETTLE_BUILD_DIR:-${repo_root}/build_windows/deps-build/nettle}"

mkdir -p "$(dirname -- "${archive}")" "${build_dir}" "${prefix}"
prefix="$(cd -- "${prefix}" && pwd)"

if [[ ! -d "${source_dir}" ]]; then
  if [[ ! -f "${archive}" ]]; then
    curl -fL "${url}" -o "${archive}"
  fi
  tar -xf "${archive}" -C "$(dirname -- "${source_dir}")"
fi

cd "${build_dir}"

PKG_CONFIG_LIBDIR="${prefix}/lib/pkgconfig" \
PKG_CONFIG_SYSROOT_DIR="${prefix}" \
"${source_dir}/configure" \
  --host=x86_64-w64-mingw32 \
  --build="$("${source_dir}/config.guess")" \
  --prefix="${prefix}" \
  --libdir="${prefix}/lib" \
  --disable-shared \
  --enable-static \
  --disable-documentation \
  CC=x86_64-w64-mingw32-gcc \
  AR=x86_64-w64-mingw32-ar \
  RANLIB=x86_64-w64-mingw32-ranlib \
  CPPFLAGS="-I${prefix}/include" \
  LDFLAGS="-L${prefix}/lib"

make -j"$(nproc)"
make install

test -f "${prefix}/include/nettle/nettle-types.h"
test -f "${prefix}/lib/libnettle.a"
test -f "${prefix}/lib/libhogweed.a"
