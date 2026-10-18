ATHENA Hodarium headless node
============================

``athena-hodarium-node`` is an independently enrolled, persistent Hodarium
member, not an authority, Relay, web application, or desktop session. Its
headless build selects the private-file keystore at build time, never as a
fallback from Secret Service or Keychain. ATHENA additions are GPL-3.0-or-later;
see the repository's ``LICENSE`` and ``COPYING``.

Development boundary
--------------------

A separate development test group and disposable test Vault are mandatory for
this workflow. Do not use production Notes, production Vaults, production
authority credentials, or an existing device's state. These instructions and
the Dockerfile do not automatically set up any production service. The image
defaults to ``--help``; building it creates no identity or enrollment.

Start unaffiliated, with new empty private state. Request fresh admission under
a distinct device name and have the development group's administrator approve
it. Never copy a desktop's identity or clone another node's state to enroll.
One running member owns one state directory and its bound Vaults; do not let
an editor, another node, or another container concurrently mutate those Vaults.

Build
-----

Linux only: the private-file backend uses Linux-specific filesystem interfaces,
including ``renameat2(RENAME_NOREPLACE)``. The headless CMake graph rejects
non-Linux targets.

From the repository root, with dependencies already installed::

  cmake -S . -B build_hodarium_node -DATHENA_BUILD_HODARIUM_NODE_ONLY=ON
  cmake --build build_hodarium_node --target athena-hodarium-node -j20

The executable is
``build_hodarium_node/src/ATHENA/HodariumNode/athena-hodarium-node``.
The dedicated CMake graph requires a C/C++ compiler, CMake, pkg-config,
threads, Qt 6.5 or newer Core/Network/WebSockets, ICU, libsodium, SQLite, GnuTLS, mimalloc,
spdlog, and nlohmann JSON headers. It does not configure the desktop, GUI, model
inference, or web assets.

The node initializes and binds XML v3 Vaults (``node_model_version=2``), and
rejects unpublished migration candidates. The standard named-symbol registry
is embedded from the same JSON source used by the desktop; no desktop resource
directory or ``ATHENA_PATH`` is needed. Existing v2 Vaults must be migrated
offline before binding. Upgrade all members together; the v3 protocol rejects
older peers and starts fresh revision/conflict journals without replacing
device identities or Vault keys.

The multi-stage Dockerfile uses Debian Trixie for both stages, with distribution
Qt 6.8.2 packages, not vendored substitutes. CMake enforces Qt 6.5 as the minimum:
the existing ``QWebSocket::errorOccurred`` signal requires it. Bookworm's Qt 6.4
is unsupported; no legacy fallback is added to the core.
The build stage uses ``build-essential``, ``cmake``,
``pkg-config``, ``qt6-base-dev``, ``qt6-websockets-dev``, ``libicu-dev``,
``libsodium-dev``, ``libsqlite3-dev``, ``libgnutls28-dev``, ``libmimalloc-dev``,
``libspdlog-dev``, and ``nlohmann-json3-dev``. The runtime uses
``libqt6core6t64``, ``libqt6network6``, ``libqt6websockets6``, ``libssl3t64``,
``libicu76``, ``libsodium23``, ``libsqlite3-0``, ``libgnutls30t64``,
``libmimalloc3``, and ``libspdlog1.15``, plus ``libstdc++6`` and CA certificates.
These names were checked against Trixie's amd64 package metadata, without
installing host packages. Debian's base Qt development package can pull GUI
development dependencies into the disposable build stage; the node does not
link them and the runtime stage does not install Qt GUI/Widgets. Only the
executable, license files, and headless runtime packages reach the final image.
Qt Network's OpenSSL backend and CA certificates support HTTPS alongside
GnuTLS for peer transport. Package references:
`Trixie package index <https://deb.debian.org/debian/dists/trixie/main/binary-amd64/Packages.xz>`_,
`Qt WebSockets <https://packages.debian.org/trixie/libqt6websockets6>`_,
`mimalloc <https://packages.debian.org/trixie/libmimalloc3>`_, and
`spdlog <https://packages.debian.org/trixie/libspdlog1.15>`_.

When explicitly ready to build the development image, run from the repository
root (this installs packages inside build layers, not on the host)::

  docker build -f tools/hodarium/node/Dockerfile -t athena-hodarium-node:dev .

Keep secrets outside the build context. The Dockerfile copies only the root
build/license files, ``cmake/``, ``src/``, and shipped label/symbol definitions;
it does not copy notes, Vaults,
model assets, or host state. No image build or runtime acceptance is implied by
the presence of this recipe.

TrueNAS deployment
------------------

Use the supported ``midclt app.create`` API, not edits to TrueNAS-managed Docker
configuration. Its input accepts ``app_name``, ``custom_app: true``, and
``custom_compose_config`` (an object) or ``custom_compose_config_string`` (YAML).
The accompanying ``compose.yaml`` provides an unjoined service with independent
state and Vault volumes. Import the locally built image first, or change the
image and pull policy to an operator-controlled registry. For bind mounts,
provision separate owned directories before creating the application.

The example uses host networking for P2P reachability. Its healthcheck verifies
only that the daemon publishes a fresh heartbeat, not that membership is valid
or all peers have converged. Enrollment and Vault binding remain explicit
offline commands; starting the application does not admit it to any group.
Dependency installation belongs only to container image build layers, never
to the TrueNAS host.

Private state and storage
-------------------------

The image runs as dedicated UID/GID ``10001:10001``. Persist
``/var/lib/athena-hodarium-node`` independently of ``/vault``. Both image
directories start owned by that UID with mode ``0700``. New Docker named
volumes inherit their mount-point contents and permissions; pre-existing
volumes or bind mounts must be provisioned with matching ownership and modes
by the operator. User-namespace/rootless mappings must be accounted for.
The image never chowns an existing mount or runs the node as root.

State contains ``device.sqlite``, private ``keys/`` files, journals,
``node.lock``, and ``status.json``. Keys are plaintext 32-byte files, mode
``0600``, in an owned ``0700`` directory. The backend rejects symlinks and unsafe
ownership/permissions, uses descriptor-relative access, and publishes keys
atomically without overwriting existing identities. Use a local filesystem
with reliable locking, atomic rename, and fsync support. Keep state outside
all notes, synced Vaults, source control, and shared home directories.

File permissions are not encryption: host root, the container engine, the
dedicated UID, and anyone with access to a state backup can read the private
device, Vault, and Relay secrets. Do not share that UID with unrelated work.
Protect backups accordingly; deleting files does not guarantee secure erasure.
Never place secrets in environment variables, image layers, command arguments,
or logs. Missing keys do not cause automatic replacement identities. Preserve
the existing state and use explicit recovery/admission procedures instead.

Development enrollment
----------------------

The following Bash examples use only new development-named volumes. Before
running them, arrange a separate development authority with trusted HTTPS and
an administrator able to approve admission. Do not disable TLS verification.
For a private authority, ``--ca-file`` accepts additional public PEM CA
certificates and appends them to Qt's default HTTPS trust configuration. It
does not disable certificate or hostname verification. Supply only CA
certificates, never a private TLS key. The file must be readable by UID 10001;
the certificates are public, but their integrity determines whom the node trusts.

Create the two development volumes and a convenience command::

  docker volume create athena-hodarium-node-dev-state
  docker volume create athena-hodarium-node-dev-vault
  node() {
    docker run --rm --network host \
      --read-only --tmpfs /tmp:rw,noexec,nosuid,nodev,mode=1777 \
      --cap-drop ALL --security-opt no-new-privileges \
      --mount type=volume,src=athena-hodarium-node-dev-state,dst=/var/lib/athena-hodarium-node \
      --mount type=volume,src=athena-hodarium-node-dev-vault,dst=/vault \
      athena-hodarium-node:dev "$@" \
      --state /var/lib/athena-hodarium-node
  }

Initialize only the empty test Vault and request fresh enrollment. Substitute
the development authority's actual HTTPS origin for the reserved example::

  node init-vault --root /vault
  node enroll --server https://hodarium-dev.example.org --name dev-headless-node

When using a private CA, add
``--mount type=bind,src=/absolute/public/dev-ca.pem,dst=/run/dev-ca.pem,readonly``
to the helper's ``docker run`` options and the long-lived ``serve`` container
below. Add ``--ca-file /run/dev-ca.pem`` to the ``enroll`` and ``serve`` commands
(including resumed enrollment). This trust addition is per process, not saved
in node state; keep supplying the mount and option after container recreation.
It also applies to Qt HTTPS connections to Relays in that process, not to the
independent peer-identity authentication inside the transport.

``init-vault`` refuses a nonempty root. ``enroll`` emits JSON including a
rotating ``admission_code`` while pending. The administrator must approve the
request in the existing development authority panel. The code alone is not
membership or permission to transfer documents. Treat enrollment output as
sensitive and do not forward it to shared logs. Enrollment waits for validated
authorization and then exits; interruption before that returns failure. To
resume a saved pending profile, use ``node enroll --group GROUP_ID``.

Take ``GROUP_ID`` from the enrollment report and ``VAULT_UUID`` from the shared
test Vault identity used by the other admitted test replica. They are identifiers,
not secrets; do not invent a different UUID for each replica. Then::

  node bind --group GROUP_ID --vault VAULT_UUID --root /vault

Binding selects an existing native-node-model Vault; it is not a remote-clone
wizard or proof of completed synchronization. The optional
``--allow-code-resources`` on ``bind`` permits style/sorter replication as inert
data, never execution. Omit it unless explicitly needed for the test.

Serve and stop
--------------

Run the admitted development node with the same persistent volumes::

  docker run -d --name athena-hodarium-node-dev --network host \
    --read-only --tmpfs /tmp:rw,noexec,nosuid,nodev,mode=1777 \
    --cap-drop ALL --security-opt no-new-privileges \
    --mount type=volume,src=athena-hodarium-node-dev-state,dst=/var/lib/athena-hodarium-node \
    --mount type=volume,src=athena-hodarium-node-dev-vault,dst=/vault \
    athena-hodarium-node:dev serve --state /var/lib/athena-hodarium-node

There is no automatic restart policy in this example. ``SIGINT``/``SIGTERM``
request graceful shutdown. Stop and wait before every offline command::

  docker stop --time 30 athena-hodarium-node-dev
  node disable --group GROUP_ID
  node enable --group GROUP_ID
  docker start athena-hodarium-node-dev

All commands except ``status`` take the exclusive state lock. This includes
``enroll``, ``init-vault``, ``bind``, ``unbind``, ``enable``, ``disable``,
``relay-add``, ``relay-remove``, and even the read-oriented ``conflicts`` command.
Do not run them with ``docker exec`` against an active daemon or delete lock
files to bypass contention. Stop the daemon first, run a one-shot command with
the same mounts/UID, and restart afterward. ``serve`` and ``enroll`` also lock
bound Vaults. If the stop timeout forces termination, do not treat it as a
successful graceful flush; inspect restart diagnostics.

Other offline commands, with the daemon stopped::

  node conflicts --group GROUP_ID --vault VAULT_UUID
  node unbind --group GROUP_ID --vault VAULT_UUID

``conflicts`` reports JSON entries, not a resolution operation. Rebind explicitly
before expecting that Vault to synchronize again. Unbinding is not remote
deletion or revocation of group membership.

Networking and optional Relay
-----------------------------

On Linux, host networking lets the node advertise host interfaces and its
dynamic direct-peer listener port. The CLI has no fixed listen-port option;
there is deliberately no misleading ``EXPOSE`` or single ``-p`` mapping here.
Host networking removes Docker's network isolation and does not bypass host
firewalls or upstream NAT. Allow only the intended development peers. It is not
NAT hole punching, and routability is not guaranteed.

For unreachable direct addresses, configure a shared development Relay on both
peers. A bridge-network container can use outbound authority/Relay HTTPS instead
of host networking, but its private advertised addresses may not be directly
reachable. Relay traffic retains peer-authenticated end-to-end encryption;
the Relay forwards encrypted streams, is not a member, and is not an offline
mailbox. Both endpoints must be online. See ``../README.rst`` for Relay service
configuration and token format.

With the daemon stopped, supply a separate private regular token file owned by
the node UID, mode ``0600``, outside both the Vault and image/build context.
The file contains the Relay's 32-byte token encoded as unpadded base64url.
The following path is a placeholder for an operator-provisioned development
file; its contents never appear on the command line::

  docker run --rm --read-only --cap-drop ALL --security-opt no-new-privileges \
    --mount type=volume,src=athena-hodarium-node-dev-state,dst=/var/lib/athena-hodarium-node \
    --mount type=bind,src=/absolute/private/dev-relay-token,dst=/run/relay-token,readonly \
    athena-hodarium-node:dev relay-add --state /var/lib/athena-hodarium-node \
    --group GROUP_ID --relay https://relay-dev.example.org --token-file /run/relay-token

The imported credential is retained in private state; the token-file mount is
not needed by ``serve``. Protect or remove the provisioning copy according to
your development secret policy. ``node relay-remove --group GROUP_ID --relay
https://relay-dev.example.org`` removes that configured route while offline;
do not treat route removal as secure erasure or rotation of the Relay's shared
credential. Restart the daemon after configuration.

Status is not convergence
-------------------------

``status`` is the one command intended to run alongside the daemon::

  docker exec athena-hodarium-node-dev athena-hodarium-node status \
    --state /var/lib/athena-hodarium-node

It reads ``status.json`` without the state lock. A successful exit requires
``running: true`` and a timestamp less than 15 seconds old; the daemon normally
publishes every five seconds. This is only a heartbeat/liveness check. Missing,
stopped, or stale status returns failure. It is not an RPC or an unconditional
proof that a PID is alive, and a recently crashed process can leave a fresh
snapshot temporarily. Host clock changes also affect this age calculation.

Inspect these separately:

* Heartbeat: ``running`` and ``timestamp`` show snapshot freshness. Even a
  process with no enrolled profiles can have a successful heartbeat.
* Membership: each profile's ``phase`` and ``diagnostic`` describe authorization
  (for example ``pending``, ``authorized``, ``offline-valid``, ``expired``, or
  ``denied``). Heartbeat success does not imply an authorized profile.
* Connectivity: ``routes`` reports ``established``, ``rtt_ms``, and
  ``bytes_per_second``; discovery/decision diagnostics are separate. A live
  route or measured throughput does not establish content convergence.
* Synchronization: listed Vaults show bindings and whether they are enabled,
  not a sync-complete flag. Verify expected test documents and revisions on
  both replicas, restart persistence, and offline ``conflicts`` output. Neither
  admission nor an empty conflict list proves all content has arrived.

Logs can contain identifiers, local paths, and diagnostics; enrollment output
can contain admission codes. Keep them private. No healthcheck in this image
claims membership, sync completion, destructive-recovery acceptance, or
production readiness.
