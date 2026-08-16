#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="${1:-$(cd -- "$script_dir/../.." && pwd)}"
packages="$repo_root/container_build/packages"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

for flavor in dev rel; do
  deb=("$packages"/ATHENA-*-$flavor-linux-x86_64.deb)
  opensuse=("$packages"/ATHENA-*-$flavor-opensuse-x86_64.rpm)
  rhel=("$packages"/ATHENA-*-$flavor-rhel-x86_64.rpm)
  [ -f "${deb[0]}" ] && [ -f "${opensuse[0]}" ] && [ -f "${rhel[0]}" ] || {
    echo "missing native package for $flavor" >&2
    exit 1
  }
  dpkg-deb --info "${deb[0]}" >/dev/null
  dpkg-deb --contents "${deb[0]}" >"$work/$flavor.deb.list"
  rpm -qpl "${opensuse[0]}" >"$work/$flavor.opensuse.list"
  rpm -qpl "${rhel[0]}" >"$work/$flavor.rhel.list"

  for listing in "$work/$flavor.deb.list" \
                 "$work/$flavor.opensuse.list" \
                 "$work/$flavor.rhel.list"; do
    grep -q '/opt/ATHENA/AppRun' "$listing"
    grep -q '/opt/ATHENA/usr/share/ATHENA/bin/athena-transmitter' "$listing"
    grep -q '/opt/ATHENA/usr/share/ATHENA/bin/athena-web-server' "$listing"
    grep -q '/opt/ATHENA/usr/share/ATHENA/share/ATHENA/web/index.html' "$listing"
    grep -q '/opt/ATHENA/usr/share/ATHENA/share/ATHENA/web/app.js' "$listing"
    if grep -E 'ATHENA\.bin\.before-|/\.venv/|/\.uv-cache/|/__pycache__/|\.(gguf|safetensors|onnx|ckpt|pth|pt)$' \
        "$listing"; then
      echo "excluded release artifact found in $listing" >&2
      exit 1
    fi
  done
done

echo "ATHENA native package structure validated."
