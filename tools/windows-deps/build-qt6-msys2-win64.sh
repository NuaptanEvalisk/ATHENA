#!/usr/bin/env bash
set -euo pipefail

prefix="${1:-${ATHENA_WIN64_PREFIX:-}}"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
download_dir="${ATHENA_WIN64_DOWNLOAD_DIR:-${repo_root}/build_windows/downloads/msys2}"
base_url="${MSYS2_MINGW64_URL:-https://repo.msys2.org/mingw/mingw64}"
qt6_host_prefix="${ATHENA_QT6_HOST_PREFIX:-${repo_root}/container_build/deps/qt/6.11.1/gcc_64}"

if [[ -z "${prefix}" ]]; then
  echo "usage: $0 /path/to/x86_64-w64-mingw32-prefix" >&2
  exit 2
fi

if ! command -v bsdtar >/dev/null 2>&1; then
  echo "bsdtar is required to extract MSYS2 .pkg.tar.zst archives" >&2
  exit 2
fi

prefix="$(mkdir -p "${prefix}" && cd -- "${prefix}" && pwd)"
mkdir -p "${download_dir}"

packages=(
  mingw-w64-x86_64-gcc-libs-16.1.0-5-any.pkg.tar.zst
  mingw-w64-x86_64-libwinpthread-12.0.0.r747.g1a99f8514-1-any.pkg.tar.zst
  mingw-w64-x86_64-winpthreads-git-12.0.0.r747.g1a99f8514-1-any.pkg.tar.zst
  mingw-w64-x86_64-libb2-0.98.1-3-any.pkg.tar.zst
  mingw-w64-x86_64-double-conversion-3.4.0-1-any.pkg.tar.zst
  mingw-w64-x86_64-icu-78.3-3-any.pkg.tar.zst
  mingw-w64-x86_64-pcre2-10.47-1-any.pkg.tar.zst
  mingw-w64-x86_64-zlib-1.3.2-2-any.pkg.tar.zst
  mingw-w64-x86_64-brotli-1.2.0-1-any.pkg.tar.zst
  mingw-w64-x86_64-bzip2-1.0.8-3-any.pkg.tar.zst
  mingw-w64-x86_64-freetype-2.14.3-1-any.pkg.tar.zst
  mingw-w64-x86_64-gettext-runtime-1.0-1-any.pkg.tar.zst
  mingw-w64-x86_64-glib2-2.88.1-1-any.pkg.tar.zst
  mingw-w64-x86_64-graphite2-1.3.15-1-any.pkg.tar.zst
  mingw-w64-x86_64-harfbuzz-14.2.1-1-any.pkg.tar.zst
  mingw-w64-x86_64-libffi-3.6.0-1-any.pkg.tar.zst
  mingw-w64-x86_64-libpng-1.6.58-1-any.pkg.tar.zst
  mingw-w64-x86_64-md4c-0.5.3-1-any.pkg.tar.zst
  mingw-w64-x86_64-qt6-base-6.11.1-1-any.pkg.tar.zst
  mingw-w64-x86_64-qt6-svg-6.11.1-1-any.pkg.tar.zst
  mingw-w64-x86_64-qt6-5compat-6.11.1-1-any.pkg.tar.zst
)

for package in "${packages[@]}"; do
  archive="${download_dir}/${package}"
  if [[ ! -s "${archive}" ]]; then
    curl -L --fail --retry 3 --retry-delay 2 \
      -o "${archive}" \
      "${base_url}/${package}"
  fi
  bsdtar -xf "${archive}" -C "${prefix}" --strip-components 1 mingw64
done

test -f "${prefix}/lib/cmake/Qt6/Qt6Config.cmake"
test -f "${prefix}/lib/cmake/Qt6Core/Qt6CoreConfig.cmake"
test -f "${prefix}/lib/cmake/Qt6Widgets/Qt6WidgetsConfig.cmake"
test -f "${prefix}/lib/cmake/Qt6Svg/Qt6SvgConfig.cmake"
test -f "${prefix}/lib/cmake/Qt6Core5Compat/Qt6Core5CompatConfig.cmake"

host_moc="${qt6_host_prefix}/libexec/moc"
host_rcc="${qt6_host_prefix}/libexec/rcc"
host_uic="${qt6_host_prefix}/libexec/uic"
if [[ -x "${host_moc}" && -x "${host_rcc}" && -x "${host_uic}" ]]; then
  perl -0pi -e "s#\\\$\\{_IMPORT_PREFIX\\}/share/qt6/bin/moc\\.exe#${host_moc}#g; s#\\\$\\{_IMPORT_PREFIX\\}/share/qt6/bin/rcc\\.exe#${host_rcc}#g" \
    "${prefix}/lib/cmake/Qt6CoreTools/Qt6CoreToolsTargets-relwithdebinfo.cmake"
  perl -0pi -e "s#\\\$\\{_IMPORT_PREFIX\\}/share/qt6/bin/uic\\.exe#${host_uic}#g" \
    "${prefix}/lib/cmake/Qt6WidgetsTools/Qt6WidgetsToolsTargets-relwithdebinfo.cmake"
fi
