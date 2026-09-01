#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"
control_helper="$script_dir/profile-control.sh"

usage() {
  cat <<'EOF'
Profile ATHENA opening one document after explicitly activating its vault.

Usage:
  profile-open-document.sh --vault PATH --document PATH [options]

Required:
  --vault PATH              Vault root containing Vaultfile.json or Vaultfile.
  --document PATH           Absolute path or path relative to the vault root.

Options:
  --backend timing|perf|vtune|both
                            Measurement backend (default: perf). `timing`
                            records real first-paint latency without a profiler.
  --binary PATH             ATHENA binary. Defaults to a suitable build binary.
  --runs N                  Number of measured runs per backend (default: 5).
  --output PATH             Result directory (default: /tmp/athena-open-profile-*).
  --athena-home PATH        ATHENA home to use (default: $ATHENA_HOME_PATH or ~/.ATHENA).
  --isolated-home           Use an empty ATHENA home below the result directory.
  --platform NAME           Qt platform (default: wayland).
  --startup-delay-ms N      Delay before measured work begins (default: 1500).
  --vault-settle-ms N       Let the vault startup page finish (default: 1000).
  --settle-ms N             Post-render guard interval (default: 500).
  --timeout-seconds N       Hard timeout for each run (default: 300).
  --frequency N             perf sampling frequency (default: 499).
  --allow-concurrent        Permit another ATHENA process to remain running.
  --help                    Show this help.

The measured interval contains load-vault-dir, load-buffer, transclusion
resolution, initial typesetting, and initial painting. Startup runs with events
disabled. No UI interaction is required.
EOF
}

backend="perf"
binary=""
vault=""
document=""
runs=5
output=""
source_athena_home="${ATHENA_HOME_PATH:-$HOME/.ATHENA}"
athena_home=""
isolated_home=0
platform="wayland"
startup_delay_ms=1500
vault_settle_ms=1000
settle_ms=500
timeout_seconds=300
frequency=499
allow_concurrent=0

require_value() {
  local option="$1"
  local value="${2:-}"
  if [[ -z "$value" ]]; then
    printf '%s requires a value\n' "$option" >&2
    exit 2
  fi
}

while (($#)); do
  case "$1" in
    --vault)
      require_value "$1" "${2:-}"
      vault="$2"
      shift 2
      ;;
    --document)
      require_value "$1" "${2:-}"
      document="$2"
      shift 2
      ;;
    --backend)
      require_value "$1" "${2:-}"
      backend="$2"
      shift 2
      ;;
    --binary)
      require_value "$1" "${2:-}"
      binary="$2"
      shift 2
      ;;
    --runs)
      require_value "$1" "${2:-}"
      runs="$2"
      shift 2
      ;;
    --output)
      require_value "$1" "${2:-}"
      output="$2"
      shift 2
      ;;
    --athena-home)
      require_value "$1" "${2:-}"
      source_athena_home="$2"
      shift 2
      ;;
    --isolated-home)
      isolated_home=1
      shift
      ;;
    --platform)
      require_value "$1" "${2:-}"
      platform="$2"
      shift 2
      ;;
    --startup-delay-ms)
      require_value "$1" "${2:-}"
      startup_delay_ms="$2"
      shift 2
      ;;
    --vault-settle-ms)
      require_value "$1" "${2:-}"
      vault_settle_ms="$2"
      shift 2
      ;;
    --settle-ms)
      require_value "$1" "${2:-}"
      settle_ms="$2"
      shift 2
      ;;
    --timeout-seconds)
      require_value "$1" "${2:-}"
      timeout_seconds="$2"
      shift 2
      ;;
    --frequency)
      require_value "$1" "${2:-}"
      frequency="$2"
      shift 2
      ;;
    --allow-concurrent)
      allow_concurrent=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      printf 'Unknown option: %s\n' "$1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

for numeric in "$runs" "$startup_delay_ms" "$vault_settle_ms" "$settle_ms" \
               "$timeout_seconds" "$frequency"; do
  [[ "$numeric" =~ ^[0-9]+$ ]] || {
    printf 'Expected a non-negative integer, got: %s\n' "$numeric" >&2
    exit 2
  }
done
((runs >= 1)) || { printf '%s\n' '--runs must be at least 1' >&2; exit 2; }
((timeout_seconds >= 1)) || {
  printf '%s\n' '--timeout-seconds must be at least 1' >&2
  exit 2
}
((frequency >= 1)) || {
  printf '%s\n' '--frequency must be at least 1' >&2
  exit 2
}

case "$backend" in
  timing|perf|vtune|both) ;;
  *)
    printf 'Unsupported backend: %s\n' "$backend" >&2
    exit 2
    ;;
esac

[[ -n "$vault" ]] || { printf '%s\n' '--vault is required' >&2; exit 2; }
[[ -n "$document" ]] || {
  printf '%s\n' '--document is required' >&2
  exit 2
}

vault="$(realpath -- "$vault")"
if [[ "$document" = /* ]]; then
  document="$(realpath -- "$document")"
else
  document="$(realpath -- "$vault/$document")"
fi

[[ -d "$vault" ]] || { printf 'Vault does not exist: %s\n' "$vault" >&2; exit 2; }
if [[ ! -f "$vault/Vaultfile.json" && ! -f "$vault/Vaultfile" ]]; then
  printf 'Not an ATHENA vault (no Vaultfile.json or Vaultfile): %s\n' "$vault" >&2
  exit 2
fi
[[ -f "$document" ]] || {
  printf 'Document does not exist: %s\n' "$document" >&2
  exit 2
}
case "$document" in
  "$vault"/*) ;;
  *)
    printf 'Document must be inside the selected vault: %s\n' "$document" >&2
    exit 2
    ;;
esac

if [[ -z "$binary" ]]; then
  for candidate in \
    "$repo_root/build_rel/src/ATHENA.bin" \
    "$repo_root/build_qt6/src/ATHENA.bin" \
    "$repo_root/ATHENA/bin/ATHENA.bin"; do
    if [[ -x "$candidate" && ( -z "$binary" || "$candidate" -nt "$binary" ) ]]; then
      binary="$candidate"
    fi
  done
fi
[[ -n "$binary" ]] || {
  printf '%s\n' 'No ATHENA binary found; pass --binary PATH' >&2
  exit 2
}
binary="$(realpath -- "$binary")"
[[ -x "$binary" ]] || { printf 'Not executable: %s\n' "$binary" >&2; exit 2; }
[[ -x "$control_helper" ]] || {
  printf 'Profiling controller is not executable: %s\n' "$control_helper" >&2
  exit 2
}

if ((allow_concurrent == 0)) &&
   { pgrep -x ATHENA.bin >/dev/null 2>&1 ||
     pgrep -x ATHENA >/dev/null 2>&1; }; then
  cat >&2 <<'EOF'
Another ATHENA process is running. Close it before profiling so preferences,
vault state, CPU samples, and timings are not contaminated. Use
--allow-concurrent only when this is intentional.
EOF
  exit 2
fi

if [[ -z "$output" ]]; then
  output="/tmp/athena-open-profile-$(date +%Y%m%d-%H%M%S)"
fi
if [[ -d "$output" && -n "$(ls -A -- "$output")" ]]; then
  printf 'Output directory is not empty: %s\n' "$output" >&2
  exit 2
fi
mkdir -p -- "$output"
output="$(realpath -- "$output")"

athena_home="$output/athena-home"
if ((isolated_home)); then
  mkdir -p -- "$athena_home/fonts/error" "$athena_home/progs" \
    "$athena_home/system/cache" "$athena_home/system/tmp"
else
  source_athena_home="$(realpath -- "$source_athena_home")"
  [[ -d "$source_athena_home" ]] || {
    printf 'ATHENA home does not exist: %s\n' "$source_athena_home" >&2
    exit 2
  }
  mkdir -p -- "$athena_home"
  cp -a --reflink=auto -- "$source_athena_home/." "$athena_home/"
fi
athena_home="$(realpath -- "$athena_home")"

perf_bin="$(command -v perf || true)"
vtune_bin=""
for candidate in \
  "$(command -v vtune || true)" \
  /opt/intel/oneapi/vtune/latest/bin64/vtune; do
  if [[ -n "$candidate" && -x "$candidate" ]]; then
    vtune_bin="$candidate"
    break
  fi
done

if [[ "$backend" == "perf" || "$backend" == "both" ]]; then
  [[ -n "$perf_bin" ]] || { printf '%s\n' 'perf is not installed' >&2; exit 2; }
fi
if [[ "$backend" == "vtune" || "$backend" == "both" ]]; then
  [[ -n "$vtune_bin" ]] || {
    printf '%s\n' 'VTune CLI was not found' >&2
    exit 2
  }
fi

scheme_quote() {
  local value="$1"
  value="${value//\\/\\\\}"
  value="${value//\"/\\\"}"
  printf '"%s"' "$value"
}

helper_start_scheme="$(scheme_quote "$control_helper start")"
helper_vault_scheme="$(scheme_quote "$control_helper vault-loaded")"
helper_vault_settled_scheme="$(scheme_quote "$control_helper vault-settled")"
helper_buffer_scheme="$(scheme_quote "$control_helper buffer-loaded")"
helper_finish_scheme="$(scheme_quote "$control_helper finish")"
vault_scheme="$(scheme_quote "$vault")"
document_scheme="$(scheme_quote "$document")"
scheme_command="(delayed (:pause $startup_delay_ms) (begin
  (system $helper_start_scheme)
  (load-vault-dir (system->url $vault_scheme))
  (system $helper_vault_scheme)
  (delayed (:pause $vault_settle_ms) (begin
    (system $helper_vault_settled_scheme)
    (load-buffer (system->url $document_scheme))
    (system $helper_buffer_scheme)
    (delayed (:pause $settle_ms) (begin
      (system $helper_finish_scheme)
      (quit-TeXmacs)))))))"

mkdir -p -- "$athena_home/progs"
printf '\n%s\n%s\n%s\n' \
  '(set-preference "check for updates" "off")' \
  '(set-preference "google oauth client id" "")' \
  "$scheme_command" \
  >> "$athena_home/progs/my-init-texmacs.scm"

# Match the normal ATHENA launcher when the local build uses the SYCL llama.cpp
# backend.  Without the oneAPI runtime path, profiling would fail before main.
if [[ -e "$repo_root/ATHENA/lib/libggml-sycl.so.0" ]]; then
  for oneapi_setup in /opt/intel/oneapi/setvars.sh \
                      "$HOME/intel/oneapi/setvars.sh"; do
    if [[ -r "$oneapi_setup" ]]; then
      set +u
      # shellcheck disable=SC1090
      source "$oneapi_setup" --force >/dev/null 2>&1
      set -u
      break
    fi
  done
fi

library_path="$repo_root/ATHENA/lib"
binary_build_dir="$(cd -- "$(dirname -- "$binary")/.." && pwd)"
build_type="unknown"
if [[ -f "$binary_build_dir/CMakeCache.txt" ]]; then
  build_type="$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' \
    "$binary_build_dir/CMakeCache.txt" | head -n 1)"
  [[ -n "$build_type" ]] || build_type="unknown"
fi
if [[ "$build_type" == "Debug" ]]; then
  printf '%s\n' \
    'Warning: profiling a Debug build; use a current RelWithDebInfo build for final numbers.' >&2
fi
if [[ -d "$binary_build_dir/x64/lib" ]]; then
  library_path="$binary_build_dir/x64/lib:$library_path"
fi
if [[ -d "$binary_build_dir/athena-guile-runtime/lib" ]]; then
  library_path="$binary_build_dir/athena-guile-runtime/lib:$library_path"
fi
if [[ -n "${LD_LIBRARY_PATH:-}" ]]; then
  library_path="$library_path:$LD_LIBRARY_PATH"
fi

cat > "$output/metadata.txt" <<EOF
created=$(date --iso-8601=seconds)
git_commit=$(git -C "$repo_root" rev-parse HEAD 2>/dev/null || printf unknown)
binary=$binary
build_type=$build_type
vault=$vault
document=$document
backend=$backend
runs=$runs
platform=$platform
source_athena_home=$source_athena_home
athena_home=$athena_home
isolated_home=$isolated_home
startup_delay_ms=$startup_delay_ms
vault_settle_ms=$vault_settle_ms
settle_ms=$settle_ms
timeout_seconds=$timeout_seconds
perf_frequency=$frequency
kernel=$(uname -srmo)
EOF
lscpu >> "$output/metadata.txt" 2>/dev/null || true

printf 'backend\trun\tvault_ms\tload_buffer_ms\tfirst_paint_ms\topen_to_paint_ms\trecorded_total_ms\n' \
  > "$output/summary.tsv"

summarize_timing() {
  local tool="$1"
  local run="$2"
  local timing_file="$3"
  local start_ns=""
  local vault_ns=""
  local buffer_ns=""
  local vault_settled_ns=""
  local paint_ns=""
  local finish_ns=""
  local first_paint_ms
  local recorded_total_ms
  local open_total_ms
  local vault_us
  local buffer_us
  local render_us
  local finish_us
  local phase ns

  while IFS=$'\t' read -r phase ns _; do
    case "$phase" in
      start) start_ns="$ns" ;;
      vault-loaded) vault_ns="$ns" ;;
      vault-settled) vault_settled_ns="$ns" ;;
      buffer-loaded) buffer_ns="$ns" ;;
      first-paint) paint_ns="$ns" ;;
      finish) finish_ns="$ns" ;;
    esac
  done < "$timing_file"

  if [[ -z "$start_ns" || -z "$vault_ns" || -z "$vault_settled_ns" ||
        -z "$buffer_ns" ||
        -z "$paint_ns" ||
        -z "$finish_ns" ]]; then
    printf 'Incomplete timing markers in %s\n' "$timing_file" >&2
    return 1
  fi

  first_paint_ms="$(((paint_ns - buffer_ns) / 1000000))"
  recorded_total_ms="$(((finish_ns - start_ns -
    (vault_settled_ns - vault_ns)) / 1000000))"
  open_total_ms="$(((vault_ns - start_ns +
    paint_ns - vault_settled_ns) / 1000000))"

  printf '%s\t%d\t%d\t%d\t%d\t%d\t%d\n' \
    "$tool" "$run" \
    "$(((vault_ns - start_ns) / 1000000))" \
    "$(((buffer_ns - vault_settled_ns) / 1000000))" \
    "$first_paint_ms" \
    "$open_total_ms" \
    "$recorded_total_ms" \
    >> "$output/summary.tsv"

  vault_us="$(((vault_ns - start_ns) / 1000))"
  buffer_us="$(((buffer_ns - vault_settled_ns) / 1000))"
  render_us="$((first_paint_ms * 1000))"
  finish_us="$((recorded_total_ms * 1000))"
  cat > "$(dirname -- "$timing_file")/trace.json" <<EOF
{"displayTimeUnit":"ms","traceEvents":[
  {"name":"vault activation","cat":"ATHENA open document","ph":"X","ts":0,"dur":$vault_us,"pid":1,"tid":1},
  {"name":"load buffer","cat":"ATHENA open document","ph":"X","ts":$vault_us,"dur":$buffer_us,"pid":1,"tid":1},
  {"name":"initial typeset and paint","cat":"ATHENA open document","ph":"X","ts":$((vault_us + buffer_us)),"dur":$render_us,"pid":1,"tid":1},
  {"name":"first paint complete","cat":"ATHENA open document","ph":"i","s":"t","ts":$((vault_us + buffer_us + render_us)),"pid":1,"tid":1},
  {"name":"post-render guard","cat":"ATHENA open document","ph":"X","ts":$((vault_us + buffer_us + render_us)),"dur":$((finish_us - vault_us - buffer_us - render_us)),"pid":1,"tid":1},
  {"name":"recording finished","cat":"ATHENA open document","ph":"i","s":"t","ts":$finish_us,"pid":1,"tid":1}
]}
EOF
}

run_perf() {
  local run="$1"
  local run_dir="$output/perf-run-$run"
  local record_ctl="$run_dir/perf-record.ctl"
  local record_ack="$run_dir/perf-record.ack"
  local stat_ctl="$run_dir/perf-stat.ctl"
  local stat_ack="$run_dir/perf-stat.ack"
  local timing_file="$run_dir/timing.tsv"
  local control_log="$run_dir/control.log"
  local first_paint_arm="$run_dir/first-paint.arm"

  mkdir -p -- "$run_dir"
  mkfifo -- "$record_ctl" "$record_ack" "$stat_ctl" "$stat_ack"
  : > "$timing_file"
  : > "$control_log"

  printf 'perf run %d/%d\n' "$run" "$runs"
  set +e
  (
    cd -- "$repo_root/ATHENA"
    timeout --signal=TERM --kill-after=10s "${timeout_seconds}s" \
      env \
        ATHENA_PATH="$repo_root/ATHENA" \
        ATHENA_BIN_PATH="$(dirname -- "$binary")" \
        ATHENA_HOME_PATH="$athena_home" \
        ATHENA_PROFILE_BACKEND=perf \
        ATHENA_PROFILE_TIMING_FILE="$timing_file" \
        ATHENA_PROFILE_FIRST_PAINT_ARM_FILE="$first_paint_arm" \
        ATHENA_PROFILE_CONTROL_LOG="$control_log" \
        ATHENA_PROFILE_PERF_RECORD_CTL="$record_ctl" \
        ATHENA_PROFILE_PERF_RECORD_ACK="$record_ack" \
        ATHENA_PROFILE_PERF_STAT_CTL="$stat_ctl" \
        ATHENA_PROFILE_PERF_STAT_ACK="$stat_ack" \
        LD_LIBRARY_PATH="$library_path" \
      "$perf_bin" stat -d -D -1 \
        --control "fifo:$stat_ctl,$stat_ack" \
        --output "$run_dir/perf-stat.txt" -- \
      "$perf_bin" record -D -1 \
        --control "fifo:$record_ctl,$record_ack" \
        --event cpu_core/cycles/P \
        --event cpu_atom/cycles/P \
        --freq "$frequency" \
        --call-graph dwarf,16384 \
        --timestamp --sample-cpu --compression-level=1 \
        --output "$run_dir/perf.data" -- \
      "$binary" --no-splash-screen -debug-bench \
        -log-file "$run_dir/athena.log" \
        --platform "$platform"
  ) > "$run_dir/stdout-stderr.log" 2>&1
  local status=$?
  set -e

  if ((status != 0)); then
    printf 'perf run %d failed with status %d; see %s\n' \
      "$run" "$status" "$run_dir/stdout-stderr.log" >&2
    return "$status"
  fi

  "$perf_bin" report --stdio --input "$run_dir/perf.data" \
    --no-children --sort comm,dso,symbol --percent-limit 0.5 \
    > "$run_dir/perf-report.txt"
  summarize_timing perf "$run" "$timing_file"
}

run_timing() {
  local run="$1"
  local run_dir="$output/timing-run-$run"
  local timing_file="$run_dir/timing.tsv"
  local control_log="$run_dir/control.log"
  local first_paint_arm="$run_dir/first-paint.arm"

  mkdir -p -- "$run_dir"
  : > "$timing_file"
  : > "$control_log"

  printf 'timing run %d/%d\n' "$run" "$runs"
  set +e
  (
    cd -- "$repo_root/ATHENA"
    timeout --signal=TERM --kill-after=10s "${timeout_seconds}s" \
      env \
        ATHENA_PATH="$repo_root/ATHENA" \
        ATHENA_BIN_PATH="$(dirname -- "$binary")" \
        ATHENA_HOME_PATH="$athena_home" \
        ATHENA_PROFILE_BACKEND=timing \
        ATHENA_PROFILE_TIMING_FILE="$timing_file" \
        ATHENA_PROFILE_FIRST_PAINT_ARM_FILE="$first_paint_arm" \
        ATHENA_PROFILE_CONTROL_LOG="$control_log" \
        LD_LIBRARY_PATH="$library_path" \
      "$binary" --no-splash-screen -debug-bench \
        -log-file "$run_dir/athena.log" \
        --platform "$platform"
  ) > "$run_dir/stdout-stderr.log" 2>&1
  local status=$?
  set -e

  if ((status != 0)); then
    printf 'timing run %d failed with status %d; see %s\n' \
      "$run" "$status" "$run_dir/stdout-stderr.log" >&2
    return "$status"
  fi
  summarize_timing timing "$run" "$timing_file"
}

run_vtune() {
  local run="$1"
  local run_dir="$output/vtune-run-$run"
  local result_dir="$run_dir/result"
  local timing_file="$run_dir/timing.tsv"
  local control_log="$run_dir/control.log"
  local first_paint_arm="$run_dir/first-paint.arm"

  mkdir -p -- "$run_dir"
  : > "$timing_file"
  : > "$control_log"

  printf 'VTune run %d/%d\n' "$run" "$runs"
  set +e
  (
    cd -- "$repo_root/ATHENA"
    timeout --signal=TERM --kill-after=10s "${timeout_seconds}s" \
      env \
        ATHENA_PATH="$repo_root/ATHENA" \
        ATHENA_BIN_PATH="$(dirname -- "$binary")" \
        ATHENA_HOME_PATH="$athena_home" \
        ATHENA_PROFILE_BACKEND=vtune \
        ATHENA_PROFILE_TIMING_FILE="$timing_file" \
        ATHENA_PROFILE_FIRST_PAINT_ARM_FILE="$first_paint_arm" \
        ATHENA_PROFILE_CONTROL_LOG="$control_log" \
        ATHENA_PROFILE_VTUNE_BIN="$vtune_bin" \
        ATHENA_PROFILE_VTUNE_RESULT="$result_dir" \
        LD_LIBRARY_PATH="$library_path" \
      "$vtune_bin" -collect hotspots -start-paused \
        -result-dir "$result_dir" -app-working-dir "$repo_root/ATHENA" -- \
      "$binary" --no-splash-screen -debug-bench \
        -log-file "$run_dir/athena.log" \
        --platform "$platform"
  ) > "$run_dir/stdout-stderr.log" 2>&1
  local status=$?
  set -e

  if ((status != 0)); then
    printf 'VTune run %d failed with status %d; see %s\n' \
      "$run" "$status" "$run_dir/stdout-stderr.log" >&2
    return "$status"
  fi

  "$vtune_bin" -quiet -report hotspots -format csv \
    -result-dir "$result_dir" > "$run_dir/hotspots.csv"
  "$vtune_bin" -quiet -report summary -format csv \
    -result-dir "$result_dir" > "$run_dir/summary.csv"
  summarize_timing vtune "$run" "$timing_file"
}

if [[ "$backend" == "timing" ]]; then
  for ((run = 1; run <= runs; ++run)); do
    run_timing "$run"
  done
fi
if [[ "$backend" == "perf" || "$backend" == "both" ]]; then
  for ((run = 1; run <= runs; ++run)); do
    run_perf "$run"
  done
fi
if [[ "$backend" == "vtune" || "$backend" == "both" ]]; then
  for ((run = 1; run <= runs; ++run)); do
    run_vtune "$run"
  done
fi

{
  printf 'backend\truns\tmedian_open_ms\n'
  for tool in timing perf vtune; do
    values="$(awk -F '\t' -v tool="$tool" 'NR > 1 && $1 == tool {print $6}' \
      "$output/summary.tsv" | sort -n)"
    [[ -n "$values" ]] || continue
    count="$(wc -l <<< "$values")"
    median="$(awk -v count="$count" '
      { value[NR] = $1 }
      END {
        if (count % 2) print value[(count + 1) / 2]
        else printf "%.1f\n", (value[count / 2] + value[count / 2 + 1]) / 2
      }' <<< "$values")"
    printf '%s\t%s\t%s\n' "$tool" "$count" "$median"
  done
} > "$output/medians.tsv"

printf '\nProfiling complete: %s\n' "$output"
column -t -s $'\t' "$output/summary.tsv" 2>/dev/null || cat "$output/summary.tsv"
printf '\n'
column -t -s $'\t' "$output/medians.tsv" 2>/dev/null || cat "$output/medians.tsv"
