# Font ownership and selection

ATHENA has one font catalog. Platform discovery (Fontconfig on Linux) runs only
when the existing font database is initialized or explicitly refreshed.
\`font_database.cpp\` owns the resulting shared immutable physical-face metadata
and Unicode coverage indexes. Buffer actors may share that immutable view, but
FreeType faces, HarfBuzz fonts, sizes, glyph caches and raster state remain
\`font_domain\`-owned.

Native text keeps the already selected \`physical_font_source\` as its primary
identity. Coverage is checked directly against that face. Missing Unicode
content queries the indexed ATHENA database; it must not create another
platform catalog or turn a known file/face back into a family-name lookup.

Legacy Cork/token compatibility stays at import or legacy logical-font
boundaries. The native selector consumes Unicode scalars and physical font
identities only.
