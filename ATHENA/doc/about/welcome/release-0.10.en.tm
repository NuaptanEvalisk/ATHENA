<TeXmacs|2.1.4>

<style|<tuple|tmdoc|english>>

<\body>
  <tmdoc-title|ATHENA 0.10: changes since 0.9>

  Like 0.9, this release distributes source code only. <ATHENA> remains
  experimental; 1.0 is reserved for a later release.

  <section|Native documents and identities>

  Native text is UTF-8, with Unicode grapheme-aware editing. Documents use
  XML v2 and styles use ATS XML. Optional UUIDs and typed properties belong
  directly to source nodes, including atomic paragraphs. Enunciations share
  one node type with a kind property and structured metadata.

  Wikilinks and transclusions use source UUIDs, not generated anchor pairs.
  A transclusion can select an ordered list of source nodes. The location
  cache can be rebuilt from documents. Artifact identities bind to source
  nodes; the cache does not own those identities.

  Unicode shaping uses ATHENA font selection, HarfBuzz, and FreeType without
  Pango. PDF export is native; the PostScript-to-PDF and distillation paths
  have been removed. Ghostscript is retained for PS/EPS import.

  <section|Workspace and editing>

  Native C++ menus and toolbars use JSON declarations. The application owns
  its menubar, command palette, and global shortcuts. Closing every buffer
  leaves the workspace open. Editor toolbars belong to their document views,
  and Focus commands follow the active work pane. Generic editing helpers,
  mathematical shortcuts, and LaTeX command lookup have moved substantially
  from Scheme to native code and data.

  Native drawing provides pen and shape tools, shape recognition, lasso
  transforms, horizontal and vertical space insertion, and trimming.

  <section|Background work and persistence>

  UUID indexing, Continuous Maintenance, artifact extraction, and optional
  NPU RAG share progress and error reporting. Realtime artifact overlays let
  radioactive links match newly edited definitions across documents. Full
  artifactization can supplement structural extraction with range selection.

  Intel NPU embedding runs in a separate OpenVINO worker and processes the
  active vault until it converges. It requires compatible runtime, driver,
  model and tokenizer files; weights are not included in the source release.

  Realtime save, document history, and recovery protect different stages of
  editing. Backup dispatch uses SQLite snapshots for live databases and can
  follow successful realtime saves.

  <section|External interoperation>

  AUDM/AUDMAP supplies authenticated external access and managed subprocess
  plugins, standalone C++ and Python clients, and an interactive REPL.
  Document model v3 exposes node UUIDs and typed properties. Buffer resolution
  and relative insertion support editing through plugins. The source tree
  includes a Python Hello World example under <samp|ATHENA/examples>.
  Internal linking and rendering continue to use tmfs, not external protocol
  requests.

  <section|Upgrading a vault>

  <strong|Back up the vault and close every application using it before
  migration.> For legacy 0.9 data, run these headless commands in order,
  substituting the real vault directory:

  <\verbatim>
    ATHENA/bin/ATHENA.bin --upgrade-vault-format /path/to/vault

    ATHENA/bin/ATHENA.bin --upgrade-vault-node-model /path/to/vault
  </verbatim>

  A vault already in UTF-8/XML v1 needs only the node-model upgrade. The
  offline tools validate and publish transactional migrations with recovery
  snapshots. Read the diagnostics, including unresolved old references, before
  resuming editing. Do not delete artifact databases: the upgrade preserves
  existing identities and associated results. Keep your backups; old ATHENA
  versions are not expected to read the new format or protocol.

  <section|Documentation and compatibility>

  Compatibility with GNU <TeXmacs> is not guaranteed in either direction.
  Supported legacy data remains importable, but retired features are not
  restored by importing their files. Bundled help temporarily retains legacy
  markup; this is not the format for new user documents. Much of the inherited
  manual awaits the planned 1.0 rewrite and may describe retired behavior.

  <tmdoc-copyright|2026|Nuaptan Felix Evalisk>

  <tmdoc-license|Permission is granted to copy, distribute and/or modify this
  document under the terms of the GNU Free Documentation License, Version 1.1
  or any later version published by the Free Software Foundation; with no
  Invariant Sections, with no Front-Cover Texts, and with no Back-Cover Texts.
  A copy of the license is included in the section entitled "GNU Free
  Documentation License".>
</body>
