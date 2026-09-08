# Hunspell and progressive live spelling

ATHENA links libhunspell >= 1.7 (required pkg-config dependency), rather than
launching Hunspell/Aspell and speaking the command-line pipe protocol. Hunspell
is the spelling and suggestion implementation; its installed headers offer
MPL 1.1 / GPL 2-or-later / LGPL 2.1-or-later licensing, compatible with ATHENA's
GPL distribution. The prior alternatives were the external Hunspell/Aspell
protocol, libaspell, and the macOS spelling service. These backends are removed.

## Dictionaries and ownership

- Discover matching `.aff` / `.dic` pairs in `DICPATH`,
  `$ATHENA_PATH/dictionaries`, the user's `.local/share/hunspell`, system
  Hunspell/MySpell directories, and macOS `Library/Spelling` directories.
  Prefer the exact locale before the language-only name. System dictionaries
  are read directly, not copied into the application or modified.
- Use the dictionary's declared encoding through Qt's codec implementation.
- Each calling thread owns its Hunspell instances and session-accepted words.
  No Hunspell instance, process, or QObject is passed between actors.
- Permanent words are shared as UTF-8 values under a mutex, with a revision
  counter invalidating thread-local verdict caches and editor scans. Save
  atomically to `$ATHENA_HOME_PATH/system/spelling/<locale>.txt`. Also read the
  legacy UTF-8 `~/.hunspell_<locale>` word list, without writing to it.
- Dictionary load is lazy and synchronous. It is not covered by the traversal
  time budget. Missing dictionaries do not underline every word; an explicit
  spell-check request reports the missing dictionary.

## Live traversal

The editor owns a resumable stack of shallow tree references and paths. It runs
only on that editor's BufferActor. Tree/environment changes discard the stack;
after a 450ms debounce, the next scan starts at the viewport's top-left tree
position. Descend the visible branch first, then visit successively farther
siblings. A long text leaf starts at the visible word and wraps to its prefix.
Scrolling during a pending scan reprioritizes the new viewport.

Each apply-changes tick processes at most 64 words, 256 traversal steps, and
4096 text bytes, with a cooperative 4ms deadline and 50ms between batches.
Individual Hunspell calls cannot be preempted. Tokens exceeding 512 bytes are
not passed to the word checker. Math and inaccessible document children retain
the existing DRD-based exclusions. Highlight ranges are sorted, and the word
under the cursor is withheld until the cursor moves away.

Background checks use only `spell`, never `suggest`. Suggestions are generated
when requested by a spelling UI. Explicit interactive spelling still uses its
existing selection/navigation interface; the incremental scheduler described
here replaces automatic check-as-you-type traversal.

## Focused validation

`tests/System/Language/hunspell_test.cpp` covers installed English dictionaries,
a DICPATH non-UTF-8 fixture, suggestions, thread-local acceptance, shared personal
words and persistence, viewport-first traversal, sorted completion, yielding on
long atoms, and excluding math. It uses a temporary profile, not the user's
Notes or personal dictionary.

The earlier watchdog report also identified a separate Qt repaint integer
overflow after the spelling stall recovered. Replacing the spellchecker is not
evidence that the repaint overflow is fixed.
