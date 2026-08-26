#!/usr/bin/env bash
set -euo pipefail

repo_root="${1:-}"
if [ -z "$repo_root" ]; then
  echo "usage: opensuse-build-inside.sh /path/to/repo" >&2
  exit 2
fi
shift || true

repo_root="$(cd -- "$repo_root" && pwd)"
container_build_dir="$repo_root/container_build"
deps_dir="$container_build_dir/deps"
prefix="$deps_dir/prefix"
src_dir="$deps_dir/src"
tools_dir="$container_build_dir/tools"
jobs="${ATHENA_BUILD_JOBS:-$(nproc)}"
git_timeout="${ATHENA_GIT_TIMEOUT:-300}"
qt_version="${ATHENA_QT_VERSION:-6.11.1}"
boost_version="${ATHENA_BOOST_VERSION:-1.87.0}"
rust_toolchain="${ATHENA_RUST_TOOLCHAIN:-1.88.0}"
boost_version_underscore="${boost_version//./_}"
athena_version="$(sed -n 's/.*set *(ATHENA_APP_VERSION *"\([^"]*\)".*/\1/p' "$repo_root/CMakeLists.txt" | head -n1)"
qt_root="$deps_dir/qt"
qt_prefix="$qt_root/$qt_version/gcc_64"
ads_patched_src="$deps_dir/ads-patched/qt6"
rapidfuzz_src="$src_dir/rapidfuzz-cpp"
boost_src="$src_dir/boost_$boost_version_underscore"

python_bin="$(command -v python3)"

mkdir -p "$prefix" "$src_dir" "$tools_dir" "$container_build_dir/logs"

export RUSTUP_HOME="$container_build_dir/rustup"
export CARGO_HOME="$container_build_dir/cargo"
export CARGO_TARGET_DIR="$container_build_dir/cargo-target"
export RUSTUP_TOOLCHAIN="$rust_toolchain"

prepend_build_paths () {
  export PATH="$HOME/.local/bin:$qt_prefix/bin:$prefix/bin:/opt/intel/oneapi/compiler/latest/bin:$PATH"
  export PKG_CONFIG_PATH="$prefix/lib64/pkgconfig:$prefix/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
  export LD_LIBRARY_PATH="$qt_prefix/lib:$prefix/lib64:$prefix/lib:${LD_LIBRARY_PATH:-}"
  export CMAKE_PREFIX_PATH="$qt_prefix:$prefix:${CMAKE_PREFIX_PATH:-}"
}

prepend_build_paths

if [ -r /opt/intel/oneapi/setvars.sh ]; then
  # shellcheck disable=SC1091
  set +u
  source /opt/intel/oneapi/setvars.sh --force >/dev/null
  set -u
fi
prepend_build_paths

if command -v icx >/dev/null 2>&1 && command -v icpx >/dev/null 2>&1; then
  cc_bin="$(command -v icx)"
  cxx_bin="$(command -v icpx)"
else
  if [ "${ATHENA_ALLOW_SYSTEM_COMPILER:-0}" != "1" ]; then
    echo "Intel icx/icpx not available in the container." >&2
    exit 1
  fi
  cc_bin="$(command -v gcc)"
  cxx_bin="$(command -v g++)"
fi

compiler_base_flags="-march=x86-64 -mtune=generic"
if [[ "$cxx_bin" == *icpx ]] &&
   [ -d /usr/lib64/gcc/x86_64-suse-linux/13 ]; then
  compiler_base_flags="--gcc-toolchain=/usr $compiler_base_flags"
fi

if ! command -v uv >/dev/null 2>&1 ||
   ! "$python_bin" -c 'import aqt' >/dev/null 2>&1; then
  "$python_bin" -m pip install --user uv aqtinstall >/dev/null
fi

git_clone_retry () {
  local dest="$1"
  shift

  local attempt
  for attempt in 1 2 3; do
    rm -rf "$dest"
    if timeout "$git_timeout" git clone "$@" "$dest"; then
      return
    fi
    if [ "$attempt" -lt 3 ]; then
      sleep "$((attempt * 10))"
    fi
  done
  return 1
}

git_retry () {
  local attempt
  for attempt in 1 2 3; do
    if timeout "$git_timeout" git "$@"; then
      return
    fi
    if [ "$attempt" -lt 3 ]; then
      sleep "$((attempt * 10))"
    fi
  done
  return 1
}

ensure_qt () {
  if [ -x "$qt_prefix/bin/qtpaths6" ] &&
     [ "$("$qt_prefix/bin/qtpaths6" --qt-version)" = "$qt_version" ]; then
    return
  fi

  rm -rf "$qt_prefix"
  local mirror_args=()
  if [ -n "${ATHENA_QT_MIRROR:-}" ]; then
    mirror_args=(-b "$ATHENA_QT_MIRROR")
  fi
  mkdir -p "$deps_dir/qt-archives"
  "$python_bin" -m aqt install-qt linux desktop "$qt_version" linux_gcc_64 \
    "${mirror_args[@]}" \
    --timeout 30 \
    -d "$deps_dir/qt-archives" \
    -k \
    -m qt5compat qtimageformats \
    --archives icu qtbase qtsvg qtwayland \
    -O "$qt_root"
  if [ ! -f "$qt_prefix/lib/libQt6Svg.so" ]; then
    echo "Official Qt $qt_version install is missing libQt6Svg.so" >&2
    exit 1
  fi
  if ! compgen -G "$qt_prefix/plugins/platforms/libqwayland*.so" >/dev/null; then
    echo "Official Qt $qt_version install is missing Wayland platform plugins" >&2
    exit 1
  fi
}

ensure_tcc () {
  if [ -f "$prefix/include/libtcc.h" ] &&
     { [ -f "$prefix/lib64/libtcc.a" ] || [ -f "$prefix/lib/libtcc.a" ]; }; then
    return
  fi

  local tcc_src="$src_dir/tinycc"
  if [ ! -d "$tcc_src/.git" ]; then
    git_clone_retry "$tcc_src" --depth 1 \
      "${TCC_REPO:-https://github.com/TinyCC/tinycc.git}"
  fi

  pushd "$tcc_src" >/dev/null
  CC=gcc ./configure --prefix="$prefix" --libdir="$prefix/lib64"
  make -j"$jobs"
  make install
  popd >/dev/null
}

ensure_resvg () {
  if [ -f "$prefix/lib64/libresvg.so.0.47.0" ] &&
     [ -f "$prefix/include/ResvgQt.h" ]; then
    return
  fi

  rustup toolchain install "$rust_toolchain" --profile minimal >/dev/null

  local resvg_src="$src_dir/resvg"
  if [ ! -d "$resvg_src/.git" ]; then
    git_clone_retry "$resvg_src" --depth 1 --branch v0.47.0 \
      "${RESVG_REPO:-https://github.com/linebender/resvg.git}"
  fi

  cargo +"$rust_toolchain" build \
    --manifest-path "$resvg_src/Cargo.toml" \
    --package resvg-capi \
    --release

  install -Dm755 "$CARGO_TARGET_DIR/release/libresvg.so" \
    "$prefix/lib64/libresvg.so.0.47.0"
  patchelf --set-soname libresvg.so.0 "$prefix/lib64/libresvg.so.0.47.0"
  ln -sfn libresvg.so.0.47.0 "$prefix/lib64/libresvg.so.0"
  ln -sfn libresvg.so.0 "$prefix/lib64/libresvg.so"
  install -Dm644 "$resvg_src/crates/c-api/resvg.h" "$prefix/include/resvg.h"
  install -Dm644 "$resvg_src/crates/c-api/ResvgQt.h" "$prefix/include/ResvgQt.h"
}

ensure_llama () {
  local llama_src="$src_dir/llama.cpp"
  local llama_build="$deps_dir/build/llama.cpp"
  local llama_ref="${LLAMA_CPP_REF:-66c4f9ded01b29d9120255be1ed8d5835bcbb51d}"

  if [ ! -d "$llama_src/.git" ]; then
    git_clone_retry "$llama_src" \
      "${LLAMA_CPP_REPO:-https://github.com/ggml-org/llama.cpp.git}"
  fi

  pushd "$llama_src" >/dev/null
  if ! git cat-file -e "$llama_ref^{commit}" 2>/dev/null; then
    git_retry fetch --tags origin >/dev/null
  fi
  git checkout "$llama_ref"
  popd >/dev/null

  if [ -f "$llama_build/CMakeCache.txt" ] &&
     { ! grep -q "CMAKE_INSTALL_PREFIX:PATH=$prefix" "$llama_build/CMakeCache.txt" ||
       ! grep -q 'GGML_NATIVE:BOOL=OFF' "$llama_build/CMakeCache.txt" ||
       ! grep -q 'LLAMA_BUILD_TESTS:BOOL=OFF' "$llama_build/CMakeCache.txt"; }; then
    rm -rf "$llama_build"
  fi

  cmake -S "$llama_src" -B "$llama_build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_C_COMPILER="$cc_bin" \
    -DCMAKE_CXX_COMPILER="$cxx_bin" \
    -DCMAKE_C_FLAGS="$compiler_base_flags" \
    -DCMAKE_CXX_FLAGS="$compiler_base_flags" \
    -DBUILD_SHARED_LIBS=ON \
    -DGGML_NATIVE=OFF \
    -DLLAMA_CURL=OFF \
    -DLLAMA_BUILD_COMMON=ON \
    -DLLAMA_BUILD_EXAMPLES=OFF \
    -DLLAMA_BUILD_TESTS=OFF \
    -DLLAMA_BUILD_TOOLS=OFF
  cmake --build "$llama_build" -j"$jobs"
  cmake --install "$llama_build"

  if [ -f "$llama_build/llama.pc" ]; then
    install -Dm644 "$llama_build/llama.pc" "$prefix/lib64/pkgconfig/llama.pc"
    sed -i "s|^prefix=.*|prefix=$prefix|" "$prefix/lib64/pkgconfig/llama.pc"
    sed -i "s|^libdir=.*|libdir=\${prefix}/lib64|" "$prefix/lib64/pkgconfig/llama.pc"
    sed -i "s|^includedir=.*|includedir=\${prefix}/include|" "$prefix/lib64/pkgconfig/llama.pc"
  fi
}

ads_source_complete () {
  local dir="$1"
  [ -f "$dir/CMakeLists.txt" ] &&
    [ -f "$dir/src/CMakeLists.txt" ] &&
    [ -f "$dir/src/DockManager.cpp" ]
}

ensure_ads () {
  local ads_src="$src_dir/Qt-Advanced-Docking-System"

  if ! ads_source_complete "$ads_src"; then
    rm -rf "$ads_src"

    local cached_ads
    for cached_ads in \
      "$container_build_dir/build-dev/_deps/ads-src" \
      "$container_build_dir/build-rel/_deps/ads-src" \
      "$repo_root/build_qt6/_deps/ads-src"; do
      if ads_source_complete "$cached_ads"; then
        echo "Using cached ADS source: $cached_ads"
        rm -rf "$ads_patched_src"
        mkdir -p "$(dirname "$ads_patched_src")"
        rsync -a --delete --exclude '.git' "$cached_ads/" "$ads_patched_src/"
        python3 "$repo_root/patch_ads.py" "$ads_patched_src"
        return
      fi
    done

    if ! git_clone_retry "$ads_src" --depth 1 --branch 4.3.1 \
      "${ADS_REPO:-https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git}"; then
      return 1
    fi
  fi

  rm -rf "$ads_patched_src"
  mkdir -p "$(dirname "$ads_patched_src")"
  rsync -a --delete --exclude '.git' "$ads_src/" "$ads_patched_src/"
  python3 "$repo_root/patch_ads.py" "$ads_patched_src"
}

rapidfuzz_source_complete () {
  local dir="$1"
  [ -f "$dir/CMakeLists.txt" ] &&
    [ -f "$dir/rapidfuzz/fuzz.hpp" ]
}

ensure_rapidfuzz () {
  if rapidfuzz_source_complete "$rapidfuzz_src"; then
    return
  fi

  local cached_rapidfuzz
  for cached_rapidfuzz in \
    "$container_build_dir/build-dev/_deps/rapidfuzz-src" \
    "$container_build_dir/build-rel/_deps/rapidfuzz-src" \
    "$repo_root/build_qt6/_deps/rapidfuzz-src" \
    "$repo_root/build_rel/_deps/rapidfuzz-src"; do
    if rapidfuzz_source_complete "$cached_rapidfuzz"; then
      echo "Using cached RapidFuzz source: $cached_rapidfuzz"
      mkdir -p "$rapidfuzz_src"
      rsync -a --delete --exclude '.git' \
        "$cached_rapidfuzz/" "$rapidfuzz_src/"
      return
    fi
  done

  git_clone_retry "$rapidfuzz_src" --depth 1 --branch v3.3.3 \
    "${RAPIDFUZZ_REPO:-https://github.com/rapidfuzz/rapidfuzz-cpp.git}"
}

ensure_boost_headers () {
  if [ -f "$boost_src/boost/json.hpp" ] &&
     [ -f "$boost_src/boost/graph/fruchterman_reingold.hpp" ]; then
    return
  fi

  local download_dir="$deps_dir/downloads"
  local archive="$download_dir/boost_$boost_version_underscore.tar.bz2"
  mkdir -p "$download_dir"
  if [ ! -f "$archive" ]; then
    curl -fL --retry 3 --retry-delay 5 \
      "https://archives.boost.io/release/$boost_version/source/boost_$boost_version_underscore.tar.bz2" \
      -o "$archive"
  fi

  rm -rf "$boost_src"
  tar -xf "$archive" -C "$src_dir"
  if [ ! -f "$boost_src/boost/json.hpp" ]; then
    echo "Boost $boost_version archive does not contain Boost.JSON." >&2
    exit 1
  fi
}

copy_runtime_tree () {
  local build_dir="$1"
  local out_dir="$2"
  local runtime_id="athena-guile-3.0.10-native"

  python3 "$repo_root/tools/release/runtime_policy.py" copy \
    "$repo_root/ATHENA" "$out_dir"
  mkdir -p "$out_dir/bin" "$out_dir/lib"

  rm -rf "$out_dir/lib/athena-scheme"
  mkdir -p "$out_dir/lib/athena-scheme"
  cp -a "$build_dir/athena-scheme/$runtime_id" \
    "$out_dir/lib/athena-scheme/$runtime_id"

  install -Dm755 "$build_dir/src/ATHENA.bin" "$out_dir/bin/ATHENA.bin"
  python3 "$repo_root/tools/release/copy-private-guile-runtime.py" \
    "$build_dir/athena-guile-runtime" "$out_dir" "$runtime_id"
  install -Dm755 "$build_dir/src/athena-codex-bridge" \
    "$out_dir/bin/athena-codex-bridge"
  install -Dm755 \
    "$build_dir/materials-engine-cargo/release/athena-materials-engine" \
    "$out_dir/bin/athena-materials-engine"
  install -Dm755 \
    "$build_dir/tools/athena-transmitter/athena-transmitter" \
    "$out_dir/bin/athena-transmitter"
  install -Dm755 \
    "$build_dir/tools/athena-web-server/athena-web-server" \
    "$out_dir/bin/athena-web-server"
  mkdir -p "$out_dir/share/ATHENA/web"
  cp -a "$repo_root/tools/athena-web-server/web/." \
    "$out_dir/share/ATHENA/web/"
  if [ -x "$build_dir/src/codex" ]; then
    install -Dm755 "$build_dir/src/codex" "$out_dir/bin/codex"
  fi
  if [ -f "$build_dir/src/codex-LICENSE" ]; then
    install -Dm644 "$build_dir/src/codex-LICENSE" \
      "$out_dir/licenses/codex/LICENSE"
  fi
  if compgen -G "$build_dir/x64/lib/libqt6advanceddocking*.so*" >/dev/null; then
    cp -a "$build_dir"/x64/lib/libqt6advanceddocking*.so* "$out_dir/lib/"
  fi
  if compgen -G "$prefix/lib64/libresvg.so*" >/dev/null; then
    cp -a "$prefix"/lib64/libresvg.so* "$out_dir/lib/"
  fi
  if compgen -G "$prefix/lib64/libllama.so*" >/dev/null; then
    cp -a "$prefix"/lib64/libllama.so* "$out_dir/lib/"
  fi
  if compgen -G "$prefix/lib64/libggml*.so*" >/dev/null; then
    cp -a "$prefix"/lib64/libggml*.so* "$out_dir/lib/"
  fi
  python3 "$repo_root/tools/release/runtime_policy.py" verify "$out_dir" \
    --require-linux-services
}

build_athena_flavor () {
  local label="$1"
  local cmake_type="$2"
  local build_dir="$container_build_dir/build-$label"
  local out_dir="$container_build_dir/ATHENA-$label"
  local common_lib="$deps_dir/build/llama.cpp/common/libcommon.a"

  if [ -f "$build_dir/CMakeCache.txt" ] &&
     ! grep -Fxq 'ATHENA_GUILE_RUNTIME_ID:INTERNAL=athena-guile-3.0.10-native' \
       "$build_dir/CMakeCache.txt"; then
    rm -rf "$build_dir"
  fi

  cmake -S "$repo_root" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE="$cmake_type" \
    -DCMAKE_C_COMPILER="$cc_bin" \
    -DCMAKE_CXX_COMPILER="$cxx_bin" \
    -DCMAKE_PREFIX_PATH="$qt_prefix;$prefix" \
    -DQt6_DIR="$qt_prefix/lib/cmake/Qt6" \
    -DCMAKE_C_FLAGS="$compiler_base_flags" \
    -DCMAKE_CXX_FLAGS="$compiler_base_flags" \
    -DATHENA_GUI=Qt6 \
    -DATHENA_CPU_TARGET=x86-64-v3 \
    -DATHENA_INTEL_NATIVE_OPTIMIZATION=OFF \
    -DADS_VERSION=4.3.1 \
    -DFETCHCONTENT_SOURCE_DIR_ADS="$ads_patched_src" \
    -DFETCHCONTENT_SOURCE_DIR_RAPIDFUZZ="$rapidfuzz_src" \
    -DBOOST_ROOT="$boost_src" \
    -DBoost_INCLUDE_DIR="$boost_src" \
    -DBoost_NO_SYSTEM_PATHS=ON \
    -DUSE_KF6_KIO_FILE_DIALOGS=OFF \
    -DPython3_EXECUTABLE="$python_bin" \
    -DLLAMA_CPP_SOURCE_DIR="$src_dir/llama.cpp" \
    -DLLAMA_COMMON_LIBRARY="$common_lib" \
    -DLLAMA_CPP_LIBRARY="$prefix/lib64/libllama.so" \
    -DLLAMA_GGML_LIBRARY="$prefix/lib64/libggml.so" \
    -DLLAMA_GGML_BASE_LIBRARY="$prefix/lib64/libggml-base.so" \
    -DLLAMA_GGML_CPU_LIBRARY="$prefix/lib64/libggml-cpu.so" \
    -DRESVGQT_INCLUDE_DIR="$prefix/include" \
    -DRESVG_LIBRARY="$prefix/lib64/libresvg.so" \
    -DTCC_INCLUDE_DIR="$prefix/include" \
    -DTCC_LIBRARY="$prefix/lib64/libtcc.a"

  cmake --build "$build_dir" -j"$jobs"

  if [ ! -f "$build_dir/athena-guile-runtime/lib/libathena-guile.so.1" ]; then
    echo "ATHENA $label build did not produce its private Guile runtime." >&2
    exit 1
  fi

  copy_runtime_tree "$build_dir" "$out_dir"

  "$repo_root/tools/container-build/package_appimage.py" \
    "$out_dir" \
    "$container_build_dir/ATHENA-$label.AppImage" \
    "$container_build_dir/appimage/ATHENA-$label.AppDir" \
    "$tools_dir/appimagetool-x86_64.AppImage"
  sha256sum "$container_build_dir/ATHENA-$label.AppImage" \
    > "$container_build_dir/ATHENA-$label.AppImage.sha256"

  "$repo_root/tools/container-build/package_native.py" \
    "$container_build_dir/appimage/ATHENA-$label.AppDir" \
    "$container_build_dir/packages" \
    "$label" \
    "$athena_version"

  if [ "$label" = rel ]; then
    source_epoch="$(git -C "$repo_root" log -1 --format=%ct)"
    python3 "$repo_root/tools/release/package_linux_service.py" \
      --kind transmitter \
      --binary "$build_dir/tools/athena-transmitter/athena-transmitter" \
      --repo-root "$repo_root" \
      --version "$athena_version" \
      --source-epoch "$source_epoch" \
      --output "$container_build_dir/ATHENA-Transmitter-$athena_version-linux-x86_64.tar.gz"
    python3 "$repo_root/tools/release/package_linux_service.py" \
      --kind web-server \
      --binary "$build_dir/tools/athena-web-server/athena-web-server" \
      --repo-root "$repo_root" \
      --version "$athena_version" \
      --source-epoch "$source_epoch" \
      --output "$container_build_dir/ATHENA-Web-Server-$athena_version-linux-x86_64.tar.gz"
    sha256sum \
      "$container_build_dir/ATHENA-Transmitter-$athena_version-linux-x86_64.tar.gz" \
      > "$container_build_dir/ATHENA-Transmitter-$athena_version-linux-x86_64.tar.gz.sha256"
    sha256sum \
      "$container_build_dir/ATHENA-Web-Server-$athena_version-linux-x86_64.tar.gz" \
      > "$container_build_dir/ATHENA-Web-Server-$athena_version-linux-x86_64.tar.gz.sha256"
  fi
}

download_appimagetool () {
  local tool="$tools_dir/appimagetool-x86_64.AppImage"
  if [ -x "$tool" ]; then
    return
  fi
  curl -L \
    -o "$tool" \
    "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"
  chmod +x "$tool"
}

ensure_qt
ensure_tcc
ensure_resvg
ensure_llama
ensure_ads
ensure_rapidfuzz
ensure_boost_headers
download_appimagetool

build_flavors="${ATHENA_BUILD_FLAVORS:-dev rel}"
for flavor in $build_flavors; do
  case "$flavor" in
    dev) build_athena_flavor dev Debug ;;
    rel) build_athena_flavor rel RelWithDebInfo ;;
    *)
      echo "Unknown ATHENA build flavor: $flavor" >&2
      exit 2
      ;;
  esac
done

find "$container_build_dir" -maxdepth 1 \
  \( -name 'ATHENA-dev*' -o -name 'ATHENA-rel*' \) -print | sort
find "$container_build_dir/packages" -maxdepth 1 -type f -print | sort
