Hodarium services
=================

This is an in-progress implementation of ``notes/athena-hodarium.tex``.
Do not enroll a production Vault yet. Membership administration is implemented;
peer content transport, client recovery acceptance, conflict decisions,
notifications and durable remote application are not yet connected.

The authority is a standalone Go binary. Its React/Mantine administrative panel
is compiled with Vite and embedded into that binary. No Node process is needed
on the deployment host. The native ATHENA client remains C++.

Build with Go 1.26 or later and a Node version supported by Vite 8::

  make -C tools/hodarium server

The binary is ``tools/hodarium/bin/athena-hodarium-server``. Dependencies are
locked by ``go.sum`` and ``web/package-lock.json``. Go WebAuthn supplies the
FIDO2 verifier (BSD-3-Clause), modernc SQLite supplies the control store
(BSD-3-Clause), and Mantine supplies the forms and components (MIT).
ATHENA additions are GPL-3.0-or-later. Do not replace these with a custom
WebAuthn verifier or hand-authored component framework.

Independent relay
-----------------

Build ``make -C tools/hodarium relay`` to produce
``tools/hodarium/bin/athena-hodarium-relay`` without building the web panel.
The relay uses coder/websocket 1.8.15 (ISC), including its maintained streaming
``net.Conn`` adapter; no custom WebSocket implementation is used.
References: https://github.com/coder/websocket and its ``LICENSE.txt``.

Run with a normal TLS certificate and a private relay resource credential::

  athena-hodarium-relay --listen :9444 --tls-cert relay.pem --tls-key relay-key.pem \
    --access-token-file /private/path/relay-token

The token file must be a private regular file containing 32 cryptographically
random bytes encoded as unpadded base64url. It is independent of all Hodarium
authority, recovery and device keys. This initial service uses one deployment
credential for resource access; restarting with another revokes it. The TLS
certificate identifies the relay. Do not expose it behind plaintext transport.

``POST /v1/tickets`` with ``Authorization: Bearer <relay-token>`` allocates two
different one-use endpoint capabilities, returned as ``endpoints`` and an
``expires`` Unix timestamp. Share the opposite endpoint through the future
authenticated rendezvous channel, not a URL or public log. Each peer connects
to ``GET /v1/stream`` using the same relay access header plus its own
``X-Hodarium-Ticket`` header and WebSocket subprotocol
``athena-hodarium-stream-v1``. Browser Origin headers and query strings are
rejected. Only binary messages are accepted, up to 64 KiB each. Message
boundaries are not preserved: this is a byte stream, not a message protocol.

Reservations expire after two minutes. Defaults bound reserved/active sessions
to 256, each direction to 1 GiB, session duration to one hour and each read/write
idle interval to 90 seconds. ``--max-sessions`` and ``--max-bytes`` configure
the first two limits. Backpressure uses a 32 KiB forwarding buffer per direction;
no Vault data, membership state or rendezvous records are written to disk.
Disconnect, expiry and shutdown cancel both ends and discard their capabilities.
Peers will need encrypted keepalives and resumable transfer above this layer.

The relay does not establish end-to-end encryption itself. Native clients must
establish authenticated inner TLS before sending any document data. The native
Qt adapter now passes an isolated 700 KiB roundtrip through that inner session.
Production configuration, authority rendezvous and automatic route selection
remain unconnected. TLS on the two outer connections alone is not sufficient.

Focused relay verification::

  cd tools/hodarium
  go test -race ./internal/relay

Authority administration
------------------------

Member rendezvous
~~~~~~~~~~~~~~~~~

``POST /api/device/rendezvous`` publishes, lists or withdraws ephemeral online
presence. Requests contain ``payload`` (base64url of the exact UTF-8 JSON bytes),
``challenge`` and ``signature``. Request a device challenge with purpose
``rendezvous`` and subject equal to base64url(SHA-256(payload bytes)), then sign
the normal group-bound proof. Changing the operation, addresses, member or epoch
invalidates that proof. Neither Passkey cookies nor a relay credential authorize
this endpoint.

The decoded request fields are ``operation`` (publish/list/withdraw), ``member``,
``generation`` and ``epoch``. Publication accepts at most eight ``direct`` IP:port
addresses and eight HTTPS ``relays`` origins. It excludes paths, credentials,
query strings, scoped/multicast/unspecified/loopback direct addresses. Relay
origins carry no resource secret. Listing optionally takes an ``after`` member
cursor and returns up to 64 entries plus ``next``; each page carries its current
generation and epoch. The caller must restart discovery after an epoch change.

Presence lasts 90 seconds, lives only in the authority process, and is discarded
on restart. Only an active member in the current generation/epoch may publish or
query; old-epoch requests return conflict, expelled devices are denied, and old
publications are filtered before listing. The authority never contacts these
addresses. Presence is a reachability hint, not peer authorization: the client
must still apply its route policy and complete inner mutual TLS and membership
context verification. The native client supports signed publication, one-page
queries and withdrawal, verified against this server with isolated device keys.
Validated application profiles now periodically publish presence and aggregate
discovery pages; suspension and authorization expiry invalidate local candidates.
Profiles publish real direct-listener interface addresses and automatically dial
discovered direct peers with inner TLS. Relay ticket exchange and measured route
selection remain unconnected.

Initialization
~~~~~~~~~~~~~~

Local initialization, using a new directory::

  athena-hodarium-server init --data /private/path/hodarium

This prints a high-entropy, 24-hour, one-use bootstrap credential and an offline
recovery seed. Store the recovery seed away from the server. It is not written
to the server's database. The recovery public key is retained. Never put either secret in a URL,
shell command argument, log collector or source control.

Recover administrative access from a trusted machine using the offline seed::

  athena-hodarium-server recover --server https://hodarium.example.org \
    --recovery-seed-file /offline/private/recovery-seed --confirm-expel-all

The file contains the printed base64url seed, with mode 0600 or 0400. The CLI
checks HTTPS normally, rejects redirects and signs a fresh server challenge
locally; the seed is never transmitted. This operation deliberately revokes
all existing members, Passkeys, sessions and pending admissions. It publishes
a new random recovery generation and prints a new one-use bootstrap credential
for registering a new administrator Passkey. Existing device data is untouched.
An interrupted response may mean recovery committed but its bootstrap was not
received: perform another fresh recovery, never roll the database back.
Native device acceptance of recovery remains unimplemented and must require
explicit trust re-establishment, not acceptance of any historical valid proof.

Run behind a stable HTTPS origin with its corresponding certificate::

  athena-hodarium-server serve --data /private/path/hodarium \
    --origin https://hodarium.example.org --listen 127.0.0.1:7443 \
    --tls-cert /private/path/fullchain.pem --tls-key /private/path/tls-key.pem

The origin must exactly match the public browser origin, with no trailing
slash. TLS 1.3 is required. The service does not trust forwarded identity
headers. A reverse proxy currently shares one source-IP rate budget across
its clients. No untrusted first visitor can claim the service: first passkey
registration requires the local bootstrap credential. Subsequent registrations,
device admission, expulsion and passkey deletion require user verification
within five minutes. Sessions last at most eight hours. Removing a passkey
invalidates all sessions; the last passkey cannot be removed this way.

Security and protocol boundaries
-------------------------------

``identity.key`` is the authority's own private signing key, not a device key.
The private state directory must have mode 0700 and that file mode 0600.
Native device identities must instead use libsecret / Keychain. Authority
backup and trusted recovery still require the rollback protocol from the
design; copying an old database into place is not a supported recovery flow.

HTTP bodies are bounded, requests have deadlines, handler concurrency and
pending challenges/requests are capped. Administrative mutations check Origin
and use Secure/HttpOnly/SameSite=Strict cookies. Nonce challenges and WebAuthn
ceremonies are consumed once, including failed verification attempts.

The API's main groups are ``/api/device/`` (proof-of-possession operations),
``/api/auth/`` (WebAuthn ceremonies), and ``/api/admin/`` (authenticated control).
No endpoint accepts document content, filenames or a Vault's plaintext hash.

Device keys are Ed25519 public keys, encoded with unpadded base64url. Proofs
sign the UTF-8 JSON array returned by ``authority.ProofMessage``. Its fields
are domain, Hodarium ID, purpose, subject and the issued nonce. All identifiers
in that array are ASCII. Join binds the subject to the exact device public key;
poll binds it to a pending request; control refresh binds it to the member
instance. The private retrieval credential is independent of the rotating
eight-digit display code. Codes rotate every 30 seconds. Requests expire in
ten minutes; challenges expire in one minute. Code lookup is available only
to recently verified administrators and has a persisted 30-per-hour budget,
independent of code rotation, session renewal and server restart.

An approval binds request ID and public key and conditionally advances the
expected membership revision. It creates a fresh member instance. Re-admission
after expulsion does not revive an earlier instance. Approved retrieval is
consumed once and still requires the private credential and a new key proof.
If an approval response is lost, ``/api/device/resolve`` recovers an already
active member instance. It requires a fresh ``resolve`` challenge whose subject
is the exact device public key, signed with that private key. It cannot admit
a device, revive an expelled member, or grant an offline lease. The native
client subsequently validates the recovered member ID through the ordinary
control protocol. Short display codes remain insufficient for either operation.

Membership states are signed as::

  Ed25519("ATHENA-HODARIUM-STATE-v1" || NUL || payload)

The exact UTF-8 JSON payload and signature are base64url fields in an envelope.
Verify those bytes before parsing; do not reserialize JSON to verify. The state
contains protocol, Hodarium ID, recovery generation, monotone revision, random
epoch and sorted active members. Each publication and its audit event commit
together under SQLite FULL synchronous durability. Epoch uniqueness is also
enforced by the database.

Fresh validation responses bind the exact state envelope, member instance,
request challenge and maximum offline interval (86400 seconds) in a separate
authority signature. A cached member publication alone is not evidence of a
fresh authority contact. Native clients must persist rollback state and use
a bounded monotonic freshness interval; this client integration is pending.
The native ``membership`` module now verifies these envelopes and persists
rollback state. Its wire fixture is generated by the actual Go authority; the
native network owner and device lifecycle integration remain pending. Active
membership is bounded to 4096 devices, shared with the native verifier.

Checks
------

``make -C tools/hodarium test`` runs only the focused authority package checks.
They use temporary authority directories and a software authenticator with
real P-256 signatures / CBOR attestation through go-webauthn, not a stubbed
verification result. Coverage includes registration, login, mandatory user
verification, wrong challenges, replay, bootstrap consumption, device proof,
epoch publication, conditional mutation, expulsion, persisted code budgets and
recovery revocation of old member/administrator credentials.
These do not constitute complete Hodarium acceptance or native-device testing.

The initial panel was also exercised over HTTPS in an isolated headless browser
with a virtual CTAP2 authenticator: first registration, logout/login, panel tabs,
and desktop/narrow-screen layout. No real Passkey account or production Vault
was involved. Browser TLS certificates should use commonly supported RSA or
P-256 keys; the authority's separate Ed25519 control-signing identity is not
its browser-facing TLS certificate.
