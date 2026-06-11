# ATHENA KWin Patch Queue

ATHENA does not vendor KWin. The supported workflow is to keep a clean upstream
KWin checkout outside this repository, then apply the patches in
`tools/kwin/patches/`.

The initial target is KWin 6.6.5, matching the version currently used for
ATHENA's Qt 6 Wayland testing.

## Build

```sh
tools/kwin/scripts/clone-kwin.sh
tools/kwin/scripts/apply-athena-patches.sh
tools/kwin/scripts/build-kwin.sh
```

`build-kwin.sh` is a quick proof build that disables several desktop-facing
subsystems. For a replacement-grade compositor build with KCMs, decorations,
tabbox, screenlocker integration, notifications, PipeWire screencast, libeis,
and the normal KWin plugin set, use:

```sh
tools/kwin/scripts/build-kwin-full.sh
```

By default the scripts use:

- source: `$HOME/data/Software/KDE/kwin-athena-src`
- build: `$HOME/data/Software/KDE/kwin-athena-build`
- full build: `$HOME/data/Software/KDE/kwin-athena-full-build`
- full install prefix: `$HOME/data/Software/KDE/kwin-athena-install`

Override them with `KWIN_ATHENA_ROOT`, `KWIN_ATHENA_SRC`, or
`KWIN_ATHENA_BUILD`. The full build script also accepts
`KWIN_ATHENA_FULL_BUILD` and `KWIN_ATHENA_INSTALL`.

## Demo API

The first patch adds a deliberately small DBus method:

```sh
qdbus6 org.kde.KWin /KWin org.kde.KWin.athenaPing
```

Expected output:

```text
ATHENA modified KWin 6.6.5
```

This proves that the running compositor is the ATHENA-patched KWin before we add
real docking-control APIs.

## Owned Docking API

The second patch adds compositor help for ATHENA docking on Wayland. The methods
are still exposed on `/KWin` under `org.kde.KWin`, but they are intentionally
caller-owned: KWin resolves the D-Bus caller PID and only returns or acts on
windows whose `Window::pid()` matches that caller. This lets ATHENA coordinate
its own main windows and floating ADS panes without exposing Dolphin, Konsole,
or other applications.

Added methods:

```text
athenaListWindows() -> aa{sv}
athenaSetWindowGeometry(windowId, x, y, width, height) -> bool
athenaRaiseWindow(windowId) -> bool
athenaBeginDockDrag(windowId, callbackPath, hotspotX, hotspotY) -> dragId
athenaCancelDockDrag(dragId) -> bool
athenaCurrentDockDragState(dragId) -> a{sv}
```

Window geometry is compositor-global logical geometry. Payloads also include
output metadata, so ATHENA can distinguish overlapping-looking coordinates on
different monitors.

Drag callbacks are direct D-Bus method calls to the caller's unique service and
the object path supplied to `athenaBeginDockDrag`. KWin calls the interface:

```text
org.athena.KWinDockDragSink
```

with:

```text
dockDragMoved(a{sv})
dockDragDropped(a{sv})
dockDragCancelled(a{sv})
```

The callback state reports the dragged pane, pointer position, output, and the
caller-owned target window under the pointer. KWin does not decide ADS policy;
ATHENA still decides whether a drop becomes a tab, side dock, center dock, or
floating pane.

`athenaPing` can be tested with `qdbus6` from any process. The owned-window
methods must be tested from the same process that owns the windows being queried
or moved.
