# Auxiliary Results Cache and Realtime Directories

Implemented scope: 2026-11-10.

## Purpose

Replace document-persisted TeXmacs auxiliary data and manually regenerated
directory bodies with realtime derived views. Keep disposable auxiliary
computation results between opens in sibling caches:

```
Lecture.ath
.cache/Lecture.ath.ac
```

The full source filename, including its extension, distinguishes ATH and AVD
documents. This is not a shaping cache or a general typesetting cache: no font
objects, boxes, editor snapshots or rendering object graphs are stored.
`tmfs://aux` buffers are unrelated and unchanged.

## Source and Derived Content

Source retains headings, captions, index/glossary markers and directory
declarations with their options. A declaration's old generated-body argument is
replaced by an empty document placeholder on admission. Its own metadata and
explicit title are retained.

The old top-level auxiliary envelope is neither restored into runtime state nor
written by document saves or interop source projection. Existing files are not
batch-rewritten; normal loading and subsequent saving perform this separation.
References and attachments retain their independent existing policies.

Generated views do not mutate source and do not produce undo entries, history
snapshots or Hodarium revisions. The runtime auxiliary maps and their copying
paths have been removed.

## Collection and Incremental Maintenance

WRITE effects are captured into immutable ordered contribution logs.
Bridge, surround and inline-fragment reuse retain shared child logs. Changed
contributions replace their units; ordinary body changes that leave contribution
identity unchanged do not schedule directory regeneration. Partial progressive
layout logs are not published as complete documents.

Folded source collects contributions during its existing semantic execution,
without constructing hidden layout boxes. Generated directory output suppresses
WRITE capture, preventing output from feeding back into its own input.

A directory declaration records a request including kind, channel, language and
index break style. The native derived-directory primitive reads the corresponding
environment binding. Shipped styles use this primitive instead of the obsolete
source body. Supported derived outputs are ToC, index, glossary and figure/table
lists, including explicit-title forms.

## Worker and Storage Ownership

One background worker owns SQLite connections, receipt calculation, directory
generation and binary encoding/decoding. SQLite uses ordinary transactions,
not WAL: editor threads never read the database, so reader/writer concurrency
would not help. No additional dependency is required.

The disk records contain generated directory results encoded as MessagePack
using the native v3 structured-value codec. Dependency receipts include the
request options and semantic contributions; index receipts also include the
referenced page bindings. A build/version stamp invalidates incompatible caches
without migration. Damaged records are regenerated.

Contribution logs are retained in memory, not serialized as complete document
snapshots. Reopening still collects source contributions during semantic/layout
execution to establish validity. Matching disk records skip directory
generation; this design does not promise zero semantic execution on reopen.

The owner freezes only changed semantic payloads. The worker returns detached
native trees through a generation-checked mailbox and wakes the owning actor
using actor/view IDs. Stale publications cannot replace current results.
Serialization, database IO and directory generation do not run on the editor's
publication path.

Pending requests are coalesced by key. Dirty records are batched for 750 ms and
updated transactionally. Cache hits do not rewrite records. Successfully
computed results remain usable after a storage failure; calculation failures
are reported explicitly. Closing a session does not wait for cache production.

## Loading, Export and Playback

Interactive layout starts with currently available views and accepts refreshed
results asynchronously. It does not wait for disk IO.

Printed export explicitly waits for current directory results and reruns layout
when they change. Page-reference and derived-view convergence are bounded;
failure is an explicit export error rather than silently exporting incomplete
directories.

Whole-document HTML export performs semantic collection, waits for the views and
overlays them on its expansion environment. Heading expansion preserves logical
anchors; HTML ToC entries link their titles to those anchors instead of emitting
unavailable printed page numbers. Snippet export uses already prepared views.

LaTeX retains its own ToC/index generation. Glossary/list declarations receive
their generated entries in a detached export copy, including when macro
expansion is disabled. Slideshow preparation also receives complete directory
views without changing the document.

## Lifecycle

Unnamed documents use memory-only sessions. A named destination that has not
been saved yet retains pending results until the source appears. Save As or
rename changes session ownership to the new source path; disposable caches need
not be moved.

The storage worker checks source existence around persistence. Background vault
inventory removes orphan ATH/AVD caches and their SQLite journals when it sees
the sibling cache directory. Opening another cache in the same directory also
prunes orphans. There is no editor-thread filesystem scan and no independent
continuous global watcher.

Vault inventory, file watches, offline document scans, Hodarium and backup paths
exclude `.cache`. Backups do not carry disposable cache copies. Deleting an
entire cache directory is supported; subsequent use recreates needed results.

## Retired Controls

Removed Document Update, Save auxiliary, the update-times preference, auxiliary
getter/setter/reset/generation commands, the offline ToC maintenance pass and its
dedicated worker CLI. The old edit_process class is gone.

Referenced-material refresh remains an explicit independent command. Resource
refresh and general rendering/font invalidation are not auxiliary regeneration
and remain supported.

## Implementation Map

- `src/Document/State/derived_directory.*`: detached directory algorithms and
  source-declaration normalization.
- `src/Document/State/auxiliary_contributions.*`: immutable ordered logs.
- `src/Document/State/auxiliary_cache.*`: worker-owned storage and job scheduling.
- `src/Document/State/directory_session.*`: dependency receipts, generated views
  and owner mailbox.
- `src/Typesetting/Env/local_environment.*`: semantic WRITE capture.
- `src/Editor/Engine/edit_typeset.*`: owner publication, export and playback.
- `src/Typesetting/Bridge/typesetter.cpp`: detached print layout and directory/
  page-reference convergence, including PDF export and reference probes.
- `ATHENA/packages/section/section-base.ats` and standard automatic macros:
  directory presentation.

## Verification

Focused storage checks cover superseded jobs, reopening, storage contention and
retry, invalidation, damaged caches, close/delete races, deferred first-save
persistence and orphan pruning. The focused bridge/view checks cover reusable
contributions, replacement/deletion, folded-source collection, output feedback
suppression and unchanged document source.

Application builds used `-j16` and were deployed with the local installer. Final
ELF build ID: `0ef40c636675afc4edd449286b0a7d69a57d2914`; installed and built
executables were verified byte-identical.

A synthetic document containing obsolete auxiliary and generated ToC content
was saved and exported through the application CLI. Native saving retained only
the declaration and empty placeholder. HTML contained the two current headings
both in its ToC and body, with title links resolving to their heading anchors.
PDF contained the complete ToC with resolved page numbers; its rendered first
page was inspected. Neither output retained the obsolete content. A separate
glossary export to LaTeX retained its current entry, explanation and page link,
not the obsolete source body. The temporary documents/profile were removed.

No broad regression suite was run. These checks establish the concrete
persistence/export paths, not exhaustive coverage of every style or directory.
