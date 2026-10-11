Hodarium joint validation, 2026-10-11
====================================

Scope and isolation
-------------------

Two new Linux ATHENA instances used separate profiles, keyrings, Xvfb displays
and empty fixture vaults under /tmp/athena-hodarium-joint-20261011. The user's
running instance and production Notes were not used. Bubblewrap hid the
production vault and profile from these processes.

A dedicated authority and Relay ran on ZBook, 100.127.82.98, bound only to its
Tailnet address on ports 17443 and 19444. TLS used the server's Tailnet hostname.
No public tunnel, production service, firewall change or system installation
was required. Admission exercised the actual WebAuthn administration interface
and rotating device verification codes, using an isolated virtual authenticator.

Results
-------

All scenarios below passed with real editor instances and native XML fixtures:

* Initial document transfer, with byte-for-byte comparison.
* P2P-only bidirectional realtime editing, including remote updates of open
  documents. A process-local network policy denied Relay connections; sockets
  between the test processes used the local Tailnet address, 100.81.168.89.
* Relay-only bidirectional realtime editing. Direct connections were denied;
  sockets on both hosts and the route display confirmed use of port 19444.
* Realtime persistence without a manual save or a menu action.
* Offline edits while the other instance was stopped, followed by automatic
  authentication and convergence after restart.
* Deletion propagation, protected pre-apply File History capture, restoration
  through the Document History UI, and propagation of the restored document.
  The restored file matched all 407 original bytes, including node UUIDs.
* Concurrent edits while both connections were paused, explicit presentation
  of two conflict branches, and manual branch adoption through the conflict UI.
  Both peers stored the same signed resolution approval and converged to the
  selected content. The resolution lists both divergent revisions as parents;
  this was not silent last-writer-wins replacement.

The accepted conflict resolution was
11680f3676d3081dc08dbbd721ee3b7caa880f8d3e724737a667ec11ca333929.

Defects found and fixed
----------------------

599f56864 publishes editor dirty state independently of menu updates. Ordinary
typing had skipped the only notification observed by realtime save.

993553022 preserves the owning buffer-data object during remote application.
A test-instance core dump proved that replacing it left the typesetting
environment referencing freed map members. Updating fields in place eliminated
the crash in subsequent open-document synchronization on both transports.

ea881ebf4 treats HTTP epoch conflicts as bounded membership-refresh hints,
cancels obsolete epoch requests after verified changes, and clears recovered
diagnostics. The original admission sequence exposed delayed recovery and stale
errors. This fix compiled and was present in the successful runs, but a fresh
membership-change scenario was not repeated afterward.

Build, evidence and limits
--------------------------

The Linux build used exactly:

::

   cmake --build build_qt6 --target ATHENA.bin -j20

The resulting binary was deployed to ATHENA/bin/ATHENA.bin. The user's running
process was not restarted. No full test suite was run.

Private local evidence remains under /tmp/athena-hodarium-joint-20261011:
evidence.json, build-fixes.log, per-instance logs and screenshots, and the
isolated crash core with reference inspection. This temporary directory also
contains test-only credentials; do not publish or archive it indiscriminately.

This run did not test iPad, free-form manual merge editing, expulsion, recovery
key rotation or hostile-network security. The earlier iPad IPA does not include
the three Linux fixes above until rebuilt. Passing these integration scenarios
is not a security audit or evidence for untested platforms.
