# Wayland Docking Plan: xdg-toplevel-drag-v1

## Summary

ATHENA should not patch KWin for Visual-Studio-style docking on Wayland.
The correct Wayland path is the same architectural route used by Chromium tab
dragging: combine normal Wayland drag-and-drop with `xdg-toplevel-drag-v1`.

The compositor moves the detached top-level window during the drag. ATHENA
decides docking policy through drag enter, motion, leave, and drop events on
its own windows, using target-local coordinates rather than global window
geometry.

## Design Principle

Do not ask the compositor for global geometry of ATHENA windows. Do not expose
or consume compositor-owned window tables. Do not require a modified KWin.

Instead:

- a detachable ADS pane remains an ATHENA-owned pane object;
- dragging starts a Wayland DnD session with an ATHENA-specific MIME type;
- when a pane becomes detached, ATHENA maps a real floating top-level for it;
- if the compositor supports `xdg-toplevel-drag-v1`, ATHENA attaches that
  floating top-level to the active DnD session;
- target ATHENA windows receive DnD events in their own local coordinate space;
- target windows render docking overlays and choose drop targets locally;
- on drop, ATHENA migrates the pane object into the target ADS layout and
  destroys or unmaps the temporary floating top-level.

## Protocol Path

Preferred Wayland path:

1. User starts dragging an ADS pane tab/title area.
2. ATHENA starts a DnD session with a MIME type such as:
   `application/x-athena-ads-pane-drag`.
3. ATHENA creates a floating top-level window containing the dragged pane.
4. Before or during mapping, ATHENA calls `xdg_toplevel_drag_v1.attach` for the
   floating top-level and the active DnD session.
5. The compositor moves the real floating top-level with the pointer.
6. ATHENA windows under the pointer receive DnD enter/motion/drop events.
7. ATHENA hit-tests ADS docking targets using the receiving window's local
   coordinates.
8. On a dock-target drop, ATHENA moves the pane object into the target ADS
   layout and destroys the floating top-level.
9. On an external drop or cancel, ATHENA keeps or restores the floating pane.

## Fallback Path

If `xdg-toplevel-drag-v1` is unavailable:

- still use ordinary Wayland DnD with the ATHENA pane MIME type;
- show a drag icon or lightweight preview instead of a compositor-moved real
  top-level;
- target ATHENA windows still receive DnD enter/motion/drop and can show
  docking overlays;
- create, keep, or merge the floating ADS pane at drop time.

This fallback is less visually direct than Chrome's real-window drag, but it
preserves the important behavior: docking is target-local and does not require
global geometry.

## Implementation Areas

### ADS Integration

Patch or wrap ADS pane dragging so ATHENA can intercept pane drag start before
ADS begins its legacy mouse-grab movement.

Required state:

- source dock widget identity;
- source dock manager/window identity;
- whether the pane is already floating;
- current DnD session id;
- temporary floating container, if created;
- target dock manager/window under the DnD cursor;
- current local drop target.

### Wayland Protocol Layer

Use Qt's Wayland-native integration where possible. If Qt does not expose enough
surface/protocol control publicly, add a small ATHENA Wayland helper isolated to
the Qt subsystem.

Responsibilities:

- detect whether Qt is using the native Wayland platform;
- detect compositor support for `xdg-toplevel-drag-v1`;
- create or access the `wl_data_source` for the pane DnD session;
- attach the floating `xdg_toplevel` to `xdg_toplevel_drag_v1`;
- avoid this path entirely under XCB, XWayland, Windows, or macOS.

### DnD Target Handling

Every ATHENA top-level window that can receive docked panes should accept the
ATHENA pane MIME type.

On DnD enter/motion:

- translate event coordinates into the receiving ADS window;
- ask ADS for possible dock areas;
- render the docking overlay in that window;
- update target preview as the pointer moves.

On DnD leave:

- hide the overlay for that target window.

On DnD drop:

- if the local hit-test selects a dock target, move the pane object into the
  target ADS layout;
- otherwise reject or keep the pane floating.

### Pane Migration

Docking and redocking must move ATHENA pane ownership, not Wayland windows.

The floating window is disposable transport UI. The persistent entity is the ADS
dock widget/pane model. A successful dock:

- removes the pane from the floating container;
- inserts it into the target dock manager;
- restores focus to the pane's primary widget;
- closes the empty floating top-level.

## Test Plan

- Native Wayland on KDE Plasma with `xdg-toplevel-drag-v1` support:
  - drag a docked pane out into a real floating window;
  - drag it over the original window and verify overlays;
  - drop into left/right/bottom/center targets and verify correct docking;
  - drag between two ATHENA main windows on different monitors.

- Native Wayland without `xdg-toplevel-drag-v1`:
  - verify fallback DnD preview path;
  - verify target overlays and final docking still work.

- XCB/XWayland:
  - verify existing ADS behavior remains available and no Wayland protocol path
    is attempted.

- Cancel and failure cases:
  - Escape cancels and restores the source pane;
  - dropping outside ATHENA leaves the pane floating;
  - closing the source or target window during drag does not crash;
  - failed protocol attachment falls back cleanly.

## Non-Goals

- No modified KWin dependency.
- No global compositor window table.
- No control over non-ATHENA application windows.
- No compositor-specific docking policy.
