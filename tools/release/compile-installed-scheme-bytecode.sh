#!/usr/bin/env bash
set -euo pipefail

if [[ $# -gt 1 ]]; then
  echo "usage: $0 [ATHENA_RUNTIME_ROOT]" >&2
  exit 2
fi

runtime_root="${1:-/opt/ATHENA/usr/share/ATHENA}"
install_root="$(cd -- "$runtime_root/.." && pwd)"
tools_root="$install_root/tools"
runtime_id="athena-guile-3.0.10-native"
cache_root="${ATHENA_SCHEME_SYSTEM_CACHE:-/var/cache/athena/scheme}"
compile_home="${ATHENA_SCHEME_COMPILE_HOME:-/var/cache/athena/scheme-compile-home}"
jobs="${ATHENA_SCHEME_COMPILE_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')}"

# Package-manager scriptlets may run under tighter memory and process limits
# than the build host.  Four workers retain most of the speedup while avoiding
# the instability observed when twenty complete ATHENA bootstrap processes run
# concurrently during installation.
if (( jobs > 4 )); then
  jobs=4
fi

exec /bin/bash "$tools_root/compile-athena-scheme-bytecode.sh" \
  "$runtime_root/bin/ATHENA.bin" \
  "$cache_root/$runtime_id" \
  "$runtime_root" \
  "$compile_home" \
  "$runtime_root/lib/athena-guile" \
  "$runtime_root/progs" \
  "$runtime_root/lib" \
  "$runtime_root/lib" \
  "$runtime_root/lib" \
  "$jobs" \
  "$runtime_id"
