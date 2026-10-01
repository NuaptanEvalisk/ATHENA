# Native application menubar migration inventory

This file records the production disposition of the current Scheme menubar
before the application shell takes ownership.  It is an inventory, not a second
menu definition: shipped ordering belongs in `application-shell.json`, behavior
belongs in the native command registry, and dynamic editor state belongs in
BufferActor/pane providers.

Disposition:

- **native** — registered native command with native state/target enforcement.
- **adapter** — native UI ownership is acceptable, but execution deliberately
  delegates to an existing business operation while that operation remains the
  domain authority.
- **pending** — legacy production path remains authoritative; shell cutover must
  not hide this entry.
- **provider** — dynamic/extension content needs a native contribution provider,
  not a static copy of the current rendered menu.

## Top-level ownership

| Current top-level entry | Source | Disposition |
| --- | --- | --- |
| File | `athena/menus/main-menu.scm -> file-menu` | partial; keep legacy production until remaining File commands migrate |
| Edit | `athena/menus/edit-menu.scm` | partial native Edit core; remaining branches pending |
| Insert | mode-dependent `insert-menu` / graphics insert menu | pending editor provider |
| Manual | conditional `tmdoc-menu` | pending editor/provider |
| Source | conditional `source-menu` | pending editor/provider |
| Dynamic | conditional presentation menu | partial native | main-toolbar traversal is native from presentation/screens snapshot bits; broader Dynamic menu remains pending |
| Focus | `athena-focus-menu` / graphics focus menu | partial native; dynamic structured focus pending |
| Format | mode-dependent `format-menu` | pending editor provider |
| Document | `generic/document-menu.scm` plus ATHENA extension | pending editor/provider |
| Interface | `athena/menus/interface-menu.scm` | pending editor-view state migration |
| View | `athena/menus/view-menu.scm` | pending; several panes already have native Qt implementations |
| Workspace | `athena/menus/main-menu.scm` plus utility menu | partial; Namespace Explorer is native |
| Go | `athena/menus/file-menu.scm` plus utility menu | partial; Command palette is native |
| Test | optional dynamic `test-menu` | provider |
| Help | `doc/help-menu.scm` plus utility menu | pending application/help provider |
| `texmacs-extra-menu` | runtime extension point | provider |

The optional/contextual top-level entries above are part of the migration
contract.  A stable shell menubar may present a stable superset, but cutover
must not make their commands unreachable.

## File

| Current direct entry | Disposition | Native target / remaining work |
| --- | --- | --- |
| New | native | `application.new-document` |
| New within namespace | pending | register existing namespace wizard operation |
| Load | native/adapter | `application.open`; native file chooser -> existing load-buffer business operation |
| Load in new window | native/adapter | `application.open-new-window`; native chooser -> existing global load-buffer-in-new-window transaction |
| Load Vault | pending | application/vault operation |
| Unload Vault | pending | application/vault operation and stale-context invalidation |
| Revert | native/adapter | `editor.revert` targets the owning BufferActor and preserves the existing revert transaction |
| Compare two files | native | `file.compare-files` |
| Open in text editor | pending | current-file editor state + external launch adapter |
| Open in file manager | pending | current-file editor state + external launch adapter |
| Recent Files | native provider | `recent-files`; plain recent-file data from `tm-files.scm`, fixed load route; clear action still pending |
| Recent Vaults | provider | vault recent-list provider + clear action |
| Save | native/adapter | `editor.save` targets the owning BufferActor and preserves permissions/conflict/Save-As/post-hook logic |
| Pause/Resume realtime save | native provider | `realtime-save-toggle`; explicit frozen document URL + native persistence API |
| Save as | native/adapter | `editor.save-as`; owning BufferActor opens the existing chooser/save-as transaction |
| Print / Page setup | partial native | `editor.preview`, `editor.print`, and application `application.page-setup`; print-to-file/page-selection branches remain provider work |
| Import | native provider | `file-import-formats`; converter inventory data + native chooser + fixed import route; embedded-PDF special adapter still pending |
| Export | partial native provider | `file-export-formats`; converter inventory data + native chooser + `buffer_export` on frozen document; print/selection extras still pending |
| Export namespace | native/adapter | `file.export-namespace`; native flow with existing export business operation |
| Close document | native/adapter | `editor.close-document` targets the owning BufferActor and preserves unsaved-change confirmation |
| Restart ATHENA | adapter pending | preserve safe restart transaction |
| Close ATHENA | native/adapter | `application.quit` -> existing safe-quit transaction |

## Edit

| Current direct entry/branch | Disposition | Native target / remaining work |
| --- | --- | --- |
| Undo | native | `editor.undo`, actor snapshot + ID dispatch |
| Redo | native | `editor.redo`; multi-branch redo submenu still pending provider |
| Copy | native | `editor.copy` with local Qt input ownership protection |
| Cut | native | `editor.cut` with read-only/selection actor state |
| Paste | native | `editor.paste` with read-only actor state |
| Clear | pending | editor actor command |
| Search | native | `editor.search`; opens the search bar on the frozen editor canvas |
| Global search | native: `workspace.global-search`; preview zoom is frozen in the originating command context |
| Replace | native | `editor.replace`; frozen editor canvas + read-only snapshot |
| Correct | native | math-mode snapshot + `editor.math-correct-all` and three native preference toggle commands |
| AI | provider | selection-gated completion commands |
| Copy to / Cut to / Paste from | provider | clipboard-format inventories and selection state |
| Import selections as / Export selections as | provider | converter/preference inventories |
| Clear undo history | pending | editor actor command |
| View all preferences | pending | application preferences surface |
| Preferences | native | `application.preferences` |

## Focus

| Current direct entry/branch | Disposition | Native target / remaining work |
| --- | --- | --- |
| Structured focus hierarchy | provider | publish compact focus capabilities/identities; do not copy focus trees to Main |
| Node properties... | native | `editor.node-properties`, actor revalidation + native dialog |
| Vault transclusion focus additions | provider | editor/vault focus provider |
| Materials focus additions | provider | editor/materials focus provider |
| Graphics focus menu | provider | graphics-mode editor provider |
| Namespace open/refresh context | native | `namespace.open`, `namespace.refresh` when Namespace Explorer owns work context |

## View

All remaining legacy View entries remain reachable through the legacy menu until
registered.  Error Messages and Artifacts now have native registry commands.

| Current direct entry/branch | Disposition |
| --- | --- |
| Full screen / Presentation / Panorama / All slides | pending editor/view state |
| Show outline | native: `view.outline`; actor/view target comes from explicit last-active document identity |
| Vault Explorer / Namespace Explorer / Document History | partial; Namespace Explorer and Document History native, Vault Explorer pending |
| Neighborhoods | native: `view.neighborhoods`; follows explicit published last-active document identity |
| Error messages | native: `view.error-messages` |
| Artifacts | native: `view.artifacts`; Current document filtering resolves the shell's explicit last-active document through actor ID + published buffer metadata instead of global current-buffer state |
| Headings -> Unfold all | pending editor actor command |
| Fit to screen / width / persistent width | pending editor view snapshot/dispatch |
| Typewriter mode | pending preference + editor state |
| Labels | provider/pending preference commands |
| Graphs | pending native graph launch commands |
| Zoom in/out / fixed Zoom / Other | pending view-owned actor commands |
| Snap to pages | pending preference/editor view state |

## Workspace

| Current direct entry/branch | Disposition |
| --- | --- |
| New tab | native: `application.new-tab` |
| New floating window | pending explicit window command |
| Configure Font for Vault | pending workspace/vault operation |
| Run global transformation | pending workspace operation |
| AUDMAP REPL | native: `workspace.audmap-repl` |
| Namespace Manager | native: `workspace.namespace-manager` |
| Websites manager | native: `workspace.websites-manager` |
| Materials manager | native: `workspace.materials-manager` |
| Custom styles manager | native: `workspace.custom-styles-manager` |
| Vault -> Bugcheck / Maintenance | pending: Bugcheck requires actor/view context; Maintenance still has application lifecycle cleanup work |
| Artifacts -> Build entire vault | native: `workspace.artifacts-build-vault` |
| Artifacts -> Build current document | pending: business operation still uses global current-buffer state |
| Google Tasks | native: `workspace.google-tasks` |
| Refresh caches -> Styles | pending application/workspace command |
| Clean cache | pending application command |
| Namespace Explorer | native additive command: `workspace.namespace-explorer` |

## Interface

Header bars, four editor icon-bar toggles, Status bar, Presentation tool, Source
macros tool, Show key presses, and Remote control are all **pending**.  Their
checked state must come from view/preference snapshots rather than Main calling
editor predicates.  The editor toolbar remains view-owned after menubar cutover.

## Go

| Current direct entry/branch | Disposition |
| --- | --- |
| Welcome (System) / Welcome (Vault) | pending application/vault navigation |
| Random document | pending vault provider |
| Command palette | native: `application.command-palette` |
| Quick switcher | native/adapter: `application.quick-switcher`; native UI with existing recent-file query adapter |
| Back / Forward / Save position | partial native | `editor.history-back` and `editor.history-forward` target the owning actor; Save position remains pending |
| Buffer/window/hidden/linked/recent/bookmark lists | provider; explicit document/view identities required |

## Insert, Format, Document

These are editor-local dynamic menus and remain **pending providers**.  The
shell must not evaluate `in-text?`, `in-math?`, `in-prog?`,
`buffer-has-preamble?`, style/font predicates, or similar Scheme predicates.
Their eventual native presentation should consume finite actor-published mode,
focus, selection, style/document and checked-state snapshots.

Document's current direct branches include Style, Citation Style, style extras,
source/preamble toggles, Update, Font, Paragraph, Page, Metadata,
Magnification, Colors, Language, Informative flags, and the ATHENA document
utilities (macros, auxiliary-data refresh/statistics/save-aux).  Every one is
pending until its state/dispatch provider is native.

## Help and runtime contributions

About ATHENA is native as `help.about`. Welcome, Getting started, Configuration,
Manual, Reference guide, Apropos, documentation/source/recent search, Full
manuals, and Shortcuts listing remain **pending application/help
commands/providers**. Help availability comes from shipped resources rather
than an editor.

Vault Bugcheck remains pending because its implementation explicitly requires a
BufferActor/view execution context. Vault Maintenance remains pending because
its application-level status/error paths and final direct `get_server()->quit()`
still need conversion to the shell's safe lifecycle contract.

`test-menu`, `bookmarks-menu`, `texmacs-extra-menu`, converter lists,
style/package lists and plugin contributions are **provider** work.  They must
join the registry/contribution model at runtime; they must not be captured by
walking a rendered legacy `QMenu` tree.

## Production cutover gate

The shell-owned presenter may replace the editor-owned production menubar only
after:

1. Every row above is native/adapter/provider with a working native presentation
   route, or explicitly retired with justification.
2. Contextual top-level menus and runtime contributions remain reachable.
3. Shortcut ownership has one winning route per key.
4. Editor-local controls keep local clipboard/typing behavior.
5. Zero-buffer File/View/Workspace/Go/Help operations remain usable.
6. The old Scheme menubar production path is removed in the same coherent
   milestone; it is not kept as a hidden fallback.

## Toolbar production retirement map

The native registry presentation resource is version 3 and now also carries
validated toolbar inventories.  `QTMEditorToolbarPresenter` is view-owned and
constructs QAction/QMenu presentation from those definitions while resolving
state and execution through the same command registry as menus and the palette.
The presenter is deliberately constructed but inactive until a toolbar group has
complete command coverage.

The legacy editor toolbar production chain is:

`edit_main.cpp::rebuild_ui_chrome`
-> `tm_frame_rep::menu_icons`
-> Scheme lazy menu expansion / `make_menu_widget`
-> `ui_menu_icons`
-> `qt_actor_widget_rep::drain_external_effects`
-> `set_main_icons/set_mode_icons/set_focus_icons/set_user_icons`
-> `qt_tm_widget_rep` QAction replacement.

Retirement is group-by-group, with no dual production for a migrated group:

- Main toolbar: **cut over**. `editor-main` is the production view-owned
  toolbar. `menu_icons(0, texmacs-main-icons)`, the Scheme definition/lazy
  registration, `SLOT_MAIN_ICONS`, and the Qt widget/action replacement cache
  have been removed. `SLOT_MAIN_ICONS_VISIBILITY` remains only as the
  visibility contract for the native toolbar.
- Mode toolbar: migrate the text/math/prog/source/dynamic mode providers, then
  remove `texmacs-mode-icons` production and its corresponding icon-bar route.
- Focus toolbar: migrate structured focus capability/value providers, then
  remove `texmacs-focus-icons` production.
- User/extension toolbar: replace `texmacs-extra-icons` with the native runtime
  contribution model, then remove the last legacy `ui_menu_icons` producer and
  consumer.  At that point `tm_frame/tm_window::menu_icons` production APIs and
  the per-editor legacy toolbar widget caches can be deleted.

Generic Scheme menu-widget machinery is not part of that deletion while it is
still used by context popups, dialogs or bottom-tools.  Main menubar/toolbars
must not survive as hidden consumers merely to justify retaining the old path.

### Parameterized editor actions for mode toolbars

Mode toolbar migration uses validated `editor_action` command declarations
instead of translating legacy Scheme menu trees.  Presentation JSON may name
only operations accepted by `native_editor_action_validate` (for example
`make`, `make-with`, `make-style-with`, fraction/root/script/wide actions
and other finite editor primitives).  Each declaration can carry
`requires`, `requires_any`, and `forbids` capability names.

The owning BufferActor publishes the finite mode/style/selection capability
bits.  Main reads those bits to present command state, sends only the validated
compact action JSON plus numeric masks, and the BufferActor recomputes the
current snapshot before executing inside the existing menu-action transaction.
Unknown operations or capability names fail presentation validation at startup;
there is no arbitrary Scheme procedure/expression route in this mechanism.

The capability snapshot currently covers text/math/prog/source/graphics mode,
poster/manual/letter/book/section/theorem/markup/list/float/fold/std style
capabilities, presentation/screens mode, and non-small selection state.  Style
capabilities are refreshed with the actor-owned menu/environment update rather
than synchronously queried by Main for each toolbar button.

Mode presentation is split into
`ATHENA/misc/ui/editor-mode-toolbar.json` and merged into the same validated
registry at startup.  The first migrated data slice contains the complete
source-mode operation groups and the shared text/source formatting controls.
Its `QTMEditorToolbarPresenter` is constructed per editor but deliberately
inactive while text-block/insert, math and prog groups are still pending.
Foreground color is already a native provider: standard/recent/saved colors and
the native QColorDialog feed a validated `make-with color` actor action; no
legacy `color-menu` expansion is used by the native surface.
