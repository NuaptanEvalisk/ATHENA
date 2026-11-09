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
