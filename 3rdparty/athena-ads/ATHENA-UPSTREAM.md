# ATHENA Qt Advanced Docking System source

This directory vendors the Qt Advanced Docking System source used by ATHENA.
It is maintained as an ATHENA-private fork rather than patched after download.

- Upstream project: Qt Advanced Docking System
- Upstream release: 4.3.1
- Upstream commit: `04f6d9168e159f07de565c5159ecd4ea16ab5be1`
- Upstream repository: `https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git`
- Upstream license: GNU LGPL version 2.1; see `LICENSE` and `gnu-lgpl-v2.1.md`

ATHENA carries only the buildable library source and supporting CMake/license
files. Upstream documentation, demos, examples, SIP bindings, packaging files,
and repository metadata are omitted because they are not part of ATHENA's ADS
runtime.

Local changes are made directly in this tree. They implement ATHENA's native
Wayland floating-pane/redocking behavior, overlay positioning, single-floating
tab/title handling, and focus behavior. The library also builds against Qt 6
private GUI headers required by the inherited ADS platform-native integration.

Do not reintroduce a generated patch step. When updating ADS, import the new
upstream source into this directory, rebase the ATHENA-specific changes here,
update the provenance above, and verify the normal ATHENA build and docking
regressions.
