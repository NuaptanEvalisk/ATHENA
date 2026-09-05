# Source Language Classification Audit

Date: 2026-09-05. Baseline: `3ec04e153`.
Read-only source audit after committing legacy identity/GPG/wallet removal.
No functionality was removed and no GUI or build tests were run for this audit.

Subsequent implementation: KF6 now supplies program-text highlighting.
The Emacs Lisp indentation bridge, legacy lexical parsers and Scheme language
tables, all six TeX support files and DraTeX have been removed. The inventory
below records the baseline, not the current runtime. Octave itself remains.

## Reproduction and Counts

Using installed tokei 14.0.0, respecting normal ignore rules:

```sh
tokei -t 'Emacs Lisp,TeX,Prolog,Objective-C,Objective-C++' -o json .
```

| Reported language | Files | Reported code lines | Actual classification |
| --- | ---: | ---: | --- |
| Emacs Lisp | 12 | 1503 | Real Elisp, including one ATHENA keyword-table source and Guile utilities/runtime/tests |
| TeX | 6 | 7733 | Real TeX/LaTeX resources; two DraTex files counted twice |
| Prolog | 4 | 159 | All four are PostScript prologues, not Prolog |
| Objective-C | 63 | 3115 | 62 Octave files (31 duplicated), plus one real Objective-C file |
| Objective-C++ | 3 | 552 | Real macOS native integration |

These are tokei's code-line counts, not corrected counts: its Objective-C
parser does not recognize Octave `#` comments. Default counting also excludes
the tracked hidden `3rdparty/athena-guile/.dir-locals.el`; including that yields
13 tracked Elisp files. No claim is made about ignored build trees.

## Emacs Lisp

- `ATHENA/progs/tm-mode.el`: Emacs support for editing TeXmacs Scheme, including
  keyword and indentation lists. It is also an ATHENA runtime data dependency:
  `ATHENA/progs/utils/misc/tm-keywords.scm:16` reads the file, parses its `setq`
  forms and evaluates transformed definitions, then constructs indentation
  arities. `progs/prog/scheme-edit.scm:17` and native Scheme, R and Mathemagix
  language implementations load this module. This is not an embedded Emacs
  instance, but deleting the file would break those consumers. Separate the
  canonical keyword data from the Emacs adapter before considering removal.
- `3rdparty/athena-guile/emacs/*.el` (8): upstream editing/debugging/development
  helpers: `gud-guile`, `guile-c`, `guile-scheme`, `guile`, `multistring`,
  `patch`, `ppexpand`, and `update-changelog`.
- `3rdparty/athena-guile/doc/hacks.el`: documentation tooling.
- `3rdparty/athena-guile/module/language/elisp/boot.el`: Guile's Elisp language
  implementation bootstrap, loaded by `language/elisp/spec.scm:42` using
  `compile-and-load` with `#:from 'elisp'`. Not merely an editor config file.
- `3rdparty/athena-guile/test-suite/standalone/test-language.el`: upstream
  language test input, listed in the standalone test Makefile.
- `3rdparty/athena-guile/.dir-locals.el`: hidden developer editor configuration.

Removing Guile's language support would be a separate dependency change, not
cleanup justified by a language counter.

## TeX / LaTeX

- `ATHENA/misc/latex/TeXmacs.sty`: historical macro package for exported LaTeX.
  Tracked-source searches found no current executable consumer by filename;
  `ATHENA/doc/about/changes/change-log.en.tm:1358` explicitly records automatic
  preamble generation replacing this package. A removal candidate, not evidence
  that current LaTeX import/export is obsolete.
- `ATHENA/misc/latex/f2pspost.tex`: small article wrapper loading graphics/math
  packages and including `pre`. No tracked executable reference found.
  `ATHENA/doc/devel/format/environment/tm-page.ps` names `f2pspost.dvi` in its
  generated PostScript header. Apparently historical figure-generation input;
  this provenance is an inference, not a reproduced build.
- `plugins/dratex/latex/{DraTex,AlDraTex}.sty` and the same paths under
  `ATHENA/plugins/`: real drawing macros. `plugins/tmpy/graph/dratex.py:25`
  explicitly inputs both into generated LaTeX; `init-dratex.scm` registers the
  external session and requires Python and LaTeX. These remain plugin resources.

The two DraTex directories are byte-identical (`diff -qr`). Six counted files
therefore represent four distinct files. No plugin was launched in this audit.

## Misidentified Prolog

All four files are under `ATHENA/misc/convert/`:
`tex.pro`, `special.pro`, `color.pro`, `texps.pro`.

Their contents are PostScript `%%BeginProcSet`, dictionary and procedure
definitions. tokei associates `.pro` with Prolog. The constructor in
`src/Graphics/Renderer/printer.cpp:64` loads all four with fatal-on-error reads
and uses them for PostScript output. They are live renderer resources, not an
unused logic-programming subsystem. Do not remove them on this classification.

## Objective-C and Octave

- `plugins/octave/octave/**/*.m` and `ATHENA/plugins/octave/octave/**/*.m`:
  31 files per tree, byte-identical directories. These use Octave syntax such
  as `endif`, `#` comments and `function`, not Objective-C. They implement the
  external session's REPL, protocol output, completion, plotting and conversion
  of Octave values to Scheme/tree representations. `tmstart.m` selects the
  plugin directory, adds its paths and starts `tmrepl`. Removing them means
  retiring the Octave plugin, not removing an Objective-C dependency.
- `src/Subsystems/MacOS/HIDRemote.m`: genuine Objective-C Apple infrared remote
  support, consumed by `mac_utilities.mm`.
- `src/Subsystems/MacOS/mac_images.mm`: native image reading/conversion and
  PostScript-to-PDF support.
- `src/Subsystems/MacOS/mac_spellservice.mm`: `NSSpellChecker` integration.
- `src/Subsystems/MacOS/mac_utilities.mm`: native startup/event handling,
  remote-control integration, platform workarounds and background activity.

`CMakeLists.txt:744` lists the four real native files and the `APPLE` branch
at line 973 adds them to the build. They are not Linux runtime code, but are
explicit macOS implementation sources. No macOS compilation was attempted.

## Removal Boundaries

Strongest isolated candidates: the historical `TeXmacs.sty` and `f2pspost.tex`.
Plugin retirement decisions: DraTex and Octave, including both tracked trees
and their launchers/registrations if removal is requested.
Preserve PostScript prologues and macOS integration unless their actual
functional surfaces are explicitly retired. Preserve the keyword data behind
`tm-mode.el` even if its Emacs editing adapter is eventually removed.

Static searches cannot rule out out-of-tree scripts referring to old resources.
No compatibility decision or deletion is implied by this inventory.
