#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"
release_dir="$repo_root/release"
local_build_dir="$repo_root/build_rel"
container_build_dir="$repo_root/container_build"
windows_build_dir="$repo_root/build_windows"
jobs="$(nproc)"
build_local=1
build_container=1
build_windows=1
run_wine_test=1
dry_run=0
cmake_extra_args=()

usage () {
  cat <<EOF
usage: $0 [options]

Build and package every ATHENA release artifact without publishing a release.
Repeated runs replace only the current version's outputs and reuse build caches.

Options:
  -j, --jobs N             Parallel build jobs (default: nproc)
  --release-dir PATH       Artifact directory (default: release)
  --local-build-dir PATH   Native Linux build tree (default: build_rel)
  --skip-local             Skip native Linux build and tar.gz
  --skip-container         Skip AppImage, DEB, and RPM builds
  --skip-windows           Skip Windows cross-build and ZIP
  --skip-wine-test         Do not smoke-test ATHENA.exe with Wine
  --cmake-arg VALUE        Append one argument to native Linux CMake configure
  --dry-run                Print the build stages without executing them
  -h, --help               Show this help

Environment:
  ATHENA_CONTAINER_IMAGE, ATHENA_CONTAINER_NAME, ATHENA_WIN64_PREFIX, and the
  existing container/Windows build variables are passed through to their
  respective build helpers.
EOF
}

while (($#)); do
  case "$1" in
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
    --release-dir)
      release_dir="$2"
      shift 2
      ;;
    --local-build-dir)
      local_build_dir="$2"
      shift 2
      ;;
    --skip-local)
      build_local=0
      shift
      ;;
    --skip-container)
      build_container=0
      shift
      ;;
    --skip-windows)
      build_windows=0
      shift
      ;;
    --skip-wine-test)
      run_wine_test=0
      shift
      ;;
    --cmake-arg)
      cmake_extra_args+=("$2")
      shift 2
      ;;
    --cmake-arg=*)
      cmake_extra_args+=("${1#--cmake-arg=}")
      shift
      ;;
    --dry-run)
      dry_run=1
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

version="$(
  sed -n 's/.*set *(ATHENA_APP_VERSION *"\([^"]*\)".*/\1/p' \
    "$repo_root/CMakeLists.txt" | head -n1
)"
if [[ -z "$version" ]]; then
  echo "could not read ATHENA_APP_VERSION from CMakeLists.txt" >&2
  exit 1
fi

run () {
  printf '+'
  printf ' %q' "$@"
  printf '\n'
  if [[ "$dry_run" -eq 0 ]]; then
    "$@"
  fi
}

checksum () {
  local artifact="$1"
  if [[ "$dry_run" -eq 1 ]]; then
    echo "+ sha256sum $(printf '%q' "$artifact")"
    return
  fi
  (
    cd -- "$(dirname -- "$artifact")"
    sha256sum "$(basename -- "$artifact")" \
      > "$(basename -- "$artifact").sha256"
  )
}

copy_artifact () {
  local source="$1"
  local destination="$2"
  run mkdir -p "$(dirname -- "$destination")"
  run cp -f "$source" "$destination"
  checksum "$destination"
}

configure_local_build () {
  local args=(
    cmake
    -S "$repo_root"
    -B "$local_build_dir"
    -G Ninja
    -DATHENA_GUI=Qt6
    -DCMAKE_BUILD_TYPE=Release
    -DATHENA_CPU_TARGET=x86-64-v3
  )
  if [[ ! -f "$local_build_dir/CMakeCache.txt" ]]; then
    local icx=/opt/intel/oneapi/compiler/latest/bin/icx
    local icpx=/opt/intel/oneapi/compiler/latest/bin/icpx
    if [[ -x "$icx" && -x "$icpx" ]]; then
      args+=(
        "-DCMAKE_C_COMPILER=$icx"
        "-DCMAKE_CXX_COMPILER=$icpx"
      )
    fi
  fi
  args+=("${cmake_extra_args[@]}")

  run "${args[@]}"
  run python3 "$repo_root/patch_ads.py" "$local_build_dir/_deps/ads-src"
  run cmake --build "$local_build_dir" -j"$jobs"
}

assemble_linux_runtime () {
  local runtime="$release_dir/ATHENA"
  local ads_libraries=()

  run python3 "$script_dir/runtime_policy.py" copy \
    "$repo_root/ATHENA" "$runtime" --keep-source-libraries
  run mkdir -p "$runtime/bin" "$runtime/lib"
  run rm -f "$runtime/lib"/libqt5advanceddocking*.so*
  run rm -f "$runtime/lib"/libqt6advanceddocking*.so*
  run install -m 755 "$local_build_dir/src/ATHENA.bin" \
    "$runtime/bin/ATHENA.bin"
  run python3 "$script_dir/copy-private-guile-runtime.py" \
    "$local_build_dir/athena-guile-runtime" "$runtime"
  run install -m 755 "$local_build_dir/src/athena-codex-bridge" \
    "$runtime/bin/athena-codex-bridge"
  run install -m 755 \
    "$local_build_dir/materials-engine-cargo/release/athena-materials-engine" \
    "$runtime/bin/athena-materials-engine"
  run install -m 755 \
    "$local_build_dir/tools/athena-transmitter/athena-transmitter" \
    "$runtime/bin/athena-transmitter"
  run install -m 755 \
    "$local_build_dir/tools/athena-web-server/athena-web-server" \
    "$runtime/bin/athena-web-server"
  run mkdir -p "$runtime/share/ATHENA/web"
  run cp -a "$repo_root/tools/athena-web-server/web/." \
    "$runtime/share/ATHENA/web/"

  if [[ "$dry_run" -eq 0 ]]; then
    shopt -s nullglob
    ads_libraries=("$local_build_dir"/x64/lib/libqt6advanceddocking*.so*)
    shopt -u nullglob
    if [[ "${#ads_libraries[@]}" -eq 0 ]]; then
      echo "missing Qt6 ADS library in $local_build_dir/x64/lib" >&2
      exit 1
    fi
    cp -a "${ads_libraries[@]}" "$runtime/lib/"
  else
    echo "+ cp -a $local_build_dir/x64/lib/libqt6advanceddocking\\*.so\\* $runtime/lib/"
  fi
  run python3 "$script_dir/runtime_policy.py" verify "$runtime" \
    --require-linux-services
}

archive_linux_runtime () {
  local artifact="$release_dir/ATHENA-$version-linux-x86_64.tar.gz"
  local temporary="$artifact.tmp"
  local source_epoch
  source_epoch="$(git -C "$repo_root" log -1 --format=%ct)"

  if [[ "$dry_run" -eq 1 ]]; then
    echo "+ reproducible tar.gz $release_dir/ATHENA -> $artifact"
  else
    rm -f "$temporary"
    tar --sort=name \
      --mtime="@$source_epoch" \
      --owner=0 --group=0 --numeric-owner \
      -C "$release_dir" -cf - ATHENA |
      gzip -9n > "$temporary"
    mv -f "$temporary" "$artifact"
  fi
  checksum "$artifact"
}

archive_linux_services () {
  local source_epoch
  source_epoch="$(git -C "$repo_root" log -1 --format=%ct)"

  run python3 "$script_dir/package_linux_service.py" \
    --kind transmitter \
    --binary "$local_build_dir/tools/athena-transmitter/athena-transmitter" \
    --repo-root "$repo_root" \
    --version "$version" \
    --source-epoch "$source_epoch" \
    --output "$release_dir/ATHENA-Transmitter-$version-linux-x86_64.tar.gz"
  checksum "$release_dir/ATHENA-Transmitter-$version-linux-x86_64.tar.gz"

  run python3 "$script_dir/package_linux_service.py" \
    --kind web-server \
    --binary "$local_build_dir/tools/athena-web-server/athena-web-server" \
    --repo-root "$repo_root" \
    --version "$version" \
    --source-epoch "$source_epoch" \
    --output "$release_dir/ATHENA-Web-Server-$version-linux-x86_64.tar.gz"
  checksum "$release_dir/ATHENA-Web-Server-$version-linux-x86_64.tar.gz"
}

stage_container_artifacts () {
  local flavor source destination package

  for flavor in dev rel; do
    source="$container_build_dir/ATHENA-$flavor.AppImage"
    if [[ "$flavor" == "dev" ]]; then
      destination="$release_dir/ATHENA-$version-dev-linux-x86_64.AppImage"
    else
      destination="$release_dir/ATHENA-$version-linux-x86_64.AppImage"
    fi
    copy_artifact "$source" "$destination"
  done

  for flavor in dev rel; do
    for package in \
      "$container_build_dir/packages/ATHENA-$version-$flavor-linux-x86_64.deb" \
      "$container_build_dir/packages/ATHENA-$version-$flavor-linux-x86_64.rpm"; do
      copy_artifact "$package" "$release_dir/$(basename -- "$package")"
    done
  done

  for source in \
    "$container_build_dir/ATHENA-Transmitter-$version-linux-x86_64.tar.gz" \
    "$container_build_dir/ATHENA-Web-Server-$version-linux-x86_64.tar.gz"; do
    copy_artifact "$source" "$release_dir/$(basename -- "$source")"
  done
}

wine_smoke_test () {
  local runtime="$release_dir/windows/ATHENA"
  local wine_command
  local attempt
  if command -v wine64 >/dev/null 2>&1; then
    wine_command=wine64
  elif command -v wine >/dev/null 2>&1; then
    wine_command=wine
  else
    echo "Wine is required; pass --skip-wine-test to bypass the smoke test." >&2
    exit 1
  fi

  if [[ "$dry_run" -eq 1 ]]; then
    echo "+ Wine version smoke test $runtime/bin/ATHENA.exe"
    return
  fi

  local log="$windows_build_dir/wine-release-smoke.log"
  local attempt_log
  mkdir -p "$(dirname -- "$log")"
  : >"$log"
  for attempt in 1 2 3; do
    attempt_log="$windows_build_dir/wine-release-smoke-$attempt.log"
    (
      cd -- "$runtime"
      WINEDEBUG=-all timeout 60s "$wine_command" \
        bin/ATHENA.exe -H -v >"$attempt_log" 2>&1
    )
    # A fresh Wine launch may keep initialization work in child processes.
    # Wait for the complete Wine process group before inspecting its output.
    WINEDEBUG=-all timeout 60s wineserver -w
    {
      echo "== attempt $attempt"
      cat "$attempt_log"
    } >>"$log"
    if grep -q 'ATHENA' "$attempt_log"; then
      return
    fi
    # Retry a freshly initialized Wine prefix after its background setup has
    # completed, while still requiring a real ATHENA version response.
    sleep 1
  done
  cat "$log" >&2
  echo "Windows release smoke test did not report ATHENA." >&2
  exit 1
}

archive_windows_runtime () {
  local parent="$release_dir/windows"
  local artifact="$release_dir/ATHENA-$version-windows-x86_64.zip"
  local temporary="$artifact.tmp.zip"

  if [[ "$dry_run" -eq 1 ]]; then
    echo "+ zip $parent/ATHENA -> $artifact"
  else
    rm -f "$temporary" "$artifact"
    (
      cd -- "$parent"
      zip -qr "$temporary" ATHENA
    )
    mv -f "$temporary" "$artifact"
  fi
  checksum "$artifact"
}

run mkdir -p "$release_dir"

if [[ "$build_local" -eq 1 ]]; then
  echo "==> Native Linux release tree and tar.gz"
  configure_local_build
  assemble_linux_runtime
  archive_linux_runtime
  archive_linux_services
fi

if [[ "$build_container" -eq 1 ]]; then
  echo "==> AppImage, DEB, and RPM"
  run env \
    "ATHENA_BUILD_JOBS=$jobs" \
    "ATHENA_BUILD_FLAVORS=dev rel" \
    "$repo_root/tools/container-build/build-athena-container.sh"
  stage_container_artifacts
fi

if [[ "$build_windows" -eq 1 ]]; then
  echo "==> Windows cross-build and ZIP"
  run "$repo_root/tools/windows-deps/build-athena-win64-release.sh" \
    --build-dir "$windows_build_dir" \
    --release-dir "$release_dir/windows/ATHENA" \
    -j "$jobs"
  run python3 "$script_dir/runtime_policy.py" verify \
    "$release_dir/windows/ATHENA"
  if [[ "$run_wine_test" -eq 1 ]]; then
    wine_smoke_test
  fi
  archive_windows_runtime
fi

echo "ATHENA $version release artifacts are ready under $release_dir."
echo "No GitHub release, tag, or upload was created."
