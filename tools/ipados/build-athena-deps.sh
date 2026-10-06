#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

ssh_target="${ATHENA_IPADOS_SSH_TARGET:-felix@127.0.0.1}"
ssh_port="${ATHENA_IPADOS_SSH_PORT:-2222}"
remote_developer_root="${ATHENA_IPADOS_REMOTE_DEVELOPER_ROOT:-/Users/felix/Developer}"
remote_source="${ATHENA_IPADOS_REMOTE_SOURCE:-${remote_developer_root}/ATHENA}"
jobs="${ATHENA_IPADOS_JOBS:-20}"
phase="${1:-mimalloc}"

case "$phase" in
  mimalloc|boost) ;;
  *)
    printf 'usage: %s [mimalloc|boost]\n' "$0" >&2
    exit 2
    ;;
esac

"${script_dir}/bootstrap-vm.sh" --quiet
"${script_dir}/sync-source-to-vm.sh"

ssh -o BatchMode=yes -o ConnectTimeout=10 -p "$ssh_port" \
  "$ssh_target" sh -s -- "$remote_developer_root" "$remote_source" "$jobs" "$phase" <<'REMOTE'
set -eu

developer_root=$1
source_root=$2
jobs=$3
phase=$4

export PATH="/Users/felix/.cargo/bin:/opt/local/bin:/opt/local/sbin:$PATH"
export LC_ALL=en_US.UTF-8
export LANG=en_US.UTF-8

host_src="$developer_root/athena-deps/host/src"
build_root="$developer_root/athena-deps/ipados/build"
prefix="$developer_root/athena-deps/ipados/prefix/deps"
toolchain="$source_root/tools/ipados/iphoneos.toolchain.cmake"

mkdir -p "$host_src" "$build_root" "$prefix" "$developer_root/athena-artifacts"

banner () {
  printf '\n\n======================================================================\n'
  printf '%s\n' "$*"
  printf '======================================================================\n'
}

for tool in cmake ninja git curl shasum tar rsync xcrun file ar; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "Missing required host tool: $tool" >&2
    exit 1
  }
done

validate_archive () {
  label=$1
  archive=$2
  test -f "$archive" || {
    echo "$label archive not found: $archive" >&2
    exit 1
  }
  printf '\n--- %s target artifact ---\n' "$label"
  ls -lh "$archive"
  file "$archive"
  tmp=$(mktemp -d)
  object=$(ar -t "$archive" | grep -E '\.(o|obj)$' | head -1 || true)
  test -n "$object" || {
    rm -rf "$tmp"
    echo "No object found in $archive" >&2
    exit 1
  }
  (
    cd "$tmp"
    ar -x "$archive" "$object"
    printf 'Representative object: %s\n' "$object"
    file "$object"
    build_version=$(xcrun vtool -show-build "$object")
    printf '%s\n' "$build_version"
    printf '%s\n' "$build_version" | grep -Eq 'platform[[:space:]]+IOS'
    printf '%s\n' "$build_version" | grep -Eq 'minos[[:space:]]+27\.0'
  )
  rm -rf "$tmp"
}

build_mimalloc () {
  version=3.3.2
  tag=v3.3.2
  commit=30b2d9d89099bee08e9f67a1ffb3e12e7ba45227
  src="$host_src/mimalloc-$version"
  build="$build_root/mimalloc-$version"

  if [ ! -d "$src/.git" ]; then
    banner "Cloning mimalloc $tag with progress"
    rm -rf "$src"
    git clone --progress --depth 1 --branch "$tag" \
      https://github.com/microsoft/mimalloc.git "$src"
  fi

  actual=$(git -C "$src" rev-parse HEAD)
  printf 'mimalloc expected commit: %s\n' "$commit"
  printf 'mimalloc actual commit:   %s\n' "$actual"
  [ "$actual" = "$commit" ] || {
    echo "mimalloc source commit mismatch" >&2
    exit 1
  }

  banner "Configuring mimalloc $version for arm64 iPadOS 27"
  cmake -S "$src" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DMI_OVERRIDE=OFF \
    -DMI_OSX_ZONE=OFF \
    -DMI_OSX_INTERPOSE=OFF \
    -DMI_BUILD_SHARED=OFF \
    -DMI_BUILD_STATIC=ON \
    -DMI_BUILD_OBJECT=OFF \
    -DMI_BUILD_TESTS=OFF

  banner "Building mimalloc $version with ${jobs} jobs (verbose)"
  cmake --build "$build" --parallel "$jobs" --verbose

  banner "Installing mimalloc $version (verbose)"
  cmake --install "$build" --verbose

  config=$(find "$prefix/lib/cmake" -type f -name 'mimalloc-config.cmake' -print -quit 2>/dev/null || true)
  test -n "$config" || {
    echo "mimalloc CMake package config was not installed" >&2
    exit 1
  }

  archive=$(find "$prefix/lib" -type f \
    \( -name 'libmimalloc*.a' -o -name 'mimalloc*.a' \) -print -quit)
  validate_archive mimalloc "$archive"

  banner "Validating installed mimalloc CMake target"
  probe="$build_root/mimalloc-downstream-probe"
  rm -rf "$probe"
  mkdir -p "$probe/src"
  cat >"$probe/src/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.21)
project(MimallocIpadProbe LANGUAGES C)
find_package(mimalloc CONFIG REQUIRED)
if(NOT TARGET mimalloc-static)
  message(FATAL_ERROR "static mimalloc package does not export target 'mimalloc-static'")
endif()
add_library(probe STATIC probe.c)
target_link_libraries(probe PRIVATE mimalloc-static)
EOF
  cat >"$probe/src/probe.c" <<'EOF'
#include <mimalloc.h>
void *athena_mimalloc_probe(void) { return mi_malloc(64); }
EOF
  cmake -S "$probe/src" -B "$probe/build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
    -DCMAKE_PREFIX_PATH="$prefix" \
    -DCMAKE_BUILD_TYPE=Release
  cmake --build "$probe/build" --parallel 4 --verbose

  printf '%s\n' "$prefix" > \
    "$developer_root/athena-artifacts/athena-deps-ipados-prefix.path"
  echo "mimalloc iPadOS dependency ready: $prefix"
}

build_boost () {
  version=1.92.0
  archive=boost_1_92_0.tar.bz2
  sha256=5c1d40cb8e19adbf740a4ec2da35b3e58f3f5804b1dce44deb53df72193cbc6c
  url=https://archives.boost.io/release/1.92.0/source/boost_1_92_0.tar.bz2
  tarball="$host_src/$archive"
  src="$host_src/boost_1_92_0"

  if [ ! -f "$tarball" ]; then
    banner "Downloading Boost $version headers (~190 MB)"
    curl -L --fail --show-error --progress-bar \
      --retry 3 --retry-delay 2 \
      -o "$tarball.part" "$url"
    mv "$tarball.part" "$tarball"
  else
    banner "Boost $version archive already exists; verifying"
  fi

  actual=$(shasum -a 256 "$tarball" | awk '{print $1}')
  printf 'Boost expected SHA-256: %s\n' "$sha256"
  printf 'Boost actual SHA-256:   %s\n' "$actual"
  [ "$actual" = "$sha256" ] || {
    echo "Boost source checksum mismatch" >&2
    exit 1
  }

  if [ ! -d "$src/boost" ]; then
    banner "Extracting Boost $version source archive"
    tar -xjf "$tarball" -C "$host_src"
  fi

  test -f "$src/boost/version.hpp" || {
    echo "Boost headers were not extracted correctly" >&2
    exit 1
  }
  grep -q '^#define BOOST_VERSION 109200' "$src/boost/version.hpp" || {
    echo "Unexpected Boost header version" >&2
    exit 1
  }

  banner "Installing Boost $version header-only dependency"
  mkdir -p "$prefix/include/boost"
  rsync -a --delete --stats "$src/boost/" "$prefix/include/boost/"

  printf '%s\n' '--- Boost installed identity ---'
  grep -E '^#define BOOST_VERSION |^#define BOOST_LIB_VERSION ' \
    "$prefix/include/boost/version.hpp"

  banner "Validating Boost headers with arm64 iPadOS compiler"
  probe="$build_root/boost-header-probe"
  rm -rf "$probe"
  mkdir -p "$probe/src"
  cat >"$probe/src/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.21)
project(BoostIpadProbe LANGUAGES CXX)
find_package(Boost 1.92.0 REQUIRED)
add_library(probe STATIC probe.cpp)
target_include_directories(probe PRIVATE ${Boost_INCLUDE_DIRS})
target_compile_features(probe PRIVATE cxx_std_17)
EOF
  cat >"$probe/src/probe.cpp" <<'EOF'
#include <boost/asio/thread_pool.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/maximum_weighted_matching.hpp>
int athena_boost_probe() {
  boost::asio::thread_pool pool(1);
  pool.join();
  boost::adjacency_list<boost::vecS, boost::vecS, boost::undirectedS> graph(2);
  boost::add_edge(0, 1, graph);
  return static_cast<int>(boost::num_edges(graph));
}
EOF
  cmake -S "$probe/src" -B "$probe/build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
    -DBOOST_ROOT="$prefix" \
    -DBoost_NO_SYSTEM_PATHS=ON \
    -DCMAKE_BUILD_TYPE=Release
  cmake --build "$probe/build" --parallel 4 --verbose

  printf '%s\n' "$prefix" > \
    "$developer_root/athena-artifacts/athena-deps-ipados-prefix.path"
  echo "Boost header-only iPadOS dependency ready: $prefix"
}

case "$phase" in
  mimalloc) build_mimalloc ;;
  boost) build_boost ;;
esac
REMOTE
