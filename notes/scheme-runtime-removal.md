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
  remains for old label jumps and explicit startup scripts; source mutations
  and document actions are migrated below.
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
  general delayed Scheme queue remains for startup scripts;
  removing this unused entry is not removal of that infrastructure.
  Built with -j16 and deployed through the official helper. No tests were
  added or run; queue shutdown/drain, owner-thread release and nonblocking
  self-submission were inspected in the transport implementation. Interactive
  canvas scrolling was not exercised. Logs are under
  `build_qt6/native-tree-callbacks-{build,deploy}.log`.

## Native Document Actions

An XML parse of shipped ATS resources found named script callbacks for reference
preview and named action callbacks for fold/unfold, todo toggling and cloud-todo
toggling, with no relay or observer instances. All these named callbacks have
generated native entries and remain on the secure-function list.

- Document actions dispatch a native command name with literal argument values.
  No Scheme symbol lookup, quoted-expression construction, lambda evaluation or
  apply remains. Embedded Scheme expressions, including lambdas, are explicitly
  refused. Trusted actions and the existing allow/prompt/deny preferences retain
  their behavior; approval now uses an asynchronous native form.
- Pending callbacks live inside their BufferActor, bound to their original view.
  GUI messages carry only IDs and approval state, not live tree/object payloads.
  Resolution removes the callback before invocation; cancellation, wrong-view
  resolution, view destruction and actor shutdown release it on the owner thread.
  Canvas deferred updates now use the same owner-held callback mechanism.
- Relay boxes use secure native dispatch with literal event/coordinate/payload
  arguments. The recursive Scheme expression checker, secure-eval bindings and
  unused eval_secure wrapper are deleted. `native_command_security` now contains
  only the native name allowlist. Transitional argument objects still need the
  final value-container migration.
- Built ATHENA.bin and the targeted buffer_actor_test with -j16 and deployed
  through the official helper. Only nativeCallbacksRemainOwnedUntilResolution
  was run: execution, duplicate resolution, cancellation, wrong-view resolution,
  view destruction and actor shutdown passed, including owner-thread destruction
  of callback payloads. The approval dialog was not exercised interactively.
  Logs: `build_qt6/native-document-actions-{build,deploy,owner-test}.log`.

## Native Label Navigation And Tag Selection

- Cross-document label jumps use the native document loader completion and
  submit navigation to the loaded document's published actor/view. The label
  crosses threads as a std::string; no evaluated load-buffer/go-to-label string
  or implicit current-window continuation remains.
- Removed the dangling show-hidden-part call. Its implementation and document
  part-selection controls were deliberately removed in commit 26fd4296e; this
  was not a surviving folding implementation to port. Existing source label
  searches and show_cursor_if_hidden remain unchanged.
- Tree command selectors accept tags and lists of tags, not Scheme procedure
  predicates. No product caller of the procedure form remains. Unsupported
  selector values report an explicit argument error.
- Built ATHENA.bin with -j16 and deployed through the official helper. No new
  tests were added or run for these direct dispatch changes; interactive
  cross-document navigation was not exercised. Build and deployment logs are
  `build_qt6/native-label-navigation-{build,deploy}.log`.

## Native Startup Task Sequencing

- Positional file arguments call the native loader directly, retaining the
  first-document/subsequent-window behavior. They no longer build load-buffer
  expressions or wait for an ownerless Scheme delayed queue.
- Conversion and quit options are ordered native tasks. Each conversion exports
  its own loaded buffer before its C++ completion advances the task queue.
  Automatic headless quit stays on the application thread. The internal
  command-line-convert Scheme binding and the server's my_init_cmds expression
  accumulator are removed.
- Explicit -x scripts are still a remaining Scheme feature, not native command
  execution. After conversion they run synchronously on that source document's
  actor before subsequent tasks continue; no Scheme continuation wraps the
  conversion pipeline. Arbitrary script support and its tooling consumers still
  require migration before Guile removal.
- Import options are decoded into native load flags before GUI dispatch instead
  of retaining a Guile handle. The ToC maintenance worker calls the strict native
  loader directly rather than evaluating a fabricated Scheme expression.
- Built with -j16 and deployed through the official helper. A headless runtime
  check converted text to v3 .ath, then that output to HTML, then exported text
  through a following -x command. All outputs contained the sentinel, the
  removed command-line-convert binding was absent, and automatic exit returned
  zero. No permanent test was added. The temporary profile and documents were
  removed; logs remain under `build_qt6/native-startup-tasks-*.log`.

## Native Background Values

- Removed the unconsumed pattern/gradient/picture selector procedure APIs and
  their old Scheme initial/result conversion bindings. The document-background
  command and the separate native slide-properties selector remain supported.
- Document background selection now constructs native trees directly from Qt
  value strings and uses an optional result for cancellation. Its existing
  default dimensions, gradient colors, effect nesting and relative-path policy
  are unchanged; there is no Scheme list round trip or procedure invocation.
- Deleted only the obsolete selector assertions from generic-extra-editor-test;
  its paragraph editing and spelling checks remain. No replacement test is
  needed for these direct value conversions.
- Built with -j16, deployed and checked the resulting binary: all five retired
  bindings are absent. No editing regression suite or interactive background
  selection was run. The temporary profile was removed; logs remain under
  `build_qt6/native-background-values-{build,runtime,deploy}.log`.

## Direct Editor Value Operations

- Thumbnail widths use the existing native numeric formatter rather than
  invoking Guile number->string. Focus-tag naming extracts its symbol through
  the value API, calls native theme lookup directly and recurses on native
  strings rather than through the command dispatcher.
- These changes were included in the subsequent native-dispatch-marshalling
  build and deployment. No tests were added or run for the direct substitutions.
- This audit identified object.cpp/object.hpp as the central data boundary:
  constructors then allocated Guile-rooted objects and hashing called Scheme.
  The subsequent native payload work below replaces ordinary scalar/tree
  storage; lists, other legacy values and hashing still require migration.

## Native Dispatch Marshalling

- The native generated entry no longer unconditionally extracts SCM arguments
  and reconstructs rooted objects. Scalars and native tree/URL/path/patch values
  use value accessors; object and procedure payloads pass directly; arrays and
  lists validate their elements and preserve their order. Ordinary results use
  object constructors and object results return unchanged.
- Scheme-facing wrappers remain separate. The explicit residual bridge handles
  types without native value APIs, including bytes, uint and callback handles,
  as well as unspecified/structured Scheme results. The object implementation
  still has a legacy representation alongside the native payloads below:
  removing the generated round trip is not removal of the runtime dependency.
- Two targeted generator compilation/execution checks cover native arity/type
  checking and C++ exception unwinding versus Scheme nonlocal exits. Both pass;
  their native entry checks assert no object-to-SCM argument conversion occurs.
- The production binary builds with -j16 and is deployed. A headless startup
  confirms representative generated bindings are registered; this is not an
  interactive editing check. Temporary profiles are removed. Logs are under
  `build_qt6/native-value-marshalling-{build,runtime,deploy}.log`.

## Native Object Payloads

- C++ bool/int/double/string, symbols, keywords, source trees, URLs,
  modifications and patches now retain native payloads. Construction, type
  checks, normal access and same-representation equality do not allocate a
  Guile root. Source trees retain their existing identity, not serialized copies.
- Explicit object-to-SCM access lazily creates a rooted bridge for remaining
  Scheme consumers. Exported strings remain readable after Scheme mutation;
  repeated bridge access preserves identity. Native numeric equality retains
  signed-zero distinction and NaN equality, and integer-to-double access remains
  accepted. This is a temporary boundary, not a replacement interpreter.
- Procedures, parsing/printing and mixed native/legacy equality still use the
  old representation; sequence storage was subsequently migrated below. The
  native payload does not make
  object/tree reference counts thread-safe; actor ownership remains mandatory.
- Startup exposed a URL comparison recursion: the non-const URL equality
  member was unavailable to variant comparison, which implicitly constructed
  objects and recursed. URL equality/inequality are now const, retaining their
  existing tree comparison. GDB identified this transition before deployment.
- Built the application and targeted check with `-j16`. The
  `nativeValuesPreserveBridgeSemantics` check passed, including construction and
  access before Guile startup, mutable bridge strings, source-tree identity,
  numeric semantics and URL equality. Isolated application startup printed
  `NATIVE_OBJECT_BOOT_OK` and exited normally; its profile was removed. This
  does not claim interactive editor verification. Logs are in
  `build_qt6/native-object-storage-{build,values-test,runtime,deploy}.log`.

## Retired Save And Vault Chooser Callbacks

- Removed the unused `on-saved` Scheme option and the three unconsumed
  low-level save bindings. Save requests no longer retain a Scheme handle
  across dialogs. Native save-before-close completion still runs only after
  success or an unchanged document, and Save As cleanup precedes completion.
- Removed the unused `vault-choose-link` Scheme entry and its mutex-protected
  request/handle registry. Existing native wikilink/transclusion callers retain
  the Qt chooser and actor-owned result delivery.
- This batch is source-reviewed without additional tests. It is included in
  the consolidated callback cleanup build recorded below.

## Native-Only Reference Export Continuations

- All product reference-export callers already use the native callback API
  (printing, selection image export, reference markup and AVD). Removed the
  unused Scheme export entry, its handle ownership and alternate dispatch path;
  kept reference discovery, source/selection validation and native completion.
- The removed Vault chooser was the last producer of
  `invoke_scheme_handle_tree`. Removed that actor command and its handler as
  well. Removed only the legacy Scheme export-entry assertion from the bridge
  test; the other node-reference assertions remain.
- Included with the save/chooser cleanup in the consolidated build below.

## Native Document Browse Completion

- Removed the Scheme closure overload and rooted conversion continuation from
  document browsing. Product completion callbacks remain `std::function<void(url)>`;
  callback-free callers and the generated browse binding take only the URL.
- The artifact range check now opens from its global context, checks the target
  buffer and explicitly enters its actor before reporting success. It no longer
  requires the removed browse callback. No additional test cases were added.
- Consolidated build with `-j16` succeeded, including the save/chooser and
  reference-export removals. Isolated startup verified that the five retired
  bindings are absent and `load-browse-buffer` remains registered, printed
  `NATIVE_CALLBACK_CLEANUP_OK` and exited normally. Deployed through the runtime
  helper and removed the check profile. No interactive editing or full export
  check was run. Logs: `build_qt6/native-callback-cleanup-{build,runtime,deploy}.log`.

## Actor-Owned Materials And Completion Nodes

- Materials chooser callbacks now stay in the originating BufferActor's native
  callback registry. Qt carries an ID and independently stored result, not the
  callback or its attached tree. Appending references consequently retains the
  target tree directly on its owner instead of rooting it through Guile.
- AI completion retains its placeholder in the same actor-owned registry;
  process completion submits only the callback ID. View destruction and actor
  shutdown release the retained node through the existing callback lifecycle.
  Process-owned request data continues to retain temporary files until process
  completion, even if its editor has closed.
- No new tests were added: this reuses the previously checked callback
  execution/cancellation/view/shutdown ownership mechanism. Included in the
  consolidated native owner/value build below, but not interactively exercised.

## Native Character Values And Pattern Access

- Tree pattern selection remains used by utility markup and tree editing. It
  now uses value-interface pair/keyword queries and ICU UTF-8 scalar decoding
  instead of directly invoking Guile's string-to-list operation. Character
  values have native payloads and only materialize SCM at an explicit bridge;
  characters remain distinct from integers. Invalid UTF-8 is rejected.
- Conversion association-list validation also uses the value API, removing its
  direct SCM dependency. The later native pair storage change below removes
  eager list conversion, but legacy values and predicates still need migration;
  this does not claim a Scheme-free selector or conversion value model.
- Included with the actor-owned Materials/AI completion changes in the build
  and focused runtime check below; no permanent tests were added.

## Native Empty Values

- Default `object` and `null_object` now store a native empty-list value instead
  of acquiring Guile roots. Empty-list, empty-path and empty-number-array
  predicates retain their existing semantics. Explicit SCM access returns the
  immutable empty-list immediate without rooting it. Nonempty lists are covered
  by the subsequent storage migration below.
- Built with `-j16`, including the preceding owner and character changes, and
  deployed with the runtime helper. Isolated startup verified empty selection
  paths and supplementary Unicode scalar matching through `tm-select`, printed
  `NATIVE_OWNER_VALUES_OK` and exited normally. Removed its profile. Logs:
  `build_qt6/native-owner-values-{build,runtime,deploy}.log`. No interactive
  Materials chooser or external AI completion request was exercised.

## Native Pairs And Sequence Storage

- `cons`, list construction, `car`/`cdr` and derived accessors now retain native
  pairs and shared tails. Paths, string/tree lists and numeric arrays construct
  and extract native sequences rather than eagerly converting through SCM.
- Explicit bridge creation walks the tail iteratively and caches each pair's
  identity. Once exposed, reads use that bridge so Scheme pair mutations remain
  visible through native aliases. Legacy lists may still form a native pair's
  tail. Proper-list checks reject improper/cyclic bridged lists.
- Native list equality and uniquely owned tail release are iterative. The
  pair implementation alone has reference-count access for tail ownership;
  callbacks still require actor confinement.
- This does not remove legacy input values, procedure/widget handles, the
  Scheme reader/printer or runtime execution. Those remain separate work.
- Built application and focused value check with `-j16`. Before Guile startup,
  the check constructs, compares and releases two 50,000-item lists and converts
  paths/numeric arrays. The runtime check passed shared-tail identity, pair
  mutation visibility, mixed tails and cyclic-list rejection. Application
  startup and Unicode selection passed with `NATIVE_PAIR_STORAGE_OK`. Deployed
  using the runtime helper; removed the startup profile. Logs are under
  `build_qt6/native-pair-storage-{build,values-test,runtime,deploy}.log`.

## Native Bytes And Structure Conversion

- Byte payloads have a distinct native value kind, including valid UTF-8 bytes;
  they never become text implicitly. Explicit legacy bytevector bridges retain
  mutation visibility through the value interface.
- Structure-tree construction, extraction and content conversion use native
  value operations. The existing quoted-tree representation and native quoting
  functions remain unchanged; this is not a new interpreter or parser.
- LaTeX options and structure intermediates no longer round-trip through SCM.
  The generic conversion executor uses native byte values for image payloads
  and native values for serialized structures. Generated native bindings marshal
  bytes and structure-tree results through the same interfaces.
- The subsequent native buffer UI change below removes the property-pane
  `qt_call_in_buffer` handle boundary. Native object reference counts remain
  actor-confined; shared cross-thread object instances are not a replacement.
- Removed two invalid `is_nil` checks on publisher metadata structure trees;
  absent title commands and empty IEEE fields use explicit tuple emptiness.
- Built application and focused value target with `-j16`. Pre-runtime binary
  content round-trip and the existing native value bridge check passed,
  including bytevector mutation and generated byte/structure bindings.
  Application startup, UTF-8 byte conversion and LaTeX snippet export passed
  with `NATIVE_STRUCTURE_VALUES_OK`. Deployed with the runtime helper and
  removed the startup profile. Logs are under
  `build_qt6/native-structure-values-{build,test,runtime,deploy}.log`.

## Native Buffer UI Calls

- Replaced every `qt_call_in_buffer` consumer with synchronous native actions
  through `qt_with_buffer`. Page, paragraph, metadata, slide background and
  handwriting panes invoke their native editor functions directly, without
  function-name lookup, object arguments or rooted Scheme result handles.
- The caller waits on the existing actor response condition without pumping GUI
  events. Inputs are local immutable values or detached tree copies; returned
  editor trees are copied on their actor. A completion flag is published only
  after the action and frozen export-reference checks finish successfully.
- Website PDF export, ToC maintenance and vault bugcheck use the same native
  action path. DataArt preparation has a typed optional-tree result, shared by
  website export and interactive printing. Its payload already detaches source
  and body trees. The subsequent content/value cleanup removes the unused
  Scheme-facing DataArt target predicate and preparation declarations.
- Added typed native initialization-text and insertion-arity entry points so
  these UI calls do not construct temporary Scheme command arguments.
- Built with `-j16` and deployed with the runtime helper. Ran the actual headless
  ToC maintenance worker on a temporary XML v3 document: it invoked the native
  actor action, saved successfully, wrote `ok`, and exited zero. Removed its
  document/profile directory. Logs are
  `build_qt6/native-buffer-ui-{build,fixture,runtime,deploy}.log`.
  No interactive pane sweep or unrelated test suite was run.

## Native Content And Void Results

- Generated native void actions return a native unspecified marker rather than
  acquiring a Guile root. It remains distinct from false and the empty list;
  explicit legacy conversion returns Guile's immutable unspecified immediate.
- Content arguments use native structural validation and conversion, including
  symbol-headed lists and the distinct `raw-data` byte payload. Tree, content,
  URL, modification and patch predicates use the corresponding value interfaces
  rather than forcing their arguments through raw SCM marshalling.
- Removed unused `data-art-pdf-target?` and `data-art-prepare-export` bindings
  and their object adapters. Supported printing uses the typed URL/optional-tree
  APIs; the public DataArt header no longer depends on Scheme values.
- The two focused generated-wrapper checks passed (native/UI invocation and
  exception unwinding). Built with `-j16`; runtime predicates accepted nested
  binary content, rejected invalid raw-data and improper lists, and verified
  the retired DataArt bindings were absent. Startup printed
  `NATIVE_CONTENT_VALUES_OK` and exited zero. Deployed with the runtime helper,
  removed the profile, and retained logs under
  `build_qt6/native-content-values-{build,runtime,deploy}.log`.
- `exec-delayed` and `exec_delayed_global` have no callers. A follow-up audit
  found that the same queue also services `exec-global`, which audit/migration
  scripts and runtime checks still use. The queue cannot be removed wholesale
  until those consumers are migrated. Native editor scheduling is separate.

## Retired Repeating Scheme Scheduling

- Deleted `exec-delayed`, `exec_delayed_global`, timer/repeat state, automatic
  current-editor adoption, delayed UI scheduling and completion messages.
- `exec-buffer` remains a supported script boundary for now. Its actor command
  executes once and releases its handle directly on success or failure, rather
  than publishing a GUI completion event merely to release the handle.
- The remaining global script queue preserves source-view selection, bounded
  execution batches and shutdown cleanup. It retains unexecuted handles if a
  callback throws. Empty queues no longer set the GUI update interval from an
  expired delayed-command timestamp; pending work still requests immediate
  updates. Native scheduled tasks are unchanged.
- `exec-global` and `exec-buffer` consumers must still be migrated before the
  remaining one-shot queue and script runtime can be removed. This is not a
  Scheme-free event loop yet.
- Built with `-j16`. An isolated headless runtime verified `exec-delayed` was
  absent and dispatched 45 one-shot global callbacks from a buffer actor,
  exceeding the per-batch call budget. All completed, printed
  `ONE_SHOT_GLOBAL_CALLBACKS_OK` and exited zero. Deployed with the runtime
  helper and removed the profile. Logs:
  `build_qt6/native-one-shot-commands-{build,runtime,deploy}.log`.

## Retired Scheme Widget And Command Bridges

- Removed unconsumed Scheme command adapters, widget promises and the legacy
  ADS tool-pane API. Native ADS panes and their lifecycle remain unchanged.
- Deleted the corresponding actor opcodes, dispatch handlers and widget
  transport registry. Native continuations/actions and the still-used one-shot
  script boundary remain separate.
- Removed the obsolete widget-promise test and assertions for retired glue
  declarations. No replacement tests are needed for APIs with no consumers.
- The initial deletion retained widget/command smob marshalling and generator
  types; the subsequent cleanup below removes them.
- Built with `-j16`; headless startup confirmed all five retired bindings were
  absent, printed `RETIRED_WIDGET_BRIDGE_OK`, and exited zero. Deployed with the
  runtime helper and removed the temporary profile. Logs are under
  `build_qt6/native-retire-widget-bridge-{build,runtime,deploy}.log`.

## Retired Widget Values And Closure Constructors

- Removed unused widget, widget-array, command and widget-promise marshalling,
  blackbox printing branches, object accessors and generator types. The XML
  registry has no signatures using those types. Qt's native widget promises
  are separate and remain supported.
- Deleted all unused `scheme_cmd` overloads, which constructed and evaluated
  Scheme lambdas. Explicit runtime evaluation remains a separate removal task.
- Namespace creation now opens the document through the existing typed native
  entry point instead of calling the named `load-buffer` bridge.
- Regenerated all interfaces with `athena_glue -j16` successfully and checked
  for remaining retired symbol references. No new bindings or runtime behavior
  were introduced. Full compilation/deployment was subsequently completed in
  the combined native document entry-point build below.

## Native Document Entry Points

- Quick Switcher, Vault opening, bugcheck, AUDMAP filesystem opening and the
  application menu call the existing typed document-opening API directly.
  Their GUI/actor dispatch and exception reporting remain in their existing
  owners. The native Revert command also calls its application implementation
  directly, and custom-style activation calls `document_set_main_style`.
- No fixed C++ calls to `load-buffer`, `load-document`, `revert-buffer` or
  `set-main-style` remain. Their script interfaces are retained for remaining
  script consumers; this is not a claim of a Scheme-free document lifecycle.
- Observer bindings still serve position and tree-pointer interfaces, including
  structural identity checks. They were audited but not removed as dead code.
- Built the application with `-j16`, including the preceding widget value and
  generator cleanup, and deployed with the official runtime helper. Logs:
  `build_qt6/native-document-entrypoints-{build,deploy}.log`. No extra regression
  suite was run for these direct substitutions and unconsumed API removals.

## Native Structured And Geometry Dispatch

- Structured navigation, insertion/removal, swipe and geometry recursion now
  pass native function pointers through their focus/parent helpers rather than
  invoking names and constructing temporary argument lists at each node.
- Predicates, extremal movement loops, geometry reset and pinch completion call
  their existing native implementations directly. The ordering of node-specific
  handlers, parent traversal and focus restoration is unchanged.
- Both modules have no remaining `call` invocation. Some APIs still use object
  values (variant lists, node groups and special navigation direction); these
  remain part of the value-system removal, not hidden behind a new dispatcher.
- Compiled both changed translation units with the generated build rules and
  `-j16`; log: `build_qt6/native-structured-dispatch-build.log`. No tests were
  added or run for this direct dispatch replacement. Full linking/deployment
  was subsequently completed in the combined editor-dispatch build below.

## Native Insertion And Variant Calls

- Replaced all fixed C++ `make`, `numbered-toggle` and `variant-circulate`
  calls with the existing typed insertion/text/math implementations. This
  covers section/header creation, source mode, format commands, keyboard
  variants, labels and JSON native editor actions.
- Retained insertion's shared selection-wrapping implementation. Omitted
  arities still use `-1`; JSON actions continue treating any negative arity as
  omitted. Removed the obsolete comment claiming wrapping remained in Scheme.
- Compiled the eight affected translation units with `-j16`; logs:
  `build_qt6/native-insertion-{dispatch,actions}-build.log`. No regression tests
  were added or run. These changes and structured/geometry dispatch were then
  linked and deployed in the combined editor-dispatch build below.

## Native Link Entry Points

- Mouse/keyboard focus events, hover/double-click handling and peek navigation
  call native link functions directly; no fixed `go-to-url` or `link-follow-ids`
  C++ name dispatch remains.
- URL navigation now has a typed optional source path. An absent source and a
  provided empty path remain distinct for history recording. The script adapter
  only unpacks arguments. Native string navigation preserves the previous
  Windows `url_system` conversion rather than changing path interpretation.
- URL disambiguation accepts its native URL array directly instead of creating
  an object list merely to iterate it again. The script-facing adapter remains
  for its existing binding.
- Compiled the six affected implementation units and the string-entry follow-up
  with `-j16`; logs: `build_qt6/native-link-dispatch-build.log` and
  `build_qt6/native-link-string-entry-build.log`. No new tests were needed for
  the direct API transition. Full linking/deployment was subsequently completed
  together with the preceding structured/insertion batches below.

## Native Format And Focus Calls

- Format and generic editor commands call their native label, parameter,
  customizable-field and return implementations directly. Neither module has
  remaining `call` invocations; object-valued interfaces themselves still need
  migration.
- Removed the obsolete slideshow clipboard distinction: the existing binding
  already invokes the same editor selection methods. Cut/paste continues on
  the owning editor, including balloon wrapping.
- Built the whole application with `-j16` and deployed with the official helper,
  including structured/geometry, insertion/variant and link-navigation changes.
  Logs: `build_qt6/native-editor-dispatch-combined-{build,deploy}.log`.
  No unrelated tests or extra regression suite were run.

## Native Focus Snapshot Queries

- Focus toolbar predicates use typed native function pointers instead of
  procedure names, preserving existing exception defaults. Label, variant,
  display-name and slide queries call their native implementations directly.
  `edit_interface.cpp` no longer invokes `call`.
- Added a native optional-text format display-name query. The existing registry
  remains the sole data source; the legacy serialized-name function delegates
  to it. The toolbar no longer serializes and parses a language display name.
- Compiled both affected implementation units with `-j16` (including the final
  snapshot cleanup); logs: `build_qt6/native-focus-snapshot-build.log` and
  `build_qt6/native-focus-snapshot-final-build.log`. No new tests were needed.
  Full linking/deployment completed in the combined runtime-dispatch build below.
  Object-valued metadata still needs migration.

## Native View Actions And Slide Export

- Viewport mode actions use a native text preference setter. The object-valued
  script adapter delegates to it, retaining the unchanged-value notification
  suppression in one implementation. Focus variant actions call their existing
  native provider directly.
- Slide PDF preparation invokes the native presentation transformation on the
  same owning actor, removing the outdated reference to `dynamic/fold-edit`.
- The backtrace preference still controls Guile's own debugging option. It is
  not ATHENA's `debug_set` facility; remove that preference with the runtime
  rather than silently changing its meaning during this batch.
- Compiled the three changed implementation units with `-j16`; log:
  `build_qt6/native-actions-print-build.log`. No new tests were needed. Combined
  linking/deployment completed in the combined runtime-dispatch build below.

## Native Save Request Parameters

- Save request construction accepts a native source URL and overwrite flag;
  the existing script argument decoder delegates to that implementation.
  Actor/view lookup, capabilities, queued continuation and permission handling
  remain unchanged.
- Vault maintenance invokes a native save entry point. Autosave, the save
  dialog and close-time saving no longer construct one-element object lists
  merely to identify their source buffer.
- Manual-save and export argument adapters still use objects and remain part
  of the ongoing removal. This batch does not change their completion behavior.
- Compiled both affected implementation units with `-j16`; log:
  `build_qt6/native-save-request-build.log`. No new tests were needed for the
  parameter separation. Full linking/deployment completed in the combined
  runtime-dispatch build below.

## Native Preferred Font Data

- Preferred-font reads and writes use native string arrays and the existing
  S-expression data reader/writer. The stored quoted-string-list format stays
  unchanged; no Scheme evaluation, object parser or object printer is needed
  by the Qt preferences interface.
- The existing script-facing font getter adapts the same native result, keeping
  a single preference interpretation. Empty/non-list/non-string-list data yields
  no preferred fonts rather than entering the UI as non-text values.
- Compiled both implementation units with `-j16`; log:
  `build_qt6/native-preferred-fonts-build.log`. Full linking/deployment completed
  in the combined runtime-dispatch build below.

## Native Actor And Keyboard Dispatch

- Four actor save/export link-location queries call the native implementation
  directly. Embedded-source PDF actions likewise invoke the native exporter.
- Generic keyboard parent traversal and focused commands use typed function
  pointers rather than procedure names, retaining dispatch order and ownership.
- Qt preferences read the default printing command directly and retain the
  existing empty-value fallback. Removed the unused boolean procedure helper.
- Built the complete application with `-j16` and deployed with the official
  helper, including the preceding four batches. Logs:
  `build_qt6/native-runtime-dispatch-combined-{build,deploy}.log`.
  Deployed ELF build ID: `396780199dd232f8b0fe1e3eb21c335e8e447127`.
  No new tests were needed for these direct-call substitutions.
- Remaining runtime calls include keyboard JSON expression dispatch, tree
  pattern predicates, transformation procedure callbacks and Guile's backtrace
  preference. Object parsing/printing, scripts, lifecycle, generated bindings
  and build dependencies also remain; the complete removal goal stays active.

## Native Keymap Expression Execution

- Audited the shipped JSON command names against generated native registrations.
  All 219 remaining command names have native entries. The native table and
  auto-close handlers retain their existing precedence; unknown commands now
  report an error instead of evaluating Scheme.
- Equality and current-focus queries are JSON expression descriptors handled
  by C++. Replaced `noop` prefixes with literal true, retaining compound-command
  identity for inverse lookup. Removed the stale `edit-focus-macro` and
  `edit-previous-macro` shortcuts: those referred to the macro windows removed
  in `4785ec601`, not the retained native macro-source command.
- Updated the keymap documentation, including the still-outstanding object
  printer used only to serialize inverse-lookup/command metadata. This batch
  removes the execution fallback, not every object dependency in the module.
- The affected implementation unit compiled with `-j16`. JSON parsing and the
  generated-registration audit passed. The complete `-j16` build and official
  helper deployment succeeded; logs:
  `build_qt6/native-keymap-combined-{build,deploy}.log`.

## Native Keymap Metadata Serialization

- Registration serializes its native literal/list values with the existing
  data writer rather than Guile's object printer. Real values use shortest
  round-trip decimal formatting and retain the `.0` distinction from integers.
- The registry normalizes registration and lookup keys using the native data
  reader/writer, expanding quote abbreviations to explicit quote forms. This
  keeps whitespace and string-escape spelling from breaking inverse lookup.
- String-based reverse lookup no longer parses and prints through Guile.
  Object-valued inverse queries still use the old object printer before native
  normalization; that adapter and user-shortcut objects remain to be migrated.
- Both affected implementation units compiled with `-j16`; log:
  `build_qt6/native-keymap-metadata-build.log`. Subsequently linked and deployed
  in the combined keyboard/lifecycle build below. No resource schema changed.

## Native Shortcut Query Parameters

- Added native string-based inverse queries and optional-text system-label
  queries. Menu shortcut lookup, geometry wrapper hints and alternate-tab
  guidance no longer parse command text into Scheme objects just to query keys.
- User macro shortcut matching consumes the normalized native command tree.
  It retains precedence over shipped bindings and matches explicit quoted
  macro names; the object adapter no longer owns this interpretation.
- Existing object-valued binding entry points delegate to the native queries.
  Their initial object printing and the remaining shortcut-dialog object API
  are still pending removal.
- Compiled all five affected implementation units with `-j16`; log:
  `build_qt6/native-keyboard-query-build.log`. No new tests were needed for
  this parameter transition. Subsequently linked and deployed in the combined
  keyboard/lifecycle build below.

## Native Zoom Number Parsing

- Custom zoom input and zoom preference notifications use Qt's numeric parser
  rather than constructing Scheme objects. The input contract is a complete
  decimal/scientific numeric value, with the existing optional percent suffix
  on custom zoom. Scheme reader-specific numeric syntax is no longer accepted.
- Finite-number checks, the 0.04 to 25.0 clamp, preference propagation and
  editor notifications stay in their existing native implementations.
- Both implementation units compiled with `-j16`; log:
  `build_qt6/native-zoom-parser-build.log`. No additional tests were needed.
  Subsequently linked and deployed in the combined keyboard/lifecycle build below.

## Native Keyboard Profile Selection

- Keymap groups and prefix rules pass native string arrays to layout matching,
  without temporary symbol objects or object lists. The script-facing adapter
  delegates individual names to the same native matcher.
- Preserved list-any matching, recursive `no-` negation, default look-and-feel
  resolution and `std` matching of non-Emacs layouts.
- Compiled all three affected implementation units with `-j16`; log:
  `build_qt6/native-keyboard-profiles-build.log`. No additional tests were
  needed. Subsequently linked and deployed in the combined keyboard/lifecycle
  build below.

## Native Document Load Options

- Startup document opening, ordinary open dialogs, link navigation and native
  document/window entry points use a typed options structure. Background,
  new-window and strict flags remain unchanged; script options only decode
  into that structure at their existing binding boundary.
- Removed temporary empty object lists from browse/reuse paths and keyword
  lists from startup window selection. Kept completion callbacks, GUI/actor
  scheduling and the separate window entry point's existing-window reuse.
- Startup `-x`/init scripts remain active consumers: the provable migration
  launcher and document profiler still use them, while the load audit uses a
  Scheme driver. They need native interop replacements before removal, not
  silent deletion of their workflows.
- Compiled the startup translation unit and three affected implementation
  units with `-j16`; logs: `build_qt6/native-startup-load-options-build.log`
  and `build_qt6/native-document-load-options-final-build.log`. The first
  compile attempt used the wrong make target for the startup translation unit;
  corrected to the executable target. Subsequently linked and deployed with
  the preceding four batches in the combined build below.

## Combined Keyboard/Lifecycle Deployment

- Built the full application with `-j16`. Boundary inspection found that
  `is_int` intentionally accepts integral real values, so using it in the new
  metadata writer incorrectly serialized `1.0` as `1`. Added an exact-integer
  value predicate and used it in metadata serialization; existing numeric
  coercion predicates keep their behavior.
- Rebuilt the corrected application and verified quote-abbreviated versus
  explicit-quote lookup, successful `1.0` lookup and rejection of an integer
  `1` match. Runtime printed `NATIVE_KEYBOARD_METADATA_OK` and exited normally.
  The probe must execute on a BufferActor after `native-keyboard-force #t`:
  initial harness attempts failed on ownership/unprepared cursor-environment
  checks and did not verify metadata. Those checks were not bypassed in code.
- Deployed using the official helper and removed the owned probe profile.
  Logs: `build_qt6/native-keyboard-lifecycle-combined-build.log`,
  `build_qt6/native-keyboard-exact-number-build.log`,
  `build_qt6/native-keyboard-metadata-runtime.log`, and
  `build_qt6/native-keyboard-lifecycle-combined-deploy.log`.
- Next automation boundary: AUDMAP currently exposes only already-loaded vault
  resources, not vault activation. The migration launchers need that operation
  before their Scheme bootstrap can disappear. Load auditing also needs its
  actor-owned typesetting/diagnostic operation, not just remote file opening.

## Native Save Timestamp Comparisons

- Save conflict checks compare native integer modification/save timestamps
  directly, preserving the existing exclusion of tmfs sources and readable
  file requirement. No object allocation or numeric type conversion remains
  in that check.
- Newer-file comparison directly excludes tmfs sources. Removed unreachable
  wrapped-file recursion from the last-save adapter: `url-wrap` always returns
  false. Its unused script binding itself remains for the binding cleanup.
- Both implementation units compiled with `-j16`; log:
  `build_qt6/native-save-timestamps-build.log`. No tests were needed for these
  equivalent simplifications. Included in the combined save/link deployment
  recorded below.

## Native Lifecycle Command Parameters

- Window closure accepts an optional native URL. Absence still triggers the
  embedded-editor close path, while explicit window closure skips it; native
  actor and window-close callbacks no longer construct object argument lists.
- Revert requests use a native source URL. Script entry points only decode
  arguments, retaining system-path interpretation for string arguments and
  the existing confirmation versus immediate-revert distinction.
- Link target pages call the existing native auxiliary-document API with
  their master URL directly. GUI scheduling, closure confirmation and actor
  identification remain at their existing ownership boundaries.
- Compiled all four affected units with `-j16`; log:
  `build_qt6/native-lifecycle-command-parameters-build.log`. No new tests were
  needed. Included in the combined save/link deployment recorded below.

## Native Manual Save And Export Requests

- Manual-save actor commands pass their native buffer URL. Both native and
  script entry points share the same manual-history notification and realtime
  save decision after owner scheduling.
- Export request execution accepts native destination text, relative-path
  status, format and portable/overwrite flags. Script destination/options are
  decoded once; neither export entry point constructs object lists to feed
  back into the save-request decoder.
- Preserved master-relative resolution, text filename sanitization, overwrite
  confirmation and portable LaTeX conversion settings.
- Both affected implementation units compiled with `-j16`; log:
  `build_qt6/native-manual-save-export-build.log`. No new tests were needed.
  Included in the combined save/link deployment recorded below.

## Retired Link Accessor Interfaces

- Removed the unused link type/attribute/vertices/source/target accessors and
  vertex id/url/script accessors, including all eight XML bindings. Repository
  consumers were audited; the similarly named embedded-image test variable is
  not an accessor call.
- Navigation serializes its target with the shared child conversion directly,
  instead of constructing and flattening an artificial link and extracting its
  target. Script arguments retain their attached tree identity. Link flattening
  itself remains used by link-query adapters and is not removed in this batch.
- Regenerated `athena_glue`; removed names are absent from source and generated
  code. Both implementation units compiled with `-j16`; log:
  `build_qt6/native-link-accessor-cleanup-build.log`. Full linking, deployment
  and runtime binding-removal verification passed in the combined deployment
  recorded below.

## Retired Lifecycle Query Adapters

- Removed `url-wrap`, `url-autosave`, `url-last-modified` and `buffer-last-save`
  bindings and object-returning implementations after confirming no remaining
  repository consumers. Native file timestamps, save requests and autosave
  behavior remain in use; only their obsolete script query adapters disappear.
- Regenerated `athena_glue`; retired native names are absent from source and
  generated interfaces. Compiled the lifecycle implementation with `-j16`;
  log: `build_qt6/native-lifecycle-adapter-cleanup-build.log`.
- Full linking, deployment and runtime binding-removal verification passed in
  the combined deployment recorded below.

## Combined Save/Link Deployment

- Built the full application through `f64c14d92` with `-j16` and deployed it
  using `tools/deploy-local-runtime.sh build_qt6`.
- An isolated runtime probe confirmed all twelve retired link/lifecycle
  bindings are absent and the retained `link-flatten` binding is present.
  It printed `NATIVE_SAVE_LINK_BINDINGS_OK` and exited normally. Removed the
  owned probe profile after completion; no broad regression suite was run.
- Logs: `build_qt6/native-save-link-combined-build.log`,
  `build_qt6/native-save-link-runtime.log`, and
  `build_qt6/native-save-link-deploy.log`.
- Link query and navigation implementations still round-trip their native
  records through object lists between collection, filtering and navigation.
  That active pipeline, runtime values/callbacks and Guile build/packaging
  remain unfinished parts of the full goal, not completed by this deployment.

## Native Link Navigation Records

- Link collection now exposes native source/link records. Event filtering and
  upward queries retain trees directly rather than flattening them into object
  lists and reconstructing them before navigation.
- Mouse/locus following builds native navigation records directly, preserving
  source exclusion, vertex positions, ordering and duplicate replacement.
  Navigation type selection and link-page scheduling consume those records
  without object-list round trips. Existing actor snapshot ownership remains.
- Link-page rendering consumes the reconstructed native records directly;
  its document construction and the explicit script-facing adapters still use
  objects and remain to be removed. This is not full link subsystem completion.
- Compiled both affected implementation units with `-j16`; log:
  `build_qt6/native-link-navigation-records-build.log`. No tests were added or
  run for this representation-preserving refactoring. Included in the combined
  native link deployment recorded below.

## Native Link Pages And Adapter Retirement

- Removed nineteen unused query/navigation/flattening XML bindings and their
  implementations after auditing source, resources and tools. The previous
  deployment's retained `link-flatten` is now unused following native-record
  migration and is removed too. Native mouse/locus following remains active.
- Link pages construct document, source, section, enumeration and locus trees
  directly. Removed object-list readers/writers and intermediate Scheme content
  construction. Display previews are copied; script targets retain their live
  argument trees. Async preview resolution and actor snapshots are unchanged.
- Regenerated `athena_glue`; removed native names are absent from source and
  generated interfaces. Compiled all three affected implementation units with
  `-j16`, log `build_qt6/native-link-adapter-retirement-build.log`.
- Runtime binding verification and full linking/deployment passed in the
  combined deployment below. No new tests were needed; conversion
  behavior was checked against `content_to_tree`, which constructs labels and
  children directly without special link normalization.
- Vertex execution still converts target/attributes to objects, and native
  document command dispatch still carries object arguments. Those remaining
  boundaries are not removed by this batch.

## Native Link Vertex Execution

- Vertex following reads native ID/URL/script trees and attribute pairs directly.
  Removed the unused `go-to-vertex` binding and the last flattening implementation
  file, `link_markup.cpp`. No target/attribute object-list serialization remains
  in the navigation path.
- ID navigation takes an optional native history path; the retained script
  entry only decodes its arguments. Native document actions pass an argument
  array to dispatch, preserving live tree arguments and existing actor-owned
  confirmation/callback handling. The command dispatcher still uses object
  values and is not claimed to be fully migrated.
- Regenerated interfaces and reconfigured CMake after source removal. Compiled
  the affected unit with `-j16`; logs: `native-link-vertex-generation.log` and
  `native-link-vertex-build.log` under `build_qt6`. Retired symbols are absent
  from source and generated interfaces. No tests were added or run.
- Included in the combined native link deployment recorded below.

## Combined Native Link Deployment

- Full `ATHENA.bin` build with `-j16` passed through `0bc16e632`; deployed with
  `tools/deploy-local-runtime.sh build_qt6`. Source removal triggered CMake
  reconfiguration and dependency rebuilding; the original build completed
  without being restarted.
- Isolated runtime verification confirmed all twenty retired bindings absent
  and the five retained link execution entries present (`link-follow-ids`,
  `locus-link-follow`, `go-to-id`, `go-to-url`, `execute-script`). Printed
  `NATIVE_LINK_BINDINGS_OK` and exited normally. This is interface-registration
  verification, not a claim of interactive navigation coverage.
- Removed the owned temporary profile. Logs:
  `build_qt6/native-link-combined-build.log`,
  `build_qt6/native-link-combined-runtime.log`, and
  `build_qt6/native-link-combined-deploy.log`.

## Remaining Reader And Callback Consumers

- `string_to_object` has two business callers: atomic selectors in
  `src/Typeset/Env/utility_markup.cpp` and non-string preference retrieval in
  `src/Edit/Interface/application_preferences.cpp`.
- The preference catalog has exactly one `scheme-object` entry:
  `editor:verbatim:tabstop`, default `4`. Its active program editor consumer
  already reads the native string preference as an integer. Replace the
  obsolete Scheme-valued preference contract at the catalog/getter boundary,
  rather than introducing another general expression reader.
- `tree_pattern.cpp` still invokes named pattern predicates with `call`;
  `native_interfaces.cpp` invokes the global transformation procedure callback.
  Native dispatch and native interop transformation ownership must replace
  those contracts, not silently remove supported operations.
- The debug-backtrace preference calls Guile's actual debug-enable/disable;
  this is not equivalent to native diagnostic flags. Runtime removal must
  remove or explicitly replace that preference with its real semantics.

## Native Preference Value Contract

- Removed the unused object-valued `get-preference` adapter. Retained
  `set-preference` now binds directly to its native string overload; all
  repository consumers already pass strings. Preference access no longer
  invokes the Scheme reader or printer.
- Removed unused dynamic default/callback registration bindings and the
  default-string classification map/query. JSON catalog declarations remain
  the source of truth for defaults and callbacks.
- The sole former `scheme-object` preference, `editor:verbatim:tabstop`, now
  uses the native string storage contract, as other numeric preferences do.
  Its default remains `4`; `native_program_edit.cpp` already interprets it as
  an integer. No new expression parser or value compatibility layer was added.
- Regenerated interfaces and compiled both implementation units with `-j16`;
  log `build_qt6/native-preference-value-cleanup-build.log`. Removed names are
  absent from source/resources/tools/tests; generated setters accept strings.
  Full linking, runtime binding verification and deployment passed in the
  combined selector/preference deployment below. No permanent tests were added.

## Native Style Selector Reading

- Atomic style selectors now use the existing native structural-data reader,
  not Guile `scm_read`. The selector conversion preserves keyword values,
  quote abbreviations, integer versus real values and string/symbol distinction.
  The existing leading-parenthesis literal rule and structured `pat-*` handling
  remain unchanged. No expression evaluation or replacement interpreter was
  introduced.
- Audited shipped selector calls in the APS/AIP styles: author-name,
  author-affiliation, author-homepage with pat-any, and author-note/document
  with pat-any. The standard pattern package defines the extern entry.
- Removed `string_to_object` after confirming no remaining repository callers.
  Preference cleanup and this change remove its last two business consumers.
- Compiled the selector and object implementation units with `-j16`; logs:
  `build_qt6/native-style-selector-build.log` and
  `build_qt6/native-reader-removal-build.log`. No permanent tests were added.
  Combined runtime verification exercised label/index/pat-any selection and
  keyword/quote decoding before deployment, as recorded below.
  The native data reader is not claimed to support arbitrary
  Guile reader extensions, which are not the structural selector contract.

## Combined Selector/Preference Deployment

- Full `-j16` build passed. The targeted runtime probe exposed an existing
  matcher type mismatch: `is_int(object)` accepts integral real values while
  `as_int(object)` requires exact integers. A selector `1.0` threw instead of
  simply not matching an integer index. Changed the index predicate to
  `is_exact_integer`; did not coerce real values in the reader.
- Rebuilt and reran the full probe successfully: labels, exact integer indices,
  rejection of real indices, pat-any, keyword, quote binding, string versus
  label distinction, nested author-note selection and union all passed.
  Confirmed four retired preference bindings absent and string preference
  writes/readback working. Printed `NATIVE_SELECTOR_PREFERENCE_OK`, exit zero.
- Deployed with the official helper, then removed the owned probe script and
  profile. No permanent regression test or broad suite was added.
- Logs under `build_qt6`: `native-selector-preference-build.log`,
  `native-selector-preference-runtime.log` (initial failure),
  `native-selector-other-boundaries-runtime.log` (isolating the real-index case),
  `native-selector-exact-index-build.log`,
  `native-selector-exact-index-runtime.log` (complete passing check), and
  `native-selector-preference-deploy.log`.

## Native Predicate Dispatch And Retired Guile Preference

- Structural pattern predicates call the existing generated native dispatcher
  directly. Unknown names raise `Unknown native pattern predicate`; there is
  no Scheme lookup/evaluation fallback or second procedure registry. Registered
  native predicates retain argument conversion, return values and exceptions.
  Arbitrary Scheme predicates are no longer an extension mechanism.
- Removed the Guile-only `debug scheme backtraces` catalog entry, callback map,
  notification branch and native Preferences toggle/empty General section.
  Native error reporting, performance controls and crash backtraces are
  unchanged; this was specifically Guile debug-enable/disable, not a native
  diagnostic setting.
- All four affected implementation units compiled with `-j16`. Logs:
  `build_qt6/native-debug-preference-removal-build.log` and
  `build_qt6/native-pattern-predicate-build.log`. No new tests were added.
  Full build/deployment and targeted native-predicate dispatch/unknown-name
  rejection verification remain pending with the next batch.

## Retired Named Scheme Call Fallback

- Name-based `call` now resolves only generated native entries and throws
  `Unknown native procedure` for missing names. Removed its `eval_scheme` plus
  `call_scheme` fallback. Production business callers already use typed/native
  dispatch; remaining named-call uses are in legacy C++ test code.
- Deleted unused `hash(object)` (which called Scheme hash) and `eval(object)`
  (which invoked primitive-eval), including their declarations. No replacement
  hash or expression-evaluation compatibility layer was introduced.
- Explicit startup string evaluation and actual procedure-object callbacks
  still exist and remain part of the full goal. Legacy tests that call removed
  Scheme APIs need migration/removal, not a restored production fallback.
- Compiled `object.cpp` with `-j16`; log
  `build_qt6/native-named-call-cleanup-build.log`. No tests were added or run.
  Combined full build/deployment remains pending with native predicate dispatch
  and Guile debug-preference removal.

## Known Outstanding Failure

- A C++ exception escaping a Scheme VM call can leave the dynamic-wind stack
  inconsistent. See the concrete core evidence in `native-application-ui.md`.

Do not describe this as fixed by removal of `progs`. It remains an explicit
part of the complete runtime-removal work.
