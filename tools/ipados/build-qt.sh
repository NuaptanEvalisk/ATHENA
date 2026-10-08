#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

ssh_target="${ATHENA_IPADOS_SSH_TARGET:-felix@127.0.0.1}"
ssh_port="${ATHENA_IPADOS_SSH_PORT:-2222}"
remote_developer_root="${ATHENA_IPADOS_REMOTE_DEVELOPER_ROOT:-/Users/felix/Developer}"
remote_source="${ATHENA_IPADOS_REMOTE_SOURCE:-${remote_developer_root}/ATHENA}"
jobs="${ATHENA_IPADOS_JOBS:-15}"
phase="${1:-all}"

case "${phase}" in
  download|host|target|all) ;;
  *)
    printf 'usage: %s [download|host|target|all]\n' "$0" >&2
    exit 2
    ;;
esac

"${script_dir}/sync-source-to-vm.sh"

ssh -o BatchMode=yes -o ConnectTimeout=10 -p "${ssh_port}" \
  "${ssh_target}" sh -s -- "${remote_developer_root}" "${jobs}" "${phase}" "${remote_source}" <<'REMOTE'
set -eu

developer_root=$1
jobs=$2
phase=$3
source_root=$4

export PATH="/Users/felix/.cargo/bin:/opt/local/bin:/opt/local/sbin:$PATH"
export LC_ALL=C
export LANG=C

qt_version=6.11.2
qt_sha256=6dcfbca271d76a6502741a2c0dc6fc98ef7dd0b7b4cfd0abcebb285a86a26f33
qt_archive="qt-everywhere-src-${qt_version}.tar.xz"
qt_url="https://download.qt.io/official_releases/qt/6.11/${qt_version}/single/${qt_archive}"

host_root="$developer_root/athena-deps/host"
target_root="$developer_root/athena-deps/ipados"
source_dir="$host_root/src/qt-everywhere-src-${qt_version}"
archive="$host_root/src/$qt_archive"
host_build="$host_root/build/qt-${qt_version}-host"
target_build="$target_root/build/qt-${qt_version}-iphoneos"
target_prefix="$target_root/prefix/qt-${qt_version}-iphoneos"

banner () {
  printf '\n\n======================================================================\n'
  printf '%s\n' "$*"
  printf '======================================================================\n'
}

require_tool () {
  command -v "$1" >/dev/null 2>&1 || {
    echo "Missing required host tool: $1" >&2
    exit 1
  }
}

for tool in cmake ninja curl shasum tar xcrun clang python3 git; do
  require_tool "$tool"
done

download_qt () {
  mkdir -p "$host_root/src"
  if [ ! -f "$archive" ]; then
    banner "Downloading Qt ${qt_version} source (~973 MB)"
    curl -L --fail --show-error --progress-bar \
      --retry 3 --retry-delay 2 \
      -o "$archive.part" "$qt_url"
    mv "$archive.part" "$archive"
  else
    banner "Qt ${qt_version} source archive already exists; verifying"
  fi

  actual=$(shasum -a 256 "$archive" | awk '{print $1}')
  printf 'expected SHA-256: %s\n' "$qt_sha256"
  printf 'actual   SHA-256: %s\n' "$actual"
  [ "$actual" = "$qt_sha256" ] || {
    echo "Qt source checksum mismatch" >&2
    exit 1
  }

  if [ ! -d "$source_dir" ]; then
    banner "Extracting Qt ${qt_version} source"
    tar -xJf "$archive" -C "$host_root/src"
  fi

  [ -x "$source_dir/configure" ] || {
    echo "Qt configure script not found after extraction: $source_dir/configure" >&2
    exit 1
  }
  for fix in "$source_root"/tools/ipados/patches/qt-*.patch; do
    if git -C "$source_dir" apply --reverse --check "$fix" 2>/dev/null; then
      continue
    fi
    git -C "$source_dir" apply --check "$fix"
    git -C "$source_dir" apply "$fix"
  done
}

configure_host () {
  download_qt
  mkdir -p "$host_build"
  if [ ! -f "$host_build/CMakeCache.txt" ]; then
    banner "Configuring Qt ${qt_version} host tools"
    (
      cd "$host_build"
      "$source_dir/configure" \
        -developer-build \
        -nomake tests \
        -nomake examples \
        -submodules qtbase,qtsvg
    )
  else
    banner "Qt host build is already configured"
  fi
}

build_host () {
  configure_host
  banner "Building Qt ${qt_version} host tools with ${jobs} jobs (verbose)"
  cmake --build "$host_build" \
    --target host_tools \
    --parallel "$jobs" \
    --verbose

  test -x "$host_build/qtbase/libexec/moc" || {
    echo "Qt host moc was not produced" >&2
    exit 1
  }
  test -x "$host_build/qtbase/libexec/rcc" || {
    echo "Qt host rcc was not produced" >&2
    exit 1
  }
}

configure_target () {
  build_host
  target_configured="$target_build/.athena-configured"
  if [ -f "$target_build/CMakeCache.txt" ] && [ ! -f "$target_configured" ]; then
    banner "Discarding incomplete Qt iphoneos configure state"
    rm -rf "$target_build"
  fi
  mkdir -p "$target_build" "$target_prefix"
  if [ ! -f "$target_configured" ]; then
    banner "Configuring Qt ${qt_version} for arm64 iphoneos / minimum iPadOS 27.0"
    (
      cd "$target_build"
      "$source_dir/configure" \
        -platform macx-ios-clang \
        -release \
        -sdk iphoneos \
        -qt-host-path "$host_build" \
        -prefix "$target_prefix" \
        -nomake tests \
        -nomake examples \
        -submodules qtbase,qtsvg,qtwebsockets \
        -- \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=27.0 \
        -DQT_HOST_PATH_CMAKE_DIR="$host_build/qtbase/lib/cmake" \
        -DQT_INSTALL_CONFIG_INFO_FILES=ON
    )
    touch "$target_configured"
  else
    banner "Qt iphoneos build is already configured"
    cmake -S "$source_dir" -B "$target_build" -DBUILD_qtwebsockets=ON
  fi
}

build_target () {
  configure_target
  banner "Building ATHENA-required Qt ${qt_version} iphoneos targets with ${jobs} jobs (verbose)"
  cmake --build "$target_build" \
    --target Core Gui Widgets Network WebSockets PrintSupport Svg QIOSIntegrationPlugin \
    --parallel "$jobs" \
    --verbose

  # Do not build/install Qt's complete optional plugin set.  Qt 6.11.2 still
  # ships QIosOptionalPlugin_NSPhotoLibraryPlugin, whose legacy AssetsLibrary
  # APIs are unavailable in the iOS 26 SDK.  It has DEFAULT_IF FALSE and ATHENA
  # neither links nor needs it.  The Qt build tree is a supported CMake package
  # tree and contains the target toolchain plus the exact frameworks/plugins we
  # consume.
  banner "Inspecting Qt iphoneos build-tree SDK"
  qt_toolchain="$target_build/qtbase/lib/cmake/Qt6/qt.toolchain.cmake"
  core_binary="$target_build/qtbase/lib/QtCore.framework/QtCore"
  ios_plugin="$target_build/qtbase/plugins/platforms/libqios.a"
  for required in "$qt_toolchain" "$core_binary" "$ios_plugin"; do
    test -f "$required" || {
      echo "Required Qt target artifact was not produced: $required" >&2
      exit 1
    }
  done
  printf 'Qt toolchain: %s\n' "$qt_toolchain"
  printf 'Qt build SDK:  %s\n' "$target_build/qtbase"
  file "$core_binary"
  file "$ios_plugin"

  tmp=$(mktemp -d)
  first_object=$(ar -t "$ios_plugin" | grep -E '\.(o|obj)$' | head -1 || true)
  if [ -n "$first_object" ]; then
    (
      cd "$tmp"
      ar -x "$ios_plugin" "$first_object"
      printf 'Representative qios object: %s\n' "$first_object"
      file "$first_object"
      xcrun vtool -show-build "$first_object"
    )
  else
    rm -rf "$tmp"
    echo "Could not select a representative object from $ios_plugin" >&2
    exit 1
  fi
  rm -rf "$tmp"

  printf '%s\n' "$qt_toolchain" > \
    "$developer_root/athena-artifacts/qt-6.11.2-iphoneos-toolchain.path"
}

case "$phase" in
  download)
    download_qt
    ;;
  host)
    build_host
    ;;
  target)
    build_target
    ;;
  all)
    build_target
    ;;
esac
REMOTE
