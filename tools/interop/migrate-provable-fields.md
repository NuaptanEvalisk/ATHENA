# Provable Field Migration

One-off migration of `theorem`, `proposition`, `corollary` and `lemma`.
No LLM and no theorem-name keyword list. All document reads and mutations use
AUDMAP. Direct filesystem access copies the backup, reads startup configuration
and writes reports.

## Run

Exit ATHENA first. The script refuses to start if an ATHENA process is still
running and lists its PID; it does not stop existing processes. After backing up,
it starts `ATHENA/bin/ATHENA.bin` from this checkout and loads the requested vault.
Use the updated binary containing saved-document `batch` support.

From the repository:

```sh
/usr/bin/python3 tools/interop/migrate-provable-fields.py \
  --vault nIKIS \
  --expect-root /home/felix/data/Notes \
  --apply
```

Omit `--apply` for a dry run. Both modes create a backup before starting ATHENA.
The Python interpreter needs the existing AUDMAP SDK dependencies `msgpack`
and `pyzmq`; the script imports the SDK from this checkout.

Use `--binary /path/to/ATHENA.bin` to choose another binary. The launched ATHENA
inherits your normal environment and profile; the script does not create another
profile or change plugin/Hodarium configuration. It connects to that child's
AUDMAP endpoint, not another instance. The exact vault name and root are checked
before document edits. Do not edit documents in the launched window during the
migration; any open documents are reported and skipped.

At the initial connection dialog, select **Full access**, then **Allow (this time
only)**. This authorizes the entire connection, without one prompt per document
or node. **Allow (always)** also remembers authorization across reruns. The
client identity remains
`~/.config/athena-audmap/provable-fields-migration.json`; `--identity` overrides it.
Do not reuse an identity previously granted only "Confirm operations".

## Backup And Progress

Before connecting or editing, the script creates a private
`/tmp/athena-provable-fields-*/` directory and immediately prints:

```text
Backup directory: /tmp/athena-provable-fields-.../vault
```

It copies the vault **except directories named `.athena` or `.cache`**, at every
depth. This avoids copying runtime databases and their transient journal files.
Other directories, including `.backup`, are retained. Symbolic links are
preserved, not followed. File contents are copied once, without hashing,
checksums or a second content-verification pass.
An unreadable file or unsupported special filesystem entry aborts before
migration. ATHENA is not running during this backup. Since `.athena` can also
contain namespace and other metadata, this is a document migration backup, not a
complete backup of all vault state.

The terminal progress bar remains active while counting files, copying bytes,
waiting for authorization, scanning and migrating. Redirected stderr receives
periodic progress lines instead of terminal control sequences. The backup path,
report path and final summary go to stdout.

Backups and reports are retained after completion, errors and interruption:

- `vault/`: pre-migration copy with the exclusions above.
- `migration.jsonl`: per-node outcomes, UUIDs, original fields and review reasons.
- `summary.json`: counts after a completed run.
- `athena.log`: output from the launched ATHENA instance.

Completion, errors and Ctrl+C all stop the ATHENA child started by this script.
The script does not terminate unrelated processes.

`/tmp` is temporary storage; it is not a permanent backup location.

## Extraction Rules

- Only a leading textual parenthesis is considered. Formatting, including bold,
  is ignored as recognition evidence; mathematical trees are opaque.
- Split comma/semicolon fields outside nested textual brackets. Trim surrounding
  whitespace.
- Extract complete four-digit year/decade fields. Also recognize an exact known
  attribution followed by a year, such as `Levy 1934`. Do not extract years or
  surnames embedded in a longer name.
- Classify whole attribution fields with `provable-attribution-fields.json`.
  Preserve spelling, accents, hyphens and multiword groupings.
- All remaining fields are names/aliases. The current schema has one rich-text
  `name`, so these are joined by `, ` in original order. Attribution is a list of
  rich-text values; year is a string. Any of these may be absent.
- Preserve mathematical subtrees exactly, including wrappers inside mathematics.
  Remove historical bold from extracted heading text, not from the remaining
  statement. This script does not replace the separate whole-body bold migration.
- Remove the old parenthetical prefix from the statement. Preserve enunciation,
  remaining body and transferred mathematical identities. Splits cannot duplicate
  an existing identity. If a prefix identity cannot be retained, leave the node
  unchanged and report it.
- The configured `Product Rule for d/dt` UUID is the sole missing-parenthesis
  exception. Require its exact prefix, derivative structure and `. If ` boundary;
  retain the original derivative tree and start the body at `If`.
- Leave already populated structured fields unchanged. Leave multiple
  attribution/year pairs, unknown malformed prefixes and configured review cases
  unchanged and explain them in the report. No silent flattening or guessing.

The existing native enunciation renderer displays `name`, `attribution` and
`year` in its heading; this migration supplies those properties.

## Commit And Recovery

All changes to one source file are sent in one AUDMAP batch. Original node
snapshots are checked, properties are validated, and native serialization must
round-trip before a single atomic file replacement. Persistent metadata is not
written through structural `set`.

There is no vault-wide transaction. On failure the script stops, retaining its
backup and report; earlier files may already be migrated. After interruption, an
already submitted batch may still finish. Rerunning is supported: nodes with
structured fields are skipped. Do not copy the entire backup over a vault with
subsequent edits merely to retry.

Exit codes:

- `0`: completed with no review/open-buffer skips.
- `2`: completed, with nodes needing review or open files skipped.
- `1`: error; see the final message and report.
- `130`: interrupted.

## Synthetic Verification

No real Notes files are used by these tests:

```sh
/usr/bin/python3 tests/interop/provable-fields-migration-test.py
/usr/bin/python3 tests/interop/provable-fields-migration-test.py --runtime
```

The runtime test supplies a temporary test environment and a synthetic v3 vault
so it does not touch desktop data; this is only test setup, not the migration's
normal startup behavior. It exercises process detection, backup exclusions,
backup-before-startup ordering, dry run, real AUDMAP migration, repeated runs,
UUID preservation, stale preconditions and rollback after a late batch failure.
It also checks that every launched test ATHENA exits.
