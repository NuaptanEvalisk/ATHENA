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

Startup can be Manual, Automatic or Delayed (seconds). Permissions have two
independent domains:

* **System access** is enforced by the Linux Minijail helper. It controls what the
  subprocess can do independently of ATHENA, such as network access and selected
  current-vault filesystem paths.
* **ATHENA access** is enforced by AUDMAP. It controls which resource types may be
  resolved and which commands may be invoked on them.

The manifest is the ceiling for both domains. User settings may grant only a subset
of permissions explicitly requested by the installed manifest. New permissions in
an updated manifest are therefore denied until reviewed. Confirmation is a separate
AUDMAP layer: granting a command can still require per-operation or per-request
confirmation; confirmation can never override a denied permission. The host injects
the private subscription capability needed for command polling.

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

On Linux the desktop launches `athena-plugin-sandbox`, which applies user/PID/IPC/
UTS/mount/network namespaces as appropriate, drops Linux capabilities, sets
`no_new_privs` and resource limits, and applies a Minijail Landlock filesystem
allowlist before executing the package entry point. Sandbox setup failure is a
launch failure; there is no unsandboxed fallback. The plugin receives a clean
environment containing only the runtime variables below and a controlled
`PATH`/locale:

* `ATHENA_AUDMAP_ENDPOINT`: the current instance discovery file.
* `ATHENA_AUDMAP_IDENTITY`: the private per-launch identity file.
* `ATHENA_SUBSCRIPTION_GUID`: launch mailbox selector component.
* `ATHENA_PLUGIN_ID`: manifest ID.
* `ATHENA_PLUGIN_DATA_DIR`: the plugin's persistent private directory.
* `ATHENA_VAULT_ROOT`: the captured current vault root, only when at least one
  vault filesystem permission is effective for this launch.

The package is Landlock read/execute; the plugin data directory is read/write and is
also used as `HOME`. `TMPDIR` points to a private `.tmp` directory under plugin data.
Host `HOME`, SSH-agent variables, language-tool environments and other inherited
secrets are not passed to the plugin. With no network permission the process receives
a private network namespace. A network grant shares the host network namespace and
therefore means Internet, LAN and localhost access.

See `tools/interop/examples/echo-plugin`. Its Python interpreter must have the
independent `clients/python` SDK installed.

## Package Contract

A package contains `manifest.json`, its executable and optional supporting files:

```json
{
  "schema": 2,
  "id": "example.plugin",
  "name": "Example Plugin",
  "version": "1.0",
  "description": "An example subprocess client",
  "executable": "plugin_exec",
  "arguments": [],
  "commands": [
    {"id": "hello", "title": "Hello", "parameters": {}}
  ],
  "license": {"format": "text", "file": "LICENSE.txt"},
  "permissions": {
    "jail": [
      {"permission": "filesystem.read", "required": false,
       "root": "vault", "path": "References", "scope": "tree"},
      {"permission": "network", "required": false}
    ],
    "audmap": [
      {"resource": "document", "actions": ["resolve", "get"], "required": true}
    ]
  }
}
```

`executable` defaults to `plugin_exec`. It is a package-relative path; scripts
need a shebang. Arguments are an argument vector, not shell source. Command IDs
and plugin IDs start with a lowercase ASCII letter and contain lowercase ASCII
letters, digits, dots, underscores or hyphens, with no consecutive dots.
Manifest command parameters are JSON objects. Schema 1 remains parseable for old
packages but requests no new system/AUDMAP permissions; packages that need access
should migrate to schema 2.

Jail permission names currently are `network`, `filesystem.read`, and
`filesystem.write`. Filesystem permissions use only `root: "vault"` and a plain
vault-relative path. Absolute paths, `.`, `..`, empty intermediate components,
backslashes, tilde/environment expansion, URI-like paths and globs are not part of
the grammar. `scope` is `file` or `tree`; omitting `path` is allowed only for a tree
grant covering the current vault. Read grants are read-only; write grants are
read/write. The manifest never contains a host absolute path. At launch ATHENA
resolves the relative target through the descriptor-backed confined filesystem
(`openat2` with `RESOLVE_BENEATH`, no symlinks/magic links). The pinned descriptor
is used to create the Landlock rule and is closed before plugin `execve`, so the
plugin receives authority only to the inode(s) named by the effective grant.

AUDMAP permissions list explicit resource types and actions. `resolve` controls
resource visibility: a denied resource is not published as an occurrence or handle.
Other actions are ordinary OPR command names. Wildcard resource requests are not
accepted in schema 2. `root` and the per-launch `subscription` are protocol
infrastructure supplied by ATHENA rather than manifest permissions.

An optional license is either native UTF-8 XML ATHENA format (`LICENSE.ath`) or
UTF-8 plain text (`LICENSE.txt`). Installation first stages and validates the package
without executing it. The license and requested permissions are then shown in the
review dialog; `.ath` licenses are rendered in the embedded ATHENA preview using a
strict static-document tag whitelist. Accept publishes the staged package; Cancel
deletes staging and leaves nothing installed.

`package_store` accepts a directory or a ZIP, optionally with a single enclosing
directory. `prepare()` stages and validates without publishing; `publish()` performs
the no-replace rename only after UI review. The convenience `install()` is retained
for license-free callers and rejects packages requiring license review.
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

An authorization `connection_grant` can replace a session's resolver registry and
supply resource capability masks. A mask independently controls resolution
visibility and command names; the host uses a deny-by-default fallback for plugin
sessions. Trust-mode confirmation and permission enforcement are separate. A denied
resource or command cannot be enabled by approving a confirmation dialog.

Minijail and AUDMAP are independent enforcement boundaries: granting network or a
vault filesystem path never grants ATHENA API access, and granting `document:set` never grants
host filesystem access. Minijail is defense-in-depth for subprocess containment,
not a claim that arbitrary malicious native code is safe to execute.

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
