# ATHENA Interop, protocol version 1

AUDM selects live resources; AUDMAP exposes ticket-local occurrence handles.
This implementation does not restore TeXmacs plugins or expose arbitrary Scheme
evaluation. The registered domains are root, the active vault, and namespaces.

## Implementation boundaries

- `src/ATHENA/Interop`: PEGTL selector grammar, typed predicates, fixed worker
  executor, ticket resolution and pruning. No GUI or Scheme dependency.
- `src/ATHENA/Data/interop_resources.cpp`: root/vault/namespace adapters and
  resolver conventions. Namespace operations use explicit captured vault paths
  and persistent UUIDs, not the GUI buffer registry or a name-based re-resolution.
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

`create_file` requires `directory`, `values` and `use_initial_content`. It builds
the filename through the existing namespace template implementation, applies the
namespace style/initial content and atomically publishes a new `.ath` file inside
the vault. Existing files are never replaced. `subproduct` requires `other_uuid`,
`name` and `template`; an empty template requests inference (with explicit
`aggressive_string` for two templated parents). It reuses native template
derivation and TCC sorter generation. No wizard, file picker or arbitrary Scheme
callback is invoked by a resource operation. Stored native sorters execute native
code, so granting OPR access includes that existing namespace capability.

## CLI

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
