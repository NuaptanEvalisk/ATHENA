<TeXmacs|2.1.4>

<style|<tuple|tmdoc|english|old-dots|old-lengths>>

<\body>
  <tmdoc-title|Getting started with <ATHENA>>

  This short guide introduces the mental model of <ATHENA>. The application
  inherits the structured editing and mathematical typesetting core of GNU
  <TeXmacs>, but adds vaults, wikilinks, transclusions, namespaces, rendered
  search, and mathematical-note workflows.

  <section|Documents are structured trees>

  <ATHENA> is not a plain-text editor. A document is a tree of structured
  elements. Move the cursor inside this <strong|piece of bold text>: the
  focus box and the status bar reveal the current structure. In mathematics,
  the same mechanism shows whether the cursor is inside a fraction, script,
  root, table cell, label, or other compound object.

  This is why <ATHENA> is useful for mathematics: it stores what you mean,
  not merely how the characters happen to look. Styles translate structure
  into layout.

  <section|Mathematics>

  Enter math mode with the usual math commands or menu entries. Inside
  mathematics, <ATHENA> uses a modernized shortcut set:

  <\itemize>
    <item><key|Ctrl+/> inserts a fraction.

    <item><key|Ctrl+2> inserts a square root.

    <item><key|Ctrl+6> inserts a superscript.

    <item><key|Ctrl+-> inserts a subscript.

    <item><key|Ctrl+7> inserts an overscript.

    <item><key|Ctrl+4> inserts an underscript.

    <item><key|Ctrl+9> inserts text inside math.

    <item><key|Ctrl+Space> jumps out of the current structure.

    <item><key|Ctrl+\<gtr\>> or <key|Ctrl+.> enlarges the structural
    selection.
  </itemize>

  Press <key|Esc> to open the symbol picker at the cursor. It can insert
  Greek letters, blackboard symbols, operators, arrows, math fonts, paired
  brackets, norm bars, and snippets such as limits. Examples include
  <samp|hom>, <samp|spec>, <samp|ilim>, <samp|plim>, <samp|scr>, and
  <samp|bs>.

  <section|Enunciations>

  Mathematical notes are full of theorem-like environments. <ATHENA> treats
  these as first-class structure: theorem, lemma, definition, proposition,
  corollary, conjecture, axiom, question, example, remark, caution,
  disambiguation, solution, proof, and alternative proof.

  Useful aliases include <samp|\\def>, <samp|\\thm>, <samp|\\lem>,
  <samp|\\prop>, <samp|\\cor>, <samp|\\eg>, <samp|\\rem>, <samp|\\qsn>,
  <samp|\\sln>, and <samp|\\pf>. Enunciations can be auto-anchored so they
  become stable link and transclusion targets.

  <section|Vaults>

  A vault is a directory of <samp|.ath> files plus vault metadata. Load or
  create a vault before using vault-wide features. Once a vault is active, you
  can use:

  <\itemize>
    <item>Vault Explorer for filesystem-oriented browsing.

    <item>Quick Switcher for fast file opening.

    <item>Global Search for occurrence-level rendered search.

    <item>Backup Viewer and Vault Maintenance for history and cleanup.
  </itemize>

  Vault-scoped preferences allow a vault to carry its own look, behavior, and
  namespace database.

  <section|Wikilinks and transclusions>

  <ATHENA> links are designed for structured mathematical documents. Use the
  wikilink wizard to locate a file first or search for a target. The wizard
  can preview rendered context and insert a stable link to a file or anchor.

  Transclusions embed source content into the current document. They are
  useful for restating a definition, theorem, or proof fragment in another
  context while preserving a link to the source.

  <section|Namespaces>

  Namespaces organize files by templates rather than filesystem folders. For
  example, a namespace may describe lecture notes, a course, or the
  intersection of the two. Concrete namespaces may also define a style and
  initial content for new files.

  Use the Namespace Manager to create and edit namespaces. Use the Namespace
  Explorer to browse files through namespace hierarchy. Namespace pages are
  available through <samp|tmfs://ns/name>, while
  <samp|tmfs://ns/!name> opens the technical summary.

  <section|Search>

  Global Search can search the whole vault or a selected namespace. It can
  also restrict hits to a chosen enunciation type. Results are listed as
  individual occurrences, and the preview pane renders a small read-only
  neighborhood around the hit.

  <section|Import and maintenance>

  The AOFM converter imports Obsidian-style mathematical vaults into
  <ATHENA>, including wikilinks, transclusions, callouts, proofs, anchors,
  images, tables, PDF links, card links, formula normalization, generated
  titles, and table of contents.

  Vault Maintenance can create compressed backups, purge old backups and
  pre-save histories, normalize structurally referenced assets of any file
  type, collect orphan assets, and anchor enunciations across the whole vault.

  <section|Where to continue>

  Explore the menus for Vault, Search, Namespaces, Preferences, and Document
  style management. <ATHENA> is powerful but experimental: expect rough edges,
  especially in recently added preview-heavy workflows.

  <\tmdoc-copyright>
    1998\U2026

    2026
  <|tmdoc-copyright>
    <person|Joris van der Hoeven>

    <person|Nuaptan Felix Evalisk>.
  </tmdoc-copyright>

  <tmdoc-license|Permission is granted to copy, distribute and/or modify this
  document under the terms of the GNU Free Documentation License, Version 1.1
  or any later version published by the Free Software Foundation; with no
  Invariant Sections, with no Front-Cover Texts, and with no Back-Cover
  Texts. A copy of the license is included in the section entitled "GNU Free
  Documentation License".>
</body>

<\initial>
  <\collection>
    <associate|preamble|false>
  </collection>
</initial>
