# Full Layout Performance and Persistent Shaping Cache

Status: **persistent shaping cache implemented and verified**; asynchronous
radioactive matching and several layout optimizations have landed. The
remaining full-layout bottlenecks still require targeted profiling and work.
Original investigation: 2026-11-05. Status updated: 2026-11-10.

## Current Implementation Status

The original measurements and design choices below are retained as historical
context, **not a description of the current build or its performance**.

| Workstream | Current status | Evidence / remaining scope |
| --- | --- | --- |
| Persistent Unicode/math shaping cache | **Completed and verified** | Native LMDB cache in `persistent_shaping_cache.cpp`, integrated at the pre-HarfBuzz lookup in `shaped_text.cpp`; cross-process persistence and geometry/caret checks in `shaped_text_test.cpp` |
| Shaping cache prewarm and maintenance | **Implemented** | `QTMCacheManager.cpp` provides Vault scanning, configurable sampling/workers, progress, stop/resume checkpoints and storage statistics; writer batches entries, hit refreshes and eviction |
| Radioactive link matching | **Implemented asynchronously** | `artifact_radioactive_links.cpp` and `concat_text.cpp` use immutable index/query snapshots and live decorations; follow-up end-to-end layout performance measurements remain distinct from functional correctness |
| Inline formula environment copying | **Partially improved; further optimization open** | `typeset_cached_math_child()` now records contract effects and reuses cached fragments but retains a copied source/sources and other context; continue profiling actual dependency/copy costs |
| Math font resolution / shared Unicode analysis | **Measured repeated work reduced** | Profile-to-face mappings now reuse font-domain entries; alphabet projection no longer constructs a full paragraph; line shaping borrows paragraph graphemes and ICU rules are cloned. See the November 10 follow-up measurements below |

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

## Pre-Fix Dynamic Diagnosis (2026-11-10)

Binary build ID: `0ef40c636675afc4edd449286b0a7d69a57d2914`.
Real ATHA 03 document, 212 direct body children, Xvfb/XCB. The profiling home
retained a copy of the user's shaping caches and preferences; the timing run
reused the first run's cache. No product code or layout threshold was changed.

| Measurement | perf capture | Unprofiled timing |
| --- | ---: | ---: |
| Buffer load callback | 4018 ms | 3361 ms |
| First target-document typeset | 2469 ms | 1823 ms |
| Next typeset before first paint | 1953 ms | 1892 ms |
| Following typeset | 411 ms | 450 ms |
| Open-to-first-paint reported by harness | 8777 ms | 7474 ms |

The harness's open-to-paint column includes vault activation. The native
`typeset` benchmark is per pass, not an assertion of total startup time. This
document is below the 256-child progressive-layout threshold. **The 500 ms
full-layout requirement is not met.**

### Measurement Method

Temporary nested C++ scope timers measured wall time on the layout thread.
Self time subtracts all instrumented children, including recursive calls.
Inclusive recursive times must not be summed. GDB established causal call
chains separately; GDB run durations are not benchmark results.

The final instrumented build was
`3f1928e1c791a32b215dd9616508606b5ba1cc93`.
In `causes/timing-run-1`, the real document's first full layout was
**2190.960 ms**, the repeated layout **1706.386 ms**, and a later partial
update **380.807 ms**. The last value does not satisfy the first-layout goal.
A second process using the same copied cache files still needed
**1748.972 ms** for the first full layout, with **zero major faults and zero
block input** during that layout. The problem is not just cold storage.

### 1. Synchronous Identity Planning Before Layout

Before the first target layout, thread 159430 called
`analysis_service::run` **616 times**, totaling **1325.933 ms**.
This time is outside the 2191 ms layout, not a component to subtract from it.

GDB at calls 100 and 400 established:

```text
buffer_actor::prepare_replacement_node_identities
  assign_detached_source_ids
    identity_planner::visit
      roles_for
        prepare_source_positions callback
          edit_env_rep::analyze_contracts
            macro_analysis::synchronize
              analysis_service::run
```

The source of repeated work is `Document/State/new_style.cpp:448`.
`roles_for` requests a source-position context for non-native macros with
children. The callback copies reference/attachment maps, restores a fresh
evaluator, replays the path inside the paragraph, and synchronously analyzes
contracts for each requested position. It already retains the preceding
root-paragraph entry; claiming that it replays the entire document prefix
every time would be incorrect. The remaining repeated per-position preparation
and inference are real. The initial style analysis is also synchronous.

This is UUID/source-identity planning, not ToC generation or HarfBuzz.
Correct optimization must preserve source-position scopes; blindly assigning
every macro the global contract or skipping identity validation is not valid.
Reuse prepared environments and inference by actual contract/input changes,
and avoid restarting inference when only the source occurrence changed.

### 2. Scrollbar Geometry Forces a Second Full Layout

The initial layout uses a viewport of **800 x 501 px**. Publishing the
resulting extent **800 x 30550 px** calls
`QTMScrollView::updateScrollBars`. It reserves a vertical scrollbar and
also gives the horizontal scrollbar a positive range because the document
still has the old width. The style metric reports 14 px; the actual viewport
then becomes **784 x 485 px**.

The actor receives `handle_notify_resize`, then
`edit_interface_rep::apply_changes` sees width **204800 -> 200704** in
internal units. It changes `PAGE_SCREEN_WIDTH/HEIGHT`, marks
`THE_ENVIRONMENT`, calls `typeset_invalidate_all`, and lays out the whole
document again through `render_view -> apply_changes`.
After that result, the horizontal range returns to zero.

This is an established feedback loop between initial layout width and
scrollbar geometry. It is not a request to implement progressive layout.
Resolve a stable viewport width before the expensive first pass.
A GDB breakpoint on `publish_derived_directories` did not fire in the earlier
duplicate-pass trace, so that notification is not the demonstrated trigger.

### 3. The Shaping Cache Works, but Its Lookup Is Late

The first full layout accepted **10630 / 10671** persisted shaping results
(99.6%). It still performed the following work:

| Disjoint first-layout budget | Self time |
| --- | ---: |
| Unicode analysis plus grapheme construction | 349.564 ms |
| Math/font profile resolution and face selection | 274.109 ms |
| Persistent cache lookup and key construction | 471.402 ms |
| Remaining shaping dispatch | 64.403 ms |
| Font catalog selection, excluding measured child work | 54.898 ms |
| Line shaping coordination, excluding measured child work | 43.881 ms |
| Concat node dispatch, boxes and uninstrumented descendants | 589.505 ms |
| Bridge work outside the measured subroutines | 136.912 ms |
| Paragraph formatting outside the measured subroutines | 91.989 ms |
| Environment execution outside the measured subroutines | 43.523 ms |
| Preparation, occurrence tracking, commits and remainder | 70.774 ms |
| **Total** | **2190.960 ms** |

Specific repeated computations:

- `math_font_source`: **13346 calls**. Most of its time is
  `profile_face_family`, which repeatedly tokenizes the same comma-separated
  font profile and role/weight conditions: **249.318 ms inclusive** across
  13362 calls. Actual `select_math_face` takes only **9.111 ms** and already
  has a font-domain cache. Cache/resolve the profile-to-role mapping as well,
  respecting font-catalog generation and alphabet variants.
- **12642 Unicode paragraph constructions** and **31505 grapheme iterator
  constructions**. Each paragraph eagerly performs UTF-16 conversion, bidi,
  line breaking, script analysis and grapheme setup. Even a single named
  mathematical symbol goes through `font_paragraph`.
- Math font selection projects characters to the mathematical alphabet and
  recursively calls `font_catalog::select(..., nullptr)`, which constructs
  another complete `unicode_paragraph` for the projection. That second pass
  needs glyph-coverage/script information, not a second editable paragraph
  with bidi, line-breaking and caret state.
- `shape_line` constructs another `grapheme_cursor boundaries(source)`
  despite already receiving a `unicode_paragraph` with its own iterator.
  Math string setup also makes a separate cursor before creating its
  `font_paragraph`. Share owner-local analysis and reuse ICU iterators;
  do not replace Unicode semantics with an ASCII heuristic.

All of those paths run before the low-level shaping-cache hit can return.
Adding more cached glyphs alone cannot remove them.

### 4. Cold mmap Reads Still Block the Layout Thread

The cold first pass's cache lookup took **410.585 ms**, of which
`db.entries.get / mdb_get` took **396.675 ms** and decode only **4.849 ms**.
Layout recorded **183 major faults** and **1041128 input blocks**.
The next pass's lookup took **42.017 ms**; its `mdb_get` was **29.516 ms**.

The cache mapping is published asynchronously, but publication of an mmap
does not mean its pages are resident. An editor-thread LMDB read can still
fault on disk. The copied profiling files make this cold cost visible; do
not present the cold number as an unavoidable cost of every opening.
The warm second-process first layout above rules out I/O as the whole cause.

Any readiness/prewarming change must distinguish mapping availability from
resident-page readiness. Do not introduce the rejected second shaping cache,
or block the editor waiting for the cache.

### 5. Remaining Typesetting Work Is Not One Hidden Copy

The 589.505 ms concat residual was also classified by node:
atomic text **128.325 ms**, scripts **124.973 ms**, images **104.347 ms**,
WITH **62.140 ms**, CONCAT **38.862 ms**, with smaller contributions from
math blocks, tables, delimiters and loci. These are disjoint residuals after
the timed children above, not costs to add again to the table.

The script path `typeset_v3_scripts` typesets all four slots, including
vacant ones, and assembles additional layouts for empty-slot cursor positions.
The font metrics path `open_type_math_metrics` reads the full MATH constant
set on each request. Those are concrete follow-up optimization surfaces, but
this measurement does not assign the whole script residual to either one.
Likewise, the image residual is not proof that image decoding alone costs
104 ms.

The old inline-formula copying hypothesis is not the present bottleneck:
576 fragment-preparation copies take **2.100 ms** total. Checkpoint map copying
and variable materialization are below 1 ms in this layout. Auxiliary capture
commits total **0.412 ms**. Removing ToC or its cache is not justified by these
measurements.

### Evidence and Outcome

`build_qt6/atha03-profile.fZmgdp/` retains timing/perf logs, GDB traces and
the temporary instrumentation patch/header. `causes/` contains cold and warm
process measurements; `contracts-trace/` contains the identity-planning
backtraces. Copied profiles/caches are removed after the runs.
Temporary product instrumentation is removed and the normal binary rebuilt
and deployed. No optimization or sub-500-ms result is claimed here.

The practical optimization order is: eliminate per-position repeated contract
inference during loading; stabilize initial viewport geometry; resolve font
profiles once per actual context; share/reuse Unicode analysis before cache
lookup; then reduce measured script/box construction costs. Measure full
first layout again after those changes, not the later partial pass.

## Four-Fix Follow-Up (2026-11-10)

Built with `cmake --build build_qt6 --target ATHENA.bin -j16` and deployed
with the local executable helper. Build ID:
`d8a52bbd86a1ef370af16d920c2f219dc79c16bb`.
The existing CMake configuration is Debug with the project's compiler options;
these are same-configuration comparisons, not release-build claims.

The four requested changes are:

1. Source-identity planning admits an existing inferred contract against the
   exact occurrence's current inputs and transitive receipts. It only calls
   inference again when that contract cannot be admitted. Source scopes,
   paragraph-prefix preparation, and full environment checkpoint callers are
   preserved; this is not a global macro-name-only cache.
2. Ordinary editor canvases reserve their vertical scrollbar gutter before
   layout. Scroll ranges start from Qt's maximum viewport size instead of
   reconstructing it from the current viewport and inconsistent scrollbar
   metrics. Fullscreen still hides scrollbars; specialized external-camera
   canvases retain their own policies.
3. Profile-to-face selection is cached in the owning font domain by profile,
   variant, series and shape, invalidated by font-database generation. Physical
   face selection and size-dependent metrics keep their existing ownership.
4. Mathematical-alphabet projection performs script analysis and grapheme
   segmentation without constructing another full bidi/line-break paragraph.
   Line shaping reuses its paragraph's grapheme cursor. ICU character/line
   iterators clone per-thread empty compiled-rule prototypes; active text and
   cursor state are never shared between owners.

### Remeasurement

Same real document, Xvfb/XCB, unchanged full-layout policy. The first run uses
a fresh copy of the populated shaping cache; subsequent processes reuse that
copy. No machine-wide page-cache eviction was performed.

| Measurement | Previous uninstrumented run | New run 1 | New run 2 | New run 3 |
| --- | ---: | ---: | ---: | ---: |
| Buffer load callback | 3361 ms | 2527 ms | 2344 ms | 2395 ms |
| First full target layout | 1823 ms | 1668 ms | 1145 ms | 1216 ms |
| Next layout before first paint | 1892 ms | 109 ms | 98 ms | 101 ms |
| Open-to-first-paint, including vault activation | 7474 ms | 4577 ms | 3991 ms | 4093 ms |
| Later layout/update | 450 ms | 454 ms | 1194 ms | 1025 ms |

Run 1 overlapped the small `unicode_text_test` target build. Runs 2 and 3 did
not overlap builds; use those for the warmed comparison. Their full layouts
are about 33-37% faster than the prior uninstrumented first pass, and the
pre-paint second full pass is gone. **The <=500 ms full-layout target remains
unmet.** The late update is recorded explicitly rather than hidden by the
first-paint metric.

Separate GDB tracing (not used as timing evidence) established:

- In the fresh-profile process, all `analysis_service::run` calls totalled
  **28**, versus **616 in the old target-loading phase alone**. In the
  restored-profile trace, startup accounted for 5 calls and target loading
  added 28. Repeated per-occurrence inference has been removed, but source
  environment restoration and identity planning still cost time.
- At the end of the target's first layout, actual profile resolution added
  **13 calls** (counter 10 -> 23), versus the old **13362**. Later updates
  added 5 configurations. A function-name breakpoint initially missed the
  compiler-inlined resolver; the validated count uses its executed source
  instruction, not that false zero.
- After the target's initial layout, no further `typeset_invalidate_all`
  occurred in the trace. In particular, the old 800 -> 784 px scrollbar
  feedback did not recur. The later ~1 s update still exists without that
  whole-tree invalidation; its internal work requires separate attribution.

Only the existing `unicode_text_test` was built and run for the concrete ICU
ownership/segmentation uncertainty; it passed UTF-8, grapheme, bidi and script
checks. No broad test suite was run. `git diff --check` passed. No temporary
timing probes remain in product code.

Evidence is under `build_qt6/atha03-profile.fZmgdp/optimized*`: per-run native
logs, timing TSVs and GDB traces. Copied profiling homes/caches are removed
after measurement; small logs and debugger scripts are retained. The remaining
budget includes normal concat/box/script work and cache lookup, not just the
four redundancies fixed here. Cold mmap reads are unchanged by this patch.

## SVG and Math Font Follow-Up (2026-11-10)

Further scoped timing used the same real document and full-layout policy.
The probes were removed before the final build and measurements. The deployed
application build ID is `07f8c613807c468e5fb87dcebb1cb190829adc70`, built with
`-j16` and installed by the verified local executable helper. The existing
configuration uses optimized ICX/ThinLTO options despite its Debug CMake name.

### Confirmed Costs and Changes

- A transcluded path-only SVG,
  `Sources/Reading/Internet/assets/figure-856189ba-00e0-4bc6-9aa9-819c7d1218b2.svg`,
  spent **98.7 ms** in its first image-size query. `svg_image_size` created a
  fresh resvg options object and loaded system fonts, although this drawing has
  no text. Size queries and rendering now share font-domain-owned options;
  self-contained non-text drawings skip font loading. Qt's XML reader detects
  text and nested resources; resvg still owns actual SVG interpretation and
  sizing. Drawings with `text`, `image` or `use`, or unreadable XML, retain the
  font-enabled path. The measured SVG query became **2.3 ms**. Depending on
  transclusion readiness this SVG appeared in the initial or later update,
  so its saving must not always be attributed to the initial pass.
- Roughly **13,000 mathematical font-source requests** repeatedly rebuilt
  physical configurations. Resolution now caches the effective font resource
  name plus mathematical alphabet in the owning font domain, with catalog
  generation invalidation. Scoped time fell from **32.5 to 6.6 ms**. Local font
  overrides, sizes and mathematical alphabets remain distinct.
- About **5,000 OpenType MATH constant queries** repeatedly read the same
  font-wide constants. The existing immutable shaping-font resource now owns
  its metrics snapshot. Scoped time fell from **12.3 to 9.8 ms**; this is a
  small saving because selecting the existing font resource still costs time.
  Glyph-specific metrics and stretch variants are unchanged. This is not a
  second in-memory tier of shaped-text results.

### Uninstrumented Results

| Measurement | Run 1 | Run 2 | Run 3 |
| --- | ---: | ---: | ---: |
| Buffer load callback | 2457 ms | 2241 ms | 2418 ms |
| First full target layout | 1593 ms | 1024 ms | 1144 ms |
| Next layout before first paint | 123 ms | 99 ms | 106 ms |
| Open-to-first-paint, including vault activation | 4643 ms | 3706 ms | 4080 ms |
| Later layout/update | 932 ms | 1036 ms | 983 ms |

No build ran alongside these measurements. Run 1 starts from a freshly copied,
populated application cache; runs 2 and 3 reuse it in new processes. No OS
page-cache eviction was performed. The warmed first-layout range improves from
**1145-1216 ms to 1024-1144 ms**, but the first run remains much slower and
**the 500 ms requirement is still unmet**. Background transclusion readiness
and shared-machine scheduling also vary, so these are observed ranges, not a
controlled percentage speedup or a cold-start guarantee.

Remaining scoped costs include about 79 ms of contract variable reads,
51 ms of shaping-cache key construction, 62 ms of cache lookup, and 117 ms
inclusive paragraph formatting. Script assembly runs roughly four times per
scripts node because empty editing slots also need cursor/source geometry;
removing these boxes blindly would repeat earlier empty-subscript cursor bugs.
Inclusive scopes overlap and must not be summed. The later near-second update
still needs its own causal trace; no progressive layout was introduced.

Evidence is in `build_qt6/atha03-next/`: scoped logs and the final native timing
logs/TSVs. Copied profiling homes/caches are removed after measurement. No new
tests or broad test suites were added/run; verification here is the real-document
performance run, application build/deployment, and `git diff --check`.

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
2. **Measured, not the present bottleneck:** The November 10 inline-formula
   fragment preparation/copy budget is about 2 ms. Do not restart a large
   environment-copy rewrite based on the older November 5 diagnosis.
3. **Improved, goal still open:** The four-fix follow-up removed repeated
   contract inference, profile parsing and scrollbar-induced full layout,
   and reduced redundant Unicode setup. SVG and physical math-font reuse lower
   the warmed full layout further to 1.02-1.14 s; the first measured process
   still takes 1.59 s. Attribute the remaining layout and separate late update
   before selecting the next changes.
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
