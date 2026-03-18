# ATHENA
**(Advanced Typesetting and Hypertext Environment for Notes and Archives)**

ATHENA is an editing platform specifically crafted for mathematical knowledge management at scale. Essentially, it confronts the difficulty in finding a structured notetaking environment for mathematics.

The intent to create ATHENA originates from seeking an alternative for Obsidian. While Obsidian is great in many aspects, it is definitely not math-centered, has numerous bugs that make for an unhappy experience, and feels too web-styled. ATHENA bridges the gap between scientific publishing and modern, interconnected notetaking, providing an "Obsidian-like" experience adapted for the rigors of technical writing and knowledge base construction.

## Features

### Structured Notetaking
*   **Vaults:** Organize your entire knowledge ecosystem of notes and archives into self-contained, portable knowledge bases, somewhat like a personalized nLab or Wikipedia.
*   **Wikilinks:** Effortlessly connect ideas and blocks (e.g. definitions, theorems) across different documents with simple, intuitive linking. Block reference is supported. Wikilinks have a self-repair functionality, so even if you touch the link UUID or anchor name by accident, the link is not lost.
*   **Transclusions:** Embed parts of one document, e.g. some theorem, into another dynamically, allowing you to effortlessly restate things that matter in context.

### Customization and UI
*   Added extensive customization options not found in upstream versions, such as the ability to define background colors for enunciations, custom cursor colors, selection colors, and more.
*   Completely restructured the Preferences dialog to make navigating these advanced styling and UI options highly intuitive.

## Foundations and Divergence

ATHENA is a fork of GNU TeXmacs. We gratefully acknowledge the decades of foundational work by Prof. Joris van der Hoeven and the TeXmacs team that made this project possible.

However, it is important to note that ATHENA is **not** a distribution of GNU TeXmacs. To support its knowledge management features, ATHENA introduces incompatible AST nodes and other source-level changes. Thus, although theoretically ATHENA can load any file created by upstream TeXmacs, the converse may not be possible. While we maintain a deep respect for our heritage, ATHENA is evolving into a distinct environment optimized for the modern researcher.

Please be aware that ATHENA is still in active development and is not yet considered a stable production version.

## Building from Source

ATHENA runs on most major GNU/Linux distributions, macOS, and Windows. 

To compile the project, please see the [COMPILE](./COMPILE) instructions.

## Licensing

ATHENA is free software and is released under the [GNU General Public License (GPL) version 3 or later](./LICENSE).

---
*Copyright © 1998–2026 Joris van der Hoeven and others.*
*Copyright © 2026 Nuaptan F. Evalisk.*
