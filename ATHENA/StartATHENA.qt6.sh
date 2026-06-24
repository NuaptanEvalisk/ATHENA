#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir"

#export QT_QPA_PLATFORM=xcb
export QT_IM_MODULE=fcitx
export GTK_IM_MODULE=fcitx
export XMODIFIERS=@im=fcitx
#export SDL_VIDEODRIVER=x11
export QT_FONT_DPI=145
#export QT_AUTO_SCREEN_SCALE_FACTOR=0
export QT_AUTO_SCREEN_SCALE_FACTOR=1
export QT_SCALE_FACTOR=1
export QT_ENABLE_HIDPI_SCALING=1
export LD_LIBRARY_PATH="$script_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

exec ./bin/ATHENA.bin --platform xcb "$@"
