#!/usr/bin/env bash
# Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
set -euo pipefail
: "${ATHENA_HODARIUM_ISOLATED_KEYRING:?run inside isolated-keyring.sh}"
binary=$(realpath "${1:?native authority test binary}")
repository=$(realpath "${2:?repository root}")
temporary=$(mktemp -d "${TMPDIR:-/tmp}/athena-hodarium-authority.XXXXXX")
server=
cleanup() {
  if [[ -n "$server" ]]; then
    kill "$server" 2>/dev/null || true
    wait "$server" 2>/dev/null || true
  fi
  rm -rf -- "$temporary"
}
trap cleanup EXIT
"$binary" prepare "$temporary"
cd "$repository/tools/hodarium"
ATHENA_HODARIUM_NATIVE_CLIENT="$temporary" go test ./internal/authority \
  -run '^TestNativeClientServer$' -count=1 >"$temporary/server.log" 2>&1 &
server=$!
for attempt in {1..100}; do
  if [[ -e "$temporary/ready.json" ]]; then
    "$binary" validate "$temporary"
    wait "$server"
    server=
    cat "$temporary/server.log"
    exit
  fi
  if ! kill -0 "$server" 2>/dev/null; then
    cat "$temporary/server.log"
    exit 1
  fi
  sleep 0.1
done
cat "$temporary/server.log"
exit 1
