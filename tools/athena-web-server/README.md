# Web-Accessible ATHENA

`athena-web-server` serves a normal local ATHENA process through a browser. It
does not turn ATHENA into a web application: each browser session receives a
dedicated native Wayland desktop, rendered by Weston and transported as WebRTC
video with an input-control data channel.

## Architecture

One browser session consists of two rootless OCI containers:

```text
browser
  | HTTPS/WebSocket signaling through the deployment reverse proxy
  | WebRTC media and input
  v
trusted streamer container (GStreamer webrtcsink)
  | VNC over a mode-0600 private per-session Unix socket capability
  v
network-disabled sandbox container
  |- Weston VNC backend (native virtual Wayland compositor)
  |- ATHENA --platform wayland
  |- Thunar
  `- foot
```

The server brokers lifecycle, capacity, signaling, uploads, and downloads. It
does not terminate TLS. Put Nginx, Cloudflare Tunnel, or another WebSocket-aware
reverse proxy in front of its HTTP listener.

The streamer is separate because WebRTC needs network access while the
untrusted desktop must not have it. The streamer receives no home directory,
host devices, or host files. Its only session-specific mount is the private VNC
bridge. The sandbox uses `--network none`, a read-only image, dropped
capabilities, `no-new-privileges`, private PID/IPC resources, bounded memory,
bounded PIDs, and a size-limited tmpfs home. It receives no host home or display
socket. Closing the browser tab asks the server to destroy both containers and
remove the session state immediately.

The Weston build is deliberately native Wayland and does not include or invoke
Xvfb. openSUSE's packaged Weston omits the VNC backend, so the image builds
Weston 15.0.1 from a pinned source archive. A small patch permits protocol-level
VNC authentication to be disabled only when Weston is explicitly running
behind ATHENA's private bridge. That VNC listener is bound to loopback inside
the network-disabled sandbox and is exported only through a mode-0600,
per-session Unix socket mounted into the trusted streamer. Possession of that
socket mount is the capability boundary. Ordinary Weston VNC authentication
remains unchanged outside this explicit private-bridge mode.

## Build

First produce the release AppImage with the existing container build. It
contains the Guile 1.8 and shared-library closure required by the openSUSE
sandbox. Then build the web image:

```bash
tools/athena-web-server/build-image.sh \
  --runtime container_build/ATHENA-rel.AppImage \
  --image localhost/athena-web:latest
```

The build script reads the AppImage SquashFS directly with `unsquashfs`; it does
not mount or execute the packaged application. The shared release policy copies
the ATHENA executable and runtime libraries but removes GGUF, safetensors, ONNX,
PyTorch weights, generated Python environments, and caches. The image includes
openSUSE, Weston, basic Latin and CJK fonts, Thunar, foot, GStreamer, and the
required WebRTC/VNC plugins.

The image build uses host networking only for its trusted package/source build
steps. Runtime sandbox containers always use `--network none`.

The rootless container account must receive both the `pids` and `memory`
controllers from cgroup v2. On a systemd host, enable memory accounting for its
user slice before starting the broker, then restart that user's login session:

```bash
sudo systemctl set-property user-$(id -u).slice MemoryAccounting=yes
```

`athena-web-server` performs a disposable OCI isolation probe at startup. It
refuses to listen when the runtime cannot enforce the configured memory, PID,
user-namespace, read-only, or network controls; it never falls back to an
unbounded sandbox.

Build the broker with the normal Qt6 tree:

```bash
cmake -S . -B build_qt6 -DATHENA_GUI=Qt6
cmake --build build_qt6 --target athena-web-server
```

## Run

The default listener is loopback-only:

```bash
build_qt6/tools/athena-web-server/athena-web-server \
  --listen-address 127.0.0.1 \
  --port 8090 \
  --image localhost/athena-web:latest \
  --max-connections 4 \
  --max-memory 4GiB \
  --storage-limit 2GiB
```

Important options:

- `--session-seconds`: lifetime added at session creation and each extension;
  default 3600.
- `--warning-seconds`: show the browser extension control this many seconds
  before expiry; default 300.
- `--expired-retention`: retain the final `Desktop/Download` archive after
  expiry; default 900.
- `--heartbeat-timeout`: remove a session whose browser disappeared without a
  close notification; default 45.
- `--width`, `--height`, `--framerate`: virtual desktop dimensions and stream
  frame rate.
- `--video-min-bitrate`, `--video-start-bitrate`, and `--video-max-bitrate`:
  tune the WebRTC congestion-control range in bits per second. The defaults
  are 4, 12, and 24 Mbit/s. ATHENA keeps the virtual desktop resolution fixed
  instead of allowing `webrtcsink` to blur text by downscaling the stream.
- `--stun-server`: one GStreamer/browser STUN URI; pass an empty string to
  disable it.
- `--turn-server`: repeatable TURN URI. Credentials may be included as
  `turn://user:password@host:port`; they are removed from the browser URL and
  supplied through the WebRTC configuration.
- `--cloudflare-turn-key-id` and `--cloudflare-turn-token-file`: use
  Cloudflare Realtime TURN instead of static TURN credentials. The broker
  generates an isolated 48-hour credential for each ATHENA session. The token
  file must be an owner-only regular file and is never passed to the browser or
  either session container.
- `--state-dir`: server-private ephemeral session metadata. Do not place
  unrelated files below this directory.

The browser UI starts the session immediately. Dragged files and the Upload
button write only to `~/Desktop/Upload`. Files placed in
`~/Desktop/Download` can be fetched individually or as a generated archive.
Symlinks and non-regular files are excluded from download materialization.

## Networking

Signaling is ordinary HTTP/WebSocket traffic and can pass through Nginx or a
Cloudflare Tunnel. WebRTC media still needs a usable ICE route:

- direct deployments can advertise a suitable STUN server;
- restrictive NAT or Cloudflare-only deployments require an externally
  reachable TURN service;
- the server's `--turn-server` values must be usable both by GStreamer and the
  browser.

Cloudflare Tunnel alone does not relay arbitrary WebRTC UDP media. Configure
TURN when direct peer connectivity is not expected.

For Cloudflare Realtime TURN, create a dedicated TURN key and store its returned
key token as a mode `0600` file owned by the broker account. Do not store the
Cloudflare account API token in the guest. The browser receives only the
short-lived credential generated for its own session.

## Session Environment

The sandbox identity is:

```text
user:     ATHENA-User
hostname: ATHENA-Experience
```

Its desktop contains `Upload`, `Download`, and `readme.txt`. The root account is
locked, Linux capabilities are removed, and `su` has no setuid bit. No `sudo`
package is installed. The home directory and all changes are tmpfs state and
are recreated for every session.

## Verification

Unit and policy checks:

```bash
cmake -S . -B build_web_tests -DATHENA_GUI=Qt6 -DBUILD_TESTS=ON
cmake --build build_web_tests --target \
  athena-web-server athena-web-server-tests
ctest --test-dir build_web_tests -R 'athena-web'
python3 tools/release/test_runtime_policy.py
```

For a runtime smoke test, create a session through `POST /api/sessions`, wait
for `status=running`, and inspect both generated containers. Confirm:

- the sandbox reports `NetworkMode=none`, all capabilities dropped, read-only
  root, the requested memory/PID limits, and hostname `ATHENA-Experience`;
- `id -un` is `ATHENA-User`, Weston reports the VNC backend, and ATHENA reports
  the Wayland Qt platform;
- upload, download, archive, extension, capacity, tab-close cleanup, heartbeat
  cleanup, and timed expiry all behave as documented;
- no session container or per-session state remains after explicit close.
