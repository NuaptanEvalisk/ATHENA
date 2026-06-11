# ATHENA KWin Owned Docking API Plan

## Summary

Add a second ATHENA KWin patch that exposes a small D-Bus API for
compositor-assisted docking under Wayland. The API must only reveal and control
windows owned by the calling process, so ATHENA can inspect and move its own
main window and floating ADS panes, but cannot inspect or act on Dolphin,
Konsole, or other applications.

The implementation belongs in ATHENA's KWin patch queue as:

```text
tools/kwin/patches/0002-athena-add-owned-docking-api.patch
```

KWin source stays external. ATHENA stores only patches and build helpers.

## Public API

Add these methods to `org.kde.KWin` on `/KWin`.

### `athenaListWindows() -> aa{sv}`

Return only windows whose `Window::pid()` equals the D-Bus caller PID.

Each returned map should include:

- `windowId`: `Window::internalId().toString()`
- `caption`
- `resourceClass`
- `resourceName`
- `desktopFile`
- `frameGeometry`
- `clientGeometry`
- `outputName`
- `outputGeometry`
- `outputScale`
- `active`
- `minimized`
- `move`
- `resize`

Geometry is compositor-global logical geometry, not monitor-local geometry.

### `athenaSetWindowGeometry(QString windowId, int x, int y, int w, int h) -> bool`

Set geometry only for caller-owned windows.

Reject the call if:

- the window id is invalid;
- the window exists but belongs to a different PID;
- the geometry is invalid.

### `athenaRaiseWindow(QString windowId) -> bool`

Raise and activate only caller-owned windows.

Reject invalid or non-owned window ids.

### `athenaBeginDockDrag(QString windowId, QString callbackPath, int hotspotX, int hotspotY) -> QString`

Start a compositor-managed drag session for a caller-owned floating pane.

KWin stores:

- generated `dragId`;
- caller unique D-Bus service;
- caller PID;
- dragged window id;
- callback object path;
- local drag hotspot.

Return an empty string if the window is invalid, already being moved/resized, or
not owned by the caller.

### `athenaCancelDockDrag(QString dragId) -> bool`

Cancel an active drag session if it belongs to the caller.

### `athenaCurrentDockDragState(QString dragId) -> a{sv}`

Polling fallback for a caller-owned active drag session.

This should return the same state shape used in callbacks.

## Callback Interface

Do not broadcast live drag geometry as public session-bus signals. Instead,
ATHENA supplies a callback object path when starting a drag, and KWin calls that
object directly on the original caller's unique D-Bus service.

Callback interface:

```text
org.athena.KWinDockDragSink
```

Methods KWin calls:

- `dockDragMoved(a{sv})`
- `dockDragDropped(a{sv})`
- `dockDragCancelled(a{sv})`

Callback payload:

- `dragId`
- `draggedWindowId`
- `pointerGlobal`
- `outputName`
- `outputGeometry`
- `outputScale`
- `draggedFrameGeometry`
- `draggedClientGeometry`
- `targetWindowId`
- `targetFrameGeometry`
- `targetClientGeometry`
- `targetLocalClientPos`

Target fields are empty when the pointer is over another application or no
caller-owned target exists.

## Security Boundary

Use caller PID as the boundary.

KWin's existing D-Bus interface inherits `QDBusContext`, so the implementation
can resolve the caller PID with:

```cpp
connection().interface()->servicePid(message().service())
```

Every ATHENA method must:

- resolve the caller PID;
- find windows only through a helper that requires `window->pid() == callerPid`;
- never return non-owned window geometry;
- never apply geometry, raise, move, resize, or drag operations to non-owned windows.

If a requested UUID belongs to another process, return `false` for boolean
methods and an empty result for value-returning methods.

## Implementation Shape

Implement patch 0002 against KWin 6.6.5 in the same style as patch 0001.

Likely touched upstream files:

- `src/dbusinterface.h`
- `src/dbusinterface.cpp`
- `src/org.kde.KWin.xml`

Add helpers in `dbusinterface.cpp`:

- `std::optional<pid_t> determineCallerPid() const`
- `Window *findCallerWindow(const QString &uuid, pid_t callerPid) const`
- `QVariantMap rectToVariantMap(const RectF &rect)`
- `QVariantMap outputToVariantMap(LogicalOutput *output)`
- `QVariantMap athenaWindowToVariantMap(Window *window)`

Add drag-session storage to `DBusInterface`:

- struct containing `dragId`, caller service, caller PID, callback path,
  dragged window `QPointer<Window>`, and latest state;
- map keyed by `dragId`;
- cleanup when the dragged window is removed, the drag is cancelled, or the drag
  finishes.

Use KWin movement primitives:

- start from existing `Window::startInteractiveMoveResize()` flow;
- initialize move state similarly to existing `NETMoveResize(... NET::Move ...)`;
- use KWin's current pointer/global position as the authoritative drag position.

KWin should not implement ATHENA docking policy. KWin reports:

- where the dragged pane is;
- where the pointer is;
- which caller-owned target window is under it;
- which output/global coordinate space is involved;
- when the drag moves, drops, or cancels.

ATHENA decides whether the drop becomes left/right/top/bottom/center/tab/floating
inside ADS.

## Patch Queue Integration

Add:

```text
tools/kwin/patches/0002-athena-add-owned-docking-api.patch
```

Update:

```text
tools/kwin/README.md
```

README additions:

- describe the owned docking API;
- document that `athenaPing` is a global proof API, but owned-window APIs must
  be called from the process whose windows are being queried;
- document nested KWin testing;
- document that geometry is compositor-global logical geometry plus output
  metadata.

## Test Plan

Apply the patch queue to a clean KWin 6.6.5 checkout and build the full install.

Start nested KWin and verify:

```sh
qdbus6 org.kde.KWin /KWin org.kde.KWin.athenaPing
```

Expected:

```text
ATHENA modified KWin 6.6.5
```

Create or use a small Qt D-Bus test client with two top-level windows inside the
nested compositor.

Test cases:

- `athenaListWindows()` returns only the test client's windows.
- Dolphin, Konsole, and other processes are absent.
- `athenaSetWindowGeometry()` succeeds for a caller-owned window.
- `athenaSetWindowGeometry()` fails for a non-owned UUID.
- `athenaRaiseWindow()` succeeds only for caller-owned windows.
- `athenaBeginDockDrag()` starts on a caller-owned floating window.
- Moving over another caller-owned window reports target-local coordinates.
- Moving over another app reports empty target fields.
- Releasing sends `dockDragDropped`.
- Cancelling sends `dockDragCancelled`.
- Killing the caller or closing the dragged window cleans up the drag session.

Later ATHENA integration test:

- detach an ADS pane;
- start compositor-managed drag through the API;
- drag across outputs;
- confirm ATHENA receives compositor-global geometry and output metadata;
- confirm ATHENA can dock only into its own main window/panes.

## Assumptions

- Caller-process ownership is the security boundary for v1.
- D-Bus callbacks are used instead of broadcast signals to avoid exposing window
  geometry to the whole session bus.
- Geometry is compositor-global logical geometry, always paired with output
  metadata.
- KWin only reports compositor state and performs compositor-owned movement.
  ATHENA remains responsible for ADS layout mutation.
- This patch does not yet add ATHENA-side ADS integration.
