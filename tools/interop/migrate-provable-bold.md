# Provable Bold Migration

This one-off Python AUDMAP client removes explicit whole-body bold formatting
from canonical `enunciation` nodes of kind `theorem`, `proposition`, `corollary`
or `lemma`. ATHENA's shipped styles now supply their bold rendering.
It does not extract names, authors or years, alter other enunciation kinds, or
remove local bold titles and phrases inside a multi-paragraph body.

Run it with the current ATHENA binary, which provides the native
`clear_formatting` document operation. The script imports the Python SDK from
this checkout; its dependencies are `pyzmq` and `msgpack` (available in this
workstation's `/usr/bin/python3`). No plugin installation is needed.

## Preview And Apply

Use a dedicated ATHENA instance with a copied test vault first. Supply that
instance's explicit `connection.json`, not an automatically discovered endpoint.
Authorize the script's client through normal AUDMAP authentication. Its identity
must live in a private directory (0700; key file 0600).

```sh
/usr/bin/python3 tools/interop/migrate-provable-bold.py \
  --endpoint /path/to/test-instance/connection.json \
  --identity /private/directory/migration.json \
  --vault "Test vault" --expect-root /tmp/copied-vault \
  "Notes Root/Sources/Lecture Notes"
```

Without `--apply`, this only reports candidate operations as JSON lines followed
by a summary. Repeat the same command with `--apply` to commit those operations.
The positional arguments can be several vault-relative files or directories.
Use `--exclude "relative/directory"` to omit a subtree. `.athena`, `.backup` and
`.git` are always excluded. Open source buffers are rejected; close them first.

All document enumeration, reads and mutations use AUDMAP. The script does not
read XML files or databases directly. `--expect-root` checks the resolved vault
before scanning. Edits use native source ownership, revision validation and
atomic file replacement, not a client-side whole-tree round trip.

## Exact Scope

- A candidate must cover the entire provable body, reached only through
  singleton `document`/`concat` nodes and enclosing formatting wrappers.
- Remove `font-series=bold` assignments from qualifying `with` wrappers;
  retain other assignments, including explicit mathematical-font settings.
- Clear whole-body unary `strong` wrappers as well.
- Do not traverse quoted/macro definitions or generated document caches.
- Preserve names, handwritten punctuation, formulas, local emphasis, UUIDs,
  properties and artifact bindings. No new enunciation identity is allocated.
- An otherwise empty formatting wrapper may remain as `with(body)` when both
  it and its child carry metadata. This is intentional: it no longer sets bold,
  but both independently addressable objects survive.

Each operation commits separately. On error the script stops and reports it;
earlier successful edits remain committed. After addressing the error it can be
rerun: completed edits are no longer candidates. There is no vault-wide rollback
or automatic backup in this script. A timeout is an unknown outcome, not proof
that a pending operation was cancelled.

## Focused Check

The integration check creates a private profile/vault in `/tmp`, launches a
separate offscreen ATHENA and connects through the real Python SDK. Optional
`--sample /absolute/path/to/document.ath` arguments copy files read-only into
that vault; `--style /path/to/custom.ats` copies required custom styles into its
isolated profile. It checks dry-run nonmutation, UUID/property retention, local and
non-provable formatting, and a second no-op run. It never loads the original
sample's vault or copies its configuration/credentials.

```sh
build_qt6/audmap-client-venv/bin/python tests/interop/provable-bold-migration-test.py \
  --sample /path/to/sample.ath
```

Use a Python environment containing both dependencies for this isolated check;
changing `HOME` deliberately hides packages installed only in the user's site.
