# Style-Declared Slide Pages

Slide types and fields belong to the active ATS style, not a C++ enumeration.
The source `slide-page` retains its `kind` and local rich-text properties. Existing
title, section, content and blank pages require no document migration.

## ATS Declarations

ATS uses ordinary assignments and tuples:

- `slide-page-types`: ordered tuple of available type identifiers.
- `slide-default-type`: type used when creating the first page.
- `slide-page-type-ID`: tuple `(label, layout-macro, fields)`.
- Each entry in `fields`: tuple `(key, placeholder, shared-variable)`.

An empty shared-variable means a page-local rich-text property. A nonempty value
binds the field to document initial variable `slide-shared-VARIABLE`; all fields
using that binding read and edit the same value. The field key need not equal the
shared variable name. Shared values are saved in the document, never in ATS.

For example, an ATS package can add this declaration and put `contact` in
`slide-page-types`:

```xml
<node tag="assign"><text>slide-page-type-contact</text><node tag="tuple">
  <text>Contact page</text><text>render-contact</text><node tag="tuple">
    <node tag="tuple"><text>email</text><text>Email address</text><text/></node>
    <node tag="tuple"><text>affiliation</text><text>Affiliation</text><text>institution</text></node>
  </node>
</node></node>
```

`render-contact` receives `email`, `affiliation`, then the source body, in that
order. It controls typography, placement and empty-field behavior using normal
ATS macros. The existing `render-slide-field` macro controls the editing frame.
Placeholders are inserted only while editing, never as stored field content.

Keys must be nonempty, unique within a type, contain no colon, and must not use
the structural properties `kind` or `background`. Field content is rich text;
the native XML codec validates that representation without needing the style.

## Editing And Ownership

The owning BufferActor reads the active style declarations. Qt receives detached
type names and identifiers for the New slide menu. Selecting a type sends its
identifier back to the actor, which checks it against the active declarations.
Ordinary New slide inherits the current type and creates empty local fields.

Page discovery traverses transparent `document` containers and stops at each
`slide-page`. Style changes must not wrap an existing deck merely because its
pages are nested inside document containers. Navigation, page editing and
thumbnail signatures use this same source-page sequence.

Slots use the existing structured-field input editor. Page-local changes update
node metadata; shared changes update the document variable and invalidate the
environment, so other occurrences are laid out with the new value. No per-page
synchronization copies are maintained.

Changing style does not delete stored properties or convert page types. A type
missing from the new style is an explicit missing-declaration error, not a
silent fallback to another layout. Existing local data is retained even if a
style temporarily binds the same field to a shared variable instead.

## Seafoam And Slide Footers

The built-in Seafoam style demonstrates title, section, content and blank page
declarations. Its square title bands, typography and segmented footer are all
defined in `ATHENA/styles/seafoam.ats`.

Title and subtitle are local fields. Author, affiliation and the long-form date
are shared document variables. Every page type also declares `footer-author`,
`footer-title` and `footer-date`, each bound to its own shared variable. Short
footer forms are deliberately independent of the full cover forms.

Slideshow pagination renders `page-this-footer` (or the ordinary odd/even footer)
at the bottom of each slide. Overflow grows the page and moves the footer below
the content. Footer loci remain in the searchable page box tree so structured
fields can be edited directly. Paper decorations keep their existing behavior.

Use `quasiquote` with unquoted field arguments when assigning `page-this-footer`:
this captures the current slide's field values and editing loci, while delaying
page numbering until pagination. During slideshow pagination, `page-nr` supplies
the current number and `slide-page-count` supplies the total physical slide count.
