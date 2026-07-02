#!/usr/bin/env bash
set -euo pipefail

prefix="${1:-${ATHENA_WIN64_PREFIX:-}}"
if [[ -z "${prefix}" ]]; then
  echo "usage: $0 /path/to/x86_64-w64-mingw32-prefix" >&2
  exit 2
fi

missing=0

require_path() {
  local label="$1"
  local pattern="$2"
  if ! compgen -G "${prefix}/${pattern}" >/dev/null; then
    printf 'missing: %-28s %s/%s\n' "${label}" "${prefix}" "${pattern}" >&2
    missing=1
  else
    printf 'found:   %-28s %s/%s\n' "${label}" "${prefix}" "${pattern}"
  fi
}

require_pkg() {
  local module="$1"
  if ! PKG_CONFIG_LIBDIR="${prefix}/lib/pkgconfig:${prefix}/lib64/pkgconfig:${prefix}/share/pkgconfig" \
       PKG_CONFIG_SYSROOT_DIR="${prefix}" \
       pkg-config --exists "${module}"; then
    printf 'missing: pkg-config module          %s\n' "${module}" >&2
    missing=1
  else
    printf 'found:   pkg-config module          %s\n' "${module}"
  fi
}

require_path "Qt6Config.cmake" "lib/cmake/Qt6/Qt6Config.cmake"
require_path "mimalloc config" "lib/cmake/mimalloc*/mimalloc-config.cmake"
require_path "ResvgQt.h" "include/ResvgQt.h"
require_path "libresvg" "lib/libresvg*"
require_path "libtcc.h" "include/libtcc.h"
require_path "libtcc" "lib/libtcc*"
require_path "llama common.h" "include/common.h"
require_path "llama-common library" "lib/libllama-common*"

require_pkg "spdlog"
require_pkg "llama"
require_pkg "libzstd"
require_pkg "MagickWand"

require_pkg "guile-1.8"

exit "${missing}"
