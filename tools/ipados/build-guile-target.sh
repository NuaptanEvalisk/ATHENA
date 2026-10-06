#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

ssh_target="${ATHENA_IPADOS_SSH_TARGET:-felix@127.0.0.1}"
ssh_port="${ATHENA_IPADOS_SSH_PORT:-2222}"
remote_developer_root="${ATHENA_IPADOS_REMOTE_DEVELOPER_ROOT:-/Users/felix/Developer}"
remote_source="${ATHENA_IPADOS_REMOTE_SOURCE:-${remote_developer_root}/ATHENA}"
jobs="${ATHENA_IPADOS_JOBS:-20}"

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
export M4=gm4

host_prefix="$developer_root/athena-deps/host/runtime"
host_guile="$host_prefix/bin/guile"
target_root="$developer_root/athena-deps/ipados"
target_prefix="$target_root/prefix/runtime"
target_source="$target_root/generated/athena-guile-3.0.10"
target_build="$target_root/build/athena-guile-3.0.10"
artifacts="$developer_root/athena-artifacts"
lock_root="$target_root/.locks"
lock_dir="$lock_root/athena-guile-target"
tmp=

sdk=$(xcrun --sdk iphoneos --show-sdk-path)
cc=$(xcrun --sdk iphoneos --find clang)
cxx=$(xcrun --sdk iphoneos --find clang++)
ar=$(xcrun --sdk iphoneos --find ar)
ranlib=$(xcrun --sdk iphoneos --find ranlib)
nm=$(xcrun --sdk iphoneos --find nm)
strip=$(xcrun --sdk iphoneos --find strip)

target_triplet=aarch64-apple-darwin
target_flags="-arch arm64 -isysroot $sdk -miphoneos-version-min=27.0"

banner () {
  printf '\n\n======================================================================\n'
  printf '%s\n' "$*"
  printf '======================================================================\n'
}

cleanup () {
  if [ -n "${tmp:-}" ] && [ -d "$tmp" ]; then
    rm -rf "$tmp"
  fi
  rm -rf "$lock_dir"
}

mkdir -p "$lock_root"
if ! mkdir "$lock_dir" 2>/dev/null; then
  owner=$(cat "$lock_dir/pid" 2>/dev/null || true)
  if [ -n "$owner" ] && kill -0 "$owner" 2>/dev/null; then
    echo "Another ATHENA target Guile build is already running (pid $owner)" >&2
    exit 1
  fi
  echo "Removing stale target Guile build lock: $lock_dir" >&2
  rm -rf "$lock_dir"
  mkdir "$lock_dir"
fi
printf '%s\n' "$$" >"$lock_dir/pid"
trap cleanup EXIT
trap 'exit 130' HUP INT TERM

for tool in autoconf automake autoreconf glibtoolize gm4 gperf pkg-config \
            gmake rsync python3 xcrun file ar nm; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "Missing required host tool: $tool" >&2
    exit 1
  }
done

test -x "$host_guile" || {
  echo "Same-version native GUILE_FOR_BUILD is not ready: $host_guile" >&2
  echo "Run tools/ipados/build-guile-host.sh first." >&2
  exit 1
}

host_version=$(
  DYLD_LIBRARY_PATH="$host_prefix/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" \
    "$host_guile" -c \
    '(format #t "~a.~a.~a" (major-version) (minor-version) (micro-version))'
)
[ "$host_version" = 3.0.10 ] || {
  echo "GUILE_FOR_BUILD must be private Guile 3.0.10, got $host_version" >&2
  exit 1
}

for required in \
    "$target_prefix/lib/libgc.a" \
    "$target_prefix/lib/libgmp.a" \
    "$target_prefix/lib/libffi.a" \
    "$target_prefix/lib/libunistring.a"; do
  test -f "$required" || {
    echo "Missing target Guile prerequisite: $required" >&2
    exit 1
  }
done

mkdir -p "$target_root/generated" "$target_root/build" "$target_prefix" "$artifacts"

##############################################################################
# Writable generated-source copy.  autoreconf must never mutate the VM source
# mirror, which is a read-only reflection of the Linux source of truth.
##############################################################################

source_digest=$(python3 - "$source_root/3rdparty/athena-guile" <<'PY'
import hashlib
import os
import sys

root = sys.argv[1]
h = hashlib.sha256()
for current, dirs, files in os.walk(root):
    dirs.sort()
    files.sort()
    for name in files:
        path = os.path.join(current, name)
        if os.path.islink(path):
            payload = os.readlink(path).encode()
        else:
            with open(path, 'rb') as f:
                payload = f.read()
        rel = os.path.relpath(path, root)
        h.update(rel.encode())
        h.update(b'\0')
        h.update(payload)
        h.update(b'\0')
print(h.hexdigest())
PY
)
source_stamp="$target_source/.athena-linux-source-sha256"
source_refreshed=0
if [ ! -f "$source_stamp" ] || [ "$(cat "$source_stamp")" != "$source_digest" ]; then
  banner "Refreshing writable target Guile source staging tree"
  rm -rf "$target_source"
  mkdir -p "$target_source"
  rsync -a --delete "$source_root/3rdparty/athena-guile/" "$target_source/"
  chmod -R u+w "$target_source"
  printf '%s\n' "$source_digest" >"$source_stamp"
  source_refreshed=1
fi

if [ "$source_refreshed" = 1 ] || [ ! -x "$target_source/configure" ]; then
  banner "Regenerating target Guile autotools files in staging tree"
  (
    cd "$target_source"
    sh -x ./autogen.sh
  )
else
  banner "Target Guile autotools staging tree is current"
fi

test -x "$target_source/configure" || {
  echo "Target Guile autoreconf did not produce configure" >&2
  exit 1
}
test -x "$target_source/build-aux/config.guess" || {
  echo "Target Guile autoreconf did not produce build-aux/config.guess" >&2
  exit 1
}
build_triplet=$("$target_source/build-aux/config.guess")

##############################################################################
# Cross configure.  GUILE_FOR_BUILD is native x86_64 macOS Guile 3.0.10 from
# the same ATHENA source line; all C objects/libraries below target arm64 iOS.
##############################################################################

export CC="$cc"
export CXX="$cxx"
export CPP="$cc $target_flags -E"
export CXXCPP="$cxx $target_flags -E"
export CC_FOR_BUILD=/usr/bin/clang
export AR="$ar"
export RANLIB="$ranlib"
export NM="$nm"
export STRIP="$strip"
export CFLAGS="$target_flags -O2 -g -std=gnu17 -fPIC -fno-omit-frame-pointer"
export CXXFLAGS="$target_flags -O2 -g -fPIC -fno-omit-frame-pointer"
export CPPFLAGS="-isysroot $sdk -I$target_prefix/include"
export LDFLAGS="$target_flags -L$target_prefix/lib"
export LIBS='-liconv -framework CoreFoundation'
export PKG_CONFIG_LIBDIR="$target_prefix/lib/pkgconfig:$target_prefix/share/pkgconfig"
export PKG_CONFIG_PATH=
export GUILE_FOR_BUILD="$host_guile"
export DYLD_LIBRARY_PATH="$host_prefix/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"

config_signature="build=$build_triplet|host=$target_triplet|sdk=$sdk|min=27.0|source=$source_digest|static|nojit|nolto|libs=iconv+CoreFoundation"
config_stamp="$target_build/.athena-target-config"
configured=0
if [ -f "$target_build/Makefile" ] && \
   [ -x "$target_build/config.status" ] && \
   [ -f "$target_build/config.h" ] && \
   [ -f "$config_stamp" ] && \
   [ "$(cat "$config_stamp")" = "$config_signature" ] && \
   grep -Eq '^(#define ENABLE_JIT 0|/\* #undef ENABLE_JIT \*/)$' "$target_build/config.h" && \
   ! grep -Eq '^#define ENABLE_JIT 1$' "$target_build/config.h" && \
   grep -Eq '^#define HOST_TYPE "aarch64-apple-darwin"$' "$target_build/config.h" && \
   grep -Eq '^#define SIZEOF_VOID_P 8$' "$target_build/config.h"; then
  configured=1
fi

if [ "$configured" = 0 ]; then
  banner "Resetting target Guile build tree to a clean configure state"
  rm -rf "$target_build"
  mkdir -p "$target_build"
  banner "Configuring private Guile 3.0.10 for arm64 iPadOS 27 (no JIT)"
  (
    cd "$target_build"
    "$target_source/configure" \
      --build="$build_triplet" \
      --host="$target_triplet" \
      --target="$target_triplet" \
      --prefix="$target_prefix" \
      --libdir="$target_prefix/lib" \
      --disable-shared \
      --enable-static \
      --disable-jit \
      --disable-nls \
      --disable-lto \
      --with-threads=pthreads \
      --with-bdw-gc=bdw-gc \
      --with-libgmp-prefix="$target_prefix" \
      --with-libunistring-prefix="$target_prefix"
  )
  printf '%s\n' "$config_signature" >"$config_stamp"
else
  banner "Private Guile iPadOS build is already configured"
fi

grep -Eq '^(#define ENABLE_JIT 0|/\* #undef ENABLE_JIT \*/)$' "$target_build/config.h" || {
  echo "Target Guile config.h does not prove that JIT is disabled" >&2
  exit 1
}
if grep -Eq '^#define ENABLE_JIT 1$' "$target_build/config.h"; then
  echo "Target Guile config.h enables JIT" >&2
  exit 1
fi

banner "Building private Guile 3.0.10 for arm64 iPadOS 27 with ${jobs} jobs (verbose)"
gmake -C "$target_build" -j"$jobs" V=1

banner "Installing private Guile 3.0.10 iPadOS runtime and target bytecode (verbose)"
gmake -C "$target_build" install V=1

guile_archive="$target_prefix/lib/libathena-guile.a"
test -f "$guile_archive" || {
  echo "Static private target Guile archive was not installed: $guile_archive" >&2
  exit 1
}
test -f "$target_prefix/lib/guile/3.0/ccache/ice-9/boot-9.go" || {
  echo "Target Guile standard-library bytecode was not installed" >&2
  exit 1
}

banner "Validating private Guile iPadOS runtime"
ls -lh "$guile_archive"
file "$guile_archive"
nm -g "$guile_archive" | grep 'scm_athena_set_auto_compile_callback' | head -1

tmp=$(mktemp -d)
object=$(ar -t "$guile_archive" | grep -E '\.(o|obj)$' | head -1 || true)
test -n "$object" || {
  echo "No Mach-O object found in $guile_archive" >&2
  exit 1
}
(
  cd "$tmp"
  ar -x "$guile_archive" "$object"
  printf 'Representative Guile object: %s\n' "$object"
  file "$object"
  build_version=$(xcrun vtool -show-build "$object")
  printf '%s\n' "$build_version"
  printf '%s\n' "$build_version" | grep -Eq 'platform[[:space:]]+IOS'
  printf '%s\n' "$build_version" | grep -Eq 'minos[[:space:]]+27\.0'
)

printf '%s\n' '--- target Guile bytecode identity ---'
file "$target_prefix/lib/guile/3.0/ccache/ice-9/boot-9.go"
file "$target_prefix/lib/guile/3.0/ccache/language/tree-il.go" 2>/dev/null || true

cp "$target_build/config.h" "$artifacts/athena-guile-ipados-config.h"
printf '%s\n' "$target_prefix" >"$artifacts/guile-ipados-prefix.path"
printf '%s\n' "$target_build/config.h" >"$artifacts/guile-ipados-config.path"
echo "Private Guile iPadOS runtime ready: $target_prefix"
REMOTE
