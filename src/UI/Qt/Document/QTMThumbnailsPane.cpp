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
#include <QClipboard>
#include <QHideEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace {
QTMThumbnailsPane* pane_widget= nullptr;
ads::CDockWidget* pane_dock= nullptr;
constexpr const char* screen_clipboard_mime= "application/x-athena-screen+xml";

QSize thumbnail_preview_size (const thumbnail_page_bounds& page) {
  const double width= double (page.x2) - double (page.x1);
  const double height= double (page.y2) - double (page.y1);
  if (width <= 0 || height <= 0) return QSize (190, 270);
  const double scale= std::min (190.0/width, 270.0/height);
  return QSize (std::max (1, int (std::ceil (width*scale))),
                std::max (1, int (std::ceil (height*scale))));
}

// A wide Beamer screen must not reserve portrait-page height beneath its
// raster. Derive the row height from the image aspect ratio, not a fixed 270px.
class thumbnail_item_delegate final: public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QSize sizeHint (const QStyleOptionViewItem& option,
                  const QModelIndex& index) const override {
    const QSize preview= index.data (Qt::UserRole).toSize ();
    return QSize (210, (preview.isValid () ? preview.height () : 270) +
                       option.fontMetrics.height () + 12);
  }

  void paint (QPainter* painter, const QStyleOptionViewItem& option,
              const QModelIndex& index) const override {
    painter->save ();
    painter->setClipRect (option.rect);
    const QSize size= index.data (Qt::UserRole).toSize ();
    const QSize iconSize= size.isValid () ? size : QSize (190, 270);
    const int iconHeight= iconSize.height ();
    const QRect imageArea= option.rect.adjusted (6, 4, -6, 0);
    const QIcon icon= qvariant_cast<QIcon> (index.data (Qt::DecorationRole));
    if (!icon.isNull ()) {
      const QPixmap preview= icon.pixmap (iconSize);
      const QSize logical= preview.deviceIndependentSize ().toSize ();
      painter->drawPixmap (
        QPoint (imageArea.center ().x () - logical.width ()/2,
                imageArea.top ()), preview);
    }

    const QString caption= index.data (Qt::DisplayRole).toString ();
    const QFontMetrics metrics (option.font);
    const int textTop= imageArea.top () + iconHeight + 2;
    const QRect textRect (
      option.rect.left () + 6, textTop, option.rect.width () - 12,
      metrics.height () + 4);
    painter->setFont (option.font);
    if (option.state.testFlag (QStyle::State_Selected)) {
      const int highlightWidth= std::min (
        textRect.width (), metrics.horizontalAdvance (caption) + 18);
      const QRect highlight (
        textRect.center ().x () - highlightWidth/2, textRect.top (),
        highlightWidth, textRect.height ());
      painter->fillRect (highlight, option.palette.highlight ());
      painter->setPen (option.palette.highlightedText ().color ());
    }
    else painter->setPen (option.palette.text ().color ());
    painter->drawText (textRect, Qt::AlignHCenter | Qt::AlignVCenter,
                       caption);
    painter->restore ();
  }
};

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
    newScreen (new QPushButton (tr ("New screen"), this)),
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
  list->setViewMode (QListView::ListMode);
  list->setFlow (QListView::TopToBottom);
  list->setWrapping (false);
  list->setMovement (QListView::Static);
  list->setUniformItemSizes (false);
  list->setItemDelegate (new thumbnail_item_delegate (list));
  // The delegate paints the image itself. Do not give the view a fixed
  // portrait-sized icon that could inflate a wide screen's row height.
  list->setSpacing (4);
  list->setSelectionMode (QAbstractItemView::SingleSelection);
  list->hide ();
  layout->addWidget (list, 1);
  newScreen->setObjectName ("athenaNewScreenButton");
  newScreen->hide ();
  layout->addWidget (newScreen);
  connect (newScreen, &QPushButton::clicked, this, [this] {
    if (available && slideshow && writable)
      (void) thumbnails_screen_action (
        actor, view, screen_edit_operation::insert_after, activeSlide);
  });
  // QAbstractScrollArea dispatches viewport context-menu events through the
  // item view. Its signal position is already in viewport coordinates.
  list->setContextMenuPolicy (Qt::CustomContextMenu);
  connect (list, &QWidget::customContextMenuRequested,
           this, [this] (const QPoint& pos) {
    if (!available || !slideshow) return;
    QListWidgetItem* item= list->itemAt (pos);
    if (!item) return;
    const int index= list->row (item);
    const athena_actor_id targetActor= actor;
    const athena_view_id targetView= view;
    const QMimeData* offer= QApplication::clipboard ()->mimeData ();
    const bool canPaste= offer && offer->hasFormat (screen_clipboard_mime);
    QMenu menu (this);
    QAction* copy= menu.addAction (tr ("Copy"));
    QAction* before= menu.addAction (tr ("Paste before"));
    QAction* after= menu.addAction (tr ("Paste after"));
    before->setEnabled (writable && canPaste);
    after->setEnabled (writable && canPaste);
    QAction* duplicate= menu.addAction (tr ("Duplicate"));
    duplicate->setEnabled (writable);
    menu.addSeparator ();
    QAction* insertBefore= menu.addAction (tr ("Insert before"));
    QAction* insertAfter= menu.addAction (tr ("Insert after"));
    insertBefore->setEnabled (writable);
    insertAfter->setEnabled (writable);
    menu.addSeparator ();
    QAction* erase= menu.addAction (tr ("Delete screen"));
    erase->setEnabled (writable && pages.size () > 1);
    QAction* chosen= menu.exec (list->viewport ()->mapToGlobal (pos));
    if (!chosen || actor != targetActor || view != targetView) return;
    const auto operation= chosen == copy ? screen_edit_operation::copy :
      chosen == before ? screen_edit_operation::paste_before :
      chosen == after ? screen_edit_operation::paste_after :
      chosen == duplicate ? screen_edit_operation::duplicate :
      chosen == insertBefore ? screen_edit_operation::insert_before :
      chosen == insertAfter ? screen_edit_operation::insert_after :
      screen_edit_operation::erase;
    (void) thumbnails_screen_action (targetActor, targetView, operation, index);
  });
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
  newScreen->hide ();
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
  writable= false;
  activeSlide= -1;
  requestedPage= -1;
  layoutSettled.invalidate ();
  pages.clear ();
  highlightedPage= -1;
  cache.clear ();
  list->clear ();
}

bool
QTMThumbnailsPane::thumbnailIsCurrent (int index) const {
  const auto found= cache.constFind (index);
  if (found == cache.cend () || index < 0 || index >= pages.size ())
    return false;
  if (!slideshow) return found->manifest_signature == signature;
  return found->style_signature == styleSignature &&
         found->content_signature == pages[index].content_signature;
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
  if (slideshow != slides) cache.clear ();
  else if (!sameEntries) {
    // Screen insertion/removal changes indices. Move existing rasters by the
    // source content identity so unaffected screens remain visible immediately.
    // Paper pages have no separate content identity; keep their old position as
    // a temporary image while new pagination rasters are being prepared.
    QHash<int, cached_thumbnail> carried;
    if (slides) {
      const std::byte* incoming= blob.data () + sizeof (header);
      for (int i=0; i<int (header.count); ++i) {
        thumbnail_page_bounds next;
        std::memcpy (&next, incoming + i*sizeof (next), sizeof (next));
        for (int j=0; j<pages.size (); ++j)
          if (pages[j].content_signature == next.content_signature &&
              cache.contains (j)) {
            carried.insert (i, cache.value (j));
            break;
          }
      }
    }
    else
      for (int i=0; i<int (header.count); ++i)
        if (cache.contains (i)) carried.insert (i, cache.value (i));
    cache.swap (carried);
  }
  signature= header.signature;
  styleSignature= header.style_signature;
  contentSignature= header.content_signature;
  slideshow= slides;
  newScreen->setVisible (available && slideshow);
  activeSlide= slides ? int (header.active_page) : -1;
  requestedPage= -1;
  layoutSettled.start ();
  // An invalidated image is still the best available image. Leave it displayed
  // until RenderService delivers its replacement, without a blank intermediate
  // frame. Freshness is tracked separately from the cached image above.
  pages.resize (int (header.count));
  if (header.count)
    std::memcpy (pages.data (), blob.data () + sizeof (header),
                 std::size_t (header.count) * sizeof (thumbnail_page_bounds));
  if (!sameEntries) {
    list->clear ();
    highlightedPage= -1;
    for (int i=0; i<pages.size (); ++i) {
      auto* item= new QListWidgetItem (
        slides ? tr ("Slide %1").arg (i+1) : tr ("Page %1").arg (i+1));
      item->setTextAlignment (Qt::AlignHCenter);
      auto oldImage= cache.constFind (i);
      if (oldImage != cache.cend ()) {
        item->setData (Qt::UserRole, oldImage->image.size ());
        item->setIcon (QPixmap::fromImage (oldImage->image));
      }
      else item->setData (Qt::UserRole, thumbnail_preview_size (pages[i]));
      list->addItem (item);
    }
  }
  else {
    for (int i=0; i<pages.size (); ++i) {
      const QSize preferred= thumbnail_preview_size (pages[i]);
      QListWidgetItem* item= list->item (i);
      if (item->data (Qt::UserRole).toSize () != preferred &&
          !cache.contains (i))
        item->setData (Qt::UserRole, preferred);
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
  // Locate nearby rows without walking every thumbnail on each timer tick.
  int lo= 0, hi= pages.size ();
  while (lo < hi) {
    const int mid= lo + (hi-lo)/2;
    if (list->visualItemRect (list->item (mid)).bottom () < top)
      lo= mid + 1;
    else hi= mid;
  }
  int next= -1;
  for (int i=lo; i<pages.size (); ++i) {
    auto* item= list->item (i);
    if (list->visualItemRect (item).top () > bottom) break;
    auto image= cache.constFind (i);
    if (image != cache.cend () && item->icon ().isNull ())
      item->setIcon (QPixmap::fromImage (image->image));
    if (next < 0 && !thumbnailIsCurrent (i) && i != requestedPage &&
             pages[i].x2 > pages[i].x1 && pages[i].y2 > pages[i].y1)
      next= i;
  }
  if (next < 0) {
    // Visible rows take priority, but offscreen pages with outdated rasters
    // also need replacement. Their old images stay on screen until then.
    for (int i=0; i<pages.size (); ++i)
      if (!thumbnailIsCurrent (i) && i != requestedPage &&
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
    newScreen->hide ();
    message->setText (tr ("Open a document to view thumbnails."));
    message->show ();
    return;
  }
  if (actor != identity.actor || view != identity.view)
    resetDocument (identity.actor, identity.view);
  auto* proxy= canvas_proxy (shell);
  const bool nowAvailable= proxy && proxy->view_id () == view &&
                             paginated (proxy->document_menu_state ().page_rendering);
  writable= proxy && proxy->editor_command_state ().valid () &&
            !proxy->editor_command_state ().read_only ();
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
  newScreen->setVisible (slideshow);
  newScreen->setEnabled (writable);
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
        cache.insert (index, cached_thumbnail {
          image, styleSignature, pages[index].content_signature, signature});
        QListWidgetItem* item= list->item (index);
        // Replace the previous image directly, after the complete raster has
        // arrived. Never show an empty icon between source edits and frames.
        if (item) item->setIcon (QPixmap::fromImage (image));
        if (item && item->data (Qt::UserRole).toSize () != image.size ())
          item->setData (Qt::UserRole, image.size ());
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

bool
thumbnails_screen_action (athena_actor_id actor, athena_view_id view,
                          screen_edit_operation operation, int index,
                          std::uint64_t confirmed) {
  athena_blob_id payload= ATHENA_NO_BLOB;
  if (operation == screen_edit_operation::paste_before ||
      operation == screen_edit_operation::paste_after) {
    const QMimeData* offer= QApplication::clipboard ()->mimeData ();
    if (!offer || !offer->hasFormat (screen_clipboard_mime)) return false;
    const QByteArray bytes= offer->data (screen_clipboard_mime);
    auto reservation= actor_blob_registry::instance ().allocate (
      std::size_t (bytes.size ()));
    if (!bytes.isEmpty ())
      std::memcpy (reservation.data (), bytes.constData (),
                   std::size_t (bytes.size ()));
    payload= reservation.publish ();
  }
  auto sent= buffer_actor::try_submit_to (
    actor, actor_command_kind::edit_screen, view,
    payload, ATHENA_NO_BLOB, EXECUTION_CAPABILITY_BUFFER,
    static_cast<std::uint64_t> (operation),
    static_cast<std::uint64_t> (static_cast<std::int64_t> (index)), confirmed);
  if (!sent && payload != ATHENA_NO_BLOB)
    (void) actor_blob_registry::instance ().discard (payload);
  return static_cast<bool> (sent);
}

void
thumbnails_screen_set_clipboard (const std::string& xml) {
  auto* offer= new QMimeData;
  offer->setData (screen_clipboard_mime,
                  QByteArray (xml.data (), qsizetype (xml.size ())));
  QApplication::clipboard ()->setMimeData (offer);
}

void
thumbnails_screen_confirm_delete (athena_actor_id actor, athena_view_id view,
                                  int index, std::uint64_t stamp) {
  QTMMainTabWindow* shell= QTMMainTabWindow::topTabWindow ();
  QMessageBox prompt (QMessageBox::Warning, QObject::tr ("Delete screen"),
    QObject::tr ("Delete screen %1 and its contents?").arg (index + 1),
    QMessageBox::NoButton, shell);
  auto* erase= prompt.addButton (QObject::tr ("Delete"),
                                 QMessageBox::DestructiveRole);
  prompt.addButton (QMessageBox::Cancel);
  prompt.exec ();
  if (prompt.clickedButton () == erase)
    (void) thumbnails_screen_action (
      actor, view, screen_edit_operation::erase, index, stamp);
}
