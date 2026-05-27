/******************************************************************************
* MODULE     : QTMReverseHierarchyGraph.cpp
* DESCRIPTION: Reverse namespace hierarchy graph for the current document
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMReverseHierarchyGraph.hpp"

#include "QTMMainTabWindow.hpp"
#include "editor.hpp"
#include "namespaces.hpp"
#include "new_view.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"
#include "vault.hpp"

#include <DockWidget.h>
#include <QApplication>
#include <QByteArray>
#include <QBuffer>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMap>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QScrollBar>
#include <QSet>
#include <QSizeGrip>
#include <QSignalBlocker>
#include <QSlider>
#include <QStringList>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <vector>

#ifndef ATHENA_GRAPHVIZ_FDP_EXECUTABLE
#define ATHENA_GRAPHVIZ_FDP_EXECUTABLE "fdp"
#endif

namespace {

struct RHNode {
  QString id;
  QString label;
  QString kind;
  QPointF pos;
  QSizeF size;
};

struct RHEdge {
  QString from;
  QString to;
};

struct RHGraph {
  QString title;
  QString filePath;
  std::vector<RHNode> nodes;
  std::vector<RHEdge> edges;
};

static ads::CDockWidget* reverse_hierarchy_graph_dock= nullptr;
class ReverseHierarchyGraphPane;
static ReverseHierarchyGraphPane* reverse_hierarchy_graph_widget= nullptr;
constexpr double pi= 3.14159265358979323846;
constexpr const char* default_graph_size= "14cm";

static QString
current_file_path () {
  editor ed= get_current_editor ();
  return to_qstring (concretize (ed->get_name ()));
}

static QString
file_stem (const QString& path) {
  QString stem= QFileInfo (path).completeBaseName ();
  return stem.isEmpty () ? QFileInfo (path).fileName () : stem;
}

static bool
has_string_qt (const strings& xs, const QString& s) {
  for (int i=0; i<N(xs); i++)
    if (to_qstring (xs[i]) == s) return true;
  return false;
}

static void
add_unique_edge (std::vector<RHEdge>& edges, const QString& from,
                 const QString& to) {
  if (from.isEmpty () || to.isEmpty () || from == to) return;
  for (const RHEdge& e: edges)
    if (e.from == from && e.to == to) return;
  edges.push_back ({ from, to });
}

static bool
reverse_hierarchy_simplify_graphs () {
  return get_preference ("vault simplify hierarchy graphs", "off") == "on";
}

static bool
has_alternate_path (const std::vector<RHEdge>& edges, int skip,
                    const QString& from, const QString& to) {
  QMap<QString,QStringList> adjacent;
  for (int i=0; i<(int) edges.size (); i++) {
    if (i == skip) continue;
    adjacent[edges[i].from] << edges[i].to;
  }

  QSet<QString> seen;
  QList<QString> pending;
  pending << from;
  seen.insert (from);
  while (!pending.isEmpty ()) {
    QString current= pending.takeFirst ();
    for (const QString& next: adjacent.value (current)) {
      if (next == to) return true;
      if (seen.contains (next)) continue;
      seen.insert (next);
      pending << next;
    }
  }
  return false;
}

static void
simplify_transitive_edges (std::vector<RHEdge>& edges) {
  std::vector<RHEdge> reduced;
  for (int i=0; i<(int) edges.size (); i++) {
    const RHEdge& e= edges[i];
    if (!has_alternate_path (edges, i, e.from, e.to))
      reduced.push_back (e);
  }
  edges.swap (reduced);
}

static void
collect_parent_namespaces (const QString& name,
                           const QMap<QString,athena_namespace_definition>& all,
                           QSet<QString>& included,
                           std::vector<RHEdge>& edges) {
  if (!all.contains (name)) return;
  const athena_namespace_definition& ns= all[name];
  QStringList parents;
  for (int i=0; i<N(ns.parents); i++) parents << to_qstring (ns.parents[i]);
  for (int i=0; i<N(ns.derived_parents); i++)
    if (!parents.contains (to_qstring (ns.derived_parents[i])))
      parents << to_qstring (ns.derived_parents[i]);

  for (const QString& parent: parents) {
    if (!all.contains (parent)) continue;
    included.insert (parent);
    add_unique_edge (edges, "ns:" + parent, "ns:" + name);
    collect_parent_namespaces (parent, all, included, edges);
  }
}

static bool
build_current_reverse_hierarchy_graph (RHGraph& graph, QString& error) {
  if (!vault_active ()) {
    error= "No active vault.";
    return false;
  }

  QString path= current_file_path ();
  if (path.isEmpty () || !QFileInfo::exists (path)) {
    error= "The current buffer is not a saved file.";
    return false;
  }

  QMap<QString,athena_namespace_definition> all;
  for (const athena_namespace_definition& ns: athena_namespaces_list ())
    all.insert (to_qstring (ns.name), ns);

  QSet<QString> included;
  QStringList matching;
  string match_error;
  string stem= from_qstring (file_stem (path));
  for (auto it= all.constBegin (); it != all.constEnd (); ++it) {
    const athena_namespace_definition& ns= it.value ();
    if (ns.kind == "abstract") continue;
    athena_namespace_match match;
    string local_error;
    if (athena_namespace_match_stem (ns, stem, match, local_error)) {
      included.insert (it.key ());
      matching << it.key ();
    }
    else if (local_error != "" && match_error == "") match_error= local_error;
  }

  if (matching.isEmpty ()) {
    error= "The current file does not match any non-abstract namespace.";
    return false;
  }

  graph= RHGraph ();
  graph.filePath= path;
  graph.title= "Reverse Hierarchy - " + file_stem (path);
  for (const QString& ns: matching) {
    add_unique_edge (graph.edges, "ns:" + ns, "file");
    collect_parent_namespaces (ns, all, included, graph.edges);
  }
  if (reverse_hierarchy_simplify_graphs ())
    simplify_transitive_edges (graph.edges);

  for (auto it= all.constBegin (); it != all.constEnd (); ++it) {
    if (!included.contains (it.key ())) continue;
    RHNode node;
    node.id= "ns:" + it.key ();
    node.label= it.key ();
    node.kind= to_qstring (it.value ().kind);
    node.size= QSizeF (1.9, 0.7);
    graph.nodes.push_back (node);
  }

  RHNode file;
  file.id= "file";
  file.label= file_stem (path);
  file.kind= "file";
  file.size= QSizeF (2.1, 0.7);
  graph.nodes.push_back (file);

  if (match_error != "")
    error= "Namespace sorter warning: " + to_qstring (match_error);
  return true;
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
dot_for_graph (const RHGraph& graph) {
  QString dot= "digraph reverse_hierarchy {\n"
               "  graph [layout=fdp, overlap=prism, splines=true, "
               "sep=\"+2\", nodesep=0.12, ranksep=0.16, K=0.12, "
               "maxiter=3000, margin=0];\n"
               "  node [shape=box, style=\"rounded,filled\", "
               "fontname=\"Alegreya Sans\", fontsize=12, margin=\"0.08,0.04\"];\n"
               "  edge [color=\"#555555\", arrowsize=0.8];\n";
  for (const RHNode& n: graph.nodes) {
    QString fill= n.kind == "file" ? "#e9f3ff" :
                  n.kind == "abstract" ? "#f2ecff" :
                  n.kind == "semi-concrete" ? "#fff5df" : "#eaf7ea";
    dot += QString ("  \"%1\" [label=\"%2\", width=%3, height=%4, "
                    "fillcolor=\"%5\"];\n")
             .arg (dot_escape (n.id), dot_escape (n.label))
             .arg (n.size.width (), 0, 'f', 2)
             .arg (n.size.height (), 0, 'f', 2)
             .arg (fill);
  }
  for (const RHEdge& e: graph.edges)
    dot += QString ("  \"%1\" -> \"%2\";\n")
             .arg (dot_escape (e.from), dot_escape (e.to));
  dot += "}\n";
  return dot;
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

static void compact_graph_positions (RHGraph& graph);

static bool
layout_with_graphviz (RHGraph& graph, QString& error) {
  QProcess proc;
  proc.start (QString::fromUtf8 (ATHENA_GRAPHVIZ_FDP_EXECUTABLE),
              QStringList () << "-Tplain");
  if (!proc.waitForStarted (5000)) {
    error= "Could not start Graphviz fdp.";
    return false;
  }
  QByteArray dot= dot_for_graph (graph).toUtf8 ();
  proc.write (dot);
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

  QMap<QString,RHNode*> byId;
  for (RHNode& n: graph.nodes) byId.insert (n.id, &n);

  double maxY= 0.0;
  QString plain= QString::fromUtf8 (proc.readAllStandardOutput ());
  for (const QString& line: plain.split ('\n')) {
    QStringList tok= plain_tokens (line);
    if (tok.size () < 6 || tok[0] != "node") continue;
    maxY= std::max (maxY, tok[3].toDouble ());
  }

  for (const QString& line: plain.split ('\n')) {
    QStringList tok= plain_tokens (line);
    if (tok.size () < 6 || tok[0] != "node") continue;
    QString id= tok[1];
    if (!byId.contains (id)) continue;
    RHNode* n= byId[id];
    double x= tok[2].toDouble ();
    double y= tok[3].toDouble ();
    double w= tok[4].toDouble ();
    double h= tok[5].toDouble ();
    n->pos= QPointF (x * 42.0, (maxY - y) * 42.0);
    n->size= QSizeF (std::max (w * 92.0, 100.0),
                     std::max (h * 92.0, 42.0));
  }
  compact_graph_positions (graph);
  return true;
}

static const RHNode*
find_node_const (const RHGraph& graph, const QString& id) {
  for (const RHNode& n: graph.nodes)
    if (n.id == id) return &n;
  return nullptr;
}

static void
adjust_node_sizes_for_text (RHGraph& graph) {
  QFont font;
  font.setPointSize (10);
  for (RHNode& n: graph.nodes) {
    QFont nodeFont= font;
    nodeFont.setBold (n.kind == "file");

    QGraphicsTextItem text (n.label);
    text.setFont (nodeFont);
    text.setTextWidth (std::max (n.size.width () - 12.0, 40.0));
    QRectF br= text.boundingRect ();
    double required= br.height () + 12.0;
    if (required > n.size.height ()) n.size.setHeight (required);
  }
}

static QRectF
node_rect (const RHNode& n) {
  return QRectF (n.pos.x () - n.size.width () / 2.0,
                 n.pos.y () - n.size.height () / 2.0,
                 n.size.width (), n.size.height ());
}

static QRectF
padded_node_rect (const RHNode& n, double padding) {
  return node_rect (n).adjusted (-padding, -padding, padding, padding);
}

static void
compact_graph_positions (RHGraph& graph) {
  if (graph.nodes.empty ()) return;

  QPointF center (0.0, 0.0);
  for (const RHNode& n: graph.nodes) center += n.pos;
  center /= (double) graph.nodes.size ();

  for (RHNode& n: graph.nodes)
    n.pos= center + (n.pos - center) * 0.72;

  const double padding= 10.0;
  for (int iter=0; iter<180; iter++) {
    bool changed= false;
    for (size_t i=0; i<graph.nodes.size (); i++) {
      for (size_t j=i+1; j<graph.nodes.size (); j++) {
        QRectF a= padded_node_rect (graph.nodes[i], padding);
        QRectF b= padded_node_rect (graph.nodes[j], padding);
        QRectF overlap= a.intersected (b);
        if (overlap.width () <= 0.0 || overlap.height () <= 0.0) continue;

        QPointF d= graph.nodes[j].pos - graph.nodes[i].pos;
        if (std::abs (d.x ()) < 0.001 && std::abs (d.y ()) < 0.001)
          d= QPointF ((i % 2) ? -1.0 : 1.0, (j % 2) ? -1.0 : 1.0);

        if (overlap.width () < overlap.height ()) {
          double sign= d.x () < 0.0 ? -1.0 : 1.0;
          double push= overlap.width () / 2.0 + 0.5;
          graph.nodes[i].pos.rx () -= sign * push;
          graph.nodes[j].pos.rx () += sign * push;
        }
        else {
          double sign= d.y () < 0.0 ? -1.0 : 1.0;
          double push= overlap.height () / 2.0 + 0.5;
          graph.nodes[i].pos.ry () -= sign * push;
          graph.nodes[j].pos.ry () += sign * push;
        }
        changed= true;
      }
    }
    if (!changed) break;
  }
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

static void
add_arrow (QGraphicsScene* scene, QPointF from, QPointF to, const QPen& pen) {
  QLineF line (from, to);
  if (line.length () < 1.0) return;
  QGraphicsLineItem* lineItem= scene->addLine (line, pen);
  lineItem->setZValue (4);

  double angle= std::atan2 (line.dy (), line.dx ());
  const double arrowSize= 10.0;
  QPointF p1= to - QPointF (std::cos (angle - pi / 6.0) * arrowSize,
                            std::sin (angle - pi / 6.0) * arrowSize);
  QPointF p2= to - QPointF (std::cos (angle + pi / 6.0) * arrowSize,
                            std::sin (angle + pi / 6.0) * arrowSize);
  QPolygonF arrow;
  arrow << to << p1 << p2;
  QGraphicsPolygonItem* arrowItem= scene->addPolygon (arrow, pen, pen.brush ());
  arrowItem->setZValue (4);
}

static QGraphicsScene*
create_scene (const RHGraph& graph) {
  RHGraph adjusted= graph;
  adjust_node_sizes_for_text (adjusted);

  QGraphicsScene* scene= new QGraphicsScene ();
  scene->setBackgroundBrush (QColor ("#fbfbfb"));
  QPen edgePen (QColor ("#555555"), 1.6);
  edgePen.setBrush (QColor ("#555555"));

  for (const RHEdge& e: adjusted.edges) {
    const RHNode* from= find_node_const (adjusted, e.from);
    const RHNode* to= find_node_const (adjusted, e.to);
    if (from == nullptr || to == nullptr) continue;
    QRectF fr= node_rect (*from);
    QRectF tr= node_rect (*to);
    QPointF start= rect_boundary_point (fr, tr.center ());
    QPointF end= rect_boundary_point (tr, fr.center ());
    add_arrow (scene, start, end, edgePen);
  }

  for (const RHNode& n: adjusted.nodes) {
    QRectF rect= node_rect (n);
    QColor fill= n.kind == "file" ? QColor ("#e9f3ff") :
                 n.kind == "abstract" ? QColor ("#f2ecff") :
                 n.kind == "semi-concrete" ? QColor ("#fff5df") :
                 QColor ("#eaf7ea");
    QGraphicsRectItem* box= scene->addRect (
      rect, QPen (QColor ("#333333"), 1.4), QBrush (fill));
    box->setZValue (2);
    QGraphicsTextItem* text= scene->addText (n.label);
    QFont font= text->font ();
    font.setPointSize (10);
    font.setBold (n.kind == "file");
    text->setFont (font);
    text->setDefaultTextColor (QColor ("#111111"));
    text->setTextWidth (rect.width () - 12.0);
    QRectF br= text->boundingRect ();
    text->setPos (rect.center ().x () - br.width () / 2.0,
                  rect.center ().y () - br.height () / 2.0);
    text->setZValue (5);
  }

  QRectF r= scene->itemsBoundingRect ().adjusted (-24, -24, 24, 24);
  scene->setSceneRect (r);
  return scene;
}

class ReverseHierarchyGraphView: public QGraphicsView {
public:
  ReverseHierarchyGraphView (QGraphicsScene* scene, QWidget* parent = nullptr)
    : QGraphicsView (scene, parent), dragging (false), zoomPercent (100) {
    setRenderHints (QPainter::Antialiasing | QPainter::TextAntialiasing);
    setDragMode (QGraphicsView::NoDrag);
    setTransformationAnchor (QGraphicsView::AnchorUnderMouse);
    setResizeAnchor (QGraphicsView::AnchorViewCenter);
    setCursor (Qt::OpenHandCursor);
  }

  void setZoomChangedCallback (std::function<void(int)> cb) {
    zoomChanged= cb;
  }

  void setOwnedScene (QGraphicsScene* scene) {
    QGraphicsScene* old= this->scene ();
    setScene (scene);
    scene->setParent (this);
    if (old != nullptr && old != scene) old->deleteLater ();
  }

  void setZoomPercent (int percent) {
    zoomPercent= std::max (25, std::min (percent, 300));
    resetTransform ();
    double factor= zoomPercent / 100.0;
    scale (factor, factor);
    if (zoomChanged) zoomChanged (zoomPercent);
  }

  void resetViewport () {
    if (scene () == nullptr) return;
    resetTransform ();
    fitInView (scene ()->sceneRect (), Qt::KeepAspectRatio);
    zoomPercent= 100;
    if (zoomChanged) zoomChanged (zoomPercent);
  }

protected:
  void wheelEvent (QWheelEvent* event) override {
    const double factor= event->angleDelta ().y () > 0 ? 1.15 : 1.0 / 1.15;
    scale (factor, factor);
    zoomPercent= std::max (25, std::min ((int) std::round (zoomPercent * factor), 300));
    if (zoomChanged) zoomChanged (zoomPercent);
    event->accept ();
  }

  void mousePressEvent (QMouseEvent* event) override {
    if (event->button () == Qt::LeftButton ||
        event->button () == Qt::MiddleButton) {
      dragging= true;
      lastDragPos= event->pos ();
      setCursor (Qt::ClosedHandCursor);
      event->accept ();
      return;
    }
    QGraphicsView::mousePressEvent (event);
  }

  void mouseMoveEvent (QMouseEvent* event) override {
    if (dragging) {
      QPoint delta= event->pos () - lastDragPos;
      lastDragPos= event->pos ();
      horizontalScrollBar ()->setValue (
        horizontalScrollBar ()->value () - delta.x ());
      verticalScrollBar ()->setValue (
        verticalScrollBar ()->value () - delta.y ());
      event->accept ();
      return;
    }
    QGraphicsView::mouseMoveEvent (event);
  }

  void mouseReleaseEvent (QMouseEvent* event) override {
    if (dragging &&
        (event->button () == Qt::LeftButton ||
         event->button () == Qt::MiddleButton)) {
      dragging= false;
      setCursor (Qt::OpenHandCursor);
      event->accept ();
      return;
    }
    QGraphicsView::mouseReleaseEvent (event);
  }

  void contextMenuEvent (QContextMenuEvent* event) override {
    QMenu menu (this);
    menu.addAction ("Reset viewport", this,
                    [this] () { resetViewport (); });
    menu.exec (event->globalPos ());
  }

private:
  bool dragging;
  int zoomPercent;
  QPoint lastDragPos;
  std::function<void(int)> zoomChanged;
};

static bool build_layout_graph (RHGraph& graph, QString& error);

class ReverseHierarchyGraphPane: public QWidget {
public:
  ReverseHierarchyGraphPane (QWidget* parent = nullptr)
    : QWidget (parent),
      view (new ReverseHierarchyGraphView (new QGraphicsScene (), this)),
      zoomSlider (new QSlider (Qt::Horizontal, this)),
      zoomLabel (new QLabel ("100%", this)),
      lockedCheck (new QCheckBox ("Locked", this)),
      floatingSizeGrip (new QSizeGrip (this)),
      refreshTimer (new QTimer (this)) {
    view->scene ()->setParent (view);
    view->setZoomChangedCallback ([this] (int percent) {
      QSignalBlocker blocker (zoomSlider);
      zoomSlider->setValue (percent);
      zoomLabel->setText (QString::number (percent) + "%");
    });

    QToolButton* zoomOut= new QToolButton (this);
    zoomOut->setIcon (style ()->standardIcon (QStyle::SP_ArrowDown));
    zoomOut->setToolTip ("Zoom out");
    QToolButton* zoomIn= new QToolButton (this);
    zoomIn->setIcon (style ()->standardIcon (QStyle::SP_ArrowUp));
    zoomIn->setToolTip ("Zoom in");
    QToolButton* reset= new QToolButton (this);
    reset->setIcon (style ()->standardIcon (QStyle::SP_BrowserReload));
    reset->setToolTip ("Reset viewport");

    zoomSlider->setRange (25, 300);
    zoomSlider->setValue (100);
    zoomSlider->setSingleStep (5);
    zoomSlider->setPageStep (25);
    zoomSlider->setFixedWidth (160);
    zoomLabel->setMinimumWidth (44);
    lockedCheck->setToolTip (
      "Keep showing this graph instead of following the active document");

    connect (zoomOut, &QToolButton::clicked, this, [this] () {
      view->setZoomPercent (zoomSlider->value () - 10);
    });
    connect (zoomIn, &QToolButton::clicked, this, [this] () {
      view->setZoomPercent (zoomSlider->value () + 10);
    });
    connect (reset, &QToolButton::clicked,
             this, [this] () { view->resetViewport (); });
    connect (zoomSlider, &QSlider::valueChanged,
             this, [this] (int value) { view->setZoomPercent (value); });

    QHBoxLayout* controls= new QHBoxLayout ();
    controls->setContentsMargins (4, 3, 4, 3);
    controls->addWidget (zoomOut);
    controls->addWidget (zoomSlider);
    controls->addWidget (zoomIn);
    controls->addWidget (zoomLabel);
    controls->addSpacing (8);
    controls->addWidget (reset);
    controls->addStretch ();
    controls->addWidget (lockedCheck);

    floatingSizeGrip->hide ();

    QHBoxLayout* gripRow= new QHBoxLayout ();
    gripRow->setContentsMargins (0, 0, 0, 0);
    gripRow->addStretch ();
    gripRow->addWidget (floatingSizeGrip, 0, Qt::AlignRight | Qt::AlignBottom);

    QVBoxLayout* layout= new QVBoxLayout (this);
    layout->setContentsMargins (0, 0, 0, 0);
    layout->addLayout (controls);
    layout->addWidget (view, 1);
    layout->addLayout (gripRow);

    refreshTimer->setInterval (700);
    connect (refreshTimer, &QTimer::timeout,
             this, [this] () { refreshIfActiveDocumentChanged (); });
    refreshTimer->start ();
  }

  QSize sizeHint () const override { return QSize (620, 520); }

  void setFloatingResizeGripVisible (bool visible) {
    floatingSizeGrip->setVisible (visible);
  }

  bool refreshFromCurrentDocument (QString* errorOut = nullptr) {
    RHGraph graph;
    QString error;
    if (!build_layout_graph (graph, error)) {
      if (errorOut != nullptr) *errorOut= error;
      showMessageScene (error);
      return false;
    }

    currentPath= graph.filePath;
    view->setOwnedScene (create_scene (graph));
    QTimer::singleShot (0, view, [this] () { view->resetViewport (); });
    if (reverse_hierarchy_graph_dock != nullptr)
      reverse_hierarchy_graph_dock->setWindowTitle (graph.title);
    return true;
  }

private:
  void showMessageScene (const QString& message) {
    QGraphicsScene* scene= new QGraphicsScene ();
    scene->setBackgroundBrush (QColor ("#fbfbfb"));
    QGraphicsTextItem* text= scene->addText (message);
    text->setDefaultTextColor (QColor ("#884444"));
    text->setTextWidth (420);
    text->setPos (20, 20);
    scene->setSceneRect (0, 0, 500, 160);
    view->setOwnedScene (scene);
    currentPath= current_file_path ();
  }

  void refreshIfActiveDocumentChanged () {
    if (lockedCheck->isChecked ()) return;
    QString path= current_file_path ();
    if (path.isEmpty () || path == currentPath) return;
    refreshFromCurrentDocument ();
  }

  ReverseHierarchyGraphView* view;
  QSlider* zoomSlider;
  QLabel* zoomLabel;
  QCheckBox* lockedCheck;
  QSizeGrip* floatingSizeGrip;
  QTimer* refreshTimer;
  QString currentPath;
};

static bool
build_layout_graph (RHGraph& graph, QString& error) {
  QString warning;
  if (!build_current_reverse_hierarchy_graph (graph, warning)) {
    error= warning;
    return false;
  }
  if (!layout_with_graphviz (graph, error)) return false;
  if (!warning.isEmpty ()) std::cerr << "ATHENA] " << warning.toStdString () << "\n";
  return true;
}

static void
show_error (const QString& error) {
  QMessageBox::warning (QApplication::activeWindow (),
                        "Reverse Hierarchy Graph", error);
}

static QImage
render_graph_image (const RHGraph& graph) {
  QGraphicsScene* scene= create_scene (graph);
  QRectF r= scene->sceneRect ();
  QSize size (std::max (900, (int) std::ceil (r.width ())),
              std::max (520, (int) std::ceil (r.height ())));
  QImage image (size, QImage::Format_ARGB32_Premultiplied);
  image.fill (Qt::white);
  QPainter painter (&image);
  painter.setRenderHints (QPainter::Antialiasing | QPainter::TextAntialiasing);
  scene->render (&painter, QRectF (QPointF (0, 0), QSizeF (size)), r);
  delete scene;
  return image;
}

static tree
graph_error_tree (const QString& error) {
  return tree (WITH, "color", "red",
               tree (from_qstring (QString ("Reverse hierarchy graph: ") +
                                   error)));
}

static tree
graph_image_tree (const QImage& image, string size) {
  QByteArray data;
  QBuffer buffer (&data);
  buffer.open (QIODevice::WriteOnly);
  if (!image.save (&buffer, "PNG"))
    return graph_error_tree ("could not render PNG");

  string raw (data.constData (), data.size ());
  tree img (IMAGE);
  img << tuple (tree (RAW_DATA, raw), "png")
      << tree (size) << tree ("") << tree ("") << tree ("");
  return img;
}

} // namespace

void
reverse_hierarchy_graph_show () {
  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr || win->dockManager () == nullptr) {
    show_error ("No active ATHENA window.");
    return;
  }

  if (reverse_hierarchy_graph_widget == nullptr) {
    reverse_hierarchy_graph_widget= new ReverseHierarchyGraphPane ();
    reverse_hierarchy_graph_widget->resize (620, 520);
    QObject::connect (reverse_hierarchy_graph_widget, &QObject::destroyed, [] () {
      reverse_hierarchy_graph_widget= nullptr;
      reverse_hierarchy_graph_dock= nullptr;
    });
  }

  if (reverse_hierarchy_graph_dock == nullptr) {
    reverse_hierarchy_graph_dock= new ads::CDockWidget (
      "Reverse Hierarchy Graph");
    reverse_hierarchy_graph_dock->setObjectName (
      "athena-reverse-hierarchy-graph");
    reverse_hierarchy_graph_dock->resize (640, 560);
    reverse_hierarchy_graph_dock->setWidget (reverse_hierarchy_graph_widget);
    reverse_hierarchy_graph_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QObject::connect (reverse_hierarchy_graph_dock,
                      &ads::CDockWidget::topLevelChanged,
                      reverse_hierarchy_graph_widget,
                      [] (bool topLevel) {
                        if (reverse_hierarchy_graph_widget != nullptr)
                          reverse_hierarchy_graph_widget->
                            setFloatingResizeGripVisible (topLevel);
                      });
    QObject::connect (reverse_hierarchy_graph_dock, &QObject::destroyed, [] () {
      reverse_hierarchy_graph_dock= nullptr;
      reverse_hierarchy_graph_widget= nullptr;
    });
    win->showAdsDockWidget (reverse_hierarchy_graph_dock,
                            ads::RightDockWidgetArea);
  }

  if (reverse_hierarchy_graph_dock->widget () != reverse_hierarchy_graph_widget)
    reverse_hierarchy_graph_dock->setWidget (reverse_hierarchy_graph_widget);
  win->showAdsDockWidget (reverse_hierarchy_graph_dock,
                          ads::RightDockWidgetArea);
  reverse_hierarchy_graph_widget->setFloatingResizeGripVisible (
    reverse_hierarchy_graph_dock->isInFloatingContainer ());

  QString error;
  if (!reverse_hierarchy_graph_widget->refreshFromCurrentDocument (&error))
    show_error (error);
}

void
reverse_hierarchy_graph_insert () {
  get_current_editor ()->insert_tree (
    tree (make_tree_label ("graph-rev-hierarchy"), default_graph_size));
}

tree
reverse_hierarchy_graph_render (string size) {
  RHGraph graph;
  QString error;
  if (!build_layout_graph (graph, error)) return graph_error_tree (error);
  if (size == "") size= default_graph_size;
  return graph_image_tree (render_graph_image (graph), size);
}
