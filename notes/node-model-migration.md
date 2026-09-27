# Native Node Model Migration

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
  and property schema validation. It is not a live-editor identity allocator.
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
2. Implement one-use cut/move credentials, copy policy for artifact bindings,
   internal tmfs-reference rewriting, and coordinated cross-document undo.
3. Finish remaining live enunciation consumers and source creation. Native
   rendering/numbering and property UI are implemented; proof targets can be
   entered explicitly, but UUID resolution and target selection remain.
4. Finish dynamic export dependencies and
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

## Build Boundary

Normal builds use only:

```sh
cmake --build build_qt6 --target ATHENA.bin -j20
```

Do not run the default target, the full test suite, or TSan implicitly. Added
regression sources are not evidence of executed tests. Report compilation,
executed checks, deployment and format activation separately.
