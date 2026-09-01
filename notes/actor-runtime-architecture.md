# ATHENA Actor, Rendering, and Guile Runtime Architecture

Status: design decision; allocation and core string prerequisites implemented

This note records the agreed direction for moving document computation away
from the Qt/Server thread while retaining one shared ATHENA Scheme world.  The
design favors ownership and immutable hand-off over coarse locks and bulk data
copies.

## Goals

- Keep the Qt event loop responsive while multiple buffers compute.
- Preserve deterministic, serial mutation within each buffer.
- Share Guile modules, bytecode, definitions, and global services process-wide.
- Allow Scheme code associated with different buffers to execute in parallel.
- Avoid copying document trees, box trees, or large render payloads between
  threads.
- Make ownership, lifetime, and thread affinity mechanically checkable.

## Execution topology

### Server and Qt thread

There remains one Server on the Qt main thread.  It owns windows, Qt objects,
application routing, and the registry of buffers and actors.  It does not own
document computation and must not synchronously execute long document Scheme or
typesetting work.

### BufferActor

Each live buffer has one long-lived `BufferActor` and one actor thread.  That
thread is the sole mutating owner of:

- the buffer's document tree and observers;
- editor, cursor, selection, undo, and per-view document state;
- typesetter state, bridge/lazy structures, and box trees;
- the open file descriptor and save transaction for a file-backed buffer;
- buffer-local Scheme handles and dynamic execution context.

Commands for a buffer are messages processed in order.  Different actors may
compute concurrently, but no two threads mutate the same buffer.

Opening a buffer creates its actor before document initialization.  Closing it
stops new commands, drains or cancels outstanding work, retires all render
submissions, releases owner-affine Scheme and C++ objects, closes the file, and
only then joins the actor thread.

### RenderService

`RenderService` is a separate thread shared by all BufferActors.  It consumes
immutable render commands; it does not execute Scheme and does not receive
document trees or box trees.  CPU rendering resources belong to this service.
If GPU rendering is introduced later, the graphics context and GPU resources
also belong here rather than to a BufferActor.

The Qt thread presents only a completed frame or surface handle.  It never
traverses actor-owned typesetting data.

### Global Scheme workers

A bounded process-lifetime pool may execute Scheme jobs that are independent of
any buffer, such as immutable parsing or indexing work.  Such jobs receive no
editor or Qt capability.  Buffer-bound Scheme is executed directly by the
corresponding BufferActor, not forwarded to this pool.

## Zero-copy render hand-off

Each BufferActor-to-RenderService connection owns a preallocated shared render
chunk arena.  Payload slots are contiguous and are not interleaved with mutable
status words.  RenderService never writes payload data.

Two single-producer/single-consumer descriptor rings manage slot ownership:

- the submission ring is written by BufferActor and read by RenderService;
- the completion ring is written by RenderService and read by BufferActor.

A descriptor contains the slot index, used byte count, buffer generation, frame
generation, and damage metadata.  Producer and consumer positions are monotonic
sequence counters on separate cache lines.

The lifecycle is:

1. BufferActor takes the oldest completed/free slot.
2. It writes the payload without synchronization because it exclusively owns
   that slot.
3. It publishes a submission descriptor with release semantics.
4. RenderService acquires the descriptor and reads the payload in place.
5. After rendering, RenderService publishes the slot index to the completion
   ring with release semantics.
6. BufferActor acquires that completion before reusing the slot.

No payload copy, per-slot lock, or linear status scan occurs.  An empty/full
queue uses an atomic wait, semaphore, or equivalent blocking wake-up rather than
busy polling.  Generation numbers make stale work discardable without an ABA
ambiguity.

Render chunks contain POD commands and stable resource identifiers.  Fonts,
images, brushes, and other resources are installed in a RenderService-owned
immutable resource table; legacy reference-counted resource objects do not cross
the ring.

## Shared Guile runtime

ATHENA boots one process-global Modified Guile 3 runtime.  All Guile execution
threads share its heap, compiled bytecode, module graph, symbols, procedures,
syntax, and global definitions.  There is no Guile runtime or module copy per
buffer and no central Scheme request thread.

Every BufferActor thread enters the shared runtime using `scm_with_guile` and
executes its own document Scheme calls.  The Qt thread remains Guile-capable for
short UI/global work.  RenderService never enters Guile.

### Dynamic buffer context

Each Scheme command binds one `SchemeExecutionContext` containing at least the
BufferActor, buffer id, view id, command id, and capability set.  A Guile
thread-local fluid and C++ thread-local pointer reference the same context
object; they do not contain duplicate copies of document state.

The context replaces process-global notions such as `the_view`, `the_drd`, and
the implicit `get_current_editor()`.  Existing Scheme editor APIs remain source
compatible, but their glue resolves the editor through the current execution
context.

Current module, ports, exception state, security context, and similar dynamic
settings are rebound for every command and restored afterwards.  State left by
one command must not leak into the next command on the actor thread.

### Capabilities and cross-thread calls

- Buffer Scheme may access only its owning document/editor state.
- UI Scheme may access Qt but may not mutate an actor-owned document directly.
- Global workers may use only thread-safe global services and immutable values.
- A UI request from Buffer Scheme is asynchronous.  The actor suspends or splits
  the command and resumes after a result; it must not synchronously wait in a
  cycle with the Qt thread.
- Cross-buffer operations are messages to the target actor, never direct tree or
  editor calls.

### Native module and lazy-definition concurrency

The ATHENA module tables in Modified Guile must become native synchronized
registries.  Each module has a record with `UNLOADED`, `LOADING(owner)`,
`LOADED`, or `FAILED` state, a condition variable, result/failure data, and a
generation.

The global registry lock is held only to find or create a record.  Module
top-level Scheme executes with no registry lock.  Different modules may load in
parallel; callers requesting the same module wait outside Guile mode.  A
wait-for graph distinguishes an actual dependency cycle from another thread
already loading the module.  Completion or failure is published once to every
waiter.

`lazy-define` uses the same per-symbol forcing state machine.  Its provider list
is an immutable snapshot and is not removed before successful completion.
`tm-define` serializes publication per binding; normal calls use the stable
Guile variable cell without a hot-path lock.

### Global mutable Scheme state

Guile's ability to run threads concurrently does not make a compound mutation
of shared state atomic.  ATHENA classifies mutable global state explicitly:

- read-mostly configuration uses immutable, versioned snapshots;
- shared caches use native concurrent services;
- intentionally shared Scheme cells use Guile mutexes, conditions, or atomic
  boxes;
- buffer state stays in its BufferActor rather than in module globals.

Module infrastructure and editor-context compatibility are fixed in Modified
Guile and glue code.  Existing `.scm` files do not need mechanical per-file
rewrites, although genuinely shared runtime caches must be audited.

### GC, roots, and finalization

All Guile threads share one BDW-GC heap.  Actor mailbox waits and long C++ work
that holds no unrooted `SCM` values run through `scm_without_guile`, reducing GC
root scanning and rendezvous cost.

The global Scheme `object-stack` and deferred destruction list are replaced by
owner-sharded rooted handle arenas.  A Guile finalizer must never directly
destroy an actor-owned editor, widget, tree, or other C++ object.  It enqueues an
owner-affine release request, and the owning actor or Qt thread performs the
destruction.

## Native object ownership and reference counts

Changing every legacy `ref_count` to an atomic integer is not the concurrency
model.  Atomic reference counts alone do not make mutable aliases safe, do not
preserve destructor affinity, and add cost to every local copy.

- Mutable document `tree` objects remain BufferActor-owned.  They do not cross
  actors.  A cross-thread tree, when truly needed, is an immutable frozen
  snapshot with its own thread-safe lifetime and structural sharing.
- Box trees remain entirely BufferActor-owned.  RenderService receives flattened
  render chunks, not `box` handles, so box reference counts need not become
  atomic.
- `string` is now a 24-byte value type with a 14-byte inline form and
  immutable shared
  `std::basic_string<char, std::char_traits<char>, mi_stl_allocator<char>>`
  storage for longer values.  Inline copies avoid allocation and atomic traffic
  for the short tokens which dominate typesetting.  Longer copies retain an
  atomic reference-counted storage object; every mutable API detaches before
  writing.  This preserves cheap transfer of paths and font descriptors without
  retaining the old shared-mutable representation, dummy moved-from object, or
  non-atomic lifetime.  Moves pass long storage directly, and append-heavy paths
  can reserve explicitly.
- Other objects crossing a boundary use immutable snapshots, stable ids, or an
  explicitly thread-safe shared representation.  Legacy arrays, paths, fonts,
  images, brushes, and observers are not assumed safe merely because they are
  reference counted.

Debug builds should store/check owner ids on actor-affine objects and reject a
wrong-thread mutation or destruction at the first boundary.

The legacy process-global small-array freelists have been removed.
`tm_new_array` now obtains aligned blocks directly from mimalloc, keeps only
per-allocation construction metadata, and safely supports remote reclamation.
`tm_new` and `tm_delete` likewise map directly to mimalloc.  This removes the
allocator corruption boundary before actors allocate trees and boxes
concurrently; ownership and reference-count safety of those objects remains a
separate requirement.

## Implementation order

1. Make allocation and cross-thread immutable value lifetimes safe.  Core
   allocator and `string` work is complete; remaining cross-thread value types
   must be handled at the boundary where they are introduced.
2. Replace Scheme root handling and finalization with owner-affine mechanisms.
3. Make Modified Guile module, `lazy-define`, and `tm-define` publication
   concurrent.
4. Introduce `SchemeExecutionContext` and route editor glue through it.
5. Move each buffer's computation and Scheme execution into its BufferActor.
6. Introduce the render chunk arena and RenderService boundary.
7. Add global Scheme workers only for capability-restricted, buffer-independent
   work.
