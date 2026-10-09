ATHENA Hodarium services
=======================

Hodarium has a single administrative Server per group and optional independent
Relays. The Server handles Passkey administration, admission/expulsion, signed
membership, ephemeral presence, opaque secret registration and conflict decisions.
It does not host a Vault or receive document plaintext. Relays forward encrypted
peer traffic while endpoints are online; they have no membership key, are not
devices and do not forward to other Relays. Neither service is an offline mailbox.

The contract is ``notes/athena-hodarium.tex``. Native usage, synchronized data
scope and current application/recovery limits are in
``src/ATHENA/Hodarium/README.rst``. Implemented components and connected workflows
should not be mistaken for complete production or iPad acceptance.

Build and Server deployment
---------------------------

Use Go 1.26 or later and a Node version supported by Vite 8::

  make -C tools/hodarium server
  make -C tools/hodarium relay

Outputs are ``tools/hodarium/bin/athena-hodarium-server`` and
``tools/hodarium/bin/athena-hodarium-relay``. The Server embeds its React/Mantine
panel; deployment needs no Node process. The Relay target does not build the
panel. Dependencies are locked by ``go.sum`` and ``web/package-lock.json``:
go-webauthn and modernc SQLite are BSD-3-Clause, Mantine and resend-go MIT,
coder/websocket ISC.
ATHENA additions are GPL-3.0-or-later.

Initialize a new private directory::

  athena-hodarium-server init --data /private/path/hodarium

This prints a high-entropy one-use bootstrap credential valid for 24 hours, plus
an offline recovery seed. Keep the seed away from the Server; its private bytes
are not retained in the Server database. Neither secret belongs in a URL, shell
argument, source control or collected log.

Run with a stable public HTTPS origin and matching certificate::

  athena-hodarium-server serve --data /private/path/hodarium \
    --origin https://hodarium.example.org --listen 127.0.0.1:7443 \
    --tls-cert /private/path/fullchain.pem --tls-key /private/path/tls-key.pem

The origin must match the browser-facing origin exactly, without a trailing
slash. The example listener is loopback-only: arrange an HTTPS reverse proxy or
choose the intended direct listening interface/port. TLS 1.3 and normal
certificate verification are required. Forwarded identity headers are not
trusted; reverse-proxied clients share the proxy's source-IP rate budget. The
browser TLS certificate is separate from the Server's Ed25519 control key.

Register the first administrator Passkey using the bootstrap credential in the
panel. An anonymous first visitor cannot claim the service. Later registration,
admission/expulsion and Passkey deletion need user verification within five
minutes. Sessions last at most eight hours. Deleting a Passkey invalidates
sessions; the last Passkey cannot be deleted this way.

In ATHENA, use ``View -> Hodarium`` to request admission against the HTTPS origin.
Approve the displayed code in the Server panel. Codes rotate every 30 seconds;
their eight digits alone authorize nothing. Approval also binds request ID,
device key, private retrieval credential and fresh private-key proof. Code lookup
requires recent administrator verification and has a persisted 30-per-hour
budget. Requests expire after ten minutes; challenges after one minute. Admission
creates a fresh member instance and still requires signed membership validation.

Bind existing local Vaults on admitted devices using the same UUID for each
replica. Selection is local; membership is device-wide. The manager supports
pause/resume, Relay configuration, conflict handling and explicit recovery.
Ordinary discovery never silently replaces established trust.

Independent Relay deployment
----------------------------

Create a private regular file containing 32 cryptographically random bytes
encoded as unpadded base64url, then run::

  athena-hodarium-relay --listen :9444 \
    --tls-cert relay.pem --tls-key relay-key.pem \
    --access-token-file /private/path/relay-token

The file must not grant group/other permissions. Its token is a Relay resource
credential, independent of authority, recovery, device and Vault keys. The current
service uses one shared deployment token; restarting with another revokes it.
Configure the origin and token in each device's ``Relays`` dialog. Native clients
store tokens in Secret Service/Keychain, retaining only handles in settings.
Profiles support at most eight Relays.

Clients measure and select available direct/shared-Relay routes automatically.
RTT and bounded throughput probes feed route choice, with switching hysteresis.
Energy-constrained clients disable throughput probing. There is no direct NAT
hole punching; a Relay is needed where advertised direct addresses cannot connect.
Both direct and Relay paths use peer-authenticated inner TLS.

All Relay endpoints require ``Authorization: Bearer <relay-token>``. Current
automatic clients use ``POST /v1/join`` with a disposable room ID and side 0/1;
each side receives its own one-use ticket. A room is a routing hint, not membership
proof. ``POST /v1/tickets`` also allocates explicit pairs of endpoint capabilities.
Do not put tokens or tickets in URLs or logs.

Open ``GET /v1/stream`` with the access header, ``X-Hodarium-Ticket`` and
WebSocket subprotocol ``athena-hodarium-stream-v1``. Browser Origin headers and
query strings are rejected. Binary messages are capped at 64 KiB; message
boundaries are not application boundaries. The native client authenticates both
device keys inside the stream; the Relay certificate does not authenticate peers.

Reservations expire after two minutes. Defaults are 256 reserved/active sessions,
1 GiB per direction per session, a one-hour lifetime and 90-second read/write idle
deadlines. ``--max-sessions`` and ``--max-bytes`` adjust the first two. Forwarding
uses bounded buffers/backpressure. Disconnect, timeout and shutdown cancel both
ends and discard tickets. Clients reconnect/resume at the revision layer. The
Relay persists no document or presence data.

Administrative recovery
-----------------------

From a trusted machine holding the offline seed::

  athena-hodarium-server recover --server https://hodarium.example.org \
    --recovery-seed-file /offline/private/recovery-seed --confirm-expel-all

The seed file contains the printed base64url seed with mode 0600 or 0400. The CLI
checks HTTPS normally, rejects redirects and signs a fresh challenge locally;
the seed is never transmitted. Recovery expels every device, revokes Passkeys,
sessions and pending admissions, creates a random generation and prints a new
bootstrap credential. Register a new administrator Passkey. Existing device data
is not erased. A lost response may follow a committed recovery: perform another
fresh recovery rather than restoring an old database to retrieve its bootstrap.

Devices use ``Recover membership`` to inspect and explicitly accept the transition.
``GET /api/recovery/current/{nonce}`` returns the current offline-signed recovery
event inside a fresh authority-signed nonce binding. The client verifies against
its pinned recovery key and rejects previously accepted generations. Historical
proof or ``/api/info`` alone cannot change trust. Acceptance clears membership;
devices must request and receive fresh admission. Old files, trust stores,
journals and bindings are retained. The native coordinator suspends the old
session, cancels and joins in-flight workers, and reconciles pending application
intents through protected application before accepting the transition. A blocked
intent prevents acceptance. Successful acceptance selects a generation-specific
journal and starts fresh admission and saved-source inventory; it does not replay
old-generation merge certificates. The directly preceding journal supplies
confirmed applied deletion roots; that Vault's application and peer replication
wait for complete source and logical/resource baseline inventory. A missing
predecessor journal provides no deletion baseline. The client README describes
this implemented coordination and its remaining unbound-Vault and historical
lineage limits.

The recovery seed restores administration, not content or lost device private
keys. Vault secrets reside in device key stores and are delivered between peers;
the Server holds commitments only. The administrative seed cannot decrypt a
missing Vault secret. Neither service can recover content after every source
replica is lost. Keep independent content backups and protect Server state against
rollback. Copying an old control database into place is not supported recovery.

Optional security notifications
-------------------------------

The Server can send admission, expulsion and authority-recovery notifications
through Resend. Add ``--notifications-config /private/path/notifications.json``
to ``serve``. The optional JSON file must be a private regular file (0600 or
0400), at most 16 KiB, with these fields::

  {
    "resend_api_key": "re_REPLACE_WITH_SERVER_PRIVATE_KEY",
    "from": "ATHENA Hodarium <security@example.org>",
    "to": ["owner@example.org"]
  }

Use a verified sending domain and a suitably restricted sending API key. Up to
eight recipients are supported. Keep the token only in Server-private deployment
configuration, never in native settings, source control or command arguments.
Restart the service to reload configuration. Omitting the flag disables delivery;
an invalid configuration emits a generic warning and leaves authentication
available. Provider error text and API tokens are never logged by this service.

Notifications contain only the event category, UTC timestamp and a generic
instruction to review the administration panel. They contain no Vault data,
filenames, device names or keys, admission codes or recovery credentials. Resend
necessarily sees the configured sender/recipients, event type and time.

The notification worker reads committed authority audit events; it does not add
network I/O to admission, expulsion or recovery transactions. First enabling it
starts at the current audit tip, without mailing historical events. Subsequent
events are consumed asynchronously, including events committed while the worker
is stopped. ``notifications.sqlite`` in the Server data directory records its
checkpoint and one pending message; it contains addresses/message metadata, not
the API key. Preserve this queue with the corresponding Server state. Audit
rollback behind its checkpoint stops notification progress rather than silently
resetting the cursor. This is not a supported Server rollback mechanism.

Retries retain the same message and opaque idempotency key across restarts,
including the original recipients if configuration changes. Resend retains
idempotency keys for 24 hours; this worker stops uncertain retries after 23 hours,
warns and advances instead of risking duplicate delivery outside that window.
Notifications are therefore best-effort, not a guaranteed security audit delivery
channel. Delivery failure never rolls back or blocks an authorization transaction.

The implementation uses the official MIT-licensed ``resend-go/v4`` SDK, with
bounded responses, request deadlines and redirects disabled. See
`Resend Go documentation <https://resend.com/docs/send-with-go>`_ and
`idempotency semantics <https://resend.com/docs/dashboard/emails/idempotency-keys>`_.
Tests use a fake HTTP transport, not real credentials or email delivery.

Protocol and privacy boundaries
-------------------------------

The Server directory is private (0700); ``identity.key`` is its signing key (0600),
not a device key. Native device keys use platform protected storage. HTTP bodies,
concurrency, challenges and pending requests are bounded. Administrative mutations
check Origin and use Secure/HttpOnly/SameSite=Strict cookies. Challenges/WebAuthn
ceremonies are consumed once, including failed verification attempts.

``/api/device/`` requires device proof, ``/api/auth/`` performs WebAuthn ceremonies,
and ``/api/admin/`` handles authenticated administration. ``/api/info`` and current
recovery inspection expose public metadata. No endpoint accepts document
plaintext, filenames or unkeyed content hashes. The Server observes membership,
presence and opaque control records; Relays observe connections, room hints and
traffic timing. Encryption does not hide every kind of network metadata.

Keys/identifiers use unpadded base64url. Device proofs sign the exact UTF-8 JSON
array from ``authority.ProofMessage``: domain, group, purpose, subject, challenge.
Signed envelopes carry original payload bytes and signature. Verify those bytes,
not reserialized JSON. Membership publications bind generation, monotone revision,
random epoch and sorted active members; publication/audit commit together with
SQLite FULL durability. Nonce-bound validation grants at most 86400 seconds of
offline validity. Cached membership alone grants none. Native clients persist
rollback state, refresh on their worker and revoke authorization on suspension.

``/api/device/rendezvous`` publishes/lists/withdraws memory-only presence under
the current generation/epoch. Each member advertises up to eight direct addresses
and eight Relay origins; pages contain at most 64 members. Presence expires after
90 seconds or Server restart. The Server never dials advertised addresses. Peers
still verify inner TLS identity and current membership context.

``/api/device/vault-secret`` registers the first canonical commitment per Vault
slot/generation and returns signed receipts. Competing registrations receive the
winner, not a second secret. Actual secret delivery is peer-to-peer.

``/api/device/decision`` performs conditional/idempotent conflict decisions over
secret-derived opaque tokens. Native clients retain exact proposals in an outbox,
verify receipts and admit only authorized merge revisions. Multi-parent receipt
alone grants no application permission. Decisions do not advance membership
epochs. New decisions need Server connectivity; protected source application
remains the native client's responsibility.

Limitations and checks
---------------------

There is no Server content browser, centralized Vault backup or Relay federation.
Optional email is best-effort and does not replace the durable audit.
Native database/resource transfer
has defined logical adapters and allowlists, not raw database or universal file
mirroring. File History protection does not provide a service-side whole-Vault
restore interface. See the native README for scope and recovery restrictions.

``make -C tools/hodarium test`` runs the focused authority tests, not editor/iPad
acceptance. Relay checks are under ``internal/relay``; native HTTPS/Relay launchers
and isolated-keyring support are under ``tests/hodarium``. ``TestNativeRecovery``
optionally runs a native fixture selected by ``ATHENA_HODARIUM_NATIVE_RECOVERY``.
Fixtures use temporary state rather than production Vaults or real user keyring/
Passkey state. Component checks do not demonstrate completed desktop/mobile,
conflict, fault-recovery and long-running convergence acceptance.
