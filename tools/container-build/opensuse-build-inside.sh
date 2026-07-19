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
athena_version="$(sed -n 's/.*set *(ATHENA_APP_VERSION *"\([^"]*\)".*/\1/p' "$repo_root/CMakeLists.txt" | head -n1)"
qt_root="$deps_dir/qt"
qt_prefix="$qt_root/$qt_version/gcc_64"
ads_patched_src="$deps_dir/ads-patched/qt6"
guile18_source="${ATHENA_GUILE18_SOURCE:-$HOME/data/Software/TeXmacs/obs/guile-1.8.8}"
guile18_prefix="$deps_dir/guile18"

unset HTTP_PROXY HTTPS_PROXY ALL_PROXY http_proxy https_proxy all_proxy

mkdir -p "$prefix" "$src_dir" "$tools_dir" "$container_build_dir/logs"

export RUSTUP_HOME="$container_build_dir/rustup"
export CARGO_HOME="$container_build_dir/cargo"
export CARGO_TARGET_DIR="$container_build_dir/cargo-target"

prepend_build_paths () {
  export PATH="$guile18_prefix/bin:$HOME/.local/bin:$qt_prefix/bin:$prefix/bin:/opt/intel/oneapi/compiler/latest/bin:$PATH"
  export PKG_CONFIG_PATH="$guile18_prefix/lib64/pkgconfig:$guile18_prefix/lib/pkgconfig:$prefix/lib64/pkgconfig:$prefix/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
  export LD_LIBRARY_PATH="$qt_prefix/lib:$prefix/lib64:$prefix/lib:$guile18_prefix/lib64:$guile18_prefix/lib:${LD_LIBRARY_PATH:-}"
  export LIBRARY_PATH="$guile18_prefix/lib64:$guile18_prefix/lib:${LIBRARY_PATH:-}"
  export CPATH="$guile18_prefix/include:${CPATH:-}"
  export CMAKE_PREFIX_PATH="$qt_prefix:$prefix:$guile18_prefix:${CMAKE_PREFIX_PATH:-}"
  export GUILE_LOAD_PATH="$guile18_prefix/share/guile/site:$guile18_prefix/share/guile/1.8:$guile18_prefix/share/guile${GUILE_LOAD_PATH:+:$GUILE_LOAD_PATH}"
}

require_guile18 () {
  if [ ! -x "$guile18_prefix/bin/guile" ] ||
     [ ! -f "$guile18_prefix/lib64/pkgconfig/guile-1.8.pc" ]; then
    echo "Guile 1.8 prefix not available in container: $guile18_prefix" >&2
    exit 1
  fi
  local gv
  gv="$("$guile18_prefix/bin/guile" -c '(display (version))')"
  case "$gv" in
    1.8.*) ;;
    *)
      echo "Expected Guile 1.8, got $gv from $guile18_prefix" >&2
      exit 1
      ;;
  esac
}

ensure_guile18 () {
  if [ -x "$guile18_prefix/bin/guile" ] &&
     [ -f "$guile18_prefix/lib64/pkgconfig/guile-1.8.pc" ]; then
    local existing_gv
    existing_gv="$("$guile18_prefix/bin/guile" -c '(display (version))')"
    case "$existing_gv" in
      1.8.*) return ;;
    esac
  fi

  if [ ! -x "$guile18_source/configure" ]; then
    echo "Guile 1.8 source tree not available in container: $guile18_source" >&2
    exit 1
  fi

  local guile_build_src="$src_dir/guile-1.8.8"
  rm -rf "$guile18_prefix" "$guile_build_src"
  rsync -a --delete \
    --exclude '.git' \
    --exclude '.deps/' \
    --exclude '.libs/' \
    --exclude '*.a' \
    --exclude '*.la' \
    --exclude '*.lo' \
    --exclude '*.o' \
    --exclude '*.so' \
    --exclude '*.so.*' \
    "$guile18_source/" "$guile_build_src/"

  pushd "$guile_build_src" >/dev/null
  CC="${ATHENA_GUILE18_CC:-gcc}" \
    CXX="${ATHENA_GUILE18_CXX:-g++}" \
    CFLAGS="${ATHENA_GUILE18_CFLAGS:--O2 -g -fPIC -fcommon}" \
    CXXFLAGS="${ATHENA_GUILE18_CXXFLAGS:--O2 -g -fPIC -fcommon}" \
    ./configure \
      --prefix="$guile18_prefix" \
      --libdir="$guile18_prefix/lib64" \
      --disable-error-on-warning \
      --enable-shared \
      --enable-static
  if [ -f libguile/scmconfig.h ]; then
    touch libguile/scmconfig.h
  fi
  make -j1 CPP="${ATHENA_GUILE18_CPP:-gcc -E -P}"
  make install CPP="${ATHENA_GUILE18_CPP:-gcc -E -P}"
  popd >/dev/null

  prepend_build_paths
  require_guile18
}

prepend_build_paths

if [ -r /opt/intel/oneapi/setvars.sh ]; then
  # shellcheck disable=SC1091
  set +u
  source /opt/intel/oneapi/setvars.sh --force >/dev/null
  set -u
fi
prepend_build_paths
ensure_guile18

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
   ! python3.11 -c 'import aqt' >/dev/null 2>&1; then
  python3.11 -m pip install --user uv aqtinstall >/dev/null
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
  python3.11 -m aqt install-qt linux desktop "$qt_version" linux_gcc_64 \
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

  rustup toolchain install 1.87.0 --profile minimal >/dev/null

  local resvg_src="$src_dir/resvg"
  if [ ! -d "$resvg_src/.git" ]; then
    git_clone_retry "$resvg_src" --depth 1 --branch v0.47.0 \
      "${RESVG_REPO:-https://github.com/linebender/resvg.git}"
  fi

  cargo +1.87.0 build \
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
        python3 "$repo_root/patch_ads.py" "$ads_patched_src" 6
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
  python3 "$repo_root/patch_ads.py" "$ads_patched_src" 6
}

copy_runtime_tree () {
  local build_dir="$1"
  local out_dir="$2"

  rm -rf "$out_dir"
  mkdir -p "$out_dir/bin" "$out_dir/lib"
  rsync -a --delete \
    --exclude 'bin/ATHENA.bin' \
    --exclude 'lib/*' \
    --exclude 'tools/formula-cleaner/*.gguf' \
    --exclude 'tools/formula-cleaner/.venv/' \
    --exclude '.venv/' \
    --exclude '.uv-cache/' \
    --exclude '__pycache__/' \
    --exclude '*.safetensors' \
    "$repo_root/ATHENA/" "$out_dir/"

  install -Dm755 "$build_dir/src/ATHENA.bin" "$out_dir/bin/ATHENA.bin"
  install -Dm755 "$build_dir/src/athena-codex-bridge" \
    "$out_dir/bin/athena-codex-bridge"
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
}

build_athena_flavor () {
  local label="$1"
  local cmake_type="$2"
  local build_dir="$container_build_dir/build-$label"
  local out_dir="$container_build_dir/ATHENA-$label"
  local common_lib="$deps_dir/build/llama.cpp/common/libcommon.a"

  if [ -f "$build_dir/CMakeCache.txt" ] &&
     { ! grep -q 'Guile_VERSION:INTERNAL=1\.8\.' "$build_dir/CMakeCache.txt" ||
       ! grep -q "Guile_PREFIX:INTERNAL=$guile18_prefix" "$build_dir/CMakeCache.txt"; }; then
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
    -DCMAKE_EXE_LINKER_FLAGS="-L$guile18_prefix/lib64 -L$guile18_prefix/lib" \
    -DATHENA_GUI=Qt6 \
    -DSCHEME_IMPL=guile-1.8 \
    -DATHENA_INTEL_NATIVE_OPTIMIZATION=OFF \
    -DADS_VERSION=4.3.1 \
    -DFETCHCONTENT_SOURCE_DIR_ADS="$ads_patched_src" \
    -DUSE_KF6_KIO_FILE_DIALOGS=OFF \
    -DPython3_EXECUTABLE=/usr/bin/python3.11 \
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

  if ! grep -q 'Guile_VERSION:INTERNAL=1\.8\.' "$build_dir/CMakeCache.txt" ||
     ! grep -q 'GUILE_C:BOOL=ON' "$build_dir/CMakeCache.txt"; then
    echo "ATHENA $label build did not configure against Guile 1.8." >&2
    grep -E 'SCHEME_IMPL|Guile_VERSION|GUILE_[CD]' "$build_dir/CMakeCache.txt" >&2 || true
    exit 1
  fi

  cmake --build "$build_dir" -j"$jobs"
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
download_appimagetool

build_athena_flavor dev Debug
build_athena_flavor rel RelWithDebInfo

find "$container_build_dir" -maxdepth 1 \
  \( -name 'ATHENA-dev*' -o -name 'ATHENA-rel*' \) -print | sort
find "$container_build_dir/packages" -maxdepth 1 -type f -print | sort
