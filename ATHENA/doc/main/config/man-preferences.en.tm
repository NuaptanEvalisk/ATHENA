<TeXmacs|2.1.4>

<style|tmdoc>

<\body>
  <tmdoc-title|Preferences>

  The <menu|Edit|Preferences> dialog collects the user preferences that are
  most often needed while writing, editing, converting, and organizing
  documents in <ATHENA>. The dialog is organized into categories on the left.
  Each category contains one or more tabs, and each tab contains sections of
  related settings.

  Use the search field at the top of the dialog to find a setting by its name,
  category, tab, or section. Selecting a result opens the corresponding tab,
  scrolls the setting into view, and focuses its control. The shortcut
  <key|C-f> moves keyboard focus to the search field.

  Changes are saved as user preferences. A few interface choices only become
  fully effective after restarting <ATHENA>; the dialog warns you when this is
  the case. Some settings are shown only when the corresponding subsystem is
  available, for instance native <name|Pdf> export or <name|Ghostscript>
  support.

  <section|General>

  <subsection|Basic>

  <\description>
    <item*|Complex actions>Controls how commands that need extra information
    ask for it. They may ask through menus or through popup windows.

    <item*|Details in menus>Chooses between simplified menus and detailed
    menus. Detailed menus expose more commands and advanced entries.

    <item*|Check for updates on startup>When enabled, <ATHENA> checks the
    <name|GitHub> releases page shortly after startup and shows a toast
    notification if a newer release is available.

    <item*|Remember panes layout>When enabled, <ATHENA> remembers the
    Advanced Docking System pane layout and restores it on startup.

    <item*|Middle-click closes ADS tabs>When enabled, clicking an Advanced
    Docking System tab with the middle mouse button closes that tab, using the
    same safe close path as the tab close button.

    <item*|Automatically save>Sets the autosave interval. Choosing
    <menu|Disable> turns periodic autosave off.

    <item*|Autosave by default>Controls whether new documents are autosaved by
    default.

    <item*|Use case-insensitive search>Search operations ignore the difference
    between upper-case and lower-case letters.
  </description>

  <subsection|Appearance>

  <\description>
    <item*|Look and feel>Chooses the general keyboard and interaction style.
    The default follows the platform; alternatives include <name|Emacs>,
    <name|Gnome>, <name|KDE>, <name|Mac OS>, and <name|Windows>. This setting
    requires restart.

    <item*|User interface theme>Chooses the interface theme: default, bright,
    dark, native, or legacy. This setting requires restart.

    <item*|Use text toolbars instead of icon toolbars>Replaces toolbar icons
    with concise command labels. This applies to the main, mode-dependent,
    focus-dependent, and user toolbars; popup menus and ordinary dialog
    buttons are unchanged.

    <item*|Hide toolbars when not using them>Collapses the main,
    mode-dependent, focus-dependent, and user toolbars into a thin centered
    <verbatim|...> strip. Hovering over the strip with a pointer or stylus
    reveals the toolbars; moving away collapses them again. The document is
    not re-typeset when the toolbar area changes height.

    <item*|Blink the editing cursor>Uses the desktop's Qt cursor flash time for
    the document editing cursor. Disabling this option keeps the cursor
    continuously visible.

    <item*|Use inertial scrolling>Enables continued scrolling after a scroll
    gesture, similar to touchpad momentum.

    <item*|Inertial momentum (0.80-0.99)>Sets the friction used by inertial
    scrolling. Values closer to 0.99 keep the scroll movement alive for
    longer; values closer to 0.80 stop it sooner.

    <item*|Inertial sensitivity multiplier>Scales the strength of scroll
    input before it is turned into inertial movement.

    <item*|Use multi-tabs>Enables tabbed document windows.

    <item*|Use print dialogue>Uses the graphical print dialog instead of the
    older command-oriented print workflow.

    <item*|Disable window positioning>Prevents <ATHENA> from restoring or
    forcing saved window positions.

    <item*|Show live statistics in central footer>Shows live document
    statistics in the central part of the footer.

    <item*|Live statistics format>Controls the footer statistics text. The
    default format uses placeholders such as <verbatim|%w> for words,
    <verbatim|%c> for characters, and <verbatim|%l> for lines.

    <item*|Use toast notifications>Enables transient toast notifications for
    suitable status messages.
  </description>

  <subsection|Fonts>

  <subsubsection|Styling>

  <\description>
    <item*|New style fonts>Uses the newer font selection and rendering path.

    <item*|Advanced font customization>Enables advanced font customization
    behavior for users who need finer font control.

    <item*|Show warning for font substitution>Warns when a requested font is
    unavailable and <ATHENA> substitutes another font.
  </description>

  <subsubsection|Preferred fonts>

  The preferred font list lets you add font families that should be preferred
  by <ATHENA> when suitable. <menu|Add font> inserts a family from the font
  database. <menu|Remove selected> removes the selected entry from the list.

  <subsubsection|Maintenance>

  <\description>
    <item*|Scan for system fonts>Scans the disk for fonts and updates the font
    database.

    <item*|Clear local font cache>Clears cached font information so that it
    will be rebuilt.
  </description>

  <section|Keyboard>

  <subsection|Input>

  <\description>
    <item*|Space bar in text mode>Controls how repeated spaces are handled in
    ordinary text: use the default behavior, reject multiple spaces, glue
    multiple spaces, or allow multiple spaces.

    <item*|Space bar in math mode>Controls whether space input in mathematics
    avoids or allows spurious spaces.

    <item*|Automatic quotes>Chooses the style used for automatically inserted
    quotation marks, or disables automatic quote handling.

    <item*|Automatic brackets>Controls automatic bracket insertion. It may be
    disabled, enabled everywhere, or enabled only inside mathematics.

    <item*|Cyrillic input method>Chooses the Cyrillic input method:
    <menu|None>, <menu|Translit>, <menu|Jcuken>, or <menu|Yawerty>.

    <item*|Advanced settings>Opens the keyboard shortcuts editor.
  </description>

  <subsection|Remote Control>

  These settings map remote-control buttons to keyboard actions used for
  navigation and presentation control.

  <\description>
    <item*|Left>Action sent by the left button.

    <item*|Right>Action sent by the right button.

    <item*|Up>Action sent by the up button.

    <item*|Down>Action sent by the down button.

    <item*|Center>Action sent by the center or select button.

    <item*|Play>Action sent by the play button.

    <item*|Pause>Action sent by the pause button.

    <item*|Menu>Action sent by the menu button.
  </description>

  <section|Editing>

  <subsection|Maths>

  <subsubsection|Math keyboard>

  <\description>
    <item*|Use spurious invisible operators>Allows automatic insertion of
    invisible operators in mathematical input where they help the internal
    structure.

    <item*|Use shortcuts for missing invisible operators>Enables manual
    shortcuts for inserting missing invisible operators.

    <item*|Homoglyph substitutions>Enables manual correction of mathematical
    symbols that look similar but have different meanings.
  </description>

  <subsubsection|Quick symbol inserter>

  <\description>
    <item*|ESC quick inserter>Opens a table editor for the quick symbol
    inserter used by the <key|escape> key in mathematical input. The table
    edits the user configuration file
    <verbatim|$ATHENA_HOME_PATH/misc/input/escape-symbol-picker.json> and uses
    the bundled symbol list as the default source when no user configuration
    exists. Each row defines a typed key, inserted action, preview, notation,
    and description.
  </description>

  <subsubsection|Math hints and semantics>

  <\description>
    <item*|Semantic editing>Uses semantic information while editing
    mathematical formulas.

    <item*|Semantic selections>Uses semantic structure when selecting
    mathematical subexpressions.

    <item*|Semantic focus>Shows focus information based on semantic
    mathematical structure.
  </description>

  <subsection|Programming>

  <\description>
    <item*|Scripting language>Chooses the default scripting language used for
    script sessions and script-related commands.

    <item*|Highlight matching brackets>Highlights the bracket matching the
    one near the cursor in program text.

    <item*|Automatic program brackets>Automatically inserts matching brackets
    in program text.

    <item*|Use smart bracket selections>Uses bracket structure to help select
    program fragments.
  </description>

  <subsection|Text>

  <\description>
    <item*|Show heading word counts>Shows word counts for headings where this
    feature is supported.

    <item*|Check spelling as you type>Runs live spell checking while editing.

    <item*|Disable UNIX primary selection>Disables the X11-style primary
    selection behavior.

    <item*|Document updates run>Controls how many times document update
    passes are run when updating automatically generated content.

    <item*|Custom dictionary language>Chooses the language whose custom
    dictionary should be imported.

    <item*|Custom dictionary>Imports the selected custom dictionary.
  </description>

  <subsection|Source>

  These settings control how <menu|Document|Edit source tree> presents the
  document structure. They are applied when source-tree editing is entered.

  <\description>
    <item*|Presentation style>Chooses the notation used to display tree tags:
    angular markup, Scheme, functional notation, or LaTeX-like notation.

    <item*|Special rendering>Controls how strongly ATHENA replaces raw source
    tags with specialized visual representations.

    <item*|Compactification>Controls which tags and arguments are collapsed
    into compact source forms.

    <item*|Closing style>Chooses how the closing portion of long source tags is
    displayed.
  </description>

  <subsection|Formula Importer>

  These settings affect LaTeX formula import.

  <\description>
    <item*|Recognize matrices and determinants disguised as arrays>Converts
    suitable array-like LaTeX constructions into matrix or determinant
    structures.

    <item*|Treat 'align' as 'aligned'>Imports LaTeX <verbatim|align>
    constructs as aligned formula structures when appropriate.

    <item*|Convert 'aligned' blocks into 'eqnarray' environments>Converts
    aligned blocks into <verbatim|eqnarray>-style environments.

    <item*|Parse operator d as differential d>Recognizes operator
    <verbatim|d> as the differential symbol.

    <item*|Parse Roman d as differential d>Recognizes roman
    <verbatim|d> as the differential symbol.

    <item*|Parse text d as differential d>Recognizes text
    <verbatim|d> as the differential symbol.

    <item*|Parse blackboard k as Bbbk>Imports blackboard-bold
    <verbatim|k> as the <verbatim|Bbbk> mathematical symbol.

    <item*|Parse blackboard i as mathi>Imports blackboard-bold
    <verbatim|i> as the mathematical <verbatim|i> symbol.

    <item*|Recognize operator names disguised as text>Converts text fragments
    that are actually operator names into operator structures.

    <item*|Run intelligent formula cleaner when importing LaTeX formulas>Runs
    the GGUF-based formula cleaner after LaTeX formula import.

    <item*|Formula cleaner GGUF model>Path to the GGUF model used by the
    intelligent formula cleaner.
  </description>

  <section|Rendering>

  <subsection|Components and Layout>

  <\description>
    <item*|Labels display>Controls how vault labels are displayed:
    <menu|visible>, <menu|small>, or <menu|hidden>.

    <item*|New style page breaking>Uses the newer page breaking algorithm.

    <item*|Render exercises in smaller font>Renders solution-like exercise
    content using a smaller font.

    <item*|Number solutions>Numbers solution environments.
  </description>

  <subsection|Document Colors>

  <\description>
    <item*|Cursor color>Color of the document cursor.

    <item*|Selection color>Color used for selected content.

    <item*|Focus box color>Color of focus boxes.

    <item*|Focus box border>Width of the focus box border.

    <item*|Unclicked link color>Color used for links that have not been
    visited.

    <item*|Clicked link color>Color used for visited links.

    <item*|Enable radioactive links>Automatically recognizes artifact terms in
    document text and renders them as links to the corresponding stable
    artifacts. Disable this option to leave matching text unlinked.

    <item*|Radioactive link color>Color used for automatically recognized
    artifact terms. It is independent of the colors for ordinary links so
    that inferred links remain visually distinguishable.

    <item*|Override white background>Replaces white document backgrounds by
    the chosen override color.

    <item*|White background color>Color used when white document background
    overriding is enabled.

    <item*|Transclusion background>Optional background color for transcluded
    vault content. <menu|None> disables the override.

    <item*|Alpha transparency>Enables alpha transparency in rendering paths
    that support it.
  </description>

  <subsection|Misc>

  <\description>
    <item*|Default CJK language>Chooses the default CJK language for font and
    typography decisions: Chinese, Japanese, Korean, or Taiwanese.

    <item*|Persistent fit width>Keeps fit-width viewing persistent.

    <item*|Fast environments>Uses faster environment handling when possible.
  </description>

  <subsubsection|Table of Contents>

  <\description>
    <item*|Fold by default in Reflow>Makes generated tables of contents start
    folded when the document uses the Reflow page medium. The title remains
    visible and can be unfolded on screen; tables of contents in other page
    media start unfolded but remain foldable.
  </description>

  <subsubsection|Graphs>

  <\description>
    <item*|Use interactive elastic graphs>Lets nodes in Reverse, Direct, and
    Global hierarchy graph panes be dragged. Connected nodes react while a
    node is dragged and the graph settles into a balanced layout after it is
    released. Disabling this option restores static hierarchy graph panes.
  </description>

  <subsection|Enunciation Colors>

  <subsubsection|Presets>

  <\description>
    <item*|Preset>Chooses an enunciation color preset.

    <item*|Apply>Applies the selected preset to the individual enunciation
    color preferences.
  </description>

  <subsubsection|Enunciations>

  These settings choose optional background colors for enunciation
  environments. <menu|None> disables the color override for that environment.

  <\description>
    <item*|Theorem>Background color for theorem environments.

    <item*|Lemma>Background color for lemma environments.

    <item*|Corollary>Background color for corollary environments.

    <item*|Proposition>Background color for proposition environments.

    <item*|Axiom>Background color for axiom environments.

    <item*|Definition>Background color for definition environments.

    <item*|Notation>Background color for notation environments.

    <item*|Convention>Background color for convention environments.

    <item*|Conjecture>Background color for conjecture environments.

    <item*|Law>Background color for law environments.
  </description>

  <subsubsection|Remarks and notes>

  These settings choose optional background colors for remark-like
  environments.

  <\description>
    <item*|Remark>Background color for remark environments.

    <item*|Note>Background color for note environments.

    <item*|Example>Background color for example environments.

    <item*|Warning>Background color for warning environments.

    <item*|Disambiguation>Background color for disambiguation environments.

    <item*|Acknowledgments>Background color for acknowledgments environments.
  </description>

  <subsubsection|Exercises and proofs>

  These settings choose optional background colors for exercise and proof
  environments.

  <\description>
    <item*|Exercise>Background color for exercise environments.

    <item*|Problem>Background color for problem environments.

    <item*|Question>Background color for question environments.

    <item*|Solution>Background color for solution environments.

    <item*|Answer>Background color for answer environments.

    <item*|Proof>Background color for proof environments.

    <item*|Proof (Alternative)>Background color for alternative proof
    environments.

    <item*|Proof (Standard)>Background color for standard proof environments.
  </description>

  <section|Convert>

  <subsection|Html>

  <subsubsection|TeXmacs -\<gtr\> Html>

  <\description>
    <item*|Use CSS for more advanced formatting>Exports additional formatting
    through CSS.

    <item*|Export mathematical formulas as MathJax>Exports formulas for
    rendering by <name|MathJax>. This is mutually exclusive with the MathML
    and image formula export choices.

    <item*|Export mathematical formulas as MathML>Exports formulas as
    <name|MathML>. This is mutually exclusive with MathJax and image formula
    export.

    <item*|Export mathematical formulas as images>Exports formulas as image
    files. This is mutually exclusive with MathJax and MathML formula export.

    <item*|CSS stylesheet>Chooses the CSS stylesheet URL used by HTML export,
    or leaves it unset.
  </description>

  <subsubsection|Html -\<gtr\> TeXmacs>

  <\description>
    <item*|Try to import formulas using LaTeX annotations>When HTML or MathML
    contains LaTeX annotations for formulas, uses them to improve import.
  </description>

  <subsection|LaTeX>

  <subsubsection|LaTeX -\<gtr\> TeXmacs>

  <\description>
    <item*|Import sophisticated objects as pictures>Falls back to picture
    import for LaTeX objects that cannot be represented structurally.
  </description>

  <subsubsection|TeXmacs -\<gtr\> LaTeX>

  <\description>
    <item*|Replace TeXmacs styles with no LaTeX equivalents>Uses replacement
    styles when a TeXmacs style has no direct LaTeX equivalent.

    <item*|Expand TeXmacs macros with no LaTeX equivalents>Expands TeXmacs
    macros that LaTeX cannot represent directly.

    <item*|Expand user-defined macros>Expands user-defined macros during
    export.

    <item*|Allow for macro definitions in preamble>Allows generated macro
    definitions in the LaTeX preamble.

    <item*|Character encoding>Chooses the LaTeX output encoding: ASCII, Cork
    with catcodes, or UTF-8 with inputenc.
  </description>

  <subsubsection|Conservative conversion options>

  <\description>
    <item*|Keep track of source code>Stores source tracking information for
    LaTeX import and export.

    <item*|Only convert changes with respect to tracked version>Uses
    conservative conversion based on the tracked source version.

    <item*|Guarantee transparent source tracking>Uses transparent source
    tracking when importing LaTeX.

    <item*|Store tracking information in LaTeX files>Embeds tracking
    information in exported LaTeX files.
  </description>

  <subsection|Verbatim>

  <subsubsection|TeXmacs -\<gtr\> Verbatim>

  <\description>
    <item*|Use line wrapping for lines longer than 80 characters>Wraps long
    lines when exporting plain text.

    <item*|Character encoding>Chooses the output encoding for verbatim export:
    automatic, Cork, ISO-8859-1, ISO-8859-2, or UTF-8.
  </description>

  <subsubsection|Verbatim -\<gtr\> TeXmacs>

  <\description>
    <item*|Merge lines into paragraphs unless separated by blank lines>Merges
    consecutive input lines into paragraphs during verbatim import.

    <item*|Character encoding>Chooses the input encoding for verbatim import:
    automatic, Cork, ISO-8859-1, ISO-8859-2, or UTF-8.
  </description>

  <subsection|Pdf>

  <subsubsection|TeXmacs -\<gtr\> Pdf/Postscript>

  <\description>
    <item*|Produce Pdf using native export filter>Uses the native PDF export
    filter. This option is shown only when native PDF export is supported.

    <item*|Produce Postscript using native export filter>Uses the native
    PostScript export filter. This option is shown only when the required
    support is available.

    <item*|Expand beamer slides>Expands beamer overlays when exporting to
    PDF or PostScript.

    <item*|Generate DataArt cover image when exporting>Generates a DataArt
    cover image during export.

    <item*|Distill encapsulated Pdf files>Distills included PDF files during
    native PDF export. This option is shown only when native PDF export is
    supported.

    <item*|Check exported Pdf files for correctness>Runs correctness checks
    on exported PDF files. This option is shown only when native PDF export is
    supported.

    <item*|Pdf version number>Chooses the PDF version to generate, or leaves
    the exporter at its default. This option is shown only when native PDF
    export is supported.
  </description>

  <subsection|Image>

  <subsubsection|TeXmacs -\<gtr\> Image>

  <\description>
    <item*|Bitmap export resolution (dpi)>Chooses the raster export
    resolution in dots per inch.

    <item*|Clipboard image format>Chooses the image format used for clipboard
    export. The available formats depend on installed converters.
  </description>

  <subsubsection|Image -\<gtr\> TeXmacs>

  <\description>
    <item*|Auto remove image background>Attempts to remove the background
    when importing images.

    <item*|Use Inkscape for conversion from SVG>Prefers <name|Inkscape> for
    SVG import conversion.
  </description>

  <section|Vault>

  <subsection|General>

  <\description>
    <item*|Auto load last vault>Automatically opens the last vault on startup.

    <item*|Report if last vault is unavailable>Shows a report when the last
    vault cannot be opened.

    <item*|Show vault welcome page on startup>Shows the vault welcome page at
    startup.

    <item*|Show vault explorer on startup>Opens the vault explorer when
    <ATHENA> starts.

    <item*|Take preferences with vault>Stores and loads preferences together
    with the active vault instead of using only the global preferences file.
  </description>

  <subsection|Navigation>

  <\description>
    <item*|Track current file in vault explorer>Keeps the vault explorer
    selection synchronized with the current document.

    <item*|Use system trash for safe deletion>Moves deleted vault files to the
    system trash when possible.

    <item*|Global Search uses case-insensitive search>Sets the initial
    Case-insensitive toggle in Global Search. When enabled, letter case is
    ignored for both unrestricted searches and searches limited to an
    enunciation type.

    <item*|Global Search uses fuzzy search>Sets the initial Fuzzy toggle in
    Global Search. When enabled, approximate matches are included after exact
    matches. Queries shorter than four characters remain exact-only.

    <item*|Preferred initial neighborhood>Chooses the first selected row for a
    document in the neighborhoods viewer. The default is the first direct
    namespace-based neighborhood; the alternative is the path-based
    neighborhood made from files in the same directory.
  </description>

  <subsection|Namespaces>

  <\description>
    <item*|Namespace explorer shows file matches only for leaf namespaces>When
    searching namespace relations, restricts file matches to leaf namespaces.

    <item*|Namespace explorer starts from root namespace>When enabled and the
    active vault has a root namespace, the namespace explorer shows only that
    namespace at the top level. Expanding the root namespace still shows its
    namespace children and matching files.

    <item*|Namespace explorer simplifies redundant child namespaces>When
    enabled, the namespace explorer hides a direct child namespace under an
    ellipsis branch when the same child is also reachable through another
    direct child. This keeps expanding a namespace focused on the shortest
    visible hierarchy while preserving access to the folded namespaces.

    <item*|Simplify hierarchy graphs>Reduces visual complexity in hierarchy
    graphs.

    <item*|Consume %s aggressively in sub-product naming template suggestion>Uses
    the <verbatim|%s> part of sub-product naming templates more aggressively
    when suggesting names.
  </description>

  <subsection|Wikilinks and Transclusion>

  <\description>
    <item*|Wikilink inserter uses case-insensitive search>When enabled, the
    search page of the wikilink insertion wizard matches text without regard
    to letter case. This does not affect the file-first page of the wizard.

    <item*|Transclusion inserter uses case-insensitive search>When enabled, the
    search page of the transclusion insertion wizard matches text without
    regard to letter case. This does not affect the file-first page of the
    wizard.

    <item*|Wikilink inserter uses fuzzy search>When enabled, the search page of
    the wikilink insertion wizard also finds sufficiently similar text when
    the exact query is absent or misspelled. Exact matches are listed first.
    This does not affect the file-first page of the wizard.

    <item*|Transclusion inserter uses fuzzy search>When enabled, the search
    page of the transclusion insertion wizard also finds sufficiently similar
    text inside eligible enunciations. Exact matches are listed first. This
    does not affect the file-first page of the wizard.

    <item*|Wikilink default display text for files>Template used to fill the
    display text field when a wikilink targets a whole file. The default is
    <verbatim|%f>.

    <item*|Wikilink default display text for headings>Template used to fill the
    display text field when a wikilink targets a heading anchor such as
    <verbatim|H1 Introduction>. The default is <verbatim|%c>.

    <item*|Wikilink default display text for anchors>Template used to fill the
    display text field when a wikilink targets a non-heading anchor. The
    default is <verbatim|%c>.
  </description>

  Display text templates use a small <name|printf>-style vocabulary:
  <verbatim|%t> inserts the target type, <verbatim|%T> inserts the type with
  its first character capitalized, <verbatim|%f> inserts the file stem,
  <verbatim|%F> inserts the path relative to the vault root,
  <verbatim|%p> inserts the absolute path, <verbatim|%c> inserts the heading
  content or anchor display text, <verbatim|%C> capitalizes the first character
  of that content, and <verbatim|%s> lowercases the first character of that
  content.

  <subsection|Artifacts>

  <\description>
    <item*|Rejected artifact names>Edits the active vault's exact-match
    filter for candidate artifact names. Entries such as <verbatim|not> and
    <verbatim|cannot> prevent incidental bold text with that complete name
    from becoming an artifact, and cause an enunciation to fall back to its
    complete content instead of using that text as its title. Double-click an
    entry to edit it. The list is stored in the <verbatim|.lst> file selected
    by the Vaultfile, one entry per line.
  </description>

  <subsection|Materials>

  Material records and their managed files belong to the active vault, while
  these provider and rendering choices are preferences.

  <\description>
    <item*|Local metadata extractor>Command used to inspect embedded metadata
    in a dropped Material file. The default is <verbatim|exiftool>.

    <item*|Local PDF text extractor>Command used to extract searchable text
    from a dropped PDF for identifier recognition. The default is
    <verbatim|pdftotext>.

    <item*|Query Crossref>Allows DOI metadata lookup through Crossref.

    <item*|Query OpenAlex>Allows DOI metadata lookup through OpenAlex.

    <item*|Query Open Library>Allows ISBN metadata lookup through Open
    Library. This provider is enabled by default so that books without useful
    embedded PDF metadata can still be recognized from their ISBN.

    <item*|Query Google Books>Allows ISBN metadata lookup through Google
    Books.

    <item*|Query arXiv>Allows metadata lookup for recognized arXiv IDs.

    <item*|Query PubMed>Allows metadata lookup for recognized PubMed IDs.

    <item*|Provider contact email>Email supplied to providers that request a
    contact address for responsible API use.

    <item*|Default CSL style>Selects the independent CSL style used for newly
    inserted citations and reference lists.  The menu is populated from the
    styles bundled with the Materials engine; legacy BibTeX style names such as
    <verbatim|ams> are not CSL styles. The default is
    <verbatim|springer-mathphys>, a numeric bracketed style suited to
    mathematical writing. A document-level Citation Style overrides this
    preference.
  </description>

  Local extraction always precedes provider lookup. Enabled providers receive
  only recognized identifiers such as DOI, ISBN, arXiv ID, or PMID, never the
  dropped document file. Open Library is enabled by default; the remaining
  providers are disabled by default.

  <subsection|Maintenance>

  <\description>
    <item*|Max allowed number of full backups>Limits the number of full vault
    backups to keep, or keeps them without a limit.

    <item*|Preservation of pre-save histories for file>Controls how long
    pre-save history records are kept for each file.

    <item*|Anchor reader processes>Limits the number of reader processes used
    while scanning anchors during maintenance.

    <item*|Update all tables of contents during vault maintenance>Runs the
    same table-of-contents generation used by
    <menu|Document|Update|Table of contents> for every vault document that
    contains a ToC. Documents are processed by a bounded pool of independent
    ATHENA worker processes.

    <item*|Update Continuous RAG during vault maintenance>Runs one incremental
    Continuous RAG indexing pass after document maintenance. With RAG
    Delegation enabled, the pass sends only changed <verbatim|.ath> documents
    to the selected configured server and applies the returned SQLite patch;
    otherwise it updates <verbatim|rag.sqlite> locally.

    <item*|If delegated RAG is unavailable>Controls failure handling when the
    selected delegation server is unreachable, refuses authentication, or
    returns invalid data: fail the entire maintenance operation, continue and
    report a warning, or run the incremental embedding pass locally.

    <item*|Collect orphan assets during vault maintenance>Finds and collects
    managed assets that are no longer referenced by vault documents. Vault
    maintenance recognizes structural image, hyperlink, card-link, include,
    sound, video, and animation references. Known image/PDF assets and
    referenced files of every ordinary asset type are normalized to stable
    <verbatim|asset-UUID> names; existing
    <verbatim|figure-UUID> names remain valid. Ordinary files that ATHENA has
    never managed are not collected.

    <item*|Generate summary page for maintenance>Writes a summary page after
    maintenance.

    <item*|Maintenance summaries to keep>Limits how many generated
    maintenance summary pages are retained.
  </description>

  Maintenance Setup also includes a missing-image scan, enabled by default.
  It parses structural local <verbatim|image> references in every
  <verbatim|.ath> document and lists references whose target image file does
  not exist in the generated maintenance summary. Remote image URLs are
  ignored without being downloaded.

  <subsection|Backup>

  Backup dispatchers maintain one-way, incremental mirrors of the active
  vault. Each row combines a destination with one trigger. A destination may
  be an absolute local directory, a path beginning with <verbatim|~/>, an SSH
  destination such as
  <verbatim|user@host:/path>, or an <verbatim|rsync://> destination. ATHENA
  uses <verbatim|rsync> and coalesces repeated save requests while a transfer
  is already running.

  <\description>
    <item*|Every successful save>Dispatches after a document has been saved
    successfully. Saves that occur during an active transfer request one
    additional synchronization instead of starting concurrent transfers.

    <item*|Vault maintenance>Dispatches after all mutating maintenance passes
    and website generation have completed.

    <item*|Idle for 5 minutes>Dispatches once after ATHENA has received no
    keyboard, pointer, touch, wheel, or gesture input for five minutes. A new
    period starts after the next user action.
  </description>

  Dispatching mirrors the whole vault and removes destination files that no
  longer exist at the source. Use a dedicated destination; ATHENA rejects a
  local destination that is the vault itself, contains the vault, or lies
  inside it. The source directories <verbatim|.backup> and paths matching
  <verbatim|.athena/rag-backup-*> are excluded.

  <subsection|Anchors and Images>

  <\description>
    <item*|Auto anchor structures on manual save>Automatically anchors
    enunciation-like structures when a document is manually saved.

    <item*|Automatically approve anchor changes on manual save>Applies the
    structural-anchor dry run without opening the confirmation dialog. This
    affects only automatic anchoring during manual save; the explicit
    <menu|Document|Anchor enunciations> command still asks for confirmation.

    <item*|Auto copy images to vault>Copies inserted images into the vault.

    <item*|Normalize image filename when inserting>Normalizes image file names
    when images are inserted into the vault.
  </description>

  <subsection|Vault Info>

  If no vault is active, this tab reports <em|No active vault>. When a vault
  is active, it edits fields stored in the vault's <verbatim|Vaultfile.json>.
  Legacy <verbatim|Vaultfile> files are migrated to this JSON file when the
  vault is opened.

  <\description>
    <item*|Vault name>Name stored for the active vault.

    <item*|Map database path>Vault-relative path to the non-temporal SQLite
    map database, normally <verbatim|map.sqlite>. Legacy
    <verbatim|map.tmdb> maps are migrated automatically when the Vault is
    opened.

    <item*|Local preferences path>Vault-relative path to the preferences file
    used when vault-local preferences are enabled.

    <item*|Namespace database path>Vault-relative path to the namespace
    database.

    <item*|Startup page>Vault document opened as the startup page.

    <item*|One-time startup page>Vault document opened once as a temporary
    startup page.

    <item*|Maintenance summary folder>Vault-relative folder used for
    maintenance summaries.

    <item*|RAG index database path>Vault-relative path to the RAG index
    database.

    <item*|Website registry path>Vault-relative path to the website registry
    used by <menu|Tools|Websites manager>. Legacy <verbatim|Vaultfile>
    files are accepted only for startup migration; the JSON field defaults to
    <verbatim|websites.json>.

    <item*|Materials database path>Vault-relative path to the Materials SQLite
    database. It defaults to <verbatim|materials.sqlite> at the vault root and
    is independent of the managed file folder.

    <item*|Stored materials folder>Vault-relative folder into which dropped
    source files are copied with canonical human-readable names. It defaults
    to <verbatim|materials/>.

    <item*|Artifact title filter path>Vault-relative path to the editable
    artifact-name rejection list. It defaults to
    <verbatim|artifact-title-filter.lst>; the file contains one exact name or
    phrase per line.

    <item*|Root namespace>Optional root namespace for the vault. It is stored
    in <verbatim|Vaultfile.json>, not in the preferences file, so it remains
    attached to the vault even when preferences are not taken with the vault.
    <ATHENA> reports an error on startup if this field names a namespace that
    no longer exists. The namespace explorer and
    <menu|View|Graphs|Global hierarchy graph> use this field.

    <item*|Global preferred font for vault>Preferred font profile for vault
    documents, generated vault pages, and vault export operations. The shared
    font configurator selects a main font and optional bold, italic, small
    caps, sans-serif, typewriter, mathematics, Greek, blackboard-bold,
    calligraphic, and Fraktur subfonts. It also reports whether the main font
    contains CJK glyphs and supports independent regular, bold, and italic CJK
    fallback fonts. <menu|Clear> restores the document default.
  </description>

  <section|Other>

  <subsection|AI>

  <\description>
    <item*|Codex home>Stores Codex authentication and configuration separately
    from the normal Codex command-line home. By default ATHENA uses the
    <verbatim|codex> directory inside the ATHENA user home.

    <item*|OpenAI Codex login>Starts the official Codex login flow using the
    selected Codex home. The adjacent status command checks whether that Codex
    home is authenticated. ATHENA does not store OpenAI API keys itself.
  </description>

  <subsection|Debugging>

  <subsubsection|General>

  <\description>
    <item*|Show the Debug menu>Shows the command-oriented Debug menu. The
    menu contains consoles, editor-state inspection commands, timing and
    memory reports, garbage collection, and other immediate debugging
    operations. Diagnostic logging is configured separately below.

    <item*|Include Guile backtraces in Scheme errors>Includes a Guile stack
    backtrace when Scheme code reports an error.

    <item*|Show live memory usage in the status bar>Appends ATHENA's current
    memory usage to status-bar messages. Disabling the option immediately
    restores the normal status bar.
  </description>

  <subsubsection|Debug Console>

  <\description>
    <item*|Open the error console automatically on errors>Opens the error
    messages pane when ATHENA records an error.

    <item*|Open the error console automatically on warnings>Opens the error
    messages pane when ATHENA records a warning.

    <item*|Message details>Selects normal messages or detailed messages with
    their associated diagnostic context.

    <item*|Messages retained>Limits how many recent messages the debugging
    consoles display. <em|All> uses a large practical upper bound.
  </description>

  <subsubsection|Diagnostic Logging>

  The diagnostic logging switches enable persistent internal logging
  channels. They take effect immediately and are restored on the next ATHENA
  startup. Some channels produce substantial output or reduce performance;
  <menu|Disable all diagnostic logging> resets all channels together.

  <\description>
    <item*|Startup and automatic configuration>Reports startup discovery,
    fonts, images, and automatic subsystem configuration.

    <item*|Verbose subsystem diagnostics>Enables additional verbose output
    from subsystems which support it.

    <item*|GUI event dispatch>Reports GUI event scheduling and dispatch.

    <item*|Core and Scheme operations>Reports core startup and Scheme
    evaluation activity.

    <item*|Files, processes, and input/output>Reports file operations,
    process launching, and other I/O activity.

    <item*|TLS transport>Reports GnuTLS connection activity.

    <item*|Performance benchmarks>Collects and reports internal benchmark
    timings.

    <item*|Document history>Reports document-history and archiving activity.

    <item*|Qt integration>Reports Qt platform and event integration.

    <item*|Qt widget construction and layout>Reports detailed Qt widget
    creation and layout activity.

    <item*|Keyboard and input translation>Reports keyboard-event translation
    and editor input handling.

    <item*|Packrat parsing>Reports packrat parser activity.

    <item*|Parser flattening>Reports flattening stages used by parsers.

    <item*|Language parsers>Reports language parser activity.

    <item*|Document correction>Reports document upgrade and structural
    correction activity. Enabling it also makes the mathematics status
    reports available from the Debug menu.

    <item*|Document and image conversion>Reports import, export, image, PDF,
    and other conversion operations.

    <item*|Live relations>Reports live-view and live-relation updates.
  </description>

  <subsubsection|Rendering Performance>

  <\description>
    <item*|Show rendering FPS and editing latency HUD>Shows a
    semi-transparent monitor at the lower-left of every document editor. FPS
    is sampled from completed canvas paints over a one-second window. Editing
    latency measures the time from a keyboard action or input-method update to
    the next completed canvas paint; the HUD shows the latest value and the
    95th percentile over the last five seconds. Its own periodic refreshes are
    excluded from both measurements, so an idle editor falls to zero FPS
    instead of measuring the monitor itself.
  </description>

  <subsection|Connectivity>

  <subsubsection|Google Tasks>

  <\description>
    <item*|OAuth desktop client ID>Google OAuth client identifier for a
    Desktop app.

    <item*|OAuth desktop client secret>Google OAuth client secret. The field
    is password-hidden.

    <item*|Cloud todo task list>Chooses the Google Tasks list used for cloud
    todo synchronization. The list is populated after Google authorization.

    <item*|Connection status>Shows whether the Google Tasks integration is
    configured and connected.

    <item*|Google account>Provides <menu|Connect to Google> and
    <menu|Disconnect> buttons. Connect opens the browser authorization flow;
    Disconnect forgets stored tokens and resets the selected task list.
  </description>

  <subsubsection|Continuous RAG>

  <\description>
    <item*|MCP port>Port for the local read-only MCP server used by
    continuous RAG.

    <item*|Enable RAG Delegation>Allows bulk document and chunk embedding to
    be computed by a configured remote RAG server. The local vault still owns
    <verbatim|rag.sqlite>, query embedding, and search. Enabling this option
    also activates the RAG Delegation controls under
    <menu|Other|Security>.

    <item*|Embedding model path>Path to the GGUF embedding model.

    <item*|Embedding device>Chooses automatic device selection or CPU-only
    embedding.

    <item*|MCP bearer token>Bearer token required by the local MCP endpoint.

    <item*|Token management>Generates a random bearer token or copies the
    current token to the clipboard.
  </description>

  The local RAG server is started from the command line with
  <verbatim|ATHENA.bin -H --rag-server VAULT_ROOT>. The endpoint is
  <verbatim|http://127.0.0.1:PORT/mcp> and requires the bearer token above.

  <subsubsection|Artifact Generation>

  <\description>
    <item*|Enable Artifact Definition Span Delegation>Delegates only the LLM
    decision that selects which candidate paragraphs belong to a bold-text
    definition. ATHENA still performs incremental detection, AST extraction,
    candidate generation, and all Artifact SQLite writes locally. Remote
    requests contain only the keyword and candidate paragraphs as LaTeX; they
    contain no file path, <verbatim|.ath> source, Vaultfile, asset, or database.
    A remote error aborts the entire Artifact build before its database
    transaction begins; it never silently falls back to paragraph zero or the
    local model.
  </description>

  <subsection|Security>

  <subsubsection|General>

  <\description>
    <item*|Script execution>Controls whether document scripts are rejected,
    allowed after prompting, or accepted without prompting.

    <item*|Encryption>Enables experimental encryption support.
  </description>

  Wallet and <name|GnuPG> maintenance remain available through their
  dedicated commands while the native Preferences dialog is being completed.

  <subsubsection|ATHENA Delegation>

  These controls are active when either RAG Delegation or Artifact Definition
  Span Delegation is enabled.
  Server trust records and client private keys are stored in the user profile,
  not in a vault or vault-local preferences.

  <\description>
    <item*|Delegation servers>Lists pinned ATHENA backends and transmitters.
    The selected row is the server shared by delegated RAG embedding and
    Artifact definition-span selection. Each entry keeps the public-key
    fingerprint used to detect later identity changes.

    <item*|Add server>Contacts the entered address, verifies that it exposes
    the ATHENA Delegation protocol, displays its fingerprint for approval,
    generates a local client key pair, and submits the client public key for
    enrollment.
    Enrollment leaves the key pending until the server administrator accepts
    it.

    <item*|Delete>Removes the selected local server configuration.

    <item*|Check authentication>Asks the selected server whether the local
    client public key is accepted, pending, or rejected.

    <item*|Run delegated embedding>Sends changed <verbatim|.ath> documents and
    deleted-document records to the selected server and transactionally merges
    the returned SQLite patch. The job excludes assets, backups, preferences,
    UUID maps, model files, and existing vault databases.
  </description>

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
