ATHENA Hodarium implementation
=============================

The contract is ``notes/athena-hodarium.tex``. This directory is the native
client/control core, not a replacement contract or a declaration of completion.
The editor, authority server and relay remain distinct ownership boundaries.

Implemented foundation
----------------------

``revisions`` stores immutable revisions and their parent DAG in SQLite with
foreign keys, transactional receipt and FULL synchronous durability. Parent
receipt precedes child receipt, so untrusted ordering cannot introduce cycles.
Parents must belong to the same vault and object. Concurrent heads survive;
receipt never applies content. Application bookkeeping uses an expected-current
revision and permits only causal advancement. Merge-base queries return all
maximal common ancestors, including ambiguous criss-cross histories.

``tools/hodarium`` now contains the independent Go authority and an embedded
React/Mantine administration panel. It implements local bootstrap, verified
Passkey enrollment/login, signed membership publication, proof-bound device
admission, expulsion, audit and persistent admission-code budgets. Configured
native clients now connect through the application worker. The recovery CLI resets administrative
credentials and member instances through proof from the offline recovery key;
native client acceptance of recovery still requires implementation.

``membership`` verifies the authority's original signed payload bytes and
nonce-bound validation responses using libsodium. Its separate device-local
SQLite trust store pins the group, authority and recovery generation, remembers
accepted revisions/epochs, and rejects rollback or conflicting publications.
A process-local lease starts expired, uses both steady and wall clocks, and
requires explicit revocation on platform suspension. Reading a stored member
list does not renew freshness. The future network owner must consume each
pending validation request once, persist its accepted state, then renew the
lease from the request's send time. The native authority client below now owns
that sequence; remote document application remains unconnected.

``device_identity`` creates a random local key handle and Ed25519 identity,
stores its seed through the platform backend, and reloads it for each signing
operation. Only handles and public keys belong in device configuration.
Linux uses libsecret (LGPL-2.1-or-later) and the default Secret Service collection;
the build now requires ``libsecret-1`` development files. Locked collections are
not silently unlocked, duplicate matches are rejected, and missing keys do not
cause replacement-key generation. The Apple backend uses Security.framework
Keychain generic-password items with WhenUnlockedThisDeviceOnly accessibility
and synchronization disabled. No plaintext file backend exists.

Calls must run on an identity worker, not a GUI or BufferActor thread. Seeds
and derived private keys use libsodium guarded allocations that are cleared
on release. Public-key equality is checked before signing with an existing
handle. Linux storage/signing/lock behavior has been exercised against a real
gnome-keyring daemon on an isolated D-Bus session and temporary HOME; the user's
keyring was not accessed. Linux Secret Service operations now carry a
15-second per-operation cancellation budget and can be cancelled from the app
thread before suspension/shutdown. Apple Keychain compilation/device verification
is still pending; its noninteractive queries have no cancellable prompt API.

``authority_client`` is an event-loop-owned Qt HTTPS client for an already
admitted device. It gets a fresh challenge, obtains the protected-key proof,
verifies the nonce-bound validation and commits its state before renewing the
process-local lease. It requires normal TLS certificate validation and TLS 1.3,
does not follow redirects, caps responses, and bounds network requests. Network
failure does not renew or erase an otherwise valid offline lease; denial or
invalid state revokes it. Suspension cancels in-flight responses and revokes
authorization. Peer membership checks require current lease and matching epoch;
they do not replace the future transport's proof of peer private-key possession.
Construct this client on its owning worker, not on the application thread.

``enrollment`` now implements the native join/proof/poll flow and returns the
rotating display code to its future UI owner. Enrollment and refresh share
``control_http`` for TLS policy, deadlines, cancellation and resource budgets.
The same transport discovers the HTTPS authority's protocol, group, signing key,
recovery key and generation through ``/api/info``. Discovery rejects malformed
identities, duplicate JSON fields and unsupported protocols; it never updates
stored trust or grants a membership lease.
The owner must persist the protected identity handle and initial authority pin
before joining. Pending retrieval credentials stay in memory. A restarted
enrollment can resolve its active member ID by fresh private-key proof, without
replaying the one-use approval result or acquiring authorization by that lookup.
It still needs a successful membership validation before peer traffic.

``client_settings`` persists device-local enrollment in a separate FULL/WAL
SQLite database outside synchronized Vaults. Profiles retain the HTTPS origin,
authority/recovery pins, protected key handle, public device key and member ID.
They contain neither private keys nor pending retrieval credentials or lease
deadlines. Pending identities commit before admission requests; completing
admission conditionally matches the stored key handle/public key and cannot
silently replace an existing member. Only admitted profiles can be enabled.
Network authorization remains independently expired on process restart.

``profile_session`` owns each configured group's enrollment/validation state on
its worker. Enabled admitted profiles refresh every minute; pending admissions
poll every ten seconds while an in-memory retrieval credential exists. Expiry
checks use the process-local lease, and suspension cancels requests and revokes
authorization. Resume forces a fresh online validation. Disabled/unstarted
pending profiles do not keep a polling timer running. The observer publishes
control-plane status, not a claim that documents are synchronized. This worker
controller has been checked through an isolated real authority exchange,
including suspend/resume. Observer callbacks must queue UI updates and must not destroy the
session synchronously from its completion stack.

``QTMHodarium`` connects configured profiles to application startup on a dedicated
Qt worker. It reads ``$ATHENA_HOME_PATH/system/hodarium/device.sqlite`` only if
that file exists; unconfigured startup creates no database, opens no keyring and
makes no request. Membership databases remain beside that device-local settings
file, not inside Vaults or disposable caches. Linux logind sleep notifications
and iPad application state transitions suspend/revalidate the sessions. Shutdown
cancels blocking key-store work before stopping and joining the worker. Diagnostic
events are queued to ATHENA's standard error/warning channel on the UI thread.
The application-owned View / Hodarium dialog requests admission, displays the
rotating admission code and membership status, opens the authority's admin
panel and pauses/resumes configured profiles. HTTPS discovery and protected
identity creation run on the worker; the pending profile is durable before the
join request. Existing group pins are never overwritten by another discovery.
The dialog creates device-local settings on explicit opening, and does not
require an editor buffer. Selected Vault journals now exchange revisions over
authenticated direct connections; remote document application is still pending.

The real C++/Go HTTPS exchange was exercised with a separately trusted temporary
TLS certificate and an isolated system keyring. The native client requests
admission, the test approves through the real administrative Admit path, and
the client polls, retrieves its member ID, recovers that ID from a fresh
enrollment object, commits device settings, reopens those settings, then validates
membership. The native dialog has not yet been exercised on iPad. Reproduce with::

  cmake --build build_qt6 --target hodarium_authority_client_test -j20
  GOPATH="$(go env GOPATH)" GOCACHE="$(go env GOCACHE)" \
    bash tests/hodarium/isolated-keyring.sh /usr/bin/bash \
    tests/hodarium/authority-smoke.sh \
    build_qt6/tests/hodarium_authority_client_test "$PWD"

Revision IDs are SHA-256 over a domain separator, canonical descriptor CBOR
and payload bytes. The descriptor binds vault/object IDs, origin member,
format and semantic version, path, deletion state and sorted parent set.
These descriptors and IDs are private peer protocol data: never publish them
to the authority or relay, where predictable content could be disclosed.
Signatures and authenticated sessions are separate from integrity hashing.

``peer_tls`` implements inner, nonblocking GnuTLS TLS 1.3 with mutual raw Ed25519
public-key authentication and mandatory ``athena-hodarium-peer-v1`` ALPN.
It pins both device keys directly, rather than trusting a relay certificate as
peer identity. GnuTLS's external-key callback signs through the protected
identity service; no device private key is exported into the transport.
Session tickets and early-data use are not enabled. The byte transport callbacks
can later wrap a direct socket or an opaque relay stream, and must return
EAGAIN rather than block on network I/O.

Before ``handshake()`` completes, both peers exchange an encrypted, exact JSON
array binding the protocol, group, recovery generation, epoch, ordered client
and server member instances, sender role and RFC 9266 TLS exporter. All IDs
have validated fixed lengths; the locally computed expected frame bounds the
receive buffer without accepting a peer-supplied length. No application record
API is available before this comparison succeeds. A required owner callback
checks authorization at handshake and each record read/write; it must validate
both identities and the exact context against a fresh membership lease.
Any failed handshake becomes terminal. The eventual connection coordinator must
also bound handshake time and cancel sockets immediately on suspension/expiry.
``profile_session`` produces peer contexts from verified membership and provides
the matching authorization predicate. Per-record authorization checks do not
copy or scan the membership list: the authority client builds a member-key index
when accepting a publication, and checks its current context and lease in place.

The protected-key smoke test exercises a real bidirectional GnuTLS handshake
over in-memory byte queues, matching exporters, encrypted record receipt,
wrong-device rejection, epoch mismatch and revocation before a record write.
This primitive check is distinct from the socket, relay and journal replication
checks described below; it does not itself prove document application.

``peer_connection`` now drives the inner TLS primitive over a shared byte-transport
interface, implemented by owned QTcpSocket and Qt WebSockets connections
on a Qt network worker. It admits application writes only after mutual device
and membership-context authentication. It bounds pending plaintext to 1 MiB,
socket buffers to 256 KiB and work per pump; a writable callback
allows the transfer layer to resume after backpressure. Read callbacks expose a
byte stream rather than assuming TCP/TLS record boundaries are document frames.
Handshake is bounded to 30 seconds, record inactivity to 90 seconds, and a
one-second membership timer closes an idle connection after authorization loss.
Explicit suspension must also close the connection immediately. Closure aborts
the socket and drops unsent data; revision resume belongs above the transport.
The isolated keyring test now sends 700 KiB across actual loopback TCP, checks
queue refusal above budget and revokes authorization after receiving the exact
payload. This is not yet an enabled Vault synchronization service.

``direct_peer_listener`` supplies the bounded incoming routing boundary. A
fixed-size versioned prelude identifies the group, generation, epoch and both
member instances. An owner resolver checks that context and chooses the expected
device key before handing the socket to inner TLS; the prelude is never treated
as proof of identity. Unrecognized contexts are disconnected. At most 16 sockets
may await a prelude, each with a five-second timeout and a header-sized read
buffer. The receiver owns accepted sockets and must separately limit concurrent
TLS sessions. ``connect_direct_peer`` queues the matching prelude before the
TLS ClientHello. The loopback check now uses this actual routing boundary,
including a rejected unknown context.

``peer_network`` now owns the direct listener in each validated application
profile, publishes up to eight active unicast interface addresses and consumes
the presence directory to dial peers. It caps live/handshaking links at 16 and
simultaneous outgoing attempts at four. Failed attempts rotate through advertised
addresses with a 30-second retry delay. Direct sockets explicitly bypass system
proxies; relays remain a separate transport. Simultaneous dialing converges by a
stable member-ID tie-break, without forbidding either endpoint from initiating
an otherwise absent connection. Epoch changes and suspension close all links.

The encrypted channel carries length-bounded CBOR frames, decoded by Qt CBOR,
with 512 KiB per-frame limits. Random-nonce heartbeats measure actual peer RTT
and maintain idle sessions; failure to answer within 30 seconds closes the link.
Application payloads have a separate frame kind and require an installed
consumer. The application currently installs no document consumer: receiving
document data is rejected, never applied. The isolated two-device check exercises
automatic simultaneous dialing, authenticated channels, heartbeat RTT, 700 KiB
revision transfer and network shutdown. Relay integration, throughput probing and
measured route switching are still pending.

The relay adapter requires Qt WebSockets, now part of the native dependencies
and the iPad Qt build recipe. It validates normal outer TLS, negotiates the relay
subprotocol and passes only ciphertext from the same inner TLS connection used
by direct peers. Ticket allocation uses the bounded control HTTP transport,
with access tokens in an Authorization header, never in URLs. Incoming frames
and messages are capped at 64 KiB, pending receive bytes at 256 KiB. Binary
messages drive the TLS consumer immediately with reentrancy protection, so Qt's
burst delivery does not enqueue an entire transfer before consuming it.
Text frames and oversized queues terminate the connection.

A separate Go TLS relay process and native C++ client passed an isolated
700 KiB encrypted roundtrip with device-key and membership-context handshakes.
The test trusts a temporary test certificate, without bypassing TLS verification.
Use ``tests/hodarium/relay-smoke.sh`` with the isolated-keyring launcher and the
``hodarium_identity_test`` binary. Production relay configuration, capability
exchange, automatic route selection and secure persistence of relay resource
credentials remain to be integrated. The new Qt module has not yet been built
in the iPad VM.

The caller owns the store on one worker thread. The database pathname is local
configuration, never supplied by a peer. ``record_applied`` is bookkeeping,
not a filesystem replacement API: only a future durable application coordinator
may call it after history protection and the application commit boundary.

Journal integration and remaining application work
-------------------------------------------------

``revision_store::capture_saved`` atomically records a successful local save and
its applied pointer. Its input has no caller-selected revision ID or parents;
only the expected applied revision becomes its parent. Received remote heads
are not silently merged. A stale expected base publishes nothing, and a pending
remote apply intent blocks capture until the owner resolves that operation.
Unchanged storage content returns the previous revision without inserting a
new one, including when the saving device differs. The comparison hashes the
new bytes against the old descriptor without loading the old payload. Renames
and tombstones remain real revisions. The caller must supply exact bytes from
a completed durable save.

Save notifications can additionally bind the storage fingerprint preceding the
save. With an existing applied revision, a differing predecessor rejects the
capture without publishing a new revision. The actor notification supplies
that fingerprint, captured before its storage transaction, rather than
assuming the worker's latest applied pointer was the actor's editing base.
Revision schema v4 caches raw payload SHA-256 separately from descriptor-bound
revision IDs. New receipts and saves populate it atomically; old records are
backfilled on first use. The pre-apply completion check shares this cache, so
neither check repeatedly reads an immutable large payload.

The native document save result now carries ``committed_bytes``, an immutable
shared pointer to the exact serialized bytes supplied to the atomic writer.
Creation, ordinary XML saves and legacy upgrades all propagate it. Allocation
occurs before publication, and retaining the result across subsequent saves
does not change its bytes. A Hodarium save notification can therefore retain
this snapshot without reparsing an actor tree or reopening a path that might
already contain a later save. The result's durability still controls whether
the bytes represent a successful save; a replaced-but-not-durable result must
not be published as one.

Device settings schema v2 stores explicit Vault UUID-to-local-directory bindings
outside synchronized data. Membership remains device-wide; selection is local.
The native Hodarium manager can bind an existing Vault (validating its Vaultfile
without rewriting it), reuse a supplied UUID for another replica, pause its
selection and unbind it. A newly generated UUID identifies a new synchronized
Vault; replicas must use the same UUID, not independently generate one each.
Binding requires an admitted profile, canonicalizes the selected directory and
rejects duplicate or nested roots across local bindings. Unbinding never removes
Vault files.

Selected Vaults now capture successful native XML v2 BufferActor saves into a
device-local per-Hodarium revision journal. Ordinary and realtime saves share
the durable-success branch. Source body UUIDs identify objects; the snapshot
contains exact committed bytes and the predecessor storage fingerprint. The
actor queues immutable bytes without accessing GUI registries or opening the
journal. The control worker owns settings and journal access. Network pause
does not discard local save history. Internal .athena/.backup/.git paths are
excluded; unselected and outside-Vault documents are ignored by the worker.
When a known object's saved path changes, the previous path is checked through
the confined filesystem. If it still exists, capture reports a source-UUID
conflict instead of interpreting an external copy as a rename. Permission and
other read failures are not treated as absence. Initial inventory also excludes
every readable file involved in a duplicate source-body UUID, rather than
arbitrarily retaining the first one.

The pending byte handoff is bounded to 1 GiB and shutdown drains queued saves
before closing journals. Overflow or capture failure uses standard diagnostics
without converting an already durable editor save into a failed save. Durable
outbox recovery remains incomplete: a crash before
the worker's transaction, a rejected notification or edits while ATHENA is
closed must be reconciled from saved files. No peer can apply files yet, and
received revisions remain separate from the document's applied state. Selection
therefore does not yet provide end-to-end document synchronization.

Startup and Vault-selection changes schedule read-only initial inventories on
an independent native task. Existing confined inventory excludes internal trees;
the XML v2 reader validates source envelopes and requires a persistent body UUID.
Only bytes, IDs and stat revisions cross back to the control worker. The worker
revalidates each file before publishing in bounded event-loop slices; changed
snapshots schedule another inventory. Aggregate held snapshots have a 4 GiB
budget and normal codec limits still apply per file. Cancellation never mutates
source documents. Identical imported replicas use the deterministic origin
``saved-source-baseline-v1`` for genesis; subsequent editor saves retain their
actual member provenance. Offline edits to known objects continue the local
applied branch. Source watches now trigger subsequent inventories. Linux polls
the existing inotify source-event stream without walking an unchanged Vault;
other platforms use its 30-second periodic fallback. A per-binding in-memory
cache skips XML reads and hashing when stat identity/revision is unchanged and
publication previously succeeded. All cached IDs still participate in duplicate
detection. Failed/unpublished rows are not considered complete, and failures
retry after 30 seconds. Changes during a scan remain observable for another
round. Scan-time deletions, persistent scan cursors and a durable local outbox
are not yet implemented.

The settings layer accepts only explicit local paths, never routing hints or
peer-provided paths. The eventual runtime must reopen and validate the binding
before access, and handle unavailable/moved roots and iPad container relocation.

``document_history_store::protect`` supplies a synchronous pre-apply history
barrier in the existing File History database (schema v2). It switches its
connection to FULL synchronization and commits a standalone full snapshot and
operation binding in one transaction. Retrying an operation with the same
original path and bytes returns its existing version; different source bytes
are rejected. Ordinary retention excludes protected rows, and the History pane
marks them as protected. The snapshot has no Fossil-delta dependency that could
be pruned. Rename updates the displayed history path without changing the
original operation binding.

This is the durable backup primitive, not yet the remote-apply coordinator.
The owner still must use the application-intent API, check live/disk revisions,
perform the conditional replacement and recover interrupted application before
advancing the applied revision. It must not infer that invoking an asynchronous
ordinary history capture has provided this barrier.

Revision schema v3 records operation-bound application intents, their expected
applied revision, protected History version and source fingerprint. Pending
intents block bypass through ``record_applied``. Completion verifies the target
fingerprint (or absence for deletion), then advances ``applied`` and completes
the intent in one transaction. These APIs do not themselves inspect the
filesystem, verify the separate History store or coordinate a BufferActor.

The revision store's schema v2 adds durable incoming transfers, without changing
existing revision identities or applied pointers. ``begin_receive`` accepts a
payload-free descriptor and total byte count; matching retries return the last
committed offset. ``receive_chunk`` writes at that exact offset with SQLite's
incremental BLOB API, committing bytes and offset in the same FULL-synchronous
transaction. Chunks are capped at 256 KiB, individual staged payloads at 512 MiB,
and pending transfers at 32 / 2 GiB total. The protocol owner must authorize the
Vault before accepting a descriptor; the store is not an authorization service.

``finish_receive`` requires every byte and all same-object parents, hashes the
descriptor and content in 64 KiB blocks, then atomically publishes the immutable
revision and removes staging. It never changes ``applied`` or a document file.
Conflicting descriptors, wrong offsets and bad hashes fail without publishing;
explicit discard removes only an incomplete transfer. Metadata-only offers and
bounded outgoing BLOB reads avoid materializing entire documents for transport.
The focused revision test checks binary-content receipt across database reopen,
idempotence, corrupt content rejection, empty tombstones and unchanged applied
state.

``revision_sender`` and ``revision_receiver`` implement versioned CBOR offer,
chunk, finish and acknowledgement messages using Qt's CBOR codec. Each sender
has one outstanding request, advances only after the matching durable offset
acknowledgement and finishes only after the immutable revision is committed.
A fresh sender/receiver pair resumes by reoffering the same descriptor. Vault
authorization is mandatory on both sides and is checked again for every
outbound step and incoming chunk/completion. A peer cannot send a chunk for an
ID it has not offered on that connection. Receipt controllers hold at most 32
accepted offers; protocol messages are bounded to 512 KiB and payload chunks
remain within the store's 256 KiB limit.

The isolated native peer-network check transfers a 700 KiB binary revision over
automatic direct dialing and real mutual TLS, including per-chunk durable
acknowledgements and content verification. Separate revision checks exercise
lost acknowledgements, controller reconstruction, resume offsets and revoked
Vault grants.

``vault_revision_sender`` supplies one direction of causal discovery and
transfer. It captures an append-only receipt-order ceiling, pages through heads
as of that ceiling, and probes the receiver before traversing missing ancestry.
Known heads prune their entire ancestry; missing parents are transferred before
children using the existing resumable protocol. Concurrent heads and tombstones
are preserved. New receipts beyond the ceiling belong to the next round.
Inventory queries use indexed metadata only, never document payloads. Traversal
is iterative and bounded; discovery and transfer recheck Vault authorization.

The completed cursor is connection-local: subsequent rounds on the same peer
store inspect only newly received revisions; reconnect or database restoration
requires rediscovery from zero. Do not persist this cursor as evidence that a
different or restored peer still has data. A fresh exchange against an unchanged
peer probes just its heads. Both directions must run to exchange concurrent
work; successful receipt still does not apply content to the user's Vault.
Focused checks cover multi-page discovery, concurrent branches, a merge received
during discovery, incremental rounds, known-head pruning and revoked grants.

``revision_exchange`` multiplexes two independent directions on one authenticated
connection. Its versioned CBOR envelope binds each request/response to a Vault
and a directional sequence. Unsolicited, replayed, mismatched and pipelined
requests are rejected. At most one outgoing request and one incoming response
are queued, with transport backpressure handled by separate ``outgoing`` and
``sent`` operations. Only transport acceptance removes a queued message, and
authorization is rechecked before queued bytes can be sent. Inner descriptors
must agree with the envelope's Vault. A new connection gets a new exchange;
session-local discovery cursors must not survive its peer connection.

The native automatic-dialing TLS check now uses two exchanges concurrently:
each peer starts with a different child of a shared revision, sends both ways,
and converges on the same two heads without applying either branch. One branch
contains a 700 KiB binary payload. It runs under an isolated temporary keyring,
not the user's device identities or production Vault.

``peer_replication`` now connects selected local journals to authenticated direct
sessions in the application worker. Peers announce only their selected Vault
IDs inside the encrypted channel and exchange the intersection. No local paths
appear in this announcement. A monotonically assigned connection incarnation
resets discovery state on reconnect or membership-context replacement. Changing
the local Vault selection closes old sessions before using new grants.
The scheduler respects transport backpressure, runs one outgoing Vault round
per peer at a time, and checks for new journal revisions every two seconds.
Unresponsive discovery/active exchanges time out; errors use the profile's
standard diagnostic path. The native TLS check uses this scheduler with
different selections on the peers and verifies that an unshared Vault does not
transfer. Remote file application, deletion reconciliation, Relay route selection
and logical database adapters remain pending.

``rendezvous_client`` now publishes, queries one bounded page and withdraws
presence through the Go authority. Each request signs the SHA-256 digest of
its exact JSON payload using a fresh challenge and the protected device key.
An owner-supplied membership predicate checks the exact group/member/generation/
epoch before sending and after each response. Cancellation discards pending
HTTP work; the whole challenge/request operation has a 30-second deadline.
Response parsing rejects duplicate fields, excessive depth, mismatched contexts,
invalid route addresses and nonmonotonic pagination. Presence is only a routing
hint and does not renew a lease or authorize a peer connection.

``presence_directory`` is attached to each validated application profile. It
publishes configured reachability and atomically replaces its candidate list
after traversing ordered pages (at most 4096 members). A cycle is bounded to
60 seconds; refresh and retry run every 30 seconds. Candidate expiry uses both
the advertised wall-clock timestamp and a 90-second monotonic ceiling measured
from the cycle's start. Epoch changes, suspension, disable and membership expiry
discard old candidates and cancel in-flight discovery. Suspension never waits
for withdrawal: server presence expires independently. These records are memory
only and do not read or write a Vault. Unconfigured profiles advertise no direct
or relay addresses. Application profiles supply the actual direct listener's
interface addresses after membership validation; no relay route is invented.

Discovery diagnostics are distinct from membership authorization and are
reported through ATHENA's standard warning channel. The native/Go authority
check publishes native presence, discovers a second admitted device's direct
and relay addresses, withdraws and exercises automatic profile discovery across
suspension/resumption. Measured route selection and Relay connection
establishment are still pending. Direct connections are
now managed by ``peer_network`` as described above.

* Wire Vault selection and synchronization status presentation, and verify the
  enrollment UI on iPad.
* Verify the Apple backend and complete explicit recovery trust transitions.
* Integrate the working native relay transport into application route management;
  complete client recovery trust, conflict decisions, deployment
  and notifications. Extend the existing
  authority/panel rather than replacing their WebAuthn implementation.
* Connect socket/relay sessions to rendezvous, revision transfers, automatic measured route selection,
  reconnection and iPad suspension/resumption.
* Save/discovery integration, stable source object mapping and logical database
  adapters; no raw SQLite/LMDB replication.
* Durable history barriers, protected retention, application intents/recovery,
  source validation, conditional BufferActor application and batch restore.
* Metadata-aware merge UI, authoritative conditional conflict decisions,
  private control commitments and persistent control cursors.
* Client preferences/status, Linux/iPad builds and isolated multi-device tests.

Dependency investigation
------------------------

Existing SQLite, nlohmann JSON and libsodium are reused for this foundation.
No new cryptographic algorithm or document parser is introduced. Existing
GnuTLS supports TLS with custom transport callbacks, a candidate for keeping
peer TLS end-to-end through opaque relay streams. Existing delegation key-file
storage does not satisfy Hodarium's credential contract and will not be reused.
libsecret development headers are available on the Linux workstation.
SimpleWebAuthn (Node) and go-webauthn (Go, BSD-3-Clause) were investigated for
WebAuthn. Prefer Go for the independent server and relay binaries, using
go-webauthn for server-side verification and framework components for the web
panel. The workstation has Go 1.26.8. React/Mantine (MIT) can provide the panel's
forms and components with a shared zero-radius theme. Coder WebSocket (ISC)
is a candidate for opaque relay streams, not a replacement for peer TLS.
GnuTLS's client-side custom transport callbacks can carry inner peer TLS
through these streams. Pin versions and verify wire interoperability before
shipping; do not hand-write WebAuthn verification or TLS.

References:

* https://doc.libsodium.org/public-key_cryptography/public-key_signatures
* https://www.sqlite.org/pragma.html#pragma_synchronous
* https://www.sqlite.org/c3ref/blob_open.html
* https://www.sqlite.org/c3ref/blob_write.html
* https://www.gnutls.org/manual/gnutls.html
* https://www.gnutls.org/manual/html_node/Raw-public_002dkey-credentials.html
* https://www.gnutls.org/manual/html_node/Channel-Bindings.html
* https://gnome.pages.gitlab.gnome.org/libsecret/
* https://gnome.pages.gitlab.gnome.org/libsecret/method.Service.search_sync.html
* https://developer.apple.com/documentation/security/ksecattraccessiblewhenunlockedthisdeviceonly
* https://simplewebauthn.dev/docs/packages/server/
* https://github.com/go-webauthn/webauthn
* https://github.com/coder/websocket
* https://github.com/mantinedev/mantine

Ordinary document history capture starts with synchronous=NORMAL and ordinary
retention pruning. Its asynchronous capture queue is not a pre-replacement
durability/retention barrier: remote application must use the synchronous
``protect`` operation described above before publishing a replacement.

Save integration starts at the successful durable-save branch in
``src/ATHENA/Server/buffer_actor.cpp``. Realtime saves bypass Scheme's
``save-buffer-post`` hook, and the current RAG notification branch is limited
to non-realtime saves. Hodarium therefore needs its own notification at the
shared durable-success boundary; neither existing notification is sufficient.
An unsaved ``document_history_snapshot`` is not evidence of a successful save.
