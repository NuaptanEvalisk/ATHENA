<TeXmacs|2.1.4>

<style|<tuple|tmdoc|english>>

<\body>
  <tmdoc-title|ATHENA 0.11: changes since 0.10>

  <ATHENA> 0.11 is an experimental release centered on the native XML v3
  document model, mathematical editing, and multi-device Hodarium replication.
  This page describes the source/runtime changes, not a promise that every
  platform has a published binary package.

  <section|Native XML v3 and mathematics>

  Native <samp|.ath> documents use a strict XML v3 representation. Persistent
  source UUIDs and typed node properties coexist with direct mathematical
  structures: paired delimiters, independent script slots, big-operator glyphs,
  display MathBlocks, and MathGrids. Native editing, cursor placement, undo,
  typesetting, and export have dedicated v3 paths. Unsupported lossless
  conversion is reported instead of silently flattened.

  <section|Vaults, history, and Hodarium>

  The offline XML v3 upgrade handles source documents, retained document
  history, and structured Artifact indexes. Its <samp|--check> and explicit
  <samp|--publish> commands are separate; no normal editor open should be
  interpreted as permission to rewrite an older Vault.

  Hodarium's v3 document-revision boundary rejects incompatible older
  clients. Use v3-capable desktop and headless members for the same Vault;
  do not mix older synchronization revisions into a migrated Vault.
  The Linux desktop and NAS headless node have been deployed and used with
  completed production convergence.

  <section|Additional editing and workspace changes>

  Recent development includes native equation-grid insertion and row editing,
  improved mathematical focus and bracket input, correct handling of
  generated auxiliary mathematical entries, and continued Qt, font, search,
  document identity, and rendering improvements.

  <section|Upgrading existing data>

  A Vault already in XML v3 with node model version 2 needs no further format
  migration. For an older Vault, close all writers, preserve a separate backup
  and validate an independent copy before running an explicit offline
  upgrade. The relevant command for a prepared XML v2 Vault is:

  <\verbatim>
    ATHENA/bin/ATHENA.bin --upgrade-vault-xml-v3 --check /path/to/copy
  </verbatim>

  Publication uses <samp|--publish>, not ordinary open/save. Older
  Cork/S-expression data may need prior legacy conversion and node-model
  upgrading. Keep Artifact databases and document histories intact.
  ATHENA 0.10 and older clients are not expected to read or synchronize
  canonical XML v3 Vaults.

  <section|Building and documentation>

  Linux x86-64 / Qt 6 / Wayland remains the maintained desktop configuration.
  The iPadOS arm64 target uses a separate macOS/Xcode and Qt for iOS toolchain,
  precompiled no-JIT Guile runtime, and application target <samp|ATHENA>.
  Device signing and packaging are separate from building the app bundle.
  The maintained build guide is the ATHENA document <em|ATHENA - Compiling>;
  it is also intended to be exported to PDF using ATHENA.

  <section|Compatibility>

  ATHENA is derived from GNU <TeXmacs>, but neither direction of document
  compatibility is guaranteed. Older support and migration paths may remain
  in the sources during cleanup, but they are not an alternate v3 format.

  <tmdoc-copyright|2026|Nuaptan Felix Evalisk>

  <tmdoc-license|Permission is granted to copy, distribute and/or modify this
  document under the terms of the GNU Free Documentation License, Version 1.1
  or any later version published by the Free Software Foundation; with no
  Invariant Sections, with no Front-Cover Texts, and no Back-Cover Texts.>
</body>
