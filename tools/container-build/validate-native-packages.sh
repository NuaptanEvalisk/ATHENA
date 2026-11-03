#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="${1:-$(cd -- "$script_dir/../.." && pwd)}"
packages="$repo_root/container_build/packages"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
version="$(
  sed -n 's/.*set *(ATHENA_APP_VERSION *"\([^"]*\)".*/\1/p' \
    "$repo_root/CMakeLists.txt" | head -n1
)"
[ -n "$version" ] || {
  echo "could not read ATHENA_APP_VERSION from CMakeLists.txt" >&2
  exit 1
}
read -r -a flavors <<<"${ATHENA_BUILD_FLAVORS:-dev rel}"

for flavor in "${flavors[@]}"; do
  deb="$packages/ATHENA-$version-$flavor-linux-x86_64.deb"
  rpm_package="$packages/ATHENA-$version-$flavor-linux-x86_64.rpm"
  [ -f "$deb" ] && [ -f "$rpm_package" ] || {
    echo "missing ATHENA $version native package for $flavor" >&2
    exit 1
  }
  dpkg-deb --info "$deb" >/dev/null
  dpkg-deb --contents "$deb" >"$work/$flavor.deb.list"
  rpm -qpl "$rpm_package" >"$work/$flavor.rpm.list"
  for listing in "$work/$flavor.deb.list" \
                 "$work/$flavor.rpm.list"; do
    grep -q '/opt/ATHENA/AppRun' "$listing"
    grep -q '/opt/ATHENA/usr/share/ATHENA/bin/athena-materials-engine' "$listing"
    grep -q '/opt/ATHENA/usr/share/ATHENA/bin/athena-transmitter' "$listing"
    grep -q '/opt/ATHENA/usr/share/ATHENA/bin/athena-web-server' "$listing"
    grep -q '/opt/ATHENA/usr/share/ATHENA/share/ATHENA/web/index.html' "$listing"
    grep -q '/opt/ATHENA/usr/share/ATHENA/share/ATHENA/web/app.js' "$listing"
    if grep -E '/(progs|lib/(athena-scheme|athena-guile|guile)|share/guile)/|/lib(athena-)?guile[^/]*[.](so|dylib)|/libgc[.]so' "$listing"; then
      echo "native package unexpectedly contains retired Scheme runtime files: $listing" >&2
      exit 1
    fi
    if grep -E 'ATHENA\.bin\.before-|/\.venv/|/\.uv-cache/|/__pycache__/|\.(gguf|safetensors|onnx|ckpt|pth|pt)$' \
        "$listing"; then
      echo "excluded release artifact found in $listing" >&2
      exit 1
    fi
  done
done

echo "ATHENA native package structure validated."
