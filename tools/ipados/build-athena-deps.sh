#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

ssh_target="${ATHENA_IPADOS_SSH_TARGET:-felix@127.0.0.1}"
ssh_port="${ATHENA_IPADOS_SSH_PORT:-2222}"
remote_developer_root="${ATHENA_IPADOS_REMOTE_DEVELOPER_ROOT:-/Users/felix/Developer}"
remote_source="${ATHENA_IPADOS_REMOTE_SOURCE:-${remote_developer_root}/ATHENA}"
jobs="${ATHENA_IPADOS_JOBS:-15}"
phase="${1:-mimalloc}"

case "$phase" in
  mimalloc|boost|vtk|zlib|zstd|bzip2|png|jpeg|brotli|freetype|harfbuzz|font-stack|nettle|libtasn1|libidn2|gnutls|gnutls-stack|libsodium|kf6-syntax|pegtl|msgpack|interop-headers|spdlog|hunspell|lmdb|icu|json|resvg) ;;
  *)
    printf 'usage: %s [mimalloc|boost|vtk|zlib|zstd|bzip2|png|jpeg|brotli|freetype|harfbuzz|font-stack|nettle|libtasn1|libidn2|gnutls|gnutls-stack|libsodium|spdlog|hunspell|lmdb|icu|json|resvg]\n' "$0" >&2
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
runtime_prefix="$developer_root/athena-deps/ipados/prefix/runtime"
toolchain="$source_root/tools/ipados/iphoneos.toolchain.cmake"

mkdir -p "$host_src" "$build_root" "$prefix" "$developer_root/athena-artifacts"

banner () {
  printf '\n\n======================================================================\n'
  printf '%s\n' "$*"
  printf '======================================================================\n'
}

for tool in cmake ninja git curl shasum tar rsync xcrun file ar gmake pkg-config; do
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

prepare_release_source () {
  name=$1
  url=$2
  sha256=$3
  extracted_name=$4

  archive="$host_src/$name"
  release_root="$host_src/releases"
  release_src="$release_root/$extracted_name"
  stamp="$release_src/.athena-release-sha256"

  if [ ! -f "$archive" ] || [ "$(shasum -a 256 "$archive" 2>/dev/null | awk '{print $1}')" != "$sha256" ]; then
    banner "Downloading $name with progress"
    rm -f "$archive" "$archive.part"
    curl -L --http1.1 --fail --show-error --progress-bar \
      --retry 3 --retry-delay 2 -o "$archive.part" "$url"
    mv "$archive.part" "$archive"
  fi

  actual=$(shasum -a 256 "$archive" | awk '{print $1}')
  printf '%s expected SHA-256: %s\n' "$name" "$sha256"
  printf '%s actual SHA-256:   %s\n' "$name" "$actual"
  [ "$actual" = "$sha256" ] || {
    echo "$name source checksum mismatch" >&2
    exit 1
  }

  if [ ! -f "$stamp" ] || [ "$(cat "$stamp")" != "$sha256" ]; then
    banner "Extracting pinned release $name"
    rm -rf "$release_src"
    mkdir -p "$release_root"
    tar -xf "$archive" -C "$release_root"
    test -d "$release_src" || {
      echo "Release archive did not create expected directory: $release_src" >&2
      exit 1
    }
    printf '%s\n' "$sha256" >"$stamp"
  fi

  test -x "$release_src/configure" || {
    echo "Pinned release is missing generated configure: $release_src" >&2
    exit 1
  }
}

set_autotools_target_env () {
  sdk=$(xcrun --sdk iphoneos --show-sdk-path)
  CC=$(xcrun --sdk iphoneos --find clang)
  CXX=$(xcrun --sdk iphoneos --find clang++)
  AR=$(xcrun --sdk iphoneos --find ar)
  RANLIB=$(xcrun --sdk iphoneos --find ranlib)
  NM=$(xcrun --sdk iphoneos --find nm)
  STRIP=$(xcrun --sdk iphoneos --find strip)
  target_flags="-arch arm64 -isysroot $sdk -miphoneos-version-min=27.0"
  CFLAGS="$target_flags -O2 -g -fPIC"
  CXXFLAGS="$target_flags -O2 -g -fPIC"
  CPPFLAGS="-I$prefix/include -I$runtime_prefix/include"
  LDFLAGS="$target_flags -L$prefix/lib -L$runtime_prefix/lib"
  PKG_CONFIG_PATH=
  PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig:$runtime_prefix/lib/pkgconfig:$runtime_prefix/share/pkgconfig"
  export CC CXX AR RANLIB NM STRIP CFLAGS CXXFLAGS CPPFLAGS LDFLAGS
  export PKG_CONFIG_PATH PKG_CONFIG_LIBDIR
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
  cmake --build "$probe/build" --parallel "$jobs" --verbose

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
  cmake --build "$probe/build" --parallel "$jobs" --verbose

  printf '%s\n' "$prefix" > \
    "$developer_root/athena-artifacts/athena-deps-ipados-prefix.path"
  echo "Boost header-only iPadOS dependency ready: $prefix"
}

build_vtk () {
  version=9.7.0
  tag=v9.7.0
  commit=23f0a095621e91bbdbeace8451e22b950c8e5f46
  src="$host_src/vtk-$version"
  driver="$build_root/vtk-$version-ios-driver"
  driver_prefix="$build_root/vtk-$version-driver-prefix"
  build="$build_root/vtk-$version-device-arm64"
  patch_file="$source_root/tools/ipados/patches/vtk-9.7.0-ipados-gles-header.patch"

  # Share ATHENA's physical font and image libraries, not VTK's bundled copies.
  for dependency in freetype png16 jpeg z; do
    test -f "$prefix/lib/lib${dependency}.a" || {
      echo "Build the iPadOS font-stack and jpeg phases before vtk: missing lib${dependency}.a" >&2
      exit 1
    }
  done

  if [ ! -d "$src/.git" ]; then
    banner "Cloning VTK $tag with progress"
    rm -rf "$src"
    git clone --progress --depth 1 --branch "$tag" \
      https://gitlab.kitware.com/vtk/vtk.git "$src"
  fi

  actual=$(git -C "$src" rev-parse HEAD)
  printf 'VTK expected commit: %s\n' "$commit"
  printf 'VTK actual commit:   %s\n' "$actual"
  [ "$actual" = "$commit" ] || {
    echo "VTK source commit mismatch" >&2
    exit 1
  }

  test -f "$patch_file" || {
    echo "Missing VTK iPadOS patch: $patch_file" >&2
    exit 1
  }

  banner "Resetting VTK source and applying the pinned iPadOS GLES header patch"
  git -C "$src" reset --hard "$commit"
  git -C "$src" clean -fd
  git -C "$src" apply --check "$patch_file"
  git -C "$src" apply "$patch_file"

  mkdir -p "$prefix" "$driver_prefix"

  banner "Configuring VTK $version iOS driver / native compile tools"
  cmake -S "$src" -B "$driver" -G Ninja \
    -DVTK_IOS_BUILD=ON \
    -DCMAKE_INSTALL_PREFIX="$driver_prefix" \
    -DCMAKE_BUILD_TYPE=Release \
    -DIOS_SIMULATOR_ARCHITECTURES:STRING= \
    -DIOS_DEVICE_ARCHITECTURES:STRING=arm64 \
    -DIOS_DEPLOYMENT_TARGET:STRING=27.0 \
    -DIOS_EMBED_BITCODE=OFF \
    -DVTK_BUILD_EXAMPLES=OFF \
    -DVTK_MODULE_ENABLE_VTK_IOImage=ON \
    -DVTK_MODULE_ENABLE_VTK_RenderingOpenGL2=ON

  banner "Building native VTK compile tools with ${jobs} jobs (verbose)"
  cmake --build "$driver" \
    --target vtk-compile-tools \
    --parallel "$jobs" \
    --verbose

  vtk_toolchain="$driver/CMake/ios.device.toolchain.arm64.cmake"
  test -f "$vtk_toolchain" || {
    echo "VTK iOS driver did not generate the arm64 device toolchain" >&2
    exit 1
  }

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

  banner "Configuring VTK $version static arm64 iPadOS 27 target"
  # VTK's finder first tries lowercase freetype-config.cmake. That export
  # omits dependency discovery; use its FindFreetype module branch instead.
  cmake -S "$src" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$vtk_toolchain" \
    -DCMAKE_CROSSCOMPILING=ON \
    -DCMAKE_SYSTEM_PROCESSOR=arm64 \
    -DVTKCompileTools_DIR="$driver/CompileTools" \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_PREFIX_PATH="$prefix" \
    -DVTK_MODULE_USE_EXTERNAL_VTK_freetype=ON \
    -DVTK_MODULE_USE_EXTERNAL_VTK_png=ON \
    -DVTK_MODULE_USE_EXTERNAL_VTK_jpeg=ON \
    -DVTK_MODULE_USE_EXTERNAL_VTK_zlib=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_freetype=ON \
    -DFREETYPE_INCLUDE_DIR_ft2build="$prefix/include/freetype2" \
    -DFREETYPE_INCLUDE_DIR_freetype2="$prefix/include/freetype2" \
    -DFREETYPE_LIBRARY_RELEASE="$prefix/lib/libfreetype.a" \
    -DPNG_PNG_INCLUDE_DIR="$prefix/include" \
    -DPNG_LIBRARY_RELEASE="$prefix/lib/libpng16.a" \
    -DJPEG_INCLUDE_DIR="$prefix/include" \
    -DJPEG_LIBRARY_RELEASE="$prefix/lib/libjpeg.a" \
    -DZLIB_INCLUDE_DIR="$prefix/include" \
    -DZLIB_LIBRARY_RELEASE="$prefix/lib/libz.a" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DVTK_BUILD_TESTING=OFF \
    -DVTK_BUILD_EXAMPLES=OFF \
    -DVTK_WRAP_PYTHON=OFF \
    -DVTK_WRAP_JAVA=OFF \
    -DVTK_WRAP_JAVASCRIPT=OFF \
    -DVTK_USE_64BIT_IDS=OFF \
    -DVTK_OPENGL_USE_GLES=ON \
    -DOPENGL_GLES3_INCLUDE_DIR="$opengles/Headers" \
    -DOPENGL_gles3_LIBRARY="$opengles/OpenGLES.tbd" \
    -DOPENGL_INCLUDE_DIR="$opengles/Headers" \
    -DOPENGL_gl_LIBRARY="$opengles/OpenGLES.tbd" \
    -DVTK_GROUP_ENABLE_Rendering=DONT_WANT \
    -DVTK_GROUP_ENABLE_StandAlone=DONT_WANT \
    -DVTK_GROUP_ENABLE_Imaging=DONT_WANT \
    -DVTK_GROUP_ENABLE_MPI=DONT_WANT \
    -DVTK_GROUP_ENABLE_Views=DONT_WANT \
    -DVTK_GROUP_ENABLE_Qt=DONT_WANT \
    -DVTK_GROUP_ENABLE_Web=DONT_WANT \
    -DVTK_MODULE_ENABLE_VTK_IOImage=YES \
    -DVTK_MODULE_ENABLE_VTK_RenderingOpenGL2=YES

  generated_glad="$build/ThirdParty/glad/vtk_glad.h"
  test -f "$generated_glad" || {
    echo "VTK did not generate vtk_glad.h" >&2
    exit 1
  }
  grep -q '#include <OpenGLES/ES3/gl.h>' "$generated_glad" || {
    echo "VTK target did not select Apple OpenGLES ES3 headers" >&2
    exit 1
  }
  if grep -q '#include <GLES3/gl3.h>' "$generated_glad" && \
     ! grep -q 'TARGET_OS_IPHONE' "$generated_glad"; then
    echo "VTK target retained a non-Apple GLES header path" >&2
    exit 1
  fi

  banner "Building VTK $version arm64 iPadOS dependency closure with ${jobs} jobs (verbose)"
  cmake --build "$build" --parallel "$jobs" --verbose

  banner "Installing VTK $version arm64 iPadOS package (verbose)"
  cmake --install "$build" --verbose

  vtk_config=$(find "$prefix/lib/cmake" -type f -name 'vtk-config.cmake' -print -quit 2>/dev/null || true)
  test -n "$vtk_config" || {
    echo "VTK CMake package was not installed" >&2
    exit 1
  }
  vtk_lib=$(find "$prefix/lib" -maxdepth 1 -type f -name 'libvtkRenderingOpenGL2-9.7.a' -print -quit)
  validate_archive VTK-RenderingOpenGL2 "$vtk_lib"

  banner "Validating installed VTK package and DataArt surface renderer includes"
  probe="$build_root/vtk-downstream-probe"
  rm -rf "$probe"
  mkdir -p "$probe/src"
  cat >"$probe/src/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.21)
project(VTKIpadProbe LANGUAGES CXX OBJCXX)
find_package(VTK 9.7 REQUIRED COMPONENTS
  CommonCore CommonDataModel IOImage RenderingCore RenderingOpenGL2)
add_library(probe STATIC probe.cpp)
target_link_libraries(probe PRIVATE
  VTK::CommonCore VTK::CommonDataModel VTK::IOImage
  VTK::RenderingCore VTK::RenderingOpenGL2)
vtk_module_autoinit(TARGETS probe MODULES VTK::RenderingOpenGL2)
EOF
  cat >"$probe/src/probe.cpp" <<'EOF'
#include <vtkNew.h>
#include <vtkPNGWriter.h>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
int athena_vtk_probe() {
  vtkNew<vtkRenderer> renderer;
  vtkNew<vtkRenderWindow> window;
  window->AddRenderer(renderer);
  vtkNew<vtkPNGWriter> writer;
  return writer ? 0 : 1;
}
EOF
  cmake -S "$probe/src" -B "$probe/build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$vtk_toolchain" \
    -DCMAKE_SYSTEM_PROCESSOR=arm64 \
    -DCMAKE_PREFIX_PATH="$prefix" \
    -DOPENGL_GLES3_INCLUDE_DIR="$opengles/Headers" \
    -DOPENGL_gles3_LIBRARY="$opengles/OpenGLES.tbd" \
    -DOPENGL_INCLUDE_DIR="$opengles/Headers" \
    -DOPENGL_gl_LIBRARY="$opengles/OpenGLES.tbd" \
    -DCMAKE_BUILD_TYPE=Release
  cmake --build "$probe/build" --parallel "$jobs" --verbose

  printf '%s\n' "$prefix" > \
    "$developer_root/athena-artifacts/vtk-ipados-prefix.path"
  echo "VTK $version iPadOS dependency ready: $prefix"
}

build_zlib () {
  version=1.3.1
  tag=v1.3.1
  commit=51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf
  src="$host_src/zlib-$version"
  build="$build_root/zlib-$version"

  if [ ! -d "$src/.git" ]; then
    banner "Cloning zlib $tag with progress"
    git clone --progress --depth 1 --branch "$tag" \
      https://github.com/madler/zlib.git "$src"
  fi
  actual=$(git -C "$src" rev-parse HEAD)
  printf 'zlib expected commit: %s\n' "$commit"
  printf 'zlib actual commit:   %s\n' "$actual"
  [ "$actual" = "$commit" ] || { echo "zlib source commit mismatch" >&2; exit 1; }

  banner "Configuring zlib $version static target for arm64 iPadOS 27"
  cmake -S "$src" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
    -DCMAKE_BUILD_TYPE=Release \
    -DZLIB_BUILD_EXAMPLES=OFF

  banner "Building zlib $version static library with ${jobs} jobs (verbose)"
  cmake --build "$build" --target zlibstatic --parallel "$jobs" --verbose

  banner "Installing zlib $version static target files"
  mkdir -p "$prefix/lib/pkgconfig" "$prefix/include"
  install -m 644 "$build/libz.a" "$prefix/lib/libz.a"
  install -m 644 "$src/zlib.h" "$build/zconf.h" "$prefix/include/"
  sed \
    -e "s|@prefix@|$prefix|g" \
    -e "s|@exec_prefix@|$prefix|g" \
    -e "s|@libdir@|$prefix/lib|g" \
    -e "s|@sharedlibdir@|$prefix/lib|g" \
    -e "s|@includedir@|$prefix/include|g" \
    -e "s|@VERSION@|$version|g" \
    "$src/zlib.pc.in" >"$prefix/lib/pkgconfig/zlib.pc"

  validate_archive zlib "$prefix/lib/libz.a"
  PKG_CONFIG_PATH= PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig" \
    pkg-config --modversion zlib
  echo "zlib $version iPadOS dependency ready: $prefix"
}

build_zstd () {
  version=1.5.7
  tag=v1.5.7
  commit=f8745da6ff1ad1e7bab384bd1f9d742439278e99
  src="$host_src/zstd-$version"
  build="$build_root/zstd-$version"

  if [ ! -d "$src/.git" ]; then
    banner "Cloning zstd $tag with progress"
    rm -rf "$src"
    git -c http.version=HTTP/1.1 clone --progress --depth 1 --branch "$tag" \
      https://github.com/facebook/zstd.git "$src"
  fi
  actual=$(git -C "$src" rev-parse HEAD)
  printf 'zstd expected commit: %s\n' "$commit"
  printf 'zstd actual commit:   %s\n' "$actual"
  [ "$actual" = "$commit" ] || {
    echo "zstd source commit mismatch" >&2
    exit 1
  }

  banner "Configuring zstd $version static library for arm64 iPadOS 27"
  cmake -S "$src/build/cmake" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_BUILD_TYPE=Release \
    -DZSTD_BUILD_STATIC=ON \
    -DZSTD_BUILD_SHARED=OFF \
    -DZSTD_BUILD_PROGRAMS=OFF \
    -DZSTD_BUILD_TESTS=OFF \
    -DZSTD_BUILD_CONTRIB=OFF

  banner "Building zstd $version with ${jobs} jobs (verbose)"
  cmake --build "$build" --target libzstd_static --parallel "$jobs" --verbose
  banner "Installing zstd $version (verbose)"
  cmake --install "$build" --verbose

  archive=$(find "$prefix/lib" -maxdepth 1 -type f -name 'libzstd.a' -print -quit)
  validate_archive zstd "$archive"
  test -f "$prefix/lib/pkgconfig/libzstd.pc" || {
    echo "zstd pkg-config metadata was not installed" >&2
    exit 1
  }

  banner "Validating libzstd pkg-config API with arm64 iPadOS compiler"
  probe="$build_root/zstd-downstream-probe"
  rm -rf "$probe"
  mkdir -p "$probe/src"
  cat >"$probe/src/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.21)
project(ZstdIpadProbe LANGUAGES C)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)
add_library(probe STATIC probe.c)
target_link_libraries(probe PRIVATE PkgConfig::ZSTD)
EOF
  cat >"$probe/src/probe.c" <<'EOF'
#include <zstd.h>
unsigned athena_zstd_probe(void) { return ZSTD_versionNumber(); }
EOF
  PKG_CONFIG_PATH= \
  PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig" \
    cmake -S "$probe/src" -B "$probe/build" -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
      -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
      -DCMAKE_PREFIX_PATH="$prefix" \
      -DCMAKE_BUILD_TYPE=Release
  cmake --build "$probe/build" --parallel "$jobs" --verbose
  echo "zstd $version iPadOS dependency ready: $prefix"
}

build_bzip2 () {
  version=1.0.8
  tag=bzip2-1.0.8
  commit=6a8690fc8d26c815e798c588f796eabe9d684cf0
  src="$host_src/bzip2-$version"
  build="$build_root/bzip2-$version"

  if [ ! -d "$src/.git" ]; then
    banner "Cloning bzip2 $tag with progress"
    git clone --progress --depth 1 --branch "$tag" \
      https://sourceware.org/git/bzip2.git "$src"
  fi
  actual=$(git -C "$src" rev-parse HEAD)
  printf 'bzip2 expected commit: %s\n' "$commit"
  printf 'bzip2 actual commit:   %s\n' "$actual"
  [ "$actual" = "$commit" ] || { echo "bzip2 source commit mismatch" >&2; exit 1; }

  banner "Preparing clean bzip2 $version build tree"
  rm -rf "$build"
  mkdir -p "$build/src"
  git -C "$src" archive HEAD | tar -x -C "$build/src"

  sdk=$(xcrun --sdk iphoneos --show-sdk-path)
  cc=$(xcrun --sdk iphoneos --find clang)
  target_ar=$(xcrun --sdk iphoneos --find ar)
  target_ranlib=$(xcrun --sdk iphoneos --find ranlib)
  target_cflags="-arch arm64 -isysroot $sdk -miphoneos-version-min=27.0 -fPIC -O3 -Wall -Winline -D_FILE_OFFSET_BITS=64"

  banner "Building bzip2 $version libbz2.a for arm64 iPadOS 27 (verbose)"
  gmake -C "$build/src" libbz2.a \
    CC="$cc" AR="$target_ar" RANLIB="$target_ranlib" \
    CFLAGS="$target_cflags"

  banner "Installing bzip2 $version static target files"
  mkdir -p "$prefix/lib/pkgconfig" "$prefix/include"
  install -m 644 "$build/src/libbz2.a" "$prefix/lib/libbz2.a"
  install -m 644 "$build/src/bzlib.h" "$prefix/include/bzlib.h"
  cat >"$prefix/lib/pkgconfig/bzip2.pc" <<EOF
prefix=$prefix
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: bzip2
Description: Lossless block-sorting data compression library
Version: $version
Libs: -L\${libdir} -lbz2
Cflags: -I\${includedir}
EOF

  validate_archive bzip2 "$prefix/lib/libbz2.a"
  PKG_CONFIG_PATH= PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig" \
    pkg-config --modversion bzip2
  echo "bzip2 $version iPadOS dependency ready: $prefix"
}

build_png () {
  version=1.6.58
  tag=v1.6.58
  commit=3061454d980de7d53608f594194cfac722721d2a
  src="$host_src/libpng-$version"
  build="$build_root/libpng-$version"

  if [ ! -d "$src/.git" ]; then
    banner "Cloning libpng $tag with progress"
    git clone --progress --depth 1 --branch "$tag" \
      https://github.com/pnggroup/libpng.git "$src"
  fi
  actual=$(git -C "$src" rev-parse HEAD)
  printf 'libpng expected commit: %s\n' "$commit"
  printf 'libpng actual commit:   %s\n' "$actual"
  [ "$actual" = "$commit" ] || { echo "libpng source commit mismatch" >&2; exit 1; }

  banner "Configuring libpng $version for arm64 iPadOS 27"
  cmake -S "$src" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
    -DZLIB_ROOT="$prefix" \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_BUILD_TYPE=Release \
    -DPNG_SHARED=OFF \
    -DPNG_STATIC=ON \
    -DPNG_FRAMEWORK=OFF \
    -DPNG_TESTS=OFF \
    -DPNG_TOOLS=OFF
  banner "Building libpng $version with ${jobs} jobs (verbose)"
  cmake --build "$build" --parallel "$jobs" --verbose
  banner "Installing libpng $version (verbose)"
  cmake --install "$build" --verbose

  archive=$(find "$prefix/lib" -maxdepth 1 -type f -name 'libpng*.a' -print -quit)
  validate_archive libpng "$archive"

  probe="$build_root/libpng-downstream-probe"
  rm -rf "$probe"
  mkdir -p "$probe/src"
  cat >"$probe/src/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.21)
project(PngIpadProbe LANGUAGES C)
find_package(PNG REQUIRED)
add_library(probe STATIC probe.c)
target_link_libraries(probe PRIVATE PNG::PNG)
EOF
  cat >"$probe/src/probe.c" <<'EOF'
#include <png.h>
unsigned long athena_png_probe(void) { return png_access_version_number(); }
EOF
  cmake -S "$probe/src" -B "$probe/build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
    -DCMAKE_PREFIX_PATH="$prefix" \
    -DCMAKE_BUILD_TYPE=Release
  cmake --build "$probe/build" --parallel "$jobs" --verbose
  echo "libpng $version iPadOS dependency ready: $prefix"
}

build_jpeg () {
  version=3.2.0
  tag=3.2.0
  commit=c85e6b905bf237038faa936dab160ebfc5da0344
  src="$host_src/libjpeg-turbo-$version"
  build="$build_root/libjpeg-turbo-$version"

  if [ ! -d "$src/.git" ]; then
    banner "Cloning libjpeg-turbo $tag with progress"
    rm -rf "$src"
    git -c http.version=HTTP/1.1 clone --progress --depth 1 --branch "$tag" \
      https://github.com/libjpeg-turbo/libjpeg-turbo.git "$src"
  fi
  actual=$(git -C "$src" rev-parse HEAD)
  printf 'libjpeg-turbo expected commit: %s\n' "$commit"
  printf 'libjpeg-turbo actual commit:   %s\n' "$actual"
  [ "$actual" = "$commit" ] || {
    echo "libjpeg-turbo source commit mismatch" >&2
    exit 1
  }

  banner "Configuring libjpeg-turbo $version static libjpeg for arm64 iPadOS 27"
  cmake -S "$src" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_SHARED=OFF \
    -DENABLE_STATIC=ON \
    -DWITH_TURBOJPEG=OFF \
    -DWITH_TOOLS=OFF \
    -DWITH_TESTS=OFF \
    -DWITH_FUZZ=OFF \
    -DWITH_JAVA=OFF

  banner "Building libjpeg-turbo $version jpeg-static with ${jobs} jobs (verbose)"
  cmake --build "$build" --target jpeg-static --parallel "$jobs" --verbose
  banner "Installing libjpeg-turbo $version (verbose)"
  cmake --install "$build" --verbose

  validate_archive JPEG "$prefix/lib/libjpeg.a"
  test -f "$prefix/include/jpeglib.h" || {
    echo "libjpeg-turbo did not install jpeglib.h" >&2
    exit 1
  }

  banner "Validating libjpeg with CMake FindJPEG on arm64 iPadOS"
  probe="$build_root/jpeg-downstream-probe"
  rm -rf "$probe"
  mkdir -p "$probe/src"
  cat >"$probe/src/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.21)
project(JpegIpadProbe LANGUAGES C)
find_package(JPEG REQUIRED)
add_library(probe STATIC probe.c)
target_link_libraries(probe PRIVATE JPEG::JPEG)
EOF
  cat >"$probe/src/probe.c" <<'EOF'
#include <stddef.h>
#include <stdio.h>
#include <jpeglib.h>
int athena_jpeg_probe(void) {
  struct jpeg_decompress_struct state;
  struct jpeg_error_mgr errors;
  state.err = jpeg_std_error(&errors);
  jpeg_create_decompress(&state);
  jpeg_destroy_decompress(&state);
  return 0;
}
EOF
  cmake -S "$probe/src" -B "$probe/build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
    -DCMAKE_PREFIX_PATH="$prefix" \
    -DJPEG_INCLUDE_DIR="$prefix/include" \
    -DJPEG_LIBRARY="$prefix/lib/libjpeg.a" \
    -DCMAKE_BUILD_TYPE=Release
  cmake --build "$probe/build" --parallel "$jobs" --verbose
  echo "libjpeg-turbo $version iPadOS dependency ready: $prefix"
}

build_brotli () {
  version=1.2.0
  tag=v1.2.0
  commit=028fb5a23661f123017c060daa546b55cf4bde29
  src="$host_src/brotli-$version"
  build="$build_root/brotli-$version"

  if [ ! -d "$src/.git" ]; then
    banner "Cloning Brotli $tag with progress"
    git clone --progress --depth 1 --branch "$tag" \
      https://github.com/google/brotli.git "$src"
  fi
  actual=$(git -C "$src" rev-parse HEAD)
  printf 'Brotli expected commit: %s\n' "$commit"
  printf 'Brotli actual commit:   %s\n' "$actual"
  [ "$actual" = "$commit" ] || { echo "Brotli source commit mismatch" >&2; exit 1; }

  banner "Configuring Brotli $version for arm64 iPadOS 27"
  cmake -S "$src" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DBROTLI_BUILD_TOOLS=OFF \
    -DBROTLI_DISABLE_TESTS=ON
  banner "Building Brotli $version with ${jobs} jobs (verbose)"
  cmake --build "$build" --parallel "$jobs" --verbose
  banner "Installing Brotli $version (verbose)"
  cmake --install "$build" --verbose

  validate_archive Brotli-decoder "$prefix/lib/libbrotlidec.a"
  validate_archive Brotli-common "$prefix/lib/libbrotlicommon.a"
  test -f "$prefix/lib/pkgconfig/libbrotlidec.pc" || {
    echo "Brotli decoder pkg-config metadata was not installed" >&2
    exit 1
  }
  echo "Brotli $version iPadOS dependency ready: $prefix"
}

build_freetype () {
  version=2.14.3
  tag=VER-2-14-3
  commit=0a0221a1347e2f1e07c395263540026e9a0aa7c7
  src="$host_src/freetype-$version"
  build="$build_root/freetype-$version"

  for required in "$prefix/lib/libbrotlidec.a" "$prefix/include/brotli/decode.h"; do
    test -e "$required" || { echo "FreeType prerequisite missing: $required" >&2; exit 1; }
  done
  find "$prefix/lib" -maxdepth 1 -type f -name 'libpng*.a' -print -quit | grep -q . || {
    echo "FreeType prerequisite libpng is missing from $prefix/lib" >&2
    exit 1
  }

  if [ ! -d "$src/.git" ]; then
    banner "Cloning FreeType $tag with progress"
    git clone --progress --depth 1 --branch "$tag" \
      https://gitlab.freedesktop.org/freetype/freetype.git "$src"
  fi
  actual=$(git -C "$src" rev-parse HEAD)
  printf 'FreeType expected commit: %s\n' "$commit"
  printf 'FreeType actual commit:   %s\n' "$actual"
  [ "$actual" = "$commit" ] || { echo "FreeType source commit mismatch" >&2; exit 1; }

  export PKG_CONFIG_PATH=
  export PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig"
  banner "Configuring FreeType $version for arm64 iPadOS 27"
  cmake -S "$src" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
    -DCMAKE_PREFIX_PATH="$prefix" \
    -DZLIB_ROOT="$prefix" \
    -DBZIP2_INCLUDE_DIR="$prefix/include" \
    -DBZIP2_LIBRARY_RELEASE="$prefix/lib/libbz2.a" \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DFT_REQUIRE_ZLIB=ON \
    -DFT_REQUIRE_BZIP2=ON \
    -DFT_REQUIRE_PNG=ON \
    -DFT_DISABLE_HARFBUZZ=ON \
    -DFT_REQUIRE_BROTLI=ON

  banner "Building FreeType $version with ${jobs} jobs (verbose)"
  cmake --build "$build" --parallel "$jobs" --verbose
  banner "Installing FreeType $version (verbose)"
  cmake --install "$build" --verbose
  validate_archive FreeType "$prefix/lib/libfreetype.a"

  for macro in ZLIB BZIP2 PNG BROTLI; do
    grep -Eq "^#define FT_CONFIG_OPTION_USE_$macro([[:space:]]|$)" \
      "$build/include/freetype/config/ftoption.h" || {
      echo "FreeType target did not enable expected feature: $macro" >&2
      exit 1
    }
  done
  if grep -q '^#define FT_CONFIG_OPTION_USE_HARFBUZZ' "$build/include/freetype/config/ftoption.h"; then
    echo "FreeType target unexpectedly enabled its optional HarfBuzz auto-hint dependency" >&2
    exit 1
  fi

  probe="$build_root/freetype-downstream-probe"
  rm -rf "$probe"
  mkdir -p "$probe/src"
  cat >"$probe/src/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.21)
project(FreeTypeIpadProbe LANGUAGES C)
find_package(Freetype REQUIRED)
add_library(probe STATIC probe.c)
target_link_libraries(probe PRIVATE Freetype::Freetype)
EOF
  cat >"$probe/src/probe.c" <<'EOF'
#include <ft2build.h>
#include FT_FREETYPE_H
int athena_freetype_probe(void) { FT_Library l = 0; return FT_Init_FreeType(&l); }
EOF
  cmake -S "$probe/src" -B "$probe/build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
    -DCMAKE_PREFIX_PATH="$prefix" \
    -DCMAKE_BUILD_TYPE=Release
  cmake --build "$probe/build" --parallel "$jobs" --verbose
  echo "FreeType $version iPadOS dependency ready: $prefix"
}

build_harfbuzz () {
  version=14.5.0
  tag=14.5.0
  commit=863d3f7787c6df18d20e4535c5906bf3eb803bd5
  src="$host_src/harfbuzz-$version"
  build="$build_root/harfbuzz-$version"

  test -f "$prefix/lib/libfreetype.a" || {
    echo "HarfBuzz prerequisite FreeType is missing" >&2
    exit 1
  }
  if [ ! -d "$src/.git" ]; then
    banner "Cloning HarfBuzz $tag with progress"
    git clone --progress --depth 1 --branch "$tag" \
      https://github.com/harfbuzz/harfbuzz.git "$src"
  fi
  actual=$(git -C "$src" rev-parse HEAD)
  printf 'HarfBuzz expected commit: %s\n' "$commit"
  printf 'HarfBuzz actual commit:   %s\n' "$actual"
  [ "$actual" = "$commit" ] || { echo "HarfBuzz source commit mismatch" >&2; exit 1; }

  export PKG_CONFIG_PATH=
  export PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig"
  banner "Configuring HarfBuzz $version for FreeType-owned ATHENA shaping on iPadOS"
  cmake -S "$src" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
    -DCMAKE_PREFIX_PATH="$prefix" \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DHB_HAVE_FREETYPE=ON \
    -DHB_HAVE_CORETEXT=OFF \
    -DHB_HAVE_GLIB=OFF \
    -DHB_HAVE_GOBJECT=OFF \
    -DHB_HAVE_INTROSPECTION=OFF \
    -DHB_HAVE_GRAPHITE2=OFF \
    -DHB_HAVE_ICU=OFF \
    -DHB_HAVE_CAIRO=OFF \
    -DHB_BUILD_UTILS=OFF \
    -DHB_BUILD_SUBSET=OFF \
    -DHB_BUILD_RASTER=OFF \
    -DHB_BUILD_VECTOR=OFF \
    -DHB_BUILD_GPU=OFF \
    -DHB_BUILD_GPU_DEMO=OFF

  banner "Building HarfBuzz $version with ${jobs} jobs (verbose)"
  cmake --build "$build" --parallel "$jobs" --verbose
  banner "Installing HarfBuzz $version (verbose)"
  cmake --install "$build" --verbose
  validate_archive HarfBuzz "$prefix/lib/libharfbuzz.a"

  test -f "$prefix/lib/pkgconfig/harfbuzz.pc" || {
    echo "HarfBuzz pkg-config metadata was not installed" >&2
    exit 1
  }
  grep -q 'freetype2' "$prefix/lib/pkgconfig/harfbuzz.pc" || {
    echo "HarfBuzz package does not record its FreeType dependency" >&2
    exit 1
  }

  banner "Validating HarfBuzz hb-ft API with arm64 iPadOS compiler"
  probe="$build_root/harfbuzz-downstream-probe"
  rm -rf "$probe"
  mkdir -p "$probe/src"
  cat >"$probe/src/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.21)
project(HarfBuzzIpadProbe LANGUAGES CXX)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)
find_package(Freetype REQUIRED)
add_library(probe STATIC probe.cpp)
target_link_libraries(probe PRIVATE PkgConfig::HARFBUZZ Freetype::Freetype)
target_compile_features(probe PRIVATE cxx_std_17)
EOF
  cat >"$probe/src/probe.cpp" <<'EOF'
#include <harfbuzz/hb-ft.h>
#include <harfbuzz/hb-ot.h>
int athena_harfbuzz_probe(FT_Face face) {
  hb_font_t* font = hb_ft_font_create_referenced(face);
  if (!font) return 1;
  hb_ot_font_set_funcs(font);
  hb_font_destroy(font);
  return 0;
}
EOF
  PKG_CONFIG_PATH= \
  PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig" \
    cmake -S "$probe/src" -B "$probe/build" -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
      -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
      -DCMAKE_PREFIX_PATH="$prefix" \
      -DCMAKE_BUILD_TYPE=Release
  cmake --build "$probe/build" --parallel "$jobs" --verbose
  echo "HarfBuzz $version iPadOS dependency ready: $prefix"
}

build_libsodium () {
  version=1.0.22
  archive=libsodium-1.0.22.tar.gz
  sha256=adbdd8f16149e81ac6078a03aca6fc03b592b89ef7b5ed83841c086191be3349
  url=https://github.com/jedisct1/libsodium/releases/download/1.0.22-RELEASE/libsodium-1.0.22.tar.gz
  src="$host_src/releases/libsodium-$version"
  build="$build_root/libsodium-$version"

  prepare_release_source "$archive" "$url" "$sha256" "libsodium-$version"
  set_autotools_target_env
  rm -rf "$build"
  mkdir -p "$build"

  banner "Configuring libsodium $version static library for arm64 iPadOS 27"
  (
    cd "$build"
    "$src/configure" \
      --build="$($src/build-aux/config.guess)" \
      --host=aarch64-apple-darwin \
      --prefix="$prefix" \
      --libdir="$prefix/lib" \
      --disable-shared \
      --enable-static
  )

  banner "Building libsodium $version with ${jobs} jobs (verbose)"
  gmake -C "$build" -j"$jobs" V=1
  banner "Installing libsodium $version (verbose)"
  gmake -C "$build" install V=1

  validate_archive libsodium "$prefix/lib/libsodium.a"
  test -f "$prefix/lib/pkgconfig/libsodium.pc" || {
    echo "libsodium pkg-config metadata was not installed" >&2
    exit 1
  }

  banner "Validating libsodium crypto API with arm64 iPadOS compiler"
  probe="$build_root/libsodium-probe"
  rm -rf "$probe"
  mkdir -p "$probe"
  cat >"$probe/probe.c" <<'EOF'
#include <sodium.h>
int athena_sodium_probe(void) {
  unsigned char pk[crypto_box_PUBLICKEYBYTES];
  unsigned char sk[crypto_box_SECRETKEYBYTES];
  if (sodium_init() < 0) return 1;
  if (crypto_box_keypair(pk, sk) != 0) return 2;
  return crypto_hash_sha256_BYTES == 32 ? 0 : 3;
}
int main(void) { return athena_sodium_probe(); }
EOF
  sdk=$(xcrun --sdk iphoneos --show-sdk-path)
  cc=$(xcrun --sdk iphoneos --find clang)
  sodium_cflags=$(PKG_CONFIG_PATH= PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig" \
    pkg-config --cflags libsodium)
  sodium_libs=$(PKG_CONFIG_PATH= PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig" \
    pkg-config --static --libs libsodium)
  # shellcheck disable=SC2086
  "$cc" -arch arm64 -isysroot "$sdk" -miphoneos-version-min=27.0 \
    $sodium_cflags "$probe/probe.c" $sodium_libs -o "$probe/probe"
  file "$probe/probe"
  xcrun vtool -show-build "$probe/probe"
  echo "libsodium $version iPadOS dependency ready: $prefix"
}

build_kf6_syntax () {
  version=6.30.0
  ecm_commit=68483132b87f4d7b953aec94ef51d1234d41f937
  syntax_commit=065ef8580096caea079fddc22d420c786cf53f92
  ecm_src="$host_src/extra-cmake-modules-$version"
  syntax_src="$host_src/syntax-highlighting-$version"
  syntax_patch="$source_root/tools/ipados/patches/kf6-syntax-highlighting-6.30.0-ipados-no-cli.patch"
  ecm_build="$developer_root/athena-deps/host/build/ecm-$version"
  ecm_prefix="$developer_root/athena-deps/host/prefix/ecm-$version"
  host_qt="$developer_root/athena-deps/host/build/qt-6.11.2-host/qtbase"
  target_qt="$developer_root/athena-deps/ipados/build/qt-6.11.2-iphoneos/qtbase"
  qt_toolchain="$target_qt/lib/cmake/Qt6/qt.toolchain.cmake"
  indexer_root="$build_root/kf6-syntax-indexer-host"
  indexer="$indexer_root/build/katehighlightingindexer"
  build="$build_root/kf6-syntax-highlighting-$version"

  if [ ! -d "$ecm_src/.git" ]; then
    banner "Cloning Extra CMake Modules v$version with progress"
    git -c http.version=HTTP/1.1 clone --progress --depth 1 --branch "v$version" \
      https://invent.kde.org/frameworks/extra-cmake-modules.git "$ecm_src"
  fi
  if [ ! -d "$syntax_src/.git" ]; then
    banner "Cloning KF6 SyntaxHighlighting v$version with progress"
    git -c http.version=HTTP/1.1 clone --progress --depth 1 --branch "v$version" \
      https://invent.kde.org/frameworks/syntax-highlighting.git "$syntax_src"
  fi
  [ "$(git -C "$ecm_src" rev-parse HEAD)" = "$ecm_commit" ] || {
    echo "ECM source commit mismatch" >&2; exit 1;
  }
  [ "$(git -C "$syntax_src" rev-parse HEAD)" = "$syntax_commit" ] || {
    echo "KF6 SyntaxHighlighting source commit mismatch" >&2; exit 1;
  }

  test -f "$syntax_patch" || {
    echo "Missing KF6 SyntaxHighlighting iPadOS patch: $syntax_patch" >&2
    exit 1
  }
  banner "Resetting KF6 SyntaxHighlighting source and disabling the unused iOS CLI"
  git -C "$syntax_src" reset --hard "$syntax_commit"
  git -C "$syntax_src" clean -fd
  git -C "$syntax_src" apply --check "$syntax_patch"
  git -C "$syntax_src" apply "$syntax_patch"

  banner "Configuring host Extra CMake Modules $version"
  cmake -S "$ecm_src" -B "$ecm_build" -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$ecm_prefix" \
    -DBUILD_TESTING=OFF \
    -DBUILD_HTML_DOCS=OFF \
    -DBUILD_MAN_DOCS=OFF \
    -DBUILD_QTHELP_DOCS=OFF
  cmake --build "$ecm_build" --parallel "$jobs" --verbose
  cmake --install "$ecm_build" --verbose
  ecm_dir="$ecm_prefix/share/ECM/cmake"
  test -f "$ecm_dir/ECMConfig.cmake" || {
    echo "ECM host package was not installed: $ecm_dir" >&2
    exit 1
  }

  banner "Building native katehighlightingindexer against host Qt 6.11.2"
  rm -rf "$indexer_root"
  mkdir -p "$indexer_root/src"
  cat >"$indexer_root/src/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.21)
project(AthenaKateHighlightingIndexer LANGUAGES CXX)
find_package(Qt6 6.11 REQUIRED COMPONENTS Core CONFIG)
add_executable(katehighlightingindexer
  "$syntax_src/src/indexer/katehighlightingindexer.cpp"
  "$syntax_src/src/lib/worddelimiters.cpp")
target_include_directories(katehighlightingindexer PRIVATE
  "$syntax_src/src/lib")
target_compile_features(katehighlightingindexer PRIVATE cxx_std_20)
target_link_libraries(katehighlightingindexer PRIVATE Qt6::Core)
EOF
  cmake -S "$indexer_root/src" -B "$indexer_root/build" -G Ninja \
    -DQt6_DIR="$host_qt/lib/cmake/Qt6" \
    -DCMAKE_PREFIX_PATH="$host_qt" \
    -DCMAKE_BUILD_TYPE=Release
  cmake --build "$indexer_root/build" --parallel "$jobs" --verbose
  test -x "$indexer" || {
    echo "Native katehighlightingindexer was not produced" >&2
    exit 1
  }
  file "$indexer"

  banner "Configuring KF6 SyntaxHighlighting $version for arm64 iPadOS 27"
  cmake -S "$syntax_src" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$qt_toolchain" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=27.0 \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_TESTING=OFF \
    -DBUILD_QCH=OFF \
    -DBUILD_WITH_QCH=OFF \
    -DKF_IGNORE_PLATFORM_CHECK=ON \
    -DKF_SKIP_PO_PROCESSING=ON \
    -DKSYNTAXHIGHLIGHTING_USE_GUI=ON \
    -DQRC_SYNTAX=ON \
    -DNO_STANDARD_PATHS=ON \
    -DKATEHIGHLIGHTINGINDEXER_EXECUTABLE="$indexer" \
    -DECM_DIR="$ecm_dir"

  banner "Building KF6 SyntaxHighlighting $version with ${jobs} jobs (verbose)"
  cmake --build "$build" --target KF6SyntaxHighlighting \
    --parallel "$jobs" --verbose
  banner "Installing KF6 SyntaxHighlighting $version (verbose)"
  cmake --install "$build" --verbose

  archive=$(find "$prefix/lib" -maxdepth 1 -type f \
    -name 'libKF6SyntaxHighlighting*.a' -print -quit)
  validate_archive KF6SyntaxHighlighting "$archive"

  banner "Validating installed KF6 SyntaxHighlighting CMake package"
  probe="$build_root/kf6-syntax-downstream-probe"
  rm -rf "$probe"
  mkdir -p "$probe/src"
  cat >"$probe/src/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.21)
project(KF6SyntaxIpadProbe LANGUAGES CXX)
find_package(KF6SyntaxHighlighting 6.30 REQUIRED CONFIG)
add_library(probe STATIC probe.cpp)
target_link_libraries(probe PRIVATE KF6::SyntaxHighlighting)
EOF
  cat >"$probe/src/probe.cpp" <<'EOF'
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Definition>
int athena_kf6_syntax_probe() {
  KSyntaxHighlighting::Repository repository;
  return repository.definitions().size();
}
EOF
  cmake -S "$probe/src" -B "$probe/build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$qt_toolchain" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=27.0 \
    -DCMAKE_PREFIX_PATH="$prefix" \
    -DKF6SyntaxHighlighting_DIR="$prefix/lib/cmake/KF6SyntaxHighlighting" \
    -DCMAKE_BUILD_TYPE=Release
  cmake --build "$probe/build" --parallel "$jobs" --verbose
  echo "KF6 SyntaxHighlighting $version iPadOS dependency ready: $prefix"
}

build_pegtl () {
  version=3.2.8
  tag=3.2.8
  commit=be527327653e94b02e711f7eff59285ad13e1db0
  src="$host_src/pegtl-$version"
  build="$build_root/pegtl-$version"

  if [ ! -d "$src/.git" ]; then
    banner "Cloning PEGTL $tag with progress"
    git -c http.version=HTTP/1.1 clone --progress --depth 1 --branch "$tag" \
      https://github.com/taocpp/PEGTL.git "$src"
  fi
  actual=$(git -C "$src" rev-parse HEAD)
  printf 'PEGTL expected commit: %s\n' "$commit"
  printf 'PEGTL actual commit:   %s\n' "$actual"
  [ "$actual" = "$commit" ] || {
    echo "PEGTL source commit mismatch" >&2
    exit 1
  }

  banner "Configuring PEGTL $version header-only package for arm64 iPadOS 27"
  cmake -S "$src" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DPEGTL_BUILD_TESTS=OFF \
    -DPEGTL_BUILD_EXAMPLES=OFF
  cmake --install "$build" --verbose
  test -f "$prefix/include/tao/pegtl.hpp" || {
    echo "PEGTL headers were not installed" >&2
    exit 1
  }
  test -f "$prefix/share/pegtl/cmake/pegtl-config.cmake" || {
    echo "PEGTL CMake package was not installed" >&2
    exit 1
  }
  echo "PEGTL $version iPadOS headers ready: $prefix"
}

build_msgpack () {
  version=9.0.0
  tag=cpp-9.0.0
  commit=e1b8bc889d0c16714fc9236af4b954cbfa182be4
  src="$host_src/msgpack-cxx-$version"
  build="$build_root/msgpack-cxx-$version"

  if [ ! -d "$src/.git" ]; then
    banner "Cloning msgpack-cxx $tag with progress"
    git -c http.version=HTTP/1.1 clone --progress --depth 1 --branch "$tag" \
      https://github.com/msgpack/msgpack-c.git "$src"
  fi
  actual=$(git -C "$src" rev-parse HEAD)
  printf 'msgpack-cxx expected commit: %s\n' "$commit"
  printf 'msgpack-cxx actual commit:   %s\n' "$actual"
  [ "$actual" = "$commit" ] || {
    echo "msgpack-cxx source commit mismatch" >&2
    exit 1
  }

  banner "Configuring msgpack-cxx $version header-only package for arm64 iPadOS 27"
  cmake -S "$src" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DMSGPACK_CXX11=OFF \
    -DMSGPACK_CXX17=ON \
    -DMSGPACK_USE_BOOST=OFF \
    -DMSGPACK_BUILD_TESTS=OFF \
    -DMSGPACK_BUILD_DOCS=OFF \
    -DMSGPACK_BUILD_EXAMPLES=OFF
  cmake --install "$build" --verbose
  test -f "$prefix/include/msgpack.hpp" || {
    echo "msgpack-cxx headers were not installed" >&2
    exit 1
  }
  test -f "$prefix/lib/cmake/msgpack-cxx/msgpack-cxx-config.cmake" || {
    echo "msgpack-cxx CMake package was not installed" >&2
    exit 1
  }
  echo "msgpack-cxx $version iPadOS headers ready: $prefix"
}

validate_interop_headers () {
  banner "Validating PEGTL + msgpack-cxx with arm64 iPadOS compiler"
  probe="$build_root/interop-headers-probe"
  rm -rf "$probe"
  mkdir -p "$probe/src"
  cat >"$probe/src/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.21)
project(InteropHeadersIpadProbe LANGUAGES CXX)
find_package(pegtl 3 REQUIRED CONFIG)
find_package(msgpack-cxx REQUIRED CONFIG)
add_library(probe STATIC probe.cpp)
target_compile_features(probe PRIVATE cxx_std_17)
target_link_libraries(probe PRIVATE taocpp::pegtl msgpack-cxx)
EOF
  cat >"$probe/src/probe.cpp" <<'EOF'
#include <tao/pegtl.hpp>
#include <msgpack.hpp>
#include <sstream>
namespace pegtl = tao::pegtl;
struct grammar : pegtl::plus<pegtl::digit> {};
int athena_interop_headers_probe() {
  pegtl::memory_input input("27", "probe");
  if (!pegtl::parse<grammar>(input)) return 1;
  std::stringstream stream;
  msgpack::pack(stream, 27);
  auto handle = msgpack::unpack(stream.str().data(), stream.str().size());
  return handle.get().as<int>() == 27 ? 0 : 2;
}
EOF
  cmake -S "$probe/src" -B "$probe/build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
    -DCMAKE_PREFIX_PATH="$prefix" \
    -Dpegtl_DIR="$prefix/share/pegtl/cmake" \
    -Dmsgpack-cxx_DIR="$prefix/lib/cmake/msgpack-cxx" \
    -DCMAKE_BUILD_TYPE=Release
  cmake --build "$probe/build" --parallel "$jobs" --verbose
}

build_spdlog () {
  version=1.17.0
  tag=v1.17.0
  commit=79524ddd08a4ec981b7fea76afd08ee05f83755d
  src="$host_src/spdlog-$version"
  build="$build_root/spdlog-$version"

  if [ ! -d "$src/.git" ]; then
    banner "Cloning spdlog $tag with progress"
    git clone --progress --depth 1 --branch "$tag" \
      https://github.com/gabime/spdlog.git "$src"
  fi
  actual=$(git -C "$src" rev-parse HEAD)
  printf 'spdlog expected commit: %s\n' "$commit"
  printf 'spdlog actual commit:   %s\n' "$actual"
  [ "$actual" = "$commit" ] || { echo "spdlog source commit mismatch" >&2; exit 1; }

  banner "Configuring spdlog $version with bundled fmt for arm64 iPadOS 27"
  cmake -S "$src" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_BUILD_TYPE=Release \
    -DSPDLOG_BUILD_SHARED=OFF \
    -DSPDLOG_FMT_EXTERNAL=OFF \
    -DSPDLOG_BUILD_EXAMPLE=OFF \
    -DSPDLOG_BUILD_TESTS=OFF \
    -DSPDLOG_BUILD_BENCH=OFF

  banner "Building spdlog $version with ${jobs} jobs (verbose)"
  cmake --build "$build" --parallel "$jobs" --verbose
  banner "Installing spdlog $version (verbose)"
  cmake --install "$build" --verbose

  archive=$(find "$prefix/lib" -maxdepth 1 -type f -name 'libspdlog*.a' -print -quit)
  validate_archive spdlog "$archive"
  test -f "$prefix/lib/pkgconfig/spdlog.pc" || {
    echo "spdlog pkg-config metadata was not installed" >&2
    exit 1
  }
  case "$(cat "$prefix/lib/pkgconfig/spdlog.pc")" in
    *'/opt/local/'*) echo "Host MacPorts path leaked into spdlog metadata" >&2; exit 1 ;;
  esac

  banner "Validating spdlog sinks with arm64 iPadOS compiler"
  probe="$build_root/spdlog-downstream-probe"
  rm -rf "$probe"
  mkdir -p "$probe/src"
  cat >"$probe/src/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.21)
project(SpdlogIpadProbe LANGUAGES CXX)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SPDLOG REQUIRED IMPORTED_TARGET spdlog)
add_library(probe STATIC probe.cpp)
target_link_libraries(probe PRIVATE PkgConfig::SPDLOG)
target_compile_features(probe PRIVATE cxx_std_17)
EOF
  cat >"$probe/src/probe.cpp" <<'EOF'
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
int athena_spdlog_probe() {
  auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  spdlog::logger logger("probe", sink);
  logger.info("ATHENA iPadOS spdlog probe");
  return 0;
}
EOF
  PKG_CONFIG_PATH= \
  PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig" \
    cmake -S "$probe/src" -B "$probe/build" -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
      -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
      -DCMAKE_PREFIX_PATH="$prefix" \
      -DCMAKE_BUILD_TYPE=Release
  cmake --build "$probe/build" --parallel "$jobs" --verbose
  echo "spdlog $version iPadOS dependency ready: $prefix"
}

build_hunspell () {
  version=1.7.2
  tag=v1.7.2
  commit=2969be996acad84b91ab3875b1816636fe61a40e
  src="$host_src/hunspell-$version"
  build="$build_root/hunspell-$version"

  if [ ! -d "$src/.git" ]; then
    banner "Cloning Hunspell $tag with progress"
    git clone --progress --depth 1 --branch "$tag" \
      https://github.com/hunspell/hunspell.git "$src"
  fi
  actual=$(git -C "$src" rev-parse HEAD)
  printf 'Hunspell expected commit: %s\n' "$commit"
  printf 'Hunspell actual commit:   %s\n' "$actual"
  [ "$actual" = "$commit" ] || { echo "Hunspell source commit mismatch" >&2; exit 1; }

  if [ ! -x "$src/configure" ]; then
    banner "Generating Hunspell $version autotools files (verbose)"
    (
      cd "$src"
      autoreconf -vfi
    )
  fi

  set_autotools_target_env
  rm -rf "$build"
  mkdir -p "$build"
  banner "Configuring Hunspell $version static library for arm64 iPadOS 27"
  (
    cd "$build"
    "$src/configure" \
      --build="$($src/config.guess)" \
      --host=aarch64-apple-darwin \
      --prefix="$prefix" \
      --libdir="$prefix/lib" \
      --disable-shared \
      --enable-static \
      --disable-nls \
      --without-ui \
      --without-readline
  )

  banner "Building Hunspell $version library with ${jobs} jobs (verbose)"
  gmake -C "$build/src/hunspell" -j"$jobs" V=1
  banner "Installing Hunspell $version library and headers (verbose)"
  gmake -C "$build/src/hunspell" install V=1
  mkdir -p "$prefix/lib/pkgconfig"
  install -m 644 "$build/hunspell.pc" "$prefix/lib/pkgconfig/hunspell.pc"

  validate_archive Hunspell "$prefix/lib/libhunspell-1.7.a"
  test -f "$prefix/include/hunspell/hunspell.hxx" || {
    echo "Hunspell C++ header was not installed" >&2
    exit 1
  }
  version_found=$(PKG_CONFIG_PATH= \
    PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig" \
    pkg-config --modversion hunspell)
  [ "$version_found" = "$version" ] || {
    echo "Unexpected Hunspell pkg-config version: $version_found" >&2
    exit 1
  }

  banner "Validating Hunspell C++ API with arm64 iPadOS compiler"
  probe="$build_root/hunspell-downstream-probe"
  rm -rf "$probe"
  mkdir -p "$probe/src"
  cat >"$probe/src/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.21)
project(HunspellIpadProbe LANGUAGES CXX)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HUNSPELL REQUIRED IMPORTED_TARGET hunspell>=1.7)
add_library(probe STATIC probe.cpp)
target_link_libraries(probe PRIVATE PkgConfig::HUNSPELL)
target_compile_features(probe PRIVATE cxx_std_17)
EOF
  cat >"$probe/src/probe.cpp" <<'EOF'
#include <hunspell.hxx>
int athena_hunspell_probe(const char* aff, const char* dic) {
  Hunspell spell(aff, dic);
  return spell.spell("athena") ? 0 : 1;
}
EOF
  PKG_CONFIG_PATH= \
  PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig" \
    cmake -S "$probe/src" -B "$probe/build" -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
      -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
      -DCMAKE_PREFIX_PATH="$prefix" \
      -DCMAKE_BUILD_TYPE=Release
  cmake --build "$probe/build" --parallel "$jobs" --verbose
  echo "Hunspell $version iPadOS dependency ready: $prefix"
}

build_lmdb () {
  version=0.9.35
  tag=LMDB_0.9.35
  commit=69087ced3cb6082f7dcfb4fc2dcaa3b68a7e2e8c
  lmdb_src="$host_src/lmdb-$version"
  lmdbxx_version=1.0.2
  lmdbxx_tag=1.0.2
  lmdbxx_commit=7e9e5adef1ae91f032cb4afcd953cfcfed203d44
  lmdbxx_src="$host_src/lmdbxx-$lmdbxx_version"
  build="$build_root/lmdb-$version"

  if [ ! -d "$lmdb_src/.git" ] || \
     [ "$(git -C "$lmdb_src" rev-parse HEAD 2>/dev/null || true)" != "$commit" ]; then
    banner "Cloning LMDB $tag with progress"
    rm -rf "$lmdb_src"
    git clone --progress --depth 1 --branch "$tag" \
      https://github.com/LMDB/lmdb.git "$lmdb_src"
  fi
  actual=$(git -C "$lmdb_src" rev-parse HEAD)
  printf 'LMDB expected commit: %s\n' "$commit"
  printf 'LMDB actual commit:   %s\n' "$actual"
  [ "$actual" = "$commit" ] || { echo "LMDB source commit mismatch" >&2; exit 1; }

  if [ ! -d "$lmdbxx_src/.git" ] || \
     [ "$(git -C "$lmdbxx_src" rev-parse HEAD 2>/dev/null || true)" != "$lmdbxx_commit" ]; then
    banner "Cloning lmdbxx $lmdbxx_tag with progress"
    rm -rf "$lmdbxx_src"
    git clone --progress --depth 1 --branch "$lmdbxx_tag" \
      https://github.com/hoytech/lmdbxx.git "$lmdbxx_src"
  fi
  actual=$(git -C "$lmdbxx_src" rev-parse HEAD)
  printf 'lmdbxx expected commit: %s\n' "$lmdbxx_commit"
  printf 'lmdbxx actual commit:   %s\n' "$actual"
  [ "$actual" = "$lmdbxx_commit" ] || { echo "lmdbxx source commit mismatch" >&2; exit 1; }

  rm -rf "$build"
  mkdir -p "$build/src"
  git -C "$lmdb_src" archive HEAD | tar -x -C "$build/src"

  sdk=$(xcrun --sdk iphoneos --show-sdk-path)
  cc=$(xcrun --sdk iphoneos --find clang)
  target_ar=$(xcrun --sdk iphoneos --find ar)
  target_flags="-arch arm64 -isysroot $sdk -miphoneos-version-min=27.0 -fPIC"
  lmdb_dir="$build/src/libraries/liblmdb"

  banner "Building LMDB $version liblmdb.a for arm64 iPadOS 27 (verbose)"
  gmake -C "$lmdb_dir" liblmdb.a \
    CC="$cc" AR="$target_ar" \
    THREADS=-pthread \
    OPT='-O2 -g' \
    XCFLAGS="$target_flags"

  banner "Installing LMDB $version and lmdbxx $lmdbxx_version target files"
  mkdir -p "$prefix/lib/pkgconfig" "$prefix/include"
  install -m 644 "$lmdb_dir/liblmdb.a" "$prefix/lib/liblmdb.a"
  install -m 644 "$lmdb_dir/lmdb.h" "$prefix/include/lmdb.h"
  install -m 644 "$lmdbxx_src/include/lmdbxx/lmdb++.h" "$prefix/include/lmdb++.h"
  cat >"$prefix/lib/pkgconfig/lmdb.pc" <<EOF
prefix=$prefix
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: lmdb
Description: OpenLDAP Lightning Memory-Mapped Database library
Version: $version
Libs: -L\${libdir} -llmdb
Cflags: -I\${includedir}
EOF

  validate_archive LMDB "$prefix/lib/liblmdb.a"
  version_found=$(PKG_CONFIG_PATH= \
    PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig" \
    pkg-config --modversion lmdb)
  [ "$version_found" = "$version" ] || {
    echo "Unexpected LMDB pkg-config version: $version_found" >&2
    exit 1
  }

  banner "Validating LMDB + lmdbxx C++17 API with arm64 iPadOS compiler"
  probe="$build_root/lmdb-downstream-probe"
  rm -rf "$probe"
  mkdir -p "$probe/src"
  cat >"$probe/src/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.21)
project(LmdbIpadProbe LANGUAGES CXX)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LMDB REQUIRED IMPORTED_TARGET lmdb)
add_library(probe STATIC probe.cpp)
target_link_libraries(probe PRIVATE PkgConfig::LMDB)
target_compile_features(probe PRIVATE cxx_std_17)
EOF
  cat >"$probe/src/probe.cpp" <<'EOF'
#include <lmdb++.h>
int athena_lmdb_probe() {
  auto env = lmdb::env::create();
  env.set_mapsize(16 * 1024 * 1024);
  return 0;
}
EOF
  PKG_CONFIG_PATH= \
  PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig" \
    cmake -S "$probe/src" -B "$probe/build" -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
      -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
      -DCMAKE_PREFIX_PATH="$prefix" \
      -DCMAKE_BUILD_TYPE=Release
  cmake --build "$probe/build" --parallel "$jobs" --verbose
  echo "LMDB $version + lmdbxx $lmdbxx_version iPadOS dependency ready: $prefix"
}

build_nettle () {
  version=3.10.2
  archive=nettle-$version.tar.gz
  sha256=fe9ff51cb1f2abb5e65a6b8c10a92da0ab5ab6eaf26e7fc2b675c45f1fb519b5
  url=https://mirrors.kernel.org/gnu/nettle/$archive
  prepare_release_source "$archive" "$url" "$sha256" "nettle-$version"
  src="$host_src/releases/nettle-$version"
  build="$build_root/nettle-$version"
  rm -rf "$build"; mkdir -p "$build"
  set_autotools_target_env
  build_triplet=$("$src/config.guess")

  banner "Configuring Nettle $version for arm64 iPadOS 27"
  (
    cd "$build"
    ac_cv_type_uid_t=yes \
    ac_cv_type_gid_t=yes \
    "$src/configure" \
      --build="$build_triplet" \
      --host=aarch64-apple-darwin \
      --prefix="$prefix" \
      --libdir="$prefix/lib" \
      --disable-shared \
      --enable-static \
      --disable-documentation
  )
  banner "Building Nettle/Hogweed $version with ${jobs} jobs (verbose)"
  gmake -C "$build" all-here -j"$jobs" V=1
  banner "Installing Nettle/Hogweed $version (verbose)"
  gmake -C "$build" install-here V=1
  rm -f "$prefix/bin/sexp-conv" "$prefix/bin/nettle-hash" \
    "$prefix/bin/nettle-pbkdf2" "$prefix/bin/nettle-lfib-stream" \
    "$prefix/bin/pkcs1-conv"
  validate_archive Nettle "$prefix/lib/libnettle.a"
  validate_archive Hogweed "$prefix/lib/libhogweed.a"
  echo "Nettle/Hogweed $version iPadOS dependency ready: $prefix"
}

build_libtasn1 () {
  version=4.21.0
  archive=libtasn1-$version.tar.gz
  sha256=1d8a444a223cc5464240777346e125de51d8e6abf0b8bac742ac84609167dc87
  url=https://mirrors.kernel.org/gnu/libtasn1/$archive
  prepare_release_source "$archive" "$url" "$sha256" "libtasn1-$version"
  src="$host_src/releases/libtasn1-$version"
  build="$build_root/libtasn1-$version"
  rm -rf "$build"; mkdir -p "$build"
  set_autotools_target_env
  build_triplet=$("$src/build-aux/config.guess")

  banner "Configuring libtasn1 $version for arm64 iPadOS 27"
  (
    cd "$build"
    "$src/configure" \
      --build="$build_triplet" \
      --host=aarch64-apple-darwin \
      --prefix="$prefix" \
      --libdir="$prefix/lib" \
      --disable-shared \
      --enable-static \
      --disable-doc
  )
  banner "Building libtasn1 $version with ${jobs} jobs (verbose)"
  gmake -C "$build" -j"$jobs" V=1
  banner "Installing libtasn1 $version (verbose)"
  gmake -C "$build" install V=1
  validate_archive libtasn1 "$prefix/lib/libtasn1.a"
  echo "libtasn1 $version iPadOS dependency ready: $prefix"
}

build_libidn2 () {
  version=2.3.8
  archive=libidn2-$version.tar.gz
  sha256=f557911bf6171621e1f72ff35f5b1825bb35b52ed45325dcdee931e5d3c0787a
  url=https://mirrors.kernel.org/gnu/libidn/$archive
  prepare_release_source "$archive" "$url" "$sha256" "libidn2-$version"
  src="$host_src/releases/libidn2-$version"
  build="$build_root/libidn2-$version"
  rm -rf "$build"; mkdir -p "$build"
  set_autotools_target_env
  build_triplet=$("$src/build-aux/config.guess")

  test -f "$runtime_prefix/lib/libunistring.a" || {
    echo "Private target libunistring is not ready: $runtime_prefix/lib/libunistring.a" >&2
    exit 1
  }
  banner "Configuring libidn2 $version with private target libunistring"
  (
    cd "$build"
    "$src/configure" \
      --build="$build_triplet" \
      --host=aarch64-apple-darwin \
      --prefix="$prefix" \
      --libdir="$prefix/lib" \
      --disable-shared \
      --enable-static \
      --disable-doc \
      --disable-nls \
      --with-libunistring-prefix="$runtime_prefix"
  )
  banner "Building libidn2 $version with ${jobs} jobs (verbose)"
  gmake -C "$build" -j"$jobs" V=1
  banner "Installing libidn2 $version (verbose)"
  gmake -C "$build" install V=1
  validate_archive libidn2 "$prefix/lib/libidn2.a"
  echo "libidn2 $version iPadOS dependency ready: $prefix"
}

build_gnutls () {
  version=3.8.13
  archive=gnutls-$version.tar.xz
  sha256=ffed8ec1bf09c2426d4f14aae377de4753b53e537d685e604e99a8b16ca9c97e
  url=https://www.gnupg.org/ftp/gcrypt/gnutls/v3.8/$archive
  prepare_release_source "$archive" "$url" "$sha256" "gnutls-$version"
  src="$host_src/releases/gnutls-$version"
  build="$build_root/gnutls-$version"
  rm -rf "$build"; mkdir -p "$build"
  set_autotools_target_env
  build_triplet=$("$src/build-aux/config.guess")

  for required in \
    "$prefix/lib/libnettle.a" \
    "$prefix/lib/libhogweed.a" \
    "$prefix/lib/libtasn1.a" \
    "$prefix/lib/libidn2.a" \
    "$runtime_prefix/lib/libgmp.a" \
    "$runtime_prefix/lib/libunistring.a"; do
    test -f "$required" || {
      echo "GnuTLS target prerequisite is missing: $required" >&2
      exit 1
    }
  done

  export GMP_CFLAGS="-I$runtime_prefix/include"
  export GMP_LIBS="-L$runtime_prefix/lib -lgmp"
  banner "Configuring GnuTLS $version static core for arm64 iPadOS 27"
  (
    cd "$build"
    ac_cv_search_u8_normalize='-lunistring -liconv' "$src/configure" \
      --build="$build_triplet" \
      --host=aarch64-apple-darwin \
      --prefix="$prefix" \
      --libdir="$prefix/lib" \
      --disable-shared \
      --enable-static \
      --disable-doc \
      --disable-tests \
      --disable-full-test-suite \
      --disable-tools \
      --disable-cxx \
      --disable-openssl-compatibility \
      --disable-libdane \
      --without-p11-kit \
      --without-tpm \
      --without-tpm2 \
      --without-brotli \
      --without-zstd \
      --with-system-priority-file=
  )

  # GnuTLS 3.8.13 probes -Wa,-march=all using an empty assembler input and
  # forgets the real target CCASFLAGS, which is a false positive with Apple
  # clang 21.  The actual AArch64 assembly sources compile correctly for
  # iPhoneOS without this GNU-assembler-only flag, so preserve acceleration
  # and remove only the bad flag from the generated target makefile.
  aarch64_makefile="$build/lib/accelerated/aarch64/Makefile"
  test -f "$aarch64_makefile" || {
    echo "GnuTLS AArch64 acceleration makefile was not generated" >&2
    exit 1
  }
  grep -q '^AARCH64_CCASFLAGS = -Wa,-march=all$' "$aarch64_makefile" || {
    echo "Unexpected GnuTLS AARCH64_CCASFLAGS; refusing an unverified rewrite" >&2
    grep '^AARCH64_CCASFLAGS' "$aarch64_makefile" >&2 || true
    exit 1
  }
  sed -i '' 's/^AARCH64_CCASFLAGS = -Wa,-march=all$/AARCH64_CCASFLAGS =/' \
    "$aarch64_makefile"
  printf '%s\n' 'GnuTLS iPadOS AArch64 assembler flags:'
  grep '^AARCH64_CCASFLAGS' "$aarch64_makefile"

  banner "Probing GnuTLS ARMv8 assembly with actual iPhoneOS flags"
  asm_probe="$build/athena-asm-probe"
  rm -rf "$asm_probe"; mkdir -p "$asm_probe"
  "$CC" $target_flags -O2 -g -fPIC \
    -c "$src/lib/accelerated/aarch64/macosx/sha256-armv8.s" \
    -o "$asm_probe/sha256-armv8.o"
  file "$asm_probe/sha256-armv8.o"
  asm_version=$(xcrun vtool -show-build "$asm_probe/sha256-armv8.o")
  printf '%s\n' "$asm_version"
  printf '%s\n' "$asm_version" | grep -Eq 'platform[[:space:]]+IOS'
  printf '%s\n' "$asm_version" | grep -Eq 'minos[[:space:]]+27\.0'

  banner "Building GnuTLS $version with ${jobs} jobs (verbose)"
  gmake -C "$build" -j"$jobs" V=1
  banner "Installing GnuTLS $version (verbose)"
  gmake -C "$build" install V=1
  validate_archive GnuTLS "$prefix/lib/libgnutls.a"

  banner "Validating static GnuTLS closure with a real arm64 iOS executable link"
  probe="$build_root/gnutls-link-probe"
  rm -rf "$probe"; mkdir -p "$probe"
  cat >"$probe/probe.c" <<'EOF'
#include <gnutls/abstract.h>
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
int main(void) {
  gnutls_privkey_t key = 0;
  if (gnutls_global_init() < 0) return 1;
  if (gnutls_privkey_init(&key) < 0) return 2;
  gnutls_privkey_deinit(key);
  gnutls_global_deinit();
  return 0;
}
EOF
  set_autotools_target_env
  static_cflags=$(pkg-config --static --cflags gnutls)
  static_libs=$(pkg-config --static --libs gnutls)
  printf 'GnuTLS static CFLAGS: %s\n' "$static_cflags"
  printf 'GnuTLS static LIBS:   %s\n' "$static_libs"
  # shellcheck disable=SC2086
  "$CC" $target_flags $static_cflags "$probe/probe.c" \
    $static_libs -L"$runtime_prefix/lib" -lgmp -lunistring -liconv \
    -o "$probe/gnutls-probe"
  file "$probe/gnutls-probe"
  build_version=$(xcrun vtool -show-build "$probe/gnutls-probe")
  printf '%s\n' "$build_version"
  printf '%s\n' "$build_version" | grep -Eq 'platform[[:space:]]+IOS'
  printf '%s\n' "$build_version" | grep -Eq 'minos[[:space:]]+27\.0'

  echo "GnuTLS $version iPadOS dependency ready: $prefix"
}

build_icu () {
  version=78.3
  tag=release-78.3
  commit=21d1eb0f306e1141c10931e914dfc038c06121da
  src="$host_src/icu-$version"
  host_build="$developer_root/athena-deps/host/build/icu-$version"
  host_prefix="$developer_root/athena-deps/host/icu-$version-prefix"
  build="$build_root/icu-$version"

  if [ ! -d "$src/.git" ]; then
    banner "Cloning ICU $tag with progress"
    rm -rf "$src"
    git clone --progress --depth 1 --branch "$tag" \
      https://github.com/unicode-org/icu.git "$src"
  fi
  actual=$(git -C "$src" rev-parse HEAD)
  printf 'ICU expected commit: %s\n' "$commit"
  printf 'ICU actual commit:   %s\n' "$actual"
  [ "$actual" = "$commit" ] || { echo "ICU source commit mismatch" >&2; exit 1; }

  mkdir -p "$host_build" "$host_prefix"
  if [ ! -f "$host_build/Makefile" ]; then
    banner "Configuring native ICU $version cross-build tools"
    (
      cd "$host_build"
      CC=/usr/bin/clang CXX=/usr/bin/clang++ \
        "$src/icu4c/source/configure" \
          --prefix="$host_prefix" \
          --enable-static \
          --disable-shared \
          --disable-tests \
          --disable-samples \
          --disable-extras \
          --disable-icuio
    )
  else
    banner "Native ICU $version cross-build tools are already configured"
  fi

  banner "Building native ICU $version tools with ${jobs} jobs (verbose)"
  gmake -C "$host_build" -j"$jobs" VERBOSE=1
  for tool in genrb icupkg pkgdata makeconv gennorm2; do
    test -x "$host_build/bin/$tool" || {
      echo "Native ICU cross-build tool was not produced: $host_build/bin/$tool" >&2
      exit 1
    }
  done

  set_autotools_target_env
  rm -rf "$build"
  mkdir -p "$build"
  banner "Configuring ICU $version static libraries for arm64 iPadOS 27"
  (
    cd "$build"
    "$src/icu4c/source/configure" \
      --build="$($src/icu4c/source/config.guess)" \
      --host=aarch64-apple-darwin \
      --with-cross-build="$host_build" \
      --prefix="$prefix" \
      --libdir="$prefix/lib" \
      --enable-static \
      --disable-shared \
      --disable-dyload \
      --disable-tests \
      --disable-samples \
      --disable-extras \
      --disable-icuio \
      --disable-tools \
      --with-data-packaging=static
  )

  banner "Building ICU $version target libraries with ${jobs} jobs (verbose)"
  gmake -C "$build" -j"$jobs" VERBOSE=1
  banner "Installing ICU $version target libraries (verbose)"
  gmake -C "$build" install VERBOSE=1

  validate_archive ICU-uc "$prefix/lib/libicuuc.a"
  validate_archive ICU-i18n "$prefix/lib/libicui18n.a"
  validate_archive ICU-data "$prefix/lib/libicudata.a"

  grep -q '^#define U_ICU_VERSION "78.3"' "$prefix/include/unicode/uvernum.h" || {
    echo "Installed ICU headers are not version $version" >&2
    exit 1
  }

  banner "Validating ICU 78.3 APIs and exact target package selection"
  probe="$build_root/icu-downstream-probe"
  rm -rf "$probe"
  mkdir -p "$probe/src"
  cat >"$probe/src/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.21)
project(IcuIpadProbe LANGUAGES CXX)
find_package(ICU 78.3 EXACT REQUIRED COMPONENTS uc i18n)
message(STATUS "ICU_VERSION=${ICU_VERSION}")
message(STATUS "ICU_INCLUDE_DIRS=${ICU_INCLUDE_DIRS}")
message(STATUS "ICU_LIBRARIES=${ICU_LIBRARIES}")
add_executable(probe probe.cpp)
target_link_libraries(probe PRIVATE ${ICU_LIBRARIES})
target_include_directories(probe PRIVATE ${ICU_INCLUDE_DIRS})
target_compile_features(probe PRIVATE cxx_std_17)
EOF
  cat >"$probe/src/probe.cpp" <<'EOF'
#include <unicode/casemap.h>
#include <unicode/edits.h>
#include <unicode/ubrk.h>
#include <unicode/ucnv.h>
#include <unicode/unorm2.h>
#include <unicode/bytestream.h>
#include <string>
int main() {
  UErrorCode status = U_ZERO_ERROR;
  UBreakIterator* breaker = ubrk_open(UBRK_CHARACTER, "root", nullptr, 0, &status);
  if (U_FAILURE(status) || !breaker) return 1;
  ubrk_close(breaker);
  UConverter* converter = ucnv_open("UTF-8", &status);
  if (U_FAILURE(status) || !converter) return 2;
  ucnv_close(converter);
  if (!unorm2_getNFKCInstance(&status) || U_FAILURE(status)) return 3;
  std::string folded;
  icu::StringByteSink<std::string> sink(&folded);
  icu::Edits edits;
  icu::CaseMap::utf8Fold(0, icu::StringPiece("ATHENA"), sink, &edits, status);
  return U_FAILURE(status) ? 4 : 0;
}
EOF
  cmake -S "$probe/src" -B "$probe/build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DATHENA_IPADOS_TARGET_PREFIX="$prefix" \
    -DICU_ROOT="$prefix" \
    -DICU_INCLUDE_DIR="$prefix/include" \
    -DICU_UC_LIBRARY_RELEASE="$prefix/lib/libicuuc.a" \
    -DICU_I18N_LIBRARY_RELEASE="$prefix/lib/libicui18n.a" \
    -DCMAKE_BUILD_TYPE=Release
  cmake --build "$probe/build" --parallel "$jobs" --verbose
  file "$probe/build/probe"
  build_version=$(xcrun vtool -show-build "$probe/build/probe")
  printf '%s\n' "$build_version"
  printf '%s\n' "$build_version" | grep -Eq 'platform[[:space:]]+IOS'
  printf '%s\n' "$build_version" | grep -Eq 'minos[[:space:]]+27\.0'
  echo "ICU $version iPadOS dependency ready: $prefix"
}

build_json () {
  version=3.12.0
  tag=v3.12.0
  commit=55f93686c01528224f448c19128836e7df245f72
  src="$host_src/nlohmann-json-$version"

  if [ ! -d "$src/.git" ]; then
    banner "Cloning nlohmann/json $tag with progress"
    rm -rf "$src"
    git clone --progress --depth 1 --branch "$tag" \
      https://github.com/nlohmann/json.git "$src"
  fi
  actual=$(git -C "$src" rev-parse HEAD)
  printf 'nlohmann/json expected commit: %s\n' "$commit"
  printf 'nlohmann/json actual commit:   %s\n' "$actual"
  [ "$actual" = "$commit" ] || {
    echo "nlohmann/json source commit mismatch" >&2
    exit 1
  }
  test -f "$src/single_include/nlohmann/json.hpp" || {
    echo "nlohmann/json single-include tree is incomplete" >&2
    exit 1
  }

  banner "Installing nlohmann/json $version header-only dependency"
  mkdir -p "$prefix/include/nlohmann"
  rsync -a --delete --stats \
    "$src/single_include/nlohmann/" "$prefix/include/nlohmann/"

  banner "Validating nlohmann/json $version with arm64 iPadOS compiler"
  probe="$build_root/nlohmann-json-probe"
  rm -rf "$probe"
  mkdir -p "$probe"
  cat >"$probe/probe.cpp" <<'EOF'
#include <nlohmann/json.hpp>
int athena_json_probe() {
  nlohmann::json value = {{"athena", 27}, {"platform", "ipados"}};
  return value["athena"].get<int>() == 27 ? 0 : 1;
}
EOF
  sdk=$(xcrun --sdk iphoneos --show-sdk-path)
  cxx=$(xcrun --sdk iphoneos --find clang++)
  "$cxx" -arch arm64 -isysroot "$sdk" -miphoneos-version-min=27.0 \
    -std=c++17 -I"$prefix/include" -c "$probe/probe.cpp" -o "$probe/probe.o"
  file "$probe/probe.o"
  build_version=$(xcrun vtool -show-build "$probe/probe.o")
  printf '%s\n' "$build_version"
  printf '%s\n' "$build_version" | grep -Eq 'platform[[:space:]]+IOS'
  printf '%s\n' "$build_version" | grep -Eq 'minos[[:space:]]+27\.0'
  echo "nlohmann/json $version iPadOS dependency ready: $prefix"
}

build_resvg () {
  version=0.48.1
  tag=v0.48.1
  commit=68b14c4c3bccdb60344c777406486b54c36ec1a4
  src="$host_src/resvg-$version"
  cargo_target="$build_root/resvg-$version-cargo"
  artifact="$cargo_target/aarch64-apple-ios/release/libresvg.a"
  patch_file="$source_root/tools/ipados/patches/resvg-0.48.1-ipados-explicit-fonts.patch"

  if [ ! -d "$src/.git" ]; then
    banner "Cloning resvg $tag with progress"
    rm -rf "$src"
    git -c http.version=HTTP/1.1 clone --progress --depth 1 --branch "$tag" \
      https://github.com/linebender/resvg.git "$src"
  fi
  actual=$(git -C "$src" rev-parse HEAD)
  printf 'resvg expected commit: %s\n' "$commit"
  printf 'resvg actual commit:   %s\n' "$actual"
  [ "$actual" = "$commit" ] || {
    echo "resvg source commit mismatch" >&2
    exit 1
  }

  test -f "$patch_file" || {
    echo "Missing resvg iPadOS font patch: $patch_file" >&2
    exit 1
  }
  banner "Resetting resvg source and applying explicit-font-only iPadOS patch"
  git -C "$src" reset --hard "$commit"
  git -C "$src" clean -fd
  git -C "$src" apply --check "$patch_file"
  git -C "$src" apply "$patch_file"

  command -v cargo >/dev/null 2>&1 || {
    echo "cargo is required to build resvg" >&2
    exit 1
  }
  rustup target list --installed | grep -qx aarch64-apple-ios || {
    echo "Rust target aarch64-apple-ios is not installed" >&2
    exit 1
  }

  mkdir -p "$cargo_target"
  export CARGO_TARGET_DIR="$cargo_target"
  export IPHONEOS_DEPLOYMENT_TARGET=27.0
  export CARGO_HTTP_MULTIPLEXING=false

  banner "Building resvg $version static C API for arm64 iPadOS 27 (verbose)"
  # Do not enable resvg's system-fonts/fontconfig or memmap-fonts features.
  # ATHENA injects explicit owner-scoped physical font files at runtime.
  cargo build \
    --manifest-path "$src/Cargo.toml" \
    --package resvg-capi \
    --release \
    --locked \
    --target aarch64-apple-ios \
    --no-default-features \
    --features svgz,text,raster-images \
    -j "$jobs" \
    -vv

  test -f "$artifact" || {
    echo "resvg static library was not produced: $artifact" >&2
    exit 1
  }

  banner "Installing resvg $version static C/Qt interface"
  mkdir -p "$prefix/lib" "$prefix/include"
  install -m 644 "$artifact" "$prefix/lib/libresvg.a"
  install -m 644 "$src/crates/c-api/resvg.h" "$prefix/include/resvg.h"
  install -m 644 "$src/crates/c-api/ResvgQt.h" "$prefix/include/ResvgQt.h"
  validate_archive resvg "$prefix/lib/libresvg.a"

  banner "Validating resvg C API object for arm64 iPadOS"
  probe="$build_root/resvg-c-api-probe"
  rm -rf "$probe"
  mkdir -p "$probe"
  cat >"$probe/probe.c" <<'EOF'
#include <resvg.h>
int athena_resvg_probe(void) {
  resvg_options* options = resvg_options_create();
  if (!options) return 1;
  resvg_options_destroy(options);
  return 0;
}
EOF
  sdk=$(xcrun --sdk iphoneos --show-sdk-path)
  cc=$(xcrun --sdk iphoneos --find clang)
  "$cc" -arch arm64 -isysroot "$sdk" -miphoneos-version-min=27.0 \
    -I"$prefix/include" -c "$probe/probe.c" -o "$probe/probe.o"
  file "$probe/probe.o"
  build_version=$(xcrun vtool -show-build "$probe/probe.o")
  printf '%s\n' "$build_version"
  printf '%s\n' "$build_version" | grep -Eq 'platform[[:space:]]+IOS'
  printf '%s\n' "$build_version" | grep -Eq 'minos[[:space:]]+27\.0'
  echo "resvg $version iPadOS dependency ready: $prefix"
}

case "$phase" in
  mimalloc) build_mimalloc ;;
  boost) build_boost ;;
  vtk) build_vtk ;;
  zlib) build_zlib ;;
  zstd) build_zstd ;;
  bzip2) build_bzip2 ;;
  png) build_png ;;
  jpeg) build_jpeg ;;
  brotli) build_brotli ;;
  freetype) build_freetype ;;
  harfbuzz) build_harfbuzz ;;
  spdlog) build_spdlog ;;
  hunspell) build_hunspell ;;
  lmdb) build_lmdb ;;
  icu) build_icu ;;
  json) build_json ;;
  resvg) build_resvg ;;
  libsodium) build_libsodium ;;
  kf6-syntax) build_kf6_syntax ;;
  pegtl) build_pegtl ;;
  msgpack) build_msgpack ;;
  nettle) build_nettle ;;
  libtasn1) build_libtasn1 ;;
  libidn2) build_libidn2 ;;
  gnutls) build_gnutls ;;
  font-stack)
    build_zlib
    build_bzip2
    build_png
    build_brotli
    build_freetype
    build_harfbuzz
    ;;
  gnutls-stack)
    build_nettle
    build_libtasn1
    build_libidn2
    build_gnutls
    ;;
  interop-headers)
    build_pegtl
    build_msgpack
    validate_interop_headers
    ;;
esac
REMOTE
