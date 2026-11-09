# Page Modes And Native Slides

Implemented layout model (2026-11-09):

- The status selector is Reflow, Continuous, Book, Panorama, Slideshow,
  Customize. Continuous is the ordinary vertically stacked Paper arrangement;
  Book and Panorama change Paper's packet/border arrangement, not pagination.
- Reflow is responsive, unbounded document layout. The former user-facing
  Scroll mode is removed. Internal unbounded cell and preview layouts remain.
- Slideshow source contains `slide-page(document(...))` children of the body,
  optionally following a document preamble. Each page has a source UUID. Initial
  entry into Slideshow wraps ordinary document content in one explicit page;
  subsequent layout changes preserve the page structure.
- In Slideshow mode, a slide is an editing root: its outer borders are not cursor positions,
  its body has no focus outline, and ordinary structural commands cannot unwrap
  the page or insert siblings outside it. Page management uses slide commands.
  Reflow and Paper arrangements expose it as an ordinary structural node,
  with a focus outline and editable outer boundaries.
- Each slide uses the configured page width and minimum height. Overflow grows
  its height; it never creates a subsequent slide. Thumbnail insertion, deletion,
  duplication, clipboard and reorder operate on these source pages on BufferActor.
  Deleting the last page clears its body, retaining one editable page.
- Bridge and lazy typesetting both emit explicit page boundaries. The pager
  lays out each interval without height-based breaking. Screen thumbnails and
  the dedicated playback window consume the resulting real page boxes.
- PDF export retains each slide's actual dimensions, including grown heights.
  HTML export uses a section for each slide. Copy/paste and file storage use
  the ordinary XML v3 codec and source-identity duplication rules.

Retired without compatibility: legacy Dynamic folds/summaries, switches,
unroll/overlay variants, graphics visibility conditions, traversal/filtering,
`screens`, the old slideshow wrappers and PDF dynamic expansion. The unrelated
native heading/ToC folding and balloon interactions remain. The retained
interaction style definitions are in `std-interaction.ats`.

Transition and animation are not part of this change.

## Typed Pages And Style Layouts

New pages carry a `kind` property declared by ATS. The default style provides
`title`, `section`, `content`, and `blank`; these are not native enumerations.
Title pages expose rich-text `title`, `subtitle`, `author`, and `date` properties;
section pages expose title and subtitle, content pages expose title, and blank
pages expose only their existing body. These default fields are page-owned.
Styles can declare additional types and fields, including bindings to shared
document variables. See [Style-Declared Slide Pages](style-declared-slide-pages.md).
Untyped existing pages retain their authored body.

The thumbnail pane's New slide split button inherits the selected page's kind.
Its arrow inserts a chosen kind. Duplicate copies content; New slide does not.
There is no layout-conversion operation in this version. Deleting the last page
clears its fields as well as its body, retaining its UUID, kind and background.

`slide-layouts.ats`, loaded by `std`, defines these style-overridable macros:

- `render-slide-title(title, subtitle, author, date, body)`
- `render-slide-section(title, subtitle, body)`
- `render-slide-content(title, body)`
- `render-slide-blank(body)`
- `render-slide-field(field, body)` for the editing decoration

Native code projects properties and preserves the body through ARG; it does not
choose typography, alignment, minimum content height or decorations. A generated
page wrapper retains the existing slide-boundary/pager implementation. The default
style uses a growing, minimum-height centered area for title and section pages.

Page fields reuse the inline structured-field editor. Their input context is
captured from the rendered field, so style title typography is retained while
typing. Empty field hints are editing-only; printed output uses field values.
Playback builds one clean page snapshot in a separate evaluation environment,
then reuses its boxes for frame rendering. It does not reuse active editor field
decorations or mutate the editing environment and auxiliary records.

## Unified Styles

Document appearance is selected through Style, categorized as General,
Articles, Books, Slides and Custom styles. The categories and hidden internal
styles are declared in `misc/styles/catalog.json`; user styles are identified
by their resolved source rather than by an unknown catalog name. Menus, the
focus toolbar and the presentation pane use the same actor-owned choices.

The separate document Theme layer and its shipped ATS packages have been
removed, including the new/copy/apply/select-theme evaluator nodes. No theme
conversion or compatibility aliases are provided. Beamer loads the former
Bluish colors directly from the ordinary `slide-colors.ats` package. Other
document styles retain their own typography and appearance. New visual schemes
should be authored as complete styles, reusing ordinary ATS packages as needed.
