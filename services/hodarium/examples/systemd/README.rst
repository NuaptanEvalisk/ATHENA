Linux deployment: authority and two Relays
=========================================

These examples run a single authority, a loopback Relay for a Cloudflare Tunnel,
and a separate Tailnet Relay. No credentials or production hostnames are included.
They use the existing service binaries; no Node runtime is required on the host.
Read ../../README.rst for bootstrap, protocol, recovery and security contracts.

Before installation
-------------------

Replace hodarium.example.org, horelay.example.org, host.example-tailnet.ts.net
and 100.64.0.10 with the intended public origins, Tailnet certificate hostname
and local Tailnet address. The authority origin is the browser-facing HTTPS
origin, not the loopback listener or origin certificate name. Passkeys are bound
to this choice. Do not reuse test authority state for a production deployment.

Create a dedicated system account and install the built binaries under
/opt/athena-hodarium/bin. Initialize the authority BEFORE systemd creates its
StateDirectory: the init command requires a nonexistent target directory::

  umask 077
  /opt/athena-hodarium/bin/athena-hodarium-server init \
    --data /var/lib/athena-hodarium
  chown -R hodarium:hodarium /var/lib/athena-hodarium

Capture the printed bootstrap credential and offline recovery seed privately.
Keep the recovery seed off the server and out of shell logs, source control and
monitoring. Bootstrap expires after 24 hours; register the administrator Passkey
promptly. Back up control state without treating a database rollback as recovery.

TLS and tokens
--------------

Create /etc/athena-hodarium owned root:hodarium with mode 0750 and its tls
subdirectory owned root with mode 0700. Obtain the initial certificate::

  tailscale cert \
    --cert-file /etc/athena-hodarium/tls/server.crt \
    --key-file /etc/athena-hodarium/tls/server.key \
    host.example-tailnet.ts.net

Provision a DIFFERENT 32-byte random base64url token for each Relay in
/etc/athena-hodarium/relay-public.token and relay-tailnet.token, owned by hodarium,
mode 0600. For example, as root with umask 077::

  openssl rand -base64 32 | tr '/+' '_-' | tr -d '=\n' > TOKEN_PATH
  chown hodarium:hodarium TOKEN_PATH
  chmod 600 TOKEN_PATH

Tokens are read directly because Relay's private-file check rejects credentials
with group permission bits, including systemd credential files on some hosts.
TLS files instead use systemd LoadCredential; the service account cannot read the
original private key. The two Relays share no token or authority signing key.

Install renew-tls.sh as /opt/athena-hodarium/renew-tls.sh, mode 0755. The daily
timer requests at least 30 days of certificate validity and restarts only already
running services when certificate bytes change. Active streams reconnect after
renewal. Monitor renewal failures rather than disabling TLS verification.

Services and ingress
--------------------

Install the service/timer files under /etc/systemd/system, verify them using
systemd-analyze verify, then daemon-reload and enable --now the three services
and athena-hodarium-tls.timer. On SELinux hosts restore the proper executable
contexts for /opt/athena-hodarium; do not disable enforcement. No general firewall
or routing change is prescribed by this example.

Merge cloudflared-ingress.yml routes into the existing locally managed Tunnel
configuration before its final catch-all. Keep all unrelated routes. Validate
with cloudflared tunnel ingress validate before restarting the connector. Both
HTTPS origins use originServerName with normal certificate verification.

Create proxied CNAME records for the two public hostnames pointing to the chosen
Tunnel's UUID.cfargotunnel.com. Do not put interactive Access challenges on native
client API or Relay requests. The authority supplies Passkey/device authentication;
the Relay requires its resource token and peers authenticate inside the encrypted
stream. Preserve existing Access policies on unrelated applications. Allow
WebSockets and avoid caching API responses. Review zone-wide challenge rules for
compatibility with non-browser clients.

Verification and operations
---------------------------

Check /api/info on the authority returns 200 and an unauthenticated POST to
/v1/join returns 401 on BOTH Relays. Confirm only the Tailnet Relay listens on
the selected Tailnet address; the other two listeners must stay loopback-only.
Then verify public routing, Passkey registration, token-authenticated Relay
connections and actual peer synchronization. Local health checks alone do not
prove the public Tunnel is configured or that synchronization works.

Configure clients with https://horelay.example.org and
https://host.example-tailnet.ts.net:19444, each with its corresponding token.
Do not replace the certificate hostname with a bare IP. Existing clients choose
available routes by measurement. Both routes share the same physical server and
are not independent failure domains.

None of these services is an offline document cache. A separate persistent
member is needed for asynchronous store-and-forward across devices.
