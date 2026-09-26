# Namespace Sub-product Ownership

This note records the current ownership and lifetime rules for generated
namespace sorters and ontology snapshots. The original source audit and the
pre-fix findings are preserved in repository history rather than maintained as
an active checklist.

## Sorter ownership

Namespace product sorters are compiled through the linked `libtcc` API. The
sorter cache is thread-local, so compiler state and generated executable storage
are never shared implicitly between unrelated owner threads.

Each cache entry owns a compiled generation. A sort operation retains the
generation it is executing even if the source changes and the cache entry is
replaced. Recompilation therefore cannot free machine code that is still in use.
Compiler error callbacks are detached before compiled state escapes the
compilation call, and sorter execution checks its owning thread.

The generated C ABI borrows stable capture buffers prepared once per record.
Comparator calls inspect those fields and reorder record indices; they do not
allocate fresh C strings for every comparison. Fallback stem comparison follows
the same borrowed-storage rule.

## Ontology snapshots

The ontology service publishes immutable snapshots. Public namespace records,
parent/capture metadata, sorter definitions and member lists belong to one
snapshot generation and are retained together.

Readers retain immutable record storage and select or reorder integer indices
instead of rebuilding TeXmacs strings, arrays or namespace payloads for each
consumer. File URLs are constructed on the consuming thread from shared path
values. Writable namespace forms remain local drafts and are never aliases into
a published snapshot.

The ontology worker owns its SQLite connections and indexing state. Qt widgets,
SQLite statements and mutable TeXmacs document objects are not passed across the
worker boundary.

## UI boundary

Namespace manager, explorer, switcher and product-generation UI remain on the Qt
main thread. Template derivation and generated C source construction are local
to the calling thread.

TCC compilation for a user-triggered product sorter is still synchronous, and a
request for a not-yet-published ontology generation can still wait for indexing.
Those are latency concerns, not ownership shortcuts: fixing them should move work
behind an explicit asynchronous boundary rather than weakening the lifetime
rules above.

## Regression coverage

`namespace_ontology_test` covers:

- cross-thread record and capture-buffer identity;
- retaining old snapshots across refresh/restart;
- retaining compiled generations across replacement and compile failure;
- thread-local compiler state and cleanup;
- stable payload addresses while sorting.

`namespaces_template_test` covers the independent template path. Previous focused
TSan runs found no host-side data race in these ownership paths; generated TCC
machine code itself is not TSan-instrumented and must not be treated as certified
arbitrary user code.

## Maintenance rule

Do not replace this model with a process-global sorter cache, a bare function
pointer whose executable storage can be retired independently, per-comparison
string allocation, or mutable cross-thread namespace payloads. New namespace
consumers should retain the current snapshot/generation explicitly and keep
owner-affine work on its owner thread.
