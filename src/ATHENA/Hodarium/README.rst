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
that sequence; peer transport and document synchronization remain unconnected.

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
The enrollment/settings UI and peer data plane remain unconnected.

The real C++/Go HTTPS exchange was exercised with a separately trusted temporary
TLS certificate and an isolated system keyring. The native client requests
admission, the test approves through the real administrative Admit path, and
the client polls, retrieves its member ID, recovers that ID from a fresh
enrollment object, commits device settings, reopens those settings, then validates
membership. The production enrollment UI remains to be completed. Reproduce with::

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

The caller owns the store on one worker thread. The database pathname is local
configuration, never supplied by a peer. ``record_applied`` is bookkeeping,
not a filesystem replacement API: only a future durable application coordinator
may call it after history protection and the application commit boundary.

Remaining integration (not enabled)
----------------------------------

* Add production enrollment/settings UI and terminate future peer sessions after
  expiry or expulsion; wire Vault selection and application status presentation.
* Verify the Apple backend and complete explicit recovery trust transitions.
* Independent relay executable; client recovery trust, conflict decisions,
  discovery, operational deployment and notifications. Extend the existing
  authority/panel rather than replacing their WebAuthn implementation.
* Authenticated E2E transport, rendezvous, bounded transfers, automatic measured
  route selection, reconnection and iPad suspension/resumption.
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
* https://www.gnutls.org/manual/gnutls.html
* https://gnome.pages.gitlab.gnome.org/libsecret/
* https://gnome.pages.gitlab.gnome.org/libsecret/method.Service.search_sync.html
* https://developer.apple.com/documentation/security/ksecattraccessiblewhenunlockedthisdeviceonly
* https://simplewebauthn.dev/docs/packages/server/
* https://github.com/go-webauthn/webauthn
* https://github.com/coder/websocket
* https://github.com/mantinedev/mantine

The existing document history connection uses synchronous=NORMAL and ordinary
retention pruning. Calling its asynchronous capture queue is not yet a valid
pre-replacement durability/retention barrier. This must be addressed before
any remote application is enabled.

Save integration starts at the successful durable-save branch in
``src/ATHENA/Server/buffer_actor.cpp``. Realtime saves bypass Scheme's
``save-buffer-post`` hook, and the current RAG notification branch is limited
to non-realtime saves. Hodarium therefore needs its own notification at the
shared durable-success boundary; neither existing notification is sufficient.
An unsaved ``document_history_snapshot`` is not evidence of a successful save.
