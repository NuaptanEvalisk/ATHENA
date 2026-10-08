Hodarium services
=================

This is an in-progress implementation of ``notes/athena-hodarium.tex``.
Do not enroll a production Vault yet. Membership administration is implemented;
peer content transport, client enrollment, control recovery, conflict decisions,
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

Local initialization, using a new directory::

  athena-hodarium-server init --data /private/path/hodarium

This prints a high-entropy, 24-hour, one-use bootstrap credential and an offline
recovery seed. Store the recovery seed away from the server. It is not written
to the server's database. The recovery public key is retained; the recovery
protocol is still pending implementation. Never put either secret in a URL,
shell command argument, log collector or source control.

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

Checks
------

``make -C tools/hodarium test`` runs only the focused authority package checks.
They use temporary authority directories and a software authenticator with
real P-256 signatures / CBOR attestation through go-webauthn, not a stubbed
verification result. Coverage includes registration, login, mandatory user
verification, wrong challenges, replay, bootstrap consumption, device proof,
epoch publication, conditional mutation, expulsion and persisted code budgets.
These do not constitute complete Hodarium acceptance or native-device testing.

The initial panel was also exercised over HTTPS in an isolated headless browser
with a virtual CTAP2 authenticator: first registration, logout/login, panel tabs,
and desktop/narrow-screen layout. No real Passkey account or production Vault
was involved. Browser TLS certificates should use commonly supported RSA or
P-256 keys; the authority's separate Ed25519 control-signing identity is not
its browser-facing TLS certificate.
