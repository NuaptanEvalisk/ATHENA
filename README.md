# ATHENA

<p align="center">
  <img src="ATHENA/misc/images/icon_fullsize.png" alt="ATHENA icon" width="160">
</p>

**Advanced Typesetting and Hypertext Environment for Notes and Archives**

ATHENA is a mathematics-centered knowledge-work environment derived from GNU
TeXmacs. It keeps structured WYSIWYG mathematical editing and high-quality
typesetting while developing its own document, vault, search, linking, and
knowledge-management model.

![ATHENA screenshot](screenshot.png)

ATHENA is not a conservative TeXmacs distribution and is not intended to be a
drop-in replacement for upstream TeXmacs. The primary ATHENA document format is
`.ath`, the executable is `ATHENA.bin`, and the user configuration directory is
`~/.ATHENA`.

## Scope

ATHENA is designed for large structured mathematical note collections. Its main
workflows center on vaults, wikilinks and transclusions, Materials and citations,
namespaces, structural search, mathematical editing, and native Qt tools. The
application also uses per-buffer execution actors so mutable editor, document,
typesetting, and buffer-bound Scheme state remain owned by the corresponding
buffer.

Many inherited TeXmacs subsystems that do not fit this direction have been
removed rather than kept for compatibility. ATHENA therefore favors its own
maintained workflows over preserving the full historical TeXmacs feature set.

## Compatibility with GNU TeXmacs

Compatibility is not guaranteed in either direction.

Upstream TeXmacs is not expected to understand ATHENA-specific document nodes,
vault semantics, or other ATHENA extensions. Conversely, ATHENA does **not**
guarantee that it can open an upstream TeXmacs document correctly. Support for
many older TeXmacs features and subsystems has been removed, so upstream
documents that depend on those features may fail to load correctly, lose
unsupported structure, or behave differently in ATHENA.

Treat `.ath` as the native ATHENA format. If interchange with another system is
required, use an explicitly supported export format and verify the result.

## Building ATHENA

The complete compilation guide is available in several forms:

- [COMPILE.ath](./COMPILE.ath) — open this in a usable previous version of
  ATHENA.
- [COMPILE.pdf](./COMPILE.pdf) — pre-rendered build documentation.
- [COMPILE.tex](./COMPILE.tex) — LaTeX exported by ATHENA from `COMPILE.ath`;
  compile it with a LaTeX distribution if the PDF cannot be viewed.

[COMPILE](./COMPILE) is only a short pointer to these documents.

The currently supported build target is x86-64 GNU/Linux with Qt 6 and native
Wayland. See the compilation guide for the tested environment, dependencies,
native builds, package builds, and the status of other platforms.

## Documentation

After starting ATHENA, use the documentation under the Help menu. The manual is
still under construction. Additional project information is available at
[athena.evalisk.org](https://athena.evalisk.org/).

## Status

ATHENA 0.9 is under active development. Interfaces, document structures, and
workflows may still change as the project develops.

## Licensing

ATHENA is free software under the GNU General Public License, version 3 or
later. See [LICENSE](./LICENSE), [COPYING](./COPYING), and
[ATHENA/COPYING](./ATHENA/COPYING).

Copyright (C) 1998-2026 Joris van der Hoeven and others.

Copyright (C) 2026 Nuaptan Felix Evalisk.
