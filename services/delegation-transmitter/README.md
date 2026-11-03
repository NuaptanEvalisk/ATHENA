# ATHENA Transmitter

`athena-transmitter` is the standalone reference proxy for ATHENA Delegation
v1. It is a small C++ program with its own `main()` and does not depend on Qt,
Scheme, or the ATHENA GUI.

The public protocol is shared with a backend ATHENA process:

- `GET /athena-delegation/v1/identity`
- `POST /athena-delegation/v1/rpc`
- `auth.enroll` and `auth.check`
- `rag.embedding.build_patch`
- `artifact.definition_span.submit`, `wait`, `cancel`, and `ack`

Local ATHENA encrypts requests to the transmitter. The transmitter authenticates
and decrypts the client request, runs the optional pre-forward hook, verifies the
upstream identity and required workload capability, then re-encrypts the request
to backend ATHENA. The response follows the same hop-by-hop process in reverse.
The transmitter is therefore a trusted intermediary and can see plaintext while
forwarding, but it never logs document or paragraph bodies.

Example configuration:

```json
{
  "listen_address": "127.0.0.1",
  "listen_port": 8766,
  "key_dir": "/var/lib/athena-transmitter",
  "accepted_clients": "/var/lib/athena-transmitter/accepted-clients.json",
  "pending_clients": "/var/lib/athena-transmitter/pending-clients.json",
  "upstream": {
    "url": "http://127.0.0.1:8765",
    "public_key": "base64-backend-public-key",
    "fingerprint": "backend-public-key-fingerprint"
  },
  "pre_forward_script": "/usr/local/bin/wake-athena-backend",
  "post_forward_script": "/usr/local/bin/sleep-athena-backend",
  "timeout_seconds": 300,
  "idle_shutdown_seconds": 30
}
```

Generate or display the transmitter keypair, then run it:

```bash
athena-transmitter --config transmitter.json --generate-keypair
athena-transmitter --config transmitter.json
```

Accepted clients use this JSON shape:

```json
{
  "accepted": [
    "base64-client-public-key"
  ]
}
```

Enrollment adds an entry to `pending-clients.json`; it never grants trust by
itself. The administrator must move the public key into `accepted-clients.json`.
The transmitter key must be listed by backend ATHENA with role `proxy`, so only
that key may assert the original authenticated client principal:

```json
{
  "accepted": [
    {
      "public_key": "base64-transmitter-public-key",
      "role": "proxy"
    }
  ]
}
```

Artifact jobs are asynchronous. The transmitter keeps leases for outstanding
jobs so long-poll, cancellation, and acknowledgement do not rerun the wake hook,
and so the shutdown hook cannot run while delegated work remains active. HTTP
connections are handled concurrently; one long-poll does not block another job.

Each `rag.embedding.build_patch` request carries a client-generated
`request_id`. The ID remains unchanged when a transport timeout reconnects and
retries the same batch, and changes only when the batch contents change. Backend
ATHENA caches a small number of successful patches for 15 minutes by client
principal and request ID. This prevents a request retained by a waking proxy and
its client retry from running the same embedding batch twice.

## Service deployment

`deploy/` contains reference systemd units and an optional XClarity Controller
Redfish hook. The hook uses `GracefulShutdown` and never falls back to force-off.
It records ownership only when it observes the server off and sends the power-on
request itself. A server that was already on is treated as externally managed
and is never shut down by the transmitter.

Install `xcc.env.example` as `/etc/athena-transmitter/xcc.env`. Keep credentials
outside `config.json`; provision `xcc.netrc` through the unit's encrypted systemd
credential. The decrypted credential then exists only in the service credential
directory.

The optional `cloudflared-athena-delegation.service` keeps a dedicated tunnel
connected to the transmitter. It uses `Wants`, not `Requires`, so restarting the
transmitter does not tear down the tunnel. Tunnel configuration and DNS routing
remain deployment-specific and are not stored in this repository.
