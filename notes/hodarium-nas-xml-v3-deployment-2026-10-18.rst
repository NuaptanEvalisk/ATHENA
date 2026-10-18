NAS XML v3 node deployment, 2026-10-18
=====================================

The production TrueNAS application ``athena-hodarium-node`` now uses
``localhost/athena-hodarium-node:xml-v3-6b5817f5c``. The user explicitly
authorized erasing this NAS replica's old-format Vault. No local Notes files
were modified and no new member enrollment was performed.

Build and verification
----------------------

Source commit: ``6b5817f5c8ad4240e453ab48ddba8f8b232916cf``.
Image ID:
``sha256:d1fb88962e50851f6e3b6c23339f7bb5e707371e0e1fd3e83d75791320b455a5``.
Installed node executable SHA-256:
``6e60805d28503df17e14906d6922eb8d35240f3a3b6ad36e98e2ce95f64ce27b``.

Debian dependency downloads stalled. The successful build reused the existing
Debian Trixie build-container image ``64e30d2b997a`` and existing runtime image,
compiled the committed source in a fresh source/build directory with networking
disabled, and replaced the node executable. No host packages were installed.
The normal clean-build recipe remains ``tools/hodarium/node/Dockerfile``.
An isolated container initialized a fresh Vault with ``node_model_version=2``.
Archive and executable checksums matched after transfer to the NAS.

Production cutover
------------------

The application was stopped with the TrueNAS middleware API. Only the contents
of ``/mnt/tank/apps/athena-hodarium-node/vault`` were deleted, after checking its
old model version, mount boundaries, binding and stopped-container state.
``init-vault`` then initialized the empty directory with the new image.

``/mnt/tank/apps/athena-hodarium-node/state`` was preserved. Private-key hashes
were checked before and after the change without exposing key contents.
Existing membership and Vault bindings were retained. XML v3 uses separate
``revisions-v3-*`` journals; old private-state journals were not deleted or
replayed. The application's existing Compose configuration was updated through
``midclt call -j app.update`` and restarted through ``app.start``.

The original member ID and Vault ID remained unchanged. Post-start status showed
``authorized``, an inventoried enabled Vault, empty diagnostics, no conflicts
and no pending applications. The previous ``Invalid Vault announcement``
diagnostic disappeared. ZBook authority and Relay services were not changed.

Acceptance boundary
-------------------

No local ATHENA instance was running at this check, and the NAS had no established
peer route. This verifies deployment, identity retention and startup, not content
convergence. The empty replica must receive documents from an online v3 desktop
or other v3 member. Do not roll the old v2 image back onto a populated v3 Vault.
