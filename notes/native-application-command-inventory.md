# Native application command migration inventory

This inventory records the ownership boundary for the first native application
shell stage. It is intentionally narrower than the final menu/toolbar inventory:
items marked **pending** still use the legacy Scheme UI production path and must
not be reported as native cutover.

## Migrated in the command foundation stage

| Command ID | Scope | Behavior owner | Presentation/state source | Status |
| --- | --- | --- | --- | --- |
| `application.new-document` | application | C++ `new_document_buffer` | native registry + `application-shell.json` | native |
| `application.open` | application | C++ file chooser, existing `load-buffer` business operation | native registry + JSON | native UI / Scheme business adapter |
| `application.preferences` | application | `QTMPreferencesDialog` | native registry + JSON | native |
| `application.command-palette` | application | `QTMCommandPalette` | native registry + JSON | native |
| `application.quit` | application | shell close -> existing safe-quit transaction | native registry + JSON | native UI / existing quit transaction |
| `workspace.namespace-explorer` | application | `QTMNamespaceExplorer` launcher | native registry + JSON | native |
| `view.error-messages` | application | `QTMErrorMessagesPane` | native registry + JSON | native |
| `view.artifacts` | workspace | `QTMArtifactsPane` | native registry + JSON | native |
| `file.compare-files` | application | native ATHENA diff dialog | native registry + JSON | native |
| `file.export-namespace` | workspace | native namespace export flow | native registry + JSON | native UI / existing export business operation |
| `application.quick-switcher` | workspace | native vault quick switcher | native registry + JSON | native UI / existing recent-file adapter |
| `workspace.namespace-manager` | workspace | `QTMNamespaceManager` | native registry + JSON | native |
| `workspace.websites-manager` | workspace | `QTMWebsitesManager` | native registry + JSON | native |
| `workspace.materials-manager` | workspace | `QTMMaterialsManager` | native registry + JSON | native |
| `workspace.custom-styles-manager` | workspace | `QTMCustomStylesManager` | native registry + JSON | native |
| `workspace.audmap-repl` | workspace | native AUDMAP ADS pane | native registry + JSON | native |
| `workspace.google-tasks` | workspace | native Google Tasks ADS pane | native registry + JSON | native |
| `workspace.artifacts-build-vault` | workspace | vault-wide artifact build dialog | native registry + JSON | native |
| `help.about` | application | `QTMAbout` | native registry + JSON | native |
| `namespace.open` | pane | `QTMNamespaceExplorer` | pane provider state | native |
| `namespace.copy` | pane | `QTMNamespaceExplorer` | pane provider state | native |
| `namespace.paste` | pane | `QTMNamespaceExplorer` | pane provider state | native |
| `namespace.rename` | pane | `QTMNamespaceExplorer` | pane provider state | native |
| `namespace.delete` | pane | `QTMNamespaceExplorer` | pane provider state | native |
| `namespace.refresh` | pane | `QTMNamespaceExplorer` | pane provider state | native |
| `editor.undo` | editor | owning BufferActor | atomic per-view editor snapshot | native |
| `editor.redo` | editor | owning BufferActor | atomic per-view editor snapshot | native |
| `editor.copy` | editor | owning BufferActor | atomic per-view editor snapshot | native |
| `editor.cut` | editor | owning BufferActor | atomic per-view editor snapshot | native |
| `editor.paste` | editor | owning BufferActor | atomic per-view editor snapshot | native |
| `editor.node-properties` | editor | owning BufferActor + native node-properties dialog | atomic per-view editor snapshot | native |

Application shortcuts currently declared in the JSON inventory are
`Ctrl+N`, `Ctrl+O`, `Ctrl+Shift+P`, and `Ctrl+Q`. They are resolved by
the registry in `QTMApplication::notify`. Application-scope shortcuts are
arbitrated before the legacy `athenaOwnsKeyInput` exemption so that an
AUDMAP/tool input owner cannot blanket-disable application routing. Active
modal dialogs remain a hard boundary and retain their local key handling.

The command palette consumes the registry directly. It no longer walks editor
`QMenuBar`/`QAction` trees or forces `QTMLazyMenu` expansion. Menu/palette
invocations capture the current work pane and input widget with `QPointer`
lifetimes; pane commands are rejected when the captured provider disappears.

Editor command state is published by the owning BufferActor as one packed
64-bit atomic snapshot per view. It currently carries read-only, selection,
native-graphics selection, focus-node availability, and bounded undo/redo
counts. Main reads that snapshot only; it never calls a live editor to populate
the palette. Execution resolves the captured document view to actor/view IDs,
uses nonblocking `try_submit_to`, and recomputes availability on the actor
before running the command under the existing menu-action transaction boundary.
Standard Qt text inputs inside an editor pane suppress the editor Edit commands
so their local selection/clipboard behavior is not redirected to the document.

Menu presentation schema version 2 supports recursive JSON submenus. The
inactive shell presenter builds those submenus recursively from registry data;
submenu presentation inherits the originating top-level menu context instead of
recapturing a target after focus has moved into the menu.

The editor command declarations intentionally do not add Ctrl+Z/C/X/V shortcuts
yet. Those keys still have their existing editor/input routes; adding a second
native shortcut route before retiring the old one would violate the one-route
rule.

## Zero-buffer ownership now established

The outer `QTMMainTabWindow` is shown directly at startup and no longer needs
an untitled bootstrap editor. Closing the final document removes its
window/view/buffer without converting that operation into application Quit.
Explicit shell close still enters `safely-quit-ATHENA`.

Configured startup content remains an explicit business policy:
`vault-startup-open-initial-buffer` may open a document when its preferences
request one, but an otherwise empty startup remains an empty application shell.

## Pending migration

The following remain intentionally on their existing path until a coherent
native stage replaces them:

- The application menubar is still produced by Scheme inside editor windows.
  The shell now owns an inactive `QTMApplicationMenuPresenter` implementation
  that consumes only the native registry, freezes work/input context on menu
  presentation, and normalizes unavailable contextual groups.  It has no
  production activation call.  It must not be enabled until overlapping
  production and shortcut routes can be retired without losing command
  coverage; see `notes/native-application-menu-migration.md`.
- The first editor Edit/Focus slice is native: Undo, Redo, Copy, Cut, Paste,
  and Node properties use BufferActor snapshots plus ID-only dispatch. The
  remaining Edit contents and the dynamic structured Focus hierarchy still need
  grouped migration before the shell menubar can replace the legacy one.
- Editor main/mode/focus toolbars remain view-owned and Scheme-produced.
- Shared worker/error status is still editor-footer-oriented and needs a
  shell-owned zero-buffer surface.
- Plugin command contribution is still owned by the existing plugin UI/lifecycle
  machinery; it should join the registry without reimplementing that protocol.
- Remaining File/Insert/Format/Document/Interface/View/Workspace/Go/Help
  command inventories, predicates, checked states, and shortcuts still require
  grouped migration before the legacy Scheme menubar can be retired.

The startup `lazy-menu-force-all` call also remains because the legacy Scheme
menu graph is still active. It is not used by the native command palette.
