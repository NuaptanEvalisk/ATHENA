#!/usr/bin/env bash
set -euo pipefail

unset \
  http_proxy \
  https_proxy \
  ftp_proxy \
  all_proxy \
  no_proxy \
  HTTP_PROXY \
  HTTPS_PROXY \
  FTP_PROXY \
  ALL_PROXY \
  NO_PROXY

mode="${1:-}"

wait_for_path() {
  local path="$1"
  local attempts="${2:-180}"
  for ((i = 0; i < attempts; i++)); do
    [[ -S "$path" ]] && return 0
    sleep 0.25
  done
  return 1
}

wait_for_tcp() {
  local port="$1"
  local attempts="${2:-180}"
  for ((i = 0; i < attempts; i++)); do
    if { exec 9<>"/dev/tcp/127.0.0.1/$port"; } 2>/dev/null; then
      exec 9>&-
      return 0
    fi
    sleep 0.25
  done
  return 1
}

run_sandbox() {
  local width="${ATHENA_WEB_WIDTH:-1920}"
  local height="${ATHENA_WEB_HEIGHT:-1080}"
  local config_dir="$HOME/.config"
  local desktop="$HOME/Desktop"

  mkdir -p /session-home/home
  cp -a /usr/share/athena-web/home-template/. "$HOME/"
  mkdir -p \
    "$config_dir/glib-2.0" \
    "$HOME/.ATHENA/fonts" \
    "$desktop/Upload" \
    "$desktop/Download"
  chmod 0700 "$HOME"
  chmod 0755 "$desktop" "$desktop/Upload" "$desktop/Download"
  cat >"$desktop/readme.txt" <<'EOF'
Welcome to Web-Accessible ATHENA.

This is a temporary, isolated demonstration environment:

* ATHENA runs as a normal native Wayland desktop application.
* The environment has no access to the host computer, its files, devices, or
  Internet connection.
* Files dropped onto the browser are placed in Desktop/Upload.
* Put files you want to retrieve in Desktop/Download, then use the browser's
  Downloads control.
* The file browser and terminal are available from the Weston top panel.
* sudo and su privileges are not available.
* Closing the browser tab destroys this environment. Sessions otherwise expire
  after the time shown by the browser.
EOF

  cat >"$config_dir/weston.ini" <<EOF
[core]
shell=desktop-shell.so
idle-time=0
require-input=false

[shell]
panel-position=top
locking=false
animation=none

[output]
name=vnc
mode=${width}x${height}
resizeable=false

[launcher]
icon=/opt/ATHENA/misc/images/ATHENA-48.png
path=/usr/local/bin/start-athena

[launcher]
icon=/usr/share/icons/hicolor/48x48/apps/org.xfce.thunar.png
path=/usr/bin/thunar

[launcher]
icon=/usr/share/icons/hicolor/48x48/apps/foot.png
path=/usr/bin/foot
EOF

  export XDG_RUNTIME_DIR=/tmp/runtime
  mkdir -p "$XDG_RUNTIME_DIR"
  chmod 0700 "$XDG_RUNTIME_DIR"
  export WAYLAND_DISPLAY=wayland-0

  dbus-run-session -- /opt/weston/bin/weston \
    --backend=vnc \
    --renderer=pixman \
    --address=127.0.0.1 \
    --port=5900 \
    --width="$width" \
    --height="$height" \
    --disable-transport-layer-security \
    --socket="$WAYLAND_DISPLAY" \
    --config="$config_dir/weston.ini" \
    --log=/tmp/weston.log &
  local weston_pid=$!
  trap "kill '$weston_pid' 2>/dev/null || true" EXIT TERM INT

  wait_for_tcp 5900 || {
    cat /tmp/weston.log >&2
    return 1
  }
  socat \
    UNIX-LISTEN:/run/athena-bridge/vnc.sock,fork,unlink-early,mode=0600 \
    TCP:127.0.0.1:5900 &
  local socat_pid=$!
  trap "kill '$socat_pid' 2>/dev/null || true; \
kill '$weston_pid' 2>/dev/null || true" EXIT TERM INT

  (
    for ((i = 0; i < 180; i++)); do
      [[ -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]] && break
      sleep 0.25
    done
    /usr/local/bin/start-athena
  ) &

  wait "$weston_pid"
  kill "$socat_pid" 2>/dev/null || true
}

run_streamer() {
  : "${ATHENA_WEB_VNC_PORT:?missing VNC bridge port}"
  : "${ATHENA_WEB_SIGNAL_PORT:?missing signaling port}"
  : "${ATHENA_WEB_SESSION_TOKEN:?missing session token}"
  local framerate="${ATHENA_WEB_FRAMERATE:-30}"
  local video_min_bitrate="${ATHENA_WEB_VIDEO_MIN_BITRATE:-4000000}"
  local video_start_bitrate="${ATHENA_WEB_VIDEO_START_BITRATE:-12000000}"
  local video_max_bitrate="${ATHENA_WEB_VIDEO_MAX_BITRATE:-24000000}"
  local stun="${ATHENA_WEB_STUN_SERVER:-}"

  mkdir -p "$HOME"
  wait_for_path /run/athena-bridge/vnc.sock || {
    echo "ATHENA VNC bridge did not become ready" >&2
    return 1
  }
  socat \
    "TCP-LISTEN:${ATHENA_WEB_VNC_PORT},bind=127.0.0.1,reuseaddr,fork" \
    UNIX-CONNECT:/run/athena-bridge/vnc.sock &
  local bridge_pid=$!
  trap "kill '$bridge_pid' 2>/dev/null || true" EXIT TERM INT
  wait_for_tcp "$ATHENA_WEB_VNC_PORT" || return 1

  local -a sink_properties=(
    "run-signalling-server=true"
    "run-web-server=false"
    "signalling-server-host=127.0.0.1"
    "signalling-server-port=${ATHENA_WEB_SIGNAL_PORT}"
    "enable-control-data-channel=true"
    "video-caps=video/x-vp8"
    "min-bitrate=${video_min_bitrate}"
    "start-bitrate=${video_start_bitrate}"
    "max-bitrate=${video_max_bitrate}"
    "enable-mitigation-modes=none"
    "stun-server=$stun"
    "meta=meta,name=ATHENA-${ATHENA_WEB_SESSION_TOKEN}"
  )
  if [[ -n "${ATHENA_WEB_TURN_SERVERS:-}" ]]; then
    local turn_array='<'
    local separator=''
    while IFS= read -r server; do
      [[ -z "$server" ]] && continue
      turn_array+="${separator}\"${server}\""
      separator=', '
    done <<<"$ATHENA_WEB_TURN_SERVERS"
    turn_array+='>'
    sink_properties+=("turn-servers=$turn_array")
  fi

  exec gst-launch-1.0 --no-position --eos-on-shutdown \
    rfbsrc host=127.0.0.1 port="$ATHENA_WEB_VNC_PORT" \
      shared=false view-only=false \
      incremental=true \
    ! videoconvert \
    ! videorate \
    ! "video/x-raw,framerate=${framerate}/1" \
    ! queue leaky=downstream max-size-buffers=2 \
    ! webrtcsink "${sink_properties[@]}"
}

case "$mode" in
  sandbox) run_sandbox ;;
  stream) run_streamer ;;
  *)
    echo "usage: athena-web-container sandbox|stream" >&2
    exit 64
    ;;
esac
