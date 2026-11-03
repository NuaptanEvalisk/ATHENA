# ATHENA Actor And Native Runtime Architecture

Status: core architecture implemented; this note records current ownership and
cross-thread invariants.

Updated 2026-11-03. Document computation runs in native C++ on its owning
BufferActor. The embedded Scheme runtime, object/glue bridge, rooted procedure
handles and interpreter workers have been removed. Ownership and immutable
hand-off remain the concurrency model.

## Goals

- Keep the Qt event loop responsive while multiple buffers compute.
- Preserve deterministic, serial mutation within each buffer.
- Run independent buffer commands concurrently with explicit native contexts.
- Share only services whose interfaces define thread-safe ownership.
- Avoid copying document trees, box trees, or large render payloads between
  threads.
- Make ownership, lifetime, and thread affinity mechanically checkable.

## Execution topology

### Server and Qt thread

There remains one Server on the Qt main thread.  It owns windows, Qt objects,
application routing, and the registry of buffers and actors.  It does not own
document computation and must not synchronously execute long document processing or
typesetting work.

### BufferActor

Each live buffer has one long-lived `BufferActor` and one actor thread.  That
thread is the sole mutating owner of:

- the buffer's document tree and observers;
- editor, cursor, selection, undo, and per-view document state;
- typesetter state, bridge/lazy structures, and box trees;
- the open file descriptor and save transaction for a file-backed buffer;
- owner-local native callbacks and dynamic execution context.

Commands for a buffer are messages processed in order.  Different actors may
compute concurrently, but no two threads mutate the same buffer.

Opening a buffer creates its actor before document initialization.  Closing it
stops new commands, drains or cancels outstanding work, retires all render
submissions, releases owner-affine C++ objects, closes the file, and
only then joins the actor thread.

### RenderService

`RenderService` is a separate thread shared by all BufferActors.  It consumes
immutable render commands; it does not execute editor commands or receive
document trees or box trees.  CPU rendering resources belong to this service.
If GPU rendering is introduced later, the graphics context and GPU resources
also belong here rather than to a BufferActor.

The Qt thread presents only a completed frame or surface handle.  It never
traverses actor-owned typesetting data.

### Background services

Native worker pools handle buffer-independent service work using immutable data
and captured service identities. They receive no editor or Qt capability.
Buffer commands always run in the corresponding BufferActor.

## Zero-copy render hand-off

## Cross-thread data invariant

Thread queues carry numeric identifiers only.  They never carry C++ pointers,
`string`, `url`, `tree`, `path`, `box`, Qt value objects, containers, or
type-erased callables.  In particular, `std::function` and lambdas with captures
are forbidden as actor messages because their captures make ownership invisible.

Fixed command and completion records live in preallocated shared arenas.  A
queue publication identifies the arena slot; the consumer reads that slot in
place and returns its id when finished.  Variable-sized data lives in an
immutable transferable blob allocated once by the producer.  The message holds
only its blob id and the consumer takes unique ownership through the registry.
Persistent text adopts that backing allocation instead of copying its bytes.

The same rule applies in both directions and at every boundary:

- Server/Qt to BufferActor commands;
- BufferActor to Server/Qt effects;
- BufferActor to RenderService command streams;
- RenderService to Qt completed frame storage;
- owner-local callback requests, completion IDs and owner-affine releases.

Writing a semantic render command directly into a shared render slot is not an
intermediate copy.  Recording into `QPicture`, serializing it, rebuilding it,
and passing a `QImage` to Qt is an intermediate-copy pipeline and must be
removed.

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

## Native command execution

Named application commands are declared once in XML under
`src/ATHENA/Commands/Interfaces/`. The `athena_commands` build target generates
typed C++ dispatch. XML controls names, parameter/result types and UI routing;
there is no interpreter, dynamic procedure registry or handwritten duplicate
per-command adapter.

The bounded `command_value` transport holds native scalar/container values,
document trees, paths, URLs and explicitly distinguished byte/syntax payloads.
It cannot contain procedures or recursive heterogeneous lists. Document trees
retain their metadata and owner-local handle identity. Serialized syntax trees
are data for legacy formats, not a substitute execution model.

### Dynamic buffer context

Each native command installs an `ExecutionContext` through `ExecutionScope`.
It identifies the actor, buffer, view, command and capabilities. The scope uses
a C++ thread-local stack; nested scopes and exceptions restore the previous
context with normal RAII. The document, DRD and node-contract contexts remain
owned by their actor rather than copied into a second runtime.

Generated editor dispatch resolves the editor through this context. Native
callbacks stay in owner-local registries; actor/UI queues carry their IDs.
`native_context_scope` provides exception-safe scoped native state without VM
dynamic-wind, roots or process-global deferred destruction.

### Capabilities and cross-thread calls

- Buffer commands access only their owning document/editor state.
- UI commands access Qt but may not mutate an actor-owned document directly.
- Global workers use only thread-safe services and immutable values.
- UI requests from actors are asynchronous; completion resumes the owning
  command through existing effect/payload IDs without a Qt/actor wait cycle.
- Cross-buffer operations dispatch to the target actor. Compound edits preserve
  prepare, apply, rollback and commit ownership and exception propagation.
- Interactive dialogs return typed string/URL data to the originating command;
  changing focus does not redirect the result to another document.

Keyboard JSON and document ACTION nodes select named native commands. They
cannot evaluate expressions. Global transformations arrive as AUDMAP document
proposals and use native preview, confirmation and backup transactions.

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

## Current implementation status

Native execution contexts, owner-local callbacks, per-buffer actors, ID-based
transports, allocator/string lifetime and the RenderService boundary are part
of the current runtime. No Guile heap, module publication, rooted Scheme handles
or interpreter entry/exit wrappers remain.

Feature-specific boundary auditing still applies. New cross-thread value types
must use immutable snapshots, stable IDs or explicitly thread-safe storage.
See `notes/scheme-runtime-removal.md` for build and runtime verification records.
