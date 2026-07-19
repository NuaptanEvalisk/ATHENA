#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

prefix="${ATHENA_WIN64_PREFIX:-${repo_root}/build_windows/prefix}"
build_dir="${ATHENA_WIN64_BUILD_DIR:-${repo_root}/build_windows}"
release_dir="${ATHENA_WIN64_RELEASE_DIR:-${repo_root}/release/windows/ATHENA}"
jobs="${ATHENA_WIN64_JOBS:-$(nproc)}"

usage() {
  cat <<EOF
usage: $0 [--prefix PATH] [--build-dir PATH] [--release-dir PATH] [-j JOBS]

Configure and build the 64-bit Windows ATHENA target, then refresh the
redistributable tree under release/windows/ATHENA.

Environment defaults:
  ATHENA_WIN64_PREFIX       ${repo_root}/build_windows/prefix
  ATHENA_WIN64_BUILD_DIR    ${repo_root}/build_windows
  ATHENA_WIN64_RELEASE_DIR  ${repo_root}/release/windows/ATHENA
  ATHENA_WIN64_JOBS         nproc
EOF
}

while (($#)); do
  case "$1" in
    --prefix)
      prefix="$2"
      shift 2
      ;;
    --build-dir)
      build_dir="$2"
      shift 2
      ;;
    --release-dir)
      release_dir="$2"
      shift 2
      ;;
    -j|--jobs)
      jobs="$2"
      shift 2
      ;;
    -j*)
      jobs="${1#-j}"
      shift
      ;;
    --jobs=*)
      jobs="${1#--jobs=}"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

prefix="$(mkdir -p "${prefix}" && cd -- "${prefix}" && pwd)"
build_dir="$(mkdir -p "${build_dir}" && cd -- "${build_dir}" && pwd)"
release_parent="$(mkdir -p "$(dirname -- "${release_dir}")" && cd -- "$(dirname -- "${release_dir}")" && pwd)"
release_dir="${release_parent}/$(basename -- "${release_dir}")"

copy_if_present() {
  local src="$1"
  local dst_dir="$2"
  if [[ -f "${src}" ]]; then
    mkdir -p "${dst_dir}"
    cp -f "${src}" "${dst_dir}/"
  fi
}

copy_qt_plugins() {
  local plugin_root="${prefix}/share/qt6/plugins"
  local target_root="${release_dir}/bin"

  copy_if_present "${plugin_root}/platforms/qwindows.dll" "${target_root}/platforms"
  copy_if_present "${plugin_root}/platforms/qminimal.dll" "${target_root}/platforms"

  copy_if_present "${plugin_root}/imageformats/qgif.dll" "${target_root}/imageformats"
  copy_if_present "${plugin_root}/imageformats/qico.dll" "${target_root}/imageformats"
  copy_if_present "${plugin_root}/imageformats/qjpeg.dll" "${target_root}/imageformats"
  copy_if_present "${plugin_root}/imageformats/qsvg.dll" "${target_root}/imageformats"

  copy_if_present "${plugin_root}/iconengines/qsvgicon.dll" "${target_root}/iconengines"
  copy_if_present "${plugin_root}/styles/qmodernwindowsstyle.dll" "${target_root}/styles"

  copy_if_present "${plugin_root}/tls/qcertonlybackend.dll" "${target_root}/tls"
  copy_if_present "${plugin_root}/tls/qschannelbackend.dll" "${target_root}/tls"
}

echo "==> Checking Windows target prefix"
"${script_dir}/check-win64-prefix.sh" "${prefix}"

echo "==> Configuring ATHENA for Win64"
"${script_dir}/configure-athena-win64.sh" "${prefix}" "${build_dir}"

echo "==> Building ATHENA.exe"
cmake --build "${build_dir}" -j"${jobs}"

if [[ ! -f "${build_dir}/src/ATHENA.exe" ]]; then
  echo "missing build output: ${build_dir}/src/ATHENA.exe" >&2
  exit 1
fi
if [[ ! -f "${build_dir}/x64/bin/libqt6advanceddocking.dll" ]]; then
  echo "missing ADS dll: ${build_dir}/x64/bin/libqt6advanceddocking.dll" >&2
  exit 1
fi

echo "==> Refreshing ${release_dir}"
rm -rf "${release_dir}"
mkdir -p "${release_dir}" "${release_dir}/bin"

rsync -a --delete \
  --exclude='tools/formula-cleaner/*.gguf' \
  --exclude='*.safetensors' \
  --exclude='.venv/' \
  --exclude='.uv-cache/' \
  --exclude='/bin/ATHENA.bin' \
  --exclude='/bin/ATHENA.bin.before-*' \
  --exclude='/lib/*.so' \
  --exclude='/lib/*.so.*' \
  "${repo_root}/ATHENA/" "${release_dir}/"

cp -f "${build_dir}/src/ATHENA.exe" "${release_dir}/bin/ATHENA.exe"
copy_if_present "${build_dir}/src/athena-codex-bridge.exe" "${release_dir}/bin"
cp -f "${build_dir}/x64/bin/libqt6advanceddocking.dll" "${release_dir}/bin/"
cp -f "${prefix}/bin/"*.dll "${release_dir}/bin/"

# Keep the packaged runtime narrow enough to avoid accidental ABI conflicts
# from stale DLLs in the reusable prefix, especially older ICU libraries.
rm -f "${release_dir}/bin/Qt6Concurrent.dll" \
      "${release_dir}/bin/Qt6DBus.dll" \
      "${release_dir}/bin/Qt6OpenGL.dll" \
      "${release_dir}/bin/Qt6OpenGLWidgets.dll" \
      "${release_dir}/bin/Qt6Sql.dll" \
      "${release_dir}/bin/Qt6Test.dll" \
      "${release_dir}/bin/Qt6Xml.dll" \
      "${release_dir}/bin/Qt6SvgWidgets.dll" \
      "${release_dir}/bin/libicu"*77.dll

copy_qt_plugins

echo "==> Windows release ready"
echo "    ${release_dir}"
echo "    ${release_dir}/StartATHENA.cmd"
