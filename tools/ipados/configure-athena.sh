#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

ssh_target="${ATHENA_IPADOS_SSH_TARGET:-felix@127.0.0.1}"
ssh_port="${ATHENA_IPADOS_SSH_PORT:-2222}"
remote_developer_root="${ATHENA_IPADOS_REMOTE_DEVELOPER_ROOT:-/Users/felix/Developer}"
remote_source="${ATHENA_IPADOS_REMOTE_SOURCE:-${remote_developer_root}/ATHENA}"

"${script_dir}/bootstrap-vm.sh" --quiet
"${script_dir}/sync-source-to-vm.sh"

ssh -o BatchMode=yes -o ConnectTimeout=10 -p "${ssh_port}" \
  "${ssh_target}" sh -s -- "${remote_developer_root}" "${remote_source}" <<'REMOTE'
set -eu

developer_root=$1
source_root=$2

export PATH="/Users/felix/.cargo/bin:/opt/local/bin:/opt/local/sbin:$PATH"
export LC_ALL=en_US.UTF-8
export LANG=en_US.UTF-8

runtime_id=athena-guile-3.0.10-ipados-arm64-nojit
build_dir="$developer_root/athena-build/ipados"
runtime_prefix="$developer_root/athena-deps/ipados/prefix/runtime"
deps_prefix="$developer_root/athena-deps/ipados/prefix/deps"
qt_build="$developer_root/athena-deps/ipados/build/qt-6.11.2-iphoneos"
qt_toolchain="$qt_build/qtbase/lib/cmake/Qt6/qt.toolchain.cmake"
chainload_toolchain="$source_root/tools/ipados/iphoneos.toolchain.cmake"
guile_build="$developer_root/athena-deps/ipados/build/athena-guile-3.0.10"
guile_config="$guile_build/config.h"
scheme_dir="$developer_root/athena-deps/ipados/prefix/athena-scheme/$runtime_id"

banner () {
  printf '\n\n======================================================================\n'
  printf '%s\n' "$*"
  printf '======================================================================\n'
}

for tool in cmake xcodebuild pkg-config xcrun; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "Missing required host tool: $tool" >&2
    exit 1
  }
done

for required in \
    "$qt_toolchain" \
    "$chainload_toolchain" \
    "$runtime_prefix/lib/libathena-guile.a" \
    "$runtime_prefix/lib/libgc.a" \
    "$runtime_prefix/lib/libgmp.a" \
    "$runtime_prefix/lib/libffi.a" \
    "$runtime_prefix/lib/libunistring.a" \
    "$guile_config" \
    "$scheme_dir/.complete"; do
  test -e "$required" || {
    echo "Missing required iPadOS build input: $required" >&2
    exit 1
  }
done

grep -Eq '^(#define ENABLE_JIT 0|/\* #undef ENABLE_JIT \*/)$' "$guile_config" || {
  echo "Target Guile config does not prove that JIT is disabled: $guile_config" >&2
  exit 1
}
if grep -Eq '^#define ENABLE_JIT 1$' "$guile_config"; then
  echo "Target Guile config enables JIT: $guile_config" >&2
  exit 1
fi

scheme_runtime_id=$(sed -n '1p' "$scheme_dir/.complete")
[ "$scheme_runtime_id" = "$runtime_id" ] || {
  echo "Packaged Scheme runtime mismatch: $scheme_runtime_id" >&2
  exit 1
}

mkdir -p "$build_dir" "$deps_prefix"

sdk=$(xcrun --sdk iphoneos --show-sdk-path)
opengles="$sdk/System/Library/Frameworks/OpenGLES.framework"
test -f "$opengles/OpenGLES.tbd" || {
  echo "iPhoneOS SDK is missing OpenGLES.tbd: $opengles" >&2
  exit 1
}
test -f "$opengles/Headers/ES3/gl.h" || {
  echo "iPhoneOS SDK is missing OpenGLES ES3 headers: $opengles" >&2
  exit 1
}

# pkg-config itself is a host executable, but every package result must come
# from an arm64 iPhoneOS prefix.  Never allow /opt/local pkg-config metadata to
# leak host macOS libraries into the target graph.
export PKG_CONFIG_PATH=
export PKG_CONFIG_LIBDIR="$runtime_prefix/lib/pkgconfig:$runtime_prefix/share/pkgconfig:$deps_prefix/lib/pkgconfig:$deps_prefix/share/pkgconfig"

banner "Configuring ATHENA for arm64 iPadOS 27 (Xcode generator)"
printf 'source:          %s\n' "$source_root"
printf 'build:           %s\n' "$build_dir"
printf 'Qt toolchain:    %s\n' "$qt_toolchain"
printf 'chainload:       %s\n' "$chainload_toolchain"
printf 'Guile prefix:    %s\n' "$runtime_prefix"
printf 'target deps:     %s\n' "$deps_prefix"
printf 'Scheme bytecode: %s\n' "$scheme_dir"

cmake -S "$source_root" -B "$build_dir" -G Xcode \
  --log-level=VERBOSE \
  -DCMAKE_TOOLCHAIN_FILE="$qt_toolchain" \
  -DQT_CHAINLOAD_TOOLCHAIN_FILE="$chainload_toolchain" \
  -DATHENA_IPADOS_TARGET_PREFIX="$runtime_prefix" \
  -DATHENA_IPADOS_TARGET_PREFIXES="$runtime_prefix;$deps_prefix" \
  -DQT_ADDITIONAL_PACKAGES_PREFIX_PATH="$deps_prefix" \
  -DKF6SyntaxHighlighting_DIR="$deps_prefix/lib/cmake/KF6SyntaxHighlighting" \
  -DATHENA_GUILE_PREBUILT_PREFIX="$runtime_prefix" \
  -DATHENA_GUILE_PREBUILT_CONFIG_HEADER="$guile_config" \
  -DATHENA_PACKAGED_SCHEME_DIR="$scheme_dir" \
  -DOPENGL_GLES3_INCLUDE_DIR="$opengles/Headers" \
  -DOPENGL_gles3_LIBRARY="$opengles/OpenGLES.tbd" \
  -DOPENGL_INCLUDE_DIR="$opengles/Headers" \
  -DOPENGL_gl_LIBRARY="$opengles/OpenGLES.tbd" \
  -DICU_ROOT="$deps_prefix" \
  -DICU_INCLUDE_DIR="$deps_prefix/include" \
  -DICU_UC_LIBRARY_RELEASE="$deps_prefix/lib/libicuuc.a" \
  -DICU_I18N_LIBRARY_RELEASE="$deps_prefix/lib/libicui18n.a" \
  -DATHENA_CPU_TARGET=arm64 \
  -DATHENA_GUI=Qt6 \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=27.0 \
  -DCMAKE_CONFIGURATION_TYPES=Debug \
  -DCMAKE_XCODE_GENERATE_SCHEME=ON \
  -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO

banner "ATHENA iPadOS configure completed"
cmake -LA -N "$build_dir" | grep -E \
  '^(ATHENA_|CMAKE_OSX_|CMAKE_SYSTEM_NAME|Qt6_DIR|CMAKE_TOOLCHAIN_FILE)' \
  | sort
REMOTE

