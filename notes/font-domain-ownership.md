# Native Font Ownership

`font_domain` owns the mutable native font graph. A BufferActor binds a domain
before constructing its implementation and destroys it after its editors and
boxes. Main-thread widgets and other native clients use their thread's default
domain. TLS selects a domain and caches slot addresses; it is not the resource
owner or a shared font registry.

## Resources And Teardown

Each domain owns its font, smart-map, charmap, translator, metric, glyph, TFM and
FreeType-face resources. Lookup publishes a resource only after construction;
translator and TFM loading also complete before publication. Recursive construction
of the same key is rejected. Constructor unwinding removes the pending entry.
Rewrapping an existing pointer never republishes it into a newer cache generation.

Native handles remain non-owning. Debug builds check the owner thread on handle
access. Font clients must therefore be destroyed before their domain. Teardown
releases fonts/maps first, metrics/glyphs next, TFM data next, and FreeType faces
last. Auxiliary state, including each generation's FreeType library, outlives
those resources. Bitmap arrays, per-character metrics and PK loader state have
destructors rather than relying on process exit.

Auxiliary spacing, correction, protrusion, smart resolution, virtual translation,
font-path and bitmap caches are domain-local too. Qt's glyph-image cache belongs
to the same domain because its keys contain non-owning glyph handles.

## UI And Render Boundaries

`widget-box` and output-only preview widgets store detached markup descriptions,
not actor-created boxes or fonts. Qt materialization typesets them in the UI
domain. Tooltip size queries may typeset a temporary box on their actor, but
that box is released there and is never handed to Qt.

The normal RenderService stream contains geometry and image resources, not
native fonts or boxes. Native glyph rasterization finishes on the producer.
The recording retains its image backing independently of the producer's font
domain; the consumer can replay it after that domain has been destroyed.
Synchronous PDF/clipboard exports keep their renderer and boxes within the
calling owner. New asynchronous exports must not capture a native font handle.

## Configuration And Persistence

Font rules and the platform catalog have process-wide publication stores.
Domains detach their native rule/cache views on the cold path. The catalog uses
its own Fontconfig configuration instead of mutating Qt's configuration.

A monotonic revision is sampled at actor-command and UI-update boundaries.
Invalidation retires local slots and starts a new cache generation. Retired
fonts remain valid for existing boxes; all generations are reclaimed at domain
teardown. Repeated imports in one long-lived domain can therefore retain old
generations until that domain exits. There is no per-glyph synchronization or
deep copy, nor a font-graph-wide mutex or atomic native reference counting.

Closest-font results merge into one canonical persistence store under its cold
writer lock. Only that store replaces the profile file, so different actors do
not overwrite it from independent local maps. Invalidation advances its
generation and rejects stale in-flight results. Database signature lookup is
outside the persistence lock to avoid reversing the database/invalidation lock
order. These are single-process guarantees, not a multi-process profile lock.

## Focused Regression Coverage

- `doc_info_upgrade_test`: overlapping complete legacy-document upgrades with
  independent keyword and classification accumulators.
- `font_domain_test`: same-name isolation, nested domain/address reuse,
  failed construction, revision invalidation with live old handles, overlapping
  real Pagella loads at two sizes, glyphs, correction maps, charmaps and virtual
  translators; a recorded glyph frame is submitted after domain destruction.
- GUI check: load `tmfs://ns/Universe`, open Page properties, inspect the rendered
  page and pane, and read the full TSan log rather than only the dispatch marker.

This boundary work does not make arbitrary mutable Scheme globals thread-safe,
and a clean focused TSan run is not a proof that all application races are gone.
