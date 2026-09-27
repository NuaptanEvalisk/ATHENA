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
  legacy variant and presentation contracts plus detached conversion. This is
  not yet the live typesetter or a replacement for existing runtime consumers.
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

## Integration Gates Still Required

1. Assign source IDs through content roles, with complete handling of nested
   bodies, headings, paragraphs, insertion, deletion, formatting, normalization
   and source-modifying Scheme paths. Preserve semantics across stree boundaries.
2. Implement one-use cut/move credentials, copy policy for artifact bindings,
   internal tmfs-reference rewriting, and coordinated cross-document undo.
3. Replace live enunciation consumers with the registry; provide native
   rendering, numbering, property editing and explicit proof associations.
4. Implement native UUID resolution using validated disposable location indexes,
   live actor snapshots, coalesced background scans and distinct error states.
   Ordered transclusions must reject ancestor/descendant overlap and retain
   missing entries. Retire hint-dependent map identity without losing rename
   recovery journals.
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

## Build Boundary

Normal builds use only:

```sh
cmake --build build_qt6 --target ATHENA.bin -j20
```

Do not run the default target, the full test suite, or TSan implicitly. Added
regression sources are not evidence of executed tests. Report compilation,
executed checks, deployment and format activation separately.
