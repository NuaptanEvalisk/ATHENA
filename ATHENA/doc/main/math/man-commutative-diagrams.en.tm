<TeXmacs|2.1.4>

<style|tmdoc>

<\body>
  <tmdoc-title|Commutative diagrams>

  ATHENA represents a commutative diagram as a native structured document
  object. Type <key|\ cd> in mathematics mode to insert one. Click an empty
  grid point to create a vertex, edit the vertex formula in place, and drag
  from one vertex to another to create an arrow. Vertices can be moved by
  dragging their halo. Arrows and vertices can be selected by clicking their
  visible geometry.

  Right-click an arrow and choose <menu|Arrow style> to edit its arrowhead,
  tail, body, label, curve, shortening, level, and color. Right-click the
  diagram background to trim the canvas to its contents or enlarge it in one
  direction.

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
    vertex identifiers, <src-arg|formula> is the editable edge label, and
    <src-arg|options> stores the visual arrow style.
  </explain-macro>

  <tmdoc-copyright|2026|ATHENA contributors>

  <tmdoc-license|This documentation is distributed under the GNU Free
  Documentation License.>
</body>

<initial|<\collection>
  <associate|language|english>
</collection>>
