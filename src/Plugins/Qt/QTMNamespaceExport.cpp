/******************************************************************************
* MODULE     : QTMNamespaceExport.cpp
* DESCRIPTION: Namespace export book workflow
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMNamespaceExport.hpp"

#include "analyze.hpp"
#include "convert.hpp"
#include "file.hpp"
#include "namespaces.hpp"
#include "new_buffer.hpp"
#include "new_style.hpp"
#include "new_window.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"
#include "server.hpp"
#include "string.hpp"
#include "tm_configure.hpp"
#include "tm_ostream.hpp"
#include "tree.hpp"
#include "vault.hpp"

#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <functional>
#include <map>
#include <set>
#include <vector>

#ifndef ATHENA_GRAPHVIZ_FDP_EXECUTABLE
#define ATHENA_GRAPHVIZ_FDP_EXECUTABLE "fdp"
#endif

namespace {

constexpr double pi= 3.14159265358979323846;
constexpr int edge_index_role= 1;
constexpr int node_id_role= 2;
constexpr int max_chapter_levels_before_section= 7;

struct ExportNode {
  QString id;
  QString label;
  QString nsName;
  QString kind;
  QPointF pos;
  QSizeF size;
};

struct ExportEdge {
  QString from;
  QString to;
  bool selected= false;
};

struct ExportGraph {
  QString root;
  std::vector<ExportNode> nodes;
  std::vector<ExportEdge> edges;
};

struct FileMatch {
  url file;
  QString stem;
};

struct ExportContext {
  QString root;
  QMap<QString,athena_namespace_definition> namespaces;
  QMap<QString,QStringList> children;
  QMap<QString,std::vector<FileMatch> > terminalFiles;
  ExportGraph graph;
};

struct NamespaceRelations {
  QMap<QString,QStringList> directChildren;
  QMap<QString,QSet<QString> > descendants;
  QMap<QString,QSet<QString> > ancestors;
};

struct ExportOptions {
  QString author;
  bool includeDate= true;
  bool includeDataArt= true;
  bool includeDiagram= true;
};

class NamespaceExportWait {
public:
  NamespaceExportWait (const QString& root) {
    QApplication::setOverrideCursor (Qt::WaitCursor);
    system_wait ("Building namespace export", from_qstring (root));
    QApplication::processEvents ();
  }

  ~NamespaceExportWait () {
    QApplication::restoreOverrideCursor ();
    QApplication::processEvents ();
  }
};

static void
show_export_render_wait () {
  system_wait ("Rendering namespace export", "please wait");
  QApplication::processEvents ();
}

static QString
dot_escape (const QString& s) {
  QString out;
  for (QChar ch: s) {
    if (ch == '\\') out += "\\\\";
    else if (ch == '"') out += "\\\"";
    else if (ch == '\n' || ch == '\r') out += " ";
    else out += ch;
  }
  return out;
}

static QString
file_key (url u) {
  return to_qstring (as_system_string (concretize (u)));
}

static QString
file_stem (url u) {
  QString path= file_key (u);
  QString stem= QFileInfo (path).completeBaseName ();
  return stem.isEmpty () ? QFileInfo (path).fileName () : stem;
}

static void
warn_export (const QString& message) {
  std_warning << "namespace-export warning: " << from_qstring (message) << "\n";
}

static bool
has_string_qt (const strings& xs, const QString& value) {
  for (int i=0; i<N(xs); i++)
    if (to_qstring (xs[i]) == value) return true;
  return false;
}

static bool
add_unique_edge (std::vector<ExportEdge>& edges, const QString& from,
                 const QString& to) {
  if (from.isEmpty () || to.isEmpty () || from == to) return false;
  for (const ExportEdge& e: edges)
    if (e.from == from && e.to == to) return false;
  ExportEdge e;
  e.from= from;
  e.to= to;
  edges.push_back (e);
  return true;
}

static void
add_unique_relation (QMap<QString,QStringList>& children, const QString& parent,
                     const QString& child) {
  if (parent.isEmpty () || child.isEmpty () || parent == child) return;
  QStringList& xs= children[parent];
  if (!xs.contains (child)) xs << child;
}

static void
remove_relation (QMap<QString,QStringList>& children, const QString& parent,
                 const QString& child) {
  if (!children.contains (parent)) return;
  children[parent].removeAll (child);
}

static QString
relation_key (const QString& parent, const QString& child) {
  return parent + "\n" + child;
}

static QSet<QString>
collect_relation_descendants (const QString& name,
                              const QMap<QString,QStringList>& children) {
  QSet<QString> seen;
  QList<QString> pending= children.value (name);
  while (!pending.isEmpty ()) {
    QString current= pending.takeFirst ();
    if (seen.contains (current)) continue;
    seen.insert (current);
    pending << children.value (current);
  }
  return seen;
}

static NamespaceRelations
build_namespace_relations (
  const QMap<QString,athena_namespace_definition>& all) {
  NamespaceRelations rel;
  QSet<QString> known;
  for (auto it= all.constBegin (); it != all.constEnd (); ++it)
    known.insert (it.key ());

  QSet<QString> denied;
  for (const athena_namespace_relation& r: athena_namespace_relations_list ()) {
    QString parent= to_qstring (r.parent);
    QString child= to_qstring (r.child);
    if (!known.contains (parent) || !known.contains (child)) continue;
    if (r.decision == "deny") denied.insert (relation_key (parent, child));
  }

  for (auto it= all.constBegin (); it != all.constEnd (); ++it) {
    const QString childName= it.key ();
    const athena_namespace_definition& child= it.value ();
    for (int i=0; i<N(child.parents); i++) {
      QString parent= to_qstring (child.parents[i]);
      if (known.contains (parent) &&
          !denied.contains (relation_key (parent, childName)))
        add_unique_relation (rel.directChildren, parent, childName);
    }
    for (int i=0; i<N(child.derived_parents); i++) {
      QString parent= to_qstring (child.derived_parents[i]);
      if (known.contains (parent) &&
          !denied.contains (relation_key (parent, childName)))
        add_unique_relation (rel.directChildren, parent, childName);
    }
  }

  for (const athena_namespace_relation& r: athena_namespace_relations_list ()) {
    QString parent= to_qstring (r.parent);
    QString child= to_qstring (r.child);
    if (!known.contains (parent) || !known.contains (child)) continue;
    if (r.decision == "deny")
      remove_relation (rel.directChildren, parent, child);
    else if (r.decision == "allow")
      add_unique_relation (rel.directChildren, parent, child);
  }

  for (auto it= rel.directChildren.begin (); it != rel.directChildren.end (); ++it)
    it.value ().sort (Qt::CaseInsensitive);

  for (const QString& name: known)
    rel.descendants.insert (name,
                            collect_relation_descendants (name,
                                                          rel.directChildren));
  for (auto it= rel.descendants.constBegin (); it != rel.descendants.constEnd ();
       ++it)
    for (const QString& child: it.value ())
      rel.ancestors[child].insert (it.key ());

  return rel;
}

static QString
terminal_id (const QString& nsName) {
  return "terminal:" + nsName;
}

static QString
terminal_label (const QString& nsName) {
  return nsName + " files";
}

static bool
node_is_terminal (const QString& id) {
  return id.startsWith ("terminal:");
}

static QString
terminal_namespace (const QString& id) {
  return node_is_terminal (id) ? id.mid (QString ("terminal:").size ()) : id;
}

static const ExportNode*
find_node (const ExportGraph& graph, const QString& id) {
  for (const ExportNode& n: graph.nodes)
    if (n.id == id) return &n;
  return nullptr;
}

static ExportNode*
find_node (ExportGraph& graph, const QString& id) {
  for (ExportNode& n: graph.nodes)
    if (n.id == id) return &n;
  return nullptr;
}

static QRectF
node_rect (const ExportNode& n) {
  return QRectF (n.pos.x () - n.size.width () / 2.0,
                 n.pos.y () - n.size.height () / 2.0,
                 n.size.width (), n.size.height ());
}

static QPointF
rect_boundary_point (const QRectF& rect, QPointF toward) {
  QPointF center= rect.center ();
  double dx= toward.x () - center.x ();
  double dy= toward.y () - center.y ();
  if (std::abs (dx) < 0.001 && std::abs (dy) < 0.001) return center;
  double sx= std::abs (dx) < 0.001 ? 1.0e9 :
             (rect.width () / 2.0) / std::abs (dx);
  double sy= std::abs (dy) < 0.001 ? 1.0e9 :
             (rect.height () / 2.0) / std::abs (dy);
  double s= std::min (sx, sy);
  return center + QPointF (dx * s, dy * s);
}

static QStringList
plain_tokens (const QString& line) {
  QStringList out;
  QString current;
  bool quote= false;
  bool escape= false;
  for (QChar ch: line) {
    if (escape) {
      current += ch;
      escape= false;
    }
    else if (ch == '\\' && quote) escape= true;
    else if (ch == '"') quote= !quote;
    else if (ch.isSpace () && !quote) {
      if (!current.isEmpty ()) {
        out << current;
        current.clear ();
      }
    }
    else current += ch;
  }
  if (!current.isEmpty ()) out << current;
  return out;
}

static QString
dot_for_graph (const ExportGraph& graph) {
  QString dot= "digraph namespace_export {\n"
               "  graph [layout=fdp, overlap=prism, splines=true, "
               "sep=\"+2\", nodesep=0.16, ranksep=0.18, K=0.16, "
               "maxiter=3000, margin=0];\n"
               "  node [shape=box, style=\"rounded,filled\", "
               "fontname=\"Alegreya Sans\", fontsize=12, margin=\"0.08,0.04\"];\n"
               "  edge [color=\"#666666\", arrowsize=0.8];\n";
  for (const ExportNode& n: graph.nodes) {
    QString fill= n.kind == "terminal" ? "#e9f3ff" :
                  n.kind == "abstract" ? "#f2ecff" :
                  n.kind == "semi-concrete" ? "#fff5df" : "#eaf7ea";
    dot += QString ("  \"%1\" [label=\"%2\", width=%3, height=%4, "
                    "fillcolor=\"%5\"];\n")
             .arg (dot_escape (n.id), dot_escape (n.label))
             .arg (n.size.width (), 0, 'f', 2)
             .arg (n.size.height (), 0, 'f', 2)
             .arg (fill);
  }
  for (const ExportEdge& e: graph.edges)
    dot += QString ("  \"%1\" -> \"%2\";\n")
             .arg (dot_escape (e.from), dot_escape (e.to));
  dot += "}\n";
  return dot;
}

static bool
layout_with_graphviz (ExportGraph& graph, QString& error) {
  QProcess proc;
  proc.start (QString::fromUtf8 (ATHENA_GRAPHVIZ_FDP_EXECUTABLE),
              QStringList () << "-Tplain");
  if (!proc.waitForStarted (5000)) {
    error= "Could not start Graphviz fdp.";
    return false;
  }
  proc.write (dot_for_graph (graph).toUtf8 ());
  proc.closeWriteChannel ();
  if (!proc.waitForFinished (15000)) {
    proc.kill ();
    error= "Graphviz fdp did not finish.";
    return false;
  }
  if (proc.exitStatus () != QProcess::NormalExit || proc.exitCode () != 0) {
    error= "Graphviz fdp failed: " + QString::fromUtf8 (proc.readAllStandardError ());
    return false;
  }

  QMap<QString,ExportNode*> byId;
  for (ExportNode& n: graph.nodes) byId.insert (n.id, &n);

  QString text= QString::fromUtf8 (proc.readAllStandardOutput ());
  for (const QString& raw: text.split ('\n')) {
    QStringList toks= plain_tokens (raw.trimmed ());
    if (toks.size () < 6 || toks[0] != "node") continue;
    ExportNode* n= byId.value (toks[1], nullptr);
    if (n == nullptr) continue;
    bool ok1=false, ok2=false, ok3=false, ok4=false;
    double x= toks[2].toDouble (&ok1);
    double y= toks[3].toDouble (&ok2);
    double w= toks[4].toDouble (&ok3);
    double h= toks[5].toDouble (&ok4);
    if (ok1 && ok2) n->pos= QPointF (x * 110.0, -y * 110.0);
    if (ok3 && ok4) n->size= QSizeF (qMax (w * 110.0, 120.0),
                                     qMax (h * 110.0, 46.0));
  }
  return true;
}

static void
adjust_node_sizes_for_text (ExportGraph& graph) {
  for (ExportNode& n: graph.nodes) {
    QGraphicsTextItem text (n.label);
    QFont font= text.font ();
    font.setPointSize (10);
    font.setBold (n.kind == "terminal");
    text.setFont (font);
    text.setTextWidth (qMax (n.size.width () - 12.0, 120.0));
    QRectF br= text.boundingRect ();
    n.size.setWidth (qMax (n.size.width (), br.width () + 20.0));
    n.size.setHeight (qMax (n.size.height (), br.height () + 18.0));
  }
}

static void
add_arrow (QGraphicsScene* scene, QPointF from, QPointF to, const QPen& pen,
           int edgeIndex, bool selected) {
  QLineF line (from, to);
  if (line.length () < 1.0) return;

  QColor hitColor (31, 111, 235, 1);
  QPen hitPen (hitColor, selected ? 18.0 : 14.0);
  hitPen.setCapStyle (Qt::RoundCap);
  QGraphicsLineItem* hitItem= scene->addLine (line, hitPen);
  hitItem->setZValue (4);
  hitItem->setData (edge_index_role, edgeIndex);

  QGraphicsLineItem* lineItem= scene->addLine (line, pen);
  lineItem->setZValue (selected ? 5 : 3);
  lineItem->setData (edge_index_role, edgeIndex);

  double angle= std::atan2 (line.dy (), line.dx ());
  const double arrowSize= selected ? 12.0 : 10.0;
  QPointF p1= to - QPointF (std::cos (angle - pi / 6.0) * arrowSize,
                            std::sin (angle - pi / 6.0) * arrowSize);
  QPointF p2= to - QPointF (std::cos (angle + pi / 6.0) * arrowSize,
                            std::sin (angle + pi / 6.0) * arrowSize);
  QPolygonF arrow;
  arrow << to << p1 << p2;
  QGraphicsPolygonItem* arrowItem= scene->addPolygon (arrow, pen, pen.brush ());
  arrowItem->setZValue (selected ? 5 : 3);
  arrowItem->setData (edge_index_role, edgeIndex);
}

static double
distance_to_segment (QPointF p, QPointF a, QPointF b) {
  double dx= b.x () - a.x ();
  double dy= b.y () - a.y ();
  double len2= dx * dx + dy * dy;
  if (len2 < 0.001) {
    double x= p.x () - a.x ();
    double y= p.y () - a.y ();
    return std::sqrt (x * x + y * y);
  }
  double t= ((p.x () - a.x ()) * dx + (p.y () - a.y ()) * dy) / len2;
  t= std::max (0.0, std::min (1.0, t));
  QPointF q (a.x () + t * dx, a.y () + t * dy);
  double x= p.x () - q.x ();
  double y= p.y () - q.y ();
  return std::sqrt (x * x + y * y);
}

static QGraphicsScene*
create_scene (ExportGraph& graph, bool selectable) {
  adjust_node_sizes_for_text (graph);
  QGraphicsScene* scene= new QGraphicsScene ();
  scene->setBackgroundBrush (QColor ("#fbfbfb"));

  for (int i=0; i<(int) graph.edges.size (); i++) {
    const ExportEdge& e= graph.edges[i];
    const ExportNode* from= find_node (graph, e.from);
    const ExportNode* to= find_node (graph, e.to);
    if (from == nullptr || to == nullptr) continue;
    QColor color= e.selected ? QColor ("#1f6feb") : QColor ("#666666");
    QPen pen (color, e.selected ? 3.0 : 1.5);
    pen.setBrush (color);
    QRectF fr= node_rect (*from);
    QRectF tr= node_rect (*to);
    add_arrow (scene, rect_boundary_point (fr, tr.center ()),
               rect_boundary_point (tr, fr.center ()), pen, i, e.selected);
  }

  for (const ExportNode& n: graph.nodes) {
    QRectF rect= node_rect (n);
    QColor fill= n.kind == "terminal" ? QColor ("#e9f3ff") :
                 n.kind == "abstract" ? QColor ("#f2ecff") :
                 n.kind == "semi-concrete" ? QColor ("#fff5df") :
                 QColor ("#eaf7ea");
    QGraphicsRectItem* box= scene->addRect (
      rect, QPen (QColor ("#333333"), 1.4), QBrush (fill));
    box->setZValue (6);
    box->setData (node_id_role, n.id);
    box->setToolTip (selectable ? "Click edges to choose the export tree" : "");

    QGraphicsTextItem* text= scene->addText (n.label);
    QFont font= text->font ();
    font.setPointSize (10);
    font.setBold (n.kind == "terminal");
    text->setFont (font);
    text->setDefaultTextColor (QColor ("#111111"));
    text->setTextWidth (rect.width () - 12.0);
    QRectF br= text->boundingRect ();
    text->setPos (rect.center ().x () - br.width () / 2.0,
                  rect.center ().y () - br.height () / 2.0);
    text->setZValue (7);
    text->setData (node_id_role, n.id);
  }

  QRectF r= scene->itemsBoundingRect ().adjusted (-24, -24, 24, 24);
  scene->setSceneRect (r);
  return scene;
}

class ExportGraphView: public QGraphicsView {
public:
  ExportGraphView (ExportGraph* graph, QWidget* parent = nullptr)
    : QGraphicsView (parent), graph (graph), dragging (false), zoomPercent (100) {
    setRenderHints (QPainter::Antialiasing | QPainter::TextAntialiasing);
    setTransformationAnchor (QGraphicsView::AnchorUnderMouse);
    setResizeAnchor (QGraphicsView::AnchorViewCenter);
    setCursor (Qt::OpenHandCursor);
    rebuild ();
  }

  ~ExportGraphView () override {
    delete scene ();
  }

  void rebuild () {
    QGraphicsScene* old= scene ();
    setScene (create_scene (*graph, true));
    if (old != nullptr) old->deleteLater ();
  }

  void resetViewport () {
    resetTransform ();
    zoomPercent= 100;
    if (scene () != nullptr) fitInView (scene ()->sceneRect (), Qt::KeepAspectRatio);
  }

protected:
  void mousePressEvent (QMouseEvent* event) override {
    if (event->button () == Qt::LeftButton) {
      QGraphicsItem* item= itemAt (event->pos ());
      QVariant edgeData= item == nullptr ? QVariant () :
                         item->data (edge_index_role);
      int edgeIndex= edgeData.isValid () ? edgeData.toInt () :
                    nearestEdgeAt (event->pos ());
      if (edgeIndex >= 0 && edgeIndex < (int) graph->edges.size ()) {
        graph->edges[edgeIndex].selected= !graph->edges[edgeIndex].selected;
        rebuild ();
        return;
      }
      dragging= true;
      lastPos= event->pos ();
      setCursor (Qt::ClosedHandCursor);
      event->accept ();
      return;
    }
    QGraphicsView::mousePressEvent (event);
  }

  void mouseMoveEvent (QMouseEvent* event) override {
    if (dragging) {
      QPoint delta= event->pos () - lastPos;
      lastPos= event->pos ();
      horizontalScrollBar ()->setValue (horizontalScrollBar ()->value () - delta.x ());
      verticalScrollBar ()->setValue (verticalScrollBar ()->value () - delta.y ());
      event->accept ();
      return;
    }
    QGraphicsView::mouseMoveEvent (event);
  }

  void mouseReleaseEvent (QMouseEvent* event) override {
    if (dragging && event->button () == Qt::LeftButton) {
      dragging= false;
      setCursor (Qt::OpenHandCursor);
      event->accept ();
      return;
    }
    QGraphicsView::mouseReleaseEvent (event);
  }

  void wheelEvent (QWheelEvent* event) override {
    double factor= event->angleDelta ().y () > 0 ? 1.12 : 1.0 / 1.12;
    zoomPercent= qBound (25, (int) std::round (zoomPercent * factor), 300);
    scale (factor, factor);
  }

private:
  int nearestEdgeAt (const QPoint& viewPos) const {
    QPointF p= mapToScene (viewPos);
    double scaleFactor= transform ().m11 ();
    if (scaleFactor <= 0.0) scaleFactor= 1.0;
    double threshold= 16.0 / scaleFactor;
    double best= threshold;
    int bestIndex= -1;
    for (int i=0; i<(int) graph->edges.size (); i++) {
      const ExportEdge& e= graph->edges[i];
      const ExportNode* from= find_node (*graph, e.from);
      const ExportNode* to= find_node (*graph, e.to);
      if (from == nullptr || to == nullptr) continue;
      QRectF fr= node_rect (*from);
      QRectF tr= node_rect (*to);
      QPointF a= rect_boundary_point (fr, tr.center ());
      QPointF b= rect_boundary_point (tr, fr.center ());
      double d= distance_to_segment (p, a, b);
      if (d < best) {
        best= d;
        bestIndex= i;
      }
    }
    return bestIndex;
  }

  ExportGraph* graph;
  bool dragging;
  QPoint lastPos;
  int zoomPercent;
};

static bool
choose_root_namespace (QString& root) {
  QDialog dialog (QApplication::activeWindow ());
  dialog.setWindowTitle ("Export namespace");
  QVBoxLayout* layout= new QVBoxLayout (&dialog);
  layout->addWidget (new QLabel ("Choose the namespace to export:", &dialog));
  QComboBox* box= new QComboBox (&dialog);
  for (const athena_namespace_definition& ns: athena_namespaces_list ())
    box->addItem (to_qstring (ns.name));
  box->model ()->sort (0);
  layout->addWidget (box);
  QDialogButtonBox* buttons=
    new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                          &dialog);
  QObject::connect (buttons, &QDialogButtonBox::accepted,
                    &dialog, &QDialog::accept);
  QObject::connect (buttons, &QDialogButtonBox::rejected,
                    &dialog, &QDialog::reject);
  layout->addWidget (buttons);
  dialog.resize (520, 130);
  if (dialog.exec () != QDialog::Accepted) return false;
  root= box->currentText ();
  return !root.isEmpty ();
}

static bool
build_export_context (const QString& root, ExportContext& cx, QString& error) {
  if (!vault_active ()) {
    error= "No active vault.";
    return false;
  }

  QMap<QString,athena_namespace_definition> all;
  for (const athena_namespace_definition& ns: athena_namespaces_list ())
    all.insert (to_qstring (ns.name), ns);
  if (!all.contains (root)) {
    error= "Unknown namespace: " + root;
    return false;
  }

  NamespaceRelations rel= build_namespace_relations (all);

  QSet<QString> included;
  included.insert (root);
  included.unite (rel.descendants.value (root));
  cx.root= root;
  for (const QString& name: included)
    cx.namespaces.insert (name, all[name]);

  for (const QString& parent: included) {
    QStringList kids;
    for (const QString& child: rel.directChildren.value (parent))
      if (included.contains (child) && !kids.contains (child)) kids << child;
    kids.sort (Qt::CaseInsensitive);
    if (!kids.isEmpty ()) cx.children.insert (parent, kids);
  }

  QMap<QString,QStringList> fileNamespaces;
  QMap<QString,std::vector<FileMatch> > directFiles;
  for (auto it= cx.namespaces.begin (); it != cx.namespaces.end (); ++it) {
    const QString name= it.key ();
    if (it.value ().kind == "abstract") continue;
    string nsError;
    std::vector<athena_namespace_match> members=
      athena_namespace_members (from_qstring (name), nsError);
    if (nsError != "")
      warn_export ("Sorter/member warning for " + name + ": " + to_qstring (nsError));
    for (const athena_namespace_match& m: members) {
      FileMatch fm;
      fm.file= m.file;
      fm.stem= to_qstring (m.stem);
      directFiles[name].push_back (fm);
      fileNamespaces[file_key (m.file)] << name;
    }
  }
  for (auto it= fileNamespaces.begin (); it != fileNamespaces.end (); ++it)
    if (it.value ().size () > 1)
      warn_export ("File matches multiple namespaces and will appear multiple times: " +
                   it.key () + " -> " + it.value ().join (", "));

  ExportGraph graph;
  graph.root= root;
  for (auto it= cx.namespaces.begin (); it != cx.namespaces.end (); ++it) {
    ExportNode node;
    node.id= it.key ();
    node.label= it.key ();
    node.nsName= it.key ();
    node.kind= to_qstring (it.value ().kind);
    node.size= QSizeF (1.6, 0.55);
    graph.nodes.push_back (node);
  }

  QStringList names= QStringList (included.values ());
  names.sort (Qt::CaseInsensitive);
  for (const QString& parent: names)
    for (const QString& child: names)
      if (parent != child && rel.descendants.value (parent).contains (child))
        add_unique_edge (graph.edges, parent, child);

  for (const QString& name: names) {
    bool hasSubspace= false;
    for (const QString& child: rel.directChildren.value (name)) {
      if (included.contains (child)) {
        hasSubspace= true;
        break;
      }
    }
    bool createTerminal= !hasSubspace || !directFiles[name].empty ();
    if (!createTerminal) continue;

    QString tid= terminal_id (name);
    ExportNode terminal;
    terminal.id= tid;
    terminal.label= terminal_label (name);
    terminal.nsName= name;
    terminal.kind= "terminal";
    terminal.size= QSizeF (1.5, 0.5);
    graph.nodes.push_back (terminal);
    add_unique_edge (graph.edges, name, tid);

    for (const QString& ancestor: names) {
      if (ancestor == name) continue;
      if (rel.descendants.value (ancestor).contains (name))
        add_unique_edge (graph.edges, ancestor, tid);
    }

    cx.terminalFiles.insert (tid, directFiles[name]);
    if (directFiles[name].empty ())
      warn_export ("Terminal namespace has no files: " + name);
  }

  if (!layout_with_graphviz (graph, error)) return false;
  cx.graph= graph;
  return true;
}

static bool
validate_selected_tree (const ExportContext& cx, QString& error,
                        QMap<QString,QStringList>& selectedChildren) {
  selectedChildren.clear ();
  QMap<QString,int> indegree;
  QSet<QString> selectedNodes;
  selectedNodes.insert (cx.root);
  int selectedEdges= 0;
  for (const ExportEdge& e: cx.graph.edges) {
    if (!e.selected) continue;
    selectedEdges++;
    selectedChildren[e.from] << e.to;
    indegree[e.to]++;
    selectedNodes.insert (e.from);
    selectedNodes.insert (e.to);
  }
  if (selectedEdges == 0) {
    error= "Select at least one edge.";
    return false;
  }
  if (indegree.value (cx.root, 0) != 0) {
    error= "The root namespace must not have a selected parent.";
    return false;
  }
  for (const QString& node: selectedNodes) {
    if (node == cx.root) continue;
    if (indegree.value (node, 0) != 1) {
      error= "Every selected node except the root must have exactly one selected parent.";
      return false;
    }
  }

  QSet<QString> seen;
  QSet<QString> visiting;
  std::function<bool(QString,int)> dfs= [&] (QString node, int nsDepth) {
    if (visiting.contains (node)) {
      error= "Selected edges contain a cycle.";
      return false;
    }
    if (seen.contains (node)) return true;
    visiting.insert (node);
    seen.insert (node);

    QStringList kids= selectedChildren.value (node);
    if (kids.isEmpty () && !node_is_terminal (node)) {
      error= "Every selected leaf must be a terminal file-class duplicate.";
      return false;
    }
    if (!node_is_terminal (node) && node != cx.root && nsDepth > 1 + max_chapter_levels_before_section) {
      error= QString ("Selected namespace depth exceeds the supported maximum before Section (%1).")
               .arg (max_chapter_levels_before_section);
      return false;
    }
    for (const QString& kid: kids)
      if (!dfs (kid, node_is_terminal (kid) ? nsDepth : nsDepth + 1))
        return false;
    visiting.remove (node);
    return true;
  };
  if (!dfs (cx.root, 0)) return false;
  if (seen.size () != selectedNodes.size ()) {
    error= "Selected edges are not connected to the chosen root namespace.";
    return false;
  }
  return true;
}

static bool
select_export_tree (ExportContext& cx, QMap<QString,QStringList>& selectedChildren) {
  QDialog dialog (QApplication::activeWindow ());
  dialog.setWindowTitle ("Select namespace export tree");
  QVBoxLayout* layout= new QVBoxLayout (&dialog);
  QLabel* hint= new QLabel (
    "Click directed edges to select a rooted tree. Leaves must be blue file-class nodes.",
    &dialog);
  hint->setWordWrap (true);
  layout->addWidget (hint);

  ExportGraphView* view= new ExportGraphView (&cx.graph, &dialog);
  layout->addWidget (view, 1);

  QDialogButtonBox* buttons=
    new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                          &dialog);
  QPushButton* reset= buttons->addButton ("Reset viewport",
                                          QDialogButtonBox::ActionRole);
  QObject::connect (reset, &QPushButton::clicked,
                    view, &ExportGraphView::resetViewport);
  QObject::connect (buttons, &QDialogButtonBox::rejected,
                    &dialog, &QDialog::reject);
  QObject::connect (buttons, &QDialogButtonBox::accepted, &dialog, [&] () {
    QString error;
    QMap<QString,QStringList> tmp;
    if (!validate_selected_tree (cx, error, tmp)) {
      QMessageBox::warning (&dialog, "Invalid export tree", error);
      return;
    }
    selectedChildren= tmp;
    dialog.accept ();
  });
  layout->addWidget (buttons);
  dialog.resize (1080, 760);
  QTimer::singleShot (0, view, [view] () { view->resetViewport (); });
  return dialog.exec () == QDialog::Accepted;
}

static bool
choose_export_options (ExportOptions& options) {
  QDialog dialog (QApplication::activeWindow ());
  dialog.setWindowTitle ("Namespace export options");
  QVBoxLayout* layout= new QVBoxLayout (&dialog);
  QFormLayout* form= new QFormLayout ();
  QLineEdit* author= new QLineEdit (&dialog);
  QCheckBox* date= new QCheckBox ("Include current date on cover", &dialog);
  QCheckBox* dataArt= new QCheckBox ("Generate DataArt cover image", &dialog);
  QCheckBox* diagram= new QCheckBox ("Include hierarchy diagram on second page", &dialog);
  date->setChecked (options.includeDate);
  dataArt->setChecked (options.includeDataArt);
  diagram->setChecked (options.includeDiagram);
  form->addRow ("Author", author);
  layout->addLayout (form);
  layout->addWidget (date);
  layout->addWidget (dataArt);
  layout->addWidget (diagram);
  QDialogButtonBox* buttons=
    new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                          &dialog);
  QObject::connect (buttons, &QDialogButtonBox::accepted,
                    &dialog, &QDialog::accept);
  QObject::connect (buttons, &QDialogButtonBox::rejected,
                    &dialog, &QDialog::reject);
  layout->addWidget (buttons);
  dialog.resize (520, 220);
  if (dialog.exec () != QDialog::Accepted) return false;
  options.author= author->text ().trimmed ();
  options.includeDate= date->isChecked ();
  options.includeDataArt= dataArt->isChecked ();
  options.includeDiagram= diagram->isChecked ();
  return true;
}

static tree
tm_text (const QString& s) {
  return tree (from_qstring (s));
}

static bool
is_absolute_image_path (const string& path) {
  return path == "" || starts (path, "/") || starts (path, "~") ||
    starts (path, "$") || occurs ("://", path);
}

static string
rebase_image_path (const string& path, url sourceDir) {
  if (is_absolute_image_path (path)) return path;
  url absolute= sourceDir * url_unix (cork_to_utf8 (path));
  return utf8_to_cork (as_system_string (absolute));
}

static tree
rebase_images (tree t, url sourceDir) {
  if (is_atomic (t)) return copy (t);
  tree r (L(t));
  for (int i=0; i<N(t); i++) {
    if (i == 0 && is_func (t, IMAGE) && is_atomic (t[i]))
      r << tree (rebase_image_path (t[i]->label, sourceDir));
    else
      r << rebase_images (t[i], sourceDir);
  }
  return r;
}

static tree
import_body (url file) {
  tree t= import_tree (file, "texmacs");
  tree body= extract (t, "body");
  return is_empty (body) ? t : body;
}

static QString
plain_tree_text (tree t) {
  if (is_atomic (t)) return to_qstring (t->label);
  QStringList parts;
  for (int i=0; i<N(t); i++) {
    QString p= plain_tree_text (t[i]).trimmed ();
    if (!p.isEmpty ()) parts << p;
  }
  return parts.join (" ").simplified ();
}

static bool
extract_title_rec (tree t, QString& title) {
  if (is_atomic (t)) return false;
  if ((is_compound (t, "doc-title", 1) ||
       is_compound (t, "title", 1) ||
       is_compound (t, "tmdoc-title", 1) ||
       is_compound (t, "tmweb-title", 1)) && N(t) >= 1) {
    title= plain_tree_text (t[0]);
    return !title.isEmpty ();
  }
  for (int i=0; i<N(t); i++)
    if (extract_title_rec (t[i], title)) return true;
  return false;
}

static QString
document_title_for_file (url file, tree body) {
  tree full= import_tree (file, "texmacs");
  QString title;
  if (extract_title_rec (full, title)) return title;
  if (extract_title_rec (body, title)) return title;
  warn_export ("No document title found; using filename stem: " + file_key (file));
  return file_stem (file);
}

static QString
demoted_tag (const QString& tag) {
  bool star= tag.endsWith ("*");
  QString base= star ? tag.left (tag.size () - 1) : tag;
  QString out;
  if (base == "part" || base == "chapter" || base == "section")
    out= "subsection";
  else if (base == "subsection") out= "subsubsection";
  else if (base == "subsubsection") out= "paragraph";
  else if (base == "paragraph") out= "subparagraph";
  else if (base == "subparagraph") out= "subparagraph";
  else return tag;
  return star ? out + "*" : out;
}

static bool
drop_source_top_level (tree t) {
  return is_compound (t, "doc-data") ||
         is_compound (t, "table-of-contents") ||
         is_compound (t, "bibliography") ||
         is_compound (t, "the-index") ||
         is_compound (t, "the-glossary");
}

static tree
copy_demote_body (tree t) {
  if (is_atomic (t)) return copy (t);
  QString tag= to_qstring (as_string (L(t)));
  QString demoted= demoted_tag (tag);
  tree r= compound (from_qstring (demoted));
  for (int i=0; i<N(t); i++)
    r << copy_demote_body (t[i]);
  return r;
}

static tree
source_body_for_export (url file) {
  tree body= rebase_images (import_body (file), head (file));
  if (!is_func (body, DOCUMENT)) return copy_demote_body (body);
  tree out (DOCUMENT);
  for (int i=0; i<N(body); i++) {
    if (drop_source_top_level (body[i])) continue;
    out << copy_demote_body (body[i]);
  }
  return out;
}

static QString
heading_tag_for_namespace_depth (int depth) {
  if (depth <= 0) return "";
  if (depth == 1) return "part";
  int chapterLevel= depth - 1;
  if (chapterLevel == 1) return "chapter";
  QString tag= "chapter";
  for (int i=1; i<chapterLevel; i++) tag= "sub" + tag;
  return tag;
}

static void
append_heading (tree& body, const QString& tag, const QString& title) {
  if (!tag.isEmpty ()) body << compound (from_qstring (tag), tm_text (title));
}

static QMap<QString,QStringList>
selected_children_sorted (const QMap<QString,QStringList>& selectedChildren) {
  QMap<QString,QStringList> out= selectedChildren;
  for (auto it= out.begin (); it != out.end (); ++it)
    it.value ().sort (Qt::CaseInsensitive);
  return out;
}

static void
append_export_subtree (tree& body, const ExportContext& cx,
                       const QMap<QString,QStringList>& selectedChildren,
                       const QString& node, int depth) {
  if (node_is_terminal (node)) {
    const std::vector<FileMatch>& files= cx.terminalFiles.value (node);
    for (const FileMatch& fm: files) {
      tree source= source_body_for_export (fm.file);
      QString title= document_title_for_file (fm.file, source);
      body << compound ("section", tm_text (title));
      if (is_func (source, DOCUMENT)) {
        for (int i=0; i<N(source); i++) body << copy (source[i]);
      }
      else body << copy (source);
    }
    return;
  }

  if (node != cx.root)
    append_heading (body, heading_tag_for_namespace_depth (depth), node);
  for (const QString& child: selectedChildren.value (node))
    append_export_subtree (body, cx, selectedChildren, child, depth + 1);
}

static QString
render_hierarchy_diagram (ExportGraph graph, const QMap<QString,QStringList>& selectedChildren) {
  QSet<QString> selected;
  selected.insert (graph.root);
  for (auto it= selectedChildren.begin (); it != selectedChildren.end (); ++it) {
    selected.insert (it.key ());
    for (const QString& kid: it.value ()) selected.insert (kid);
  }
  std::vector<ExportNode> nodes;
  for (const ExportNode& n: graph.nodes)
    if (selected.contains (n.id)) nodes.push_back (n);
  graph.nodes.swap (nodes);
  std::vector<ExportEdge> edges;
  for (const ExportEdge& e: graph.edges)
    if (e.selected && selected.contains (e.from) && selected.contains (e.to))
      edges.push_back (e);
  graph.edges.swap (edges);
  QGraphicsScene* scene= create_scene (graph, false);
  QRectF rect= scene->sceneRect ();
  QSize imageSize (qMax (800, (int) std::ceil (rect.width () + 40)),
                   qMax (480, (int) std::ceil (rect.height () + 40)));
  QImage image (imageSize, QImage::Format_ARGB32_Premultiplied);
  image.fill (QColor ("#fbfbfb"));
  QPainter painter (&image);
  scene->render (&painter, QRectF (0, 0, image.width (), image.height ()), rect);
  painter.end ();
  delete scene;
  url tmp= url_temp (".png");
  QString path= to_qstring (as_system_string (tmp));
  image.save (path, "PNG");
  return path;
}

static tree
build_cover (const QString& title, const ExportOptions& options) {
  tree data= compound ("doc-data");
  data << compound ("doc-title", tm_text (title));
  if (!options.author.isEmpty ())
    data << compound ("doc-author", tm_text (options.author));
  if (options.includeDate)
    data << compound ("doc-date", tm_text (QDate::currentDate ().toString (Qt::ISODate)));
  return data;
}

static tree
build_export_document (const ExportContext& cx,
                       const QMap<QString,QStringList>& selectedChildren,
                       const ExportOptions& options) {
  tree body (DOCUMENT);
  body << build_cover (cx.root, options);
  body << compound ("table-of-contents", "toc", tree (DOCUMENT));
  body << compound ("new-page");
  if (options.includeDiagram) {
    QString imagePath= render_hierarchy_diagram (cx.graph, selectedChildren);
    body << compound ("section*", tree ("Selected namespace hierarchy"));
    body << compound ("image", tm_text (imagePath), "0.86par", "", "", "");
    body << compound ("new-page");
  }
  QMap<QString,QStringList> sorted= selected_children_sorted (selectedChildren);
  append_export_subtree (body, cx, sorted, cx.root, 0);

  tree doc (DOCUMENT);
  doc << compound ("TeXmacs", TEXMACS_COMPAT_VERSION);
  doc << compound ("style", tuple ("namespace-export-book"));
  doc << compound ("body", copy (body));
  string font= get_preference ("vault preferred font", "");
  if (font != "") {
    tree init (COLLECTION);
    init << compound ("associate", "font", font);
    init << compound ("associate", "font-family", "rm");
    doc << compound ("initial", init);
  }
  return doc;
}

static void
schedule_export_finalization (url buffer, bool includeDataArt) {
  string b= as_string (buffer);
  string cmd= "(delayed (:idle 1) (begin ";
  if (includeDataArt) {
    cmd << "(import-from (athena athena tm-data-art)) "
        << "(let* ((buf (string->url " << scm_quote (b) << ")) "
        << "(cover (data-art-generate-cover buf))) "
        << "(when cover "
        << "(data-art-insert-cover-in-doc-data-buffer buf cover))) ";
  }
  cmd << "(switch-to-buffer (string->url " << scm_quote (b) << ")) "
      << "(update-document \"all\") "
      << "(delayed (:idle 1000) (system-wait \"\" \"\"))))";
  exec_delayed (scheme_cmd (cmd));
}

static bool
create_export_buffer (const ExportContext& cx,
                      const QMap<QString,QStringList>& selectedChildren,
                      const ExportOptions& options) {
  // The export stylesheet is edited in-tree; invalidate so the auxiliary
  // document sees the current stylesheet within the same ATHENA session.
  style_invalidate_cache ();
  NamespaceExportWait wait (cx.root);
  tree doc= build_export_document (cx, selectedChildren, options);
  string name= "tmfs://aux/namespace-export-" *
               as_string ((long long) std::time (nullptr));
  url buffer (name);
  new_buffer_in_this_window (buffer, doc);
  show_export_render_wait ();
  schedule_export_finalization (buffer, options.includeDataArt);
  return true;
}

} // namespace

void
namespace_export_show () {
  if (!vault_active ()) {
    QMessageBox::warning (QApplication::activeWindow (), "Export namespace",
                          "No active vault. Please load a vault first.");
    return;
  }

  QString root;
  if (!choose_root_namespace (root)) return;

  ExportContext cx;
  QString error;
  if (!build_export_context (root, cx, error)) {
    QMessageBox::warning (QApplication::activeWindow (), "Export namespace",
                          error);
    return;
  }

  QMap<QString,QStringList> selectedChildren;
  if (!select_export_tree (cx, selectedChildren)) return;

  ExportOptions options;
  if (!choose_export_options (options)) return;

  create_export_buffer (cx, selectedChildren, options);
}
