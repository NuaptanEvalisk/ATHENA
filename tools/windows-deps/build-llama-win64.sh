#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

prefix="${1:-${ATHENA_WIN64_PREFIX:-${repo_root}/build_windows/prefix}}"
source_dir="${LLAMA_CPP_SOURCE_DIR:-/home/felix/data/Software/llamacpp/llama.cpp}"
build_dir="${LLAMA_BUILD_DIR:-${repo_root}/build_windows/deps-build/llama}"

if [[ ! -f "${source_dir}/CMakeLists.txt" ]]; then
  echo "llama.cpp source tree not found: ${source_dir}" >&2
  exit 2
fi

mkdir -p "${prefix}"
prefix="$(cd -- "${prefix}" && pwd)"

rm -rf "${build_dir}"

cmake -S "${source_dir}" -B "${build_dir}" -G Ninja \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
  -DCMAKE_FIND_ROOT_PATH="${prefix}" \
  -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
  -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
  -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
  -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
  -DCMAKE_INSTALL_PREFIX="${prefix}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  -DBUILD_SHARED_LIBS=OFF \
  -DLLAMA_BUILD_COMMON=ON \
  -DLLAMA_BUILD_EXAMPLES=OFF \
  -DLLAMA_BUILD_SERVER=OFF \
  -DLLAMA_BUILD_TESTS=OFF \
  -DLLAMA_BUILD_TOOLS=OFF \
  -DLLAMA_OPENSSL=OFF \
  -DGGML_NATIVE=OFF \
  -DGGML_OPENMP=OFF \
  -DGGML_STATIC=ON \
  -DGGML_LLAMAFILE=OFF \
  -DGGML_BUILD_EXAMPLES=OFF \
  -DGGML_BUILD_TESTS=OFF

cmake --build "${build_dir}" -j"$(nproc)"
cmake --install "${build_dir}"

# llama.cpp installs the public llama/ggml surface, but the common utility
# library is currently a build-only target.  ATHENA's CMake requires it directly.
cp -f "${build_dir}/common/libcommon.a" "${prefix}/lib/libllama-common.a"
mkdir -p "${prefix}/include/llama-common"
cp -f "${source_dir}/common/"*.h "${prefix}/include/llama-common/"
cp -f "${source_dir}/common/common.h" "${prefix}/include/common.h"

test -f "${prefix}/lib/pkgconfig/llama.pc"
test -f "${prefix}/lib/libllama.a"
test -f "${prefix}/lib/libllama-common.a"
test -f "${prefix}/include/common.h"
