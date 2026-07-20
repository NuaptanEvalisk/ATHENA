# ATHENA RAG Transmitter

`rag-transmitter` is the standalone reference proxy for ATHENA RAG
Delegation v1. It is a small C++ program with its own `main()` and does not
depend on Qt, Scheme, or the ATHENA GUI.

The transmitter implements the same public protocol as a backend ATHENA RAG
server:

- `GET /athena-rag/v1/identity`
- `POST /athena-rag/v1/rpc`

Local ATHENA encrypts requests to the transmitter. The transmitter decrypts
accepted client requests, runs the optional pre-forward script, re-encrypts the
request to the upstream backend ATHENA server, decrypts the upstream response,
runs the optional post-forward script, and re-encrypts the result to the local
client.

Example configuration:

```json
{
  "listen_address": "127.0.0.1",
  "listen_port": 8766,
  "key_dir": "/var/lib/athena-rag-transmitter",
  "accepted_clients": "/var/lib/athena-rag-transmitter/accepted-clients.json",
  "pending_clients": "/var/lib/athena-rag-transmitter/pending-clients.json",
  "upstream": {
    "url": "http://127.0.0.1:8765",
    "fingerprint": "backend-public-key-fingerprint"
  },
  "pre_forward_script": "/usr/local/bin/wake-rag-backend",
  "post_forward_script": "/usr/local/bin/sleep-rag-backend",
  "timeout_seconds": 300,
  "idle_shutdown_seconds": 0
}
```

Generate or display the transmitter server keypair:

```bash
rag-transmitter --config transmitter.json --generate-keypair
```

Run the transmitter:

```bash
rag-transmitter --config transmitter.json
```

Accepted clients use the same JSON shape as backend ATHENA:

```json
{
  "accepted": [
    "base64-client-public-key"
  ]
}
```

Enrollment writes pending clients as:

```json
{
  "pending": [
    {
      "public_key": "base64-client-public-key",
      "fingerprint": "client-public-key-fingerprint",
      "requested_at": "2026-07-09T00:00:00Z"
    }
  ]
}
```

The transmitter never logs document bodies. Job payloads are visible in memory
to the transmitter because it is a trusted hop-by-hop proxy.

## Service deployment

`deploy/` contains the systemd units used by the reference deployment and an
optional XClarity Controller Redfish power hook. The hook deliberately uses
`GracefulShutdown` and never falls back to `ForceOff`.

Install `xcc-power-hook.sh` as the transmitter's pre- and post-forward script.
The pre hook powers on the backend when necessary and waits for its ATHENA RAG
identity endpoint. The post hook requests a graceful shutdown. Configure a
positive `idle_shutdown_seconds`; post-forward work is scheduled in the
background, so the local ATHENA response is not delayed. A later job cancels a
pending idle shutdown, and shutdown is never attempted while a forward request
is active.

The power hook records an ephemeral ownership marker only when it observes the
server powered off and successfully sends the power-on request itself. It may
shut down only a server for which that marker remains present. If the server was
already on, it is treated as externally managed and is never shut down by the
transmitter. The marker lives below the service runtime directory, so a service
restart deliberately loses ownership rather than risking an incorrect shutdown.

Keep XCC credentials outside `config.json`. Install `xcc.env.example` as
`/etc/athena-rag-transmitter/xcc.env`, and provision the netrc data using the
unit's `LoadCredentialEncrypted` entry and `systemd-creds`. The decrypted
credential exists only in the service's private runtime credential directory.
Do not commit deployed configuration or credential files.

The optional `cloudflared-athena-rag.service` keeps a dedicated Cloudflare
Tunnel connected to the transmitter. Its dependency on the transmitter is a
`Wants`, not a `Requires`: restarting the transmitter must not stop the tunnel.
The tunnel configuration and DNS route remain deployment-specific and are not
stored in this repository.

The backend unit is a user service. Enable lingering for its service account so
the backend starts without an interactive login. The reference runtime keeps
only compatibility libraries that are absent from the host in `backend-lib`;
do not prepend a complete bundled desktop runtime because its OpenSSL libraries
may conflict with the host networking stack.
