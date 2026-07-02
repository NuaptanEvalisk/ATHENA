#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

prefix="${1:-${ATHENA_WIN64_PREFIX:-${repo_root}/build_windows/prefix}}"
version="${RESVG_VERSION:-0.47.0}"
source_dir="${RESVG_SOURCE_DIR:-${repo_root}/build_windows/src/resvg}"
target_dir="${RESVG_TARGET_DIR:-${repo_root}/build_windows/deps-build/resvg-cargo-target}"
repo="${RESVG_REPO:-https://github.com/linebender/resvg.git}"

mkdir -p "${prefix}" "$(dirname -- "${source_dir}")" "${target_dir}"
prefix="$(cd -- "${prefix}" && pwd)"

if [[ ! -d "${source_dir}/.git" ]]; then
  rm -rf "${source_dir}"
  git clone --depth 1 --branch "v${version}" "${repo}" "${source_dir}"
fi

export CARGO_TARGET_DIR="${target_dir}"
export RUSTC="${HOME}/.rustup/toolchains/1.87.0-x86_64-unknown-linux-gnu/bin/rustc"
export CARGO_TARGET_X86_64_UNKNOWN_LINUX_GNU_LINKER=gcc
export CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER=x86_64-w64-mingw32-gcc
export CC_x86_64_pc_windows_gnu=x86_64-w64-mingw32-gcc
export AR_x86_64_pc_windows_gnu=x86_64-w64-mingw32-ar

"${HOME}/.rustup/toolchains/1.87.0-x86_64-unknown-linux-gnu/bin/cargo" build \
  --manifest-path "${source_dir}/Cargo.toml" \
  --package resvg-capi \
  --release \
  --target x86_64-pc-windows-gnu

install -Dm644 "${source_dir}/crates/c-api/resvg.h" "${prefix}/include/resvg.h"
install -Dm644 "${source_dir}/crates/c-api/ResvgQt.h" "${prefix}/include/ResvgQt.h"

if [[ -f "${target_dir}/x86_64-pc-windows-gnu/release/resvg.dll" ]]; then
  install -Dm755 "${target_dir}/x86_64-pc-windows-gnu/release/resvg.dll" \
    "${prefix}/bin/resvg.dll"
fi
if [[ -f "${target_dir}/x86_64-pc-windows-gnu/release/libresvg.dll.a" ]]; then
  install -Dm644 "${target_dir}/x86_64-pc-windows-gnu/release/libresvg.dll.a" \
    "${prefix}/lib/libresvg.dll.a"
fi
if [[ -f "${target_dir}/x86_64-pc-windows-gnu/release/libresvg.a" ]]; then
  install -Dm644 "${target_dir}/x86_64-pc-windows-gnu/release/libresvg.a" \
    "${prefix}/lib/libresvg.a"
fi

test -f "${prefix}/include/ResvgQt.h"
test -e "${prefix}/lib/libresvg.dll.a" -o -e "${prefix}/lib/libresvg.a"
