#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

prefix="${1:-${ATHENA_WIN64_PREFIX:-${repo_root}/build_windows/prefix}}"
version="${ZSTD_VERSION:-1.5.7}"
source_dir="${ZSTD_SOURCE_DIR:-${repo_root}/build_windows/src/zstd-${version}}"
archive="${repo_root}/build_windows/src/zstd-${version}.tar.gz"
url="${ZSTD_URL:-https://github.com/facebook/zstd/releases/download/v${version}/zstd-${version}.tar.gz}"
build_dir="${ZSTD_BUILD_DIR:-${repo_root}/build_windows/deps-build/zstd}"

mkdir -p "$(dirname -- "${archive}")" "${prefix}"
prefix="$(cd -- "${prefix}" && pwd)"

if [[ ! -d "${source_dir}" ]]; then
  if [[ ! -f "${archive}" ]]; then
    curl -fL "${url}" -o "${archive}"
  fi
  tar -xf "${archive}" -C "$(dirname -- "${source_dir}")"
fi

rm -rf "${build_dir}"

cmake -S "${source_dir}/build/cmake" -B "${build_dir}" -G Ninja \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
  -DCMAKE_INSTALL_PREFIX="${prefix}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  -DZSTD_BUILD_SHARED=ON \
  -DZSTD_BUILD_STATIC=ON \
  -DZSTD_BUILD_PROGRAMS=OFF \
  -DZSTD_BUILD_TESTS=OFF

cmake --build "${build_dir}" -j"$(nproc)"
cmake --install "${build_dir}"

if [[ ! -f "${prefix}/lib/pkgconfig/libzstd.pc" ]]; then
  mkdir -p "${prefix}/lib/pkgconfig"
  cat > "${prefix}/lib/pkgconfig/libzstd.pc" <<EOF
prefix=${prefix}
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: zstd
Description: fast lossless compression algorithm library
Version: ${version}
Libs: -L\${libdir} -lzstd
Cflags: -I\${includedir}
EOF
fi

test -f "${prefix}/include/zstd.h"
test -f "${prefix}/lib/pkgconfig/libzstd.pc"
test -e "${prefix}/lib/libzstd.dll.a" -o -e "${prefix}/lib/libzstd.a"
