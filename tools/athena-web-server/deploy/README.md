# KVM-Isolated Deployment

The production deployment keeps `cloudflared` on the gateway host and runs the
broker and all OCI containers inside a dedicated MicroOS KVM guest:

```text
Cloudflare Tunnel
  -> gateway loopback 127.0.0.1:18090
  -> libvirt session VM through passt
  -> athena-web-server on guest port 8090
  -> rootless Podman sandbox and streamer containers
```

The gateway account `athena-web-vm` owns only a `qemu:///session` domain. It has
no password, login shell, sudo rights, system libvirt access, or shared host
filesystem. The domain exposes only loopback maintenance port 18022 and broker
port 18090. It has no graphics, USB, filesystem, host-device, or virtiofs
devices.

The guest uses the official openSUSE MicroOS Container Host cloud image.
`microos-user-data.in` provisions a locked root account with a deployment SSH
key and the unprivileged `athena-web` runtime account. The broker runs as that
account through `athena-web-server.service.in`; its nested session containers
retain the runtime isolation enforced by the broker.

SUSE disables user-service cgroup controller delegation by default.
`microos-user-data.in` overrides that policy only for `user@1100.service`, so
rootless Podman can enforce the broker's per-session CPU, memory, and PID
limits. Do not remove the delegation drop-in or weaken the broker's isolation
preflight. `athena-web-podman-migrate.service` runs `podman system migrate` as
a root-owned one-shot before `user@1100.service`. It changes to the runtime
account before invoking Podman and has no container-management role after
startup. This ordering reconciles the persistent rootless store with the new
user namespace after a VM reboot. The broker then calls `podman info` inside
the delegated user manager to initialize the live runtime before it checks the
image and isolation policy.

MicroOS ships `newuidmap` and `newgidmap` without privilege bits. Rootless
Podman needs those helpers to apply only the ranges assigned to `athena-web` in
`/etc/subuid` and `/etc/subgid`. The migration unit copies the immutable
package binaries into the root-owned `/usr/local/libexec/athena-web`
directory with their standard setuid mode and restores the `bin_t` SELinux
label. The runtime account cannot replace that directory or either helper.
Both the migration unit and broker unit prepend that directory to `PATH`.
Do not add `LockPersonality` or `SystemCallArchitectures` to the broker's user
unit: systemd makes either restriction prevent the setuid uidmap helper from
operating in descendant Podman processes. The untrusted sandbox and streamer
containers retain their own `no-new-privileges`, capability, read-only,
namespace, and resource restrictions.

`athena-web-isolation.xml.in` intentionally uses libvirt's `passt` backend.
Replace `@VM_DISK@`, `@SEED_ISO@`, and `@SSH_PUBLIC_KEY@` while assembling the
deployment. Do not add host filesystem shares or devices.

The gateway runs a dedicated Cloudflare Tunnel for this service. Replace
`@TUNNEL_ID@` in `cloudflared-athena-web.yml.in`, install it as
`/etc/cloudflared/athena-web.yml`, and install
`cloudflared-athena-web.service` as a system unit. This tunnel owns only
`athweb.evalisk.org` and forwards only to the VM's loopback-bound port 18090.
It must not be merged into the gateway's default or ATHENA Delegation tunnels.

Cloudflare Tunnel transports only HTTPS and WebSocket signaling. The broker
uses Cloudflare Realtime TURN for media relay across the guest, gateway, and
browser NAT boundaries. Create a dedicated TURN key, replace `@TURN_KEY_ID@`
in `athena-web-server.service.in`, and write the key's returned token to:

```text
/var/lib/athena-web/secrets/cloudflare-turn-token
```

The token file must be owned by `athena-web`, mode `0600`. It is a TURN-key
token, not a Cloudflare account API token. The broker uses it to mint one
short-lived credential per session; only those short-lived credentials enter
the streamer container and browser.
