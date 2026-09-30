# Node equality and performance audit

This note records the durable conclusions from the September 2026 audit of
ATHENA's node-metadata equality and hashing semantics.  The node-model design
memo in `notes/node-model-handoff.md` remains the authoritative contract; this
note preserves the performance evidence and the correctness counterexample that
motivated keeping full tree equality metadata-sensitive.

## Contract

Global `tree ==`, `tree !=`, and `hash(tree)` include persistent node metadata
recursively.  An equal UUID alone is not sufficient for tree equality: nodes
with different contents, parameters, or typed properties are different stored
snapshots.

Consumers that intentionally ignore source identity must opt into a narrower
comparison.  `athena::node::content_equal` ignores source UUIDs while retaining
typed property values, and `content_projection` removes source UUIDs while
preserving properties and reference targets.  Neither operation should be
substituted for global tree equality.

The tempting alternative—making global equality ignore metadata—was rejected
because it changes program semantics, not merely cache behavior.

## Correctness counterexample

`src/Scheme/Scheme/native_tree_diff.cpp` uses full tree equality for its early
exit and for child prefix/suffix scans.  If global equality ignores metadata, a
target that changes only a node property can compare equal to the source and be
skipped before the diff applies the new header.

The native tree-diff regression therefore checks metadata fields independently
of `operator==`, including property changes through undo and redo.  Tests whose
only oracle is the equality operation under test are insufficient for this
class of bug.

This is one concrete reason that identity-insensitive equality must remain an
explicit caller choice instead of a global optimization.

## Historical primitive-cost measurements

A focused local C++ benchmark was run after `9cef60bcc` (`perf: share immutable
node metadata`).  Construction was outside the timed loops; results below are
the median of seven batches and measure warm primitive operations, not editor
latency.

| Operation | Median per operation |
| --- | ---: |
| Full equality, equal unannotated leaves | 9.24 ns |
| Full equality, UUID-only leaves, separate storage | 13.29 ns |
| Full equality, UUID-only leaves, shared CoW storage | 9.18 ns |
| Metadata equality, UUID-only, separate storage | 5.77 ns |
| Metadata hash, UUID-only, warm cache | 2.18 ns |
| Full equality with a 128-leaf rich-text property | 1,434.05 ns |
| Metadata hash with a 128-leaf rich-text property | 5,526.26 ns |
| Full equality, 112-leaf plain document | 1,150.79 ns |
| Full equality, 112 UUID leaves, CoW copy | 1,147.67 ns |
| Full hash, 112 UUID leaves, warm metadata cache | 2,314.20 ns |

The important distinction is between scalar metadata and rich-tree properties.
Scalar metadata uses immutable reference-counted backing storage, equality
fast-paths shared storage, and metadata hashes are cached.  Rich-text property
values recursively invoke tree equality/hash and deliberately do not share
mutable legacy tree handles in the same way.

These measurements do not establish the cause of any interactive latency tail.
They show that UUID comparison itself is not equivalent to recursively hashing
or comparing a rich metadata tree, and that a cache optimization should target
the measured consumer rather than weaken the global equality contract.

## Performance interpretation

A flat `perf` profile can identify CPU hotspots but does not by itself prove a
causal chain from metadata equality to an editor p95 regression.  Likewise, the
performance HUD's latest `Edit` sample and its five-second p95 are samples from
the same latency stream; a low latest value beside a high p95 does not identify
which pipeline stage was slow.

Before changing equality or cache semantics for performance, measure the actual
caller count and cost on the slow editing path, preserve a matching executable
for sampled profiles, and distinguish storage equality from content/model-input
fingerprints.

## CoW lifetime defect found during the audit

The audit also found a separate lifetime bug in `content_projection`: clearing
ID-only metadata could release the backing storage while the walker still held
a pointer into that storage.  `700b974e4` (`fix: keep metadata projection
storage valid`) fixed the callback to null the handle after clearing it and
added focused regression coverage.

That defect was a real correctness issue, but it was not evidence that global
metadata-sensitive equality caused the reported interactive latency tail.

## Related commits

- `9cef60bcc` — share immutable node metadata and cache metadata hashes.
- `700b974e4` — keep metadata projection storage valid after ID removal.
- `7e9b23a9b` — native UTF-8 tree diff whose header updates rely on full tree
  equality semantics.
