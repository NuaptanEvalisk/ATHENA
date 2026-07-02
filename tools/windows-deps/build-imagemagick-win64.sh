#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

prefix="${1:-${ATHENA_WIN64_PREFIX:-${repo_root}/build_windows/prefix}}"
version="${IMAGEMAGICK_VERSION:-7.1.2-8}"
source_dir="${IMAGEMAGICK_SOURCE_DIR:-${repo_root}/build_windows/src/ImageMagick-${version}}"
archive="${repo_root}/build_windows/src/ImageMagick-${version}.tar.xz"
url="${IMAGEMAGICK_URL:-https://imagemagick.org/archive/releases/ImageMagick-${version}.tar.xz}"
build_dir="${IMAGEMAGICK_BUILD_DIR:-${repo_root}/build_windows/deps-build/imagemagick}"

mkdir -p "$(dirname -- "${archive}")" "${build_dir}" "${prefix}"
prefix="$(cd -- "${prefix}" && pwd)"

if [[ ! -d "${source_dir}" ]]; then
  if [[ ! -f "${archive}" ]]; then
    curl -fL "${url}" -o "${archive}"
  fi
  tar -xf "${archive}" -C "$(dirname -- "${source_dir}")"
fi

cd "${build_dir}"

export PKG_CONFIG_LIBDIR="${prefix}/lib/pkgconfig:${prefix}/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR=
export PKG_CONFIG_PATH=

"${source_dir}/configure" \
  --host=x86_64-w64-mingw32 \
  --build="$("${source_dir}/config/config.guess")" \
  --prefix="${prefix}" \
  --libdir="${prefix}/lib" \
  --enable-shared \
  --enable-static \
  --disable-docs \
  --disable-openmp \
  --disable-deprecated \
  --without-perl \
  --without-magick-plus-plus \
  --without-x \
  --without-bzlib \
  --without-djvu \
  --without-fftw \
  --without-fontconfig \
  --without-freetype \
  --without-heic \
  --without-jbig \
  --without-jxl \
  --without-lcms \
  --without-lqr \
  --without-openexr \
  --without-raqm \
  --without-tiff \
  --without-webp \
  --without-wmf \
  --without-xml \
  --with-jpeg=yes \
  --with-png=yes \
  --with-zlib=yes \
  --with-zstd=yes \
  CC=x86_64-w64-mingw32-gcc \
  CXX=x86_64-w64-mingw32-g++ \
  AR=x86_64-w64-mingw32-ar \
  RANLIB=x86_64-w64-mingw32-ranlib \
  CPPFLAGS="-I${prefix}/include" \
  LDFLAGS="-L${prefix}/lib"

make -j"$(nproc)"
make install

test -f "${prefix}/include/ImageMagick-7/MagickWand/MagickWand.h"
test -f "${prefix}/lib/pkgconfig/MagickWand.pc"
test -e "${prefix}/lib/libMagickWand-7.Q16HDRI.dll.a" -o \
     -e "${prefix}/lib/libMagickWand-7.Q16.dll.a" -o \
     -e "${prefix}/lib/libMagickWand-7.Q16HDRI.a" -o \
     -e "${prefix}/lib/libMagickWand-7.Q16.a"
