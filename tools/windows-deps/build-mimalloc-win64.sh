#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

prefix="${1:-${ATHENA_WIN64_PREFIX:-${repo_root}/build_windows/prefix}}"
source_dir="${MIMALLOC_SOURCE_DIR:-${repo_root}/build_windows/src/mimalloc}"
build_dir="${MIMALLOC_BUILD_DIR:-${repo_root}/build_windows/deps-build/mimalloc}"
tag="${MIMALLOC_TAG:-v3.3.1}"

mkdir -p "$(dirname -- "${source_dir}")" "${build_dir}" "${prefix}"
prefix="$(cd -- "${prefix}" && pwd)"

if [[ ! -d "${source_dir}/.git" ]]; then
  git clone --depth 1 --branch "${tag}" \
    https://github.com/microsoft/mimalloc.git "${source_dir}"
fi

cmake -S "${source_dir}" -B "${build_dir}" -G Ninja \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
  -DCMAKE_INSTALL_PREFIX="${prefix}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DMI_BUILD_TESTS=OFF \
  -DMI_BUILD_OBJECT=OFF \
  -DMI_BUILD_SHARED=ON \
  -DMI_BUILD_STATIC=OFF \
  -DMI_WIN_REDIRECT=OFF

cmake --build "${build_dir}" -j"$(nproc)"
cmake --install "${build_dir}"

if [[ -f "${prefix}/include/mimalloc-3.3/mimalloc.h" ]]; then
  ln -sf mimalloc-3.3/mimalloc.h "${prefix}/include/mimalloc.h"
fi

if ! compgen -G "${prefix}/lib/cmake/mimalloc*/mimalloc-config.cmake" >/dev/null; then
  echo "mimalloc-config.cmake was not installed under ${prefix}/lib/cmake" >&2
  exit 1
fi
