#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

prefix="${1:-${ATHENA_WIN64_PREFIX:-${repo_root}/build_windows/prefix}}"
version="${GNUTLS_VERSION:-3.8.10}"
source_dir="${GNUTLS_SOURCE_DIR:-${repo_root}/build_windows/src/gnutls-${version}}"
archive="${repo_root}/build_windows/src/gnutls-${version}.tar.xz"
url="${GNUTLS_URL:-https://www.gnupg.org/ftp/gcrypt/gnutls/v3.8/gnutls-${version}.tar.xz}"
build_dir="${GNUTLS_BUILD_DIR:-${repo_root}/build_windows/deps-build/gnutls}"

mkdir -p "$(dirname -- "${archive}")" "${build_dir}" "${prefix}"
prefix="$(cd -- "${prefix}" && pwd)"

if [[ ! -d "${source_dir}" ]]; then
  if [[ ! -f "${archive}" ]]; then
    curl -fL "${url}" -o "${archive}"
  fi
  tar -xf "${archive}" -C "$(dirname -- "${source_dir}")"
fi

cd "${build_dir}"

export PKG_CONFIG_LIBDIR="${prefix}/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="${prefix}"
export PKG_CONFIG_PATH=

"${source_dir}/configure" \
  --host=x86_64-w64-mingw32 \
  --build="$("${source_dir}/build-aux/config.guess")" \
  --prefix="${prefix}" \
  --libdir="${prefix}/lib" \
  --disable-shared \
  --enable-static \
  --disable-doc \
  --disable-tests \
  --disable-tools \
  --disable-cxx \
  --disable-guile \
  --disable-openssl-compatibility \
  --without-p11-kit \
  --without-idn \
  --without-brotli \
  --without-zstd \
  --without-tpm \
  --without-unbound-root-key-file \
  --with-included-unistring \
  CC=x86_64-w64-mingw32-gcc \
  AR=x86_64-w64-mingw32-ar \
  RANLIB=x86_64-w64-mingw32-ranlib \
  CPPFLAGS="-I${prefix}/include" \
  LDFLAGS="-L${prefix}/lib"

make -j"$(nproc)"
make install

test -f "${prefix}/include/gnutls/gnutls.h"
test -f "${prefix}/lib/libgnutls.a"

# This dependency tree builds GnuTLS as static-only.  The upstream installed
# Windows header still marks public symbols as dllimport, which makes ATHENA
# object files reference __imp_* import variables that libgnutls.a does not
# provide.
perl -0pi -e 's/#if !defined\(GNUTLS_INTERNAL_BUILD\) && defined\(_WIN32\)\n#define _SYM_EXPORT __declspec\(dllimport\)\n#else\n#define _SYM_EXPORT\n#endif/#define _SYM_EXPORT/s' \
  "${prefix}/include/gnutls/gnutls.h"
