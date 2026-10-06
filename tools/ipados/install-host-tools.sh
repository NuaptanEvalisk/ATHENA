#!/usr/bin/env bash

set -euo pipefail

# SSH may forward Linux locale names that macOS does not provide.  Host-tool
# builds do not need a localized environment, so keep their text processing and
# configure probes deterministic.
export LC_ALL=C
export LANG=C

prefix="${ATHENA_IPADOS_HOST_PREFIX:-/Users/felix/Developer/athena-deps/host}"
jobs="${ATHENA_IPADOS_HOST_JOBS:-16}"

cmake_version=4.4.4
cmake_sha256=bd24c30d80a7744ae84b845ff080cc8453b06c622ef01066564108e9cefc44cf
ninja_version=1.13.1
rust_version=1.99.0
rustup_sha256=259e2b84274434085163fe8d556510571772cda2aa6d87ca6aa664f57bc644e3
m4_version=1.4.21
autoconf_version=2.73
automake_version=1.18.1
libtool_version=2.5.4
pkgconf_version=3.0.7

src_dir="${prefix}/src"
build_dir="${prefix}/build"
bin_dir="${prefix}/bin"
manifest="${prefix}/toolchain-manifest.txt"

mkdir -p "${src_dir}" "${build_dir}" "${bin_dir}"

die () {
  printf 'install-host-tools: %s\n' "$*" >&2
  exit 1
}

[[ "$(uname -s)" == Darwin ]] || die "this script must run on macOS"
[[ "$(uname -m)" == x86_64 ]] || die "expected the current x86_64 macOS VM"
command -v xcrun >/dev/null 2>&1 || die "Xcode command-line tools are unavailable"
command -v curl >/dev/null 2>&1 || die "curl is unavailable"
command -v shasum >/dev/null 2>&1 || die "shasum is unavailable"
command -v make >/dev/null 2>&1 || die "make is unavailable"

download () {
  url=$1
  output=$2
  if [[ ! -f "${output}" ]]; then
    printf 'Downloading %s\n' "${url}"
    tmp="${output}.tmp.$$"
    rm -f "${tmp}"
    curl --fail --location --retry 3 --connect-timeout 15 \
      --output "${tmp}" "${url}"
    mv "${tmp}" "${output}"
  fi
}

verify_sha256 () {
  file=$1
  expected=$2
  actual="$(shasum -a 256 "${file}" | awk '{print $1}')"
  [[ "${actual}" == "${expected}" ]] || \
    die "SHA-256 mismatch for ${file}: expected ${expected}, got ${actual}"
}

extract_fresh () {
  archive=$1
  destination=$2
  rm -rf "${destination}"
  mkdir -p "${destination}"
  tar -xf "${archive}" -C "${destination}" --strip-components=1
}

write_env () {
  cat >"${prefix}/athena-host-env.sh" <<EOF
export ATHENA_IPADOS_HOST_PREFIX="${prefix}"
export CARGO_HOME="${prefix}/cargo"
export RUSTUP_HOME="${prefix}/rustup"
export DEVELOPER_DIR="/Applications/Xcode.app/Contents/Developer"
export PATH="${prefix}/cargo/bin:${prefix}/bin:/usr/bin:/bin:/usr/sbin:/sbin"
export PKG_CONFIG="${prefix}/bin/pkg-config"
EOF
}

install_cmake () {
  archive="${src_dir}/cmake-${cmake_version}.tar.gz"
  source="${src_dir}/cmake-${cmake_version}"
  build="${build_dir}/cmake-${cmake_version}"
  root="${prefix}/cmake-${cmake_version}"
  download \
    "https://cmake.org/files/v4.4/cmake-${cmake_version}.tar.gz" \
    "${archive}"
  verify_sha256 "${archive}" "${cmake_sha256}"

  if [[ ! -x "${root}/bin/cmake" ]]; then
    extract_fresh "${archive}" "${source}"
    rm -rf "${build}" "${root}"
    mkdir -p "${build}"
    (
      cd "${build}"
      "${source}/bootstrap" --prefix="${root}" --parallel="${jobs}" -- \
        -DBUILD_TESTING=OFF
      make -j"${jobs}"
      make install
    )
  fi

  for tool in cmake cpack ctest; do
    ln -sfn "${root}/bin/${tool}" "${bin_dir}/${tool}"
  done
  "${bin_dir}/cmake" --version | head -1
}

install_ninja () {
  archive="${src_dir}/ninja-${ninja_version}.tar.gz"
  source="${src_dir}/ninja-${ninja_version}"
  build="${build_dir}/ninja-${ninja_version}"
  download \
    "https://github.com/ninja-build/ninja/archive/refs/tags/v${ninja_version}.tar.gz" \
    "${archive}"

  if [[ ! -x "${bin_dir}/ninja" ]] || \
     [[ "$("${bin_dir}/ninja" --version 2>/dev/null || true)" != "${ninja_version}" ]]; then
    extract_fresh "${archive}" "${source}"
    rm -rf "${build}"
    "${bin_dir}/cmake" -S "${source}" -B "${build}" -G 'Unix Makefiles' \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTING=OFF
    "${bin_dir}/cmake" --build "${build}" --parallel "${jobs}" --target ninja
    install -m 0755 "${build}/ninja" "${bin_dir}/ninja"
  fi
  "${bin_dir}/ninja" --version
}

install_rust () {
  rustup_init="${src_dir}/rustup-init-x86_64-apple-darwin"
  download \
    "https://static.rust-lang.org/rustup/dist/x86_64-apple-darwin/rustup-init" \
    "${rustup_init}"
  verify_sha256 "${rustup_init}" "${rustup_sha256}"
  chmod +x "${rustup_init}"

  export CARGO_HOME="${prefix}/cargo"
  export RUSTUP_HOME="${prefix}/rustup"
  "${rustup_init}" -y --no-modify-path --profile minimal \
    --default-toolchain "${rust_version}" \
    --target aarch64-apple-ios
  "${CARGO_HOME}/bin/rustc" --version
  "${CARGO_HOME}/bin/cargo" --version
  "${CARGO_HOME}/bin/rustup" target list --installed --toolchain "${rust_version}"
}

build_autotool_package () {
  name=$1
  version=$2
  url=$3
  archive="${src_dir}/${name}-${version}.tar.gz"
  source="${src_dir}/${name}-${version}"
  build="${build_dir}/${name}-${version}"

  download "${url}" "${archive}"
  extract_fresh "${archive}" "${source}"
  rm -rf "${build}"
  mkdir -p "${build}"
  (
    cd "${build}"
    PATH="${bin_dir}:/usr/bin:/bin:/usr/sbin:/sbin" \
      "${source}/configure" --prefix="${prefix}"
    PATH="${bin_dir}:/usr/bin:/bin:/usr/sbin:/sbin" \
      make -j"${jobs}"
    PATH="${bin_dir}:/usr/bin:/bin:/usr/sbin:/sbin" \
      make install
  )
}

install_autotools () {
  if [[ ! -x "${bin_dir}/m4" ]] || \
     ! "${bin_dir}/m4" --version 2>/dev/null | head -1 | grep -F "${m4_version}" >/dev/null; then
    build_autotool_package m4 "${m4_version}" \
      "https://ftp.gnu.org/gnu/m4/m4-${m4_version}.tar.gz"
  fi

  if [[ ! -x "${bin_dir}/autoconf" ]] || \
     ! "${bin_dir}/autoconf" --version 2>/dev/null | head -1 | grep -F "${autoconf_version}" >/dev/null; then
    build_autotool_package autoconf "${autoconf_version}" \
      "https://ftp.gnu.org/gnu/autoconf/autoconf-${autoconf_version}.tar.gz"
  fi

  if [[ ! -x "${bin_dir}/automake" ]] || \
     ! "${bin_dir}/automake" --version 2>/dev/null | head -1 | grep -F "${automake_version}" >/dev/null; then
    build_autotool_package automake "${automake_version}" \
      "https://ftp.gnu.org/gnu/automake/automake-${automake_version}.tar.gz"
  fi

  if [[ ! -x "${bin_dir}/libtool" ]] || \
     ! "${bin_dir}/libtool" --version 2>/dev/null | head -1 | grep -F "${libtool_version}" >/dev/null; then
    build_autotool_package libtool "${libtool_version}" \
      "https://ftp.gnu.org/gnu/libtool/libtool-${libtool_version}.tar.gz"
  fi
}

install_pkgconf () {
  if [[ ! -x "${bin_dir}/pkgconf" ]] || \
     ! "${bin_dir}/pkgconf" --version 2>/dev/null | grep -Fx "${pkgconf_version}" >/dev/null; then
    build_autotool_package pkgconf "${pkgconf_version}" \
      "https://distfiles.dereferenced.org/pkgconf/pkgconf-${pkgconf_version}.tar.gz"
  fi
  ln -sfn "${bin_dir}/pkgconf" "${bin_dir}/pkg-config"
}

write_manifest () {
  {
    printf 'generated_utc=%s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    printf 'macos=%s\n' "$(sw_vers -productVersion)"
    printf 'xcode=%s\n' "$(xcodebuild -version | tr '\n' ' ' | sed 's/ $//')"
    printf 'sdk=%s\n' "$(xcrun --sdk iphoneos --show-sdk-version)"
    printf 'cmake=%s\n' "$("${bin_dir}/cmake" --version | head -1)"
    printf 'ninja=%s\n' "$("${bin_dir}/ninja" --version)"
    printf 'rustc=%s\n' "$("${prefix}/cargo/bin/rustc" --version)"
    printf 'cargo=%s\n' "$("${prefix}/cargo/bin/cargo" --version)"
    printf 'm4=%s\n' "$("${bin_dir}/m4" --version | head -1)"
    printf 'autoconf=%s\n' "$("${bin_dir}/autoconf" --version | head -1)"
    printf 'automake=%s\n' "$("${bin_dir}/automake" --version | head -1)"
    printf 'libtool=%s\n' "$("${bin_dir}/libtool" --version | head -1)"
    printf 'pkgconf=%s\n' "$("${bin_dir}/pkgconf" --version)"
    printf '\narchives_sha256:\n'
    find "${src_dir}" -maxdepth 1 -type f \( -name '*.tar.gz' -o -name 'rustup-init-*' \) \
      -print0 | sort -z | while IFS= read -r -d '' file; do
        shasum -a 256 "${file}"
      done
  } >"${manifest}"
}

install_cmake
install_ninja
install_rust
install_autotools
install_pkgconf
write_env
write_manifest

printf '\nHost toolchain ready. Source this for subsequent SSH builds:\n  . %s\n' \
  "${prefix}/athena-host-env.sh"
printf 'Manifest: %s\n' "${manifest}"
