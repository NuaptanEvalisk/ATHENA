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

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/fruchterman_reingold.hpp>
#include <boost/graph/random_layout.hpp>
#include <boost/graph/topology.hpp>
#include <boost/property_map/property_map.hpp>
#include <boost/random/linear_congruential.hpp>

#include "QTMMainTabWindow.hpp"
#include "QTMGraphTopology.hpp"
#include "QTMVaultInfoModel.hpp"
#include "QTMVaultPreviewWidget.hpp"
#include "ATHENA/Data/reference_graph_cache.hpp"
#include "analyze.hpp"
#include "editor.hpp"
#include "namespaces.hpp"
#include "new_buffer.hpp"
#include "new_view.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"
#include "tm_ostream.hpp"
#include "vault.hpp"

#include <DockWidget.h>
#include <QApplication>
#include <QBasicTimer>
#include <QByteArray>
#include <QBuffer>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMap>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QSizeGrip>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QStyle>
#include <QTimer>
#include <QTimerEvent>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <filesystem>
#include <map>
#include <set>
#include <vector>

namespace {

struct RHNode {
  QString id;
  QString label;
  QString kind;
  QPointF pos;
  QSizeF size;
  QString target;
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

static tree
graph_topology_formula (const QTMGraphTopologySummary& summary,
                        bool homology) {
  tree formula (CONCAT);
  if (homology) {
    formula << tree ("H") << tree (RSUB, "1") << tree ("(G; ")
            << tree ("<bbb-Z>") << tree (") <cong> ")
            << tree ("<bbb-Z>")
            << tree (RSUP, as_string (summary.firstBettiNumber));
    return compound ("math", formula);
  }

  if (summary.components <= 1) {
    formula << tree ("<pi>") << tree (RSUB, "1") << tree ("(G) <cong> F")
            << tree (RSUB, as_string (summary.firstBettiNumber));
    return compound ("math", formula);
  }

  for (int i=0; i<(int) summary.componentRanks.size (); i++) {
    if (i > 0) formula << tree (",   ");
    formula << tree ("<pi>") << tree (RSUB, "1") << tree ("(G")
            << tree (RSUB, as_string (i + 1)) << tree (") <cong> F")
            << tree (RSUB, as_string (summary.componentRanks[i]));
  }
  return compound ("math", formula);
}

static tree
graph_topology_summary_tree (const RHGraph& graph) {
  std::vector<QString> vertices;
  std::vector<std::pair<QString,QString>> edges;
  vertices.reserve (graph.nodes.size ());
  edges.reserve (graph.edges.size ());
  for (const RHNode& node: graph.nodes) vertices.push_back (node.id);
  for (const RHEdge& edge: graph.edges)
    edges.push_back ({ edge.from, edge.to });
  QTMGraphTopologySummary summary= qtm_graph_topology (vertices, edges);

  tree body (DOCUMENT);
  body << compound ("strong", tree ("Underlying undirected graph"));
  if (summary.vertices == 0) {
    body << tree ("No vertices.");
    return body;
  }
  body << graph_topology_formula (summary, true);
  if (summary.components > 1)
    body << tree (from_qstring (
      QString ("%1 connected components; fundamental groups are componentwise:")
        .arg (summary.components)));
  body << graph_topology_formula (summary, false);
  return body;
}

static ads::CDockWidget* reverse_hierarchy_graph_dock= nullptr;
class ReverseHierarchyGraphPane;
static ReverseHierarchyGraphPane* reverse_hierarchy_graph_widget= nullptr;
static ads::CDockWidget* direct_hierarchy_graph_dock= nullptr;
class DirectHierarchyGraphPane;
static DirectHierarchyGraphPane* direct_hierarchy_graph_widget= nullptr;
static ads::CDockWidget* global_hierarchy_graph_dock= nullptr;
static DirectHierarchyGraphPane* global_hierarchy_graph_widget= nullptr;
static ads::CDockWidget* local_reference_graph_dock= nullptr;
static ads::CDockWidget* reference_graph_dock= nullptr;
class ReferenceGraphPane;
static ReferenceGraphPane* local_reference_graph_widget= nullptr;
static ReferenceGraphPane* reference_graph_widget= nullptr;
constexpr double pi= 3.14159265358979323846;
constexpr const char* default_graph_size= "14cm";
constexpr int graph_item_url_role= 1;
constexpr int unlimited_reference_depth= 0;
constexpr int local_reference_depth= 1;
constexpr int default_reference_depth= 2;

static QString
current_buffer_identity () {
  url name= get_current_buffer_safe ();
  return is_none (name) ? QString () : to_qstring (as_string (name));
}

static QString
file_stem (const QString& path) {
  QString stem= QFileInfo (path).completeBaseName ();
  return stem.isEmpty () ? QFileInfo (path).fileName () : stem;
}

static QMap<QString,athena_namespace_definition>
namespace_map () {
  QMap<QString,athena_namespace_definition> all;
  for (const athena_namespace_definition& ns: athena_namespaces_list ())
    all.insert (to_qstring (ns.name), ns);
  return all;
}

static bool
current_physical_file_path (QString& path) {
  url name= get_current_buffer_safe ();
  if (!is_rooted (name, "default") && !is_rooted (name, "file"))
    return false;
  path= to_qstring (concretize (name));
  return !path.isEmpty () && QFileInfo::exists (path);
}

static QStringList
parse_namespace_tmfs_path (const QString& identity) {
  const QString prefix= "tmfs://ns/";
  if (!identity.startsWith (prefix)) return QStringList ();

  QString rest= identity.mid (prefix.size ());
  QStringList out;
  for (const QString& raw: rest.split ('/', Qt::SkipEmptyParts)) {
    QString part= QString::fromUtf8 (
      QByteArray::fromPercentEncoding (raw.toUtf8 ()));
    if (!part.isEmpty ()) out << part;
  }
  if (!out.isEmpty () && out.last ().startsWith ("!"))
    out.last ()= out.last ().mid (1);
  return out;
}

static bool
has_string_qt (const strings& xs, const QString& s) {
  for (int i=0; i<N(xs); i++)
    if (to_qstring (xs[i]) == s) return true;
  return false;
}

static QString
namespace_url (const QString& name) {
  return QString ("tmfs://ns/") + name;
}

static QString
graph_node_target (const RHNode& node, const RHGraph& graph) {
  if (!node.target.isEmpty ()) return node.target;
  if (node.kind == "file" || node.kind == "current-file")
    return graph.filePath;
  return namespace_url (node.label);
}

static bool
file_node_kind (const QString& kind) {
  return kind == "file" || kind == "current-file";
}

static void
open_graph_url (const QString& target) {
  if (target.isEmpty ()) return;
  exec_delayed (scheme_cmd ("(load-buffer (string->url " *
                            scm_quote (from_qstring (target)) * "))"));
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
interactive_elastic_graphs () {
  return get_preference ("interactive elastic graphs", "on") == "on";
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
namespace_has_parent (const athena_namespace_definition& ns,
                      const QString& parent) {
  return has_string_qt (ns.parents, parent) ||
         has_string_qt (ns.derived_parents, parent);
}

static void
collect_child_namespaces (const QString& name,
                          const QMap<QString,athena_namespace_definition>& all,
                          QSet<QString>& included,
                          std::vector<RHEdge>& edges) {
  for (auto it= all.constBegin (); it != all.constEnd (); ++it) {
    QString child= it.key ();
    if (!namespace_has_parent (it.value (), name)) continue;
    included.insert (child);
    add_unique_edge (edges, "ns:" + name, "ns:" + child);
    collect_child_namespaces (child, all, included, edges);
  }
}

static bool
finish_namespace_graph (RHGraph& graph,
                        const QMap<QString,athena_namespace_definition>& all,
                        QSet<QString>& included, bool simplify) {
  if (simplify)
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
  return !graph.nodes.empty ();
}

static bool
build_file_reverse_hierarchy_graph (RHGraph& graph,
                                    const QMap<QString,athena_namespace_definition>& all,
                                    const QString& identity,
                                    const QString& path,
                                    QString& error) {
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
  graph.filePath= identity;
  graph.title= "Reverse Hierarchy - " + file_stem (path);
  for (const QString& ns: matching) {
    add_unique_edge (graph.edges, "ns:" + ns, "file");
    collect_parent_namespaces (ns, all, included, graph.edges);
  }
  finish_namespace_graph (graph, all, included,
                          reverse_hierarchy_simplify_graphs ());

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

static bool
build_namespace_reverse_hierarchy_graph (
  RHGraph& graph, const QMap<QString,athena_namespace_definition>& all,
  const QString& identity, const QStringList& path, QString& error) {
  if (path.isEmpty ()) {
    error= "No namespace specified.";
    return false;
  }

  for (int i=0; i + 1<path.size (); i++) {
    string relation_error;
    if (!athena_namespace_validate_relation (from_qstring (path[i]),
                                             from_qstring (path[i + 1]),
                                             true, relation_error)) {
      error= "Invalid namespace relation: " + to_qstring (relation_error);
      return false;
    }
  }

  QString name= path.last ();
  if (!all.contains (name)) {
    error= "Unknown namespace: " + name;
    return false;
  }

  QSet<QString> included;
  included.insert (name);
  graph= RHGraph ();
  graph.filePath= identity;
  graph.title= "Reverse Hierarchy - Namespace " + name;
  collect_parent_namespaces (name, all, included, graph.edges);
  if (!finish_namespace_graph (graph, all, included,
                               reverse_hierarchy_simplify_graphs ())) {
    error= "Could not build namespace hierarchy graph.";
    return false;
  }
  return true;
}

static bool
build_current_reverse_hierarchy_graph (RHGraph& graph, QString& error) {
  if (!vault_active ()) {
    error= "No active vault.";
    return false;
  }

  QString identity= current_buffer_identity ();
  QMap<QString,athena_namespace_definition> all= namespace_map ();
  QStringList ns_path= parse_namespace_tmfs_path (identity);
  if (!ns_path.isEmpty ())
    return build_namespace_reverse_hierarchy_graph (
      graph, all, identity, ns_path, error);

  QString path;
  if (!current_physical_file_path (path)) {
    error= "Reverse hierarchy applies to saved vault files and tmfs://ns/ namespace pages.";
    return false;
  }

  return build_file_reverse_hierarchy_graph (graph, all, identity, path, error);
}

static QString
namespace_path_to_file (const QString& path) {
  if (path.trimmed ().isEmpty ()) return QString ();
  QFileInfo info (path);
  if (info.isAbsolute ()) return QDir::cleanPath (path);
  return QDir::cleanPath (
    to_qstring (concretize (vault_get_root ())) + "/" + path);
}

static QString
canonical_or_clean_path (const QString& path) {
  QFileInfo info (path);
  QString canonical= info.canonicalFilePath ();
  return canonical.isEmpty () ? QDir::cleanPath (path) : canonical;
}

static bool
namespace_for_current_homepage (
  const QMap<QString,athena_namespace_definition>& all, QString& name) {
  QString path;
  if (!current_physical_file_path (path)) return false;
  QString current= canonical_or_clean_path (path);
  for (auto it= all.constBegin (); it != all.constEnd (); ++it) {
    QString homepage= to_qstring (it.value ().homepage_path);
    if (homepage.trimmed ().isEmpty ()) continue;
    if (canonical_or_clean_path (namespace_path_to_file (homepage)) == current) {
      name= it.key ();
      return true;
    }
  }
  return false;
}

static bool
build_namespace_direct_hierarchy_graph (
  RHGraph& graph, const QMap<QString,athena_namespace_definition>& all,
  const QString& identity, const QString& name, bool simplify,
  QString& error) {
  if (name.isEmpty ()) {
    error= "select a namespace to view direct hierarchy graph";
    return false;
  }
  if (!all.contains (name)) {
    error= "Unknown namespace: " + name;
    return false;
  }

  QSet<QString> included;
  included.insert (name);
  graph= RHGraph ();
  graph.filePath= identity.isEmpty () ? namespace_url (name) : identity;
  graph.title= "Direct Hierarchy - Namespace " + name;
  collect_child_namespaces (name, all, included, graph.edges);
  if (!finish_namespace_graph (graph, all, included, simplify)) {
    error= "Could not build namespace hierarchy graph.";
    return false;
  }
  return true;
}

static bool
build_current_direct_hierarchy_graph (RHGraph& graph, bool simplify,
                                      QString& error) {
  if (!vault_active ()) {
    error= "No active vault.";
    return false;
  }

  QString identity= current_buffer_identity ();
  QMap<QString,athena_namespace_definition> all= namespace_map ();
  QStringList ns_path= parse_namespace_tmfs_path (identity);
  if (!ns_path.isEmpty ())
    return build_namespace_direct_hierarchy_graph (
      graph, all, identity, ns_path.last (), simplify, error);

  QString homepageName;
  if (namespace_for_current_homepage (all, homepageName))
    return build_namespace_direct_hierarchy_graph (
      graph, all, identity, homepageName, simplify, error);

  error= "select a namespace to view direct hierarchy graph";
  return false;
}

static bool
build_named_direct_hierarchy_graph (RHGraph& graph, const QString& name,
                                    bool simplify, QString& error) {
  if (!vault_active ()) {
    error= "No active vault.";
    return false;
  }
  return build_namespace_direct_hierarchy_graph (
    graph, namespace_map (), namespace_url (name), name, simplify, error);
}

static QString
reference_absolute_path (const QString& vaultRoot, const QString& path) {
  QFileInfo info (path);
  return QDir::cleanPath (info.isAbsolute () ? path :
    QDir (vaultRoot).absoluteFilePath (path));
}

static bool
current_reference_source (QString& absolute, QString& relative,
                          QString& error) {
  if (!vault_active ()) {
    error= "No active vault.";
    return false;
  }
  if (!current_physical_file_path (absolute) ||
      QFileInfo (absolute).suffix ().compare ("ath", Qt::CaseInsensitive) != 0) {
    error= "Select a saved .ath note to view its reference graph.";
    return false;
  }

  std::error_code ec;
  std::filesystem::path root= std::filesystem::weakly_canonical (
    std::filesystem::path (
      to_qstring (concretize (vault_get_root ())).toStdString ()), ec);
  if (ec) {
    error= "Could not resolve the active vault root.";
    return false;
  }
  std::filesystem::path source= std::filesystem::weakly_canonical (
    std::filesystem::path (absolute.toStdString ()), ec);
  if (ec) {
    error= "Could not resolve the current note path.";
    return false;
  }
  std::filesystem::path rel= std::filesystem::relative (source, root, ec);
  if (ec || rel.empty () || rel.is_absolute () ||
      (*rel.begin () == "..")) {
    error= "The current note is outside the active vault.";
    return false;
  }
  absolute= QString::fromStdString (source.string ());
  relative= QString::fromStdString (rel.generic_string ());
  return true;
}

static bool
build_reference_graph (
  RHGraph& graph, int maxDepth, bool localGraph,
  const std::function<void(size_t,size_t)>& progress, QString& error)
{
  QString currentAbsolute;
  QString currentRelative;
  if (!current_reference_source (currentAbsolute, currentRelative, error))
    return false;

  std::vector<AthenaReferenceGraphEdge> cachedEdges;
  std::string cacheError;
  if (!athena_reference_graph_query (currentRelative.toStdString (), maxDepth,
                                     cachedEdges, progress, cacheError)) {
    error= QString::fromStdString (cacheError);
    return false;
  }

  graph= RHGraph ();
  graph.filePath= currentAbsolute;
  graph.title= QString (localGraph ? "Local Reference Graph - " :
                                  "Reference Graph - ") +
               file_stem (currentAbsolute);
  QString root= to_qstring (concretize (vault_get_root ()));

  QMap<QString,QString> ids;
  auto ensureNode= [&] (const QString& relativePath) {
    if (ids.contains (relativePath)) return ids.value (relativePath);
    QString id= "ref:" + relativePath;
    RHNode node;
    node.id= id;
    node.label= file_stem (relativePath);
    node.kind= relativePath == currentRelative ? "current-file" : "file";
    node.size= QSizeF (2.1, 0.7);
    node.target= reference_absolute_path (root, relativePath);
    graph.nodes.push_back (node);
    ids.insert (relativePath, id);
    return id;
  };

  ensureNode (currentRelative);
  for (const AthenaReferenceGraphEdge& cached: cachedEdges) {
    QString referenced= QString::fromStdString (cached.referenced_path);
    QString referencing= QString::fromStdString (cached.referencing_path);
    add_unique_edge (graph.edges, ensureNode (referenced),
                     ensureNode (referencing));
  }
  return true;
}

static void compact_graph_positions (RHGraph& graph);
static void resize_nodes_for_text (RHGraph& graph);

static bool
layout_with_boost_force_directed (RHGraph& graph, QString& error) {
  if (graph.nodes.empty ()) {
    error= "Graph has no nodes.";
    return false;
  }

  resize_nodes_for_text (graph);

  using BoostGraph= boost::adjacency_list<boost::vecS, boost::vecS,
                                          boost::undirectedS>;
  using Topology= boost::rectangle_topology<boost::minstd_rand>;
  using Point= Topology::point_type;

  BoostGraph boostGraph (graph.nodes.size ());
  QMap<QString,int> indexById;
  for (int i=0; i<(int) graph.nodes.size (); i++)
    indexById.insert (graph.nodes[i].id, i);

  for (const RHEdge& edge: graph.edges) {
    if (!indexById.contains (edge.from) || !indexById.contains (edge.to))
      continue;
    boost::add_edge (indexById.value (edge.from),
                     indexById.value (edge.to), boostGraph);
  }

  double maxWidth= 0.0;
  double maxHeight= 0.0;
  for (const RHNode& node: graph.nodes) {
    maxWidth= std::max (maxWidth, node.size.width ());
    maxHeight= std::max (maxHeight, node.size.height ());
  }

  double n= std::max (1.0, (double) graph.nodes.size ());
  double extent= std::sqrt (n);
  double width= std::max (900.0, extent * std::max (220.0, maxWidth * 1.8));
  double height= std::max (620.0, extent * std::max (150.0, maxHeight * 4.0));

  boost::minstd_rand rng (5489u);
  Topology topology (rng, 0.0, 0.0, width, height);
  std::vector<Point> positions (graph.nodes.size ());
  auto positionMap= boost::make_iterator_property_map (
    positions.begin (), boost::get (boost::vertex_index, boostGraph));

  boost::random_graph_layout (boostGraph, positionMap, topology);
  boost::fruchterman_reingold_force_directed_layout (
    boostGraph, positionMap, topology,
    boost::cooling (boost::linear_cooling<double> (
      std::max<std::size_t> (200, graph.nodes.size () * 8))));

  for (int i=0; i<(int) graph.nodes.size (); i++)
    graph.nodes[i].pos= QPointF (positions[i][0], positions[i][1]);

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
resize_nodes_for_text (RHGraph& graph) {
  QFont font;
  font.setPointSize (10);
  for (RHNode& n: graph.nodes) {
    QFont nodeFont= font;
    nodeFont.setBold (file_node_kind (n.kind));

    QGraphicsTextItem text (n.label);
    text.setFont (nodeFont);
    QRectF natural= text.boundingRect ();
    double requiredWidth= std::min (std::max (natural.width () + 24.0,
                                             n.size.width ()),
                                    420.0);
    if (requiredWidth > n.size.width ()) n.size.setWidth (requiredWidth);

    text.setTextWidth (std::max (n.size.width () - 12.0, 40.0));
    QRectF br= text.boundingRect ();
    double required= br.height () + 12.0;
    if (required > n.size.height ()) n.size.setHeight (required);
  }
}

static void
adjust_node_sizes_for_text (RHGraph& graph) {
  resize_nodes_for_text (graph);
  compact_graph_positions (graph);
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

class ElasticHierarchyScene;
class ElasticHierarchyEdgeItem;

class ElasticHierarchyNodeItem: public QGraphicsItem {
public:
  enum { Type = UserType + 101 };
  enum HighlightState { NormalHighlight, RelatedHighlight, HoverHighlight };

  ElasticHierarchyNodeItem (const RHNode& node, const QString& target,
                            bool movable)
    : nodeId (node.id), nodeSize (node.size), nodeKind (node.kind),
      textItem (new QGraphicsTextItem (node.label, this)), movable (movable),
      highlightState (NormalHighlight), pressed (false) {
    setFlag (ItemIsMovable, movable);
    setFlag (ItemSendsGeometryChanges, true);
    setAcceptHoverEvents (true);
    setCacheMode (DeviceCoordinateCache);
    setCursor (movable ? Qt::OpenHandCursor : Qt::ArrowCursor);
    setZValue (2);
    setData (graph_item_url_role, target);
    setToolTip (target.isEmpty () ? "Double click to open" :
                "Double click to open\n" + target);

    QFont font= textItem->font ();
    font.setPointSize (10);
    font.setBold (file_node_kind (node.kind));
    textItem->setFont (font);
    textItem->setDefaultTextColor (QColor ("#111111"));
    textItem->setTextWidth (node.size.width () - 12.0);
    QRectF br= textItem->boundingRect ();
    textItem->setPos (-br.width () / 2.0, -br.height () / 2.0);
    textItem->setZValue (3);
    textItem->setAcceptedMouseButtons (Qt::NoButton);
    textItem->setData (graph_item_url_role, target);
    textItem->setToolTip (toolTip ());
  }

  int type () const override { return Type; }
  QRectF boundingRect () const override {
    return QRectF (-nodeSize.width () / 2.0, -nodeSize.height () / 2.0,
                   nodeSize.width (), nodeSize.height ());
  }

  void paint (QPainter* painter, const QStyleOptionGraphicsItem*,
              QWidget*) override {
    QColor fill= nodeKind == "current-file" ? QColor ("#cfe5ff") :
                 nodeKind == "file" ? QColor ("#e9f3ff") :
                 nodeKind == "abstract" ? QColor ("#f2ecff") :
                 nodeKind == "semi-concrete" ? QColor ("#fff5df") :
                 QColor ("#eaf7ea");
    if (highlightState == RelatedHighlight) fill= fill.lighter (106);
    if (highlightState == HoverHighlight) fill= QColor ("#fff1b8");
    if (pressed) fill= fill.darker (106);
    QColor border= highlightState == HoverHighlight ? QColor ("#c06b00") :
                   highlightState == RelatedHighlight ? QColor ("#1466b8") :
                   QColor ("#333333");
    double width= highlightState == NormalHighlight ? 1.4 : 2.6;
    painter->setPen (QPen (border, width));
    painter->setBrush (fill);
    painter->drawRect (boundingRect ());
  }

  void addEdge (ElasticHierarchyEdgeItem* edge) { edges << edge; }
  const QList<ElasticHierarchyEdgeItem*>& edgeItems () const { return edges; }
  QSizeF size () const { return nodeSize; }
  void setHighlightState (HighlightState state, bool dimmed) {
    highlightState= state;
    setOpacity (dimmed ? 0.22 : 1.0);
    update ();
  }

protected:
  QVariant itemChange (GraphicsItemChange change,
                       const QVariant& value) override;
  void mousePressEvent (QGraphicsSceneMouseEvent* event) override {
    pressed= true;
    setCursor (Qt::ClosedHandCursor);
    update ();
    QGraphicsItem::mousePressEvent (event);
  }
  void mouseReleaseEvent (QGraphicsSceneMouseEvent* event) override;
  void hoverEnterEvent (QGraphicsSceneHoverEvent* event) override;
  void hoverMoveEvent (QGraphicsSceneHoverEvent* event) override;
  void hoverLeaveEvent (QGraphicsSceneHoverEvent* event) override;

private:
  QString nodeId;
  QSizeF nodeSize;
  QString nodeKind;
  QGraphicsTextItem* textItem;
  QList<ElasticHierarchyEdgeItem*> edges;
  bool movable;
  HighlightState highlightState;
  bool pressed;
};

class ElasticHierarchyEdgeItem: public QGraphicsItem {
public:
  enum { Type = UserType + 102 };

  ElasticHierarchyEdgeItem (ElasticHierarchyNodeItem* source,
                            ElasticHierarchyNodeItem* destination)
    : source (source), destination (destination), arrowSize (10.0) {
    setAcceptedMouseButtons (Qt::NoButton);
    setZValue (1);
    source->addEdge (this);
    destination->addEdge (this);
    adjust ();
  }

  int type () const override { return Type; }
  ElasticHierarchyNodeItem* sourceNode () const { return source; }
  ElasticHierarchyNodeItem* destinationNode () const { return destination; }
  void setHighlighted (bool value, bool dimmed) {
    highlighted= value;
    setOpacity (dimmed ? 0.16 : 1.0);
    setZValue (highlighted ? 1.5 : 1.0);
    update ();
  }

  void adjust () {
    if (source == nullptr || destination == nullptr) return;
    QRectF sourceRect= source->mapRectToScene (source->boundingRect ());
    QRectF destinationRect=
      destination->mapRectToScene (destination->boundingRect ());
    QPointF sourcePointScene=
      rect_boundary_point (sourceRect, destinationRect.center ());
    QPointF destinationPointScene=
      rect_boundary_point (destinationRect, sourceRect.center ());
    prepareGeometryChange ();
    sourcePoint= mapFromScene (sourcePointScene);
    destinationPoint= mapFromScene (destinationPointScene);
  }

  QRectF boundingRect () const override {
    double extra= arrowSize + 3.0;
    return QRectF (sourcePoint, destinationPoint).normalized ().adjusted (
      -extra, -extra, extra, extra);
  }

  void paint (QPainter* painter, const QStyleOptionGraphicsItem*,
              QWidget*) override {
    QLineF line (sourcePoint, destinationPoint);
    if (line.length () < 1.0) return;

    QPen pen (highlighted ? QColor ("#1466b8") : QColor ("#555555"),
              highlighted ? 3.0 : 1.6, Qt::SolidLine,
              Qt::RoundCap, Qt::RoundJoin);
    painter->setPen (pen);
    painter->setBrush (pen.color ());
    painter->drawLine (line);

    double angle= std::atan2 (line.dy (), line.dx ());
    QPointF p1= destinationPoint -
      QPointF (std::cos (angle - pi / 6.0) * arrowSize,
               std::sin (angle - pi / 6.0) * arrowSize);
    QPointF p2= destinationPoint -
      QPointF (std::cos (angle + pi / 6.0) * arrowSize,
               std::sin (angle + pi / 6.0) * arrowSize);
    painter->drawPolygon (QPolygonF () << destinationPoint << p1 << p2);
  }

private:
  ElasticHierarchyNodeItem* source;
  ElasticHierarchyNodeItem* destination;
  QPointF sourcePoint;
  QPointF destinationPoint;
  double arrowSize;
  bool highlighted= false;
};

class ElasticHierarchyScene: public QGraphicsScene {
public:
  explicit ElasticHierarchyScene (const RHGraph& graph, bool elastic,
                                  bool referenceHover)
    : elastic (elastic), referenceHover (referenceHover), building (true),
      stableTicks (0), hoveredNode (nullptr) {
    setBackgroundBrush (QColor ("#fbfbfb"));
    setItemIndexMethod (QGraphicsScene::NoIndex);

    RHGraph adjusted= graph;
    adjust_node_sizes_for_text (adjusted);
    for (const RHNode& node: adjusted.nodes) {
      QString target= graph_node_target (node, graph);
      ElasticHierarchyNodeItem* item=
        new ElasticHierarchyNodeItem (node, target, elastic);
      addItem (item);
      item->setPos (node.pos);
      nodes << item;
      nodesById.insert (node.id, item);
      velocities.insert (item, QPointF ());
    }

    for (const RHEdge& edge: adjusted.edges) {
      ElasticHierarchyNodeItem* source= nodesById.value (edge.from, nullptr);
      ElasticHierarchyNodeItem* destination=
        nodesById.value (edge.to, nullptr);
      if (source == nullptr || destination == nullptr) continue;
      ElasticHierarchyEdgeItem* item=
        new ElasticHierarchyEdgeItem (source, destination);
      addItem (item);
      edges << item;
    }

    QPointF sum;
    for (ElasticHierarchyNodeItem* node: nodes) sum += node->pos ();
    center= nodes.isEmpty () ? QPointF () : sum / nodes.size ();
    building= false;
    expandSceneRect ();
  }

  void nodeMoved () {
    if (building) return;
    expandSceneRect ();
    if (elastic && mouseGrabberItem () != nullptr) startSimulation ();
  }

  void nodeReleased () {
    stopSimulation ();
  }

  void referenceHoverChanged (ElasticHierarchyNodeItem* node,
                              Qt::KeyboardModifiers modifiers) {
    if (!referenceHover) return;
    hoveredNode= node;
    applyReferenceHighlight (modifiers.testFlag (Qt::ShiftModifier));
  }

  void referenceHoverLeft (ElasticHierarchyNodeItem* node) {
    if (!referenceHover || hoveredNode != node) return;
    hoveredNode= nullptr;
    clearReferenceHighlight ();
  }

  void referenceShiftChanged (bool pressed) {
    if (referenceHover && hoveredNode != nullptr)
      applyReferenceHighlight (pressed);
  }

protected:
  void timerEvent (QTimerEvent* event) override {
    if (event->timerId () != timer.timerId ()) {
      QGraphicsScene::timerEvent (event);
      return;
    }
    advanceSimulation ();
  }

  void keyPressEvent (QKeyEvent* event) override {
    if (event->key () == Qt::Key_Shift) referenceShiftChanged (true);
    QGraphicsScene::keyPressEvent (event);
  }

  void keyReleaseEvent (QKeyEvent* event) override {
    if (event->key () == Qt::Key_Shift) referenceShiftChanged (false);
    QGraphicsScene::keyReleaseEvent (event);
  }

private:
  void startSimulation () {
    stableTicks= 0;
    if (!timer.isActive ()) timer.start (40, this);
  }

  void stopSimulation () {
    timer.stop ();
    stableTicks= 0;
    for (ElasticHierarchyNodeItem* node: nodes)
      velocities[node]= QPointF ();
  }

  void clearReferenceHighlight () {
    for (ElasticHierarchyNodeItem* node: nodes)
      node->setHighlightState (
        ElasticHierarchyNodeItem::NormalHighlight, false);
    for (ElasticHierarchyEdgeItem* edge: edges)
      edge->setHighlighted (false, false);
  }

  void applyReferenceHighlight (bool recursive) {
    if (hoveredNode == nullptr) {
      clearReferenceHighlight ();
      return;
    }

    QSet<ElasticHierarchyNodeItem*> selectedNodes;
    QSet<ElasticHierarchyEdgeItem*> selectedEdges;
    QList<ElasticHierarchyNodeItem*> pending;
    selectedNodes.insert (hoveredNode);
    pending << hoveredNode;
    while (!pending.isEmpty ()) {
      ElasticHierarchyNodeItem* destination= pending.takeFirst ();
      for (ElasticHierarchyEdgeItem* edge: edges) {
        if (edge->destinationNode () != destination) continue;
        selectedEdges.insert (edge);
        ElasticHierarchyNodeItem* source= edge->sourceNode ();
        if (selectedNodes.contains (source)) continue;
        selectedNodes.insert (source);
        if (recursive) pending << source;
      }
      if (!recursive) break;
    }

    for (ElasticHierarchyNodeItem* node: nodes) {
      bool selected= selectedNodes.contains (node);
      ElasticHierarchyNodeItem::HighlightState state=
        node == hoveredNode ? ElasticHierarchyNodeItem::HoverHighlight :
        selected ? ElasticHierarchyNodeItem::RelatedHighlight :
                   ElasticHierarchyNodeItem::NormalHighlight;
      node->setHighlightState (state, !selected);
    }
    for (ElasticHierarchyEdgeItem* edge: edges) {
      bool selected= selectedEdges.contains (edge);
      edge->setHighlighted (selected, !selected);
    }
  }

  void expandSceneRect () {
    QRectF needed= itemsBoundingRect ().adjusted (-80, -80, 80, 80);
    if (sceneRect ().isNull ()) setSceneRect (needed);
    else if (!sceneRect ().contains (needed))
      setSceneRect (sceneRect ().united (needed));
  }

  void advanceSimulation () {
    if (nodes.isEmpty ()) {
      timer.stop ();
      return;
    }

    QHash<ElasticHierarchyNodeItem*, QPointF> forces;
    for (ElasticHierarchyNodeItem* node: nodes)
      forces.insert (node, QPointF ());

    for (int i=0; i<nodes.size (); ++i) {
      ElasticHierarchyNodeItem* a= nodes[i];
      for (int j=i+1; j<nodes.size (); ++j) {
        ElasticHierarchyNodeItem* b= nodes[j];
        QPointF delta= b->pos () - a->pos ();
        double distance= std::hypot (delta.x (), delta.y ());
        if (distance < 0.01) {
          delta= QPointF ((i & 1) ? -1.0 : 1.0,
                         (j & 1) ? -1.0 : 1.0);
          distance= std::hypot (delta.x (), delta.y ());
        }
        QPointF direction= delta / distance;

        double overlapX= (a->size ().width () + b->size ().width ()) / 2.0 +
                         24.0 - std::abs (delta.x ());
        double overlapY= (a->size ().height () + b->size ().height ()) / 2.0 +
                         24.0 - std::abs (delta.y ());
        double magnitude;
        if (overlapX > 0.0 && overlapY > 0.0) {
          if (overlapX < overlapY)
            direction= QPointF (delta.x () < 0.0 ? -1.0 : 1.0, 0.0);
          else
            direction= QPointF (0.0, delta.y () < 0.0 ? -1.0 : 1.0);
          magnitude= 4.0 + std::min (24.0,
            std::min (overlapX, overlapY) * 0.35);
        }
        else
          magnitude= 9000.0 / std::max (400.0, distance * distance);

        QPointF force= direction * magnitude;
        forces[a] -= force;
        forces[b] += force;
      }
    }

    for (ElasticHierarchyEdgeItem* edge: edges) {
      ElasticHierarchyNodeItem* source= edge->sourceNode ();
      ElasticHierarchyNodeItem* destination= edge->destinationNode ();
      QPointF delta= destination->pos () - source->pos ();
      double distance= std::hypot (delta.x (), delta.y ());
      if (distance < 0.01) continue;
      double sourceRadius= std::hypot (source->size ().width (),
                                      source->size ().height ()) / 2.0;
      double destinationRadius=
        std::hypot (destination->size ().width (),
                    destination->size ().height ()) / 2.0;
      double ideal= sourceRadius + destinationRadius + 70.0;
      QPointF force= (delta / distance) * ((distance - ideal) * 0.012);
      forces[source] += force;
      forces[destination] -= force;
    }

    double maximumMovement= 0.0;
    QGraphicsItem* grabbed= mouseGrabberItem ();
    for (ElasticHierarchyNodeItem* node: nodes) {
      if (grabbed == node) {
        velocities[node]= QPointF ();
        continue;
      }

      QPointF force= forces.value (node) + (center - node->pos ()) * 0.0015;
      QPointF velocity= (velocities.value (node) + force) * 0.72;
      double speed= std::hypot (velocity.x (), velocity.y ());
      if (speed > 14.0) velocity *= 14.0 / speed;
      else if (speed < 0.05) velocity= QPointF ();
      velocities[node]= velocity;
      maximumMovement= std::max (
        maximumMovement, std::hypot (velocity.x (), velocity.y ()));
      if (!velocity.isNull ()) node->setPos (node->pos () + velocity);
    }

    expandSceneRect ();
    if (maximumMovement < 0.12) stableTicks++;
    else stableTicks= 0;
    if (stableTicks >= 6) timer.stop ();
  }

  QList<ElasticHierarchyNodeItem*> nodes;
  QList<ElasticHierarchyEdgeItem*> edges;
  QMap<QString, ElasticHierarchyNodeItem*> nodesById;
  QHash<ElasticHierarchyNodeItem*, QPointF> velocities;
  QBasicTimer timer;
  QPointF center;
  bool elastic;
  bool referenceHover;
  bool building;
  int stableTicks;
  ElasticHierarchyNodeItem* hoveredNode;
};

QVariant
ElasticHierarchyNodeItem::itemChange (GraphicsItemChange change,
                                      const QVariant& value) {
  if (change == ItemPositionHasChanged) {
    for (ElasticHierarchyEdgeItem* edge: edges) edge->adjust ();
    if (ElasticHierarchyScene* elasticScene=
          dynamic_cast<ElasticHierarchyScene*> (scene ()))
      elasticScene->nodeMoved ();
  }
  return QGraphicsItem::itemChange (change, value);
}

void
ElasticHierarchyNodeItem::mouseReleaseEvent (QGraphicsSceneMouseEvent* event) {
  pressed= false;
  setCursor (Qt::OpenHandCursor);
  update ();
  QGraphicsItem::mouseReleaseEvent (event);
  if (ElasticHierarchyScene* elasticScene=
        dynamic_cast<ElasticHierarchyScene*> (scene ()))
    elasticScene->nodeReleased ();
}

void
ElasticHierarchyNodeItem::hoverEnterEvent (QGraphicsSceneHoverEvent* event) {
  if (ElasticHierarchyScene* elasticScene=
        dynamic_cast<ElasticHierarchyScene*> (scene ()))
    elasticScene->referenceHoverChanged (this, event->modifiers ());
  QGraphicsItem::hoverEnterEvent (event);
}

void
ElasticHierarchyNodeItem::hoverMoveEvent (QGraphicsSceneHoverEvent* event) {
  if (ElasticHierarchyScene* elasticScene=
        dynamic_cast<ElasticHierarchyScene*> (scene ()))
    elasticScene->referenceHoverChanged (this, event->modifiers ());
  QGraphicsItem::hoverMoveEvent (event);
}

void
ElasticHierarchyNodeItem::hoverLeaveEvent (QGraphicsSceneHoverEvent* event) {
  if (ElasticHierarchyScene* elasticScene=
        dynamic_cast<ElasticHierarchyScene*> (scene ()))
    elasticScene->referenceHoverLeft (this);
  QGraphicsItem::hoverLeaveEvent (event);
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
    QColor fill= n.kind == "current-file" ? QColor ("#cfe5ff") :
                 n.kind == "file" ? QColor ("#e9f3ff") :
                 n.kind == "abstract" ? QColor ("#f2ecff") :
                 n.kind == "semi-concrete" ? QColor ("#fff5df") :
                 QColor ("#eaf7ea");
    QGraphicsRectItem* box= scene->addRect (
      rect, QPen (QColor ("#333333"), 1.4), QBrush (fill));
    box->setZValue (2);
    box->setData (graph_item_url_role, graph_node_target (n, graph));
    QString target= graph_node_target (n, graph);
    box->setToolTip (target.isEmpty () ? "Double click to open" :
                     "Double click to open\n" + target);
    QGraphicsTextItem* text= scene->addText (n.label);
    QFont font= text->font ();
    font.setPointSize (10);
    font.setBold (file_node_kind (n.kind));
    text->setFont (font);
    text->setDefaultTextColor (QColor ("#111111"));
    text->setTextWidth (rect.width () - 12.0);
    QRectF br= text->boundingRect ();
    text->setPos (rect.center ().x () - br.width () / 2.0,
                  rect.center ().y () - br.height () / 2.0);
    text->setZValue (5);
    text->setData (graph_item_url_role, graph_node_target (n, graph));
    text->setToolTip (box->toolTip ());
  }

  QRectF r= scene->itemsBoundingRect ().adjusted (-24, -24, 24, 24);
  scene->setSceneRect (r);
  return scene;
}

static QGraphicsScene*
create_pane_scene (const RHGraph& graph, bool referenceHover = false) {
  bool elastic= interactive_elastic_graphs ();
  if (elastic || referenceHover)
    return new ElasticHierarchyScene (graph, elastic, referenceHover);
  return create_scene (graph);
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
  void keyPressEvent (QKeyEvent* event) override {
    if (event->key () == Qt::Key_Shift) {
      if (ElasticHierarchyScene* elasticScene=
            dynamic_cast<ElasticHierarchyScene*> (scene ()))
        elasticScene->referenceShiftChanged (true);
    }
    QGraphicsView::keyPressEvent (event);
  }

  void keyReleaseEvent (QKeyEvent* event) override {
    if (event->key () == Qt::Key_Shift) {
      if (ElasticHierarchyScene* elasticScene=
            dynamic_cast<ElasticHierarchyScene*> (scene ()))
        elasticScene->referenceShiftChanged (false);
    }
    QGraphicsView::keyReleaseEvent (event);
  }

  void wheelEvent (QWheelEvent* event) override {
    const double factor= event->angleDelta ().y () > 0 ? 1.15 : 1.0 / 1.15;
    scale (factor, factor);
    zoomPercent= std::max (25, std::min ((int) std::round (zoomPercent * factor), 300));
    if (zoomChanged) zoomChanged (zoomPercent);
    event->accept ();
  }

  void mousePressEvent (QMouseEvent* event) override {
    if (event->button () == Qt::LeftButton) {
      QGraphicsItem* item= itemAt (event->pos ());
      while (item != nullptr &&
             !(item->flags () & QGraphicsItem::ItemIsMovable))
        item= item->parentItem ();
      if (item != nullptr) {
        dragging= false;
        QGraphicsView::mousePressEvent (event);
        return;
      }
    }
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

  void mouseDoubleClickEvent (QMouseEvent* event) override {
    if (event->button () == Qt::LeftButton) {
      QGraphicsItem* item= itemAt (event->pos ());
      while (item != nullptr) {
        QVariant target= item->data (graph_item_url_role);
        if (target.isValid () && !target.toString ().isEmpty ()) {
          dragging= false;
          setCursor (Qt::OpenHandCursor);
          open_graph_url (target.toString ());
          event->accept ();
          return;
        }
        item= item->parentItem ();
      }
    }
    QGraphicsView::mouseDoubleClickEvent (event);
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

    setGraphScene (graph);
    if (reverse_hierarchy_graph_dock != nullptr)
      reverse_hierarchy_graph_dock->setWindowTitle (graph.title);
    return true;
  }

  void interactionPreferenceChanged () {
    if (hasCurrentGraph) setGraphScene (currentGraph);
  }

private:
  void setGraphScene (const RHGraph& graph) {
    currentGraph= graph;
    hasCurrentGraph= true;
    currentPath= graph.filePath;
    view->setOwnedScene (create_pane_scene (currentGraph));
    QTimer::singleShot (0, view, [this] () { view->resetViewport (); });
  }

  void showMessageScene (const QString& message) {
    QGraphicsScene* scene= new QGraphicsScene ();
    scene->setBackgroundBrush (QColor ("#fbfbfb"));
    QGraphicsTextItem* text= scene->addText (message);
    text->setDefaultTextColor (QColor ("#884444"));
    text->setTextWidth (420);
    text->setPos (20, 20);
    scene->setSceneRect (0, 0, 500, 160);
    view->setOwnedScene (scene);
    hasCurrentGraph= false;
    currentPath= current_buffer_identity ();
  }

  void refreshIfActiveDocumentChanged () {
    if (lockedCheck->isChecked ()) return;
    QString path= current_buffer_identity ();
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
  RHGraph currentGraph;
  bool hasCurrentGraph= false;
};

class DirectHierarchyGraphPane: public QWidget {
public:
  DirectHierarchyGraphPane (ads::CDockWidget** dockRef,
                             const QString& defaultTitle,
                             bool allowFollow,
                             QWidget* parent = nullptr)
    : QWidget (parent),
      dockRef (dockRef),
      defaultTitle (defaultTitle),
      allowFollow (allowFollow),
      view (new ReverseHierarchyGraphView (new QGraphicsScene (), this)),
      zoomSlider (new QSlider (Qt::Horizontal, this)),
      zoomLabel (new QLabel ("100%", this)),
      followCheck (new QCheckBox ("Follow viewport", this)),
      simplifyCheck (new QCheckBox ("Simplify", this)),
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
    followCheck->setChecked (allowFollow);
    followCheck->setToolTip (
      "Rebuild the graph when the active namespace page changes");
    followCheck->setVisible (allowFollow);
    simplifyCheck->setChecked (reverse_hierarchy_simplify_graphs ());
    simplifyCheck->setToolTip (
      "Remove transitive containment edges such as A -> C when A -> B -> C exists");

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
    connect (simplifyCheck, &QCheckBox::toggled, this, [this] (bool checked) {
      set_preference ("vault simplify hierarchy graphs",
                      checked ? "on" : "off");
      refreshUsingCurrentMode ();
    });
    connect (followCheck, &QCheckBox::toggled, this, [this] (bool checked) {
      if (checked) refreshFromCurrentDocument ();
    });

    QHBoxLayout* controls= new QHBoxLayout ();
    controls->setContentsMargins (4, 3, 4, 3);
    controls->addWidget (zoomOut);
    controls->addWidget (zoomSlider);
    controls->addWidget (zoomIn);
    controls->addWidget (zoomLabel);
    controls->addSpacing (8);
    controls->addWidget (reset);
    controls->addStretch ();
    controls->addWidget (followCheck);
    controls->addWidget (simplifyCheck);

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
    fixedNamespace.clear ();
    fixedTitleOverride.clear ();
    return rebuildFromCurrentDocument (errorOut);
  }

  bool refreshFromNamespace (const QString& name,
                             QString* errorOut = nullptr,
                             const QString& titleOverride = QString ()) {
    fixedNamespace= name;
    fixedTitleOverride= titleOverride;
    followCheck->setChecked (false);
    return rebuildFromNamespace (name, errorOut, titleOverride);
  }

  void showMessage (const QString& message) {
    fixedNamespace.clear ();
    fixedTitleOverride.clear ();
    followCheck->setChecked (false);
    showMessageScene (message);
  }

  void interactionPreferenceChanged () {
    if (hasCurrentGraph)
      setGraphScene (currentGraph, currentTitleOverride);
  }

private:
  bool simplify () const { return simplifyCheck->isChecked (); }

  void setGraphScene (const RHGraph& graph,
                      const QString& titleOverride = QString ()) {
    currentGraph= graph;
    currentTitleOverride= titleOverride;
    hasCurrentGraph= true;
    currentPath= graph.filePath;
    view->setOwnedScene (create_pane_scene (currentGraph));
    QTimer::singleShot (0, view, [this] () { view->resetViewport (); });
    if (dockRef != nullptr && *dockRef != nullptr)
      (*dockRef)->setWindowTitle (
        titleOverride.isEmpty () ? graph.title : titleOverride);
  }

  bool rebuildFromCurrentDocument (QString* errorOut = nullptr) {
    RHGraph graph;
    QString error;
    if (!build_current_direct_hierarchy_graph (graph, simplify (), error)) {
      if (errorOut != nullptr) *errorOut= error;
      showMessageScene (error);
      return false;
    }
    if (!layout_with_boost_force_directed (graph, error)) {
      if (errorOut != nullptr) *errorOut= error;
      showMessageScene (error);
      return false;
    }
    setGraphScene (graph);
    return true;
  }

  bool rebuildFromNamespace (const QString& name,
                             QString* errorOut = nullptr,
                             const QString& titleOverride = QString ()) {
    RHGraph graph;
    QString error;
    if (!build_named_direct_hierarchy_graph (graph, name, simplify (), error)) {
      if (errorOut != nullptr) *errorOut= error;
      showMessageScene (error);
      return false;
    }
    if (!layout_with_boost_force_directed (graph, error)) {
      if (errorOut != nullptr) *errorOut= error;
      showMessageScene (error);
      return false;
    }
    setGraphScene (graph, titleOverride);
    return true;
  }

  void refreshUsingCurrentMode () {
    if (followCheck->isChecked () || fixedNamespace.isEmpty ())
      refreshFromCurrentDocument ();
    else rebuildFromNamespace (fixedNamespace, nullptr, fixedTitleOverride);
  }

  void showMessageScene (const QString& message) {
    QGraphicsScene* scene= new QGraphicsScene ();
    scene->setBackgroundBrush (QColor ("#fbfbfb"));
    QGraphicsTextItem* text= scene->addText (message);
    text->setDefaultTextColor (QColor ("#444444"));
    text->setTextWidth (420);
    text->setPos (20, 20);
    scene->setSceneRect (0, 0, 500, 160);
    view->setOwnedScene (scene);
    hasCurrentGraph= false;
    currentPath= current_buffer_identity ();
    if (dockRef != nullptr && *dockRef != nullptr)
      (*dockRef)->setWindowTitle (defaultTitle);
  }

  void refreshIfActiveDocumentChanged () {
    if (!allowFollow || !followCheck->isChecked ()) return;
    QString path= current_buffer_identity ();
    if (path.isEmpty () || path == currentPath) return;
    rebuildFromCurrentDocument ();
  }

  ads::CDockWidget** dockRef;
  QString defaultTitle;
  bool allowFollow;
  ReverseHierarchyGraphView* view;
  QSlider* zoomSlider;
  QLabel* zoomLabel;
  QCheckBox* followCheck;
  QCheckBox* simplifyCheck;
  QSizeGrip* floatingSizeGrip;
  QTimer* refreshTimer;
  QString currentPath;
  QString fixedNamespace;
  QString fixedTitleOverride;
  RHGraph currentGraph;
  QString currentTitleOverride;
  bool hasCurrentGraph= false;
};

class ReferenceGraphPane: public QWidget {
public:
  ReferenceGraphPane (ads::CDockWidget** dockRef, bool depthSelectable,
                      const QString& defaultTitle, QWidget* parent = nullptr)
    : QWidget (parent), dockRef (dockRef),
      depthSelectable (depthSelectable),
      defaultTitle (defaultTitle),
      view (new ReverseHierarchyGraphView (new QGraphicsScene (), this)),
      zoomSlider (new QSlider (Qt::Horizontal, this)),
      zoomLabel (new QLabel ("100%", this)),
      depthSpin (new QSpinBox (this)),
      unlimitedCheck (new QCheckBox ("Unlimited", this)),
      followCheck (new QCheckBox ("Follow viewport", this)),
      statusLabel (new QLabel (this)), topologyPreview (this),
      topologyHost (new QWidget (this)),
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
    QPushButton* refresh= new QPushButton ("Refresh", this);

    zoomSlider->setRange (25, 300);
    zoomSlider->setValue (100);
    zoomSlider->setSingleStep (5);
    zoomSlider->setPageStep (25);
    zoomSlider->setFixedWidth (160);
    zoomLabel->setMinimumWidth (44);
    depthSpin->setRange (local_reference_depth, 99);
    depthSpin->setValue (depthSelectable ? default_reference_depth :
                                           local_reference_depth);
    depthSpin->setToolTip (
      "Maximum number of reference levels expanded from the current note");
    unlimitedCheck->setChecked (false);
    unlimitedCheck->setToolTip (
      "Follow references without a backtracking depth limit");
    followCheck->setChecked (true);
    followCheck->setToolTip (
      "Rebuild the graph when the active .ath note changes");
    statusLabel->setText ("Ready");
    topologyHost->setMinimumHeight (100);
    topologyHost->setMaximumHeight (150);
    topologyHost->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Preferred);
    QVBoxLayout* topologyLayout= new QVBoxLayout (topologyHost);
    topologyLayout->setContentsMargins (0, 0, 0, 0);

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
    connect (refresh, &QPushButton::clicked,
             this, [this] () { refreshFromCurrentDocument (); });
    connect (depthSpin, QOverload<int>::of (&QSpinBox::valueChanged),
             this, [this] (int) {
      if (this->depthSelectable) refreshFromCurrentDocument ();
    });
    connect (unlimitedCheck, &QCheckBox::toggled,
             this, [this] (bool unlimited) {
      depthSpin->setEnabled (!unlimited);
      if (this->depthSelectable) refreshFromCurrentDocument ();
    });
    connect (followCheck, &QCheckBox::toggled, this, [this] (bool checked) {
      if (checked) refreshFromCurrentDocument ();
    });

    QHBoxLayout* controls= new QHBoxLayout ();
    controls->setContentsMargins (4, 3, 4, 3);
    controls->addWidget (zoomOut);
    controls->addWidget (zoomSlider);
    controls->addWidget (zoomIn);
    controls->addWidget (zoomLabel);
    controls->addSpacing (8);
    controls->addWidget (reset);
    controls->addWidget (refresh);
    controls->addStretch ();
    controls->addWidget (followCheck);

    QWidget* depthControls= new QWidget (this);
    QHBoxLayout* depthLayout= new QHBoxLayout (depthControls);
    depthLayout->setContentsMargins (6, 0, 6, 2);
    QLabel* depthLabel= new QLabel ("Backtracking level:", this);
    depthLabel->setBuddy (depthSpin);
    depthLayout->addWidget (depthLabel);
    depthLayout->addWidget (depthSpin);
    depthLayout->addWidget (unlimitedCheck);
    depthLayout->addStretch ();
    depthControls->setVisible (depthSelectable);

    floatingSizeGrip->hide ();
    QHBoxLayout* statusRow= new QHBoxLayout ();
    statusRow->setContentsMargins (6, 2, 0, 0);
    statusRow->addWidget (statusLabel);
    statusRow->addStretch ();
    statusRow->addWidget (floatingSizeGrip, 0,
                          Qt::AlignRight | Qt::AlignBottom);

    QVBoxLayout* layout= new QVBoxLayout (this);
    layout->setContentsMargins (0, 0, 0, 0);
    layout->addLayout (controls);
    layout->addWidget (depthControls);
    layout->addWidget (view, 1);
    layout->addWidget (topologyHost);
    layout->addLayout (statusRow);

    QTimer::singleShot (0, this, [this] () {
      topologyPreview.ensureCreated (topologyHost);
      topologyPreview.refresh ();
    });

    refreshTimer->setInterval (700);
    connect (refreshTimer, &QTimer::timeout,
             this, [this] () { refreshIfActiveDocumentChanged (); });
    refreshTimer->start ();
  }

  QSize sizeHint () const override { return QSize (680, 560); }

  void setFloatingResizeGripVisible (bool visible) {
    floatingSizeGrip->setVisible (visible);
  }

  bool refreshFromCurrentDocument (QString* errorOut = nullptr) {
    if (refreshing) return false;
    refreshing= true;
    statusLabel->setText ("Checking reference cache...");
    QApplication::processEvents (QEventLoop::ExcludeUserInputEvents);

    RHGraph graph;
    QString error;
    size_t lastDone= 0;
    bool built= build_reference_graph (
      graph, selectedDepth (), !depthSelectable,
      [this, &lastDone] (size_t done, size_t total) {
        if (done == lastDone) return;
        lastDone= done;
        statusLabel->setText (
          QString ("Indexing .ath notes: %1 / %2").arg (done).arg (total));
        if (done == total || done % 25 == 0)
          QApplication::processEvents (QEventLoop::ExcludeUserInputEvents);
      }, error);
    if (built) built= layout_with_boost_force_directed (graph, error);

    currentIdentity= current_buffer_identity ();
    QString currentPath;
    currentModified= current_physical_file_path (currentPath) ?
      QFileInfo (currentPath).lastModified () : QDateTime ();
    if (!built) {
      if (errorOut != nullptr) *errorOut= error;
      showMessageScene (error);
      refreshing= false;
      return false;
    }

    currentGraph= graph;
    hasCurrentGraph= true;
    view->setOwnedScene (create_pane_scene (currentGraph, true));
    topologyPreview.ensureCreated (topologyHost);
    topologyPreview.setBody (graph_topology_summary_tree (currentGraph));
    topologyPreview.refresh ();
    QTimer::singleShot (0, view, [this] () { view->resetViewport (); });
    if (dockRef != nullptr && *dockRef != nullptr)
      (*dockRef)->setWindowTitle (graph.title);
    statusLabel->setText (
      QString ("%1 note(s), %2 reference(s)")
        .arg (graph.nodes.size ()).arg (graph.edges.size ()));
    refreshing= false;
    return true;
  }

  void interactionPreferenceChanged () {
    if (!hasCurrentGraph) return;
    view->setOwnedScene (create_pane_scene (currentGraph, true));
    QTimer::singleShot (0, view, [this] () { view->resetViewport (); });
  }

private:
  int selectedDepth () const {
    if (!depthSelectable) return local_reference_depth;
    return unlimitedCheck->isChecked () ? unlimited_reference_depth :
                                          depthSpin->value ();
  }

  void showMessageScene (const QString& message) {
    QGraphicsScene* scene= new QGraphicsScene ();
    scene->setBackgroundBrush (QColor ("#fbfbfb"));
    QGraphicsTextItem* text= scene->addText (message);
    text->setDefaultTextColor (QColor ("#884444"));
    text->setTextWidth (440);
    text->setPos (20, 20);
    scene->setSceneRect (0, 0, 520, 160);
    view->setOwnedScene (scene);
    hasCurrentGraph= false;
    topologyPreview.ensureCreated (topologyHost);
    topologyPreview.setBody (tree (DOCUMENT, ""));
    topologyPreview.refresh ();
    statusLabel->setText (message);
    if (dockRef != nullptr && *dockRef != nullptr)
      (*dockRef)->setWindowTitle (defaultTitle);
  }

  void refreshIfActiveDocumentChanged () {
    if (refreshing || !followCheck->isChecked ()) return;
    QString identity= current_buffer_identity ();
    QString path;
    QDateTime modified= current_physical_file_path (path) ?
      QFileInfo (path).lastModified () : QDateTime ();
    if (identity == currentIdentity && modified == currentModified) return;
    refreshFromCurrentDocument ();
  }

  ads::CDockWidget** dockRef;
  bool depthSelectable;
  QString defaultTitle;
  ReverseHierarchyGraphView* view;
  QSlider* zoomSlider;
  QLabel* zoomLabel;
  QSpinBox* depthSpin;
  QCheckBox* unlimitedCheck;
  QCheckBox* followCheck;
  QLabel* statusLabel;
  WikilinkPreview topologyPreview;
  QWidget* topologyHost;
  QSizeGrip* floatingSizeGrip;
  QTimer* refreshTimer;
  QString currentIdentity;
  QDateTime currentModified;
  RHGraph currentGraph;
  bool hasCurrentGraph= false;
  bool refreshing= false;
};

static bool
build_layout_graph (RHGraph& graph, QString& error) {
  QString warning;
  if (!build_current_reverse_hierarchy_graph (graph, warning)) {
    error= warning;
    return false;
  }
  if (!layout_with_boost_force_directed (graph, error)) return false;
  if (!warning.isEmpty ()) std_warning << from_qstring (warning) << LF;
  return true;
}

static void
show_error (const QString& error,
            const QString& title = "Hierarchy Graph") {
  QMessageBox::warning (QApplication::activeWindow (),
                        title, error);
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
hierarchy_graph_interactivity_changed () {
  if (reverse_hierarchy_graph_widget != nullptr)
    reverse_hierarchy_graph_widget->interactionPreferenceChanged ();
  if (direct_hierarchy_graph_widget != nullptr)
    direct_hierarchy_graph_widget->interactionPreferenceChanged ();
  if (global_hierarchy_graph_widget != nullptr)
    global_hierarchy_graph_widget->interactionPreferenceChanged ();
  if (local_reference_graph_widget != nullptr)
    local_reference_graph_widget->interactionPreferenceChanged ();
  if (reference_graph_widget != nullptr)
    reference_graph_widget->interactionPreferenceChanged ();
}

void
reverse_hierarchy_graph_show () {
  if (qt_defer_to_main_thread (reverse_hierarchy_graph_show)) return;
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
    reverse_hierarchy_graph_dock->setWidget (
      reverse_hierarchy_graph_widget, ads::CDockWidget::ForceNoScrollArea);
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
    win->dockManager ()->addDockWidgetFloating (reverse_hierarchy_graph_dock);
    reverse_hierarchy_graph_dock->toggleView (true);
    reverse_hierarchy_graph_dock->show ();
    reverse_hierarchy_graph_dock->raise ();
  }
  else {
    if (reverse_hierarchy_graph_dock->widget () != reverse_hierarchy_graph_widget)
      reverse_hierarchy_graph_dock->setWidget (
        reverse_hierarchy_graph_widget, ads::CDockWidget::ForceNoScrollArea);
    win->showAdsDockWidget (reverse_hierarchy_graph_dock,
                            ads::RightDockWidgetArea);
  }
  reverse_hierarchy_graph_widget->setFloatingResizeGripVisible (
    reverse_hierarchy_graph_dock->isInFloatingContainer ());

  QString error;
  if (!reverse_hierarchy_graph_widget->refreshFromCurrentDocument (&error))
    show_error (error, "Reverse Hierarchy Graph");
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

static bool
ensure_reference_graph_pane (
  bool depthSelectable, ads::CDockWidget*& dock, ReferenceGraphPane*& widget,
  const QString& title, const QString& objectName, QString& error)
{
  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr || win->dockManager () == nullptr) {
    error= "No active ATHENA window.";
    return false;
  }

  ads::CDockWidget** dockSlot= &dock;
  ReferenceGraphPane** widgetSlot= &widget;
  if (widget == nullptr) {
    widget= new ReferenceGraphPane (dockSlot, depthSelectable, title);
    widget->resize (680, 560);
    QObject::connect (widget, &QObject::destroyed,
                      [dockSlot, widgetSlot] () {
      *widgetSlot= nullptr;
      *dockSlot= nullptr;
    });
  }

  if (dock == nullptr) {
    dock= new ads::CDockWidget (title);
    dock->setObjectName (objectName);
    dock->resize (700, 600);
    dock->setWidget (widget, ads::CDockWidget::ForceNoScrollArea);
    dock->setFeature (ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QObject::connect (dock, &ads::CDockWidget::topLevelChanged, widget,
                      [widgetSlot] (bool topLevel) {
      if (*widgetSlot != nullptr)
        (*widgetSlot)->setFloatingResizeGripVisible (topLevel);
    });
    QObject::connect (dock, &QObject::destroyed,
                      [dockSlot, widgetSlot] () {
      *dockSlot= nullptr;
      *widgetSlot= nullptr;
    });
    win->dockManager ()->addDockWidgetFloating (dock);
    dock->toggleView (true);
    dock->show ();
    dock->raise ();
  }
  else {
    if (dock->widget () != widget)
      dock->setWidget (widget, ads::CDockWidget::ForceNoScrollArea);
    win->showAdsDockWidget (dock, ads::RightDockWidgetArea);
  }
  widget->setFloatingResizeGripVisible (dock->isInFloatingContainer ());
  return true;
}

static bool
ensure_direct_hierarchy_graph_pane (QString& error) {
  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr || win->dockManager () == nullptr) {
    error= "No active ATHENA window.";
    return false;
  }

  if (direct_hierarchy_graph_widget == nullptr) {
    direct_hierarchy_graph_widget= new DirectHierarchyGraphPane (
      &direct_hierarchy_graph_dock, "Direct Hierarchy Graph", true);
    direct_hierarchy_graph_widget->resize (620, 520);
    QObject::connect (direct_hierarchy_graph_widget, &QObject::destroyed, [] () {
      direct_hierarchy_graph_widget= nullptr;
      direct_hierarchy_graph_dock= nullptr;
    });
  }

  if (direct_hierarchy_graph_dock == nullptr) {
    direct_hierarchy_graph_dock= new ads::CDockWidget (
      "Direct Hierarchy Graph");
    direct_hierarchy_graph_dock->setObjectName (
      "athena-direct-hierarchy-graph");
    direct_hierarchy_graph_dock->resize (640, 560);
    direct_hierarchy_graph_dock->setWidget (
      direct_hierarchy_graph_widget, ads::CDockWidget::ForceNoScrollArea);
    direct_hierarchy_graph_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QObject::connect (direct_hierarchy_graph_dock,
                      &ads::CDockWidget::topLevelChanged,
                      direct_hierarchy_graph_widget,
                      [] (bool topLevel) {
                        if (direct_hierarchy_graph_widget != nullptr)
                          direct_hierarchy_graph_widget->
                            setFloatingResizeGripVisible (topLevel);
                      });
    QObject::connect (direct_hierarchy_graph_dock, &QObject::destroyed, [] () {
      direct_hierarchy_graph_dock= nullptr;
      direct_hierarchy_graph_widget= nullptr;
    });
    win->dockManager ()->addDockWidgetFloating (direct_hierarchy_graph_dock);
    direct_hierarchy_graph_dock->toggleView (true);
    direct_hierarchy_graph_dock->show ();
    direct_hierarchy_graph_dock->raise ();
  }
  else {
    if (direct_hierarchy_graph_dock->widget () != direct_hierarchy_graph_widget)
      direct_hierarchy_graph_dock->setWidget (
        direct_hierarchy_graph_widget, ads::CDockWidget::ForceNoScrollArea);
    win->showAdsDockWidget (direct_hierarchy_graph_dock,
                            ads::RightDockWidgetArea);
  }
  direct_hierarchy_graph_widget->setFloatingResizeGripVisible (
    direct_hierarchy_graph_dock->isInFloatingContainer ());
  return true;
}

static bool
ensure_global_hierarchy_graph_pane (QString& error) {
  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr || win->dockManager () == nullptr) {
    error= "No active ATHENA window.";
    return false;
  }

  if (global_hierarchy_graph_widget == nullptr) {
    global_hierarchy_graph_widget= new DirectHierarchyGraphPane (
      &global_hierarchy_graph_dock, "Global Hierarchy Graph", false);
    global_hierarchy_graph_widget->resize (620, 520);
    QObject::connect (global_hierarchy_graph_widget, &QObject::destroyed, [] () {
      global_hierarchy_graph_widget= nullptr;
      global_hierarchy_graph_dock= nullptr;
    });
  }

  if (global_hierarchy_graph_dock == nullptr) {
    global_hierarchy_graph_dock= new ads::CDockWidget (
      "Global Hierarchy Graph");
    global_hierarchy_graph_dock->setObjectName (
      "athena-global-hierarchy-graph");
    global_hierarchy_graph_dock->resize (640, 560);
    global_hierarchy_graph_dock->setWidget (
      global_hierarchy_graph_widget, ads::CDockWidget::ForceNoScrollArea);
    global_hierarchy_graph_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QObject::connect (global_hierarchy_graph_dock,
                      &ads::CDockWidget::topLevelChanged,
                      global_hierarchy_graph_widget,
                      [] (bool topLevel) {
                        if (global_hierarchy_graph_widget != nullptr)
                          global_hierarchy_graph_widget->
                            setFloatingResizeGripVisible (topLevel);
                      });
    QObject::connect (global_hierarchy_graph_dock, &QObject::destroyed, [] () {
      global_hierarchy_graph_dock= nullptr;
      global_hierarchy_graph_widget= nullptr;
    });
    win->dockManager ()->addDockWidgetFloating (global_hierarchy_graph_dock);
    global_hierarchy_graph_dock->toggleView (true);
    global_hierarchy_graph_dock->show ();
    global_hierarchy_graph_dock->raise ();
  }
  else {
    if (global_hierarchy_graph_dock->widget () !=
        global_hierarchy_graph_widget)
      global_hierarchy_graph_dock->setWidget (
        global_hierarchy_graph_widget, ads::CDockWidget::ForceNoScrollArea);
    win->showAdsDockWidget (global_hierarchy_graph_dock,
                            ads::RightDockWidgetArea);
  }
  global_hierarchy_graph_widget->setFloatingResizeGripVisible (
    global_hierarchy_graph_dock->isInFloatingContainer ());
  return true;
}

void
direct_hierarchy_graph_show () {
  if (qt_defer_to_main_thread (direct_hierarchy_graph_show)) return;
  QString error;
  if (!ensure_direct_hierarchy_graph_pane (error)) {
    show_error (error, "Direct Hierarchy Graph");
    return;
  }
  direct_hierarchy_graph_widget->refreshFromCurrentDocument ();
}

void
direct_hierarchy_graph_show_namespace (string name) {
  if (qt_defer_to_main_thread (direct_hierarchy_graph_show_namespace, name))
    return;
  QString error;
  if (!ensure_direct_hierarchy_graph_pane (error)) {
    show_error (error, "Direct Hierarchy Graph");
    return;
  }
  direct_hierarchy_graph_widget->refreshFromNamespace (to_qstring (name));
}

void
global_hierarchy_graph_show () {
  if (qt_defer_to_main_thread (global_hierarchy_graph_show)) return;
  QString error;
  if (!ensure_global_hierarchy_graph_pane (error)) {
    show_error (error, "Global Hierarchy Graph");
    return;
  }

  QTMVaultfileInfo info;
  if (!qtm_vaultfile_read (info) || info.rootNamespace.trimmed ().isEmpty ()) {
    global_hierarchy_graph_widget->showMessage (
      "Set a root namespace in Preferences / Vault / Vault Info first.");
    return;
  }

  QString rootNamespace= info.rootNamespace.trimmed ();
  if (!global_hierarchy_graph_widget->refreshFromNamespace (
        rootNamespace, &error, "Global Hierarchy Graph - " + rootNamespace)) {
    show_error (error);
    return;
  }
}

void
local_reference_graph_show () {
  if (qt_defer_to_main_thread (local_reference_graph_show)) return;
  QString error;
  if (!ensure_reference_graph_pane (
        false, local_reference_graph_dock, local_reference_graph_widget,
        "Local Reference Graph", "athena-local-reference-graph", error)) {
    show_error (error, "Local Reference Graph");
    return;
  }
  if (!local_reference_graph_widget->refreshFromCurrentDocument (&error))
    show_error (error, "Local Reference Graph");
}

void
reference_graph_show () {
  if (qt_defer_to_main_thread (reference_graph_show)) return;
  QString error;
  if (!ensure_reference_graph_pane (
        true, reference_graph_dock, reference_graph_widget,
        "Reference Graph", "athena-reference-graph", error)) {
    show_error (error, "Reference Graph");
    return;
  }
  if (!reference_graph_widget->refreshFromCurrentDocument (&error))
    show_error (error, "Reference Graph");
}
