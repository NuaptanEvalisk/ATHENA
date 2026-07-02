#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

prefix="${1:-${ATHENA_WIN64_PREFIX:-${repo_root}/build_windows/prefix}}"
version="${SQLITE_VERSION:-3530100}"
year="${SQLITE_YEAR:-2026}"
source_dir="${SQLITE_SOURCE_DIR:-${repo_root}/build_windows/src/sqlite-autoconf-${version}}"
archive="${repo_root}/build_windows/src/sqlite-autoconf-${version}.tar.gz"
url="${SQLITE_URL:-https://www.sqlite.org/${year}/sqlite-autoconf-${version}.tar.gz}"
build_dir="${SQLITE_BUILD_DIR:-${repo_root}/build_windows/deps-build/sqlite}"

mkdir -p "$(dirname -- "${archive}")" "${build_dir}" "${prefix}/bin" \
  "${prefix}/include" "${prefix}/lib" "${prefix}/lib/pkgconfig"
prefix="$(cd -- "${prefix}" && pwd)"

if [[ ! -d "${source_dir}" ]]; then
  if [[ ! -f "${archive}" ]]; then
    curl -fL "${url}" -o "${archive}"
  fi
  tar -xf "${archive}" -C "$(dirname -- "${source_dir}")"
fi

cd "${build_dir}"

x86_64-w64-mingw32-gcc -O2 -DNDEBUG \
  -DSQLITE_THREADSAFE=1 \
  -DSQLITE_ENABLE_FTS5 \
  -DSQLITE_ENABLE_RTREE \
  -DSQLITE_ENABLE_JSON1 \
  -DSQLITE_ENABLE_COLUMN_METADATA \
  -DSQLITE_ENABLE_MATH_FUNCTIONS \
  -shared "${source_dir}/sqlite3.c" \
  -Wl,--out-implib,libsqlite3.dll.a \
  -o sqlite3.dll

cp -f sqlite3.dll "${prefix}/bin/"
cp -f libsqlite3.dll.a "${prefix}/lib/"
cp -f "${source_dir}/sqlite3.h" "${source_dir}/sqlite3ext.h" "${prefix}/include/"

cat > "${prefix}/lib/pkgconfig/sqlite3.pc" <<EOF
prefix=${prefix}
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: SQLite
Description: SQL database engine
Version: 3.53.1
Libs: -L\${libdir} -lsqlite3
Cflags: -I\${includedir}
EOF

test -f "${prefix}/include/sqlite3.h"
test -f "${prefix}/lib/libsqlite3.dll.a"
