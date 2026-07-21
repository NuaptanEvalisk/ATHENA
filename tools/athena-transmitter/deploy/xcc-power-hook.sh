#!/usr/bin/env bash
set -euo pipefail

config="${ATHENA_TRANSMITTER_XCC_CONFIG:-/etc/athena-transmitter/xcc.env}"
if [[ ! -r "$config" ]]; then
  echo "ATHENA Transmitter XCC configuration is not readable: $config" >&2
  exit 1
fi
# shellcheck source=/dev/null
source "$config"

: "${XCC_URL:?XCC_URL is required}"
: "${XCC_SYSTEM_URI:?XCC_SYSTEM_URI is required}"
: "${XCC_NETRC:?XCC_NETRC is required}"
: "${ATHENA_BACKEND_IDENTITY_URL:?ATHENA_BACKEND_IDENTITY_URL is required}"
XCC_WAKE_TIMEOUT_SECONDS="${XCC_WAKE_TIMEOUT_SECONDS:-900}"
XCC_WAKE_POLL_SECONDS="${XCC_WAKE_POLL_SECONDS:-5}"
XCC_SHUTDOWN_TIMEOUT_SECONDS="${XCC_SHUTDOWN_TIMEOUT_SECONDS:-300}"
XCC_SHUTDOWN_POLL_SECONDS="${XCC_SHUTDOWN_POLL_SECONDS:-5}"
XCC_RUNTIME_DIR="${XCC_RUNTIME_DIR:-/run/athena-transmitter}"
XCC_OWNERSHIP_FILE="${XCC_OWNERSHIP_FILE:-$XCC_RUNTIME_DIR/xcc-power-owned}"
XCC_LOCK_FILE="${XCC_LOCK_FILE:-$XCC_RUNTIME_DIR/xcc-power.lock}"

if [[ ! -r "$XCC_NETRC" ]]; then
  echo "ATHENA Transmitter XCC credential file is not readable: $XCC_NETRC" >&2
  exit 1
fi

log() {
  logger -t athena-xcc -- "$*"
  printf '%s\n' "athena-xcc: $*"
}

xcc_get_system() {
  curl --fail --silent --show-error --insecure \
    --netrc-file "$XCC_NETRC" \
    "$XCC_URL$XCC_SYSTEM_URI"
}

xcc_reset() {
  local reset_type="$1" system target
  system="$(xcc_get_system)"
  target="$(jq -r '.Actions["#ComputerSystem.Reset"].target // empty' \
    <<<"$system")"
  if [[ -z "$target" ]]; then
    echo "XCC did not advertise ComputerSystem.Reset" >&2
    exit 1
  fi
  curl --fail --silent --show-error --insecure \
    --netrc-file "$XCC_NETRC" \
    --header 'Content-Type: application/json' \
    --request POST \
    --data "{\"ResetType\":\"$reset_type\"}" \
    "$XCC_URL$target" >/dev/null
}

backend_ready() {
  curl --fail --silent --show-error --max-time 3 \
    "$ATHENA_BACKEND_IDENTITY_URL" >/dev/null 2>&1
}

mkdir -p "$XCC_RUNTIME_DIR"
exec 9>"$XCC_LOCK_FILE"

record_power_ownership() {
  local temporary="$XCC_OWNERSHIP_FILE.$$"
  printf 'transmitter_pid=%s\nwake_time=%s\n' \
    "$PPID" "$(date --utc +%Y-%m-%dT%H:%M:%SZ)" >"$temporary"
  mv -f "$temporary" "$XCC_OWNERSHIP_FILE"
}

phase="${1:-${ATHENA_DELEGATION_TRANSMITTER_PHASE:-}}"
case "$phase" in
  pre)
    flock 9
    if backend_ready; then
      if [[ -e "$XCC_OWNERSHIP_FILE" ]]; then
        log "backend already ready; retaining transmitter power ownership"
      else
        log "backend already ready; server was not started by transmitter"
      fi
      exit 0
    fi
    state="$(xcc_get_system | jq -r '.PowerState // "Unknown"')"
    if [[ "$state" == "Off" ]]; then
      log "requesting server power on"
      rm -f "$XCC_OWNERSHIP_FILE"
      xcc_reset On
      record_power_ownership
      log "recorded transmitter power ownership"
    elif [[ -e "$XCC_OWNERSHIP_FILE" ]]; then
      log "server power state is $state; retaining transmitter power ownership"
    else
      log "server was already $state; it remains externally managed"
    fi
    flock -u 9
    deadline=$((SECONDS + XCC_WAKE_TIMEOUT_SECONDS))
    until backend_ready; do
      if (( SECONDS >= deadline )); then
        echo "ATHENA delegation backend did not become ready before timeout" >&2
        exit 1
      fi
      sleep "$XCC_WAKE_POLL_SECONDS"
    done
    log "backend is ready"
    ;;
  post)
    flock 9
    if [[ ! -e "$XCC_OWNERSHIP_FILE" ]]; then
      log "skipping shutdown; transmitter did not power on this server"
      exit 0
    fi
    state="$(xcc_get_system | jq -r '.PowerState // "Unknown"')"
    if [[ "$state" == "Off" ]]; then
      rm -f "$XCC_OWNERSHIP_FILE"
      log "server is already off"
      exit 0
    fi
    log "requesting graceful server shutdown"
    xcc_reset GracefulShutdown
    deadline=$((SECONDS + XCC_SHUTDOWN_TIMEOUT_SECONDS))
    while :; do
      state="$(xcc_get_system | jq -r '.PowerState // "Unknown"')"
      if [[ "$state" == "Off" ]]; then
        rm -f "$XCC_OWNERSHIP_FILE"
        log "server shutdown completed; released transmitter power ownership"
        exit 0
      fi
      if (( SECONDS >= deadline )); then
        echo "XCC did not report PowerState Off before timeout; ownership retained" >&2
        exit 1
      fi
      sleep "$XCC_SHUTDOWN_POLL_SECONDS"
    done
    ;;
  *)
    echo "Usage: $0 pre|post" >&2
    exit 2
    ;;
esac
