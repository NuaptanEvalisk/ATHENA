#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir"

# On native Wayland, let Qt use its built-in Wayland input context.  Forcing
# QT_IM_MODULE=fcitx loads the fcitx Qt plugin instead and breaks fcitx5
# preedit/candidate geometry in native Qt line edits.
unset QT_IM_MODULE
unset GTK_IM_MODULE
unset XMODIFIERS
# Let KDE/KWin Display Configuration provide the Wayland output scale to Qt.
# Clear Qt test/override scale knobs that replace or multiply the compositor scale.
export QT_AUTO_SCREEN_SCALE_FACTOR=0
unset QT_SCALE_FACTOR
unset QT_SCREEN_SCALE_FACTORS
unset QT_FONT_DPI
unset QT_SCALE_FACTOR_ROUNDING_POLICY
export LD_LIBRARY_PATH="$script_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# A locally installed SYCL-enabled llama.cpp needs the matching oneAPI runtime.
# Generic release builds do not ship libggml-sycl and skip this block.
if [[ -e "$script_dir/lib/libggml-sycl.so.0" ]]; then
  for oneapi_setup in /opt/intel/oneapi/setvars.sh "$HOME/intel/oneapi/setvars.sh"; do
    if [[ -r "$oneapi_setup" ]]; then
      set +u
      source "$oneapi_setup" >/dev/null 2>&1
      set -u
      break
    fi
  done
fi

# Keep ATHENA's TSan-compatible TCM blocker ahead of the oneAPI runtime.
export LD_LIBRARY_PATH="$script_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export TCM_ENABLE=0

exec ./bin/ATHENA.bin --platform wayland "$@"
