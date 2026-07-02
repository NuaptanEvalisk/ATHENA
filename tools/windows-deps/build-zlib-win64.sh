#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

prefix="${1:-${ATHENA_WIN64_PREFIX:-${repo_root}/build_windows/prefix}}"
version="${ZLIB_VERSION:-1.3.1}"
source_dir="${ZLIB_SOURCE_DIR:-${repo_root}/build_windows/src/zlib-${version}}"
archive="${repo_root}/build_windows/src/zlib-${version}.tar.gz"
url="${ZLIB_URL:-https://zlib.net/fossils/zlib-${version}.tar.gz}"
build_dir="${ZLIB_BUILD_DIR:-${repo_root}/build_windows/deps-build/zlib}"

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
  -DCMAKE_INSTALL_PREFIX="${prefix}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY

cmake --build "${build_dir}" -j"$(nproc)"
cmake --install "${build_dir}"

if [[ ! -e "${prefix}/lib/libz.dll.a" && -e "${prefix}/lib/libzlib.dll.a" ]]; then
  cp -a "${prefix}/lib/libzlib.dll.a" "${prefix}/lib/libz.dll.a"
fi
if [[ ! -e "${prefix}/lib/libz.a" && -e "${prefix}/lib/libzlibstatic.a" ]]; then
  cp -a "${prefix}/lib/libzlibstatic.a" "${prefix}/lib/libz.a"
fi
if [[ ! -f "${prefix}/lib/pkgconfig/zlib.pc" ]]; then
  mkdir -p "${prefix}/lib/pkgconfig"
  cat >"${prefix}/lib/pkgconfig/zlib.pc" <<EOF
prefix=${prefix}
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: zlib
Description: zlib compression library
Version: ${version}
Libs: -L\${libdir} -lz
Cflags: -I\${includedir}
EOF
fi

test -f "${prefix}/include/zlib.h"
test -e "${prefix}/lib/libz.dll.a" -o -e "${prefix}/lib/libz.a"
