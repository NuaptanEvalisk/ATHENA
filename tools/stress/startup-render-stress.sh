#!/usr/bin/env bash

set -u
set -o pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"

usage() {
  cat <<'EOF'
Start ATHENA repeatedly under Xvfb and reject a solid-color first document.

Usage:
  startup-render-stress.sh [options]

Options:
  --runs N                 Number of launches (default: 100).
  --jobs N                 Concurrent ATHENA launches (default: 1).
  --binary PATH            ATHENA.bin to test (default: ATHENA/bin/ATHENA.bin).
  --athena-home PATH       ATHENA home to snapshot (default: $ATHENA_HOME_PATH
                           or ~/.ATHENA).
  --ready-title REGEX      ERE identifying the loaded first buffer
                           (default: Universe).
  --timeout-seconds N      Per-launch title/load timeout (default: 120).
  --settle-seconds N       Delay after the loaded title appears (default: 2).
  --output PATH            Artifact directory (default: /tmp/athena-startup-*).
  --geometry WIDTHxHEIGHT  ATHENA window geometry (default: 1400x900).
  --help                   Show this help.

Each run gets an independent ATHENA home and Xvfb display, and retains
window.png, canvas.png, title.txt, and stdout-stderr.log. Vault paths stored in
the source home are not rewritten, so concurrent runs still share that vault.
Point --athena-home at a configuration for a disposable vault if concurrent
vault writes are unwanted.
The canvas sample is the central 70% x 58% of the window, starting 15% from
the left and 24% from the top, which excludes ATHENA's normal window chrome.

On a timeout or solid-color canvas the script stops other active runs, exits
nonzero, and deliberately leaves the failing ATHENA and Xvfb alive. The final
diagnostic prints their PIDs and DISPLAY.
EOF
}

runs=100
jobs=1
binary="$repo_root/ATHENA/bin/ATHENA.bin"
source_athena_home="${ATHENA_HOME_PATH:-$HOME/.ATHENA}"
ready_title='Universe'
timeout_seconds=120
settle_seconds=2
output=""
geometry='1400x900'

require_value() {
  if [[ -z "${2:-}" ]]; then
    printf '%s requires a value\n' "$1" >&2
    exit 2
  fi
}

while (($#)); do
  case "$1" in
    --runs)
      require_value "$1" "${2:-}"
      runs="$2"
      shift 2
      ;;
    --jobs)
      require_value "$1" "${2:-}"
      jobs="$2"
      shift 2
      ;;
    --binary)
      require_value "$1" "${2:-}"
      binary="$2"
      shift 2
      ;;
    --athena-home)
      require_value "$1" "${2:-}"
      source_athena_home="$2"
      shift 2
      ;;
    --ready-title)
      require_value "$1" "${2:-}"
      ready_title="$2"
      shift 2
      ;;
    --timeout-seconds)
      require_value "$1" "${2:-}"
      timeout_seconds="$2"
      shift 2
      ;;
    --settle-seconds)
      require_value "$1" "${2:-}"
      settle_seconds="$2"
      shift 2
      ;;
    --output)
      require_value "$1" "${2:-}"
      output="$2"
      shift 2
      ;;
    --geometry)
      require_value "$1" "${2:-}"
      geometry="$2"
      shift 2
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

for value in "$runs" "$jobs" "$timeout_seconds" "$settle_seconds"; do
  [[ "$value" =~ ^[0-9]+$ ]] || {
    printf 'Expected a non-negative integer, got: %s\n' "$value" >&2
    exit 2
  }
done
((runs >= 1)) || { printf '%s\n' '--runs must be at least 1' >&2; exit 2; }
((jobs >= 1)) || { printf '%s\n' '--jobs must be at least 1' >&2; exit 2; }
if ((jobs > runs)); then jobs="$runs"; fi
((timeout_seconds >= 1)) || {
  printf '%s\n' '--timeout-seconds must be at least 1' >&2
  exit 2
}
[[ "$geometry" =~ ^([0-9]+)x([0-9]+)$ ]] || {
  printf 'Invalid --geometry: %s\n' "$geometry" >&2
  exit 2
}
window_width="${BASH_REMATCH[1]}"
window_height="${BASH_REMATCH[2]}"
((window_width >= 640 && window_height >= 480)) || {
  printf '%s\n' '--geometry must be at least 640x480' >&2
  exit 2
}
screen_width=$((window_width + 200))
screen_height=$((window_height + 100))

for command in Xvfb xdotool import magick setsid realpath; do
  command -v "$command" >/dev/null 2>&1 || {
    printf 'Required command is unavailable: %s\n' "$command" >&2
    exit 2
  }
done
printf '' | grep -Eq -- "$ready_title" 2>/dev/null
regex_status=$?
if ((regex_status == 2)); then
  printf 'Invalid --ready-title regular expression: %s\n' "$ready_title" >&2
  exit 2
fi

binary="$(realpath -- "$binary")"
source_athena_home="$(realpath -- "$source_athena_home")"
[[ -x "$binary" ]] || { printf 'Not executable: %s\n' "$binary" >&2; exit 2; }
[[ -d "$source_athena_home" ]] || {
  printf 'ATHENA home does not exist: %s\n' "$source_athena_home" >&2
  exit 2
}

if [[ -z "$output" ]]; then
  output="/tmp/athena-startup-render-stress-$(date +%Y%m%d-%H%M%S)"
fi
if [[ -d "$output" && -n "$(ls -A -- "$output")" ]]; then
  printf 'Output directory is not empty: %s\n' "$output" >&2
  exit 2
fi
mkdir -p -- "$output"
output="$(realpath -- "$output")"

home_template="$output/athena-home-template"
mkdir -p -- "$home_template"
cp -a --reflink=auto -- "$source_athena_home/." "$home_template/"
rm -f -- "$home_template/system/boot_lock"

runtime_root="$repo_root/ATHENA"
binary_dir="$(dirname -- "$binary")"
binary_name="$(basename -- "$binary")"

# Match the installed launcher: a SYCL-enabled llama.cpp build needs oneAPI's
# dnnl, SYCL, and MKL directories in the runtime environment.
oneapi_setup=""
if [[ -e "$runtime_root/lib/libggml-sycl.so.0" ]]; then
  for candidate in /opt/intel/oneapi/setvars.sh \
                   "$HOME/intel/oneapi/setvars.sh"; do
    if [[ -r "$candidate" ]]; then
      oneapi_setup="$candidate"
      set +u
      # shellcheck disable=SC1090
      source "$candidate" --force >/dev/null 2>&1
      set -u
      break
    fi
  done
  if [[ -z "$oneapi_setup" ]]; then
    printf '%s\n' \
      'This ATHENA build uses SYCL, but no readable oneAPI setvars.sh was found.' >&2
    exit 2
  fi
fi

library_path="$runtime_root/lib"
if [[ -n "${LD_LIBRARY_PATH:-}" ]]; then
  library_path="$library_path:$LD_LIBRARY_PATH"
fi

preserve=0
declare -A run_pids=()
declare -A run_dirs=()
declare -A run_windows=()
declare -A run_deadlines=()
declare -A run_ready_at=()
declare -A run_xvfb_pids=()
declare -A run_displays=()

stop_process_group() {
  local pid="$1"
  ((pid > 0)) || return 0
  if kill -0 "$pid" 2>/dev/null; then
    kill -TERM -- "-$pid" 2>/dev/null || true
    for _ in {1..30}; do
      kill -0 "$pid" 2>/dev/null || break
      sleep 0.1
    done
    if kill -0 "$pid" 2>/dev/null; then
      kill -KILL -- "-$pid" 2>/dev/null || true
    fi
  fi
  wait "$pid" 2>/dev/null || true
}

stop_xvfb() {
  local pid="$1"
  ((pid > 0)) || return 0
  if kill -0 "$pid" 2>/dev/null; then
    kill -TERM "$pid" 2>/dev/null || true
  fi
  wait "$pid" 2>/dev/null || true
}

cleanup() {
  if ((preserve)); then return; fi
  local pid
  for pid in "${run_pids[@]}"; do
    stop_process_group "$pid"
  done
  for pid in "${run_xvfb_pids[@]}"; do
    stop_xvfb "$pid"
  done
}

interrupted() {
  printf '\nInterrupted.\n' >&2
  exit 130
}

trap cleanup EXIT
trap interrupted INT TERM

capture_failure_state() {
  local run="$1"
  local run_dir="$2"
  local pid="$3"
  local reason="$4"
  local xvfb_pid="${run_xvfb_pids[$run]}"
  local display="${run_displays[$run]}"
  local window title geometry_output

  {
    printf 'reason=%s\n' "$reason"
    printf 'ATHENA_PID=%d\n' "$pid"
    printf 'XVFB_PID=%d\n' "$xvfb_pid"
    printf 'DISPLAY=%s\n' "$display"
    printf 'RUN_DIR=%s\n' "$run_dir"
  } > "$output/FAILURE.txt"

  : > "$run_dir/windows.txt"
  while IFS= read -r window; do
    [[ -n "$window" ]] || continue
    title="$(DISPLAY="$display" \
      xdotool getwindowname "$window" 2>/dev/null || true)"
    geometry_output="$(DISPLAY="$display" \
      xdotool getwindowgeometry --shell "$window" 2>/dev/null || true)"
    {
      printf 'WINDOW=%s\nTITLE=%s\n' "$window" "$title"
      printf '%s\n\n' "$geometry_output"
    } >> "$run_dir/windows.txt"
  done < <(DISPLAY="$display" \
    xdotool search --onlyvisible --pid "$pid" 2>/dev/null || true)

  DISPLAY="$display" import -silent -window root \
    "$run_dir/xvfb-root.png" 2>/dev/null || true
  ps -L -o pid,tid,ppid,pgid,stat,comm,wchan:32 -p "$pid" \
    > "$run_dir/threads.txt" 2>/dev/null || true
  printf 'DISPLAY=%q gdb -p %d %q\n' \
    "$display" "$pid" "$binary" > "$run_dir/attach-command.sh"
  chmod +x "$run_dir/attach-command.sh"
}

stop_other_runs() {
  local failed_run="$1"
  local active_run
  for active_run in "${!run_pids[@]}"; do
    [[ "$active_run" == "$failed_run" ]] && continue
    stop_process_group "${run_pids[$active_run]}"
    stop_xvfb "${run_xvfb_pids[$active_run]}"
    unset 'run_pids[$active_run]' 'run_dirs[$active_run]' \
      'run_windows[$active_run]' 'run_deadlines[$active_run]' \
      'run_ready_at[$active_run]' 'run_xvfb_pids[$active_run]' \
      'run_displays[$active_run]'
  done
}

preserve_failure() {
  local run="$1"
  local run_dir="$2"
  local reason="$3"
  local pid="${run_pids[$run]}"
  local xvfb_pid="${run_xvfb_pids[$run]}"
  local display="${run_displays[$run]}"

  stop_other_runs "$run"
  preserve=1
  capture_failure_state "$run" "$run_dir" "$pid" "$reason"
  printf '\n\nFAIL: %s\n' "$reason" >&2
  printf 'ATHENA PID: %d (process group %d)\n' \
    "$pid" "$pid" >&2
  printf 'Xvfb PID:   %d\nDISPLAY:    %s\nArtifacts:  %s\n' \
    "$xvfb_pid" "$display" "$run_dir" >&2
  printf 'Both processes were left alive for attachment and inspection.\n' >&2
  exit 1
}

abort_run() {
  local run="$1"
  local reason="$2"
  local pid="${run_pids[$run]}"
  local run_dir="${run_dirs[$run]}"

  capture_failure_state "$run" "$run_dir" "$pid" "$reason"
  printf '\n\nFAIL: %s\nArtifacts: %s\n' "$reason" "$run_dir" >&2
  exit 1
}

bar_width=40
draw_progress() {
  local completed="$1"
  local phase="$2"
  local filled=$((completed * bar_width / runs))
  local empty=$((bar_width - filled))
  local left right
  printf -v left '%*s' "$filled" ''
  printf -v right '%*s' "$empty" ''
  left="${left// /#}"
  right="${right// /-}"
  printf '\r[%s%s] %3d/%-3d  %-34s' \
    "$left" "$right" "$completed" "$runs" "$phase"
}

largest_matching_window() {
  local pid="$1"
  local display="$2"
  local best_window=""
  local best_area=0
  local window geometry_output width height area title

  while IFS= read -r window; do
    [[ -n "$window" ]] || continue
    title="$(DISPLAY="$display" xdotool getwindowname "$window" 2>/dev/null || true)"
    printf '%s\n' "$title" | grep -Eq -- "$ready_title" || continue
    geometry_output="$(DISPLAY="$display" \
      xdotool getwindowgeometry --shell "$window" 2>/dev/null || true)"
    width="$(printf '%s\n' "$geometry_output" |
      sed -n 's/^WIDTH=//p' | head -n 1)"
    height="$(printf '%s\n' "$geometry_output" |
      sed -n 's/^HEIGHT=//p' | head -n 1)"
    [[ "$width" =~ ^[0-9]+$ && "$height" =~ ^[0-9]+$ ]] || continue
    area=$((width * height))
    if ((area > best_area)); then
      best_area="$area"
      best_window="$window"
    fi
  done < <(DISPLAY="$display" xdotool search --onlyvisible --pid "$pid" 2>/dev/null || true)

  printf '%s' "$best_window"
}

cat > "$output/metadata.txt" <<EOF
created=$(date --iso-8601=seconds)
git_commit=$(git -C "$repo_root" rev-parse HEAD 2>/dev/null || printf unknown)
binary=$binary
runs=$runs
jobs=$jobs
source_athena_home=$source_athena_home
athena_home_template=$home_template
ready_title=$ready_title
timeout_seconds=$timeout_seconds
settle_seconds=$settle_seconds
geometry=$geometry
display_policy=one private Xvfb per run
oneapi_setup=$oneapi_setup
canvas_crop=70%x58%+15%+24%
EOF
printf 'run\tpid\twindow\tunique_colors\ttitle\n' > "$output/results.tsv"

launch_run() {
  local run="$1"
  local run_dir
  run_dir="$output/run-$(printf '%03d' "$run")"
  local run_home="$run_dir/athena-home"
  local display_file="$run_dir/xvfb-display"
  local xvfb_log="$run_dir/xvfb.log"
  local pid xvfb_pid display

  mkdir -p -- "$run_dir"
  mkdir -p -- "$run_home"
  cp -a --reflink=auto -- "$home_template/." "$run_home/"
  rm -f -- "$run_home/system/boot_lock"

  : > "$display_file"
  Xvfb -displayfd 3 \
    -screen 0 "${screen_width}x${screen_height}x24" -nolisten tcp \
    >"$xvfb_log" 2>&1 3>"$display_file" &
  xvfb_pid=$!
  run_xvfb_pids[$run]="$xvfb_pid"
  for _ in {1..100}; do
    [[ -s "$display_file" ]] && break
    if ! kill -0 "$xvfb_pid" 2>/dev/null; then
      printf 'Xvfb exited during run %d startup; see %s\n' \
        "$run" "$xvfb_log" >&2
      return 1
    fi
    sleep 0.05
  done
  if [[ ! -s "$display_file" ]]; then
    printf 'Timed out waiting for run %d Xvfb; see %s\n' \
      "$run" "$xvfb_log" >&2
    return 1
  fi
  display=":$(tr -d '[:space:]' < "$display_file")"
  run_displays[$run]="$display"
  if ! DISPLAY="$display" xdotool getmouselocation >/dev/null 2>&1; then
    printf 'Run %d Xvfb display is not usable: %s\n' "$run" "$display" >&2
    return 1
  fi

  (
    cd -- "$binary_dir" || exit 1
    exec setsid env \
      DISPLAY="$display" \
      QT_QPA_PLATFORM=xcb \
      QT_AUTO_SCREEN_SCALE_FACTOR=0 \
      QT_SCALE_FACTOR=1 \
      QT_FONT_DPI=96 \
      ATHENA_PATH="$runtime_root" \
      ATHENA_BIN_PATH="$(dirname -- "$binary")" \
      ATHENA_HOME_PATH="$run_home" \
      LD_LIBRARY_PATH="$library_path" \
      "./$binary_name" --platform xcb -g "$geometry" \
        -d -debug-bench -log-file "$run_dir/athena.log"
  ) >"$run_dir/stdout-stderr.log" 2>&1 &
  pid=$!
  printf '%s\n' "$pid" > "$run_dir/athena.pid"
  run_pids[$run]="$pid"
  run_dirs[$run]="$run_dir"
  run_windows[$run]=""
  run_deadlines[$run]=$((SECONDS + timeout_seconds))
  run_ready_at[$run]=0
}

complete_run() {
  local run="$1"
  local pid="${run_pids[$run]}"
  local run_dir="${run_dirs[$run]}"
  local window="${run_windows[$run]}"
  local display="${run_displays[$run]}"
  local xvfb_pid="${run_xvfb_pids[$run]}"
  local title root_image full_image canvas_image geometry_output
  local window_x window_y image_width image_height
  local crop_x crop_y crop_width crop_height unique_colors

  title="$(DISPLAY="$display" xdotool getwindowname "$window" 2>/dev/null || true)"
  printf '%s\n' "$title" > "$run_dir/title.txt"
  printf '%s\n' "$window" > "$run_dir/window.id"

  root_image="$run_dir/xvfb-root.png"
  full_image="$run_dir/window.png"
  canvas_image="$run_dir/canvas.png"
  geometry_output="$(DISPLAY="$display" \
    xdotool getwindowgeometry --shell "$window" 2>/dev/null || true)"
  window_x="$(printf '%s\n' "$geometry_output" |
    sed -n 's/^X=//p' | head -n 1)"
  window_y="$(printf '%s\n' "$geometry_output" |
    sed -n 's/^Y=//p' | head -n 1)"
  image_width="$(printf '%s\n' "$geometry_output" |
    sed -n 's/^WIDTH=//p' | head -n 1)"
  image_height="$(printf '%s\n' "$geometry_output" |
    sed -n 's/^HEIGHT=//p' | head -n 1)"
  if [[ ! "$window_x" =~ ^[0-9]+$ || ! "$window_y" =~ ^[0-9]+$ ||
        ! "$image_width" =~ ^[0-9]+$ || ! "$image_height" =~ ^[0-9]+$ ]]; then
    abort_run "$run" "could not determine ATHENA window geometry in run $run"
  fi
  if ! DISPLAY="$display" import -silent -window root "$root_image"; then
    abort_run "$run" "could not capture the Xvfb framebuffer in run $run"
  fi
  if ! magick "$root_image" \
      -crop "${image_width}x${image_height}+${window_x}+${window_y}" +repage \
      "$full_image"; then
    abort_run "$run" "could not crop the ATHENA window in run $run"
  fi
  crop_x=$((image_width * 15 / 100))
  crop_y=$((image_height * 24 / 100))
  crop_width=$((image_width * 70 / 100))
  crop_height=$((image_height * 58 / 100))
  if ((crop_width < 200 || crop_height < 150)); then
    abort_run "$run" \
      "captured window in run $run is unexpectedly small: ${image_width}x${image_height}"
  fi
  magick "$full_image" \
    -crop "${crop_width}x${crop_height}+${crop_x}+${crop_y}" +repage \
    "$canvas_image"
  unique_colors="$(magick "$canvas_image" -alpha off -format '%k' info:)"
  printf '%d\t%d\t%s\t%s\t%s\n' \
    "$run" "$pid" "$window" "$unique_colors" "$title" \
    >> "$output/results.tsv"

  if [[ "$unique_colors" == "1" ]]; then
    preserve_failure "$run" "$run_dir" \
      "run $run produced a solid-color canvas"
  fi

  stop_process_group "$pid"
  stop_xvfb "$xvfb_pid"
  unset 'run_pids[$run]' 'run_dirs[$run]' 'run_windows[$run]' \
    'run_deadlines[$run]' 'run_ready_at[$run]' \
    'run_xvfb_pids[$run]' 'run_displays[$run]'
  last_result="run $run passed ($unique_colors colors)"
}

completed=0
next_run=1
last_result="starting"
while ((completed < runs)); do
  while ((next_run <= runs && ${#run_pids[@]} < jobs)); do
    draw_progress "$completed" "launching run $next_run"
    if ! launch_run "$next_run"; then
      exit 2
    fi
    ((next_run++))
  done

  made_progress=0
  for run in "${!run_pids[@]}"; do
    pid="${run_pids[$run]}"
    run_dir="${run_dirs[$run]}"
    display="${run_displays[$run]}"
    if ! kill -0 "$pid" 2>/dev/null; then
      wait "$pid" 2>/dev/null
      status=$?
      abort_run "$run" \
        "ATHENA exited in run $run with status $status"
    fi

    window="${run_windows[$run]}"
    if [[ -z "$window" ]]; then
      window="$(largest_matching_window "$pid" "$display")"
      if [[ -n "$window" ]]; then
        run_windows[$run]="$window"
        run_ready_at[$run]=$((SECONDS + settle_seconds))
        made_progress=1
      elif ((SECONDS >= run_deadlines[$run])); then
        preserve_failure "$run" "$run_dir" \
          "run $run timed out waiting for a title matching /$ready_title/"
      fi
      continue
    fi

    if ((SECONDS >= run_ready_at[$run])); then
      complete_run "$run"
      ((completed++))
      made_progress=1
    fi
  done

  draw_progress "$completed" \
    "$last_result; ${#run_pids[@]} active"
  if ((made_progress == 0)); then sleep 0.1; fi
done

printf '\nPASS: all %d startup renders contained non-background pixels.\n' "$runs"
printf 'Artifacts: %s\n' "$output"
