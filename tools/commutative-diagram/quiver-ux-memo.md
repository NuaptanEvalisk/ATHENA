# Quiver Interaction UX Findings for ATHENA Commutative Diagrams

## Purpose

This memo records the interaction design used by Quiver and the implications for
ATHENA's native `commutative-diagram` AST editor. It focuses on five operations:

- selecting a vertex;
- creating a vertex;
- creating an arrow;
- selecting and editing an arrow;
- moving a vertex.

The investigation used Quiver 1.6.0 at commit
`d1484f68fb3e766357a6ce7b187862a7a69ab00f`, especially:

- [the interaction state machine in `src/ui.mjs`](https://github.com/varkor/quiver/blob/d1484f68fb3e766357a6ce7b187862a7a69ab00f/src/ui.mjs);
- [arrow geometry and interaction paths in `src/arrow.mjs`](https://github.com/varkor/quiver/blob/d1484f68fb3e766357a6ce7b187862a7a69ab00f/src/arrow.mjs);
- [hover, selection, and pointer-event rules in `src/main.css`](https://github.com/varkor/quiver/blob/d1484f68fb3e766357a6ce7b187862a7a69ab00f/src/main.css);
- [the user-facing interaction description in `tutorial.md`](https://github.com/varkor/quiver/blob/d1484f68fb3e766357a6ce7b187862a7a69ab00f/tutorial.md).

The central finding is that Quiver does not obtain good interaction merely by
making visible lines thicker or by using one global nearest-object threshold.
It separates visible geometry, interaction geometry, object state, and gesture
state. Vertices and arrows are both first-class selectable objects.

## 1. Unified Object Selection

Quiver represents both vertices and edges as cells. Each cell has its own
interaction element and may be hovered, selected, targeted, edited, or removed.
The UI maintains a set of selected cells rather than a vertex-only selection.

The important selection rules are:

- An ordinary click selects the object and deselects unrelated objects.
- Shift, Control, or Command click toggles an object in the multi-selection.
- Clicking an already-selected object focuses or toggles its label input.
- Clicking the canvas or pressing Escape clears the selection.
- Delete operates on the selected objects, including selected arrows.

The distinction between the first and second click is intentional. The first
click selects the object without immediately letting a text input consume
keyboard commands such as Delete. A further click enters label editing.

An arrow is therefore not a passive line drawn behind the vertices. It has the
same semantic interaction lifecycle as a vertex.

## 2. Selecting a Vertex

A Quiver vertex has nested interaction regions:

1. The inner content region contains its formula or label.
2. The outer vertex region occupies the surrounding grid cell.

Clicking the inner content region selects the vertex. Clicking an already
selected vertex can focus the editable formula. This content region is also the
source region for beginning an arrow connection gesture.

The surrounding part of the grid cell is deliberately not equivalent to the
content region. It is reserved for moving the vertex. This spatial separation
prevents selection, text editing, arrow creation, and vertex movement from all
competing for the same exact pointer gesture.

## 3. Creating a Vertex

### Quiver's behavior

On an empty grid cell, Quiver first reveals a focus point. A second click in the
same cell creates a vertex. In user-facing terms this is a double-click creation
gesture. The new vertex is selected and its label input is normally focused.

Quiver also supports compound creation gestures:

- Dragging from an existing vertex to an empty cell creates a target vertex and
  connects the source to it.
- Dragging from one empty cell to another can create both endpoint vertices and
  the connecting arrow.

Internally, an empty-cell pointer press begins as a pending focus-point action.
Only continued movement changes it into a connection operation.

### Required ATHENA difference

ATHENA's agreed behavior is simpler and intentionally different:

- A single click on an empty grid cell creates a vertex immediately.

ATHENA should not copy Quiver's double-click requirement. What should be copied
is the separation between object selection, connection, movement, and empty-grid
creation states.

## 4. Creating an Arrow

Quiver does not interpret every small pointer movement on a vertex as an arrow
drag. Its state transition is staged:

1. Pointer down on a cell's inner content selects or toggles that cell and marks
   it as pending.
2. Releasing inside the same content region remains an ordinary click.
3. Leaving the content region while the pointer remains down changes the mode to
   `Connect` and marks the source visually.
4. A phantom arrow follows the pointer during the connection.
5. Entering a valid target marks that object as the current target.
6. Releasing on the target creates an edge, selects the new edge, and normally
   focuses its label input.

The decisive threshold is spatial: the pointer leaves the source content area.
It is not simply a test that the pointer moved by any nonzero amount. This avoids
accidental arrows when the user intended to select a vertex.

Quiver permits cells, including higher cells, as endpoints. ATHENA's initial
native diagram model may remain vertex-to-vertex without copying that additional
generality.

## 5. Selecting an Arrow

This is the most important finding for ATHENA's current usability problem.

Quiver does not require the pointer to hit the thin visible stroke. Each arrow
owns two separate SVG layers:

- a visible arrow SVG;
- a background SVG used for interaction.

The relevant Quiver constants are:

```text
STROKE_WIDTH       = 1.5 px
BACKGROUND_PADDING = 16 px per side
BACKGROUND_OPACITY = 0.2
HANDLE_RADIUS      = 14 px
```

The background layer contains a path following the same complete geometry as
the visible arrow. Its stroke width is approximately:

```text
visible edge width + 2 * BACKGROUND_PADDING
```

For an ordinary arrow, this produces an effective hit corridor about 33.5 px
wide around a 1.5 px visible line. CSS enables `pointer-events: stroke` on this
background path while the visible arrow itself need not receive pointer events.

This design has several consequences:

- The user may click near an arrow instead of precisely on a hairline.
- Hovering displays a faint halo, previewing which arrow would be selected.
- A selected arrow retains a stronger background highlight.
- Rendering and hit testing use the same curve geometry, so the clickable shape
  follows the arrow accurately.
- Overlapping arrows remain individual interaction objects rather than entries
  inferred afterward from an undifferentiated canvas.

Merely increasing a small line-distance threshold is not equivalent. Quiver's
arrow owns its interaction path, hover state, selected state, and endpoint
handles.

### Endpoint handles

When an arrow is hovered or selected, Quiver exposes endpoint handles. Each has
a diameter of about 28 px because `HANDLE_RADIUS` is 14 px. Dragging a handle can
reconnect the source or target independently. The handles are separate hit
targets and take precedence over the broad arrow corridor.

## 6. Moving a Vertex

Quiver uses the nested vertex regions to distinguish movement from connection:

- Dragging from the central formula/content region creates an arrow.
- Dragging from the empty region around the formula, within the vertex's grid
  cell, moves the vertex.

The outer region uses a move cursor, giving a visible affordance before the drag
starts. No modifier key is needed.

If the dragged vertex belongs to the current selection, all selected vertices
move together. If it is not selected, only that vertex moves. Movement snaps to
the grid. A destination already occupied by another vertex is rejected. All
connected and dependent arrows are redrawn continuously as vertices move.

This spatial division is the main reason Quiver can support both easy arrow
creation and easy vertex movement without ambiguous modifier-heavy controls.

## 7. Current ATHENA Implementation

ATHENA's current native AST is structurally appropriate:

```text
commutative-diagram
  cd-vertex
  cd-arrow
```

It no longer needs to use an upstream TeXmacs `graphics` object as the stored
document representation. The current implementation still uses generated
graphics only as a rendering surface, wrapped in a relay for pointer events.

The interaction implementation, however, remains vertex-only:

- `cd-nearest-vertex` searches only vertices, with a fixed diagram-coordinate
  threshold of 0.42.
- `commutative-diagram-handle` computes only a nearest `vertex`.
- Clicking near a vertex records it as a possible connection source.
- Clicking elsewhere immediately creates a vertex.
- Drag/select completion can create an arrow only when another vertex is found.
- Double-clicking a vertex moves the editor cursor into its formula.

The following capabilities do not yet exist:

- nearest-arrow or point-to-segment hit testing;
- selected-arrow state;
- arrow hover state;
- a broad arrow hit corridor;
- visual arrow hover or selection feedback;
- arrow endpoint handles;
- vertex movement;
- separate pending, connecting, moving, and selecting interaction modes.

Consequently, the current problem is stronger than arrows merely being hard to
select. Pointer-based arrow selection is not meaningfully implemented. Clicking
near an arrow but outside the vertex threshold is interpreted as clicking empty
grid space and creates another vertex.

Relevant ATHENA source:

- [`cd-nearest-vertex`](../../ATHENA/progs/athena/athena/commutative-diagram.scm#L58)
- [`commutative-diagram-handle`](../../ATHENA/progs/athena/athena/commutative-diagram.scm#L160)

## 8. Recommended ATHENA Interaction Model

ATHENA should retain its required single-click empty-grid creation while adopting
Quiver's first-class object state and spatially separated gestures.

### Proposed behavior

- Single-click empty grid: create a vertex and enter its editable formula.
- Click vertex formula/content: select the vertex; clicking an already-selected
  vertex enters formula editing.
- Drag from vertex formula/content beyond the connection threshold: create an
  arrow, with a phantom arrow and target feedback.
- Drag from the surrounding vertex-cell region: move the vertex on the grid.
- Hover near an arrow: show a subtle hit-corridor halo.
- Click within the arrow hit corridor: select the arrow.
- Click an already-selected arrow: enter its editable formula or expose its
  properties.
- Hover or select an arrow: expose endpoint handles.
- Drag an endpoint handle: reconnect that endpoint.
- Click empty canvas where creation is not triggered by another interaction, or
  press Escape: clear selection as appropriate.

### Interaction state

The editor needs explicit session state rather than only two global drag values.
At minimum it should track:

- selected object ids and their types;
- hovered object id and type;
- interaction mode: idle, pending, connecting, moving, or reconnecting;
- press position and current pointer position;
- source vertex or arrow endpoint;
- current valid target;
- whether movement has crossed the mode transition threshold.

This state is editing-session UI state and should not become part of the saved
`commutative-diagram` AST.

### Hit-test priority

When regions overlap, ATHENA should resolve pointer targets in this order:

1. visible arrow endpoint handle;
2. vertex formula/content region;
3. arrow's broad interaction corridor;
4. vertex's surrounding movement region;
5. empty grid, which creates a new vertex.

When several arrow corridors overlap, choose the arrow with the smallest distance
to its actual curve. A currently selected or visually topmost arrow may be used
as a deterministic tie-breaker.

### Arrow hit geometry

For the current straight-arrow implementation, the broad corridor can use the
point-to-line-segment distance. The threshold should correspond to a physical
screen-space target, approximately 16 px on either side, and must be converted
to diagram coordinates using the current zoom and device scaling. It should not
be a fixed centimeter constant.

For future curved arrows, parallel arrows, and loops, both rendering and hit
testing should consume the same line or Bezier geometry. Implementing a separate
approximate hit curve would recreate the display-versus-interaction mismatch
that Quiver avoids.

### Visual feedback

The user should not need to guess whether a thin arrow is selectable:

- hover shows a faint wide halo;
- selection shows a persistent stronger halo;
- connection sources and valid targets have distinct feedback;
- endpoint handles appear only when relevant;
- the pointer cursor changes for moving, connecting, and editing regions.

## 9. Implementation Principle

The next implementation should not patch the current vertex-only handler by
only adding a larger numeric tolerance. The source-of-truth correction is to
introduce first-class diagram object interaction and explicit gesture modes.

Quiver's reusable lesson is:

```text
semantic object
  + visible geometry
  + broad interaction geometry
  + hover/selection state
  + explicit gesture transitions
```

That model makes arrows easy to select while keeping vertices editable and
movable, and it provides a sound basis for later curved arrows, labels,
reconnection, multi-selection, and keyboard deletion.
