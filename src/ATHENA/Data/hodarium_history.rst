Hodarium application and File History
====================================

Document application
--------------------

``apply_closed_document`` and ``apply_open_document`` accept the callbacks from
``make_document_rename_records(root)``. A cross-path application cannot proceed
without both callbacks. The application journal retains its intent until the
namespace CAS, artifact lineage update, history path update and filesystem
durability checks complete. The callbacks capture only the canonical Vault
root; connections and native working data belong to each invocation.

``.athena/hodarium-rename-records.sqlite`` binds an immutable operation to its
source object, old/new paths, namespace database and logical preimages. The
prepare callback protects those original records before destructive file
publication. The commit callback uses the logical namespace adapter, recognizes
an interrupted successful CAS by exact replacement records, and rejects newer
namespace edits. Resource-path rewriting belongs to
``rename_namespace_object_resources``; the coordinator does not replace strings
inside serialized JSON. Namespace ontology invalidation uses the existing
thread-safe queue and only targets the matching active Vault.

New document paths use the shared ``confined_root::ensure_parents(path, gate)``
before publication. Missing parent directories are created within the confined
Vault root, with the same application gate and directory durability checks as
resource synchronization; no separate document-specific directory creator is
used.

A clean open document is checked again by its BufferActor. Same-path updates
adopt the saved source. A deletion or move freezes queued edits before retiring
the old source view through the GUI close path. Dirty buffers defer; they are
not saved, discarded or renamed over another buffer by synchronization.

Typed protected snapshots
-------------------------

History schema version 3 adds ``protected_metadata``. Its primary key refers to
a protected version, and its immutable values are the logical format and object
key. They are not inferred from the payload, array arity or a filename.

``protect_logical(path, bytes, operation, format, key, version, error)`` performs
both the existing synchronous byte-protection barrier and the metadata barrier.
The caller must stop if either fails. ``set_protected_metadata`` supports
idempotent recovery from a known revision or immutable rename plan and rejects
a different identity for an already-bound version. ``protected_metadata``
returns an optional identity; absence is not authorization to guess one.

Payload bytes are unchanged. Revision fingerprints and persisted application
intents therefore continue to refer to the same original content. A crash
between protection and metadata registration may leave an untyped snapshot,
but cannot authorize source application. Normal retention cannot remove the
protected snapshot. Old untyped logical snapshots remain exportable but cannot
be applied as logical records by the recovery UI.

User recovery
-------------

Document History offers a current-document view and a paginated, Vault-wide
``Protected vault history`` view. The latter requires neither a surviving file
nor an open buffer. Rows retain the source path and version identity. The user
can open a historical document, inspect resource/JSON bytes, export an exact
copy to a new filename, or restore a selected version. Export never overwrites
an existing file or an open buffer's destination.

``qtm_document_history_restore_protected(root, version, diagnostic)`` checks the
active Vault and reads the protected snapshot's explicit identity. Logical
restoration admits formats through ``logical_revision_format``, validates them
with the native logical adapter, protects the current value, and uses a
current-value CAS. Namespace records, relations, artifact rejections and the
Materials record families use their respective adapters. Derived caches do not
become authoritative source records through history recovery.

Ordinary document restoration reuses the existing editor/actor history path.
Deleted documents and closed resources use confined create/conditional replace;
the current bytes are protected before replacement. Open resources must first
be closed. A Vault switch, stale preimage, missing logical dependency or failed
durability barrier produces a diagnostic instead of a successful restoration.

Restoration creates new local state, not a rollback of Hodarium's applied
revision. Normal saved-source/logical inventory capture publishes that state.
Recovery does not directly manipulate server decisions or replication cursors.

In-place recovery recreates missing parent directories through the shared
``confined_root::ensure_parents(path, gate)`` API. Directory creation remains
under the publication lock and active-Vault/open-buffer gate, using confined
directory descriptors and synchronization rather than an unrestricted path
creation helper. Conditional replacement and create-only publication still
reject a changed or newly occupied destination.

Logical dependencies must be restored before their dependents; the interface
does not yet provide a dependency-ordered bulk restore wizard. Resource previews
are bounded to 64 KiB, while exports contain the complete original bytes.
