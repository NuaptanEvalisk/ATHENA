# Complete Native Runtime

## Completion Contract

The active goal is complete removal of Scheme execution from ATHENA, not a
single migration batch. Batches are commit and verification boundaries only.
Completion requires no production Scheme execution, no Guile runtime/build or
packaging dependency, and preserved supported editor behavior and actor
ownership. Do not replace Guile with another Scheme interpreter or retain
dead compatibility APIs. Explicit Scheme script execution necessarily ends
with runtime removal; native automation must use the existing interop API.

## Dependency Order

1. Replace keyboard registry closures and user-shortcut wrappers with native
   actions and predicates. Preserve context precedence, prefixes, Unicode text,
   command ordering, inverse lookup and asynchronous loading.
2. Replace name-based native business dispatch and JSON action evaluation with
   typed native interfaces. Reuse the XML interface source where generation
   is needed; do not maintain a second handwritten procedure registry.
3. Move interactive metadata, argument defaults and callback composition off
   Guile objects. Preserve thread ownership and completion/error behavior.
4. Replace actor/UI/observer Scheme handles and delayed callbacks with owned
   native payloads. Remove the exception path that unwinds C++ through Guile.
5. Make style extern functions native; remove retired export hooks. Keep
   document source trees separate from Scheme runtime values.
6. Delete unused bindings, object/marshalling helpers, embedded runtime code,
   script entry points, private Guile build/deployment and platform packaging.
   Update retained checks and developer documentation with their actual native
   replacement, rather than retaining Scheme solely to run obsolete tests.
7. Build with -j16, deploy using the official helper, and verify the affected
   uncertain runtime boundaries. Audit production source/resources/build files
   again before marking the overall goal complete.

## Completed

- `649eb5b4f`: native application startup; application `progs` removed.
- `1614e060e`: direct native calls for several editor/Qt services; dead generic
  module load and buffer notifier API removed. Build/deployment/startup passed.
- Keyboard storage and dispatch now use native callbacks and context identities,
  including user shortcut prefixes and search/spelling/completion dispatch.
  Removed the Scheme closure-registration/catalog APIs. JSON command expressions
  and inverse-lookup values still use the Guile-backed object container; native
  movement and ESC actions no longer evaluate Scheme procedures.
  The -j16 build and deployed actor-owned text/prefix/inverse checks passed.
  The check deliberately does not claim physical cursor-motion coverage:
  direct low-level key calls after source replacement omit `apply_changes()`;
  the real `handle_keypress` entry performs that step.
- The XML generator now emits direct native command entries as well as the
  remaining Scheme-facing wrappers. Named C++ calls prefer native entries;
  receiver, UI dispatch, move/rest arguments and C++ exception unwinding are
  preserved without evaluating a Scheme procedure name. The deployed keyboard
  check overrides Scheme `kbd-space` with an error and still inserts a space,
  demonstrating that the real input path uses native dispatch. Generated
  arity/type, UI scheduling and exception-boundary checks also passed.
  `object` marshalling and the fallback for runtime builtins still remain;
  this is not completion of the value/runtime migration.
- Style `extern` uses generated native entries exclusively. Its security
  allowlist is a native immutable-after-startup set, rather than Scheme
  procedure properties. Version/build metadata functions are native too.
  Retired HTML eqnarray hooks and style module loading/state are removed;
  explicit `use-module` nodes report unsupported functionality. Structured
  argument/result values and the reentrant environment scope are still Guile
  backed and remain in the complete goal.
  The -j16 build and official deployment passed. An actor-owned runtime check
  replaced the Scheme version procedure with a throwing lambda; style extern
  still returned the native version. Structured tuple arguments and explicit
  rejection of style module loading also passed. The temporary profile was
  removed; logs are under `build_qt6/native-style-extern-*.log`.
- Keyboard demand loading accepts native domains only and shares native mode
  predicates with keymap registration. Startup vault opening, pane restoration,
  keyboard prewarming and autosave scheduling no longer construct Scheme
  commands. Delayed editor tasks retain actor/view identity and use native
  continuation mailboxes; ownerless startup work adopts a concrete view before
  executing. At that stage, the transitional `schedule-idle` binding retained
  a callback for namespace export. That binding and consumer are now removed
  by the native export finalization described below; the general delayed
  Scheme queue still needs removal.
  The -j16 build and deployment passed. A headless runtime check opened another
  buffer before the delayed callback and confirmed the callback still mutated
  its original buffer. The first offscreen-GUI probe waited indefinitely because
  GUI `idle_time()` deliberately returns zero for an unfocused view; thread
  inspection showed idle actors, not a deadlock. Production idle semantics were
  preserved, and the ownership check used headless timed scheduling instead.
  The temporary profile was removed. Logs: `build_qt6/native-idle-tasks-*.log`.

- Interactive command fields now read JSON directly, and default providers use
  native dispatch. Keymaps use explicit native prompt descriptors; spelling
  replacement uses the same API. Asynchronous Qt forms return plain strings to
  the originating actor/view before invoking the native command, without
  retaining Scheme procedures or blocking the BufferActor on a modal dialog.
  Removed procedure introspection, lazy-definition forcing, property-table
  initialization and the old server/frame interactive interface. Defunct
  command-input shortcuts now open the existing native command palette.
  Recent-file and argument-history APIs use explicit command names. Their
  formerly Scheme-backed value containers are replaced below.
  The -j16 build and official deployment passed. A temporary headless check
  loaded all keymaps, checked prompt inverse metadata and removal of both old
  interactive bindings, and invoked the print-to-file shortcut without errors.
  This did not fill in or submit the Qt form and does not claim visual or
  end-to-end answer-submission coverage. The temporary profile was removed;
  logs are under `build_qt6/native-interactive-commands-*.log`.

- Namespace export finalization is bound to the newly created export buffer's
  actor/view before active-tab changes can redirect it. Cover generation,
  document update and wait-overlay dismissal use native calls; there is no
  generated Scheme command or focus-stealing switch-to-buffer step. Removed
  the last `schedule-idle` consumer and binding. Cover generation now returns
  a native URL with `url_none()` for failure.
- Interactive/recent-file history is native string-map/vector data, read and
  written as `system/interactive.json`. Missing JSON triggers a one-time data
  import using the existing native S-expression reader, not Guile execution.
  String and URL answers are retained; unsupported obsolete non-text values
  remain in the untouched legacy file. History retrieval no longer reparses
  Scheme values or creates rooted objects. Default providers and actual native
  command marshalling still use the transitional `object` dispatcher.
  Built with -j16 and deployed. Temporary-profile checks passed for legacy
  string/URL import, deduplication/order, hidden/autosave filtering, preservation
  of a two-field answer and fresh-process JSON reload. Removed idle/cover Scheme
  bindings were also absent at runtime. Namespace export finalization was
  compiled and inspected for actor ownership, not exercised as an end-to-end
  namespace export. Temporary data was removed; logs are under
  `build_qt6/native-history-*.log`.

- Save As shortcuts now use the same native asynchronous save frontend as the
  menu. Removed the callback-based choose-file bindings, server/frame bridge,
  dedicated actor UI message, unused chooser widgets and their obsolete tests.
  Independent image insertion and Namespace file dialogs are unchanged.
  OS FileOpen events wait for a concrete startup editor through native tasks,
  then use native document loading, retaining first-current/subsequent-new-window
  semantics without generating Scheme expressions.
- Selection and plain-movement keymaps pass command identities to generated
  native dispatch instead of looking up Scheme procedures. ESC angle/norm
  bracket descriptors also dispatch natively; their intermediate Scheme call
  list and unused public dispatch binding are removed. Argument containers
  remain transitional objects, not a completed runtime-value migration.
  The -j16 build and official deployment passed. A temporary headless runtime
  loaded all keyboard domains and confirmed removal of the three retired
  chooser/ESC bindings. Direct synthetic movement and bracket checks were not
  valid: they bypassed the editor preparation boundary and reported awaiting
  layout/mode preparation. They were stopped, not used to justify cursor fixes
  or claim end-to-end input coverage. OS FileOpen and Save As dialogs were not
  exercised interactively. Temporary data was removed; successful logs are
  `build_qt6/native-file-keyboard-{build,deploy,runtime}.log`.

- Explorer, Namespace, neighborhood and reverse-hierarchy document opening now
  queues native lifecycle work on the GUI owner, without Scheme source quoting
  or expression construction. Global-search UUID fallback and formula AST
  inspection schedule native work on the current document's actor/view.
  Swipe events call native structured-edit handlers directly; the unused empty
  wheel-event callback and binding are removed. Qt business pages no longer
  construct Scheme commands; the general delayed-execution infrastructure still
  remains for editor source mutations and script actions.
- Removed the retired `Subsystems/Gnutls` contact implementation and its three
  unused certificate bindings. Whole-source/resource searches found no caller
  of either contact factory or certificate API outside the dead implementation
  and glue declarations. This removes its invalid certificate callback/session
  handling rather than porting unreachable functionality. Hodarium's active
  `ATHENA/Hodarium/peer_tls.cpp`, system GnuTLS dependency and native TLS
  verification are unchanged.
  The combined -j16 build and official local deployment passed. No new tests
  were added or run for these direct substitutions and dead-code removals;
  the actual window interactions were not exercised. Logs are under
  `build_qt6/native-navigation-cleanup-{build,deploy}.log`.

## Native Tree Callbacks

- Inner-canvas cursor scrolling, mouse scrolling and scrollbar boxes queue
  native tree updates directly on the originating BufferActor. The queued
  continuation retains the original tree identity and uses the existing native
  diff/assignment routines. It does not copy live, non-atomic tree references
  through Qt timers or reinterpret them through Scheme.
- Submission is nonblocking because an actor cannot wait for a free slot in
  its own mailbox. Failed submissions release the retained trees on that same
  actor. Accepted tasks are consumed before the actor's closed transport ends;
  closed-view tasks drop without redirecting to another editor.
- Tree observer announce/done/touched callbacks use generated native command
  dispatch only, with no Scheme lookup fallback. Locus registration consults
  the native secure-function set directly instead of parsing a fabricated
  expression. Callback arguments still use the transitional object container.
- Removed the unused exec-delayed-pause binding, API and queue wrapper. The
  general delayed Scheme queue remains for script actions and old label jumps;
  removing this unused entry is not removal of that infrastructure.
  Built with -j16 and deployed through the official helper. No tests were
  added or run; queue shutdown/drain, owner-thread release and nonblocking
  self-submission were inspected in the transport implementation. Interactive
  canvas scrolling was not exercised. Logs are under
  `build_qt6/native-tree-callbacks-{build,deploy}.log`.

Next callback audit: an XML parse of shipped ATS resources found named script
callbacks for reference preview and named action callbacks for fold/unfold,
todo toggling and cloud-todo toggling, with no relay or observer instances.
Their native entry points should replace the generic script evaluator; do not
introduce another expression interpreter. Relay event dispatch in
`Typeset/Boxes/Modifier/change_boxes.cpp` is the other secure-eval consumer.

## Known Outstanding Failure

- A C++ exception escaping a Scheme VM call can leave the dynamic-wind stack
  inconsistent. See the concrete core evidence in `native-application-ui.md`.

Do not describe this as fixed by removal of `progs`. It remains an explicit
part of the complete runtime-removal work.
