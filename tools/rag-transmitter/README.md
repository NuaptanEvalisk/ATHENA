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
