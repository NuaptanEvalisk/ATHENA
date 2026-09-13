# ATHENA Subprocess Plugins

This is the new AUDMAP-based system, not the removed TeXmacs plugin mechanism.
The native desktop manager owns subprocesses and launch grants. Package operations
run on one management worker. Preferences has a Plugins section; the Plugins menu
lists lifecycle actions and manifest commands.

## Desktop Use

Install a directory or ZIP via Preferences -> Plugins or Plugins -> Manage plugins.
Packages live in `~/.ATHENA/plugins/ID`; `ATHENA_HOME_PATH` overrides the profile.
Install never starts code. Uninstall requires a stopped process and retains
`plugins-data/ID`.

Startup can be Manual, Automatic or Delayed (seconds). API permissions can be
Read only, Full API access or Custom commands. Custom rules map resource types to
command names; `*` is the fallback resource type, not a command glob. Missing types
are denied in custom mode. Confirmation is independently No confirmation, Confirm
operations, or Confirm every request. Defaults are Manual, Read only, Confirm
operations. The host preauthorizes get/reply/inspect on the private control
subscription, so polling does not open repeated operation prompts.

Applying a policy stops the old process and revokes its grants in this desktop
instance. Settings persist in `plugins/.settings.json`; other running desktop
instances load the new settings on restart. Each instance starts its own configured
subprocesses. Every Start/Restart creates a fresh CURVE identity and subscription
GUID. Stop cancels scheduled startup, revokes the connection and sends SIGTERM to
the group, escalating to SIGKILL after two seconds. Force quit skips the grace.
Shutdown and leader exit also clean up group children. Deliberately detached
processes are outside this cooperative lifecycle model and the API permission model.

Process state, bounded stdout/stderr and the latest result are shown in the
management page. Failures remain visible; there is no automatic crash/restart loop.

The executable runs directly, without a shell, in its package directory with:

* `ATHENA_AUDMAP_ENDPOINT`: current instance discovery file.
* `ATHENA_AUDMAP_IDENTITY`: private per-launch SDK identity file.
* `ATHENA_SUBSCRIPTION_GUID`: launch mailbox selector component.
* `ATHENA_PLUGIN_ID`: manifest ID.
* `ATHENA_PLUGIN_DATA_DIR`: persistent plugin data directory.

See `tools/interop/examples/echo-plugin`. Its Python interpreter must have the
independent `clients/python` SDK installed.

## Package Contract

A package contains `manifest.json`, its executable and optional supporting files:

```json
{
  "schema": 1,
  "id": "example.plugin",
  "name": "Example Plugin",
  "version": "1.0",
  "description": "An example subprocess client",
  "executable": "plugin_exec",
  "arguments": [],
  "commands": [
    {"id": "hello", "title": "Hello", "parameters": {}}
  ]
}
```

`executable` defaults to `plugin_exec`. It is a package-relative path; scripts
need a shebang. Arguments are an argument vector, not shell source. Command IDs
and plugin IDs start with a lowercase ASCII letter and contain lowercase ASCII
letters, digits, dots, underscores or hyphens, with no consecutive dots.
Manifest command parameters are JSON objects. A manifest does not grant itself
access to ATHENA resources.

`package_store` accepts a directory or a ZIP, optionally with a single enclosing
directory. It stages and validates before publishing with a no-replace rename.
Existing installations must be explicitly removed before replacement. It never
runs package code. Uninstall detaches the directory before removing its contents;
the caller must stop the plugin first.

ZIP parsing uses the system **libarchive** library (BSD-2-Clause), rather than an
in-house ZIP reader. Only ZIP support is enabled. Directory copying uses POSIX
descriptor-relative reads without changing the process working directory.
Both paths reject links and special files, traversal, and ambiguous paths.
Limits are 10,000 entries, 64 directory levels, 256 MiB per file, 1 GiB unpacked,
and 256 KiB for the manifest. Duplicate JSON keys and duplicate ZIP paths fail.
Ownership, ACLs and privileged mode bits are not imported. Failed installs leave
neither a published partial package nor staging debris.

## Subscription Contract

The host creates one mailbox and a fresh identity per process launch. Only the
registry granted to that authenticated CURVE key contains its subscription
resolver. The GUID selects a resource; knowing it is not authorization. The
resolver accepts `@/subscription/GUID` from a root basepoint and publishes a
`subscription` accessor. It has ordinary AUDM parent lineage.

Operations:

* `inspect`: plugin identity, active state and per-message byte limit.
* `get {"after": 0, "limit": 64}`: pending commands, a scan cursor,
  `first_retained_id` and `history_truncated`. Each command contains `id`,
  `command` and `parameters`. `limit` is 1 through 256.
* `reply {"id": 1, "status": "OK", "result": ...}`: return a result. Status
  is `OK` or `ERROR`. An identical retained reply is accepted idempotently;
  conflicting, unknown and expired replies fail.

Polling is non-destructive. Polling from zero returns outstanding work again;
clients should deduplicate command IDs, and must not advance past a command
unless they retain responsibility for its eventual reply. Reconnects during the
same launch can recover pending work. This is retryable delivery, **not a claim
of exactly-once external side effects**.

The default queue retains at most 256 entries with a 32 MiB byte budget. Each
entry reserves a bounded command and reply slot (64 KiB each by default).
Only replied commands whose results the host has consumed may be evicted for
new commands. Otherwise enqueue fails with backpressure. Closing a launch
rejects further get/reply/enqueue calls, including through old handles; results
already admitted remain available to the host. A restart uses a new mailbox and
does not silently replay old commands into the new process.

## Ownership And Policy

The mailbox uses standard UTF-8 strings, JSON values and a mutex. No Qt object,
Scheme value or editor tree crosses its boundary. Resolvers and operations run
on the existing bounded worker pools.

An authorization `connection_grant` can replace a session's resolver registry
and supply command capability masks. A `"*"` mask is the fallback for resource
types without an explicit mask, allowing deny-by-default policies. Trust-mode
confirmation and command permission enforcement are separate. A denied command
cannot be enabled by approving a confirmation dialog.

Plugins are native subprocesses running as the user. AUDMAP permissions restrict
the API, **not operating-system filesystem or network access**. This system is
not an OS sandbox.

## Focused Verification

`plugin_subscription_test` exercises replay, bounded queues, result retention,
concurrent enqueue, close invalidation, real CURVE client isolation and transport
capability enforcement. `plugin_package_test` covers directory/ZIP install,
executable permissions, duplicate installs, uninstall, traversal, links,
oversized files, malformed manifests and staging cleanup. Both use isolated
temporary directories and neither runs installed package code.
`plugin_runtime_test` additionally runs a real Python SDK subprocess under the
desktop manager, covering menu delivery, arguments/results, permission denial,
policy changes, restart identity, group cleanup, stop escalation, startup modes,
policy persistence and uninstall. It uses a temporary profile and the system
Python with PyZMQ/msgpack, not a user's Conda interpreter.
