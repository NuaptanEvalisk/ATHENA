<TeXmacs|2.1.4>

<style|tmdoc>

<\body>
  <tmdoc-title|What is new in <ATHENA> 0.6>

  <ATHENA> 0.6 develops six connected parts of the system: semantic reuse,
  delegated computation, AI-assisted editing, native mathematical structures,
  Web access, and operational reliability. This page summarizes user-visible
  changes since version 0.5.

  <section|Artifacts become reusable knowledge>

  The wikilink and transclusion inserters now offer an Artifact-based locating
  workflow. It searches the vault's SQLite Artifact index instead of loading
  every document, shows a rendered preview, and can reuse indexed anchor ranges
  for enunciations. When a bold-text definition has no reusable range,
  <ATHENA> asks before inserting anchors into the live source document.

  Artifact definition-span inference may run locally or through ATHENA
  Delegation. The remote path submits only keywords and candidate paragraph
  text to an authenticated FIFO service. The backend microbatches requests,
  while the local application validates every result and does not begin its
  database transaction until the complete job succeeds.

  <section|One delegation protocol>

  Continuous RAG embedding and Artifact definition-span selection share the
  authenticated ATHENA Delegation protocol. The standalone
  <verbatim|athena-transmitter> can forward both workloads to a remote ATHENA
  backend, maintain encrypted identities, and coordinate optional wake and
  shutdown hooks without teaching the desktop application about the surrounding
  network.

  RAG delegation remains incremental: only changed <verbatim|.ath> documents
  are submitted, interrupted batches resume, and validated SQLite row patches
  are merged locally. Vault assets, backups, preferences, maps, and existing
  databases are not included.

  <section|Codex completion>

  <menu|Edit|AI|AI completion (custom)> selects a supported Codex model,
  reasoning effort, Fast service tier, web-search permission, and inline or
  detached output. <menu|AI completion (new buffer)> uses the ordinary defaults
  but places the editable result in a popped-out document pane.

  If a selection contains images, native graphics, or commutative diagrams,
  <ATHENA> renders canonical temporary PNG assets, inserts matching placeholders
  into the textual prompt, and supplies the images through Codex multimodal
  input. Temporary prompt, response, and image files are removed after the
  request.

  <section|Native mathematical editing>

  Commutative diagrams are native <ATHENA> AST objects rather than variants of
  the legacy graphics object. Vertices contain editable formulas; dragging
  between vertices creates semantic arrows. Hover geometry, keyboard
  navigation, live arrow styling, grid sizing, and <verbatim|tikz-cd> export
  support direct diagram editing while preserving exact structured round trips.

  Native two-argument forms are available for binomial coefficients and
  Stirling numbers of both kinds. <key|Tab> and <key|Shift+Tab> cycle the
  notation without changing either argument.

  Selection and formatting fixes preserve tree order during structured
  recoloring, allow selections to cross equation-array and table boundaries,
  and bound edge scrolling so a small upward drag cannot unexpectedly select
  the beginning of a document.

  <section|Web-accessible ATHENA>

  <verbatim|athena-web-server> provides a normal native-Wayland ATHENA desktop
  over WebRTC. Every browser tab receives a dedicated disposable Weston
  environment with a file manager, terminal, Upload and Download directories,
  and enforced resource and time limits. The untrusted desktop has no access to
  host files, devices, or the server network. Try the public demonstration at
  <hlink|athweb.evalisk.org|https://athweb.evalisk.org/>.

  <section|Maintenance, diagnostics, and distribution>

  Vault Maintenance now discovers local file references structurally across
  images, hyperlinks, card links, includes, and media. It normalizes all
  referenced vault assets, including PDFs and arbitrary linked files, without
  breaking relative references or misclassifying them as orphans.

  A per-document HUD reports completed-paint FPS, latest editing latency, and
  five-second p95 latency. Persistent debugging controls have moved into
  <menu|Edit|Preferences|Other|Debugging>, while immediate inspection commands
  remain in the Debug menu.

  The Qt 5 frontend has been removed; <ATHENA> 0.6 requires Qt 6. A unified
  model-free release driver produces native Linux archives, AppImages, DEB and
  RPM packages, and Wine-checked Windows ZIPs without accidentally bundling
  optional model weights.

  Malformed <verbatim|.ath> and style-package source now reports the exact file,
  line, and column instead of silently accepting a partial parse that later
  renders raw tags.

  <tmdoc-copyright|2026|Nuaptan Felix Evalisk>

  <tmdoc-license|Permission is granted to copy, distribute and/or modify this
  document under the terms of the GNU Free Documentation License, Version 1.1
  or any later version published by the Free Software Foundation; with no
  Invariant Sections, with no Front-Cover Texts, and with no Back-Cover
  Texts. A copy of the license is included in the section entitled "GNU Free
  Documentation License".>
</body>

<initial|<\collection>
  <associate|language|english>
</collection>>
