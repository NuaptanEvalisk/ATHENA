# Hodarium Logical Database Adapters

These adapters exchange logical objects, never SQLite files, WAL files, LMDB
pages, scan state or task queues. Run capture and application on the serialized
Hodarium worker with its captured Vault root and revision journal. They do not
access GUI widgets or live editor trees.

## Authoritative Objects

| Format | Object key | Source |
| --- | --- | --- |
| `athena-namespace-v1` | `namespace/<uuid>` | Namespace definitions, declared parent UUIDs, Materials membership and portable sorter/style/initial/homepage paths |
| `athena-namespace-relation-v1` | `namespace-relation/<parent>/<child>` | Explicit non-derived relation decisions |
| `athena-artifact-rejections-v1` | `artifact-rejections` | Plain and structured rejected artifact names |
| `athena-material-v1` | `material/<uuid>` | Bibliographic fields, creators, identifiers, tags and portable provenance |
| `athena-material-attachment-v1` | `material-attachment/<uuid>` | Attachment identity, owning Material, relative path and content digest |
| `athena-material-relation-v1` | `material-relation/<subject>/<relation-hash>/<object>` | Bibliographic relations |
| `athena-material-alias-v1` | `material-alias/<uuid>` | Canonical Material UUID after a merge |

Payloads are bounded, compact, versioned JSON arrays. Use
`validate_logical_object()` before accepting a user-edited merge. Unknown
formats and versions are rejected. The authoritative formats support independent
revision histories; rejected names use an explicit empty list rather than a
tombstone. Missing databases and incomplete exports are not mass deletions.

Current artifacts storage has no separate manual-correction table. Artifact
names, categories, proof relationships and `athena:artifact-bindings` originate
in source trees and travel through native document revisions. The artifact
index, extraction records and identity-matching audit are not additional
authoritative identities. Do not reinterpret derived checkpoints as manual
corrections or invent such a database surface.

Materials publication uses native store transactions and verifies the expected
logical payload. It retains local revisions/search state and local provenance;
Zotero `localServerId` is not transferred. Attachment bytes belong to the
resource adapter. Metadata application waits for the owning Material and
matching confined attachment bytes. Missing dependent records defer rather
than partially cascade a deletion. An attachment outside the resource adapter's
supported inventory cannot be claimed as synchronized merely because its
metadata exists.

## Derived Objects

`athena-rag-vector-v1` carries an embedding-space contract, input fingerprint,
dimension and canonical little-endian vector. `athena-artifact-range-v1` carries
source/artifact UUIDs, extraction role, input/content/structure fingerprints and
range offsets. Journal identities additionally include the payload digest;
different valid results remain immutable alternatives, not competing manual
edits. Derived objects have neither parents nor tombstones.

Vector application requires a matching local embedding-space contract and input
reference. Range application only installs a reusable checkpoint. The native
extractor revalidates the model/input fingerprint and candidate offsets before
using it; application never replaces source attributes or an already selected
range. The range-model contract uses model-content SHA-256 rather than machine
paths or timestamps. Existing older machine-dependent fingerprints are not
relabelled as equivalent without recomputation. A device without a matching
model/input contract does not consume a checkpoint under a different contract.

## Capture Scheduling

`capture_logical_databases(root, journal, vault, member, gate)` captures only
authoritative records. Its wakeup inputs are Vaultfile, namespace database,
rejected-name file and Materials database, including relevant WAL files.

Keep one `derived_database_capture_state` per binding and call
`capture_derived_databases(root, journal, vault, member, state, gate)` separately.
Its wakeup inputs are Vaultfile, RAG, artifacts and bold-text databases and WALs.
Both operations share the same serial worker as file capture/application.

RAG initialization creates local disposable `hodarium_embedding_changes` and
`hodarium_embedding_contracts` tables with SQLite triggers. Existing vector keys
are seeded once. Insert, update and replacement enqueue a fresh monotonically
increasing sequence; deletion removes its key. Identical embedding-space
replacement does not requeue the whole space. The queue contains at most one
entry per live embedding key. Its indexed read is:

```sql
SELECT sequence,space_id,input_hash
FROM hodarium_embedding_changes
WHERE sequence > ?
ORDER BY sequence LIMIT 64;
```

Only those keys fetch vector BLOBs for encoding/hashing. Advance the in-memory
cursor only after journal capture succeeds. A restarted binding may replay the
initial inventory safely; an active binding does not serialize the entire
vector store on each WAL write. These tracking tables are local operational
data, not replicated logical objects.

Range capture checks artifacts/bold-text database stamps first, then compares
small raw records against the worker-owned cache. Unchanged records do not
enter JSON encoding, payload hashing or journal lookup. This remains a bounded
page scan of lightweight rows when the artifact database actually changes; it
is not a persistent range-change cursor.

## Application And History

`apply_logical_revision()` uses explicit format/model validation, expected
payload comparison and a durable apply intent. Authoritative preimages use
`protect_logical()` with the known prior revision's format and object key.
Both byte protection and metadata binding must succeed before mutation.
Resumption reconstructs and checks the protected bytes, verifies the prior
revision, and requires immutable metadata registration before proceeding.
Neither JSON shape nor array length determines the history object's type.

The original payload bytes remain unchanged for fingerprint verification.
Namespace publication invalidates the active ontology through its thread-safe
native invalidation entry point. Missing dependencies and stale expected values
defer publication. Successful transactions are verified before finishing the
journal intent.

## Verification Boundary

Focused regression cases are supplied in
`tests/ATHENA/Data/hodarium_logical_database_test.cpp`. The SQLite cursor behavior
and query plan were checked in an isolated in-memory database. Final combined
application build, integration tests, UI recovery checks and production-Vault
acceptance are deliberately deferred to the owner's joint verification phase.
No production Vault migration or test write is part of this adapter work.
