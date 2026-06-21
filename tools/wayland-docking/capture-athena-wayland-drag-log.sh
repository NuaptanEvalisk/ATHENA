#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd -- "$script_dir/../.." && pwd)"
athena_dir="$repo_dir/ATHENA"

timestamp="$(date +%Y%m%d-%H%M%S)"
log_dir="${ATHENA_WAYLAND_LOG_DIR:-/tmp/athena-wayland-drag-log-$timestamp}"
mkdir -p "$log_dir"

stdout_log="$log_dir/athena.stdout.log"
stderr_log="$log_dir/athena.stderr-wayland.log"
env_log="$log_dir/environment.log"
ads_tmp_log="/tmp/athena-ads-giant-rendering.log"
ads_log="$log_dir/ads-giant-rendering.log"
protocol_focus_log="$log_dir/wayland-protocol-focus.log"
qt_focus_log="$log_dir/qt-athena-focus.log"

rm -f "$ads_tmp_log"

export QT_IM_MODULE=fcitx
export GTK_IM_MODULE=fcitx
export XMODIFIERS=@im=fcitx
export QT_AUTO_SCREEN_SCALE_FACTOR=1
export QT_ENABLE_HIDPI_SCALING=1
export LD_LIBRARY_PATH="$athena_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

export WAYLAND_DEBUG="${WAYLAND_DEBUG:-1}"
export ATHENA_ADS_WAYLAND_DEBUG="${ATHENA_ADS_WAYLAND_DEBUG:-1}"
export ATHENA_SCALE_DEBUG="${ATHENA_SCALE_DEBUG:-1}"

qt_rules="qt.qpa.wayland=true;qt.qpa.wayland.*=true;qt.qpa.backingstore=true"
if [[ -n "${QT_LOGGING_RULES:-}" ]]; then
  export QT_LOGGING_RULES="${QT_LOGGING_RULES};${qt_rules}"
else
  export QT_LOGGING_RULES="$qt_rules"
fi

{
  echo "date=$(date --iso-8601=seconds)"
  echo "repo_dir=$repo_dir"
  echo "athena_dir=$athena_dir"
  echo "log_dir=$log_dir"
  echo "binary=$athena_dir/bin/ATHENA.bin"
  echo "platform=wayland"
  echo
  env | sort | rg '^(ATHENA_|QT_|WAYLAND|XDG_|KDE|GTK_|GDK_|XMODIFIERS|LD_LIBRARY_PATH)=' || true
} > "$env_log"

echo "ATHENA Wayland drag logging directory:"
echo "$log_dir"
echo
echo "Reproduce the drag-out gigantic rendering bug, then close ATHENA."
echo "Raw Wayland/Qt stderr: $stderr_log"
echo

set +e
(
  cd "$athena_dir" || exit 1
  ./bin/ATHENA.bin --platform wayland "$@"
) > "$stdout_log" 2> "$stderr_log"
status=$?
set -e

if [[ -f "$ads_tmp_log" ]]; then
  cp -f "$ads_tmp_log" "$ads_log"
fi

rg 'xdg_toplevel_drag|wp_viewport|fractional_scale|wp_fractional|preferred_scale|set_buffer_scale|wl_surface@[0-9]+\.(enter|leave|commit|attach|damage|damage_buffer|set_buffer_scale)|xdg_surface@[0-9]+\.(configure|ack_configure)|xdg_toplevel@[0-9]+\.(set_title|configure|set_app_id)|wl_data_(device|source|offer)@' \
  "$stderr_log" > "$protocol_focus_log" || true

rg 'ATHENA_GIANT|ATHENA_ADS_WAYLAND|ATHENA_SCALE|qt\.qpa\.wayland|QWayland|backingstore|devicePixelRatio|DPR|viewport|scale' \
  "$stderr_log" > "$qt_focus_log" || true

{
  echo "exit_status=$status"
  echo "stdout_log=$stdout_log"
  echo "stderr_log=$stderr_log"
  echo "environment_log=$env_log"
  echo "ads_log=$ads_log"
  echo "protocol_focus_log=$protocol_focus_log"
  echo "qt_focus_log=$qt_focus_log"
} > "$log_dir/README.log"

echo
echo "ATHENA exited with status $status"
echo "Logs captured in:"
echo "$log_dir"
echo
echo "Key files:"
echo "$protocol_focus_log"
echo "$qt_focus_log"
echo "$ads_log"

exit "$status"
