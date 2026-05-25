<TeXmacs|2.1.4>

<style|<tuple|tmdoc|english|old-spacing|old-dots|old-lengths>>

<\body>
  <\tmdoc-title>
    Welcome to <ATHENA> version 0.1
  </tmdoc-title>

  Thank you for using <ATHENA>.

  <ATHENA> stands for the <em|Advanced Typesetting and Hypertext Environment
  for Notes and Archives>. It is a mathematics-centered knowledge work
  environment built from GNU <TeXmacs>, combining structured WYSIWYG
  typesetting with vaults, wikilinks, transclusions, namespaces, rendered
  search, and import tooling for large mathematical note collections.

  <\description>
    <item*|Warning: not for everyone>

    Use <ATHENA> only if you are disappointed with <strong|both <LaTeX> and
    Obsidian>. If Markdown plus plugins is enough, use Obsidian. If
    source-first batch typesetting is enough, use <LaTeX>. <ATHENA> exists
    for the uncomfortable middle: interactive mathematical writing at scale.

    <item*|Vaults, wikilinks, and transclusions>

    <ATHENA> vaults are self-contained mathematical knowledge bases. They
    support UUID-backed wikilinks to files, anchors, and theorem-like blocks;
    preview-backed wikilink insertion; self-repair of moved links; and
    transclusions of enunciations or anchored document ranges.

    <item*|Namespaces>

    Namespaces classify files by filename templates rather than by filesystem
    folders. <ATHENA> supports abstract, semi-concrete, and concrete
    namespaces; namespace homepages; namespace summaries at
    <samp|tmfs://ns/!name>; namespace-aware quick switching; namespace-aware
    search; namespace-aware file creation; custom C sorters; and generated
    sub-product namespaces.

    <item*|Search and navigation>

    The global search pane shows individual occurrences rather than just
    filenames. Hits can be filtered by namespace and enunciation type, and the
    preview pane renders a small read-only <ATHENA> document around the hit.

    <item*|Mathematical input>

    <ATHENA> provides Mathematica-style math shortcuts, an <key|Esc>-based
    symbol picker, convenient enunciation aliases, extended textual math
    operators, upright <samp|\\mathrm>, script <samp|\\mathscr>, boldsymbol
    input, paired angle brackets, and norm brackets.

    <item*|Enunciations>

    Theorem-like environments are treated as first-class structure. <ATHENA>
    supports many enunciation types, configurable colors, CJK line breaking,
    automatic anchors, proper solution rendering, and corrected display-first
    title layout.

    <item*|Obsidian/AOFM conversion>

    <ATHENA> can import Obsidian-style mathematical vaults, including
    wikilinks, transclusions, callouts, proofs, anchors, images, tables, PDF
    links, card links, formula normalization, generated titles, and table of
    contents.

    <item*|Maintenance and export>

    Vault maintenance can create zstd backups, purge old full backups and
    pre-save histories, collect orphan assets, and anchor enunciations across
    the whole vault. PDF export can optionally generate temporary DataArt
    cover images.

    <item*|Native Qt interface>

    <ATHENA> uses native Qt panes and dialogs for vault exploration,
    namespaces, global search, error messages, custom styles, wikilinks,
    transclusions, command palette, font selection, and color selection.

    <item*|Foundations and divergence>

    <ATHENA> is a fork of GNU <TeXmacs>. We gratefully acknowledge the
    decades of foundational work by <name|Prof. Joris van der Hoeven> and the
    <TeXmacs> contributors.

    <ATHENA> is <strong|not> a conservative distribution of GNU <TeXmacs>. To
    support its knowledge-management features, <ATHENA> introduces
    incompatible AST nodes and runtime behavior. <ATHENA> can often load
    upstream <TeXmacs> documents, but documents that use <ATHENA> features are
    not guaranteed to be readable by upstream <TeXmacs>.

    <item*|Development status>

    <ATHENA> 0.1 is active experimental software. Expect rough edges. Expect
    features to be deeper than their polish. The current build and runtime
    workflow is tested on Linux; inherited Windows and macOS code paths have
    not been tested for <ATHENA>.
  </description>

  For new users, we recommend \P<hlink|Getting started with
  <ATHENA>|start.en.tm>\Q.

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
    <associate|font|roman>
    <associate|font-family|tt>
    <associate|math-font|roman>
  </collection>
</initial>
