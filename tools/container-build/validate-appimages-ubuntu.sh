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
      appdir="$tmp/squashfs-root"
      for required in \
        "$appdir/usr/share/ATHENA/bin/athena-materials-engine" \
        "$appdir/usr/share/ATHENA/bin/athena-transmitter" \
        "$appdir/usr/share/ATHENA/bin/athena-web-server" \
        "$appdir/usr/share/ATHENA/share/ATHENA/web/index.html" \
        "$appdir/usr/share/ATHENA/share/ATHENA/web/app.js"; do
        if [ ! -e "$required" ]; then
          echo "missing AppImage payload: $required" >&2
          exit 1
        fi
      done
      ldd_log="/tmp/athena-${flavor}-ldd.txt"
      LD_LIBRARY_PATH="$appdir/usr/lib:$appdir/usr/share/ATHENA/lib" \
        ldd "$appdir/usr/share/ATHENA/bin/ATHENA.bin" >"$ldd_log"
      if grep "not found" "$ldd_log"; then
        exit 1
      fi
      if grep -E "libguile-(2|3)" "$ldd_log"; then
        echo "unexpected non-1.8 Guile dependency" >&2
        cat "$ldd_log" >&2
        exit 1
      fi
      ATHENA_QT_PLATFORM=offscreen \
        QT_QPA_PLATFORM=offscreen \
        "$appdir/AppRun" -H -v >/tmp/athena-version.txt
      grep -q "ATHENA" /tmp/athena-version.txt
      echo "version check passed for $flavor"
      startup_log="/tmp/athena-${flavor}-startup.log"
      set +e
      ATHENA_QT_PLATFORM=offscreen \
        QT_QPA_PLATFORM=offscreen \
        timeout 60s "$appdir/AppRun" \
        >"$startup_log" 2>&1
      startup_status=$?
      set -e
      echo "startup status for $flavor: $startup_status"
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
