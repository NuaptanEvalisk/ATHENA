#!/usr/bin/env bash
set -euo pipefail

export ATHENA_PATH=/opt/ATHENA
private_guile=/opt/ATHENA/lib/athena-guile
export ATHENA_GUILE_RUNTIME_ROOT="$private_guile"
export LD_LIBRARY_PATH="$private_guile/lib:/opt/ATHENA/lib:/opt/ATHENA/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

export QT_QPA_PLATFORM=wayland
export QT_PLUGIN_PATH=/opt/ATHENA/plugins
export QT_QPA_PLATFORM_PLUGIN_PATH=/opt/ATHENA/plugins/platforms
export QT_AUTO_SCREEN_SCALE_FACTOR=0
unset QT_SCALE_FACTOR
unset QT_SCREEN_SCALE_FACTORS
unset QT_FONT_DPI
unset QT_SCALE_FACTOR_ROUNDING_POLICY

unset QT_IM_MODULE
unset GTK_IM_MODULE
unset XMODIFIERS

shopt -s nullglob
magick_config_dirs=(/opt/ATHENA/lib64/ImageMagick-*/config-*)
magick_coder_dirs=(/opt/ATHENA/lib64/ImageMagick-*/modules-*/coders)
if ((${#magick_config_dirs[@]})); then
  export MAGICK_CONFIGURE_PATH="${magick_config_dirs[0]}${MAGICK_CONFIGURE_PATH:+:$MAGICK_CONFIGURE_PATH}"
fi
if ((${#magick_coder_dirs[@]})); then
  export MAGICK_CODER_MODULE_PATH="${magick_coder_dirs[0]}${MAGICK_CODER_MODULE_PATH:+:$MAGICK_CODER_MODULE_PATH}"
fi
unset magick_config_dirs
unset magick_coder_dirs
shopt -u nullglob

export LANG=C.UTF-8
export LC_ALL=C.UTF-8

cd /opt/ATHENA
exec ./bin/ATHENA.bin --platform wayland
