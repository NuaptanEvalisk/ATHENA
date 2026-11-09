# Full Layout Performance and Persistent Shaping Cache

Status: **persistent shaping cache implemented and verified**; asynchronous
radioactive matching and several layout optimizations have landed. The
remaining full-layout bottlenecks still require targeted profiling and work.
Original investigation: 2026-11-05. Status updated: 2026-11-09.

## Current Implementation Status

The original measurements and design choices below are retained as historical
context, **not a description of the current build or its performance**.

| Workstream | Current status | Evidence / remaining scope |
| --- | --- | --- |
| Persistent Unicode/math shaping cache | **Completed and verified** | Native LMDB cache in `persistent_shaping_cache.cpp`, integrated at the pre-HarfBuzz lookup in `shaped_text.cpp`; cross-process persistence and geometry/caret checks in `shaped_text_test.cpp` |
| Shaping cache prewarm and maintenance | **Implemented** | `QTMCacheManager.cpp` provides Vault scanning, configurable sampling/workers, progress, stop/resume checkpoints and storage statistics; writer batches entries, hit refreshes and eviction |
| Radioactive link matching | **Implemented asynchronously** | `artifact_radioactive_links.cpp` and `concat_text.cpp` use immutable index/query snapshots and live decorations; follow-up end-to-end layout performance measurements remain distinct from functional correctness |
| Inline formula environment copying | **Partially improved; further optimization open** | `typeset_cached_math_child()` now records contract effects and reuses cached fragments but retains a copied source/sources and other context; continue profiling actual dependency/copy costs |
| Math font resolution / shared Unicode analysis | **Partially optimized; further profiling open** | Font-domain caches and reuse of paragraph analysis exist; the historical call counts do not establish present-day savings or prove all redundant analysis is gone |

### Shaping Cache: What Is Already Done

- The disposable **`native-shaping-v1` LMDB cache** lives under
  `$ATHENA_HOME_PATH/system/cache/` (or `~/.ATHENA/system/cache/`), outside
  Vault documents. The `body` and `math` databases use separate **700 MiB**
  and **256 MiB** LMDB map limits, respectively, including LMDB overhead.
- A shaping key includes ATHENA revision, physical font file identity and
  revision, face index/variation coordinates, size and DPI, source range and
  context, direction, script/language, features and math-specific options.
  Cache hits reconstruct native positioned glyphs, metrics, clusters and
  carets, retaining access to the physical glyph source. Large or unsupported
  runs fall back to normal shaping.
- Mapping starts in a background writer thread. An unavailable cache is a
  normal miss; the interactive path does not wait for writer locks or queue
  capacity. Hits use short LMDB read transactions and record access in a
  bounded atomic ring; writes, frequency decay and eviction are batched off
  the editor path. This is **not a second in-memory shaping-result cache**.
- **Workspace → Cache Manager** provides a Vault prewarm workflow: entire
  Vault or sampled percentage, configurable workers, optional lower CPU/I/O
  priority, progress and stop/resume. It uses isolated ATHENA worker processes,
  a file lock for coordination and an on-disk per-document checkpoint journal.
  Cache storage/entry counts can be inspected there. The old standalone
  `athena-shaping-cache-builder` executable was retired in favor of this UI.
- Verification is recorded in `tests/src/Rendering/Fonts/shaped_text_test.cpp`:
  direct HarfBuzz results are compared with persisted glyph advances,
  positions, clusters and caret coordinates; differing size/language keys are
  distinguished; a **second process** reads the same LMDB run. Related native
  shaping and rendering checks are in that test. These checks have been
  completed and verified; **no fresh benchmark or test run was performed for
  this note update**.
- Main implementation history: `2c68e3ca6` (asynchronous radioactive links
  and persistent shaping), `ec7339e5e` (resumable Cache Manager prewarm),
  `8b4e9dd0c` (storage statistics / Vault backup exclusions), and `babcab8da`
  (worker initialization and Vault context).

**Do not re-plan or reimplement the shaping cache as an unstarted project.**
Future shaping-cache work should be prompted by a concrete performance or
correctness regression, not by the historical checklist below.

## Objective

Eliminate redundant computation and full environment copies during document
layout. Progressive rendering is not a substitute for making full layout fast.
Keep cache preparation, persistence, maintenance and radioactive matching out
of the editor's critical path. Implement natively in C++.

## Original Dynamic Findings (2026-11-05 Baseline)

Document: `Notes Root/Sources/Reading/Algebraic Topology (Hatcher)/ATHA 03 Features of Homology Theory.ath`.

Measured an isolated copy with the real native loading/typesetting pipeline,
Qt offscreen, and temporary nested C++ scope timers. Original content retained:
no ToC removal, disabled radioactive links or progressive-threshold padding in
the detailed measurements. Times are wall time, not CPU time. Self times exclude
instrumented children, but include uninstrumented descendants. Recursive
inclusive times must not be added together.

One **pre-implementation** complete typeset pass took **3549.727 ms**:

| Disjoint measured group | Time |
| --- | ---: |
| Radioactive queries, changed-head unions, defining-occurrence checks | 751.353 ms |
| Inline-math environment checkpoint/materialization expression | 465.178 ms |
| Unicode paragraph setup and grapheme iterator construction | 540.536 ms |
| Math font source resolution, font selection, shaping dispatch and shaping | 700.104 ms |
| Remaining bridge, concat, boxes, macro execution and other work | 1092.556 ms |

The root layout call itself took 3548.026 ms; typeset_prepare took 0.606 ms.
These numbers describe actual full layout, not time waiting for the first frame.
Several passes reproduced costs around 3.3-3.6 seconds.

### Radioactive Matching

`src/Domains/Artifacts/artifact_radioactive_links.cpp`:

- `changed_heads()` ran 1427 times, costing 622.528 ms.
- The sum of input set cardinalities merged was 2,546,733, not unique names.
- 1211 queries were uninitialized. `update_query()` nevertheless computed
  historical change heads before matching. Fresh queries necessarily match
  every token and do not need this incremental-change calculation.

### Inline Formula Cache

`src/Typesetting/Concat/concat_text.cpp`, `typeset_cached_math_child()`:

```cpp
prepared.variables = evaluation_environment_checkpoint (*env.operator-> ()).variables ();
```

This is the **historical expression from the original profile**, not code
still present in today's `typeset_cached_math_child()` implementation. At the
time, the checkpoint constructor in `src/Typesetting/Env/local_environment.cpp`
copied the entire environment hashmap; `variables()` copied it again.

- 475 cache preparations.
- Resulting maps contained 1,106,937 entries in total.
- The two explicit copies therefore copied over 2.2 million hashmap entries.
- The expression, including destruction of its temporary, cost 465.178 ms.
- Later full passes prepared the same 475 fragments again.

### Unicode Setup

`src/Core/Text/unicode_text.cpp`:

- 48,899 grapheme_cursor constructions: 295.376 ms.
- 12,310 unicode_paragraph constructions: 245.160 ms excluding nested
  grapheme constructors, or 329.494 ms inclusive.
- Their disjoint sum is 540.536 ms.

grapheme_cursor validates UTF-8, creates UText, opens an ICU character-break
iterator and binds the text. It identifies editing-safe grapheme boundaries.

unicode_paragraph performs UTF-8/UTF-16 conversion and index mapping, bidi
analysis, line-break analysis, script segmentation and its own grapheme setup.
The name does not mean there are 12,310 document paragraphs: short text and
math fragments also instantiate this object.

Independent grapheme analysis is created in the math-text constructor,
unicode_paragraph, font selection, line shaping and build_carets(). Relevant
files include `concat_text.cpp`, `font_selection.cpp`, `shaped_text.cpp` and
`unicode_text.cpp`. A pipeline can repeatedly analyze the same source text.

### Font Work

- math_font_source: 12,487 calls, 277.712 ms.
- Physical-source shape_freetype_utf8: 11,364 calls, 314.284 ms excluding
  instrumented descendants, 381.358 ms inclusive.
- Font requests, metrics and stretch paths repeatedly resolve physical math
  sources. A document normally uses only a small set of font configurations.
- Actual shaping is necessary work; not all measured font time is removable.

### Exclusions and Limits

- Macro execution self time was 56.329 ms, not multiple seconds.
- Local environment start/update/end and contract/occurrence commits together
  took about 13 ms.
- Full passes reported page_refs=0, missing=0, redefined=0. They were not
  repeated by the typeset loop to converge ToC page numbers. The external
  invalidation causing each pass was not traced.
- The separate approximately four-second buffer-loading interval is not
  explained by these full-layout timers.
- Earlier experiments found that reaching 256 direct body children enabled
  progressive layout and improved first-frame latency. The original has 215.
  This only demonstrates deferred work; it is not the root performance fix.

Small diagnostic artifacts, including logs and instrumentation patch/header:
`/tmp/athena-atha03-profile-YsQujk/`. These are temporary and may disappear.
Detailed report there: `FULL-LAYOUT-FINDINGS.md`.
Instrumented build ID: `6a5bb518d8d73498d74342a0d691477c715c4b07`.
Temporary probes were removed, the uninstrumented application was rebuilt and
deployed, and large profiling copies were deleted. No permanent tests added.

## Layout Optimization Workstreams: Implemented vs. Remaining

### 1. Asynchronous Radioactive Links

**Implemented:** asynchronous matching and live decorations have landed.
The following are design and correctness criteria for ongoing maintenance,
not an instruction to begin the implementation again.

- Initial layout may show ordinary text without radioactive highlighting.
- It consumes already prepared matching results without waiting for matching.
- Background workers match immutable content and index snapshots. Fresh
  queries do not calculate historical changed-head sets.
- Later index changes are processed in the background. Share change-head
  unions for the same immutable index/revision interval instead of merging
  them separately for every text query.
- Publish results to the owning document actor with content-version checks.
- Update affected highlighting/link hit regions without changing text metrics
  or triggering full-document layout merely to add decorations.
- Preserve negative-match tracking so initially unmatched text can become a
  link after index updates.

### 2. Inline Formula Cache Without Full Environment Copies

**Partially implemented:** native fragment caching and contract-effect replay
exist. Full elimination of unnecessary materialization/copies and current
full-layout cost attribution remain open; the original 465 ms measurement is
historical and must not be presented as a current benchmark.

- Remove the temporary checkpoint-to-map double copy.
- Final design should retain shared immutable environment state with delta
  overrides and/or actual dependency receipts from existing contract reads.
- Each formula must not own a full environment hashmap.
- Check relevant dependencies for cache admission; unrelated environment
  changes should not invalidate otherwise reusable formula output.
- Reuse existing contract infrastructure, not a parallel dependency system.
- Preserve reference-effect replay, source occurrences, macro context and
  necessary copies of mutable line_item/space records.
- Reducing two copies to one is an intermediate improvement, not the final
  solution. Disabling formula caching is not the intended solution.

### 3. Resolve Math Font Configurations Once

**Partially optimized:** font-domain-local font resolution and shaping-resource
reuse exist. Reprofile before deciding whether remaining repeated resolution
justifies more cache structures or invalidation complexity.

- Cache physical font resolution by effective font configuration, math role,
  weight/variant and font database generation in the existing font domain.
- A document is not restricted to one physical face: local overrides and
  normal/bold/bb/cal/scr/frak roles remain supported.
- Separate physical source selection from size/DPI-dependent metrics.
- Reuse the few actual configurations instead of resolving them per symbol,
  fragment, layout-metrics request or stretch request.

### 4. Share Unicode Analysis

**Partially optimized:** downstream text shaping can reuse paragraph analysis,
while the persistent shaping cache avoids repeated setup on eligible hits.
Cross-layer analysis sharing and the remaining number of ICU constructions
must be measured on a current build before proposing further changes.

- Analyze unchanged text once for the same direction/language/context.
- Retain immutable grapheme boundaries, byte/UTF-16 mapping, break candidates
  and bidi/script information; downstream stages consume range views.
- Do not create fresh ICU analysis objects at each processing layer.
- Reuse cluster/caret results from cached shaping when appropriate.
- Keep correct Unicode handling; do not replace it with ASCII heuristics.
- Word-level cached analysis cannot substitute for paragraph-level bidi and
  line-breaking context. Math-alphabet projection may also require distinct
  analysis because its Unicode source differs.

## Persistent Shaping Cache: Original Design (Implemented)

The subsections below record the decisions that guided the completed
implementation. For the actual current behavior, refer first to **Shaping
Cache: What Is Already Done** and the referenced source files above.

### Scope

Global, local-machine, disposable cache shared across documents and sessions.
Not stored in the vault, not synchronized, and not duplicated into every .ac.
No cross-ATHENA-version stability promise.

Cache actual mathematical characters/variants and short runs, plus reusable
body-text words or runs such as "obvious" and "however". Do not mechanically
split at spaces: use units that can be shaped independently, otherwise retain
the required larger run/context. Preserve ligatures, kerning and combining
marks. Cache lookup must happen before the expensive setup it replaces.

**First version excludes large-operator size variants and delimiter stretching.**
**No hardcoded list of common letters, digits or symbols.**

### Storage Choice: LMDB

Use the existing LMDB dependency, with versioned compact binary values.
The workload is keyed lookup, not SQL joins. LMDB provides mmap, concurrent
reads, serialized writes and reuse of freed pages. SQLite WAL adds SQL and
checkpoint management without a clear benefit here. A custom mmap store would
unnecessarily reimplement indexing, replacement and free-space management.

Keys represent the complete effective shaping request: actual font identity
and face/variation axes, source text and required context, size/DPI, direction,
language/script, features, math variant/script level, and other output-affecting
options. Do not identify a font only by its display name "Pagella".

Values contain offsets and compact arrays for glyph IDs, advances/offsets,
ink bounds, clusters, carets and applicable mathematical metrics. Do not dump
process pointers or reconstruct the entire database as C++ object graphs.
Preserve access to physical font resources needed to render cached glyph IDs.

### Mapping Readiness: Implemented User Decision

**Do not introduce a second in-memory shaping cache tier.**

- Prepare/open/map the persistent cache in the background.
- Until ready, the editor computes normally without waiting.
- Once ready, use the mapped database directly for lookups.
- On a miss, compute normally and queue asynchronous persistence.
- Mapping readiness does not imply every page is resident: subsequent page
  faults can still involve I/O. Do not claim strict zero-blocking disk reads.
- Keep read transactions short; returned LMDB pointers cannot outlive their
  transaction. Do not pin an old transaction for the lifetime of layout boxes.
  Copy only the needed result into its normal layout-owned representation
  where required, not into a second cache tier.

### Bounded Text Cache and Refresh

Default maximum **700 MiB for the body-text cache**, including database/index
overhead, not just glyph payload. Keep its accounting separate from math data.

- Use decaying access frequency plus recency; infrequently used entries are
  replaced first, and old popularity does not pin entries forever.
- Record hits cheaply in memory and batch/coalesce refreshes in the background.
  These access counters are maintenance metadata, not a second shaping cache.
- No transaction, flush or eviction work per foreground hit.
- A single background writer batches insertions, access refreshes and eviction.
- Reserve headroom for copy-on-write transactions; do not wait until the map
  is completely full to evict. Avoid long-lived readers preventing page reuse.
- LMDB provides storage, not eviction policy; ATHENA implements the policy.
- Cache maintenance must not make the editor wait for a writer or fsync.

### Cache Builder: Implemented in ADS Cache Manager

The native builder now lives in **Workspace → Cache Manager**, not a separate
shaping-cache builder executable. It scans a chosen Vault, either all documents
or a selected percentage, with configurable parallelism and visible progress.

- Derive the corpus from documents, never a hardcoded alphabet/symbol list.
- Reuse actual font resolution and shaping semantics, including structured
  math variants, local styles, script levels and necessary expansion context.
  Merely extracting XML text would produce incomplete or incorrect requests.
- Workers have independent owning font contexts; no sharing thread-affine
  FreeType/ICU/font-domain objects between workers.
- Deduplicate requests/results and batch through the background writer.
- Corpus frequency seeds entry popularity; ongoing real use refreshes it.
- Builder and runtime use the same cache key and result representation.
- Missing caches never block editor startup; normal computation remains valid.

## Remaining Priorities and Completion Criteria

1. **Done:** Move radioactive matching off the first layout, with cached
   revision/change unions and live decoration updates. Investigate regressions
   rather than restarting this workstream.
2. **Open:** Reprofile the inline-formula fragment path; eliminate remaining
   redundant environment materialization without losing contract/source effects.
3. **Open / partly optimized:** Measure remaining math font-resolution and
   Unicode-analysis duplication, then optimize only demonstrated hot paths.
4. **Done and verified:** Persistent shaping cache, asynchronous maintenance,
   bounded body/math storage and Vault prewarm builder. Maintain its existing
   correctness checks, but do not treat it as outstanding implementation work.

Measure the same real document's **complete layout**, not only first paint,
after meaningful chunks. Report call counts and time attribution. Do not
declare success merely because work was postponed or highlighting disabled.
Use targeted checks for uncertain ownership, transaction lifetime, cache-key
correctness and representation transitions, not broad test rituals.

References:
- https://github.com/LMDB/lmdb/blob/mdb.master/libraries/liblmdb/lmdb.h
- https://harfbuzz.github.io/adding-text-to-the-buffer.html
- https://harfbuzz.github.io/shaping-operations.html
