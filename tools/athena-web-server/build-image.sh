#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"
runtime="${ATHENA_WEB_RUNTIME:-$repo_root/container_build/ATHENA-rel.AppImage}"
image="${ATHENA_WEB_IMAGE:-localhost/athena-web:latest}"
context="$repo_root/container_build/athena-web-image-context"

usage() {
  cat <<EOF
Usage: $0 [--runtime ATHENA-DIRECTORY-OR-APPIMAGE] [--image OCI-TAG]

Build the openSUSE Web-Accessible ATHENA image. A directory runtime must contain
bin/ATHENA.bin and its portable libraries. An AppImage runtime is unpacked
without FUSE and supplies both ATHENA and its library closure. Model weights and
generated Python environments are removed and rejected by the shared release
policy.
EOF
}

while (($#)); do
  case "$1" in
    --runtime)
      runtime="${2:?missing runtime path}"
      shift 2
      ;;
    --image)
      image="${2:?missing image tag}"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 64
      ;;
  esac
done

mkdir -p "$context"
if [[ -d "$runtime" ]]; then
  if [[ ! -x "$runtime/bin/ATHENA.bin" ]]; then
    echo "ATHENA runtime does not contain executable bin/ATHENA.bin: $runtime" >&2
    exit 1
  fi
  python3 "$repo_root/tools/release/runtime_policy.py" copy \
    --keep-source-libraries \
    --keep-athena-binary \
    "$runtime" "$context/runtime"
elif [[ -f "$runtime" ]]; then
  command -v unsquashfs >/dev/null || {
    echo "unsquashfs is required to consume an ATHENA AppImage" >&2
    exit 1
  }
  offset="$("$runtime" --appimage-offset)"
  [[ "$offset" =~ ^[0-9]+$ ]] || {
    echo "Could not read AppImage SquashFS offset: $runtime" >&2
    exit 1
  }
  extracted="$(mktemp -d "$context/appimage.XXXXXX")"
  trap 'rm -rf -- "$extracted"' EXIT
  unsquashfs -quiet -dest "$extracted" -offset "$offset" "$runtime" \
    usr/share/ATHENA usr/share/guile usr/lib usr/lib64 usr/plugins
  if [[ ! -x "$extracted/usr/share/ATHENA/bin/ATHENA.bin" ]]; then
    echo "AppImage does not contain usr/share/ATHENA/bin/ATHENA.bin" >&2
    exit 1
  fi
  python3 "$repo_root/tools/release/runtime_policy.py" copy \
    --keep-source-libraries \
    --keep-athena-binary \
    "$extracted/usr/share/ATHENA" "$context/runtime"
  mkdir -p "$context/runtime/lib"
  cp -a "$extracted/usr/lib/." "$context/runtime/lib/"
  mkdir -p "$context/runtime/plugins"
  cp -a "$extracted/usr/plugins/." "$context/runtime/plugins/"
  if [[ -d "$extracted/usr/lib64" ]]; then
    mkdir -p "$context/runtime/lib64"
    cp -a "$extracted/usr/lib64/." "$context/runtime/lib64/"
  fi
  if [[ -d "$extracted/usr/share/guile" ]]; then
    mkdir -p "$context/runtime/share/guile"
    cp -a "$extracted/usr/share/guile/." "$context/runtime/share/guile/"
  fi
  python3 "$repo_root/tools/release/runtime_policy.py" verify \
    "$context/runtime"
else
  echo "ATHENA runtime does not exist: $runtime" >&2
  exit 1
fi

for file in \
  Containerfile \
  container-entrypoint.sh \
  start-athena.sh \
  session-helper.py \
  weston-vnc-private-bridge.patch
do
  install -m 0644 "$script_dir/$file" "$context/$file"
done
chmod 0755 \
  "$context/container-entrypoint.sh" \
  "$context/start-athena.sh" \
  "$context/session-helper.py"

exec podman build \
  --network=host \
  --tag "$image" \
  --file "$context/Containerfile" \
  "$context"
