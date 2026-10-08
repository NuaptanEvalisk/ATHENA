#!/usr/bin/env bash
# Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
set -euo pipefail
binary=$(realpath "${1:?identity test binary required}")
shift
temporary=$(mktemp -d "${TMPDIR:-/tmp}/athena-hodarium-keyring.XXXXXX")
trap 'rm -rf -- "$temporary"' EXIT
mkdir -p "$temporary/data" "$temporary/config" "$temporary/runtime"
chmod 700 "$temporary/runtime"
env HOME="$temporary" XDG_DATA_HOME="$temporary/data" \
  XDG_CONFIG_HOME="$temporary/config" XDG_RUNTIME_DIR="$temporary/runtime" \
  GNOME_KEYRING_CONTROL="$temporary/runtime/keyring" \
  ATHENA_HODARIUM_ISOLATED_KEYRING=1 \
  dbus-run-session -- bash -euo pipefail -c '
    printf "%s" "isolated-test-password" | gnome-keyring-daemon \
      --foreground --components=secrets --unlock \
      --control-directory="$GNOME_KEYRING_CONTROL" &
    daemon=$!
    trap '\''kill "$daemon" 2>/dev/null || true; wait "$daemon" 2>/dev/null || true'\'' EXIT
    for attempt in {1..50}; do
      if gdbus call --session --dest org.freedesktop.DBus \
          --object-path /org/freedesktop/DBus --method org.freedesktop.DBus.NameHasOwner \
          org.freedesktop.secrets | grep -q true; then
        "$@"
        exit
      fi
      sleep 0.1
    done
    echo "Isolated Secret Service did not start" >&2
    exit 1
  ' bash "$binary" "$@"
