#!/usr/bin/env bash

set -euo pipefail

ssh_target="${ATHENA_IPADOS_SSH_TARGET:-felix@127.0.0.1}"
ssh_port="${ATHENA_IPADOS_SSH_PORT:-2222}"
remote_developer_root="${ATHENA_IPADOS_REMOTE_DEVELOPER_ROOT:-/Users/felix/Developer}"
remote_source="${ATHENA_IPADOS_REMOTE_SOURCE:-${remote_developer_root}/ATHENA}"
remote_image="${ATHENA_IPADOS_SOURCE_IMAGE:-${remote_developer_root}/ATHENA-source.sparsebundle}"
remote_marker="${remote_developer_root}/.athena-linux-source-mirror"
quiet=0

if [[ "${1:-}" == "--quiet" ]]; then
  quiet=1
elif [[ $# -ne 0 ]]; then
  printf 'usage: %s [--quiet]\n' "$0" >&2
  exit 2
fi

case "${remote_source}" in
  "${remote_developer_root}"/ATHENA) ;;
  *)
    printf 'bootstrap-vm: refusing unexpected remote source path: %s\n' \
      "${remote_source}" >&2
    exit 1
    ;;
esac

ssh -o BatchMode=yes -o ConnectTimeout=10 -p "${ssh_port}" \
  "${ssh_target}" sh -s -- \
  "${remote_developer_root}" "${remote_source}" "${remote_image}" \
  "${remote_marker}" "${quiet}" <<'REMOTE'
set -eu

developer_root=$1
source_root=$2
source_image=$3
marker=$4
quiet=$5

export PATH="/Users/felix/.cargo/bin:/opt/local/bin:/opt/local/sbin:$PATH"
export LC_ALL=en_US.UTF-8
export LANG=en_US.UTF-8

mkdir -p \
  "$developer_root" \
  "$developer_root/athena-deps/host/bin" \
  "$developer_root/athena-deps/host/src" \
  "$developer_root/athena-deps/host/build" \
  "$developer_root/athena-deps/ipados/build" \
  "$developer_root/athena-deps/ipados/prefix" \
  "$developer_root/athena-build/ipados" \
  "$developer_root/athena-artifacts"

if [ -e "$marker" ]; then
  recorded=$(cat "$marker")
  [ "$recorded" = "$source_root" ] || {
    echo "source mirror marker names '$recorded', refusing '$source_root'" >&2
    exit 1
  }
fi

is_source_mount=0

# macOS may restore an already-known sparsebundle at /Volumes/<volname> after
# login/reboot instead of our requested source_root.  Resolve attachment by
# image path, not by guessing from `mount`, and remount it at the canonical
# source path when necessary.
attached_info=$(
  hdiutil info -plist | python3 -c '
import os
import plistlib
import sys

image = os.path.realpath(sys.argv[1])
data = plistlib.loads(sys.stdin.buffer.read())
for entry in data.get("images", []):
    path = entry.get("image-path")
    if not path or os.path.realpath(path) != image:
        continue
    for entity in entry.get("system-entities", []):
        device = entity.get("dev-entry", "")
        mount = entity.get("mount-point", "")
        if mount:
            print(device)
            print(mount)
            raise SystemExit(0)
' "$source_image"
)

if [ -n "$attached_info" ]; then
  attached_device=$(printf '%s\n' "$attached_info" | sed -n '1p')
  attached_mount=$(printf '%s\n' "$attached_info" | sed -n '2p')
  if [ "$attached_mount" = "$source_root" ]; then
    is_source_mount=1
  else
    echo "Remounting ATHENA source image from $attached_mount to $source_root"
    hdiutil detach "$attached_device" >/dev/null
  fi
fi

if [ "$is_source_mount" -eq 0 ]; then
  if [ ! -e "$source_image" ]; then
    if [ -d "$source_root" ] && [ -n "$(find "$source_root" -mindepth 1 -print -quit)" ]; then
      if [ ! -e "$marker" ] || [ "$(cat "$marker")" != "$source_root" ]; then
        echo "refusing to replace unclaimed non-empty source directory: $source_root" >&2
        exit 1
      fi
      # This is a previously claimed mirror on the case-insensitive host volume.
      # It is disposable because Linux is the sole source of truth.
      chmod -R u+w "$source_root" 2>/dev/null || true
      rm -rf "$source_root"
    fi

    mkdir -p "$(dirname "$source_image")"
    hdiutil create \
      -size 4g \
      -type SPARSEBUNDLE \
      -fs 'Case-sensitive APFS' \
      -volname ATHENA-Source \
      "$source_image" >/dev/null
  fi

  if [ -e "$source_root" ] && [ ! -d "$source_root" ]; then
    echo "source mountpoint exists and is not a directory: $source_root" >&2
    exit 1
  fi
  mkdir -p "$source_root"
  if [ -n "$(find "$source_root" -mindepth 1 -print -quit)" ]; then
    echo "source mountpoint is not empty before image attach: $source_root" >&2
    exit 1
  fi

  hdiutil attach \
    -nobrowse \
    -mountpoint "$source_root" \
    "$source_image" >/dev/null
fi

fs_info=$(diskutil info "$source_root")
printf '%s\n' "$fs_info" | grep -F 'File System Personality:   Case-sensitive APFS' >/dev/null || {
  echo "ATHENA source mirror is not on case-sensitive APFS: $source_root" >&2
  exit 1
}

printf '%s\n' "$source_root" >"$marker"

if [ "$quiet" -eq 0 ]; then
  echo "ATHENA VM bootstrap ready"
  echo "  source mirror: $source_root"
  echo "  source image:  $source_image"
  echo "  host prefix:   $developer_root/athena-deps/host"
  echo "  target prefix: $developer_root/athena-deps/ipados"
  echo "  build tree:    $developer_root/athena-build/ipados"
  echo "  artifacts:     $developer_root/athena-artifacts"
  echo
  echo "Host tool inventory:"
  for tool in cmake ninja cargo rustc pkg-config autoconf automake make clang git python3 rsync; do
    if command -v "$tool" >/dev/null 2>&1; then
      printf '  %-10s %s\n' "$tool" "$(command -v "$tool")"
    else
      printf '  %-10s %s\n' "$tool" MISSING
    fi
  done
fi
REMOTE
