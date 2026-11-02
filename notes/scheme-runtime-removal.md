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
  and body trees. Existing Scheme-facing DataArt declarations are still pending
  removal; they no longer have production consumers.
- Added typed native initialization-text and insertion-arity entry points so
  these UI calls do not construct temporary Scheme command arguments.
- Built with `-j16` and deployed with the runtime helper. Ran the actual headless
  ToC maintenance worker on a temporary XML v3 document: it invoked the native
  actor action, saved successfully, wrote `ok`, and exited zero. Removed its
  document/profile directory. Logs are
  `build_qt6/native-buffer-ui-{build,fixture,runtime,deploy}.log`.
  No interactive pane sweep or unrelated test suite was run.

## Known Outstanding Failure

- A C++ exception escaping a Scheme VM call can leave the dynamic-wind stack
  inconsistent. See the concrete core evidence in `native-application-ui.md`.

Do not describe this as fixed by removal of `progs`. It remains an explicit
part of the complete runtime-removal work.
