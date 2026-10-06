#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ssh_target="${ATHENA_IPADOS_SSH_TARGET:-felix@127.0.0.1}"
ssh_port="${ATHENA_IPADOS_SSH_PORT:-2222}"
remote_developer_root="${ATHENA_IPADOS_REMOTE_DEVELOPER_ROOT:-/Users/felix/Developer}"
jobs="${ATHENA_IPADOS_JOBS:-20}"

"${script_dir}/configure-athena.sh"

ssh -o BatchMode=yes -o ConnectTimeout=10 -p "${ssh_port}" \
  "${ssh_target}" sh -s -- "${remote_developer_root}" "${jobs}" <<'REMOTE'
set -eu

developer_root=$1
jobs=$2

export PATH="/Users/felix/.cargo/bin:/opt/local/bin:/opt/local/sbin:$PATH"
export LC_ALL=en_US.UTF-8
export LANG=en_US.UTF-8
export IPHONEOS_DEPLOYMENT_TARGET=27.0

build_dir="$developer_root/athena-build/ipados"

printf '\n======================================================================\n'
printf 'Building ATHENA iPadOS target with %s jobs (verbose)\n' "$jobs"
printf '======================================================================\n'

cmake --build "$build_dir" \
  --config Debug \
  --target ATHENA \
  --parallel "$jobs" \
  --verbose
REMOTE

