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
**795 / 765 ms (new)**. Current deployed build ID:
`ec633b1dd46e9261feb5b9202ea79b3b71e3f532`. This is a modest improvement,
not the 500 ms goal. See the main performance note for complete run details.

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
3. Shaping cache lookup occurs after paragraph analysis and font selection.
   Determine how much repeated preparation is for identical short math atoms.
   Any reuse must respect owner-local ICU/FreeType state and font configuration
   invalidation. Do not add a duplicate RAM tier for persisted shaped runs.

## Deliverable

Report exact dominant paths, relevant source/expanded trees or boxes where
semantics matter, expected observable effects, and one bounded implementation
plan. After implementation compare old/new complete layout in the same time
window. Do not claim the 500 ms goal complete without a real warmed measurement.
