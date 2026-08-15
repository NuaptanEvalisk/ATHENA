#!/usr/bin/env bash

set -euo pipefail

phase="${1:-}"
timing_file="${ATHENA_PROFILE_TIMING_FILE:?ATHENA_PROFILE_TIMING_FILE is not set}"
control_log="${ATHENA_PROFILE_CONTROL_LOG:?ATHENA_PROFILE_CONTROL_LOG is not set}"

case "$phase" in
  start|vault-loaded|vault-settled|buffer-loaded|finish) ;;
  *)
    printf 'profile-control: unknown phase: %s\n' "$phase" >&2
    exit 2
    ;;
esac

log_control() {
  printf '%s\t%s\n' "$(date --iso-8601=ns)" "$*" >> "$control_log"
}

record_phase() {
  printf '%s\t%s\t%s\n' \
    "$phase" "$(date +%s%N)" "$(date --iso-8601=ns)" >> "$timing_file"
}

perf_command() {
  local command="$1"
  local label="$2"
  local control_fifo="$3"
  local ack_fifo="$4"
  local response=""
  local ack_fd

  [[ -n "$control_fifo" ]] || return 0
  [[ -p "$control_fifo" ]] || {
    log_control "$label $command failed: missing control FIFO $control_fifo"
    return 1
  }
  [[ -p "$ack_fifo" ]] || {
    log_control "$label $command failed: missing acknowledgement FIFO $ack_fifo"
    return 1
  }

  exec {ack_fd}<>"$ack_fifo"
  printf '%s\n' "$command" > "$control_fifo"
  if ! IFS= read -r -t 15 response <&"$ack_fd"; then
    exec {ack_fd}>&-
    log_control "$label $command failed: profiler acknowledgement timed out"
    return 1
  fi
  exec {ack_fd}>&-
  if [[ "$response" != "ack" ]]; then
    log_control "$label $command failed: unexpected acknowledgement '$response'"
    return 1
  fi
  log_control "$label $command acknowledged"
}

control_perf() {
  local command="$1"
  perf_command "$command" "perf-record" \
    "${ATHENA_PROFILE_PERF_RECORD_CTL:-}" \
    "${ATHENA_PROFILE_PERF_RECORD_ACK:-}"
  perf_command "$command" "perf-stat" \
    "${ATHENA_PROFILE_PERF_STAT_CTL:-}" \
    "${ATHENA_PROFILE_PERF_STAT_ACK:-}"
}

control_vtune() {
  local command="$1"
  local vtune_bin="${ATHENA_PROFILE_VTUNE_BIN:?ATHENA_PROFILE_VTUNE_BIN is not set}"
  local result_dir="${ATHENA_PROFILE_VTUNE_RESULT:?ATHENA_PROFILE_VTUNE_RESULT is not set}"

  "$vtune_bin" -quiet -command "$command" -result-dir "$result_dir" \
    >> "$control_log" 2>&1
  log_control "vtune $command acknowledged"
}

backend="${ATHENA_PROFILE_BACKEND:?ATHENA_PROFILE_BACKEND is not set}"
if [[ "$phase" == "start" ]]; then
  case "$backend" in
    timing) ;;
    perf) control_perf enable ;;
    vtune) control_vtune resume ;;
    *)
      log_control "unsupported backend '$backend'"
      exit 2
      ;;
  esac
  record_phase
elif [[ "$phase" == "finish" ]]; then
  record_phase
  case "$backend" in
    timing) ;;
    perf) control_perf disable ;;
    vtune) control_vtune pause ;;
  esac
else
  record_phase
  if [[ "$phase" == "buffer-loaded" &&
        -n "${ATHENA_PROFILE_FIRST_PAINT_ARM_FILE:-}" ]]; then
    : > "$ATHENA_PROFILE_FIRST_PAINT_ARM_FILE"
  fi
fi
