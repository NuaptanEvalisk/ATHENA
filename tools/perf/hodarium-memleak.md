# Hodarium node live memory capture

Use the host's installed BCC `memleak-bpfcc` (Apache-2.0), adapted by
`hodarium-memleak.py` for the container's mimalloc. This is a diagnostic, not a
memory-growth fix. It does not use ptrace, call functions in the target, restart
the container, or write Vault/state databases.

Upstream: [BCC memleak](https://github.com/iovisor/bcc/blob/master/tools/memleak.py)
and [stack-map definitions](https://github.com/iovisor/bcc/blob/master/src/cc/export/helpers.h).
The installed tool was preferred over a new allocator tracker; stock memleak
alone cannot attach to libmimalloc's missing mmap/munmap symbols or its C++ new
entry points. Heaptrack and Valgrind were not installed on this host.

## Current TrueNAS capture

Started 2026-11-10 against release PID 1206143. Subsequently replaced, with user
authorization, by diagnostic PID 1253690 in the same container
`ix-athena-hodarium-node-node-1`. Current image:
`localhost/athena-hodarium-node:debug-6b5817f5c-20261110`, mimalloc 3.0.1.

- Unit: `athena-node-memleak.service` (transient, not enabled at boot).
- Directory: `/mnt/tank/apps/athena-hodarium-node/diagnostics/`.
- Output: `memleak-debug.log`, plus at most three rotated files of 8 MiB each.
  The previous release capture remains in `memleak.log`.
- Sample rate: approximately 1/1024 allocations; reports every 60 seconds,
  listing samples still outstanding after at least 60 seconds, grouped by
  allocator return address (one frame, not a full calling chain).
- Independent `/proc` RSS, anonymous memory, huge pages, thread and CPU counters
  are recorded every minute. The initial memory map is also recorded.
- `CAPTURE` lines record sampled live bytes/counts, aged samples, unresolved
  stacks and used stack slots. These distinguish empty aged reports from a
  tracer that never observes allocations or cannot unwind stacks.
- Lifetime: at most 24 hours; stops when the target exits or its PID is reused.
- Tracer cgroup: 512 MiB maximum memory, 25% of one CPU, nice 10.
  Uprobe work executes in the target and is not covered by this CPU quota;
  sampling reduces stack/accounting work but not all probe trap overhead.
- BCC's outstanding allocation map has a fixed one-million-entry capacity;
  the caller map has 16,384 entries. Do not interpret exhausted/truncated maps
  or unresolved stacks as evidence that no leak exists.

Inspect or stop on TrueNAS as root:

```sh
systemctl status athena-node-memleak.service
tail -100 /mnt/tank/apps/athena-hodarium-node/diagnostics/memleak-debug.log
systemctl stop athena-node-memleak.service
```

The adapter routes the standard heap probes to mimalloc and adds `mi_new` and
`mi_new_nothrow`. On the inspected library, ordinary C++ new/new[] alias
`mi_new`; they must not be attached twice. mmap/munmap probes are intentionally
excluded: virtual arena reservations are not live application allocations.
This avoids the stock tool's attempt to find mmap in libmimalloc.

## Interpretation and boundaries

- Bytes/counts are **sampled**, not exact heap totals. Outstanding does not by
  itself mean leaked. Look for the same aged allocation sites growing across
  consecutive reports alongside anonymous RSS.
- Allocations before attachment are invisible. Custom heap APIs and aligned
  C++ APIs not flowing through a hooked standard entry point are not covered.
- BCC user stack unwinding depends on usable frame pointers. The inspected
  release produced over 2,000 pseudo-distinct stacks per minute, including data
  addresses past the first frame. The adapter changes only BCC's stack-map
  declaration and walker depth to retain the allocator return PC. This avoids
  exhausting the map with invalid deep frames. Use a frame-pointer build if
  deeper attribution is needed; the current reports do not provide full stacks.
- Upstream memleak's realloc accounting removes the old allocation at entry;
  failed realloc can undercount. This is sampling evidence, not a substitute
  for a full allocation census.
- If aged sampled allocations remain bounded while anonymous RSS keeps rising,
  investigate allocator retention/fragmentation and uncovered allocation APIs.
  Do not automatically label that result an application leak or allocator bug.
- The earlier 21.7 GiB incident is confirmed private anonymous RSS, but the
  root allocation site is not established. During earlier GDB investigation
  the old process exited and Docker restarted it. This capture targets the
  replacement process, not the lost original heap.
- 444 material-attachment revisions were pending. One inspected attachment's
  file was absent; repeated application tries export the entire Materials
  database to observe one object. This is a separate confirmed work amplifier,
  not yet proof of the cause of retained memory.

## Deploying another capture

Install the adapter outside Vault/state directories and start it against the
current host PID. No system packages are installed by this script.

```sh
systemd-run --unit=athena-node-memleak --collect \
  --property=RuntimeMaxSec=24h --property=MemoryMax=512M \
  --property=CPUQuota=25% --property=Nice=10 \
  /usr/bin/python3 /path/to/hodarium-memleak.py HOST_PID \
  --log /path/to/diagnostics/memleak.log
```

Confirm attachment succeeds and at least two reports are written. A running
unit alone is not evidence that allocator hooks are working. Stop the capture
before changing the node image/PID. Delete its bounded logs after diagnosis;
there is no automatic recurring or boot-time installation.

## Diagnostic image deployed 2026-11-10

The source is exactly git commit `6b5817f5c`, not current HEAD. Existing local
Podman builder image `202b363155e5` provided the Debian Trixie compiler and
dependencies, with networking disabled and no package installation. CMake
used the standalone node target and `-j16`, with `RelWithDebInfo` flags:

```text
-O1 -g3 -DNDEBUG -fno-omit-frame-pointer -fno-optimize-sibling-calls
```

`services/hodarium/node/Dockerfile.diagnostic` layers the unstripped executable
onto the original runtime image, preserving dependency versions. Runtime ELF
build ID, verified against the local artifact:
`120bb92b81f6615acfc7adf9fb4f6b12afa54ab1`.

Deployment used `app.stop`, `app.update` with the existing compose configuration
and only the image changed, then `app.start`. The deployment.json used by
`nodectl` was updated to the same image. No identities or Vault bindings changed.

The executable now contains DWARF information and frame pointers for ATHENA
code. Distribution Qt/SQLite libraries are unchanged; their frame chains may
still be unavailable. Therefore the long-running BCC capture remains a
single-return-PC capture. Use bounded `perf record --call-graph dwarf,8192`
captures for deeper attribution rather than assuming all dependencies acquired
frame pointers. Paths in DWARF refer to `/diagnostic/source`, reconstructable
from the exact source commit above.

Rollback image: `localhost/athena-hodarium-node:xml-v3-6b5817f5c`. Stop the capture
first and use the same TrueNAS app API path to change the image back.
