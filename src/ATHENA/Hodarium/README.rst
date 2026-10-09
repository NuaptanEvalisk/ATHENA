ATHENA Hodarium native client
============================

The design contract is ``notes/athena-hodarium.tex``. This directory implements
native identity, control, transport and revision storage. Application wiring is
in ``src/Subsystems/Qt/QTMHodarium.cpp``; source, resource and logical-database
application lives in ``src/ATHENA/Data/hodarium_*.{hpp,cpp}``. See
``tools/hodarium/README.rst`` for independent Server and Relay deployment.

Enrollment, peer transfer, protected application, conflict submission and
explicit recovery now have application integration. This describes implemented
paths, not completed production or cross-platform acceptance. Important scope
and recovery limitations appear below.

Configuration
-------------

Open ``View -> Hodarium``; no editor buffer is required. Supply the Server's
HTTPS origin and a device name, then request admission. The protected identity
handle and initial public trust pins are persisted before the join request.
Use ``Administration`` to open the Server panel, log in with a Passkey and
approve the displayed rotating code. Membership validation follows admission;
the display code alone authorizes neither a device nor document transfer.
``Resume / retry`` resumes a profile; ``Pause`` stops it.

For an admitted profile, ``Bind Vault`` selects an existing local Vault. Its
Vaultfile is checked, not cloned or rewritten. Use the same Vault UUID on every
replica: the newly generated default is for the first binding, not a separate
identity for every copy. Duplicate and nested local roots are rejected.
Unchecking selection pauses a Vault; unbinding does not delete its files.

``Relays`` stores up to eight HTTPS origins per profile. Supply each Relay's
resource-access token on the device; it goes into Secret Service/Keychain and
only its handle enters settings. Peers need a common configured Relay to use it.
Users configure available services, not the active route; route choice is measured
and automatic. The per-Vault code-resource permission defaults to false:
received ``.ts`` styles and ``.luau`` sorters may enter the journal without being
installed. Enabling this local permission allows application of those resources.
Receipt is not an implicit grant to execute peer-supplied code.

Settings are in ``$ATHENA_HOME_PATH/system/hodarium/device.sqlite``. Membership
trust, revision journals and conflict outboxes remain device-local beside it,
not inside synchronized Vaults. Unconfigured startup does not create settings,
open the keyring or contact a Server; explicitly opening the manager can create
settings. Copying this directory is not an enrollment procedure.

Identity, authorization and recovery
-----------------------------------

Device keys, Vault secrets and Relay credentials use Linux libsecret/Secret
Service or Apple Keychain, with no plaintext-file fallback. Apple items use
WhenUnlockedThisDeviceOnly accessibility and disable synchronization. Missing or
locked credentials produce errors, not replacement identities. Identity work
runs off the GUI and BufferActor threads; Linux operations have cancellation
and deadlines. Settings contain public pins and handles, never private keys.

The HTTPS client requires TLS 1.3, normal certificate verification, bounded
requests and no redirects. It verifies original signed bytes and persists
membership rollback state before renewing a process-local lease. The offline
interval is bounded to 24 hours using steady and wall clocks. Cached membership
does not renew authorization. Network failure can leave an existing lease valid;
denial or invalid state revokes it. Suspension cancels work and revokes leases;
resume requires fresh validation. Restart starts without a valid lease.

After administrative recovery, choose ``Recover membership``. The client fetches
``GET /api/recovery/current/{nonce}``, verifies the offline authorization against
its pinned recovery key and the new authority's fresh nonce-bound response, then
asks for explicit approval. Discovery and historical proof alone never replace
trust. The approval expires two minutes after the query. Accepting it clears
membership and disables authorization; the UI requests a new admission, which
the administrator must approve. Previously accepted generations are rejected.
Admission callbacks carry their captured generation to prevent stale approval.

Vault bindings, source files, history, old journals and secret descriptors are
retained. ``client_settings::trust_database_path`` and
``revision_database_path`` provide generation-specific paths after recovery while
preserving the original generation's legacy filenames. Old trust high-water
records must not be deleted. Old-generation secrets and conflict certificates
must not be silently reused under new-generation commitments.

The application coordinator suspends the old session, cancels and joins in-flight
scan/application workers, then attempts to settle pending application intents for
configured Vault bindings through the protected resource, logical and document
application paths. A blocked operation reports an error before accepting new
trust; it is not silently discarded. After acceptance it drops the old in-memory
journal/inventory and requests fresh admission. New-generation inventory uses
saved-source baselines instead of replaying old merge certificates.

Settings schema 6 records the directly preceding generation in the same durable
acceptance transaction. ``previous_revision_database_path`` resolves only that
predecessor, including its legacy filename rule, without searching the filesystem.
After admission, the coordinator imports its confirmed applied deletions in pages
as new-member root revisions. It gates that Vault's application and peer
replication until source inventory and extra logical/resource inventory finish.
Old merge certificates are not imported as authorization for the new generation.

If the recorded predecessor journal file is absent, no old deletion baseline is
available. Initial generations have no predecessor; a recovered schema-5 profile
without recorded lineage reports an error rather than guessing one. Reconciliation
currently walks configured bindings, not abandoned/unbound Vaults recorded only
in an old journal. Retain old journals and independent backups; pin replacement
and admission alone do not prove complete destructive-recovery convergence.

The Server registers one canonical Vault-secret commitment per slot/generation.
The native registration client persists protected candidates before registration
and verifies the winner. ``peer_secrets`` delivers missing canonical secrets
over authenticated sessions for mutually selected Vaults, using signed ephemeral
requests and libsodium sealed boxes. Exact commitments and authorization are
checked; pending exchanges are discarded on disconnect. The Server stores no
Vault secret. Losing its sole protected copy cannot be repaired from a public
commitment or an administrative recovery seed.

Peer transport
--------------

Inner GnuTLS TLS 1.3 uses mutual raw Ed25519 device keys and
``athena-hodarium-peer-v1`` ALPN. The encrypted exporter-bound handshake binds
group, generation, epoch and both member instances. Per-record authorization
continues after handshake. Outer Relay TLS is not a substitute for peer identity.

Authenticated Server rendezvous supplies short-lived direct addresses and Relay
origins. Presence expires after 90 seconds and is only a routing hint.
``peer_network`` opens a listener, dials direct candidates and joins common
Relays through ``/v1/join``. Direct connections bypass system proxies. There is
no NAT hole-punching implementation, arbitrary intermediate-device routing or
Relay-to-Relay forwarding.

One application stream is selected per peer, with authenticated alternatives.
Heartbeats measure RTT; bounded 64 KiB roundtrip probes measure transfer rate
when recent application traffic warrants it. Route selection uses RTT until
throughput samples exist, then a delivery-cost estimate. Sustained advantage,
multiple samples and a cooldown are required before negotiated switching.
Battery/power-saver state disables throughput probes, not all synchronization.
Reconnection resumes revisions above the socket layer. Epoch changes and
suspension close old sessions.

Capture, receipt and application
-------------------------------

Immutable revisions form a parent DAG in a device-local SQLite journal. Parents
must be received first and belong to the same Vault/object. IDs bind descriptors
and payloads; plaintext content hashes and descriptors do not go to the Server.
Replication exchanges only mutually selected Vaults, with bounded frames,
backpressure and resumable receipt. Receiving a revision is not applying it.

Successful durable saves, including realtime saves, hand exact committed bytes
from BufferActor to Hodarium. Unsaved edits are not published. Background
inventory covers closed sources and external changes, using watches and cached
file observations. Unchanged stored content does not create another revision.
Incomplete/error scans do not manufacture deletions. Source UUIDs identify
documents across paths; duplicates are errors, not arbitrarily chosen replicas.

Application validates authorization, current heads, expected revision, source
preimage and root identity. It synchronously protects destructive preimages in
File History, persists application intents and resumes unfinished operations
before ordinary application. Confined conditional publication prevents blind
overwrite. Document renames coordinate namespace path records durably.
Clean live native documents are updated through their owning actor; live
rename/deletion uses actor retirement. Dirty buffers, changed preimages and open
AVD resources defer application. A valid membership indicator is not a claim
that every object has been applied or that all Vaults have converged.

Data scope and conflict handling
-------------------------------

Supported source is XML v2 / document model v3 with persistent identities;
Hodarium does not implicitly migrate legacy documents. AVD JSON v1 definitions
omit disposable computation cache and rebase Vault references portably; selecting
an external Vault is rejected. Resources use an explicit lowercase suffix list:
``.avd``, ``.ts``, ``.luau``, PNG/JPEG/GIF/WebP/TIFF/BMP/SVG, PDF/PS/EPS, BibTeX,
MP3/WAV/OGG/FLAC and MP4/WebM/MOV. Hidden/internal trees, operational database
files, symlinks and unknown file types are not a general mirroring surface.
Materials attachment records can explicitly declare additional suffixes or
suffix-free files. Only current local records or uniquely eligible incoming
metadata authorize those paths; unresolved received branches are not grants.
Internal databases and private paths remain excluded.

Logical adapters cover namespace definitions, declared parents, Materials records,
attachments, aliases, UUID associations, non-derived relation decisions and explicit artifact rejection
names, including structured text. Derived RAG vectors and artifact range
checkpoints transfer as immutable results, reused only with matching input/model
contracts. They do not replace source identities, model settings or scan state.
Artifact source bindings travel in source metadata. Derived namespace closure
and UUID indexes remain local caches.

No adapter copies SQLite/WAL/SHM or LMDB files. Local database paths come from
the existing Vaultfile. These are named logical contracts, not replication of
every database table. Host preferences, credentials, model weights and local
Vaultfile configuration are not automatically mirrored. Logical application may
wait for dependencies or changed preimages; publication is not a whole-Vault
atomic transaction.

Concurrent heads require explicit resolution. In ``Conflicts``, compare native
branches or an ancestor with the structural differ, which includes node metadata.
``Adopt branch`` selects its content/path or deletion. ``Edit merge`` opens an
isolated native draft; ``Submit merge`` captures it and asks for a relative result
path. ``Duplicate branch`` creates new source identities and leaves the original
conflict unresolved. Authoritative logical records use validated JSON editing;
resource conflicts support branch adoption, not a universal binary merge editor.

Submitted proposals persist exact bytes, parent set and operation ID in a durable
outbox. Secret-derived opaque tokens let the Server decide conditionally and
idempotently without content. The winner for an exact branch set is immutable;
subsequent edits are new causal revisions, not replacement decisions. A signed,
generation-scoped paginated feed delivers decisions independently of content.
Only a matching signed decision admits the merge
into applicable history. Incoming multi-parent revisions and descendants cannot
bypass verification. ``Saved drafts`` lists pending, accepted and retained losing
proposals. Native drafts open as separate copies with new identities; logical
drafts can be inspected without changing the source. ``conflict_store::proposals``
provides paginated pending/accepted/retained metadata without loading payloads;
``proposal(operation)`` loads an individual draft. Server connectivity is needed
for new decisions and newly verifying received resolutions, even while ordinary
peer traffic is possible under an offline-valid lease.

``File paths`` separately lists eligible, nondeleted physical heads belonging to
different object UUIDs at the same relative path. Its paginated query reads only
metadata, excluding logical database/history objects. ``Choose another path``
records an ordinary single-parent rename revision for the selected object and
leaves application to the protected apply path. It does not merge independent
object identities. Content conflicts within one object still use the resolution
workflow above.

Operational limits and development
---------------------------------

There is no offline content mailbox: peers must overlap online, although a replica
can forward revisions received earlier from another device. New replicas need an
existing local Vault, matching UUID and usable database configuration; the manager
is not a remote-clone wizard. File History provides a paginated Protected vault
history view for native documents, resources and explicitly typed logical records,
with preview, export and restoration. Restoration creates new local state for
ordinary synchronization; it does not rewind replication history. Missing parent
directories are recreated through confined descriptors. Old untyped logical
snapshots remain export-only. There is no dependency-ordered bulk disaster restore
wizard; restore logical dependencies before their dependents. See
``src/ATHENA/Data/hodarium_history.rst`` for the protection and restore contract.

Apple credential and lifecycle implementations exist, but Linux component checks
do not demonstrate current iPad device acceptance or execution while suspended.
The current UI and code are not evidence that every platform, long-running
convergence or fault-recovery case has passed. Use isolated Vault copies for
release verification and keep independent content backups.

The normal Linux build is::

  cmake --build build_qt6 --target ATHENA.bin -j20

Do not invoke the default all-target build or complete test suite for this module.
Focused checks are under ``tests/hodarium``. Secret Service tests must use
``isolated-keyring.sh``; ``authority-smoke.sh`` and ``relay-smoke.sh`` launch
isolated HTTPS services. The Go ``TestNativeRecovery`` fixture accepts a focused
native test binary via ``ATHENA_HODARIUM_NATIVE_RECOVERY``. Component checks do
not replace editor, iPad or destructive-recovery acceptance.
