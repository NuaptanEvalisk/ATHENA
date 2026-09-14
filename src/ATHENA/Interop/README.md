# ATHENA Interop, protocol version 1

AUDM selects live resources; AUDMAP exposes ticket-local occurrence handles.
This implementation does not restore TeXmacs plugins or expose arbitrary Scheme
evaluation. The registered domains are root, the active vault, namespaces,
indexed artifacts, the vault filesystem and structured documents.

## Implementation boundaries

- `src/ATHENA/Interop`: PEGTL selector grammar, typed predicates, fixed worker
  executor, ticket resolution and pruning. No GUI or Scheme dependency.
- `src/ATHENA/Data/interop_resources.cpp`: root/vault/namespace adapters and
  resolver conventions. Namespace operations use explicit captured vault paths
  and persistent UUIDs, not the GUI buffer registry or a name-based re-resolution.
- `src/ATHENA/Data/interop_artifacts.cpp`: read-only indexed artifact accessors,
  bound to a captured vault incarnation and artifact UUID.
- `src/System/Files/confined_filesystem.cpp`: descriptor-based vault confinement,
  regular-file reads and revision-checked atomic replacement.
- `src/ATHENA/Data/interop_filesystem.cpp`: physical directory/file accessors.
- `src/ATHENA/Data/interop_document*.cpp`: document source selection, node
  identity, lossless portable tree encoding and owner-thread editing. One
  document resolver produces both document and node accessors.
- `src/Subsystems/AUDMAP`: MessagePack codec, connection-owned protocol state,
  local ZeroMQ transport. Resource operations have a separate fixed worker pool.
- `src/Subsystems/Qt/QTMAudmap.cpp`: connection authorization and request dialogs.
- `tools/interop/client.cpp`: JSON-facing command-line client.

Resolution uses Boost.Asio's fixed `thread_pool`, shared by all tickets. The
desktop currently starts min(8, hardware_concurrency), with at least one worker,
and two operation workers. A resolver publishes child continuations and returns.
Idle workers can execute different branches of the same ticket concurrently;
otherwise they remain queued. Workers never synchronously wait for their children.
There are no threads created per ticket, invocation, or branch.

Each invocation has private output and ancestor-only accessor visibility. A
ticket coordinator merges it under a short mutex. Completion waits for all
pending work, then prunes prefixes with no complete match. Resource identities
are not deduplicated: different routes preserve different handles and lineages.
Projection limits are applied only after completion and pruning.

Dependency choices: PEGTL 3 (MIT) for parsing, Boost.Asio (BSL-1.0) for worker
execution, cppzmq (MIT) and libzmq (MPL-2.0) for transport, and msgpack-cxx
(BSL-1.0) for the portable wire codec. The repository's existing nlohmann JSON
(MIT) represents portable values and the CLI's textual boundary. No handwritten
MessagePack decoder, transport cryptography, or alternate Scheme frontend is used.

## Connection and authorization

Each desktop instance publishes a `connection.json` under
`$XDG_RUNTIME_DIR/athena-audmap-PID-XXXXXX/` (fallback: `/tmp`). The directory is
0700; descriptor and IPC socket are 0600. No TCP listener is created. The
descriptor contains only the endpoint, PID, protocol version and ephemeral
server public key. libzmq restricts IPC clients to the server's UID.

The bundled clients persist a CURVE keypair. ZAP binds
the authenticated public key to message `User-Id`; a ROUTER identity must match
that identity. A self-reported client name is only a GUI label, never authority.
Unknown keys prompt with four choices: **Allow (this time only)**,
**Allow (always)**, **Reject (this time only)**, **Reject (always)**, together
with a trust-mode selector (default: Confirm operations). Closing the dialog
rejects once. Only Always writes a rule; its key is the authenticated public key,
not the display name. A saved allow restores the chosen mode on future connections;
a saved deny rejects without prompting. Other keys never inherit the rule.
Rules live in `$ATHENA_HOME_PATH/system/audmap/clients.json` (default
`~/.ATHENA/system/audmap/clients.json`). The directory is 0700 and the file 0600;
updates use a cross-process lock and atomic replacement. Read errors do not grant
access, and failed persistence leaves the dialog open instead of claiming success.
Deleting a client's entry from this file revokes its remembered decision for
subsequent connections; existing sessions are not retroactively changed.
Dialogs use a separate connection incarnation so a late answer cannot authorize
a new connection that happens to reuse the same key.

Transport control arrays:

| Opcode | Request/response |
|---|---|
| 100 | Client HELLO: `[100, 1, client_name]` |
| 101 | Server WELCOME: `[101, 1]` |
| 102 | Server authorization pending: `[102, null]` |
| 103 | Client heartbeat: `[103]` |
| 104 | Client disconnect: `[104]` |
| 105 | Server rejection: `[105, reason]` |

Send heartbeats at least once a second. A logical connection ends on explicit
disconnect or after 15 seconds without a message; a brief transport reconnection
with the same key can retain that logical connection. After a disconnect, even a
remembered key starts a fresh session; tickets and handles are not restored.
Disconnect retires all tickets and withdraws pending dialogs;
already admitted operations finish detached, without delivery to the old client.

Trust modes:

- **Full access**: REQ and OPR need no further confirmation.
- **Confirm operations**: every OPR, including get and inspect, needs confirmation.
- **Confirm every request**: every REQ and OPR needs confirmation.

ASK, LIN, REL, CNL and FIN never prompt. Replays do not prompt again. Closing an
authorization dialog denies the request. Operation confirmations display the
client name, accessor handle/ticket, frozen selection, resource type/identity,
literal command and JSON arguments as labeled fields. The GUI understands only
the AUDMAP envelope: it neither interprets domain types/commands nor fetches
resource properties. Connection public key, operation ID and the complete raw
request remain available in collapsed details. Buttons explicitly allow this
request or reject it; rejection is the default. Confirmation
dialogs are queued rather than stacked. Per-adapter capability masks are enforced
before invoking OPR; the native session API accepts an enforced flag and command
whitelist for each type. The desktop's initial policy adds no narrower masks.

## Selection grammar

```
@/vaults/@/namespaces/@
@/vaults/@/namespaces/"A name containing /"
@/vaults/@/namespaces/?($kind = "abstract")
@/vaults/@/namespaces/@/??($name contains "Topology")
@/???($type = "namespace" | $max_depth = 3, $max_matches = 20)
```

Names can be bare or JSON-quoted. Predicates support JSON scalar literals,
`= != < <= > >=`, `contains`, `starts_with`, `ends_with`, `exists($property)`,
NOT/!, AND/&&/comma, OR/|| and parentheses. No regex or arbitrary code evaluation
is accepted. `$name` and `$type` are universal; the dollar prefix denotes a
property reference rather than part of the returned map key. Missing properties
do not satisfy comparisons; existence distinguishes missing from explicit null.

String predicates in `?`, `??` and `???` support `*` for zero or more characters,
including newlines. `=` matches the whole value; `contains`, `starts_with` and
`ends_with` keep their respective anchoring, and `!=` negates the match. For
example, `?($name = "*strong*nullstellensatz*")` matches a name containing both
words in order. Matching is case-sensitive. `?` and brackets are literal within
strings; `\\*` in a JSON string denotes a literal star and `\\\\` a backslash.
Numeric comparisons and lexical `<`, `<=`, `>` and `>=` do not use patterns.
Names outside predicates remain exact names. The implementation uses libc
`fnmatch` on ASCII byte tokens, preserving UTF-8 and embedded NUL without enabling
additional shell-pattern syntax or locale-dependent character classes.

Namespace `@` reads `Vaultfile.json.root_namespace`; absent or missing targets
produce MISS, not a hardcoded Universe fallback. Namespace descent follows both
declared and derived parent edges, independently of explorer folding. A detected
cycle is a resolution fault. `??` stays within a resolver's domain; `???` can
cross domains and requires at least one explicit bound.

`max_depth` is a normal boundary. `max_matches` and `max_duration` produce
truncated success, not failure. Duration is in milliseconds, at most 86400000.
Quoted decimal integer bounds are accepted for the design document's spelling.
Truncation reasons are `[1, human_message]` for matches and `[2, human_message]`
for duration. A fault observed by another branch still fails the entire ticket.
Selection size is limited to 64 KiB, nesting to 64 and selector count to 256;
node and dispatch capacities protect the executor from unbounded queue growth.

## AUDMAP positional frames

Ticket IDs, operation IDs and handles are positive uint64 values. Zero is used
only for the absent parent in the FULL projection. Message size is at most 8 MiB.
Maps have unique UTF-8 string keys; extension objects, non-finite numbers,
trailing bytes and language-specific serialization are rejected.

| Opcode | Frame |
|---|---|
| 1 REQ | `[1, ticket, selection, projection]` |
| 2 ACK | `[2, ticket]` or `[2, ticket, opid]` |
| 3 ASK | `[3, ticket]` or `[3, ticket, opid]` |
| 4 ACX | `[4, ticket, [projected_handles, truncation_reasons]]` |
| 5 OPR | `[5, ticket, opid, handle, command, parameter_map]` |
| 6 RSP | `[6, ticket, opid, status, data]` |
| 7 ERR | `[7, ticket, reason]` or `[7, ticket, opid, reason]` |
| 8 REL | `[8, ticket, opid]` |
| 9 LIN | `[9, ticket, opid, handle]` |
| 10 CNL | `[10, ticket]` |
| 11 FIN | `[11, ticket]` |

Projection `[0]` is FULL, represented by flat `[handle, parent_handle]` records.
`[1]` is LEAVES; `[1, N]` returns at most N leaves. LIN returns the unique
root-to-handle sequence without calling a resource adapter.

REQ/OPR duplicates with identical IDs and payloads replay existing state rather
than re-executing. ASK is the intended recovery operation. REL releases terminal
payloads but leaves an operation-ID tombstone. CNL is resolution-only; a single
connection owner orders it against ACX. FIN closes admission and drains already
accepted operations. Distinct OPRs are unordered and can execute concurrently.
Ticket-scoped ERR is fatal; operation-scoped ERR does not invalidate the ticket.

## Native commands

All resources expose `inspect` (command parameter descriptions) and `get`.
The vault exposes `create_namespace` with a `definition` map.

Namespaces expose `get`, full-definition `set`, `rename`, `delete`, `members`,
`relations`, `set_relation`, `remove_relation`, `template_fields`, `create_file`,
`sorter_source` and `subproduct`. `set` can change names while preserving UUID;
`rename` changes only the name and graph references in one SQLite transaction.
Relation operations identify their other endpoint by UUID. Parent lists in full
definitions use names, matching the native namespace definition model.

Full namespace definitions include an ordered `materials` array of Material
UUIDs. Semi-concrete and concrete namespaces may specify it; abstract namespaces
must leave it empty. Namespace Manager exposes the same list in its Documents
tab. Opening a namespace database migrates schema v2 to v3 transactionally,
preserving namespace UUIDs and existing definitions.

Loading an `.ath` document recomputes referenced Materials from every matching
namespace's filename template. Bibliographies include the union of cited,
explicitly selected and inherited Materials, deduplicated by canonical UUID.
Inheritance never modifies the document's explicit UUID tuple; the reference
chooser shows inherited entries and their originating namespaces separately.
Changing namespace membership and reloading or refreshing the document therefore
removes obsolete inherited entries instead of converting them to explicit ones.

`create_file` requires `directory`, `values` and `use_initial_content`. It builds
the filename through the existing namespace template implementation, applies the
namespace style/initial content and atomically publishes a new `.ath` file inside
the vault. Existing files are never replaced. `subproduct` requires `other_uuid`,
`name` and `template`; an empty template requests inference (with explicit
`aggressive_string` for two templated parents). It reuses native template
derivation and TCC sorter generation. No wizard, file picker or arbitrary Scheme
callback is invoked by a resource operation. Stored native sorters execute native
code, so granting OPR access includes that existing namespace capability.

## Filesystem and documents

```text
@/vaults/@/filesystem/dir1/dir2/example.ath/online
@/vaults/@/filesystem/??($type = "file" AND $name = "*.ath")/saved
@/vaults/@/filesystem/example.ath/online/body/[0]/[0]
@/vaults/@/filesystem/example.ath/saved/??($tag = "transclude")
```

`filesystem` exposes the physical vault root. Names select immediate entries;
`?` selects children, `??` recurses within the filesystem, and bounded `???`
can continue into document sources. Entries expose `name`, `type` (`directory`
or `file`), `absolute_path`, `created_time`, `modified_time`, `accessed_time`,
and file `size` in bytes. Times are `{seconds, nanoseconds}` since the Unix epoch;
unavailable creation time is null, never replaced with inode change time.

Absolute paths, `..`, embedded separators and NUL are rejected as path components.
Internal symbolic links may resolve inside the vault; outside links, magic links,
special files and symlink cycles cannot escape confinement or block on device
reads. Explicit inaccessible paths fail; enumeration skips inaccessible entries.
An existing file handle becomes `STALE` if its inode is replaced. `check {}`
loads an `.ath` file with the native document parser and reports `valid` and
`reason`; it does not typeset the document, execute its macros, or verify links.

The document resolver accepts only `.ath` file accessors. `online` uses the
current BufferActor source when open and otherwise reads disk; `saved` always
reads disk. Both expose the **full unexpanded native source**, including the
version, style, initial settings, body and custom document attributes. An empty
native document may legitimately omit its body. Expansion products are not
substituted for source nodes or transclusion references.

Document handles identify the logical canonical file path and source mode.
They follow later disk revisions and, for `online`, buffer opening/closing.
Node handles identify a particular source generation and native node, not a
path to be looked up again. In-place edits and sibling insertions preserve
unaffected node identities; replacement/deletion invalidates affected handles.
Changing the underlying source generation makes old node handles `STALE` while
the document handle can retrieve the current root. Closing/reopening the vault
invalidates all its accessors. Different tickets share a live document session,
not independent copies of the same resource.

Names within a document select immediate children by tag (or atomic text).
`[0]` addresses the first child; repeated offsets descend further. A name or
local predicate can carry positions to select within its immediate matches.
Positions are zero-based and are not supported on recursive query result sets.
Node properties include `node_kind` (`compound` or `text`), `arity`, `path`,
`tag` or `text`, plus `source` and `absolute_path`. The universal `type` remains
`document` or `node`.

`get {}` returns the source mode, path and encoded tree. An atomic node is
`{"text":"UTF-8 text"}`; a compound is `{"tag":"name","children":[...]}`.
When native Cork bytes cannot round-trip through UTF-8, `cork` or `tag_cork`
contains MessagePack binary instead, preserving arbitrary source bytes.

Buffers can also be reached independently of a vault:

- `@/buffers/[ID]` selects a stable, non-reused BufferActor ID within this process,
  not a position in the buffer list. `@/buffers/?($type = "buffer")` enumerates buffers.
- `@/buffers/@` selects the active document buffer when resolution reaches this
  step. The resulting handle stays bound to that buffer when focus changes.
  No active buffer means no match; dialogs and the REPL are not buffer targets.
- `@/buffers/@/document` accesses the full live source, including metadata;
  `/body/[0]` then selects its body document, and `/[0]` its first paragraph.
- Buffer `get {}` returns `id`, `name`, `url`, `modified`, `active`, and `type`.
  Renaming preserves buffer and node identities. Closing invalidates these
  accessors without falling back to disk, even if the same file is reopened.
- A filesystem `.ath` file's `buffers {}` returns its open buffer IDs (an empty
  array when closed), including canonical-path aliases, without opening it.
- Unsaved, external, and virtual buffers are supported. Tree operations use the
  same BufferActor ownership and read-only checks as file-based online access;
  they do not require an active vault or save changes implicitly.

For example, resolve `@/buffers/@/document/body/[0]/[0]`, then run
`insert_after {"siblings": [{"text": "hello world"}]}` to add a paragraph after
the first paragraph. Inside a `concat`, the same operation inserts inline nodes,
not paragraphs. These are source-tree edits, not high-level editor commands.

Document and node operations:

- `set {"tree": NODE}`: replace this node (or the full document root).
- `insert {"index": N, "children": [NODE, ...]}`: insert children at an offset.
- `insert_before {"siblings": [NODE, ...]}` and `insert_after {"siblings": [NODE, ...]}`:
  insert siblings relative to this node's current identity. The structural parent
  and insertion position are located on the source owner during the edit, not
  supplied by the client. Root nodes have no siblings; stale targets are rejected.
- `erase {}`: remove this node; removing the full document root is forbidden.
- `set_tag {"tag": "name"}`: change a compound node's tag.

Invalid edits are rejected before changing the source. Online mutations of an
open document run on its BufferActor, update editor state and mark the document
modified without saving. Read-only buffers return `READ_ONLY`. GUI registries,
native trees, observers and editors are never accessed directly by resolution
workers. Cross-thread messages use standard values and opaque node identities.

Saved edits (and online edits of closed files) validate native serialization,
check the source revision and atomically replace the file inside the vault.
They return `committed`, `directory_synced` and `revision_available`; a failed
directory fsync after rename does not falsely report that the mutation was
rolled back. Revision mismatches return `CONFLICT`; cooperating writers are
serialized, but ordinary filesystem replacement is not a compare-and-swap
against arbitrary external programs. Files are limited to 64 MiB, tree decoding
to one million nodes and depth 256; the protocol's 8 MiB message limit still
applies independently.

## CLI

### Artifact queries

Artifacts are children of a vault, optionally selected through the `artifacts`
marker. For example:

```text
@/vaults/@/?($type = "provable" AND $name = "*strong nullstellensatz*")
@/vaults/@/artifacts/??($type = "provable" AND $name contains "null*" | $max_matches = 20)
@/???($resource_type = "artifact" AND $type = "definition" | $max_depth = 3)
```

An artifact accessor has resource type `artifact`; its `$type` property is the
semantic artifact type, such as `provable`, `definition` or `completion`.
`$name` is its first semantic name (or display text when unnamed), not its anchor.
The `names` array contains all semantic names; matching `$name` does not implicitly
match secondary names. Different UUIDs with equal names remain distinct results.
Artifacts are leaves, and their lineage retains the containing vault.

`get` and `inspect` take empty parameter maps. `get` reads the current record by
UUID; deletion returns `NOT_FOUND`, and closing or reopening the vault invalidates
old accessors with `STALE`. Predicates use the record captured during resolution.
Native Cork-encoded name and keyword trees are MessagePack binary values, not
mislabelled UTF-8 strings. The resolver opens existing indexes read-only: an
unbuilt index yields no matches; damaged schemas or missing companion databases
fail without creating, rebuilding or upgrading the index.

### Command-line client

Workspace -> AUDMAP REPL opens an ADS pane with a real PTY, using the installed
QTermWidget 6 library (GPL-2.0-or-later, LGPL-2.0-or-later and BSD-3-Clause
components, compatible with ATHENA's GPLv3). It runs the same standalone Readline
client directly, not a shell command or a QTextEdit terminal approximation.
QTermWidget provides terminal emulation, selection, scrolling and PTY resizing;
Readline still owns command editing and history. See the
[upstream implementation](https://github.com/lxqt/qtermwidget/tree/2.4.0).

The pane explicitly connects to this desktop instance. Each launch has a private
temporary client key and requires normal AUDMAP authorization; it does not grant
itself full access. Closing the pane terminates its process. Restart starts a
fresh connection so abruptly closed tickets cannot collide with new ticket IDs.
The external CLI's persistent identity is not read or overwritten. Terminal
keystrokes, including Ctrl+W and Ctrl+C, go to the PTY rather than desktop menu
shortcuts. Use the dock close button to close the pane.

The startup log prints the instance's descriptor path. For example:

```sh
ATHENA/bin/athena-audmap --endpoint /run/user/1000/athena-audmap-PID-XXXXXX/connection.json \
  --select '@/vaults/@/namespaces/@' --command get
```

`--endpoint` is optional when exactly one live endpoint is discoverable in the
runtime directory. Multiple instances require an explicit choice. The CLI uses
`$XDG_CONFIG_HOME/athena-audmap/cli.json` (default `~/.config/athena-audmap/cli.json`)
for its persistent identity. `--identity /private/directory/key.json` selects a
different identity; its directory must be 0700 and existing files must be 0600.
One key identifies one logical connection at a time; concurrent independent
clients must use different identity files. Whoever possesses a private key can
act as that client: this authenticates a key, not an executable path. Regenerating
a key creates an unknown client; it does not inherit the old key's allow/deny.

`--parameters '{...}'` supplies an operation parameter map. `--stdio` instead
keeps the connection open for newline-delimited JSON positional frames and
prints responses. Wait for terminal responses before closing stdin; EOF is an
explicit disconnect, not a request to finish queued operations interactively.

## Interactive REPL

`ATHENA/bin/athena-audmap-repl` is a separate binary. It shares the CLI transport
and uses GNU Readline (GPL-3.0-or-later, compatible with ATHENA), rather than a
custom terminal editor. Readline callback mode leaves the socket and one-second
heartbeat serviced while editing a line or awaiting GUI confirmation. History
is memory-only and bounded to 500 entries. The default persistent identity is
`$XDG_CONFIG_HOME/athena-audmap/repl.json`, separate from the scripting CLI.
`--endpoint` and `--identity` work as above; `--help` needs no running server.

```text
audm> @/vaults/@/namespaces/@
Resolving ... done.
Resolution 1
Handle  Parent  Target
------  ------  --------
     1       -
     2       1
     3       2  selected
Parent '-' means no parent. Operations target the selected handle.
handle 3> inspect
handle 3> get
handle 3> lineage
handle 3> use 2
handle 2> get {}
handle 2> exit
audm> exit
```

FULL projection retains the pruned parent topology. `handles` lists it; the first
leaf is selected initially, and `use HANDLE` selects any handle from this ticket.
`COMMAND [JSON object]` invokes an operation on the selected handle. `lineage`
uses LIN; `inspect` is an ordinary operation and obeys the chosen trust mode.
The first `exit` sends FIN and returns to selector mode. The next `exit`
disconnects and quits. While resolving or running an operation there is a status
line, not an input prompt; heartbeats continue. Ctrl+C during this wait sends
CNL for a pending resolution, or FIN for a ticket with an outstanding operation,
and returns to selector mode. Already admitted operations are not undone.
EOF disconnects as well. Piped input is supported and waits for each terminal
reply before executing the next command.

Focused tests are `interop_test` (parser, single/multiple fixed workers, pruning,
cancellation, replay, persistent identities/policies, real CURVE IPC and the
standalone REPL), `audmap_authorization_ui_test` (the four actual Qt buttons,
remembered decisions, trust-mode restoration and disconnect cleanup), and
`namespace_database_test` (temporary-vault native resolver and identity tests).
`audmap_repl_terminal_test` drives Readline through a PTY to check prompt counts,
waiting output, help text and Ctrl+C cancellation with a synthetic IPC server.
