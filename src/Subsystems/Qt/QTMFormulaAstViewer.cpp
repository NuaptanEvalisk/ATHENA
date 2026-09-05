/******************************************************************************
* MODULE     : QTMFormulaAstViewer.cpp
* DESCRIPTION: Generic tree graph viewer and formula AST entry point
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMFormulaAstViewer.hpp"

#include "QTMMainTabWindow.hpp"
#include "editor.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"
#include "new_view.hpp"

#include <DockWidget.h>
#include <QApplication>
#include <QFontMetricsF>
#include <QGraphicsLineItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace {

constexpr double horizontalGap= 28.0;
constexpr double verticalGap= 78.0;
constexpr double nodePaddingX= 12.0;
constexpr double nodePaddingY= 8.0;
constexpr double pi= 3.14159265358979323846;

struct AstNode {
  tree value;
  QString path;
  QString title;
  QString detail;
  std::vector<int> children;
  QSizeF size;
  double subtreeWidth= 0.0;
  QPointF center;
  int depth= 0;
};

static ads::CDockWidget* astViewerDock= nullptr;

static QString
qstring (string value) {
  return QString::fromUtf8 (as_charp (value), N(value));
}

static QString
shorten (QString text, int limit= 72) {
  text.replace ('\n', QChar (0x21b5));
  if (text.size () <= limit) return text;
  return text.left (limit - 1) + QChar (0x2026);
}

class AstGraphView: public QGraphicsView {
public:
  explicit AstGraphView (QWidget* parent= nullptr)
    : QGraphicsView (parent) {
    setRenderHints (QPainter::Antialiasing | QPainter::TextAntialiasing);
    setTransformationAnchor (QGraphicsView::AnchorUnderMouse);
    setResizeAnchor (QGraphicsView::AnchorViewCenter);
    setDragMode (QGraphicsView::NoDrag);
    setCursor (Qt::OpenHandCursor);
  }

  void setZoomCallback (std::function<void(int)> callback) {
    zoomCallback= std::move (callback);
  }

  void setZoomPercent (int value) {
    zoomPercent= std::clamp (value, 20, 300);
    resetTransform ();
    const double factor= zoomPercent / 100.0;
    scale (factor, factor);
    if (zoomCallback) zoomCallback (zoomPercent);
  }

  void resetViewport () {
    if (scene () == nullptr || scene ()->items ().isEmpty ()) return;
    resetTransform ();
    fitInView (scene ()->sceneRect (), Qt::KeepAspectRatio);
    const double factor= transform ().m11 ();
    zoomPercent= std::clamp ((int) std::round (factor * 100.0), 20, 300);
    if (zoomCallback) zoomCallback (zoomPercent);
  }

protected:
  void wheelEvent (QWheelEvent* event) override {
    const double factor= event->angleDelta ().y () > 0 ? 1.15 : 1.0 / 1.15;
    scale (factor, factor);
    zoomPercent= std::clamp (
      (int) std::round (zoomPercent * factor), 20, 300);
    if (zoomCallback) zoomCallback (zoomPercent);
    event->accept ();
  }

  void mousePressEvent (QMouseEvent* event) override {
    if (event->button () == Qt::LeftButton ||
        event->button () == Qt::MiddleButton) {
      panning= true;
      lastPosition= event->pos ();
      setCursor (Qt::ClosedHandCursor);
      event->accept ();
      return;
    }
    QGraphicsView::mousePressEvent (event);
  }

  void mouseMoveEvent (QMouseEvent* event) override {
    if (!panning) {
      QGraphicsView::mouseMoveEvent (event);
      return;
    }
    const QPoint delta= event->pos () - lastPosition;
    lastPosition= event->pos ();
    horizontalScrollBar ()->setValue (
      horizontalScrollBar ()->value () - delta.x ());
    verticalScrollBar ()->setValue (
      verticalScrollBar ()->value () - delta.y ());
    event->accept ();
  }

  void mouseReleaseEvent (QMouseEvent* event) override {
    if (panning && (event->button () == Qt::LeftButton ||
                    event->button () == Qt::MiddleButton)) {
      panning= false;
      setCursor (Qt::OpenHandCursor);
      event->accept ();
      return;
    }
    QGraphicsView::mouseReleaseEvent (event);
  }

private:
  bool panning= false;
  int zoomPercent= 100;
  QPoint lastPosition;
  std::function<void(int)> zoomCallback;
};

class AstViewerPane: public QWidget {
public:
  explicit AstViewerPane (QWidget* parent= nullptr)
    : QWidget (parent), view (new AstGraphView (this)),
      zoomSlider (new QSlider (Qt::Horizontal, this)),
      zoomLabel (new QLabel ("100%", this)),
      summary (new QLabel (this)) {
    QToolButton* zoomOut= new QToolButton (this);
    zoomOut->setIcon (style ()->standardIcon (QStyle::SP_ArrowDown));
    zoomOut->setToolTip ("Zoom out");
    QToolButton* zoomIn= new QToolButton (this);
    zoomIn->setIcon (style ()->standardIcon (QStyle::SP_ArrowUp));
    zoomIn->setToolTip ("Zoom in");
    QToolButton* reset= new QToolButton (this);
    reset->setIcon (style ()->standardIcon (QStyle::SP_BrowserReload));
    reset->setToolTip ("Fit AST to viewport");

    zoomSlider->setRange (20, 300);
    zoomSlider->setValue (100);
    zoomSlider->setSingleStep (5);
    zoomSlider->setPageStep (25);
    zoomSlider->setFixedWidth (160);
    zoomLabel->setMinimumWidth (48);

    view->setZoomCallback ([this] (int percent) {
      const QSignalBlocker blocker (zoomSlider);
      zoomSlider->setValue (percent);
      zoomLabel->setText (QString::number (percent) + "%");
    });
    connect (zoomOut, &QToolButton::clicked, this, [this] () {
      view->setZoomPercent (zoomSlider->value () - 10);
    });
    connect (zoomIn, &QToolButton::clicked, this, [this] () {
      view->setZoomPercent (zoomSlider->value () + 10);
    });
    connect (reset, &QToolButton::clicked,
             view, &AstGraphView::resetViewport);
    connect (zoomSlider, &QSlider::valueChanged,
             view, &AstGraphView::setZoomPercent);

    QHBoxLayout* controls= new QHBoxLayout ();
    controls->setContentsMargins (4, 3, 4, 3);
    controls->addWidget (zoomOut);
    controls->addWidget (zoomSlider);
    controls->addWidget (zoomIn);
    controls->addWidget (zoomLabel);
    controls->addSpacing (8);
    controls->addWidget (reset);
    controls->addStretch (1);
    controls->addWidget (summary);

    QVBoxLayout* layout= new QVBoxLayout (this);
    layout->setContentsMargins (0, 0, 0, 0);
    layout->addLayout (controls);
    layout->addWidget (view, 1);
  }

  QSize sizeHint () const override { return QSize (760, 600); }

  void setTree (tree value) {
    std::vector<AstNode> nodes;
    appendNode (nodes, value, QString (), 0);
    calculateSizes (nodes);
    positionTree (nodes, 0, 20.0);
    QGraphicsScene* scene= makeScene (nodes);
    QGraphicsScene* old= view->scene ();
    view->setScene (scene);
    scene->setParent (view);
    if (old != nullptr) old->deleteLater ();
    summary->setText (QString ("%1 node(s)").arg (nodes.size ()));
    QTimer::singleShot (0, view, &AstGraphView::resetViewport);
  }

private:
  static int appendNode (std::vector<AstNode>& nodes, tree value,
                         const QString& path, int depth) {
    AstNode node;
    node.value= value;
    node.path= path.isEmpty () ? "root" : path;
    node.depth= depth;
    if (is_atomic (value)) {
      node.title= "text";
      node.detail= shorten (qstring (value->label));
    }
    else {
      node.title= qstring (as_string (L(value)));
      node.detail= QString ("arity %1").arg (N(value));
    }
    const int index= (int) nodes.size ();
    nodes.push_back (node);
    if (is_compound (value)) {
      for (int i=0; i<N(value); i++) {
        const QString childPath= path.isEmpty () ? QString::number (i):
          path + "." + QString::number (i);
        const int child= appendNode (nodes, value[i], childPath, depth + 1);
        nodes[index].children.push_back (child);
      }
    }
    return index;
  }

  static void calculateSizes (std::vector<AstNode>& nodes) {
    QFont titleFont= QApplication::font ();
    titleFont.setBold (true);
    QFont detailFont= QApplication::font ();
    QFontMetricsF titleMetrics (titleFont);
    QFontMetricsF detailMetrics (detailFont);
    for (AstNode& node: nodes) {
      node.detail= detailMetrics.elidedText (node.detail, Qt::ElideRight,
                                             336.0);
      const double width= std::min (360.0, std::max (
        titleMetrics.horizontalAdvance (node.title),
        detailMetrics.horizontalAdvance (node.detail)) + 2 * nodePaddingX);
      const double height= titleMetrics.height () + detailMetrics.height () +
                           2 * nodePaddingY + 2.0;
      node.size= QSizeF (std::max (80.0, width), height);
    }
    std::function<double(int)> measure= [&] (int index) {
      AstNode& node= nodes[index];
      double childrenWidth= 0.0;
      for (int child: node.children) {
        if (childrenWidth > 0.0) childrenWidth += horizontalGap;
        childrenWidth += measure (child);
      }
      node.subtreeWidth= std::max (node.size.width (), childrenWidth);
      return node.subtreeWidth;
    };
    measure (0);
  }

  static void positionTree (std::vector<AstNode>& nodes, int index,
                            double left) {
    AstNode& node= nodes[index];
    node.center= QPointF (left + node.subtreeWidth / 2.0,
                          30.0 + node.depth * verticalGap);
    if (node.children.empty ()) return;
    double childrenWidth= 0.0;
    for (int child: node.children) {
      if (childrenWidth > 0.0) childrenWidth += horizontalGap;
      childrenWidth += nodes[child].subtreeWidth;
    }
    double childLeft= left + (node.subtreeWidth - childrenWidth) / 2.0;
    for (int child: node.children) {
      positionTree (nodes, child, childLeft);
      childLeft += nodes[child].subtreeWidth + horizontalGap;
    }
  }

  static void addArrow (QGraphicsScene* scene, const QPointF& from,
                        const QPointF& to) {
    QPen pen (QColor ("#6f7782"));
    pen.setWidthF (1.4);
    QGraphicsLineItem* line= scene->addLine (QLineF (from, to), pen);
    line->setZValue (-2.0);
    const QLineF direction (from, to);
    const double angle= std::atan2 (-direction.dy (), direction.dx ());
    const double arrowSize= 7.0;
    QPolygonF arrow;
    arrow << to
          << to - QPointF (std::sin (angle + pi / 3.0) * arrowSize,
                           std::cos (angle + pi / 3.0) * arrowSize)
          << to - QPointF (std::sin (angle + pi - pi / 3.0) * arrowSize,
                           std::cos (angle + pi - pi / 3.0) * arrowSize);
    QGraphicsPolygonItem* head= scene->addPolygon (arrow, pen,
                                                   QBrush (pen.color ()));
    head->setZValue (-1.0);
  }

  static QGraphicsScene* makeScene (const std::vector<AstNode>& nodes) {
    QGraphicsScene* scene= new QGraphicsScene ();
    scene->setBackgroundBrush (QApplication::palette ().base ());
    for (const AstNode& parent: nodes) {
      for (int childIndex: parent.children) {
        const AstNode& child= nodes[childIndex];
        const QPointF from (parent.center.x (),
          parent.center.y () + parent.size.height () / 2.0);
        const QPointF to (child.center.x (),
          child.center.y () - child.size.height () / 2.0);
        addArrow (scene, from, to);
      }
    }

    QFont titleFont= QApplication::font ();
    titleFont.setBold (true);
    for (const AstNode& node: nodes) {
      const QRectF rect (node.center.x () - node.size.width () / 2.0,
                         node.center.y () - node.size.height () / 2.0,
                         node.size.width (), node.size.height ());
      QGraphicsRectItem* box= scene->addRect (
        rect, QPen (QColor ("#556273"), 1.2), QBrush (QColor ("#f7f9fc")));
      box->setToolTip (QString ("Path: %1\nNode: %2\n%3")
                         .arg (node.path, node.title, node.detail));
      box->setZValue (1.0);

      QGraphicsTextItem* title= scene->addText (node.title, titleFont);
      title->setDefaultTextColor (QColor ("#1f2933"));
      title->setPos (rect.left () + nodePaddingX,
                     rect.top () + nodePaddingY - 2.0);
      title->setZValue (2.0);
      QGraphicsTextItem* detail= scene->addText (node.detail);
      detail->setDefaultTextColor (QColor ("#52606d"));
      detail->setPos (rect.left () + nodePaddingX,
        rect.top () + nodePaddingY + title->boundingRect ().height () - 4.0);
      detail->setZValue (2.0);
    }
    scene->setSceneRect (scene->itemsBoundingRect ().adjusted (-24, -24, 24, 24));
    return scene;
  }

  AstGraphView* view;
  QSlider* zoomSlider;
  QLabel* zoomLabel;
  QLabel* summary;
};

static AstViewerPane* astViewerPane= nullptr;

static void
showError (const QString& message) {
  QMessageBox::warning (QApplication::activeWindow (), "Formula AST", message);
}

} // namespace

void
ast_viewer_show_tree (tree value, string title) {
  if (qApp != nullptr && QThread::currentThread () != qApp->thread ()) {
    title.ensure_transferable ();
    qt_post_to_main_thread (
      [value= copy (value), title= std::move (title)] () mutable {
        ast_viewer_show_tree (std::move (value), std::move (title));
      });
    return;
  }
  QTMMainTabWindow* window= QTMMainTabWindow::topTabWindow ();
  if (window == nullptr || window->dockManager () == nullptr) {
    showError ("No active ATHENA window.");
    return;
  }
  if (astViewerPane == nullptr) astViewerPane= new AstViewerPane ();
  astViewerPane->setTree (value);

  const QString windowTitle= qstring (title);
  if (astViewerDock == nullptr) {
    astViewerDock= new ads::CDockWidget (windowTitle);
    astViewerDock->setObjectName ("athena-formula-ast-viewer");
    astViewerDock->setWidget (astViewerPane,
                              ads::CDockWidget::ForceNoScrollArea);
    astViewerDock->setFeature (ads::CDockWidget::DockWidgetDeleteOnClose,
                               false);
    QObject::connect (astViewerDock, &QObject::destroyed, [] () {
      astViewerDock= nullptr;
      astViewerPane= nullptr;
    });
    window->dockManager ()->addDockWidgetFloating (astViewerDock);
  }
  else {
    astViewerDock->setWindowTitle (windowTitle);
    if (astViewerDock->widget () != astViewerPane)
      astViewerDock->setWidget (astViewerPane,
                                ads::CDockWidget::ForceNoScrollArea);
    window->showAdsDockWidget (astViewerDock, ads::RightDockWidgetArea);
  }
  astViewerDock->toggleView (true);
  astViewerDock->show ();
  astViewerDock->raise ();
}

void
formula_ast_show () {
  if (qApp != nullptr && QThread::currentThread () == qApp->thread ()) {
    if (!has_current_view ()) {
      showError ("No active document editor.");
      return;
    }
    exec_delayed (scheme_cmd ("(formula-ast-show)"));
    return;
  }
  editor ed= get_current_editor ();
  if (is_nil (ed)) {
    qt_post_to_main_thread ([] { showError ("No active document editor."); });
    return;
  }
  const path cursor= ed->the_path ();
  const path root= ed->semantic_root (cursor);
  if (!ed->test_subtree (root)) {
    qt_post_to_main_thread ([] {
      showError ("The current formula could not be resolved.");
    });
    return;
  }
  ast_viewer_show_tree (ed->the_subtree (root), "Formula AST");
}
