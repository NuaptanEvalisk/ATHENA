<TeXmacs|2.1.4>

<style|tmdoc>

<\body>
  <tmdoc-title|Commutative diagrams>

  ATHENA represents a commutative diagram as a native structured document
  object, centered on its containing line. Type <key|\ cd> in mathematics
  mode to insert one. Click an empty grid point to create a vertex, edit the
  vertex formula in place, and drag from one vertex to another to create an
  arrow. To create a self-pointing arrow, drag away from a vertex and return
  to the same vertex; the live loop preview follows the drag direction.
  Vertices can be moved by dragging the area around their formula.
  Hovered and selected vertices have an outline which leaves their formula
  unobscured. Arrows and vertices can be selected by clicking their visible
  geometry.

  After selecting a vertex or arrow, use the arrow keys to select the nearest
  object in that direction. Press <key|return> to edit the selected formula,
  <key|delete> or <key|backspace> to remove the selected object, and
  <key|escape> to clear the selection. Removing a vertex also removes its
  incident arrows.

  Right-click an arrow and choose <menu|Arrow style> to edit its arrowhead,
  tail, body, label, curve, shortening, level, color, loop radius, and loop
  angle. Dragging an endpoint handle onto the arrow's other endpoint converts
  an ordinary arrow into a self-pointing arrow; dragging either self-loop
  handle to another vertex converts it back. Right-click the diagram
  background to trim the canvas to its contents or enlarge it in one direction.

  <menu|Focus|Show hidden> deactivates the rendered diagram and exposes its
  complete native AST, including dimensions, stable identifiers,
  coordinates, endpoints, and arrow options. Press <key|return> while the
  inactive structure is selected to return to the rendered diagram.

  <\explain-macro|commutative-diagram|width|height|body>
    A native ATHENA commutative diagram. <src-arg|width> and
    <src-arg|height> are the canvas dimensions in centimetres. The
    <src-arg|body> stores the vertices and arrows.
  </explain-macro>

  <\explain-macro|cd-vertex|id|x|y|formula>
    A diagram vertex. <src-arg|id> is its stable identifier,
    <src-arg|x> and <src-arg|y> are its coordinates, and
    <src-arg|formula> is the editable mathematical label.
  </explain-macro>

  <\explain-macro|cd-arrow|id|source|target|formula|options>
    A directed diagram edge. <src-arg|source> and <src-arg|target> refer to
    vertex identifiers and may be equal for a self-pointing arrow,
    <src-arg|formula> is the editable edge label, and <src-arg|options> stores
    the visual arrow style.
  </explain-macro>

  <tmdoc-copyright|2026|ATHENA contributors>

  <tmdoc-license|This documentation is distributed under the GNU Free
  Documentation License.>
</body>

<initial|<\collection>
  <associate|language|english>
</collection>>
