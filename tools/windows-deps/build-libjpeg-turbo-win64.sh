#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

prefix="${1:-${ATHENA_WIN64_PREFIX:-${repo_root}/build_windows/prefix}}"
version="${LIBJPEG_TURBO_VERSION:-3.1.2}"
source_dir="${LIBJPEG_TURBO_SOURCE_DIR:-${repo_root}/build_windows/src/libjpeg-turbo-${version}}"
archive="${repo_root}/build_windows/src/libjpeg-turbo-${version}.tar.gz"
url="${LIBJPEG_TURBO_URL:-https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/${version}.tar.gz}"
build_dir="${LIBJPEG_TURBO_BUILD_DIR:-${repo_root}/build_windows/deps-build/libjpeg-turbo}"

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
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  -DENABLE_SHARED=ON \
  -DENABLE_STATIC=ON \
  -DWITH_JPEG8=ON \
  -DWITH_TURBOJPEG=OFF

cmake --build "${build_dir}" -j"$(nproc)"
cmake --install "${build_dir}"

test -f "${prefix}/include/jpeglib.h"
test -e "${prefix}/lib/libjpeg.dll.a" -o -e "${prefix}/lib/libjpeg.a"
