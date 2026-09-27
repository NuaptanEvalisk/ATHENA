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
