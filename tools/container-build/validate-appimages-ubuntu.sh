#!/usr/bin/env bash
set -euo pipefail

repo_root="${1:-}"
if [ -z "$repo_root" ]; then
  script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
  repo_root="$(cd -- "$script_dir/../.." && pwd)"
fi
repo_root="$(cd -- "$repo_root" && pwd)"

for image in "$repo_root/container_build/ATHENA-dev.AppImage" \
             "$repo_root/container_build/ATHENA-rel.AppImage"; do
  [ -x "$image" ] || {
    echo "missing AppImage: $image" >&2
    exit 1
  }
done

podman run --rm \
  --network host \
  --env HTTP_PROXY \
  --env HTTPS_PROXY \
  --env ALL_PROXY \
  --env http_proxy \
  --env https_proxy \
  --env all_proxy \
  -v "$repo_root/container_build:/work:ro" \
  docker.io/library/ubuntu:24.04 \
  bash -lc '
    set -euo pipefail
    apt-get update >/dev/null
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      binutils ca-certificates file fontconfig libegl1 libgl1 libglx0 \
      libc6 libstdc++6 >/dev/null
    export LANG=C.UTF-8
    export LC_ALL=C.UTF-8
    for flavor in dev rel; do
      img="/work/ATHENA-${flavor}.AppImage"
      echo "== $img"
      tmp="$(mktemp -d)"
      cd "$tmp"
      "$img" --appimage-extract >/dev/null
      ldd_log="/tmp/athena-${flavor}-ldd.txt"
      LD_LIBRARY_PATH="$tmp/squashfs-root/usr/lib:$tmp/squashfs-root/usr/share/ATHENA/lib" \
        ldd "$tmp/squashfs-root/usr/share/ATHENA/bin/ATHENA.bin" >"$ldd_log"
      if grep "not found" "$ldd_log"; then
        exit 1
      fi
      if grep -E "libguile-(2|3)" "$ldd_log"; then
        echo "unexpected non-1.8 Guile dependency" >&2
        cat "$ldd_log" >&2
        exit 1
      fi
      APPIMAGE_EXTRACT_AND_RUN=1 \
        ATHENA_QT_PLATFORM=offscreen \
        QT_QPA_PLATFORM=offscreen \
        "$img" -H -v >/tmp/athena-version.txt
      grep -q "ATHENA" /tmp/athena-version.txt
      startup_log="/tmp/athena-${flavor}-startup.log"
      set +e
      APPIMAGE_EXTRACT_AND_RUN=1 \
        ATHENA_QT_PLATFORM=offscreen \
        QT_QPA_PLATFORM=offscreen \
        timeout 60s "$img" \
        >"$startup_log" 2>&1
      startup_status=$?
      set -e
      if [ "$startup_status" -ne 0 ] && [ "$startup_status" -ne 124 ]; then
        cat "$startup_log" >&2
        exit "$startup_status"
      fi
      if grep -E "Syntax error|Unbound variable|Backtrace:|libguile-(2|3)" "$startup_log"; then
        cat "$startup_log" >&2
        exit 1
      fi
      rm -rf "$tmp"
    done
  '
