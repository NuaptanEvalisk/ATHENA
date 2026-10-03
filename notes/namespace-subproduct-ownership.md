# Namespace Sub-product Ownership

This note records the current ownership and lifetime rules for generated
namespace sorters and ontology snapshots. The original source audit and the
pre-fix findings are preserved in repository history rather than maintained as
an active checklist.

## Sorter ownership

Namespace script sorters are compiled with the pinned official Luau compiler and
executed by its bytecode VM. The sorter cache is thread-local, so VM state and
compiled generations are never shared implicitly between unrelated owner
threads. Native CodeGen/JIT is not enabled.

Each cache entry owns a compiled generation. A sort operation retains the
generation it is executing even if the source changes and the cache entry is
replaced. Recompilation therefore cannot invalidate VM state that is still in
use. Sorter execution and destruction check their owning thread.

Key sorters receive read-only typed capture tables once per member and C++ sorts
the resulting immutable typed keys. Comparator sorters evaluate a complete
relation for the current member set before record indices are reordered. Exact
signed 64-bit captures use Luau's integer representation rather than doubles.
Structural product and restricted sorters project fields with dynamic strings,
so generated compositions have no fixed-size text buffer.

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
main thread. Template derivation and structural JSON composition construction
are local to the calling thread.

Luau compilation for a newly loaded script is synchronous, and a request for a
not-yet-published ontology generation can still wait for indexing. Those are
latency concerns, not ownership shortcuts: fixing them should move work behind
an explicit asynchronous boundary rather than weakening the lifetime rules
above.

## Regression coverage

`namespace_ontology_test` covers:

- cross-thread record and capture-buffer identity;
- retaining old snapshots across refresh/restart;
- retaining compiled generations across replacement and compile failure;
- thread-local compiler state and cleanup;
- exact-int64, script-error/time-budget and atomic-failure contracts;
- structural restricted/product projection and constraint-cycle reporting.

`namespaces_template_test` covers the independent template path. Previous focused
TSan runs found no host-side data race in these ownership paths. Luau scripts are
sandboxed and budgeted, but they remain user programs whose errors are reported
instead of being treated as host invariants.

## Maintenance rule

Do not replace this model with a process-global sorter cache, a bare function
pointer whose executable storage can be retired independently, per-comparison
string allocation, or mutable cross-thread namespace payloads. New namespace
consumers should retain the current snapshot/generation explicitly and keep
owner-affine work on its owner thread.
