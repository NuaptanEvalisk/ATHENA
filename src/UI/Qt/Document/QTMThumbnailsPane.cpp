/******************************************************************************
* MODULE     : QTMThumbnailsPane.cpp
* DESCRIPTION: Nonblocking page thumbnails for paginated presentation modes
******************************************************************************/
#include "UI/Qt/Document/QTMThumbnailsPane.hpp"
#include "UI/Qt/Document/QTMDocumentIdentity.hpp"
#include "UI/Qt/Shell/QTMMainTabWindow.hpp"
#include "UI/Qt/Shell/QTMWindow.hpp"
#include "UI/Qt/Widgets/QTMWidget.hpp"
#include "UI/Qt/Toolkit/qt_actor_widget.hpp"
#include "UI/Qt/Toolkit/qt_utilities.hpp"
#include "App/Runtime/buffer_actor.hpp"
#include "renderer.hpp"

#include <DockWidget.h>
#include <QApplication>
#include <QHideEvent>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace {
QTMThumbnailsPane* pane_widget= nullptr;
ads::CDockWidget* pane_dock= nullptr;

bool paginated (const std::string& mode) {
  return mode == "paper" || mode == "beamer" || mode == "slideshow" ||
         mode == "book" || mode == "panorama";
}

qt_actor_widget_rep* canvas_proxy (QTMMainTabWindow* shell) {
  if (!shell) return nullptr;
  QWidget* pane= shell->lastActiveDocumentWidget ();
  QTMWidget* canvas= qobject_cast<QTMWidget*> (pane);
  if (!canvas)
    if (auto* document= qobject_cast<QTMWindow*> (pane))
      canvas= document->editorCanvas ();
  return canvas ? dynamic_cast<qt_actor_widget_rep*> (canvas->tm_widget ()) : nullptr;
}
} // namespace

QTMThumbnailsPane::QTMThumbnailsPane (QWidget* parent): QWidget (parent),
    list (new QListWidget (this)), message (new QLabel (this)),
    timer (new QTimer (this)) {
  setObjectName ("athenaThumbnailsContent");
  auto* layout= new QVBoxLayout (this);
  layout->setContentsMargins (4, 4, 4, 4);
  message->setAlignment (Qt::AlignCenter);
  message->setWordWrap (true);
  message->setText (tr ("Switch to Paper, Slideshow, Book, or Panorama "
                        "to view page thumbnails."));
  layout->addWidget (message);
  list->setObjectName ("athenaThumbnailsList");
  list->setViewMode (QListView::IconMode);
  list->setFlow (QListView::TopToBottom);
  list->setWrapping (false);
  list->setMovement (QListView::Static);
  list->setResizeMode (QListView::Adjust);
  list->setIconSize (QSize (190, 270));
  list->setGridSize (QSize (224, 304));
  list->setSpacing (3);
  list->setSelectionMode (QAbstractItemView::SingleSelection);
  list->hide ();
  layout->addWidget (list, 1);
  connect (list, &QListWidget::itemClicked, this,
           [this] (QListWidgetItem* item) {
    activatePage (list->row (item));
  });
  connect (list->verticalScrollBar (), &QScrollBar::valueChanged,
           this, [this] { updateVisibleThumbnails (); });
  timer->setInterval (300);
  connect (timer, &QTimer::timeout, this, [this] { refresh (); });
  timer->start ();
}

QSize QTMThumbnailsPane::sizeHint () const { return QSize (270, 600); }

void
QTMThumbnailsPane::hideEvent (QHideEvent* event) {
  // A dock hidden by ADS must stop producing raster work immediately. The
  // existing tiny cached images remain available when the pane is reopened.
  if (raster) raster->retire ();
  raster.reset ();
  requestedPage= -1;
  QWidget::hideEvent (event);
}

void
QTMThumbnailsPane::resetDocument (athena_actor_id next, athena_view_id nextView) {
  if (raster) raster->retire ();
  raster.reset ();
  actor= next;
  view= nextView;
  signature= 0;
  styleSignature= 0;
  contentSignature= 0;
  slideshow= false;
  activeSlide= -1;
  requestedPage= -1;
  layoutSettled.invalidate ();
  pages.clear ();
  visibleIcons.clear ();
  highlightedPage= -1;
  cache.clear ();
  list->clear ();
}

void
QTMThumbnailsPane::acceptManifest (athena_view_id target, owned_actor_blob blob) {
  if (!blob || target != view ||
      blob.size () < sizeof (thumbnail_manifest_header)) return;
  thumbnail_manifest_header header;
  std::memcpy (&header, blob.data (), sizeof (header));
  if (header.count > (blob.size () - sizeof (header)) /
                       sizeof (thumbnail_page_bounds)) return;
  if (header.signature == signature) return;
  const bool slides= (header.flags & THUMBNAIL_MANIFEST_SLIDES) != 0;
  const bool sameEntries= slideshow == slides &&
    pages.size () == int (header.count);
  const bool preserveVisited= slideshow && slides && sameEntries &&
    styleSignature == header.style_signature &&
    contentSignature == header.content_signature;
  signature= header.signature;
  styleSignature= header.style_signature;
  contentSignature= header.content_signature;
  slideshow= slides;
  activeSlide= slides ? int (header.active_page) : -1;
  requestedPage= -1;
  layoutSettled.start ();
  if (!preserveVisited) {
    cache.clear ();
  }
  else cache.remove (activeSlide);
  // Only visible rows own icon pixmaps; invalidating those does not require
  // rebuilding every QListWidgetItem or resetting the user's scroll position.
  for (int i: visibleIcons)
    if (i >= 0 && i < list->count () &&
        (!preserveVisited || i == activeSlide))
      list->item (i)->setIcon (QIcon ());
  pages.resize (int (header.count));
  if (header.count)
    std::memcpy (pages.data (), blob.data () + sizeof (header),
                 std::size_t (header.count) * sizeof (thumbnail_page_bounds));
  if (!sameEntries) {
    list->clear ();
    visibleIcons.clear ();
    highlightedPage= -1;
    for (int i=0; i<pages.size (); ++i) {
      auto* item= new QListWidgetItem (
        slides ? tr ("Slide %1").arg (i+1) : tr ("Page %1").arg (i+1));
      item->setTextAlignment (Qt::AlignHCenter);
      item->setSizeHint (QSize (220, 300));
      list->addItem (item);
    }
  }
  if (available) {
    message->setVisible (pages.isEmpty ());
    list->setVisible (!pages.isEmpty ());
  }
  message->setText (pages.isEmpty () ? tr ("Waiting for paginated layout…") : QString ());
  updateVisibleThumbnails ();
}

void
QTMThumbnailsPane::highlightViewportPage (
    const actor_viewport_snapshot& viewport, double zoom) {
  if (pages.isEmpty () || !std::isfinite (zoom) || zoom <= 0) return;
  const double mag= zoom / std_shrinkf;
  const double x= (double (viewport.visible_x1) + viewport.visible_x2) /
                  (2.0 * mag);
  const double y= (double (viewport.visible_y1) + viewport.visible_y2) /
                  (2.0 * mag);
  int best= -1;
  double distance= std::numeric_limits<double>::infinity ();
  for (int i=0; i<pages.size (); ++i) {
    const auto& page= pages[i];
    const double dx= x < page.x1 ? page.x1-x :
                     x > page.x2 ? x-page.x2 : 0;
    const double dy= y < page.y1 ? page.y1-y :
                     y > page.y2 ? y-page.y2 : 0;
    const double score= dx*dx + dy*dy;
    if (score < distance) { distance= score; best= i; }
  }
  if (best != highlightedPage) {
    if (highlightedPage >= 0 && highlightedPage < list->count ())
      list->item (highlightedPage)->setSelected (false);
    highlightedPage= best;
    if (best >= 0) {
      list->item (best)->setSelected (true);
      list->scrollToItem (list->item (best), QAbstractItemView::EnsureVisible);
    }
  }
}

void
QTMThumbnailsPane::updateVisibleThumbnails () {
  if (!available || pages.isEmpty () || !list->isVisible ()) return;
  const int top= -320;
  const int bottom= list->viewport ()->height () + 320;
  // Items have fixed grid geometry and are ordered vertically. Find the first
  // nearby row in O(log pages), instead of visiting every page on each tick.
  int lo= 0, hi= pages.size ();
  while (lo < hi) {
    const int mid= lo + (hi-lo)/2;
    if (list->visualItemRect (list->item (mid)).bottom () < top)
      lo= mid + 1;
    else hi= mid;
  }
  QSet<int> nowVisible;
  int next= -1;
  for (int i=lo; i<pages.size (); ++i) {
    auto* item= list->item (i);
    if (list->visualItemRect (item).top () > bottom) break;
    nowVisible.insert (i);
    auto image= cache.constFind (i);
    if (image != cache.cend ()) {
      if (item->icon ().isNull ()) item->setIcon (QPixmap::fromImage (*image));
    }
    else if (next < 0 && i != requestedPage &&
             pages[i].x2 > pages[i].x1 && pages[i].y2 > pages[i].y1)
      next= i;
  }
  for (int i: visibleIcons)
    if (!nowVisible.contains (i)) list->item (i)->setIcon (QIcon ());
  visibleIcons= std::move (nowVisible);
  if (next < 0) {
    // Populate never-visited paper pages as well as slides. Visible rows take
    // priority; only one additional page is queued at a time. Keep the
    // completed images in memory for fast switching back to any page.
    for (int i=0; i<pages.size (); ++i)
      if (!cache.contains (i) &&
          pages[i].x2 > pages[i].x1 && pages[i].y2 > pages[i].y1) {
        next= i;
        break;
      }
  }
  // Source edits can produce consecutive new layouts while the user types.
  // Do not enqueue page recording behind interactive edits until the current
  // layout has remained stable; rasterization itself stays on RenderService.
  if (requestedPage < 0 && next >= 0 && raster &&
      (!layoutSettled.isValid () || layoutSettled.elapsed () >= 500)) {
    if (buffer_actor::try_submit_to (
          actor, actor_command_kind::request_thumbnail_raster, view,
          ATHENA_NO_BLOB, ATHENA_NO_BLOB, EXECUTION_CAPABILITY_BUFFER,
          static_cast<std::uint64_t> (next), signature, raster->id ())) {
      requestedPage= next;
      requestAge.start ();
    }
  }
}

void
QTMThumbnailsPane::refresh () {
  if (!isVisible ()) return;
  QTMMainTabWindow* shell= QTMMainTabWindow::topTabWindow ();
  const auto identity= qtm_last_active_document_identity (shell);
  if (!identity.has_view ()) {
    if (actor != ATHENA_NO_ACTOR) resetDocument (ATHENA_NO_ACTOR, ATHENA_NO_VIEW);
    available= false;
    list->hide ();
    message->setText (tr ("Open a document to view thumbnails."));
    message->show ();
    return;
  }
  if (actor != identity.actor || view != identity.view)
    resetDocument (identity.actor, identity.view);
  auto* proxy= canvas_proxy (shell);
  const bool nowAvailable= proxy && proxy->view_id () == view &&
                            paginated (proxy->document_menu_state ().page_rendering);
  if (nowAvailable != available) {
    available= nowAvailable;
    if (!available) {
      if (raster) raster->retire ();
      raster.reset ();
      requestedPage= -1;
      list->hide ();
      message->setText (tr ("Switch to Paper, Slideshow, Book, or Panorama "
                            "to view page thumbnails."));
      message->show ();
    }
    else {
      list->setVisible (!pages.isEmpty ());
      message->setVisible (pages.isEmpty ());
      if (pages.isEmpty ())
        message->setText (tr ("Waiting for paginated layout…"));
    }
  }
  if (!available) return;
  if (!raster) raster= QTMRenderConnection::create ();
  if (slideshow) {
    if (activeSlide != highlightedPage) {
      if (highlightedPage >= 0 && highlightedPage < list->count ())
        list->item (highlightedPage)->setSelected (false);
      highlightedPage= activeSlide;
      if (highlightedPage >= 0 && highlightedPage < list->count ()) {
        list->item (highlightedPage)->setSelected (true);
        list->scrollToItem (list->item (highlightedPage),
                            QAbstractItemView::EnsureVisible);
      }
    }
  }
  else highlightViewportPage (proxy->viewport_state (), identity.zoom_factor);

  // Completed tiny raster frames are published by the shared RenderService.
  // The UI only copies an already-rasterized ~190px image; it never paints a page.
  if (raster) {
    auto frame= raster->acquireLatestFrame ();
    if (frame && frame.bufferGeneration () == signature) {
      const int index= int (frame.frameGeneration ())-1;
      if (index >= 0 && index < pages.size ()) {
        const QImage image= frame.image ().copy ();
        cache.insert (index, image);
      }
      if (index == requestedPage) requestedPage= -1;
    }
  }
  if (requestedPage >= 0 && requestAge.isValid () && requestAge.elapsed () > 20000)
    requestedPage= -1;

  static int cycle= 0;
  if (++cycle % 3 == 0)
    (void) buffer_actor::try_submit_coalesced_to (
      actor, actor_command_kind::request_thumbnail_manifest, view,
      signature, signature != 0);
  updateVisibleThumbnails ();
}

void
QTMThumbnailsPane::activatePage (int page) {
  if (!available || page < 0 || page >= pages.size ()) return;
  (void) buffer_actor::try_submit_to (
    actor, actor_command_kind::activate_thumbnail_page, view,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, EXECUTION_CAPABILITY_BUFFER,
    static_cast<std::uint64_t> (page));
}

void
thumbnails_pane_show () {
  if (qt_defer_to_main_thread (thumbnails_pane_show)) return;
  auto* shell= QTMMainTabWindow::topTabWindow ();
  if (!shell || !shell->dockManager ()) return;
  if (!pane_widget) {
    pane_widget= new QTMThumbnailsPane;
    QObject::connect (pane_widget, &QObject::destroyed, [] {
      pane_widget= nullptr;
      pane_dock= nullptr;
    });
  }
  if (!pane_dock) {
    pane_dock= new ads::CDockWidget ("Thumbnails");
    pane_dock->setObjectName ("athena-thumbnails-pane");
    pane_dock->resize (280, 600);
    pane_dock->setWidget (pane_widget);
    pane_dock->setFeature (ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QObject::connect (pane_dock, &QObject::destroyed, [] { pane_dock= nullptr; });
  }
  shell->showAdsDockWidget (pane_dock, ads::LeftDockWidgetArea);
  pane_widget->setFocus ();
}

void
thumbnails_pane_accept_manifest (athena_view_id view, athena_blob_id id) {
  owned_actor_blob blob= actor_blob_registry::instance ().take (id);
  if (pane_widget) pane_widget->acceptManifest (view, std::move (blob));
}
