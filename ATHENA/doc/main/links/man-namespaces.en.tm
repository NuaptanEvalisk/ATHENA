<TeXmacs|2.1.4>

<style|<tuple|tmdoc|english>>

<\body>
  <tmdoc-title|Namespaces in <ATHENA>>

  <ATHENA> namespaces are a mathematical structure on top of a vault. They are
  not folders, and they are not tags. A namespace describes a class of files by
  a filename template, orders the files by a sorting algorithm, and participates
  in a hierarchy of inclusions. The namespace tools use this structure for
  navigation, document creation, wikilinks, transclusions, search, graphs, and
  namespace export.

  <section|The namespace model>

  Let <math|V> be the active vault. A namespace <math|N> has a name
  <math|\|N\|>. In <ATHENA> there are three kinds of namespaces:

  <\itemize>
    <item>An <em|abstract> namespace has no filename template. It is used as a
    structural object in the hierarchy.

    <item>A <em|semi-concrete> namespace has a filename template
    <math|T<rsub|N>> and a sorting algorithm <math|S<rsub|N>>.

    <item>A <em|concrete> namespace is semi-concrete and also has a document
    style and optional initial content for creating new files.
  </itemize>

  A file <math|f> belongs to <math|N>, written <math|f\<in\>N>, when its
  filename satisfies <math|T<rsub|N>>. A namespace <math|N'> is a subspace of
  <math|N>, written <math|N'\<subset\>N>, when every file of <math|N'> also
  belongs to <math|N>. The hierarchy relation is therefore semantic: it is a
  relation between classes of files, not a relation between directories.

  Namespace pages are addressed by <verbatim|tmfs://ns/name>. A structured
  namespace path such as <verbatim|tmfs://ns/Course/Lecture> first checks that
  <verbatim|Lecture> is a subspace of <verbatim|Course>; if the relation fails,
  <ATHENA> shows an error page instead of silently resolving the last name.

  <subsection|Filename templates>

  Namespace templates are <verbatim|printf>-like patterns. They are deliberately
  weaker than regular expressions, so that <ATHENA> can expose the captured fields
  in file-creation dialogs and sorters.

  <\itemize>
    <item><verbatim|%s> captures an arbitrary string.

    <item><verbatim|%w> captures a word.

    <item><verbatim|%c> captures one character.

    <item><verbatim|%d> captures an integer, including zero and negative
    integers.

    <item><verbatim|%N> captures a positive integer.

    <item><verbatim|%R> captures a Roman numeral.
  </itemize>

  For example, a lecture namespace may use
  <verbatim|%w Lecture Notes %R>. The file
  <verbatim|REAL Lecture Notes IV.ath> then has a course word
  <verbatim|REAL> and a Roman lecture number <verbatim|IV>. Filling the first
  field gives a derived namespace such as
  <verbatim|REAL Lecture Notes %R>.

  <subsection|Sorting algorithms>

  The sorting algorithm <math|S<rsub|N>> is a deterministic comparison on the
  captured fields of filenames matching <math|T<rsub|N>>. Two different names
  may compare equal; a sorter is complete when this never happens.

  Sorters are C files loaded through <verbatim|libtcc>. A sorter defines
  <verbatim|athena_ns_compare>. <ATHENA> passes the captured fields as
  <verbatim|AthenaNsField> values with text, field type, integer value, and Roman
  value. The helper functions <verbatim|athena_ns_strcmp>,
  <verbatim|athena_ns_strcasecmp>, <verbatim|athena_ns_cmp_int>,
  <verbatim|athena_ns_cmp_roman>, and <verbatim|athena_ns_roman_value> are
  available to sorter code. A trivial sorter is also available; it declares all
  matches equal.

  If <math|N'\<subset\>N>, then the child ordering should refine the parent
  ordering: whenever <math|S<rsub|N>(f,g)> is strict, the child sorter must not
  reverse it. <ATHENA> can generate restricted and product sorters for common
  namespace constructions, but the mathematical compatibility condition remains
  the user's responsibility.

  <subsection|Products and sub-products>

  If two semi-concrete namespaces <math|N> and <math|M> have compatible
  templates and sorters, their intersection may be represented as a sub-product
  namespace <math|N\<cap\>M>. Its template is obtained by unifying the parent
  templates, and its sorter is a product sorter. In the Real Analysis example,
  a course namespace and a lecture namespace can meet in the namespace of Real
  Analysis lecture notes.

  The universal namespace of a vault has template <verbatim|%s> and the trivial
  sorter. Every concrete or semi-concrete namespace is a subspace of it. At the
  other extreme, an individual file may be regarded as a discrete terminal
  namespace.

  <section|Namespace Manager>

  Use <menu|Tools|Namespace Manager> to create and maintain namespaces in the
  active vault. The manager opens as a dock pane. Its namespace list remains
  visible on the left, while the editor on the right is divided into five tabs:
  <em|Definition> contains the name, kind, filename template, and sorter;
  <em|Documents> contains the style, initial content, and homepage;
  <em|Hierarchy> contains explicit and derived parents; <em|Matched Files>
  shows the files selected by the saved template and sorter; and
  <em|Relation Decisions> edits the vault-wide allow and deny decisions used
  while deriving the hierarchy.

  The toolbar remains available on every tab. <em|Update namespace> saves all
  fields, including fields on tabs other than the visible one. When a namespace
  has unsaved changes, selecting another namespace asks whether to save,
  discard, or cancel the switch. A validation error selects the tab and field
  that need attention.

  To create a namespace, press <em|New namespace...>, choose whether it is
  abstract, semi-concrete, or concrete, then fill the relevant fields. Names
  must be non-empty and must not contain <verbatim|!>. For a semi-concrete or
  concrete namespace, enter a filename template and either choose a sorter C
  file or enable the trivial sorter. For a concrete namespace, also choose the
  style and optional initial content used for new documents.

  The manager distinguishes explicit parents from derived parents. Explicit
  parents are relations you enter manually. Derived parents are inferred from
  template derivations and sub-products. The relation editor records cached
  decisions as allow or deny entries. Use it when <ATHENA> cannot safely infer a
  hierarchy relation, or when a mathematical inclusion is intended but not
  visible from templates alone.

  The <em|Generate sub-products> command constructs namespaces from selected
  parents. For two semi-concrete parents, <ATHENA> suggests a unified template,
  asks you to confirm sorter compatibility, and writes a generated product
  sorter under <verbatim|.athena/ns-sorters>. For one semi-concrete parent and
  one abstract parent, <ATHENA> generates a restricted namespace. For abstract
  parents, it creates abstract sub-products.

  <section|Namespace Explorer>

  Use <menu|Tools|Namespace Explorer> to browse the namespace hierarchy. The
  explorer shows namespaces as expandable nodes and matching files as leaves.
  Double-clicking a file opens it. Double-clicking a namespace expands or
  collapses it.

  The toolbar has a refresh command and a <em|Leaf matches only> filter. When
  the filter is enabled, files are shown only under namespaces that have no
  child namespaces. This is useful when parent namespaces would otherwise repeat
  the same files at many levels of the hierarchy.

  The context menu on a namespace provides <em|Open homepage> and
  <em|Technical summary>. The context menu on a file provides file operations
  near the selected file: load, new file, new folder, rename, copy, paste,
  delete, open in the system file manager, and refresh.

  Rename uses <ATHENA>'s safe Vault rename operation. Before changing the
  filesystem, <ATHENA> shows how many files, UUID map rows, candidate documents,
  and local path references will be affected. Renaming a directory or a
  referenced asset first performs a fast textual candidate scan, then parses
  only those candidate documents and structurally updates image, ordinary link,
  include, and media paths. Relative references remain relative. UUID wikilink
  and transclusion hints, the RAG index, and recent-file entries are auxiliary
  data and are not rewritten by this operation.

  <section|Namespace homepages>

  A namespace may have an <ATHENA> homepage document. The homepage is configured
  in the manager using the <em|Homepage> field, the <em|Browse...>,
  <em|Create...>, and <em|Edit homepage> buttons. Opening
  <verbatim|tmfs://ns/name> displays the homepage if one is configured;
  otherwise <ATHENA> displays a technical summary.

  Homepage documents may contain dynamic namespace tags:
  <verbatim|<ns-name>>, <verbatim|<ns-type>>,
  <verbatim|<ns-sorting-algo>>, <verbatim|<ns-matches>>,
  <verbatim|<ns-children>>, <verbatim|<ns-parents>>,
  <verbatim|<ns-filename-template>>, and
  <verbatim|<ns-summary-link>>. When the homepage is loaded, <ATHENA> replaces
  these tags by the current namespace name, kind, sorter link, member list,
  children, parents, filename template, or technical-summary link. Relative
  image paths in the homepage are rebased relative to the homepage file.

  <section|Reverse hierarchy graphs>

  Use <menu|View|Graphs|Reverse hierarchy graph> to open a docked reverse
  hierarchy graph. Use <menu|Insert|Graph|Reverse Hierarchy> to insert such a
  graph into the current document. The command
  <verbatim|graph-rev-hierarchy> inserts the same graph from the keyboard
  command system.

  The reverse hierarchy of a namespace or file is the part of the namespace DAG
  in which the selected object is terminal. If <math|N> is terminal in
  <math|M>, then <math|M> belongs to <math|rev N>. This graph answers the
  question: in which larger mathematical contexts does this object appear as a
  leaf?

  Graphs are rendered from the current namespace database. If the graph is too
  dense, use the hierarchy simplification preference before inserting the graph
  into a document.

  When <menu|Edit|Preferences|Rendering|Misc|Graphs|Use interactive elastic
  graphs> is enabled, nodes in the Reverse, Direct, and Global hierarchy graph
  panes can be dragged. Connected nodes react only while a node is being
  dragged; the graph remains stationary before a drag and immediately after
  release. Drag empty graph background to pan the viewport. Hierarchy graphs
  inserted into documents remain static images.

  <section|Document reference graphs>

  For a saved <verbatim|.ath> note, use <menu|View|Graphs|Local reference
  graph> to inspect its immediate document dependencies. If note <math|A>
  contains a wikilink to <math|B>, or transcludes content from <math|B>, the
  graph contains the directed edge <math|B\longrightarrow A>. The highlighted
  node is the note whose graph is being shown. Double-click any node to open
  that note.

  <menu|View|Graphs|Reference graph> follows the same relation for a bounded
  number of levels. Its <em|Backtracking level> stepper defaults to <verbatim|2>:
  level <verbatim|1> is equivalent to the Local Reference Graph, while level
  <verbatim|2> also expands references made by the first-level notes. Enable
  <em|Unlimited> to compute the full recursive closure, which was the original
  Reference Graph behavior. Both panes follow the active viewport by default
  and provide a Refresh button. Hover a node to highlight every direct
  reference arrow pointing to it and the source nodes of those arrows. Hold
  <key|Shift> while hovering to recursively highlight the complete subgraph
  that leads to the node.

  The information area computes topological invariants of the graph currently
  displayed. It reports connected components and, component by component, the
  first integral homology group and free fundamental group of the underlying
  undirected one-complex. Loops and independent cycles contribute generators;
  opposite directed edges do not create duplicate undirected edges.

  Reference targets are resolved exclusively through the UUID map configured
  by <verbatim|Vaultfile.json> (<verbatim|map.sqlite> by default). Optional file
  and anchor hints stored in wikilink or transclusion tags are never treated as
  authoritative. <ATHENA> maintains the rebuildable cache
  <verbatim|.athena/reference-graph.sqlite> so unchanged documents do not need
  to be parsed again. Changes to the configured UUID map invalidate cached
  target paths automatically.

  <section|Websites manager>

  Use <menu|Tools|Websites manager> to define static websites for the active
  vault. Website definitions are stored in the vault registry named by the
  <verbatim|websites_path> field of <verbatim|Vaultfile.json>; by default
  this is <verbatim|websites.json>. Normal HTML export remains available
  separately.
  The removed legacy <em|Create web site> command is not used by this system.

  A website definition has a name, a selector, a destination folder, optional
  sitemap settings, a public description for generated metadata, an optional
  favicon, an entrypoint, an optional post-generation command, and a
  regeneration mode. A favicon path may be absolute or relative to the vault;
  leaving it empty uses the <ATHENA> logo. The selector is
  built visually from recursive path
  selectors, recursive namespace selectors, and boolean operators such as
  <verbatim|and>, <verbatim|or>, <verbatim|xor>, <verbatim|nand>,
  <verbatim|nor>, and <verbatim|not>. Empty selectors are rejected.

  Press <em|Generate now> to open a floating progress pane. <ATHENA> starts a
  separate headless process using
  <verbatim|ATHENA.bin --generate-website vault-root website-id>, exports the
  selected documents to HTML, writes the site shell and supporting indexes, and
  opens the configured entrypoint. Links to selected vault documents become
  relative links. Links outside the selected set become static modal messages.
  Transclusions are inlined during export. If sitemap generation is enabled,
  the website definition must also provide a public website base URL, and
  <ATHENA> writes a standards-compliant <verbatim|sitemap.xml> using the public
  base URL and absolute URLs below that base URL. The generated
  <verbatim|index.html> also receives a canonical link to the public base URL
  and the configured description as its search-engine description. If sitemap
  generation is disabled, no sitemap is written.

  Websites whose regeneration mode is <em|Vault maintenance> are regenerated by
  the maintenance pass after document-changing passes and before the final
  summary. Websites whose mode is <em|Manual> are skipped by maintenance.

  <section|Creating files in namespaces>

  Use <menu|File|New within namespace> to create a file from a concrete
  namespace. The wizard first asks for a concrete namespace, then asks for the
  fields captured by its filename template, then asks for a storage directory.
  The confirmation page shows the target filename, style, and initial content.

  The created file receives the namespace style and initial content configured
  in the manager. This is the preferred way to create files that are meant to be
  members of a concrete namespace: the filename, document style, and initial
  body are all derived from the same namespace definition.

  When creating an ordinary file whose name already matches concrete
  namespaces, <ATHENA> may offer to initialize the file from one of those matching
  namespaces, or to create a plain <ATHENA> document.

  <section|Wikilinks and transclusions>

  Use <menu|Insert|Link|Wikilink> to insert a vault wikilink, or press
  <shortcut|(insert-wikilink)> through the command/keyboard system. Use
  <menu|Insert|Link|Transclusion> to insert a transclusion. In text mode,
  <ATHENA> also binds <verbatim|=> to wikilink insertion and <verbatim|+> to
  transclusion insertion.

  A wikilink stores a stable vault UUID and file/anchor hints in a
  <verbatim|tmfs://wikilink/...> URL. If the target file or anchor moves, <ATHENA>
  can use the hints to repair the link. A transclusion stores a UUID and
  optional begin/end anchors, imports the target body, strips labels, rebases
  images, detects cyclic transclusions, and displays the result as an
  ornamented block.

  Namespace data affects these workflows because the chooser operates on vault
  files and anchors that are organized by namespace membership. A well-defined
  namespace hierarchy gives stable mathematical context to the files chosen for
  wikilinks and transclusions.

  <section|Search and quick switching>

  Use <menu|Edit|Global search> to search the current vault. The global search
  pane can restrict a search to one namespace. Its <em|Namespace> field accepts
  a namespace name and offers namespace completion. If the field is empty,
  <ATHENA> searches all <ATHENA> files in the current vault. If it contains a
  namespace name, <ATHENA> searches only the matching <verbatim|.ath> files of
  that namespace. The <em|Enunciation> filter further restricts matches to
  theorem-like environments such as theorem, proposition, lemma, definition,
  example, proof, and related tags.

  Use <menu|Tools|Quick switcher> for fast vault navigation. The quick switcher
  has a structured namespace mode. At the root it lists namespaces. Entering a
  namespace lists child namespaces and matching files. Opening a namespace
  entry loads its <verbatim|tmfs://ns/...> page; opening a file entry loads the
  corresponding vault file. The parent navigation entry shows parents of the
  current namespace when moving upward through the hierarchy.

  <section|Neighborhoods and gestures>

  A document neighborhood is a local ordering of <verbatim|.ath> files around
  the current note. <ATHENA> currently builds two kinds of natural
  neighborhoods.

  <\itemize>
    <item>The <em|Path> neighborhood contains the <verbatim|.ath> files in the
    same directory as the current note, sorted alphabetically.

    <item>An <em|NS: name> neighborhood contains the files in a direct
    namespace containing the current note. A containing namespace is direct
    when no more specific containing namespace lies between it and the note.
    Namespace neighborhoods use the namespace's sorter.
  </itemize>

  Use <menu|View|Neighborhoods> to open the neighborhoods viewer. It is an ADS
  pane, floating by default. Each row is one neighborhood. The cells in a row
  are files, and the cells representing the current note are aligned in one
  highlighted column. Selecting a row chooses the active neighborhood for the
  current note; double-clicking a file cell opens that note in the current
  viewport. The viewer follows the current viewport.

  On platforms where Qt delivers gestures, such as native Wayland and Windows,
  neighborhood navigation is available from the document editor. Swiping left
  opens the left neighbor and swiping right opens the right neighbor. If the
  current note is already at the edge, nothing happens.

  Some desktops deliver horizontal touchpad movement as ordinary horizontal
  scrolling when the document has a bottom scrollbar. In that case, hold
  <key|Shift> while swiping horizontally: <key|Shift>+swipe left opens the
  left neighbor and <key|Shift>+swipe right opens the right neighbor, without
  horizontally scrolling the document. The keyboard equivalents are
  <key|Ctrl+Alt+Left> and <key|Ctrl+Alt+Right>.

  A three-finger tap cycles the selected neighborhood and shows a toast with
  the new selection. On KDE, three-finger tap may be delivered as a middle
  mouse click; when <menu|Edit|Preferences|Editing|Text|Disable UNIX primary
  selection> is enabled, middle-clicking in the document editor has the same
  cycle-neighborhood effect. These gestures and shortcuts operate only on
  ordinary vault <verbatim|.ath> documents; system pages, namespace homepages,
  and other non-file buffers do nothing.

  The initial selected neighborhood is controlled by
  <menu|Edit|Preferences|Vault|Navigation|Preferred initial
  neighborhood>.

  <section|Namespace export>

  Use <menu|Tools|Export namespace> to export a namespace as a document. <ATHENA>
  builds a graph from the selected root namespace and asks you to select a
  rooted export tree. Selected leaves must be terminal file-class nodes. This
  selection is a substantial tree in the augmented hierarchy: it chooses how the
  namespace DAG is linearized into a book-like document.

  The export options dialog asks for the author and whether to include the
  current date, a DataArt cover image, a selected namespace hierarchy page, and
  reverse hierarchy information in the exported graph. The graph shown in the
  exported document is the selected hierarchy; if reverse hierarchy is enabled,
  additional reverse-hierarchy vertices are included as grey vertices in that
  exported graph.

  The exported document uses the <verbatim|namespace-export-book> style. It
  contains cover metadata, a table of contents, the optional selected namespace
  hierarchy page, and then the selected namespace tree. Namespace levels become
  parts, chapters, and deeper heading levels. Terminal file-class leaves become
  sections whose bodies are imported from the source files. During import,
  <ATHENA> removes source-only metadata such as top-level document data and
  generated contents, rebases images, demotes headings when necessary, and
  rewrites exported wikilinks so that links between exported files point to the
  generated labels in the exported document.

  The mathematical point is that export is not a directory traversal. The
  namespace hierarchy is a directed acyclic graph whose arrows are inclusions
  <math|N'\<subset\>N>. A file may lie in several incomparable namespaces, and a
  namespace may have several parents. Such a graph cannot be printed as a book
  until one chooses a tree inside it.

  For a chosen root <math|N>, the ordinary hierarchy <math|hr N> consists of
  namespaces below <math|N>. Its terminal vertices represent the file classes
  that will become exported sections. The reverse hierarchy <math|rev M> of a
  terminal namespace <math|M> records the larger namespaces in which
  <math|M> also appears as a terminal object. When reverse hierarchy is enabled
  in the export graph, these extra contexts are shown as grey vertices; they are
  explanatory metadata, not additional chapters to be exported.

  The augmented hierarchy duplicates terminal file-class vertices when this is
  necessary to make the selected export tree explicit. The user selects a
  substantial tree: it has root <math|N>, follows namespace-inclusion arrows,
  and has terminal file-class vertices as leaves. This selected tree is the
  mathematical object that <ATHENA> linearizes into parts, chapters, sections,
  labels, and rewritten internal links.

  <tmdoc-copyright|2026|Felix>

  <tmdoc-license|Permission is granted to copy, distribute and/or modify this
  document under the terms of the GNU Free Documentation License, Version 1.1
  or any later version published by the Free Software Foundation; with no
  Invariant Sections, with no Front-Cover Texts, and with no Back-Cover
  Texts. A copy of the license is included in the section entitled "GNU Free
  Documentation License".>
</body>
