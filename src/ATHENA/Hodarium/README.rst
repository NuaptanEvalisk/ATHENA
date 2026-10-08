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
admission, expulsion, audit and persistent admission-code budgets. Native
clients are not yet connected, and administrative recovery is not yet complete.

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

* Native verification/persistence of signed membership, rollback protection,
  bounded online freshness and session termination after expulsion.
* Protected device identities in libsecret/Keychain, recovery and admission.
* Independent relay executable; authority recovery, conflict decisions,
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
