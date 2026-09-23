# Document localization data

These dictionaries are used only by document-level `localize` and explicit
`translate` markup. ATHENA's user interface is English-only and does not load
this directory while constructing menus, dialogs, tooltips, or status text.

The dictionaries are versioned UTF-8 JSON. `translations` is an ordered array
of `[source, target]` pairs so duplicate-key last-wins behavior is explicit and
stable. Older Scheme/Cork dictionary files are migration input only and are no
longer loaded at runtime.

The entries cover structural document terminology shipped by ATHENA, including
section headings, enunciations, proofs, captions, title metadata, indexes, and
bibliographies. Natural-language editing support remains under
`langs/natural`, including hyphenation and language-specific input behavior.
