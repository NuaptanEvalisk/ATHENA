#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

ssh_target="${ATHENA_IPADOS_SSH_TARGET:-felix@127.0.0.1}"
ssh_port="${ATHENA_IPADOS_SSH_PORT:-2222}"
remote_developer_root="${ATHENA_IPADOS_REMOTE_DEVELOPER_ROOT:-/Users/felix/Developer}"
remote_source="${ATHENA_IPADOS_REMOTE_SOURCE:-${remote_developer_root}/ATHENA}"
jobs="${ATHENA_IPADOS_JOBS:-15}"

"${script_dir}/bootstrap-vm.sh" --quiet
"${script_dir}/sync-source-to-vm.sh"

ssh -o BatchMode=yes -o ConnectTimeout=10 -p "${ssh_port}" \
  "${ssh_target}" sh -s -- \
  "${remote_developer_root}" "${remote_source}" "${jobs}" <<'REMOTE'
set -eu

developer_root=$1
source_root=$2
jobs=$3

export PATH="/Users/felix/.cargo/bin:/opt/local/bin:/opt/local/sbin:$PATH"
export LC_ALL=en_US.UTF-8
export LANG=en_US.UTF-8

downloads="$developer_root/athena-deps/host/src"
build_root="$developer_root/athena-deps/ipados/build"
prefix="$developer_root/athena-deps/ipados/prefix/runtime"
artifacts="$developer_root/athena-artifacts"

sdk=$(xcrun --sdk iphoneos --show-sdk-path)
cc=$(xcrun --sdk iphoneos --find clang)
cxx=$(xcrun --sdk iphoneos --find clang++)
ar=$(xcrun --sdk iphoneos --find ar)
ranlib=$(xcrun --sdk iphoneos --find ranlib)
nm=$(xcrun --sdk iphoneos --find nm)
strip=$(xcrun --sdk iphoneos --find strip)

target_flags="-arch arm64 -isysroot $sdk -miphoneos-version-min=27.0"
common_cflags="$target_flags -O2 -g -fPIC"
common_cppflags="-isysroot $sdk -I$prefix/include"
common_ldflags="$target_flags -L$prefix/lib"

mkdir -p "$downloads" "$build_root" "$prefix" "$artifacts"

banner () {
  printf '\n\n======================================================================\n'
  printf '%s\n' "$*"
  printf '======================================================================\n'
}

download_verify () {
  name=$1
  url=$2
  expected=$3
  archive="$downloads/$name"

  if [ ! -f "$archive" ]; then
    banner "Downloading $name"
    curl -L --fail --show-error --progress-bar \
      --retry 8 --retry-all-errors --retry-delay 2 \
      -C - \
      -o "$archive.part" "$url"
    mv "$archive.part" "$archive"
  else
    banner "$name already downloaded; verifying"
  fi

  actual=$(shasum -a 256 "$archive" | awk '{print $1}')
  printf 'expected SHA-256: %s\n' "$expected"
  printf 'actual   SHA-256: %s\n' "$actual"
  [ "$actual" = "$expected" ] || {
    echo "Checksum mismatch for $archive" >&2
    exit 1
  }
}

extract_once () {
  archive_name=$1
  source_name=$2
  archive="$downloads/$archive_name"
  source_dir="$downloads/$source_name"

  if [ ! -d "$source_dir" ]; then
    banner "Extracting $archive_name"
    case "$archive_name" in
      *.tar.xz) tar -xJf "$archive" -C "$downloads" ;;
      *.tar.gz) tar -xzf "$archive" -C "$downloads" ;;
      *) echo "Unsupported archive: $archive_name" >&2; exit 1 ;;
    esac
  fi
  test -d "$source_dir"
}

prepare_build () {
  build_dir=$1
  fingerprint=$2
  stamp="$build_dir/.athena-ipados-config"

  if [ -f "$stamp" ] && [ "$(cat "$stamp")" != "$fingerprint" ]; then
    banner "Build configuration changed; resetting $build_dir"
    rm -rf "$build_dir"
  fi
  mkdir -p "$build_dir"
  printf '%s\n' "$fingerprint" >"$stamp.next"
}

finish_config () {
  build_dir=$1
  mv "$build_dir/.athena-ipados-config.next" \
     "$build_dir/.athena-ipados-config"
}

validate_archive () {
  label=$1
  archive=$2

  test -f "$archive" || {
    echo "$label static library was not produced: $archive" >&2
    exit 1
  }
  printf '\n--- %s target artifact ---\n' "$label"
  ls -lh "$archive"
  file "$archive"

  tmp=$(mktemp -d)
  object=$(ar -t "$archive" | grep -E '\.(o|lo|obj)$' | head -1 || true)
  if [ -n "$object" ]; then
    (
      cd "$tmp"
      ar -x "$archive" "$object"
      printf 'Representative object: %s\n' "$object"
      file "$object"
      build_version=$(xcrun vtool -show-build "$object" 2>/dev/null || true)
      printf '%s\n' "$build_version"
      printf '%s\n' "$build_version" | grep -Eq 'platform[[:space:]]+IOS'
      printf '%s\n' "$build_version" | grep -Eq 'minos[[:space:]]+27\.0'
    )
  else
    rm -rf "$tmp"
    echo "Could not find a Mach-O object in $archive" >&2
    exit 1
  fi
  rm -rf "$tmp"
}

export CC="$cc"
export CXX="$cxx"
export CPP="$cc $target_flags -E"
export CXXCPP="$cxx $target_flags -E"
export AR="$ar"
export RANLIB="$ranlib"
export NM="$nm"
export STRIP="$strip"
export CFLAGS="$common_cflags"
export CXXFLAGS="$common_cflags"
export CPPFLAGS="$common_cppflags"
export LDFLAGS="$common_ldflags"
export PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig"
export PKG_CONFIG_PATH=

##############################################################################
# GMP 6.3.0
##############################################################################

gmp_archive=gmp-6.3.0.tar.xz
gmp_source=gmp-6.3.0
gmp_url=https://gmplib.org/download/gmp/gmp-6.3.0.tar.xz
gmp_sha=a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898
download_verify "$gmp_archive" "$gmp_url" "$gmp_sha"
extract_once "$gmp_archive" "$gmp_source"
gmp_build="$build_root/gmp-6.3.0"
gmp_fp="host=aarch64-apple-darwin|prefix=$prefix|sdk=$sdk|min=27.0|static|cflags=$common_cflags"
prepare_build "$gmp_build" "$gmp_fp"
if [ ! -f "$gmp_build/Makefile" ]; then
  banner "Configuring GMP 6.3.0 for arm64 iPadOS 27"
  (
    cd "$gmp_build"
    "$downloads/$gmp_source/configure" \
      --host=aarch64-apple-darwin \
      --prefix="$prefix" \
      --disable-shared \
      --enable-static \
      --with-pic
  )
fi
finish_config "$gmp_build"
banner "Building GMP 6.3.0 with ${jobs} jobs (verbose)"
gmake -C "$gmp_build" -j"$jobs" V=1
banner "Installing GMP 6.3.0 (verbose)"
gmake -C "$gmp_build" install V=1
validate_archive GMP "$prefix/lib/libgmp.a"

##############################################################################
# libffi 3.5.2
##############################################################################

ffi_archive=libffi-3.5.2.tar.gz
ffi_source=libffi-3.5.2
ffi_url=https://github.com/libffi/libffi/releases/download/v3.5.2/libffi-3.5.2.tar.gz
ffi_sha=f3a3082a23b37c293a4fcd1053147b371f2ff91fa7ea1b2a52e335676bac82dc
download_verify "$ffi_archive" "$ffi_url" "$ffi_sha"
extract_once "$ffi_archive" "$ffi_source"
ffi_build="$build_root/libffi-3.5.2"
ffi_fp="host=aarch64-apple-darwin|prefix=$prefix|sdk=$sdk|min=27.0|static|cflags=$common_cflags"
prepare_build "$ffi_build" "$ffi_fp"
if [ ! -f "$ffi_build/Makefile" ]; then
  banner "Configuring libffi 3.5.2 for arm64 iPadOS 27"
  (
    cd "$ffi_build"
    "$downloads/$ffi_source/configure" \
      --host=aarch64-apple-darwin \
      --prefix="$prefix" \
      --disable-shared \
      --enable-static \
      --disable-docs \
      --with-pic
  )
fi
finish_config "$ffi_build"
banner "Building libffi 3.5.2 with ${jobs} jobs (verbose)"
gmake -C "$ffi_build" -j"$jobs" V=1
banner "Installing libffi 3.5.2 (verbose)"
gmake -C "$ffi_build" install V=1
validate_archive libffi "$prefix/lib/libffi.a"

##############################################################################
# libunistring 1.4.2
##############################################################################

unistring_archive=libunistring-1.4.2.tar.xz
unistring_source=libunistring-1.4.2
unistring_url=https://mirrors.kernel.org/gnu/libunistring/libunistring-1.4.2.tar.xz
unistring_sha=5b46e74377ed7409c5b75e7a96f95377b095623b689d8522620927964a41499c
download_verify "$unistring_archive" "$unistring_url" "$unistring_sha"
extract_once "$unistring_archive" "$unistring_source"
unistring_build="$build_root/libunistring-1.4.2"
unistring_fp="host=aarch64-apple-darwin|prefix=$prefix|sdk=$sdk|min=27.0|static|iconv=SDK|cflags=$common_cflags"
prepare_build "$unistring_build" "$unistring_fp"
if [ ! -f "$unistring_build/Makefile" ]; then
  banner "Configuring libunistring 1.4.2 for arm64 iPadOS 27"
  (
    cd "$unistring_build"
    LIBS=-liconv "$downloads/$unistring_source/configure" \
      --host=aarch64-apple-darwin \
      --prefix="$prefix" \
      --disable-shared \
      --enable-static
  )
fi
finish_config "$unistring_build"
banner "Building libunistring 1.4.2 with ${jobs} jobs (verbose)"
gmake -C "$unistring_build" -j"$jobs" V=1
banner "Installing libunistring 1.4.2 (verbose)"
gmake -C "$unistring_build" install V=1
validate_archive libunistring "$prefix/lib/libunistring.a"

banner "Guile target prerequisites ready"
printf 'target prefix: %s\n' "$prefix"
for pc in gmp libffi libunistring bdw-gc; do
  printf '%-14s ' "$pc"
  PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig" \
    pkg-config --modversion "$pc" 2>/dev/null || echo '(no pkg-config file)'
done
printf '%s\n' "$prefix" >"$artifacts/guile-deps-ipados-prefix.path"
REMOTE
