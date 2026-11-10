# ATHENA Document Cache and Realtime Derived Content

Design date: 2026-10-20. Scope revised: 2026-11-10.

Status: implementation in progress. Background storage and reusable contribution
logs are implemented; document cache ownership and realtime views are not yet
connected. The following scope supersedes the broader historical design.

## Revised Scope (2026-11-10)

- Keep `.cache/<full-source-filename>.ac`, including the source extension to
  distinguish ATH/AVD files with the same stem.
- Cache only auxiliary-derived contributions and directory results, dependency
  identities, numbering and separately validated layout page bindings.
- Do not cache shaping, general boxes, font objects or other typesetting object
  graphs. The persistent shaping cache owns that independent capability.
- Reopening must reuse matching results, not rebuild every directory. Cache IO
  and computation run asynchronously; publication checks the source generation.
- Changed contributions are replaced incrementally. Persistence coalesces work
  and updates records, rather than serializing the complete cache per keystroke.
- Source contains directory declarations/options, headings, captions and markers,
  not generated bodies or auxiliary envelopes. Derived changes must not enter
  undo, history or Hodarium source revisions. `tmfs://aux` is outside this work.
- Export waits for the derived results of its source/layout snapshot. Closing
  the editor does not require waiting for cache production.

Implementation sequence:

1. Separate directory algorithms from editor mutation and shared mutable state.
2. Introduce contribution ownership, asynchronous `.ac` storage and publication.
3. Render generated directories as derived views and connect incremental changes.
4. Remove persisted auxiliary, obsolete update controls and source-generation
   paths, integrate export readiness and cache lifecycle, then build/deploy.

The historical rendering-cache design in sections below is background only;
it is not an implementation requirement for the revised scope.

### Current Implementation Boundary

`src/Document/State/derived_directory.{hpp,cpp}` now owns detached ToC, index,
glossary and list generation. Inputs are explicit, source identities are removed
from derived copies, and rich-text properties survive label removal. Index
grouping and locale state are local to an invocation. The editor currently calls
these functions through the existing Update entry points; it still assigns the
results into source. This is preparatory work, not realtime-cache completion.

`auxiliary_cache.{hpp,cpp}` provides worker-owned SQLite storage and computation,
receipt-checked in-memory lookup, replacement of pending work by key, stale-job
rejection, and result publication before disk IO. Dirty records are coalesced
for 750 ms and written transactionally; cache hits do not rewrite records.
Explicit waiting and flushing are separate from nonblocking close. Incompatible
or damaged caches can be rebuilt, while storage failures leave computed results
available. Source existence is rechecked around writes. Idle-source deletion
watching and lifecycle integration are still pending.

`auxiliary_contributions.{hpp,cpp}` retains frozen WRITE payloads in shared
ordered logs. Bridge, surround and inline-fragment replay reuse child logs;
unchanged contributions preserve log identity even if ordinary body text changes.
Failed evaluation rolls back captures. The old complete-pass auxiliary map is
currently populated from this log; partial-pass logs are ready for the upcoming
asynchronous consumer. This does not yet remove auxiliary from saved documents.

The focused storage test covers superseded computations, reopening, storage
contention without blocking result publication, transactional retry, record
deletion, format invalidation, damaged-cache rebuilding and closing/deleting
during computation. The bridge check exercises reuse, replacement, deletion and
retention of an earlier snapshot. These checks do not establish completed
realtime directory, export or save integration.

Verification on 2026-11-10: `auxiliary_cache_test` and
`auxiliaryContributionsFollowBridgeReuse` passed. `ATHENA.bin` was built with
`-j16` and deployed with the local executable installer (build ID
`499588b48a8f4c8e2529aff3d42b0df1f5926f40`). No broad regression suite was run.

Remaining owner integration must keep receipt preparation, MessagePack encoding
and decoding, and directory generation on the worker. Publishing raw cache bytes
and then decoding a complete directory on the BufferActor would merely move the
pause to a different boundary. The actor should receive frozen generated trees
and invalidate only affected directory displays. Unsaved buffers need an
in-memory session; Save As must change cache ownership without treating the
not-yet-created destination as a deleted source. Folded/progressive display logs
are partial views and must not be mistaken for a complete document inventory.
Before enabling production cache writes, exclude `.cache` from vault inventory,
watch registration, and Hodarium application/file traversal. The current explicit
exclusions in `background_workers.cpp`, `hodarium_worker.cpp`, and
`hodarium_application.cpp` list `.athena`, `.backup` and `.git`, not `.cache`.

Next integration points, verified against the current native implementation:

- `bridge_rep::typeset` already retains and reuses immutable-shaped event logs
  for contracts and source occurrences. Auxiliary contribution capture should
  follow bridge reuse/replacement, including nested and cached inline expansion,
  instead of relying on `env->complete` and a cleared global append-only map.
- `concater_rep::typeset_write` currently computes entries even when incomplete,
  then discards them. Capture must distinguish source contributions from derived
  directory rendering so generated output cannot feed its own collection.
- `edit_typeset_rep::typeset_sub` is the owner-side layout publication boundary.
  Worker publication needs an ID-only actor readiness signal, analogous to
  `buffer_actor::notify_contracts_ready`, with a separate derived generation.
- Native `tree` handles cannot be casually shared across threads. Existing
  `node::frozen_tree` and detached ownership transfer provide the appropriate
  contracts. Freeze changed contribution units, not the entire document after
  each edit. The existing v3 structured-value codec preserves node properties;
  its MessagePack representation is suitable for binary cache records.
- Use the already-linked SQLite implementation with worker-owned connections and
  transactional record replacement. Editor reads consume published memory, not
  database queries. A single storage worker does not require WAL to keep the
  editor responsive. Record source/style/dependency receipts, contribution order
  and result versions separately from layout-specific page bindings.
- Remove both `attach_data` and `refresh_interop_document_source` auxiliary
  persistence only when the derived view replacement is connected. Audit
  `append_interop_source_attributes` as well: it can restore annotated empty
  auxiliary envelopes. Do not conflate references or attachments with auxiliary.
- Directory generation and page-number convergence remain required for export;
  replacing the editor update menu alone is insufficient.

This plan replaces persisted TeXmacs auxiliary data and user-driven Document
Update with realtime derived content and disposable per-document `.ac` caches.
The cache must reduce computation without making editing, saving, or closing
wait for cache production. It accelerates reopening by retaining auxiliary
computation results rather than rerunning generation on every open.

## 1. Requirements and Decision Status

### Required behavior

- Store one ATHENA Cache file beside each source document, inside its sibling
  `.cache/` directory. ATHENA manages creation, maintenance, and removal.
- The cache contains former auxiliary computation results and their dependencies.
- Persist binary records, not XML or Scheme text. General rendering state and
  shaping are outside this cache.
- `.ac` is entirely disposable. Cross-ATHENA-version compatibility is not a goal;
  an incompatible cache is discarded rather than migrated.
- Deliberately malicious cache files are outside the threat model. Do not add
  authentication, signing, sandboxing, or an elaborate security architecture.
  Handle ordinary interrupted writes, incompatible builds, and damaged files.
- Cache serialization and filesystem operations must not block the GUI or
  BufferActor. Do not deep-copy a document or box graph on an editing boundary.
- Computation and persistence are incremental at appropriate units. Do not
  serialize a whole document after each edit or create a disk record per keystroke.
- Cached content must never be required to save source, capture history, or sync.
- The table of contents is realtime, including unsaved edits and undo/redo.
  A user must not need Document Update to make derived content correct.
- Deleting a source document eventually removes its managed cache. Cache writes
  already queued for that document must not resurrect the deleted cache.

### Proposed implementation choices

The SQLite container, MessagePack encoding, record schema, naming collision
policy, queue budgets, and scheduling parameters below are implementation
proposals. They are not existing capabilities or measured performance results.

The original naming example was `Example.ath -> .cache/Example.ac`. A proposed
collision-free spelling is `.cache/Example.ath.ac`, with `.cache/Example.avd.ac`
for an AVD of the same stem. Settle this spelling before implementation; do not
allow two source types with the same basename to share a cache accidentally.

This plan does not authorize rewriting the user's Notes, recovering closed
buffers, or repairing generated directory entries in existing documents.

## 2. Implementation Audit Baseline (2026-10-20)

### Auxiliary lifecycle

`new_data::aux` is a map from a channel name to a generated tree. Standard styles
write `toc`, `figure`, `table`, `index`, and `glossary` channels; custom channel
names are possible. Figure captions can be collected even without an explicit
list of figures in the document.

The current pipeline is:

```text
source headings, captions, index and glossary markers
  -> style evaluation / WRITE
  -> buffer auxiliary map
  -> generate_aux and its consumers
  -> generated bodies inserted into source directory nodes
  -> auxiliary and generated bodies persisted with the source document
```

Relevant implementation:

- `src/Typesetting/Concat/concat_active.cpp`: `typeset_write` evaluates and collects
  channel entries only when `env->complete` is true.
- `src/Typesetting/Bridge/typesetter.cpp`: a complete pass clears `local_aux` before
  collecting it again. Partial passes do not incrementally replace contributions.
- `src/Editor/Process/edit_process.cpp`: generates contents, indexes, and glossaries
  by clearing generated bodies, moving the cursor, and inserting rebuilt trees.
- `src/Document/State/new_data.cpp`: attaches and detaches persisted collections.
- `src/Interop/Adapters/interop_document_source.cpp`: refreshes the source envelope
  from the live auxiliary and reference maps.
- `src/App/Runtime/buffer_actor.cpp`: normal source saving and document-history
  snapshots serialize that envelope, so invalid auxiliary data blocks them too.
- `src/Sync/State/hodarium_worker.cpp`: document revisions carry saved source
  bytes, including these derived values. Cache-only differences can therefore
  create source revision differences.

`references` is a separate number/page binding map, not another name for `aux`.
Attachments are separate again. Materials has its own rendering update path.
The native Outline pane reads the current document through actor snapshots; it
does not depend on a saved auxiliary table of contents.

### Already fixed

Commit `8ededcc97`, `fix: preserve native math in generated auxiliary entries`:

- Preserves semantic properties in the WRITE-side label-removal transformation.
- Removes source identities from derived copies.
- Adds XML writer paths, ancestry, and concrete MathScripts validation reasons.
- Adds caption generation and codec regression coverage.

The triggering live document had valid math in its body, but its generated figure
caption lost `math:scripts-policy` on `S^1`. Saving and history capture failed.
No repair of that closed buffer or its source file is part of this plan.

### Remaining code-audit findings

These are dated findings, not newly executed runtime tests. On 2026-11-01,
`edit_process.cpp` still reconstructs `remove_labels` compounds without metadata
and clears generated bodies before checking `which`. Recheck other call sites
when implementing this plan; intervening native migrations changed some owners:

1. A second `remove_labels` in `edit_process.cpp` still reconstructs compounds
   without their metadata. Consuming auxiliary entries can again damage v3 math.
2. `generate_aux_recursively` clears every recognized generated section before
   checking `which`. Updating one kind can erase other kinds without rebuilding.
3. Native menu actions send `index` and `glossary`, while the generator matches
   `the-index` and `the-glossary`. These paths can clear without regenerating.
4. The Save auxiliary data preference is not consulted by the current native
   save and history-snapshot paths. Older `attach_data(no_aux)` also couples
   auxiliary omission with reference-map omission.
5. Auxiliary production is a full-pass side effect, not a dependency-indexed
   incremental calculation. Its map has no independently published generation.

## 3. Document Update Inventory

The menu is authored in `src/UI/Qt/Commands/QTMDocumentMenuCommands.cpp`; native
actions dispatch through `src/Editor/Commands/native_editor_actions.cpp` to
`ATHENA/progs/generic/document-edit.scm`.

| Current command | Actual work | Replacement |
| --- | --- | --- |
| Update All | Materials update, auxiliary generation, inclusion and picture cache resets, full buffer invalidation | Dependency-driven derived updates |
| Update Buffer | Full typesetting invalidation, including preamble and root bridge | Ordinary scoped invalidation; diagnostic full rebuild only |
| Referenced Materials | Recompute citations/list and assign a replacement document tree | Citation and Materials dependency updates |
| Table of contents | Force typesetting, collect entries, overwrite generated source body | Realtime directory view |
| Index / Glossary | Auxiliary transformation and generated source-body replacement | Incremental entry indexes and views |
| Refresh inclusions | Reset inclusion cache and invalidate all views | Resource change notification and dependent-unit invalidation |
| Refresh pictures | Reset picture cache and update all buffers | Image-version invalidation of actual consumers |

`inclusions_gc` in `tm_server.cpp` and `picture-gc` in `tm-tools.scm` make Update
All affect other open documents, not merely the active one.

Other surfaces that must change:

- `ATHENA/misc/ui/application-shell.json`: `editor.update-all`, also exposed by
  the native editor command provider and actor dispatch.
- `ATHENA/misc/preferences/catalog.json`: `document update times`, currently a
  choice of repeated passes, with the `doc-update-times` callback.
- `src/UI/Qt/Commands/QTMFocusSpecialCommands.cpp`: Materials update action.
- `src/UI/Qt/Vault/QTMNamespaceExport.cpp`: finalization calls Update All and
  uses delayed work rather than a derived-state completion contract.
- `src/Domains/Vault/vault_maintenance_pass_health.cpp` and
  `src/App/Entry/athena.cpp`: ToC maintenance workers load documents, generate
  their contents, and save source files.
- Remaining Scheme documentation utilities and some runtime checks call
  `generate-all-aux`, `generate-aux`, and `update-current-buffer` directly.

Materials already refreshes on document import and several explicit insert/style
operations. That is not a unified subscription to citation order, Materials
record revisions, inherited Namespace membership, and CSL dependencies.

Keep the concepts of invalidation, recomputation, and waiting for a fixed
snapshot's derived results. Remove the requirement that users invoke them to
obtain a correct document. Explicit external data retrieval and plugin execution
remain separate user operations; do not automatically rerun them as cache work.

## 4. Source and Derived State Boundary

The authoritative source retains headings, captions, index/glossary markers,
citation UUIDs and locators, explicit material selections, styles, user metadata,
and directory declarations with their scope and display options.

Generated directory bodies, collected auxiliary entries, resolved layout page
numbers, and typesetting products are derived state. They belong in memory and
optionally `.ac`, not in source saving, undo records, document history, or
Hodarium document payloads.

Do not implement this as a serializer that silently omits any invalid subtree.
Define explicit derived roles and source projection rules. Invalid authoritative
source still fails validation. Only data explicitly classified as regenerable
is excluded. Preserve custom directory titles/options and distinguish manual
content from old generated output before changing persisted representations.

Audit `references`, attachments, and Materials fields individually. Logical
labels and link targets remain source semantics even when their resolved number
or page is cached. Do not delete all of these under an auxiliary cleanup flag.

## 5. Cache Storage and Binary Representation

Use existing SQLite for the per-document indexed container and existing
`msgpack-cxx` for binary C++ value/object records. Do not invent a database file
format, allocation manager, or crash journal. SQLite is public domain;
msgpack-cxx uses the Boost Software License 1.0.

Reference implementations and documentation:

- https://www.sqlite.org/appfileformat.html
- https://www.sqlite.org/atomiccommit.html
- https://www.sqlite.org/copyright.html
- https://github.com/msgpack/msgpack-c

Suggested logical records, not a finalized SQL schema:

| Record family | Content |
| --- | --- |
| Header | Application/format marker, build compatibility token, source UUID/type, known source revision |
| Units | Source anchor, product kind, input fingerprint, environment/dependency signature, layout variant, binary payload |
| Dependencies | Unit-to-resource and unit-to-semantic-input relationships needed for validation/invalidation |
| Contributions | Per-source auxiliary/reference contributions and their ordering/scope |
| Resources | Referenced font/image/style descriptors and stable revision fingerprints, not live handles |
| Checkpoints | Sequential environment and pagination continuation state |

Persist one unit or bounded group per BLOB, not one SQL row per box or glyph.
Use record-local object IDs for shared subobjects. Avoid duplicating the same
large payload in every intermediate layer; derived records may reference shared
records within the cache.

Use short batched transactions owned by a cache service. A normal rollback
journal is a suitable initial single-file-oriented choice; temporary journal
files are permitted. Do not disable the established transaction machinery just
because the data is disposable. Lock contention skips/requeues cache work rather
than waiting in the editor. No per-keystroke fsync or shutdown flush barrier.

This is direct binary serialization of computational object state, not a dump
of C++ object memory. Encode concrete type tags, fields, and relationships; do
not persist vtables, addresses, observers, or process-specific handles. Bind
fonts and other owner-local resources when materializing a result for an owner.

## 6. Persisted Computational Products

| Product | Granularity and reusable content |
| --- | --- |
| Semantic evaluation | Cacheable macro expansion, observed input dependencies, output environment changes |
| Auxiliary entries | Individual source contributions with full structured content and logical destinations |
| Paragraph analysis | Glyph shaping, metrics, math spacing, hyphenation and line-break candidates |
| Paragraph layout | Width-specific break decisions, local line boxes and spacing |
| Formula layout | Scripts, accents, fractions, stretchy delimiters, operator geometry and source mapping |
| Tables | Per-cell content/measurements, table constraint solution, local placement |
| Block layout | Page items, stack borders, relative box graph, break penalties and backgrounds |
| Navigation basis | Relative source-to-box mappings, caret positions, labels and link declarations |
| Pagination | Output-profile-specific pages and continuation checkpoints, including pending floats/notes |

Current useful boundaries are `bridge_rep::l`, `sb`, and `changes`, plus
`line_item`, `page_item`, and the box graph. See `src/Typesetting/Bridge/bridge.hpp`,
`src/Typesetting/Format/line_item.hpp`, `src/Typesetting/Format/page_item.hpp`, and
`src/Typesetting/boxes.hpp`.

Do not cache mutable lazy closures carrying `edit_env`, arbitrary plugin runtime
state, or external side effects as if they were pure values. Represent known
pending computations as dependency-bound data, or mark that product uncacheable
and use the normal computation path. Core text, math, and table results need
actual codecs and tests, not a screenshot-based substitute.

Exclude cursor/selection/focus/hover state, GUI widgets, threads, locks, renderer
handles, and live font objects. Do not duplicate existing RAG/search databases.
Painting-only state should be separable from geometry where feasible; otherwise
the relevant appearance dependency must invalidate the stored product.

## 7. Identity and Incremental Validity

A cache key is conceptually:

```text
source anchor + product kind + semantic input fingerprint
              + environment dependency fingerprint + layout profile
```

Use persistent UUIDs where available, with relative source paths where needed.
Insertion before a block must not reuse old absolute paths or caret positions.
Persist block-relative mappings and rebind them against the current source.

UUID alone is not a content revision. Fingerprints include semantic properties,
not just visible text. In-session generations reject stale asynchronous work;
cross-session validation uses source and dependency fingerprints. Existing small
tree hashes must not automatically become authoritative persistent cache keys.

Reuse fingerprints computed during parsing/edit propagation. Do not scan or hash
the whole tree on every edit. A changed source-file fingerprint does not reject
all units: still-valid units in the changed document can remain useful.

Dependencies include actual font selection and file revisions, relevant styles
and preferences, language/line-breaking configuration, images, transclusion
sources, and relevant artifact/material revisions. Never trust an old layout
whose dependency checks have not completed.

Sequential state requires explicit input/output checkpoints: counters, macro
definitions, scoped environment changes, and reference contributions. Recompute
forward when an input changes and stop when the unchanged suffix's required
state converges. Reusing boxes without replaying their semantic contributions
would render plausible output with incorrect numbering and references.

Resize reuses shaping where valid and recomputes line breaks. Font changes
invalidate dependent shaping. Table cell reuse does not imply local-only column
solving, and a page break can affect later pages. Keep only a bounded number of
recent geometry variants rather than storing every intermediate drag width.

## 8. Ownership and Nonblocking Production

Existing boxes are mutable (`position_at`, relocation, geometry adjustment),
their reference counts are not generally atomic, and fonts belong to an owner
domain. See `src/Core/Abstractions/basic.hpp` and
`notes/font-domain-ownership.md`. Wrapping an existing box in `shared_ptr` does
not make its descendants or resource handles safe to traverse on a worker.

Required ownership change:

1. Normal typesetting produces immutable computational result blocks that are
   also used by rendering. Separate mutable placement/interaction and owner-local
   resource bindings from those blocks.
2. Publishing a completed block transfers a bounded-cost lifetime-safe handle and
   generation token. It does not traverse or deep-copy the result graph.
3. Cache workers serialize only frozen result data. They never access live trees,
   mutable bridges, editor environments, Scheme state, or native font handles.
4. Worker completion carries source/session generations. Stale results cannot
   replace a newer in-memory result or become its claimed current cache record.
5. Destruction of a large detached result must not cause an editing-thread pause;
   thread-neutral payload reclamation belongs off the editor hot path.

Do not build a second full graph synchronously merely to make it serializable.
This ownership boundary is a prerequisite, not an optimization to postpone.

Use a bounded queue charged by retained bytes as well as task count. Coalesce
successive versions of the same unit, discard superseded work, and skip caching
when saturated. Normal typing must not enqueue an unbounded history of results.
Cache maintenance needs CPU/I/O budgets too: low-priority scheduling alone does
not prevent it from competing with interactive work.

Only persist computations that normal display/export already needed. Do not
force layout to fill `.ac`. Cheap units or low-value large results may remain
memory-only when serialization is not worth its measured cost.

## 9. Open and Restore

1. Load authoritative source normally; read cache metadata asynchronously.
2. Reject incompatible builds/formats and source-identity mismatches. Missing,
   readonly, locked, or damaged cache files do not prevent opening the source.
3. Prioritize visible units and a small neighborhood rather than decoding the
   whole cache. Validate content, environment, and resource dependencies.
4. Decode immutable computation records off the editor thread. Bind owner-local
   resources and relative source locations incrementally on the proper owner.
5. Reuse matching geometry and semantic contributions. If cache I/O loses the
   race to normal computation, discard the late result rather than replacing
   newer state or making the editor wait.

Do not front-load whole-document object reconstruction onto BufferActor. Restoring
computed geometry must avoid rerunning shaping and line breaking for cache hits.

Unsaved-edit results may be cached, but they are only reusable when their input
matches the source being displayed. `.ac` is not an autosave or recovery source
and must never overwrite `.ath`. It is valid to have an incomplete or lagging cache.

## 10. Realtime Directories and Materials

Replace global append-only auxiliary collection with per-source contributions:

```text
source edit / undo / redo / dependency change
  -> affected semantic units
  -> replace or remove their contributions
  -> update scoped ordered indexes
  -> publish affected generated views
  -> optionally persist completed results in .ac
```

Directory rendering subscribes to this state. Editing a heading updates its
entry; moving or deleting it updates order/membership. Numbering changes propagate
according to their actual sequential dependencies. Preserve math and formatting
in titles, custom `toc-override` behavior, scoped channel names, and transclusion
inclusion rules. A plain-text heading scanner is not a replacement for semantics.

Semantic work needed for a requested directory can cover offscreen source, but
must not require full offscreen physical layout. Missing contributions must not
be silently presented as a complete current directory. Track completeness and
revision explicitly; publish coherent generations and cancel superseded work.

Generated view updates do not mutate source, move the cursor, create undo steps,
or trigger source saves. Preserve native sidebar folding as view state. Reflow
uses logical navigation such as `Go`; paginated display and PDF use their own
layout's page map, never a previously persisted screen/PDF page number.

Materials dependencies include citation membership/order, locator changes,
record revisions, Namespace-inherited selections, CSL style, and locale. Preserve
explicit UUID selections and locators as source; generated citation strings and
bibliographies belong to derived state. Some CSL changes affect many citations;
that scope must be represented rather than falsely treated as independent edits.

## 11. Export Completion

Replace calls to Update All and arbitrary idle delays with an internal completion
request for a fixed source snapshot and output profile. Export computes needed
directory, reference, material, and page results for that snapshot, without
modifying the open source buffer.

Page-numbered directories can change pagination themselves. Iterate dependent
results until stable, detect repeated/nonconverging states, and report a useful
bounded failure rather than exporting a supposedly current stale directory.
Do not expose a user setting for how many times to press or repeat Update.

Keep caches for different page sizes, widths, fonts, and export ranges separate.
AVD cache validation also includes definition/order and constituent source
revisions. Scope/order changes invalidate assembly results while unaffected
source computations may remain reusable. Unsaved or virtual documents without
a durable source location use memory caches until an owned disk location exists.

## 12. Realtime Filesystem Maintenance

- Prioritize visible cache reads, nearby reads, coalesced writes, then idle cleanup.
- Use source events and bounded reconciliation scans, not a vault walk per edit.
  Existing `source_watch` can supply notifications, but cache-directory events
  must not repeatedly invalidate normal source inventories.
- On confirmed deletion, invalidate the cache owner token, cancel queued work,
  release database users, and delete the managed cache and its owned sidecars.
- Distinguish atomic source replacement and rename from deletion. A missing or
  unmounted vault root is not proof that all its documents were deleted.
- On rename/move, migrate a matching cache if convenient; otherwise discard and
  rebuild. Coordinate active owners and never overwrite an unrelated cache.
- Multiple instances share the transaction protocol but do not wait for cache
  locks during editing. Validate units by input keys, not last-writer-wins state.
  Namespace generation tokens by session; process-local counters are insufficient.
- Evict obsolete variants and orphan records under byte/usage budgets. Avoid
  foreground VACUUM or compaction. Cleanup must not delete unrelated user files
  merely because they are located in a directory named `.cache`.
- Exclude managed cache directories from search, UUID/artifact/RAG inventories,
  website inputs, backup dispatch, and Hodarium. Include temporary database files
  in those exclusions. Audit the current inventory exclusions when implementing
  this cache; the original October 20 audit found `.cache` was not excluded.
- Cache failures are nonfatal and rate-limited in diagnostics. Readonly directories
  use memory-only operation. Source saving and shutdown never join cache flushes.

## 13. Implementation Sequence

Each phase needs focused tests and an appropriate commit. These are required
pieces of the complete design, not a proposal to stop after the first cache layer.

1. Establish baselines and exact derived/source field ownership. Add regressions
   for the remaining auxiliary metadata, selector, and selective-update defects.
2. Introduce generation-aware contribution storage and derived directory views.
   Remove generated-body edits from normal directory updates.
3. Establish immutable typesetting result blocks and owner-local resource binding;
   prove publication and reclamation do not traverse large graphs on the editor.
4. Implement per-document cache ownership, SQLite records, binary codecs, bounded
   asynchronous reads/writes, and dependency validation for actual core layouts.
5. Connect incremental semantic, paragraph, math, table, reference, and external
   resource invalidation. Add AVD assembly and output-profile dependencies.
6. Replace export/maintenance Update calls with completion requests. Remove source
   rewrite jobs whose only purpose is refreshing generated directories.
7. Switch source persistence/history/sync to the authoritative projection; remove
   auxiliary persistence and obsolete generated-body storage under explicit rules.
   Validate on isolated document copies before any separately authorized Notes work.
8. Remove normal Update/Refresh Auxiliary/Save Auxiliary UI, command registrations,
   repeat-count preferences, and obsolete glue only after their consumers move.
   Preserve diagnostic cache rebuild and genuine user-requested external actions.
9. Finish filesystem lifecycle, exclusions, budgets, and end-to-end performance
   validation. Do not claim completion from successful serialization alone.

Native command binding changes must follow
`src/App/Commands/Interfaces/README.md`: edit the XML declarations and
generate the C++ adapters, never hand-write per-command wrapper glue.

## 14. Acceptance Criteria

### Correctness

- Without `.ac`, with stale `.ac`, and with matching `.ac`, source semantics and
  rendered output agree, including caret mapping, structured math, and links.
- Header/math-caption edits immediately update directory views before saving;
  move/delete/undo/redo and custom scopes behave correctly.
- Reflow directory navigation and PDF page numbers are independently correct.
- Auxiliary/cache corruption never pauses source saving or blocks history/sync.
- A late result cannot overwrite newer content, reference state, or layout.
- Source deletion cannot be followed by cache resurrection from pending writes.
- Atomic saves, moves, root unavailability, two instances, forced exit, missing
  fonts, resource changes, and readonly storage have explicit regression cases.
- Source-only edits, derived updates, and diagnostic rebuilds have the intended
  modified/undo/history behavior; derived updates alone create none of these.

### Performance

Compare cache-disabled, cold-cache, warm-cache, and actively-writing scenarios on
the same documents. Include long lecture notes, complex tables, mathematical
captions/headings, transclusions, and AVDs. Use isolated copies, not live Notes.

Measure first usable viewport time, complete layout time, input-to-frame median
and tail latency, BufferActor time attributable to caching, worker CPU/I/O,
retained memory, bytes written, hit rate, and recomputed-unit counts.

Trace assertions must show no cache I/O, lock waits, graph serialization, full
document hashing, or large snapshot copying on the interactive actor path.
Bound both cache queues and late-result materialization work. A normal local edit
must leave unrelated units reusable. Counter/table/pagination propagation must
stop when dependencies permit, not unconditionally invalidate the whole document.

Tune granularity, batching, budgets, and eviction from those measurements. No
numeric latency claim is established yet. Faster reopening does not justify worse
typing or resize latency; skip low-value cache production when necessary.

## 15. Current Completion State

The per-document persistent `.ac` cache and replacement of Document Update by
realtime derived directories are still planned, not delivered. The metadata fix
in `8ededcc97` is existing behavior, not the completion of this cache project.

Other subsystems already publish immutable results and use in-memory layout
caches; this plan does not claim those mechanisms are absent. Reuse their
current owner/domain contracts. DRD has been replaced by native NodeContracts,
and conversion is native; neither migration is a prerequisite still to do.

Acceptance checks should follow AGENTS.md: the cases above identify concrete
uncertainties, not a requirement to add a permanent suite for every item.
