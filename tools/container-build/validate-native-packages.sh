#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="${1:-$(cd -- "$script_dir/../.." && pwd)}"
packages="$repo_root/container_build/packages"

for flavor in dev rel; do
  deb=("$packages"/ATHENA-*-$flavor-linux-x86_64.deb)
  opensuse=("$packages"/ATHENA-*-$flavor-opensuse-x86_64.rpm)
  rhel=("$packages"/ATHENA-*-$flavor-rhel-x86_64.rpm)
  [ -f "${deb[0]}" ] && [ -f "${opensuse[0]}" ] && [ -f "${rhel[0]}" ] || {
    echo "missing native package for $flavor" >&2
    exit 1
  }
  dpkg-deb --info "${deb[0]}" >/dev/null
  dpkg-deb --contents "${deb[0]}" | grep -q './opt/ATHENA/AppRun'
  rpm -qpl "${opensuse[0]}" | grep -q '/opt/ATHENA/AppRun'
  rpm -qpl "${rhel[0]}" | grep -q '/opt/ATHENA/AppRun'
done

echo "ATHENA native package structure validated."
