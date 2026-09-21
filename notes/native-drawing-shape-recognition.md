# Native drawing shape recognition

ATHENA's native drawing recognizer is an independent C++ implementation.  The
recognition worker receives detached stroke samples only; it never receives an
editor, document tree, box, or renderer pointer.  A successful result is sent
back as detached geometry and BufferActor remains the sole owner of the final
document mutation and undo transaction.

The design was informed by two public descriptions of the Xournal family of
recognizers, without copying their implementation:

- Denis Auroux's description of Xournal's recognizer explains the use of
  length-weighted first/second moments and the normalized determinant of the
  inertia matrix for distinguishing straight strokes from round strokes, with
  conservative thresholds and polygonal decomposition for corners:
  https://sourceforge.net/p/xournal/support-requests/6/
- The current Xournal++ guide documents the deliberately narrow automatic
  recognizer surface as line segments, circles, and rectangles, leaving the
  original stroke unchanged when no shape matches:
  https://xournalpp.github.io/guide/tools/pen/

ATHENA intentionally starts with the same narrow output vocabulary: line,
circle, and rotated rectangle.  It adds stricter rejection checks for endpoint
backtracking, closure, radial residual, right angles, parallel opposite sides,
and distance from the fitted rectangle perimeter.  Recognition is therefore
biased toward false negatives rather than changing handwriting unexpectedly.

Recognition is an optional Pen property.  When enabled, Qt keeps the original
stroke as an ephemeral preview while a worker classifies the detached samples.
The serial recognition worker then submits exactly one final command through
the actor transport: either detached recognized geometry or, on rejection, the
original detached Pen stroke.  It never posts recognition completion back into
the GUI event queue.  No provisional stroke is inserted into the document, so
successful recognition does not create a second automatic undo step.
