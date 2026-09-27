# Native Node Model Migration

For the current consolidated architecture, activation gates and handoff status,
read `notes/node-model-handoff.md`. The dated sections below are chronological:
later batches can complete work recorded as outstanding in earlier sections.

## Activation Contract

This is a staged migration, not an enabled document format change. Source
identities and properties must not be automatically assigned by the current
editor until editing, persistence, references and offline migration agree.
Production vaults must not be migrated as part of development or normal builds.

The existing tree container owns optional metadata on both atoms and compound
nodes. Properties are outside ordinary child indexing. Snapshot copies preserve
identity; duplication as new source objects regenerates identities and remaps
typed internal references. Neither operation is a clipboard move credential.

## Foundation Interfaces

- `src/Kernel/Types/node_metadata.*`: typed metadata, UUID validation, owned
  deep copying, full equality/hash, identity-free content comparison/projection,
  and explicit duplication. Content projection preserves properties and target
  references; it is not automatically a model-input or embedding fingerprint.
- `modification.*` and the observer/history pipeline: metadata-only edits and
  stored split/join headers. Replaying an already prepared edit reuses its IDs.
  Normalization retains independently annotated inner nodes instead of silently
  deleting or flattening them.
- `athena_document_xml.*`: explicit XML v2 codec with typed properties and
  atomic metadata. Default v1 entry points must reject unsupported metadata.
- `interop_document_codec.*`: explicit document-model v3 codecs. These do not
  advertise v3, change authentication, or activate new mutation operations.
- `enunciation_model.*` and `ATHENA/misc/enunciations.json`: declarative kind,
  legacy variant and presentation contracts plus detached conversion. Search
  classification, filter menus, block statistics, legacy artifact classification
  and typesetter source colors consume this registry. Canonical-node native
  presentation uses the same declaration; remaining consumers and source
  creation/property UI still require integration.
- `document_node_model.*`: explicit detached identity planning from source roles
  and DRD contracts, caller-supplied deterministic allocation, duplicate checks
  and property schema validation. An opt-in incremental source identity index
  also prepares observer-aware metadata edits at owner transaction boundaries.
  It is not yet enabled by normal source loading.
  Attribution is a list of structured names; year is optional unparsed text.

The APIs are owner-local. Native trees and mutable rich property values must not
be shared between actors; produce a deep snapshot or use an explicit wire codec.

## Editing And Clipboard Integration

- Atomic paragraph promotion to CONCAT now transfers its metadata in one
  explicit structural modification. Enter/paragraph join use this operation so
  identity belongs to the split/join unit; undo restores the original header.
  Generic wrapping of an independently identified compound does not promote it.
- Complete atomic/annotated selections preserve source metadata. Partial text
  selections do not claim the containing object's UUID. Concat and WITH
  decomposition/recomposition retain annotated boundaries and empty objects.
- Clipboard snapshots use XML v2 when annotated, preserving v1 output for
  unannotated selections. This is not normal-document XML v2 activation.
- Source duplication renews all IDs, remaps typed references, native HLINK
  targets and the staged TRANSCLUDE(TUPLE(uuid,...)) list. Ordinary text,
  external links and legacy four-argument anchor transclusions are not rewritten.
  Reserved `athena:artifact-bindings` (role -> artifact UUID strings) are removed
  from new objects; the artifact producer still needs integration with this key.
- Paste uses that new-object operation once per selection. Cut currently falls
  back to copy semantics: verified one-use move credentials and cross-actor undo
  are NOT implemented, so identity-preserving moves must not yet be enabled.
- Scheme `tree-rebuild` preserves parent metadata with normal owner-local child
  sharing. `tm-replace` uses it for native source inputs instead of reconstructing
  stree. `tree-copy` remains an independent snapshot; `tree-duplicate-source`
  explicitly creates new identities. Patch Scheme conversion is now in-memory
  native-tree transport, not a persistent or printable S-expression format.

These changes cover specific editing routes, not every correction, formatting,
insertion, serialization or Scheme source-modification helper. In particular,
partial range reconstruction, source roles during arbitrary formatting and
whole-document insertion still require the integration audit below.

## Property Mutation Boundary

`document_node_model::prepare_property_edit` prepares one `MOD_SET_METADATA`
operation for an entire typed property delta and optional assign-if-absent ID.
It validates the resulting schema and UUID occurrences across the supplied
document (including rich properties). No operation is returned on failure;
successful no-ops create no history. The ancestor spine and metadata are copied,
not the complete body. Apply immediately on the owner through observers/history.
Undo/redo replays the stored UUID, never another allocation.
This explicit-edit preflight scans identity occurrences; it is not a per-keypress
or automatic paragraph-allocation hot-path API.

Ordinary edits cannot set/erase `id`, `uuid`, or `athena:artifact-bindings`.
The latter is a reserved role-to-artifact-UUID dictionary, validated independently
of enunciation kind. Other valid namespaced properties survive unchanged.

Generated Scheme interfaces provide:

- `tree-node-properties`: independent typed property snapshot.
- `tree-update-node-properties! tree replacements removals`: one atomic delta.
- `tree-ensure-node-id! tree`: assign only if absent; no arbitrary ID setter.

The two mutation functions return `(ok value)` or `(error diagnostic-string)`.
The value is the native target tree for a property edit, or its UUID for ensure.
Replacements are a list of `(string-key (type payload))` entries, not dotted
pairs. Removals are a list of string keys. Duplicate keys, conflicting set/remove,
cycles, malformed lists and resource overflows are rejected before mutation.

Property types are `string`, `boolean`, `integer` (exact signed 64-bit), `real`
(finite inexact), `reference` (canonical UUID), `rich-text` (native tree, never
stree), `list` (typed values) and `dictionary` (the same keyed entry format).
Rich trees remain inert data, and are independently copied on both write and
read; they do not acquire execution permissions.

Attached mutations require the owning BufferActor editor and a writable buffer;
detached calls operate only within the supplied detached subtree. Callers must
not treat detached validation as a vault-wide uniqueness guarantee. No consumer
is automatically assigning IDs yet, and these APIs do not activate normal XML
v2 saves, advertise AUDMAP v3, or implement cross-document moves.

### Property Boundary Verification (2026-09-27)

- Normal `ATHENA.bin -j20` build passed, without deployment.
- `document_node_model_test`: 13 passed; `modification_metadata_test`: 27 passed.
  These include schema failure atomicity, reserved bindings, rich-text identity
  conflicts, assign-once replay and one-step observer/archiver undo and redo.
- The generated Scheme bindings passed `node-model-bridge-test.scm` in an
  isolated headless profile: all eight types, exact int64 extremes, cycle/type
  rejection, copy isolation, ID assignment and self-reference duplication.
  No full suite or production vault was used. The pre-existing startup
  `lazy-keyboard-provide` diagnostic remains unrelated and unresolved.
- The role suite exposed a DRD assumption: legacy child descriptors initialize
  `block` to zero, also named `BLOCK_REQUIRE_BLOCK`, even for CONCAT. The source
  planner no longer interprets this default as a declared body slot. Nested
  DOCUMENTs and explicit body contracts still create identities; ordinary
  inline text does not. The code-field fixture also now sets TYPE_CODE after
  `accessible()`, which otherwise resets the descriptor to TYPE_REGULAR.

### Focused Verification (2026-09-27)

- Normal `ATHENA.bin -j20` build passed; no deployment or vault conversion.
- `document_node_copy_test`: 8 passed; `modification_metadata_test`: 26 passed;
  `node_metadata_test`: 10 passed. These are the only native suites executed for
  this integration batch, not the whole test suite.
- `tests/scheme/node-model-bridge-test.scm` passed in the newly built headless
  binary with an isolated temporary profile, exercising generated native
  bindings, native/stree replacement and modification/author/birth history.
  Startup also reported `lazy-keyboard-provide` unbound twice; that separate
  startup diagnostic has not been investigated or fixed by this batch.

## Runtime Registry Integration (2026-09-27)

- Global search and both inserters derive their filter choices from the native
  registry. Search matches legacy aliases and canonical kind/variant properties,
  retaining physical body paths. UI choice lists no longer define source-tree
  recognition. Metadata titles still need a separate property-aware result path.
- Block word counts use declared body layouts, correcting the previous ordinary
  body child-1 and proof-of child-2 assumptions. Canonical bodies remain child 0.
- All three macro typesetting routes use registry-backed source color kinds;
  render helpers retain their separate background-injection behavior. Canonical
  presentation and numbering were added in the following integration batch.
- Legacy artifact extraction uses declared alias policies, preserving exactly
  the old 21 recognized aliases and their extraction base tags. This does not
  allocate canonical artifact identities or activate source-owned bindings.
- Normal ATHENA.bin build passed. The enunciation registry suite reported
  14 passes; three selected vault-search cases plus initialization/cleanup
  reported five passes. No full suite, deployment or production migration ran.

## Native Enunciation Presentation (2026-09-27)

- Canonical enunciations now use one native presentation plan in environment
  evaluation, cursor environment traversal, inline, lazy and incremental bridge
  typesetting. The standard DRD exposes the sole body child at index zero.
  The plan binds that original child as a macro argument; it does not replace
  the source node or copy/reparent its body to generate a title.
- The registry supplies label, style, counter group, variant label and exceptional
  number formatting. Existing style macros retain presentation control. Names,
  attribution and unparsed year text form a heading without changing body text.
  Complete historical titles, proof variants and body-only quotes are retained;
  unknown kinds use a generic heading and preserve their body.
- Structured title display is a non-executing projection: supported native math
  and literal formatting remain structured, historical label references become
  read-only links, and arbitrary macros/assignments/external calls display as
  literal source. This does not modify the stored property. Typed proof targets
  link the heading to the UUID tmfs target; resolution remains a separate gate.
- Native enunciation layout helpers now also participate in environment
  evaluation, matching their existing typesetting interpretation. This repairs
  undefined-helper errors and preserves the body font environment at the cursor.
- The focused presentation suite passed all six QtTest cases (four feature
  cases plus setup/cleanup), including incremental title replacement and exact
  source cursor mapping. The registry suite passed 14 cases, and the normal
  `cmake --build build_qt6 --target ATHENA.bin -j20` build passed. No full suite,
  deployment, automatic identity assignment or production migration ran.

## Native Property Editor (2026-09-27)

- The shared Focus/context menu opens a native Qt node inspector. UUIDs and
  generic/extension properties are read-only. Canonical enunciations expose
  kind, variant, structured name, ordered structured attribution, year text,
  numbering and an explicit target UUID. Unknown properties and reserved
  artifact bindings survive confirmation unchanged.
- The UI receives only a body-free XML v2 header and an opaque source-node
  lease. It reuses the existing strict observer registry, not an AUDMAP
  connection, resolver or request state machine. Live source access and final
  schema checks run on the originating BufferActor. No body tree crosses to Qt.
- Submission compares the captured tag/metadata with the still-live target,
  accepting independent body edits but rejecting replacement, deletion and
  concurrent property changes. One menu edit transaction contains one metadata
  modification; a no-op creates no transaction. UUID changes are rejected.
- Structured field editing reuses a private native input buffer in source mode
  instead of executing arbitrary property macros. Cancel discards the draft;
  the embedded buffer is explicitly destroyed on close. Simple atomic names
  also retain any existing metadata when their text changes.
- Normal unannotated source remains an inspector-only view. This UI does not
  assign identities automatically, change normal saves, or migrate old theorem
  tags merely because a dialog is opened. Format and source-creation gates
  remain in force.
- Focused checks passed: `document_node_model_test` (14 cases) and
  `node_properties_dialog_test` (6 cases). The latter exercised lossless no-op
  confirmation, reserved/structured property preservation, failed-submission
  feedback and read-only inspection; its offscreen screenshot was inspected.
  The normal `ATHENA.bin -j20` build passed. The binary exposed the generated
  binding and rejected detached input
  in the isolated Scheme bridge check. This is not yet a full live-actor GUI
  acceptance test of structured editing, close races and undo; that remains
  part of the final integration gates. No full suite or production vault ran.

## Native UUID Location Service (2026-09-27)

- `node_location.*` inventories persistent IDs in source children and typed
  rich properties. Addresses distinguish child, property, list, dictionary and
  rich-text steps without changing source child indices. Collection never
  allocates an ID; consuming a location checks the expected UUID again.
- A service has one sleeping worker and merges concurrent requests into one
  captured inventory. Request/poll return native data without filesystem I/O or
  actor waits. Ordered selections deduplicate first occurrences, preserve
  missing entries, reject ancestor/descendant overlap and detect expansion
  ancestry cycles. Duplicate identities are conflicts, not arbitrary choices.
- The disposable in-memory cache stores validated file revisions and identity
  censuses. Each scan currently inventories the vault and reuses unchanged
  documents, reparsing only changed files. Clearing it loses no identity data.
  A future filesystem-change invalidation layer is still needed to avoid whole
  inventory validation on every independent request batch; do not describe the
  present implementation as an O(1) cached-hit fast path or a persistent index.
- Confined reads exclude `.athena`, `.backup` and `.git`, including aliases
  pointing into them. Symlink escapes and changed roots fail closed. Canonical
  path aliases do not create duplicate documents. Read failures remain distinct
  from proven absence, including failures that prevent proving uniqueness.
  XML duplicate-ID exceptions carry the offending UUID rather than requiring
  diagnostic-string matching. XML v1 contributes no invented identities;
  pre-XML input requires the separate upgrade boundary.
- `vault_node_location.*` shares services by vault incarnation and captures live
  inventories on the owning BufferActors. Only standard strings, addresses and
  IDs cross the boundary; no full source-tree copy is made by the census. Live
  files override disk even after unsaved deletion or an actor read failure.
  Actor command watermarks describe captures, not fictitious storage revisions.
- Nonblocking actor submission, cancellable timed waits and source membership
  checks avoid a saturated actor mailbox trapping the locator worker or vault
  close. Online consumption refuses newly actor-owned disk candidates, verifies
  source names and UUIDs on the owner, and transfers an XML v2 fragment. These
  are direct native calls, not AUDMAP sessions, requests or authentication.
- `node_location_test` passed 11 QtTest cases (nine feature cases and
  setup/cleanup), covering addresses, stale reads, cache rebuild, external
  rename, excluded trees, malformed XML, duplicate IDs, ordered lists, cycles,
  overlap, live deletion/read failure, merged requests, cancellation and root
  replacement. Live precedence is exercised with a controlled provider; actual
  actor lifecycle integration still requires runtime acceptance.
- The normal `cmake --build build_qt6 --target ATHENA.bin -j20` build passed.
  No full suite, deployment or production vault migration was performed.
- This batch does not switch existing tmfs handlers or render caches, delete
  map.sqlite, add a persistent SQLite location cache, or enable XML v2 normal
  saves. Those changes require the remaining reference and migration gates.

## Watched Native Reference Presentation (2026-09-27)

- Canonical single-tuple transclusions consume immutable async location/content
  snapshots. Both incremental and inline typesetting preserve ordered missing
  items, render locating/error states, and track ancestry per selected object.
  Actor-local presentation copies remove source identities and artifact bindings;
  native source trees never cross the worker boundary.
- The Qt reference facade subscribes owner views using nonblocking actor
  continuations. Source edits, buffer membership changes and filesystem watches
  invalidate snapshots. New watches require another scan before publication;
  watcher failures remain visible and transient read failures retry. Stable
  facade hits avoid filesystem work, although cold locator batches still census
  the vault and invalidation is currently coarse.
- Source links use tmfs://transclude/<uuid> and asynchronously navigate through
  the existing buffer/tab loader, rechecking the actual source UUID before cursor
  placement. Existing bare wikilinks can have legacy map semantics, so their
  default dispatcher is intentionally not switched by URL-shape guessing.
- Normal ATHENA.bin -j20 build passed. The locator/presentation suite passed
  14 cases, and the isolated Scheme bridge check reported
  ATHENA-NODE-BRIDGE-PASS for generated runtime bindings. Actual Qt watcher,
  actor lifecycle and interactive navigation acceptance remain outstanding.
- Native hover source styling/refresh and an offline export preparation barrier
  are not integrated yet. There is no production migration, deployment, default
  XML v2 save activation or default UUID wikilink switch in this batch.

## Native UUID Hover Integration (2026-09-27)

- Native transclusion-source URLs now use the same async reference service for
  hover. The source owner serializes the selected fragment, style, initial
  environment and relevant hidden preamble in one capture. Saved reads validate
  the source revision around decoding. No full source tree crosses actors.
- Preview envelopes keep source styling and relative-link context; presentation
  copies strip persistent identities. Whole-document targets display their body,
  and a preamble already contained by the selected root is not duplicated.
- Reference completion refreshes native overlay layers in place, retaining the
  ancestor stack and clamping existing scroll positions. Nested hover carries
  explicit target ancestry. Legacy map-based hover remains unchanged until the
  unified vault/model activation switches bare wikilink semantics.
- Live interactive overlay acceptance is still required, as is offline export
  preparation. This implementation does not enable default saves, migrate any
  vault or deploy the binary.
- The normal ATHENA.bin -j20 build passed. The focused location/presentation
  suite passed 16 cases, including source context preservation, root body
  extraction, preamble deduplication, identity-free previews and explicit
  pending/missing bodies. No full suite or production data was used.

## Frozen Export Reference Preparation (2026-09-27)

- `node_reference_export.*` inspects an owner-local source once, then prepares
  static native transclusion dependencies on the existing locator worker. No
  per-export worker or actor wait is introduced. Typed rich properties are
  inspected, dependency ancestry terminates cycles, and terminal failure states
  remain explicit. Selection/content budgets and cancellation bound the work;
  inconsistent revisions of the same saved source reject preparation.
- Frozen export scopes bypass the interactive cache and event loop. References
  generated dynamically but absent from preparation are recorded as incomplete.
  PDF/print and image-snippet layout check readiness before opening a renderer
  or writing output; they must not print a locating placeholder. Nested scopes
  propagate incomplete status, and bridge reuse compares snapshot identity as
  well as revision so frozen and interactive generations cannot alias.
- The generated `node-reference-with-export` binding preserves a rooted Scheme
  action behind an opaque handle, asynchronously prepares references, and resumes
  on its original actor/view. It rejects changed source epochs, vaults, cursor or
  selection state, and retries nonblocking mailbox submission. Closing the owner
  cancels preparation. File/PDF, print, preview and selection-image/clipboard UI
  wrappers keep their dependent actions inside that completion boundary.
- Cross-actor `buffer_export` transfers immutable prepared wire snapshots for
  temporary export buffers. The locator lifetime now permits the last owner to
  be released inside a completion: worker state survives until stopped work
  unwinds rather than attempting to join its own thread.
- This is not complete export activation. Headless/batch callers still need an
  explicit preparation coordinator; dynamically generated selections currently
  fail closed rather than automatically retrying layout. Interactive PDF/preview,
  temporary-buffer paths and cancellation still need end-to-end acceptance.
  No normal-save format change, deployment or production vault migration ran.
- The normal ATHENA.bin -j20 build passed. The focused location/reference suite
  passed 21 cases, including dependency closure with cycles/missing targets,
  nested frozen scopes, cancellation, budget failures, mixed saved revisions and
  last-owner release inside a locator completion. These do not substitute for
  full GUI export acceptance.
- The isolated Scheme bridge run exited successfully with
  ATHENA-NODE-BRIDGE-PASS, exercising the generated export binding's owner check
  and loading the changed Scheme modules. No full test suite was run.

## Integration Gates Still Required

1. Assign source IDs through content roles, with complete handling of nested
   bodies, headings, paragraphs, insertion, deletion, formatting, normalization
   and source-modifying Scheme paths. Preserve semantics across stree boundaries.
2. Implement one-use cut/move credentials and coordinated cross-document undo.
   New-object copying already clears artifact bindings and rewrites internal
   native tmfs references; the move path must respect those separate policies.
3. Finish remaining live enunciation consumers and source creation. Native
   rendering/numbering and property UI are implemented; proof targets can be
   entered explicitly, but UUID resolution and target selection remain.
4. Finish derived-buffer/selection-specific dynamic export acceptance and
   actual actor lifecycle/hover/export GUI acceptance for the integrated async
   reference paths. Switch
   default wikilinks together with migrated vault semantics. Persist any index
   only as disposable data; retire hint-dependent map identity without losing
   rename recovery journals.
5. Persist artifact identity bindings on source nodes. Separate storage
   revisions, content revisions and actual model-input fingerprints before
   changing index reuse or invalidation.
6. Integrate document-model v3 operations, SDK, REPL and examples together. Keep
   persistent UUIDs separate from connection-scoped node leases and handles.
7. Switch every save, recovery, clipboard and tree-bearing persistence boundary
   explicitly. Default readers/writers must not become lossy compatibility paths.
8. Extend offline vault upgrade with deterministic identity mapping, reference
   rewriting, unresolved diagnostics, artifact preservation and transactional
   publish/recovery. Validate using isolated vault copies, never production Notes.
9. Complete focused acceptance, including deleting the entire location cache,
   external renames, duplicate IDs, unsaved live sources and injected failures.
   Only then enable the model and deploy after user acceptance.

## Headless Export Coordination (2026-09-27)

- The central global/headless buffer export captures selections and a full
  XML-v2/SHA256 source fingerprint on the owning actor, then waits on the shared
  locator's completion condition outside all BufferActors. It does not require
  a Qt application or pump an event loop. Sources without native references do
  not require an active vault. Preparation errors return export failure.
- The immutable prepared snapshot carries its source actor, view, URL and
  fingerprint. The original owner checks these before output; derived export
  buffers inherit reference snapshots without claiming the original identity.
  These are cold export checks, not per-keypress whole-tree serialization.
- Website PDF export now explicitly prepares before DataArt/print dispatch.
  Existing synchronous Qt-to-actor helpers propagate a frozen scope only when
  one is present, so their ordinary UI behavior remains unchanged.
- The normal ATHENA.bin -j20 build passed. All 22 focused location/reference
  cases passed. An isolated headless runtime produced three PDFs without a Qt
  event loop: plain/no-vault output, a disk UUID transclusion with its actual
  body, and an explicit missing target after an unsaved live deletion. Extracted
  PDF text confirmed the latter two outcomes, not merely successful dispatch.
- Website/DataArt end-to-end acceptance and dynamically generated dependency
  discovery remain outstanding. No full suite, deployment, normal XML-v2 save
  activation or production migration was performed.

## Dynamic Export Dependency Discovery (2026-09-27)

- Owner-side reference probing uses the same paper layout as printing, but
  creates no renderer, file, clipboard content or printer command. Derived
  label, auxiliary and attachment maps are independently copied for the probe
  and restored afterward, including on layout failure. The probe does not
  masquerade as a source edit or invalidate its captured storage fingerprint.
- Headless and interactive coordinators repeat preparation and layout discovery
  until no new selections remain. They retain ordered target/ancestry keys,
  propagate missing selections through nested scopes and enforce round and
  selection budgets. Headless completion has one five-minute preparation
  deadline across the rounds. Actual user export actions run only after the
  dependency closure, never as speculative actions retried after side effects.
- The normal ATHENA.bin -j20 build and all 22 focused reference cases passed.
  Isolated real PDF checks passed for a label-bearing plain source, static UUID
  content, a secure expansion generating a transclusion absent from the source
  tree, and a second dynamic expansion inside that transcluded target. Extracted
  PDF text contained the final nested payload. Unsaved target deletion produced
  an explicit missing item through the same dynamic chain.
- This probes the original source's paper layout. References introduced only
  by subsequent DataArt/selection-specific transformations still fail closed at
  the actual renderer if absent from the frozen snapshot; those derived paths
  and interactive lifecycle behavior still need end-to-end acceptance. No full
  suite, deployment, production migration or default model activation ran.

## Incremental Source Identity Transactions (2026-09-27)

- An owner-local UUID/path index now prepares native metadata modifications
  for missing source-role identities. Text edits classify only their affected
  branch and ancestors; structural batches merge their affected scopes before
  reindexing. The index retains no source trees and performs no content copies.
  Rich-property identities are indexed at their containing source node, not
  at invented ordinary child paths. The shared enunciation registry supplies
  its body roles; there is no duplicated list of theorem/proof kinds.
- Opt-in editor hooks observe modifications and finalize identities before
  history confirmation or serialization. Generated metadata participates in
  the same undo step. Rollback rebuilds the disposable index from the actual
  restored source, even if an earlier save preflight already applied its plan.
  Failed initialization remains pending and fails closed.
- Normal ATHENA.bin -j20 compilation passed. Focused model tests passed 20
  cases; metadata/history tests passed 28. Coverage includes a 10,000-paragraph
  document edited within a four-node traversal budget, inserted nested bodies,
  rich-property conflicts, stale plans, applied-plan rollback, failed rebuild,
  and observer-driven insertion undo/redo with no additional UUID allocations.
- This remains staged: ordinary buffers do not instantiate the opt-in index.
  Loading/activation, full source-edit audit and actual enabled editor lifecycle
  acceptance are not complete. No deployment, full suite or Notes migration ran.

## DataArt Source Preservation (2026-09-27)

- Cover insertion now rebuilds only native body/doc-data containers on a
  detached snapshot, retaining their headers and untouched child metadata.
  The export tuple deep-copies the native envelope and prepared body instead
  of reconstructing either through stree. No source IDs are generated for the
  synthetic cover. Read-only cover detection and the existing content seed
  still use content-only stree projections; neither reconstructs source data.
- The normal ATHENA.bin -j20 build passed. An isolated headless invocation of
  `tests/scheme/node-data-art-test.scm` passed checks for root/title/atomic
  paragraph metadata, doc-data and fallback insertion, idempotence, complete
  envelope preservation and snapshot isolation. This does not substitute for
  the outstanding full DataArt PDF/UI acceptance or enable new-format saves.

## Native Formatting And Preamble Source Edits (2026-09-27)

- Formatting simplification, adjacent WITH merging and resetting the last
  format pair retain independently annotated WITH/DOCUMENT nodes. This also
  applies to properties-only nodes, not just assigned UUIDs. Selection-based
  multi-format reconstruction carries existing wrapper metadata; anonymous
  wrappers retain their established compact behavior.
- Preamble creation and show/hide preserve body-root and preamble headers.
  Existing child trees retain their own identities. Explicitly annotated
  temporary content groups become visible DOCUMENT groups on hide, retaining
  their identities instead of being silently flattened away.
- The normal ATHENA.bin -j20 build passed. The focused
  `tests/scheme/node-source-edit-test.scm` passed on a real BufferActor-owned
  editor in an isolated headless profile, including preamble undo/redo,
  complete show/hide round trips, atomic paragraphs, annotated temporary
  groups, selection formatting, properties-only nodes and anonymous merging.
- The general Scheme tree-set-diff optimizer still needs a metadata-aware
  audit: its same-children/assign-label shortcut does not apply target headers.
  Its text prefix/suffix helpers also mix character counts and UTF-8 byte
  offsets. This batch does not enable source-role allocation or complete the
  overall source-modification audit. No deployment or production migration ran.

## Native Exact Tree Diff (2026-09-27)

- `tree-set-diff` now uses a generated binding to native owner-local code.
  Atomic changes compare complete ICU graphemes and edit UTF-8 byte offsets;
  compound changes apply the target header even when children are unchanged.
  Actual-descendant wrapping/unwrapping keeps observers and records explicit
  headers so undo restores both parent and child identities.
- The contract is exact replacement, including target metadata. Content-only
  callers must explicitly preserve the original header; the helper must not
  guess identities onto an anonymous target. Native formatting, embedded source
  text and cardlink conversion now do this explicitly. Materials field updates
  no longer reconstruct their entire reference-list node/body through stree.
  This does not complete the audit of every tree-set! caller.
- The normal ATHENA.bin -j20 build passed. The isolated owner-editor fixture
  `tests/scheme/node-tree-diff-test.scm` exited zero with
  ATHENA-NODE-TREE-DIFF-PASS, covering Unicode edits, exact metadata, cursor and
  observer retention, compound edits, nested wrapping/unwrapping undo/redo and
  bibliography field changes. Runtime execution also verified the generated
  binding rather than only its declaration.
- The fixture initially used an ambient Scheme author after setup. Undo then
  traversed both foreign-author steps instead of stopping at the latest editor
  step. Explicit start-editing now matches normal command entry. History/kernel
  code was not changed to compensate for this fixture issue. Temporary history
  diagnostics were removed. No full suite, deployment or Notes migration ran.

## Staged source lifecycle and correction integration (2026-09-27 evening)

- Added explicit owner adoption of an already identity-complete body through
  `adopt-source-node-identities` / `source-node-identities-active?`. This is a
  fresh-baseline operation, not an implicit migration or a preference switch.
  The native validator does not allocate identities, refuses incomplete or
  ambiguous baselines, and keeps ordinary buffers unactivated.
- Active document/body replacement validates the entire incoming baseline
  before mutating the old source or disposable index. Successful replacement
  replaces the index; a rejected import reports failure rather than pinning a
  different disk revision or marking unchanged old contents saved. Existing
  transaction/finalization/history hooks now have a validated owner entry path.
- DRD WITH-like passthrough macros may have UNKNOWN tail argument types even
  when their content flow is known. The planner uses the DRD with_like contract
  (not a hardcoded macro list or accessibility alone), preserves inline role
  inheritance, and still rejects unclassified custom macros.
- Full buffer snapshots preserve document envelope, standard field,
  COLLECTION/ASSOCIATE/key headers and unknown attributes without undoing
  viewport/no_aux filtering. Annotated empty collections remain present. In
  particular, the two-argument is_func predicate excludes zero-arity nodes;
  comparing L(t) avoids resetting annotated empty COLLECTION nodes. Updating
  or removing document attributes also preserves their surviving headers.
- Structural correction and math normalization now reconstruct native headers
  explicitly. A source-specific tokenize/recompose path preserves independently
  annotated children and their parent identity; read-only token analysis is
  unchanged. Heuristics do not consume identified delimiter/script wrappers or
  flatten identified nested DOCUMENT/CONCAT nodes.
- Normal ATHENA.bin build succeeded. One consolidated isolated real-BufferActor
  fixture, `tests/scheme/node-source-lifecycle-test.py` / `.scm`, was debugged to
  exit 0 with ATHENA-NODE-SOURCE-LIFECYCLE-PASS. It covers adoption, inline roles,
  insert/split/join and replayed IDs, conflict/cancel rollback, baseline
  replacement, source snapshots including empty collections, correction entry
  points and native attached-source diff. Default XML v1 save was verified to
  reject metadata while leaving the isolated original file byte-for-byte intact.
- Final evidence: `build_qt6/node-source-lifecycle-build.log` and
  `build_qt6/node-source-lifecycle-check/source-lifecycle-7s839pll/`.
  Earlier same-prefix artifacts are failed diagnostic runs, not the final result.
  A fixture initially invoked tree-set-diff on a detached target; it was fixed,
  not the native ownership precondition. Temporary native tracing was removed.
- No global XML/AUDMAP/tmfs cutover, production migration, deployment, full
  suite or unrelated tests ran in this batch. It was subsequently committed as
  `6b965f3c9 improve: integrate source identity lifecycle`.

## Normal XML v2 persistence activation (2026-09-27 evening)

- Native document dispatch now distinguishes XML v1 from XML v2 while keeping
  the low-level v1 codec strict. Pinned XML storage records its envelope version
  and serializes/round-trips with the same codec on later saves. Legacy-to-XML
  conversion and ordinary v1 documents still land/stay on v1; nothing is
  promoted merely because v2 support exists.
- `buffer-import` carries the decoded persistence version to the BufferActor.
  A normal v2 load preflights an identity-complete source baseline and activates
  the owner-local identity index before publishing it. Ordinary edit
  transactions then allocate new source UUIDs through the existing finalizer.
  v1/legacy loads explicitly leave the index disabled. A v2 Save As to a new
  path creates v2; an existing target with a different XML storage version is
  rejected instead of silently changing the persistence contract.
- Normal v2 saves preserve metadata with `write_xml_v2`. Native `texmacs`
  autosave export for an active v2 buffer also writes v2 after finalizing pending
  identities, rather than passing through the legacy TeXmacs serializer.
  Recovery now uses `buffer-import` so the recovered buffer retains the v2
  envelope state and recreates its identity index. Bare wikilink, AUDMAP
  protocol activation, Artifact production and vault migration were untouched.
- The shared semantic-document fingerprint now uses canonical v2 whenever node
  metadata is present, so existing indexing/reference consumers can hash v2
  source without a lossy v1 serialization. This is still distinct from the
  outstanding model-input/embedding fingerprint contract.
- The normal `ATHENA.bin -j20` build passed. One consolidated isolated runtime
  fixture, `tests/scheme/node-v2-persistence-test.py` / `.scm`, passed
  `ATHENA-NODE-V2-PERSISTENCE-PASS`. It exercised a real v2 normal load,
  automatic owner activation, transactional UUID assignment, normal save,
  v2 autosave, recovery into another buffer, save of that recovered buffer and
  reopen of the normally saved file. The Python side parsed the resulting XML
  and verified the original and newly allocated identities survived those
  boundaries.
- Final evidence: `build_qt6/node-v2-persistence-build.log` and
  `build_qt6/node-v2-persistence-check/node-v2-persistence-lv75g3b8/`.
  No production vault, deployment, full suite or unrelated test target was used.

## AUDMAP document-model v3 activation (2026-09-27 evening)

- The transport remains AUDMAP protocol 2, while the negotiated document model
  is now version 3 in the endpoint descriptor, HELLO and WELCOME. Exact-version
  rejection remains in place; authentication identities and remembered trust
  rules were not reset or migrated.
- Document resources now use the existing v3 codec for full reads, scalar
  properties and source-generation handoff. Persistent node `id` and typed
  `properties` are exposed separately from connection/ticket-scoped integer
  handles. Structural `set` preserves the target's existing source metadata and
  rejects client-supplied metadata; insertions likewise cannot inject UUIDs.
- Active XML-v2 sources advertise `assign_id {}` and
  `update_properties {set:[...], remove:[...]}`. Both reuse the native property
  planner: assign-ID is server-generated and idempotent, property deltas keep
  protected UUID/artifact-binding fields inaccessible, and edits remain owner
  local. Live v2 structural edits finalize required source identities before
  commit; saved v2 edits complete the detached identity baseline before atomic
  publication. v1/legacy sources are not silently promoted by metadata commands.
- The standalone Python SDK, bundled plugin SDK copy, public C++ SDK constants,
  CLI/REPL help and examples now advertise document model 3. Documentation
  explicitly distinguishes transient handles from persistent source UUIDs.
- The normal `ATHENA.bin -j20` build passed. Because the host Python lacks
  pyzmq/msgpack, the consolidated real-protocol fixture builds the repository's
  current C++ stdio client sources as a disposable probe without adding or
  invoking another CMake target. `tests/interop/audmap-v3-runtime-test.py`
  passed `ATHENA-AUDMAP-V3-PASS` against an isolated offscreen ATHENA instance.
  It verified v3 negotiation, anonymous-node assign-ID, idempotence, typed
  property round-trip, arbitrary-UUID rejection, automatic identity allocation
  after structural insertion, replacement-handle staleness with UUID retention,
  and metadata persistence across a fresh client connection/re-resolution.
- Final evidence: `build_qt6/audmap-v3-build.log` and
  `build_qt6/audmap-v3-check/audmap-v3-4x6vdpl2/`. No full test suite,
  production vault, deployment or persistence migration was used.

## Artifact source binding and revision separation (2026-09-27 night)

- XML-v2 Artifact extraction now carries the persistent source UUID and a stable
  extraction role. The Artifact producer has a dedicated internal mutation
  boundary for the reserved `athena:artifact-bindings` dictionary; ordinary
  property APIs, AUDMAP and UI remain unable to overwrite this producer-owned
  identity field. Missing source IDs are assigned through the same validated
  metadata path.
- On first v2 adoption, an existing Artifact UUID may be inherited from the old
  conservative cross-build association when that association is accepted. The
  role -> Artifact UUID pair is then persisted in the source document. From that
  point the source binding is authoritative: database rows store
  `source_uuid`, `source_role` and the exact model-input fingerprint, and
  source lookup prefers UUID over anchor/content heuristics. v1/legacy documents
  keep the old association behavior until offline migration.
- Artifact database schema is now version 3. Existing databases are extended in
  place with source/revision columns; read-only queries remain compatible with
  older rows by projecting absent v3 columns as empty values. Artifact DBs are
  still disposable caches rather than identity authorities.
- Revision semantics are separated explicitly. Storage revision tracks source
  bytes; Artifact content revision strips source UUIDs and producer bindings but
  retains semantic typed properties; the full semantic/source revision still
  notices UUID/property identity changes. Definition-range checkpoints use the
  exact request/model contract hash, so an unrelated edit elsewhere in a
  document does not invalidate a byte-identical model request.
- Producer binding publication is completed before the disposable DB cache is
  replaced. Closed files use the pinned XML-v2 document transaction. An open,
  unmodified buffer is mutated on its BufferActor and saved normally; an open
  modified buffer is rejected rather than merging disk-derived identity into
  unsaved source state.
- The normal `ATHENA.bin -j20` build passed. A single focused Artifact test,
  `TestArtifacts::persistsNativeSourceBindingsAndReusesExactModelInput`,
  passed with 0 failures. It verified source binding persistence, reuse of an
  unchanged range-model input after a different enunciation edit, and recovery
  of the same Artifact UUIDs after deleting all three Artifact databases.
  No production vault, deployment or broad test suite was used.

## One-use cut/move identity credentials (2026-09-27 night)

- Native cut/paste now distinguishes snapshot copying from an authorized move.
  A cut of metadata-bearing source in an active v2 vault issues an opaque
  process-local credential bound to the exact native clipboard snapshot, source
  actor/view, vault root and a history marker. The token may travel with the
  native clipboard envelope, but clipboard bytes alone never authorize identity
  preservation because the registry entry is process-local.
- The first compatible paste in the same vault reserves that credential and
  inserts the stored source identities unchanged. The credential becomes active
  only after the target identity finalizer accepts the edit; failed target edits
  release the reservation. Ordinary copy, second paste, cross-vault paste,
  transformed/table/graphics paste, stale source history and external clipboard
  offers all use `duplicate_source_nodes`, renewing UUIDs/remapping internal
  references/clearing Artifact bindings as before. A cross-vault first paste
  consumes the move opportunity rather than leaving a later same-vault paste
  able to resurrect the old identity.
- Full identified source-object cut semantics were corrected at the same
  boundary. Selecting a complete identified child removes that source object;
  it no longer leaves an empty node carrying the old UUID. Partial text deletion
  still preserves the paragraph/source identity, so delete-content and
  delete-object remain distinct operations.
- Source and target history entries share the move marker. Coordinated undo
  first removes the target then restores the source; redo first removes the
  source then restores the target. Both peers must expose the corresponding move
  as their next applicable history operation, otherwise coordination refuses to
  cross later independent edits. Peer redo selects the branch containing the
  marker instead of assuming branch zero. The marker detector recognizes both
  birth directions because history inversion flips the birth bit on redo.
- The normal `ATHENA.bin -j20` build passed after tracing was removed. The one
  isolated real BufferActor fixture `tests/scheme/node-move-lifecycle-test.py`
  / `.scm` passed `ATHENA-NODE-MOVE-LIFECYCLE-PASS`. It exercised same-vault
  first move identity preservation, repeat-paste duplication, cross-actor
  coordinated undo and redo, a second cut followed by cross-vault copy, consumed
  credential fallback on returning to the original vault, and ordinary local
  undo once the move credential had been discarded. No production vault,
  deployment or broad test suite was used.

## Born-v2 ordinary source documents (2026-09-27 night)

- Ordinary user-created source documents no longer begin life as anonymous v1
  buffers. The native creation helper builds a detached document body, assigns
  the standard source-role identity baseline, installs it through the normal v2
  replacement path and therefore starts the BufferActor with its owner-local
  identity index active from the first edit.
- Missing-file creation in the normal load UI now calls that source helper
  instead of first installing `(document "")`. New document and New Window
  entry points likewise allocate a born-v2 source buffer. Transient/derived
  buffers such as DataArt retain the older generic `make_new_buffer` path so
  this does not silently turn every internal scratch tree into persisted source.
- The first normal save of such a document creates XML v2 storage. New edits
  receive UUIDs transactionally before that save rather than relying on a
  persistence-time migration.
- The normal `ATHENA.bin -j20` build passed. One isolated real-BufferActor
  fixture, `tests/scheme/node-new-v2-test.py` / `.scm`, passed
  `ATHENA-NODE-NEW-V2-PASS`. It covered named source creation, active
  identities, ordinary UUID allocation, first-save XML-v2 output and the
  user-facing New scratch-buffer allocator.
- This does not replace vault migration. Existing UTF-8/XML-v1 vaults remain v1
  until a separate offline node-model migration is requested. The existing
  `--upgrade-vault-format` command remains the older legacy
  Cork/S-expression -> UTF-8/XML converter and must not be overloaded for the
  node-model migration.

## Offline UTF-8 XML -> node-model vault migration (2026-09-27 night)

- Added a separate `--upgrade-vault-node-model VAULT_DIRECTORY` CLI. It is not
  an alias for `--upgrade-vault-format`: an unmarked input vault must consist
  entirely of native UTF-8 XML-v1 `.ath` documents. Legacy Cork/TeXmacs markup
  and legacy Scheme serialization are rejected with an explicit instruction to
  run the older format upgrader first. A migrated vault records
  `node_model_version: 1` in `Vaultfile.json`; a repeated node-model upgrade
  revalidates XML-v2 identity completeness and returns without creating another
  backup or rewriting bytes.
- Migration reuses the whole-vault offline safety model but has its own semantic
  pipeline. It acquires the exclusive vault directory lease, inventories and
  hashes the original tree, clones a private metadata-preserving sibling
  snapshot, performs every source/database rewrite there, validates the staged
  result, fsyncs it, rechecks the untouched original for external changes and
  only then publishes by Linux `renameat2(RENAME_EXCHANGE)`. Before the
  exchange, failures delete only the private workspace. After exchange, the
  complete original UTF-8/XML vault remains as the sibling recovery backup with
  a migration manifest.
- Each source document first converts declared legacy enunciations through the
  registry using the effective vault `number solutions` preference (defaulting
  to the normal built-in value when absent). Identity planning then uses the
  real document/style DRD and the standard source-role contract. HLINK and both
  legacy/canonical transclusion forms now have explicit source-role contracts:
  display text is inline source content while locator UUID/path/anchor payloads
  are data, so reference metadata never receives spurious source IDs.
- Missing source UUIDs use a deterministic SHA-256-derived UUID keyed by the
  relative document path, semantic role/category and source path. This allocator
  is pure and reproducible. When a legacy map row resolves structurally to one
  identity candidate and its old map UUID is already a canonical UUID, that old
  UUID is preferred so existing links retain identity without a rewrite where
  possible. Multiple aliases may collapse to the same source UUID; no fuzzy
  content matching is used to choose a source object.
- Legacy map locations are resolved structurally in the private snapshot:
  whole-document rows map to the body, generated heading/enunciation anchors map
  to their following source object, explicit labels remain explicit targets, and
  general begin/end ranges enumerate the identified source objects they contain.
  Missing, ambiguous, reversed or structurally inconsistent anchors are
  diagnostics. In-document `tmfs://wikilink/<old>` targets are rewritten to
  the single migrated source UUID. Four-argument legacy transclusions become
  canonical `TRANSCLUDE(TUPLE(uuid,...))` lists; unresolved references abort
  migration rather than falling back to file/anchor hints.
- Existing Artifact UUIDs are preserved. The old Artifact index is queried
  read-only in the snapshot, each legacy artifact source is located before the
  database becomes authoritative, and the producer-reserved
  `athena:artifact-bindings` role -> Artifact UUID property is written into
  migrated source. The disposable Artifact database is then extended/backfilled
  with `source_uuid/source_role`. The later normal Artifact builder can
  therefore reconstruct the same Artifact UUIDs from source even if all Artifact
  databases are deleted.
- `map.sqlite` is retained only as a compatibility locator for the subsequent
  bare-wikilink cutover. Single-target rows are re-keyed to their migrated source
  UUID and duplicate aliases collapse; old multi-target range rows may remain
  for compatibility, but canonical transclusions in source no longer depend on
  them. The Vaultfile node-model marker is the future semantic switch; URL shape
  alone still does not authorize treating every bare wikilink UUID as a source
  node until the next cutover block.
- The normal `ATHENA.bin -j20` build passed. One isolated CLI fixture,
  `tests/Data/Convert/Xml/vault_node_model_upgrade_test.py`, passed
  `ATHENA-NODE-MODEL-UPGRADE-PASS`. It exercised XML-v1 -> XML-v2 conversion,
  canonical enunciation conversion, old-map UUID preference/alias collapse,
  wikilink and transclusion rewriting, Artifact UUID/source-binding preservation,
  original-vault backup retention, Vaultfile activation, second-run idempotence
  and explicit rejection of legacy Cork input without modifying it. No
  production vault, deployment or broad test suite was used.

## Bare wikilink source-UUID cutover for migrated Vaults (2026-09-28)

- `Vaultfile.json` `node_model_version` is now published in the immutable native
  Vault snapshot/context and exposed to Scheme. It is the only semantic switch:
  a UUID-shaped `tmfs://wikilink/...` is not enough to opt a legacy Vault into
  source-node semantics.
- In a migrated Vault, following a wikilink strips any historical file/anchor
  suffix from the identity operation and routes the UUID through the existing
  native node-reference locator. That locator inventories live BufferActor
  owners before saved files, distinguishes missing/conflict/read failures, and
  revalidates the UUID at the resolved source address before navigation. Broken
  migrated links no longer invoke fuzzy/file-hint repair: hints are explicitly
  non-authoritative. Unmigrated Vaults retain the old `vault-get-node` / map
  navigation and repair path unchanged.
- Link preview uses the same Vaultfile gate. Migrated wikilinks, including URLs
  that still carry old hint suffixes, request the native UUID reference view;
  legacy wikilinks continue through map/anchor preview. The native
  `node-reference-target?` and open boundary likewise reject wikilink UUID
  semantics unless the active Vault is migrated.
- The incremental reference graph now expands canonical transclusion UUID
  tuples correctly and, in migrated Vaults, derives `UUID -> document path`
  from the XML-v2 source census rather than `map.sqlite`. Duplicate source UUIDs
  stay unresolved instead of choosing a file. Legacy Vaults keep map-signature
  invalidation and map-based target resolution. Changing only compatibility map
  rows therefore cannot redirect a migrated graph edge.
- New Wikilink insertion in a migrated Vault resolves the user's selected file,
  whole-document body, heading or migrated enunciation target to the actual
  persistent UUID stored in the XML-v2 source. The result tuple carries that
  UUID back to Scheme; Scheme refuses insertion when no persistent source
  identity is available instead of allocating a new map UUID. Legacy insertion
  continues its existing map lookup/allocation behavior.
- Static website generation snapshots `node_model_version` from Vaultfile and
  builds a source UUID -> document index from the Vault documents. Migrated
  wikilinks use this index and never fall back to file hints/static assets when
  identity resolution fails; legacy website export keeps map/hint behavior.
  `map.sqlite` remains present for compatibility/rename consumers and is not
  deleted by this cutover.
- The normal `ATHENA.bin -j20` build passed. One focused existing Qt fixture,
  `TestVaultMapSqlite::gatesBareWikilinksOnNodeModelVersion`, passed with 0
  failures. It loaded a migrated Vault whose compatibility map deliberately
  pointed the wikilink UUID at the wrong file and verified native-target gating
  plus a reference edge to the real XML-v2 source UUID owner; changing the map
  again did not redirect the edge. The same URL in an unmarked Vault was not a
  native wikilink target, resolved through map.sqlite, and followed subsequent
  map redirection. No production Vault, deployment or broad test suite was used.

## Generated identity-anchor production removed (2026-09-28)

- The old anchor generator is no longer a runtime subsystem. Removed
  `src/ATHENA/Data/vault_anchors.{cpp,hpp}`, the manual `Anchor enunciations`
  command/native glue, manual-save preflight/auto-approval, the Qt confirmation
  dialog, the two auto-anchor preferences and their UI, and the dedicated
  `anchor-structures` vault-maintenance pass. Their focused generator/dialog
  tests were removed with the implementation.
- Manual saves now go directly through the normal buffer save path. Vault
  maintenance no longer creates, updates or rewrites heading/enunciation
  identity anchors. The old "anchor reader processes" setting had also been
  reused by ToC workers; it was renamed to the generic
  `vault maintenance worker processes` / `maintenance_worker_processes` rather
  than preserving an obsolete anchor name.
- A legacy range helper still recognizes heading structure directly in Scheme;
  it no longer calls a native anchor-generator predicate. Old anchor parsing in
  transclusion/legacy compatibility code remains for reading unmigrated source,
  but it is not an identity producer.
- The normal `ATHENA.bin -j20` build passed. The existing two-BufferActor manual
  save fixture passed six saves with correct source ownership and no anchor side
  effect. A separate isolated `--vault-maintenance --check-only` run on an XML-v1
  fixture passed health-check and asserted that no `anchor-structures`/anchoring
  pass appeared (`ATHENA-NO-AUTO-ANCHORS-PASS`). No production Vault or broad
  suite was used.
- Historical generated labels are deliberately not deleted by ordinary runtime
  maintenance. Their safe removal belongs to the offline node-model migration,
  after legacy map/reference resolution has completed and before the staged XML
  v2 vault is published. That migration-time cleanup and fully anchorless
  migrated transclusion selection are the next block.

## Migration-time generated-label retirement (2026-09-28)

- Historical generated identity labels are now consumed as one-time migration
  evidence and removed only in the private node-model staging vault. No runtime
  anchor generator or maintenance pass was reintroduced.
- Heading labels are retired only when a legacy map row points at the label,
  the next substantive source object is a heading, and the label exactly equals
  the old generated `H<level> <current heading title>` convention. Enunciation
  wrapper pairs are retired only when begin/end form the same `{ / }` stem,
  enclose exactly one canonical enunciation, and that stem is also recorded by
  the old Artifact index for an enunciation in the same document. This
  intentionally prefers false negatives over deleting a user label.
- Removal happens only after source UUID planning, Artifact source binding and
  reference rewriting. Matching single-target compatibility map rows are
  re-keyed to the migrated source UUID and have `anchor_begin/anchor_end`
  cleared, so they cannot point at labels that were just removed.
- The existing isolated node-model migration fixture passed again. It now also
  verifies that the generated theorem wrapper labels disappear, an unrelated
  explicit `User-kept` label survives, and the retained map row contains the
  migrated UUID/path with empty anchor fields while wikilink/transclusion and
  Artifact UUID preservation remain correct.

## Build Boundary

Normal builds use only:

```sh
cmake --build build_qt6 --target ATHENA.bin -j20
```

Do not run the default target, the full test suite, or TSan implicitly. Added
regression sources are not evidence of executed tests. Report compilation,
executed checks, deployment and format activation separately.
