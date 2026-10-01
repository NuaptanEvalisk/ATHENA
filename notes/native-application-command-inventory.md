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
| `view.outline` | application | `QTMOutlinePane` | explicit last-document actor/view identity | native |
| `view.neighborhoods` | workspace | `QTMNeighborhoodsPane` | explicit last-document published buffer identity | native |
| `view.document-history` | workspace | `QTMDocumentHistoryPane` | frozen document identity from command context | native |
| `workspace.global-search` | workspace | `QTMGlobalSearch` | frozen last-document zoom + vault state | native |
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
| `editor.search` | editor | native per-view document search bar | frozen editor canvas + actor-backed search transport | native |
| `editor.replace` | editor | native per-view document search bar | frozen editor canvas + read-only actor snapshot | native |
| `editor.save` | editor | owning BufferActor -> existing save-buffer-manual transaction | frozen editor target + read-only actor snapshot | native UI / Scheme business adapter |
| `editor.revert` | editor | owning BufferActor -> existing revert transaction | frozen editor target | native UI / Scheme business adapter |
| `editor.update-all` | editor | owning BufferActor -> existing update-document operation | frozen editor target + read-only actor snapshot | native UI / Scheme business adapter |
| `editor.close-document` | editor | owning BufferActor -> existing safely-kill-buffer transaction | frozen editor target | native UI / Scheme business adapter |
| `application.new-tab` | application | native window/document creation | application registry | native |
| `application.open-new-window` | application | native file chooser -> existing load-buffer-in-new-window transaction | application registry | native UI / Scheme business adapter |
| `application.page-setup` | application | native page setup dialog | application registry | native |
| `editor.save-as` | editor | owning BufferActor -> existing chooser/save-as transaction | frozen editor target | native UI / Scheme business adapter |
| `editor.preview` | editor | owning BufferActor -> existing preview transaction | frozen editor target | native UI / Scheme business adapter |
| `editor.print` | editor | owning BufferActor -> existing print transaction | frozen editor target | native UI / Scheme business adapter |
| `editor.close-window` | editor | owning BufferActor -> existing safely-kill-window transaction | frozen editor target | native UI / Scheme business adapter |
| `editor.history-back` | editor | owning BufferActor -> cursor history transaction | frozen editor target | native UI / Scheme business adapter |
| `editor.history-forward` | editor | owning BufferActor -> cursor future transaction | frozen editor target | native UI / Scheme business adapter |
| `editor.math-correct-all` | editor | owning BufferActor -> existing math correction operation | math-mode + read-only actor snapshot | native UI / Scheme business adapter |
| `editor.math-correct-remove-superfluous` | editor | native preference registry | math-mode actor snapshot + native preference state | native |
| `editor.math-correct-insert-missing` | editor | native preference registry | math-mode actor snapshot + native preference state | native |
| `editor.math-correct-homoglyph` | editor | native preference registry | math-mode actor snapshot + native preference state | native |
| `editor.presentation-first` | editor | owning BufferActor -> presentation traversal | presentation-mode actor snapshot | native UI / Scheme business adapter |
| `editor.presentation-previous-screen` | editor | owning BufferActor -> screen traversal | presentation/screens actor snapshot | native UI / Scheme business adapter |
| `editor.presentation-previous` | editor | owning BufferActor -> presentation traversal | presentation-mode actor snapshot | native UI / Scheme business adapter |
| `editor.presentation-next` | editor | owning BufferActor -> presentation traversal | presentation-mode actor snapshot | native UI / Scheme business adapter |
| `editor.presentation-next-screen` | editor | owning BufferActor -> screen traversal | presentation/screens actor snapshot | native UI / Scheme business adapter |
| `editor.presentation-last` | editor | owning BufferActor -> presentation traversal | presentation-mode actor snapshot | native UI / Scheme business adapter |
| `editor.print-to-file` | editor | owning BufferActor -> fixed print-to-file transaction | native print preference state | native UI / Scheme business adapter |
| `editor.print-page-selection` | editor | owning BufferActor -> fixed page-range print transaction | native print preference/system state | native UI / Scheme business adapter |
| `editor.print-page-selection-to-file` | editor | owning BufferActor -> fixed page-range file transaction | native print preference state | native UI / Scheme business adapter |
| `editor.export-pdf` | editor | owning BufferActor -> fixed print/export transaction | editor presence | native UI / Scheme business adapter |
| `editor.export-postscript` | editor | owning BufferActor -> fixed print/export transaction | editor presence | native UI / Scheme business adapter |

## Migrated dynamic presentation providers

These provider IDs return plain item data and accept opaque provider keys.
Presentation JSON cannot name Scheme procedures or carry menu ASTs.

| Provider ID | Scope | Data source | Execution route | Status |
| --- | --- | --- | --- | --- |
| `recent-files` | application | `native-recent-file-provider-data` in file/business module | fixed `load-buffer` route | native provider |
| `file-import-formats` | application | converter registry via `native-import-format-provider-data` | native chooser -> fixed `import-buffer` route | native provider |
| `file-export-formats` | editor | converter registry via `native-export-format-provider-data` | native chooser -> `buffer_export(frozen_document,...)` | native provider |
| `realtime-save-toggle` | editor | explicit document persistence state | `athena_set_realtime_save_paused(frozen_document,...)` | native provider |
| `selection-image-formats` | editor | converter/image format inventory + selection snapshot | fixed owning-actor image-export adapter | native provider |

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
native-graphics selection, focus-node availability, math/presentation/screens
mode bits, and bounded undo/redo counts. Main reads that snapshot only; it never
calls a live editor to populate
the palette. Execution resolves the captured document view to actor/view IDs,
uses nonblocking `try_submit_to`, and recomputes availability on the actor
before running the command under the existing menu-action transaction boundary.
Standard Qt text inputs inside an editor pane suppress the editor Edit commands
so their local selection/clipboard behavior is not redirected to the document.

Menu presentation schema version 2 supports recursive JSON submenus. The
inactive shell presenter builds those submenus recursively from registry data;
submenu presentation inherits the originating top-level menu context instead of
recapturing a target after focus has moved into the menu.

`QTMCommandContext` now also freezes a `QTMDocumentIdentity`: weak document
widget, actor/view IDs, published native buffer name and zoom snapshot. Tools
that explicitly follow the last active document consume this identity instead
of reading the legacy global current buffer/view. Stale actor/view IDs are
validated naturally by mailbox submission; file-oriented tools use the copied
published buffer name.

The editor command declarations intentionally do not add Ctrl+Z/C/X/V shortcuts
yet. Those keys still have their existing editor/input routes; adding a second
native shortcut route before retiring the old one would violate the one-route
rule.

Parameterized editor commands are also accepted by the same registry when a
JSON command carries a validated `editor_action`.  Their state is determined
only from published actor capability masks; execution uses the nonblocking
`native_editor_action_json` transport and actor-side revalidation.  This is
the foundation for text/math/prog/source mode toolbar migration and is not a
generic Scheme invocation facility.

Parameterized presentation commands can set `palette:false`.  They remain
normal registry commands for toolbar state/dispatch, but are intentionally
excluded from command-palette enumeration; static math symbol families use
this to avoid hundreds of low-signal palette entries.

Dynamic toolbar providers expose a cheap native `providerState` separately
from item enumeration. The view-owned presenter can therefore decide whether a
provider-only submenu is visible or enabled during periodic state refresh
without polling Scheme-backed data. Personal macros use an actor-owned grouped
snapshot and a nonblocking request command; color providers use only
mode/read-only snapshot state until the user opens the submenu.

The focus-toolbar migration uses a separate actor-owned
`actor_focus_toolbar_snapshot`. It carries only plain structured state
(focus capabilities, tag label/display name, and variant values), never a
Scheme menu tree. The first inactive native focus slice binds similar-tag
traversal, structured insert/remove, variant selection, structured exit,
remove-tag, and Describe through finite BufferActor commands. The legacy focus
toolbar remains the sole production surface until specialized document/screens
and tag-specific contributors are migrated.

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
- The editor main and mode toolbars are view-owned and fully native. The legacy
  `texmacs-main-icons` and `texmacs-mode-icons` producer/transport/Qt
  replacement paths are deleted. The focus toolbar remains on the legacy
  production path until its structured capability/value providers reach parity.
- Shared worker/error status is still editor-footer-oriented and needs a
  shell-owned zero-buffer surface.
- Plugin command contribution is still owned by the existing plugin UI/lifecycle
  machinery; it should join the registry without reimplementing that protocol.
- Remaining File/Insert/Format/Document/Interface/View/Workspace/Go/Help
  command inventories, predicates, checked states, and shortcuts still require
  grouped migration before the legacy Scheme menubar can be retired.

The startup `lazy-menu-force-all` call also remains because the legacy Scheme
menu graph is still active. It is not used by the native command palette.
