#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

prefix="${1:-${ATHENA_WIN64_PREFIX:-${repo_root}/build_windows/prefix}}"
source_dir="${TCC_SOURCE_DIR:-${repo_root}/build_windows/src/tinycc}"
build_dir="${TCC_BUILD_DIR:-${repo_root}/build_windows/deps-build/tinycc}"
repo="${TCC_REPO:-https://github.com/TinyCC/tinycc.git}"

mkdir -p "${prefix}" "$(dirname -- "${source_dir}")"
prefix="$(cd -- "${prefix}" && pwd)"

if [[ ! -d "${source_dir}/.git" ]]; then
  rm -rf "${source_dir}"
  git clone --depth 1 "${repo}" "${source_dir}"
fi

rm -rf "${build_dir}"
mkdir -p "${build_dir}"
cd "${build_dir}"

"${source_dir}/configure" \
  --source-path="${source_dir}" \
  --prefix="${prefix}" \
  --libdir="${prefix}/lib" \
  --includedir="${prefix}/include" \
  --tccdir="${prefix}/lib/tcc" \
  --cc=x86_64-w64-mingw32-gcc \
  --ar=x86_64-w64-mingw32-ar \
  --cpu=x86_64 \
  --targetos=WIN32 \
  --enable-static \
  --config-mingw32=yes

make -j"$(nproc)"
make install

install -Dm644 "${source_dir}/libtcc.h" "${prefix}/include/libtcc.h"

test -f "${prefix}/include/libtcc.h"
test -f "${prefix}/lib/libtcc.a"
