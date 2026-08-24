#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"
container_build_dir="$repo_root/container_build"
image_name="${ATHENA_CONTAINER_IMAGE:-localhost/athena-leap160-builder:latest}"
container_home="$container_build_dir/distrobox-home"

mkdir -p "$container_build_dir/logs" "$container_home"

env -u HTTP_PROXY -u HTTPS_PROXY -u ALL_PROXY \
    -u http_proxy -u https_proxy -u all_proxy \
  podman build \
  --http-proxy=false \
  --format docker \
  -t "$image_name" \
  -f "$script_dir/Containerfile.opensuse-leap160" \
  "$repo_root" \
  2>&1 | tee "$container_build_dir/logs/podman-build-image.log"

podman run --rm \
  --userns=keep-id \
  --user "$(id -u):$(id -g)" \
  --security-opt label=disable \
  --network host \
  --env HOME="$container_home" \
  --volume "$repo_root:$repo_root:rw" \
  --volume /opt/intel/oneapi:/opt/intel/oneapi:ro \
  "$image_name" \
  "$script_dir/opensuse-build-inside.sh" "$repo_root" "$@" \
  2>&1 | tee "$container_build_dir/logs/opensuse-build.log"

if [ "${ATHENA_SKIP_UBUNTU_VALIDATE:-0}" != "1" ]; then
  "$script_dir/validate-appimages-ubuntu.sh" "$repo_root" \
    2>&1 | tee "$container_build_dir/logs/ubuntu-validate.log"
fi

"$script_dir/validate-native-packages.sh" "$repo_root" \
  2>&1 | tee "$container_build_dir/logs/native-package-validate.log"
