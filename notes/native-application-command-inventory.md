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
| `namespace.open` | pane | `QTMNamespaceExplorer` | pane provider state | native |
| `namespace.copy` | pane | `QTMNamespaceExplorer` | pane provider state | native |
| `namespace.paste` | pane | `QTMNamespaceExplorer` | pane provider state | native |
| `namespace.rename` | pane | `QTMNamespaceExplorer` | pane provider state | native |
| `namespace.delete` | pane | `QTMNamespaceExplorer` | pane provider state | native |
| `namespace.refresh` | pane | `QTMNamespaceExplorer` | pane provider state | native |

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
  A shell-owned menubar must not be enabled until overlapping production and
  shortcut routes can be retired without losing command coverage.
- Editor Edit/Focus commands and their dynamic state need compact
  BufferActor-published snapshots plus ID-only mailbox dispatch. Main must not
  query a live editor synchronously or use the last-focused editor fallback.
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
