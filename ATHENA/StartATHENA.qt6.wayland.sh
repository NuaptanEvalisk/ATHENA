#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir"

export QT_IM_MODULE=fcitx
export GTK_IM_MODULE=fcitx
export XMODIFIERS=@im=fcitx
# Let KDE/KWin Display Configuration provide the Wayland output scale to Qt.
# Clear Qt test/override scale knobs that replace or multiply the compositor scale.
export QT_AUTO_SCREEN_SCALE_FACTOR=0
unset QT_SCALE_FACTOR
unset QT_SCREEN_SCALE_FACTORS
unset QT_FONT_DPI
unset QT_SCALE_FACTOR_ROUNDING_POLICY
export LD_LIBRARY_PATH="$script_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

exec ./bin/ATHENA.bin --platform wayland "$@"
