# Native Application Menus And Toolbars

Updated: 2026-11-02. The application shell owns menus, toolbars, command palette
and application shortcuts. The former Scheme UI migration inventories and the
October command-loss handoff are retired; they are not current TODO lists.

## Sources Of Truth

- `ATHENA/misc/ui/application-shell.json` and the other UI JSON files author
  presentation, command references, grouping and conditions.
- `src/Subsystems/Qt/QTMCommandRegistry.cpp` owns command registration,
  dispatch and dynamic providers.
- `QTMDocumentMenuCommands.cpp`, `QTMFocus*Commands.cpp` and other provider
  modules supply context-dependent entries.
- `QTMToolbarController.cpp` owns toolbar materialization.
- `src/Edit/Interface/native_editor_actions.cpp` executes editor actions on
  the owning BufferActor. State comes from published editor/pane snapshots.
- `ATHENA/misc/input/` holds keyboard declarations. Registry, event dispatch
  and generic editing are native; `progs/kernel/gui` is removed.

Native UI ownership does not imply every underlying business operation has
already lost its Scheme implementation. Follow the actual command handler
when assessing a remaining native rewrite; do not infer it from a command ID.

## Lessons From The Command-Loss Bug

- A valid registry proves that declared commands resolve, not that an older
  menu's useful capabilities were all migrated.
- Do not derive semantic menu membership solely from command-ID prefixes.
  Menus and toolbars should reference the same commands through authored data.
- Menu conditions must be consumed by the presentation path, not just parsed.
- An empty source atom may carry UUID metadata. Use text-content comparison
  for empty-input conditions, not equality against a metadata-free tree.
- Property panels can legitimately replace older menu branches. Distinguish
  changed presentation from lost capability before restoring anything.

The user-provided-icons toolbar, Scheme API documentation UI and docgrep were
intentionally removed. Plugins retain their Plugins menu. Do not restore those
retired surfaces from historical inventories.

Real-window IME validation after native keyboard/event dispatch is complete,
confirmed by the user on 2026-11-01. Historical commit `abdd53b8e` records an
earlier unverified checkpoint, not an open acceptance item.

## Passive-View GUI Dispatch

During the 2026-11-02 file-frontend migration, an isolated offscreen GUI process
running `-X -c source.ath output.txt -x ...` completed conversion and its native
file API assertions, but did not exit after the continuation requested
`(exec-global (lambda () (quit-TeXmacs)))`. The equivalent headless check exited
successfully. The conversion continuation runs on the source actor, whose view
was not opened in a visible document window. Its endpoint existed, but no canvas
was registered to drain its UI effects. Global commands and native application
continuations now dispatch through the GUI event queue when the originating
view is unattached, instead of leaving requests in that endpoint's queue.

Do not use `primitive-exit` from a BufferActor to bypass this observation: a
temporary diagnostic did so and crashed during process exit. No such bypass
was added to production. Offscreen conversion checks verified normal exit both
through `exec-global` and through the native `safely-quit-ATHENA` entrypoint.
The lower-level `quit-TeXmacs` server binding is not an actor-dispatching
application entrypoint; calling it directly from an actor is rejected by the
realtime-save context check. No save-policy change was needed. Temporary
checks and logs are removed after the batch.

## Native Link Pages

`build-navigation-page` now uses a generated native binding. It retains the
source actor, style and master, loads unresolved locations through the native
document frontend, waits for the destination actor's typesetting command, and
captures detached target previews there, and returns to the source actor to
build the auxiliary page. Locus registration is a typesetting operation and
its registry is actor-local: file-load completion alone is insufficient, and
the source actor cannot read the target's live loci. The former Scheme
idle-delay loop and lazy registration are removed.

`go-to-id` now resolves and positions on the destination actor. Resolution
attempts belong to one navigation request rather than a process-wide set, so a
later jump to the same document is not suppressed. `go-to-url` uses native
protocol dispatch and destination-actor post-load positioning, including vault
UUID navigation, legacy map navigation and link repair. No vault Scheme
conditional handler wraps that default. `progs/link` and its module references
are removed.

Focused runtime checks cover cross-document loading, auxiliary page creation,
source-master preservation, repeated locus navigation and target preview
transfer. The live-locus fixture uses local macro expansion of a declared tag.
A legacy raw `locus` source fixture remains rejected by the v3 child-role
contract; raw legacy-locus persistence compatibility was not added.

## Native Application Frontends

The `progs/athena` business migration is complete. Its former modules and the
empty directory are removed, rather than moved to another Scheme directory.
The native entrypoints are declared in `src/Scheme/Glue/native.xml`; generated
marshalling remains a bridge to the retained Guile runtime.

- `application_lifecycle.cpp` and `document_save_frontend.cpp` own document
  loading, recovery, import/export, revert, save, close and application exit.
- `document_print.cpp` and the existing Qt print/export services own printing
  and PDF workflows. `document_style_commands.cpp`, `application_preferences.cpp`,
  `application_tools.cpp` and the `view_*` frontends own document defaults,
  appearance, application tools and view commands.
- `vault_frontend.cpp`, `vault_navigation.cpp`, `reference_markup.cpp` and
  `materials_edit.cpp` delegate to the existing vault, node-reference and
  artifacts services. Link repair edits are submitted to each buffer owner;
  native candidate pages reuse vault file and anchor readers.
- `codex_completion.cpp` prepares selected content and figures, owns request
  files, creates document or floating-buffer placeholders, and imports results
  on the target actor. The existing Qt bridge process and options dialog are
  reused. A local fake bridge verified both destinations, cursor preservation,
  deleted placeholders and request-file cleanup without model requests.

The later native-kernel migration completed the TMFS and preference registries.
`init-athena.scm` now orchestrates native startup and declares interactive command
metadata; secure command names are JSON. Page construction, navigation, repair,
editing and preference callbacks are C++. `progs/kernel` has been removed.

Final registration checks exercise TMFS loads, titles and permissions after
normal startup with no former application module loaded. Batches were built
with `-j16` and deployed through `tools/deploy-local-runtime.sh`; temporary
runtime homes, scripts, generated figures and logs were cleaned after use.

## Generic, Dynamic And Source Frontends

The former `progs/generic`, `progs/dynamic` and `progs/source` implementations
now live in native code:

- `generic_frontend.cpp` owns insertion dialogs, figure wrapping, document
  updates and global-search result navigation. Destination positioning is an
  owner task after loading, rather than a timed Scheme continuation.
- `presentation_commands.cpp` owns folds, switches, overlays, slide editing,
  traversal, presentation expansion and navigation markup. Slideshow clipboard
  insertion uses `edit_select.cpp`'s normal version and source-identity pipeline.
- `source_commands.cpp` owns source activation, macro definition lookup/editing,
  style extraction and parameter/theme analysis.
- `Typeset/Env/title_markup.cpp` owns title, author and abstract rendering,
  including note deduplication and author clustering.

Bindings in `src/Scheme/Glue/native.xml` retain the entrypoints needed by the
keymap dispatcher and style `extern` calls. The removed
modules are not imported or relocated into another Scheme directory.

### Custom shortcuts

`user_shortcuts.cpp` stores custom bindings in
`$ATHENA_HOME_PATH/system/shortcuts.json` using Qt JSON and atomic `QSaveFile`
replacement. Example:

```json
{
  "version": 1,
  "bindings": [
    {"key": "C-A-t", "action": {"op": "make", "tag": "theorem"}}
  ]
}
```

Actions use the existing native editor-action schema, also used by menus and
toolbars. The Qt shortcut editor validates these JSON actions. Custom bindings
form a separate override layer, including multi-stroke prefixes; removing one
reveals the default binding, and later loading a keymap cannot replace a user
override. There is no `shortcuts.scm` reader or migration tool.

### Completion checks

The native runtime passed focused checks for ordinary, clustered, abbreviated
and AMS title expansion, abstract options, custom shortcut overrides and
multi-key prefixes, folding/unfolding, and macro parameter discovery. These
checks ran in a temporary profile and did not exercise every presentation or
clipboard workflow. The temporary scripts and runtime data were removed after
use. The three retired source directories are removed as well as their files.

## Native Startup (2026-11-02)

`progs` is removed. The server directly initializes preferences, node groups,
keyboard domains, user shortcuts and dialogs. BufferActor view initialization
sets the default style and vault font for unnamed documents. Command argument
labels, dynamic default providers, menu predicates and interactive flags are
declared in `misc/command-properties.json` and published by the native loader.
There is no application Scheme bytecode build, refresh or packaging step.
Explicit command-line scripts and the private Guile runtime remain supported.

The deployed -j16 build passed startup, metadata/provider lookup, new-document
style initialization and the generated page-format applicability binding with
no application Scheme sources or bytecode. Temporary probe files were removed.

The probe also exposed a separate exception-boundary issue: invoking `insert`
directly on a newly created BufferActor before cursor-environment preparation
throws `Cursor environment is awaiting preparation for mode`. The C++ exception
escapes the Scheme VM call and is caught by actor dispatch, but the subsequent
`scm_c_with_fluid` cleanup asserts in `scm_dynstack_unwind_fluid`. Core PID
1226975 identifies `run_scheme_handle`, command 8, not `initialize_view`;
`actor_dispatch_request.failure` holds that runtime_error. Evidence is in
`build_qt6/native-startup-core-{context,message}.log`. This exception cleanup
issue was not changed by the bounded startup migration and remains unresolved.

## Direct Native Business Calls (2026-11-02)

The first post-`progs` batch removes Guile dispatch from macro activation,
source-mode preferences, LaTeX expansion preferences, autosave timestamp
comparison, saved-buffer timestamp lookup, file chooser format metadata and
the table/cell property pane. Table snapshots and edits still execute in their
existing BufferActor continuations; Qt does not read editor state directly.
The conversion registry still returns some `scheme_tree` values, which are
native trees, not evaluated Scheme expressions.

The obsolete buffer notifier API, actor command and state are removed together
with its XML binding. Its only remaining entry was the binding itself, and its
`buffer-initialize` and `buffer-notify` callbacks no longer existed. Generic
macro creation also no longer attempts to load the deleted `generic-edit`
module.

The -j16 build and official local deployment completed. A temporary headless
startup check confirmed that the deployed runtime no longer defines
`buffer-attach-notifier`; it exited successfully. No editor regression suite
was added or run for these direct-call substitutions.

This is not complete Guile removal. Further bounded work remains:

- XML-defined named calls now use generated native dispatch. Remaining runtime
  builtins and procedure-valued callbacks still enter Guile.
- Keyboard registry actions, mode conditions and user shortcuts now use native
  callbacks (`3202f1eaa`); JSON expression arguments and inverse lookups still
  use Scheme values. See `scheme-runtime-removal.md` for the complete goal.
- UI callbacks, observers and actor Scheme handles retain Guile objects;
  ownership must survive their replacement. Interactive fields now use native
  JSON metadata and asynchronous Qt forms, while argument/recent-file history
  uses native containers and JSON persistence.
- Style `extern` now selects only generated native entries, without Scheme
  expression evaluation or fallback. Argument/result marshalling and the
  reentrant environment scope still depend on Guile. Explicit command-line
  scripts also remain until the value/runtime migration is complete.
- The obsolete GnuTLS contact layer and its unused certificate bindings were
  removed after confirming no callers. This also removes its broken certificate
  callback/error handling. Hodarium's independent native TLS remains in use.
- The obsolete `ext-tmhtml-eqnarray*` export hook and style Scheme-module
  loading are removed. A `use-module` node now reports an explicit error.
- The exception-boundary failure described above is still separate work.
