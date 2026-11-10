# ATHA 03 Complete-Layout Performance Review

## Request

Review the actual first full layout of ATHENA's real document, identify the
remaining dominant repeated work, and propose a source-of-truth optimization.
The target is **less than 500 ms with a warm shaping cache**, not progressive
layout, first-viewport rendering, skipped work or a synthetic document.

This review follows repeated local changes without a stable aggregate gain.
Do not assume another small cache will fix it. First reconcile measured CPU
budgets and the active representation pipeline. Follow repository AGENTS.md;
no subagents, no system installations, no broad test/build rituals. Build the
application with `-j16`; after a successful build use the documented atomic
deployment helper. Do not operate on the user's live window/process.

## Location and Reproduction

Repository: `/home/felix/data/Software/TeXmacs/texmacs`

Document:
`/home/felix/data/Notes/Notes Root/Sources/Reading/Algebraic Topology (Hatcher)/ATHA 03 Features of Homology Theory.ath`

Use `tools/perf/profile-open-document.sh` under Xvfb, with a disposable profile
containing the normal preferences and shaping cache. Previous profile clones
were removed after use. For a new clone, exclude `system/hodarium/`,
`system/rag-openvino-cache/`, `system/hangs/`, `system/tmp/`, and `system/log/`;
they add tens of GiB unrelated to this measurement. Remove the clone afterward.

The harness opens the same vault and document via the native profiling path.
It does not modify the document. Example options:

```sh
xvfb-run -a tools/perf/profile-open-document.sh \
  --vault /home/felix/data/Notes \
  --document 'Notes Root/Sources/Reading/Algebraic Topology (Hatcher)/ATHA 03 Features of Homology Theory.ath' \
  --binary "$PWD/ATHENA/bin/ATHENA.bin" \
  --athena-home /path/to/disposable-profile \
  --backend timing --runs 3 --platform xcb --allow-concurrent \
  --output "$PWD/build_qt6/new-unique-measurement" --timeout-seconds 100
```

For CPU stacks use `--backend perf --call-graph lbr --frequency 1999`.
Local DWARF unwinding fails to recover application frames. Use
`perf script --no-inline`; perf report inline resolution is unreliable here.
Match the log's first large `Task 'typeset' took ... ms` to the same timestamp
interval and layout actor. Later full retypesets are separate measurements.
`summary.tsv` reports load/first-paint latency, not the complete-layout budget.

## Current Evidence

**Latest decision:** do not disable transparent huge pages based only on the
compaction samples. A clean default/disabled/disabled/default experiment on
the current binary measured warmed layouts of 705/721, 763/753, 742/752,
684/734 ms, respectively. Default mean 711 ms, disabled mean 752.5 ms.
Application/system allocator policies remain unchanged. After three local
attempts without stable aggregate gains, this is the AGENTS.md escalation;
reconcile the complete cost model before another local patch.

Evidence bundle: `build_qt6/atha03-layout-review-20261110.tar.gz`.
Source baseline: commit `efc537f72`; deployed ELF ID
`c41bdda87a75d6b58c95b475e63dd610cbb67069`. The bundle contains source,
the exact document, clean timing logs and sampled stacks, not executables or
the 1.4 GiB profile clones. The application uses an optimized Debug build:
ICX `-O3 -march=native -funroll-loops -fp-model fast -g -flto=thin`.
Do not assume it is an unoptimized `-O0` build or silently change the target
to a release-only benchmark. Relevant generated compiler flags are included.

Read `notes/full-layout-performance-and-shaping-cache-plan.md`, especially the
November 10 attribution tables and subsequent updates. Important facts:

- The former multi-second costs in contract inference, repeated profile loads,
  redundant font-source parsing and whole-body link scans were already fixed.
- Inline-formula cache preparation was measured at about 2 ms, not hundreds.
- An earlier warmed run measured roughly 114 ms shaping/resource attachment,
  51 ms Unicode paragraph construction, 43 ms variable reads, 40 ms exclusive
  tree copying, 71 ms exclusive paragraph formatting and 23 ms line breaking.
  Do not add inclusive nested timings to these exclusive values.
- The earlier Unicode/resource round's final build ID was
  `93f3f1eefa3ed529b9fd99833ed0b4de1b7b1d90`.
  Contemporaneous warmed control: 796 / 836 ms; final build: 847 / 806 ms.
  No document-level speedup has been established for this round.
- Retained changes: ICU-derived boundary arrays with reusable thread-local
  analysis iterators; typed logical-font lookup; immutable owners for HAMT key
  bytes. Unicode and snapshot ownership checks passed under ASan/UBSan.
- Rejected change: sharing the latest frozen variable payload among nested
  contract captures. It replaced deep copying with deep comparisons and map
  lookups without an aggregate gain. It is absent from the current source.

The warmed baseline LBR data is in `build_qt6/atha03-lbr-warm/`:
first full layout 778 ms, ending 18:50:31.098 on November 10, 2026.
Select **18:50:30.320-18:50:31.098**, actor **TID 268134**.
The busiest sampled thread is background maintenance, not the layout actor.
Of 674 actor samples, 128 include structured scripts, 85 environment writes,
79 font-paragraph construction, 43 variable reads and 41 shaping-cache lookup.
Those groups overlap. Seventeen lookup samples include mmap page faults.

The removed payload-sharing experiment has stacks in
`build_qt6/atha03-unicode-snapshots-perf/`: select
**19:07:32.712-19:07:33.522**, actor **TID 302037**. Its source is not the final
source; use it only to understand why that experiment was rejected.

## Specific Leads to Prove or Reject

### Scoped Follow-Up, November 10

The first two leads below have now been measured and are not the dominant
remaining gap. An instrumented warmed first layout took 812.105 ms wall and
804.971 ms actor CPU. Instrumentation overhead and machine load mean this is
not a before/after speed comparison. The disjoint self-time categories include:

| Category | Calls | Self time |
| --- | ---: | ---: |
| Bridge bookkeeping, excluding its body | 3,476 | 19.8 ms |
| Bridge body outside instrumented descendants | 3,476 | 111.8 ms |
| CONCAT outside instrumented descendants | 1,945 | 111.5 ms |
| Paragraph formatting outside instrumented descendants | 368 | 71.8 ms |
| Shaping cache lookup | 10,667 | 61.6 ms |
| Macro expansion outside instrumented descendants | 3,225 | 54.5 ms |
| Math paragraph setup excluding font selection | 5,335 | 41.9 ms |
| Variable reads excluding tree copying | 121,613 | 37.5 ms |
| Environment writes excluding nested measured work | 16,459 | 35.1 ms |
| Font selection | 12,379 | 33.5 ms |
| Tree copying, counting recursive children only once | 150,252 | 33.2 ms |
| Shaping excluding persistent lookup | 10,667 | 31.7 ms |
| Font updates excluding nested measured work | 3,784 | 25.6 ms |
| Length conversion excluding nested measured work | 14,735 | 24.4 ms |
| Source occurrence construction | 23,073 | 22.8 ms |
| Input revision recording | 15,159 | 21.7 ms |

These categories do not cover every call; the residual CONCAT and bridge-body
categories still contain other uninstrumented work. They are not evidence
that the wrapper loops themselves take 111 ms.

Empty script slots cost 33.2 ms inclusive; their empty math atoms cost 11.7 ms
inclusive. All input revision updates cost 21.7 ms, despite 10,313 updates
having a unique root. Neither can explain the remaining hundreds of ms.
All variable reads including their copying cost 63.9 ms, not 63.9 + 33.2 ms.
All length conversion including nested work cost 29.6 ms.

Probe source and the exact log are retained in the small archive
`build_qt6/atha03-scoped-probe-evidence.tar.gz`. Temporary probes were removed
from production sources. See `build_qt6/atha03-ledger-probe/timing-run-2/` for
the complete log. Earlier broad probes are in `build_qt6/atha03-broad-probe/`.

The numeric-format path is concrete: `as_tmlen` executes unit expressions,
serializes numeric TMLEN children, scales and serializes them again, and
`as_length`/`as_vspace` immediately parse the strings. LBR samples show libc
floating-point printf machinery inside this path. Preserve the existing
six-significant-digit representation when changing its formatting primitive;
do not silently change geometry precision while optimizing it.

The numeric formatter has now been changed to `std::to_chars`, retaining
`%g`'s six significant digits. A 1,200,007-value comparison found identical
output, including signed zero and nonfinite values. Clean A/B/B/A application
measurements gave warmed first full layouts of **805 / 807 ms (control)** and
**795 / 765 ms (new)**. That deployed build ID was:
`ec633b1dd46e9261feb5b9202ea79b3b71e3f532`. This is a modest improvement,
not the 500 ms goal. See the main performance note for complete run details.

The next measured change shares paragraph analysis/font selection for exact
input matches within one typesetting pass and font domain. A warmed probe saw
5,400 math preparations for just 256 distinct inputs; repeated math/text
preparation consumed 53.9 / 8.5 ms. No shaped results, source paths or caret
boxes are shared. Keys include font identity and font-database generation.
Clean A/B/B/A measurements: control 723 / 832 ms, new 687 / 675 ms (warmed
fresh processes). That deployed build:
`867cf86ce660071bbde6cfa6f0b153dc007bb9ef`. All probes were removed.

Current LBR evidence: `build_qt6/atha03-paragraph-perf/stacks.txt`, actor
405433, **20:02:31.664-20:02:32.415**. Of 800 samples, 77 are anonymous
transparent-huge-page allocation/compaction, not LMDB mmap reads. Investigate
that separately before changing allocator policy. Initial no-THP timing was
768 / 673 ms. Follow-up default/no-THP warmed times were 716 / 705 versus
697 / 691 ms; the application policy remains unchanged. The no-THP LBR run
(`build_qt6/atha03-no-thp-perf/stacks.txt`, actor 410437,
20:04:42.159-20:04:42.873) has zero huge-page-compaction samples, but still
14 persistent-lookup file-mapping fault samples. Keep these costs distinct.

That sample also identified repeated reversed-list allocation for cursor
boundary comparisons, mainly in `composite_box_rep::finalize`. The comparator
now uses Boost small vectors and preserves the final 0/1 border semantics;
1,893,225 temporary direct comparisons against the old implementation matched.
That deployed build: `144eae81758f004e5154d423679e3e941365aa41`.
Warmed complete layouts were 673 / 663 ms, a reverse control 682 / 643 ms,
then a new-build recheck 653 / 631 ms. These overlap; allocation elimination is
proven, but no stable aggregate speedup is claimed for this small change alone.
The goal remains unmet. Logs and detailed results are in the main note.

Current deployed build: `c41bdda87a75d6b58c95b475e63dd610cbb67069`.
Environment reads now combine value/presence lookup, and source capture avoids
reversed-list allocation and repeated anchor construction. Clean A/B/B/A warmed
times were 717/681, 746/720, 682/667, 680/709 ms respectively: no demonstrated
aggregate gain. Do not describe this as reaching or reliably nearing 500 ms.
The new LBR capture (`build_qt6/atha03-read-perf/stacks.txt`, actor 463516,
20:30:46.253-20:30:47.170) again finds substantial anonymous huge-page
allocation/compaction: 73 of 693 samples, distinct from shaping-cache faults.
The isolated policy comparison above rejected disabling THP as a speedup;
the default policy remains unchanged.

1. The real XML contains 563 MathScripts nodes and **1,685 empty script slots**.
   `concater_rep::typeset_v3_scripts` sends each vacant slot through full
   `typeset_as_concat`, including text/font/Unicode setup. Empty slots are
   intentional cursor destinations. Preserve source/environment capture,
   nominal metrics, script placement and caret affinities; do not replace them
   with an arbitrary generic empty box.
2. `evaluation_inputs::record` clones the root before every Immer update, even
   when no snapshot retains that root, forcing shared map branches. Check
   whether safe unique-owner updates materially reduce actual layout CPU;
   published snapshots must remain immutable.
3. Repeated paragraph preparation is now shared as described above. Inspect
   the remaining persistent lookup and shaping work rather than repeating this
   optimization. Do not add a duplicate RAM tier for persisted shaped runs.

## Deliverable

Report exact dominant paths, relevant source/expanded trees or boxes where
semantics matter, expected observable effects, and one bounded implementation
plan. After implementation compare old/new complete layout in the same time
window. Do not claim the 500 ms goal complete without a real warmed measurement.
