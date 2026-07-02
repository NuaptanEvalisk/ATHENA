#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

prefix="${1:-${ATHENA_WIN64_PREFIX:-${repo_root}/build_windows/prefix}}"
original_source_dir="${LIBLTDL_SOURCE_DIR:-/usr/share/libtool}"
source_dir="${LIBLTDL_STAGED_SOURCE_DIR:-${repo_root}/build_windows/src/libtool-libltdl-win64}"

if [[ ! -x "${original_source_dir}/configure" ]]; then
  echo "missing libtool configure script: ${original_source_dir}/configure" >&2
  exit 2
fi

rm -rf "${source_dir}"
mkdir -p "$(dirname -- "${source_dir}")" "${prefix}"
cp -a "${original_source_dir}/." "${source_dir}/"
rm -rf "$(dirname -- "${source_dir}")/build-aux"
cp -a "${original_source_dir}/build-aux" "$(dirname -- "${source_dir}")/build-aux"
rm -rf "$(dirname -- "${source_dir}")/m4"
mkdir -p "$(dirname -- "${source_dir}")/m4"
for macro in libtool.m4 ltargz.m4 ltdl.m4 ltoptions.m4 ltsugar.m4 ltversion.m4 'lt~obsolete.m4'; do
  cp -a "/usr/share/aclocal/${macro}" "$(dirname -- "${source_dir}")/m4/${macro}"
done
prefix="$(cd -- "${prefix}" && pwd)"
source_dir="$(cd -- "${source_dir}" && pwd)"

cd "${source_dir}"

"${source_dir}/configure" \
  --host=x86_64-w64-mingw32 \
  --build="$("${source_dir}/build-aux/config.guess")" \
  --prefix="${prefix}" \
  --libdir="${prefix}/lib" \
  --enable-ltdl-install \
  --enable-shared \
  --enable-static \
  CC=x86_64-w64-mingw32-gcc \
  AR=x86_64-w64-mingw32-ar \
  RANLIB=x86_64-w64-mingw32-ranlib \
  CPPFLAGS="-I${prefix}/include" \
  LDFLAGS="-L${prefix}/lib"

touch aclocal.m4 configure config.h.in Makefile.in
make -j"$(nproc)"
make install

test -f "${prefix}/include/ltdl.h"
test -e "${prefix}/lib/libltdl.dll.a" -o -e "${prefix}/lib/libltdl.a"
