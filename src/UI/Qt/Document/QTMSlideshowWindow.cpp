/******************************************************************************
* MODULE     : QTMSlideshowWindow.cpp
* DESCRIPTION: Independent, non-editable, actor-rendered slide presentation
******************************************************************************/
#include "UI/Qt/Document/QTMSlideshowWindow.hpp"
#include "UI/Qt/Editor/QTMRenderService.hpp"
#include "App/Runtime/buffer_actor.hpp"
#include "UI/Qt/Toolkit/qt_utilities.hpp"
#include "boot.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDialog>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidgetAction>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <functional>

namespace {

bool slideshow_option (const char* name, const char* fallback= "on") {
  return get_preference (name, fallback) == "on";
}

QString slideshow_setting (const char* name, const char* fallback) {
  return to_qstring (get_preference (name, fallback));
}

struct slide_image {
  QImage pixels;
  QSize render_size;
};

class QTMSlideshowWindow final: public QWidget {
  athena_actor_id actor_;
  athena_view_id view_;
  const int count_;
  int page_;
  bool finished_= false;
  bool panZoom_= false;
  bool panning_= false;
  QPoint lastPointer_;
  QPointF pan_;
  qreal zoom_= 1.0;
  const QString visibility_;
  const QString location_;
  std::shared_ptr<QTMRenderConnection> raster_;
  QHash<int, slide_image> images_;
  QTimer poll_;
  QTimer resizeDebounce_;
  QElapsedTimer requestAge_;
  std::uint64_t session_;
  std::uint64_t serial_= 0;
  std::uint64_t pendingSerial_= 0;
  int pendingPage_= -1;
  QSize pendingSize_;
  QWidget* controls_= nullptr;
  QSlider* progress_= nullptr;
  QLabel* pageCount_= nullptr;
  QPointer<QDialog> panorama_;
  QPointer<QListWidget> panoramaList_;

  QSize renderSize () const {
    const qreal dpr= devicePixelRatioF ();
    return QSize (
      std::clamp (int (width () * dpr * zoom_), 1, 16384),
      std::clamp (int (height () * dpr * zoom_), 1, 16384));
  }

  bool ready (int index) const {
    const auto found= images_.constFind (index);
    return found != images_.cend () &&
           found->render_size == renderSize () &&
           !found->pixels.isNull ();
  }

  void requestFrame (int index) {
    if (pendingPage_ >= 0 || !raster_) return;
    const QSize size= renderSize ();
    const std::uint64_t newSerial= ++serial_;
    if (!buffer_actor::try_submit_to (
          actor_, actor_command_kind::render_slideshow_frame, view_,
          ATHENA_NO_BLOB, ATHENA_NO_BLOB, EXECUTION_CAPABILITY_BUFFER,
          static_cast<std::uint64_t> (index), raster_->id (),
          static_cast<std::uint64_t> (size.width ()),
          static_cast<std::uint64_t> (size.height ()),
          session_, newSerial)) return;
    pendingPage_= index;
    pendingSerial_= newSerial;
    pendingSize_= size;
    requestAge_.restart ();
  }

  void syncPanorama () {
    if (!panoramaList_) return;
    for (int i=0; i<std::min (count_, panoramaList_->count ()); ++i) {
      const auto found= images_.constFind (i);
      if (found == images_.cend () || found->pixels.isNull ()) continue;
      auto* item= panoramaList_->item (i);
      if (!item->icon ().isNull ()) continue;
      item->setIcon (QPixmap::fromImage (found->pixels.scaled (
        QSize (190, 108), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }
  }

  void pollFrames () {
    if (raster_) {
      auto frame= raster_->acquireLatestFrame ();
      if (frame && frame.bufferGeneration () == session_ &&
          frame.frameGeneration () == pendingSerial_ && pendingPage_ >= 0) {
        const int index= pendingPage_;
        images_.insert (index, {frame.image ().copy (), pendingSize_});
        pendingPage_= -1;
        update ();
        syncPanorama ();
      }
    }
    if (pendingPage_ >= 0 && requestAge_.elapsed () > 15000)
      pendingPage_= -1;
    if (pendingPage_ >= 0 || finished_) return;
    if (!ready (page_)) { requestFrame (page_); return; }
    // Fill other slides in the background. Fast navigation then usually needs
    // no actor work, and images remain cached for the presentation's lifetime.
    for (int delta=1; delta<count_; ++delta) {
      const int following= page_+delta;
      const int preceding= page_-delta;
      if (following < count_ && !ready (following)) {
        requestFrame (following);
        break;
      }
      if (preceding >= 0 && !ready (preceding)) {
        requestFrame (preceding);
        break;
      }
    }
  }

  QToolButton* tool (QHBoxLayout* row, const QString& label,
                     const char* preference, std::function<void()> action) {
    auto* button= new QToolButton (controls_);
    button->setText (label);
    button->setFocusPolicy (Qt::NoFocus);
    if (slideshow_option (preference)) row->addWidget (button);
    else button->hide ();
    connect (button, &QToolButton::clicked, this,
             [action= std::move (action)] { action (); });
    return button;
  }

  void makeControls () {
    controls_= new QWidget (this);
    controls_->setObjectName (QStringLiteral ("athenaSlideshowControls"));
    controls_->setStyleSheet (
      QStringLiteral ("#athenaSlideshowControls { background: rgba(20,24,28,215); "
                      "border-radius: 7px; }"
                     "#athenaSlideshowControls QToolButton, "
                     "#athenaSlideshowControls QLabel { color: white; "
                     "padding: 3px 6px; }"));
    auto* row= new QHBoxLayout (controls_);
    row->setContentsMargins (8, 3, 8, 3);
    row->setSpacing (4);
    tool (row, QStringLiteral ("|◀"), "slideshow control first",
          [this] { go (0); });
    tool (row, QStringLiteral ("◀"), "slideshow control previous",
          [this] { go (std::max (0, page_-1)); });
    tool (row, QStringLiteral ("▶"), "slideshow control next",
          [this] { next (); });
    tool (row, QStringLiteral ("▶|"), "slideshow control last",
          [this] { go (count_-1); });
    tool (row, tr ("Go to…"), "slideshow control go to",
          [this] { jumpDialog (); });
    tool (row, tr ("Panorama"), "slideshow control panorama",
          [this] { openPanorama (); });
    if (slideshow_option ("slideshow control progress")) {
      progress_= new QSlider (Qt::Horizontal, controls_);
      progress_->setRange (1, count_);
      progress_->setValue (page_+1);
      progress_->setFixedWidth (180);
      progress_->setFocusPolicy (Qt::NoFocus);
      progress_->setToolTip (tr ("Slide progress — drag to jump"));
      connect (progress_, &QSlider::sliderReleased, this, [this] {
        go (progress_->value ()-1);
      });
      row->addWidget (progress_);
      pageCount_= new QLabel (QStringLiteral ("%1/%2").arg (page_+1).arg (count_),
                              controls_);
      row->addWidget (pageCount_);
    }
    controls_->adjustSize ();
    positionControls ();
  }

  void positionControls () {
    if (!controls_) return;
    controls_->adjustSize ();
    const QSize size= controls_->sizeHint ();
    const int x= location_ == QStringLiteral ("left") ? 12 :
      location_ == QStringLiteral ("right") ? width ()-size.width ()-12 :
      (width ()-size.width ())/2;
    controls_->setGeometry (std::max (0, x),
      std::max (0, height ()-size.height ()-12),
      std::min (size.width (), width ()), size.height ());
    controls_->raise ();
  }

  void showControlsAt (const QPoint& pos) {
    if (!controls_ || finished_) return;
    if (visibility_ == QStringLiteral ("always")) {
      controls_->show ();
      return;
    }
    if (visibility_ == QStringLiteral ("never")) {
      controls_->hide ();
      return;
    }
    const bool nearBottom= pos.y () >= height ()-90;
    const bool nearControls= pos.x () >= controls_->x ()-70 &&
                             pos.x () <= controls_->geometry ().right ()+70;
    controls_->setVisible (controls_->underMouse () ||
                            (nearBottom && nearControls));
  }

  void go (int index) {
    if (index < 0 || index >= count_) return;
    finished_= false;
    page_= index;
    pan_= {};
    zoom_= 1.0;
    if (progress_) progress_->setValue (page_+1);
    if (pageCount_) pageCount_->setText (
      QStringLiteral ("%1/%2").arg (page_+1).arg (count_));
    if (controls_) controls_->setVisible (
      visibility_ == QStringLiteral ("always") &&
      controls_->layout ()->count () != 0);
    update ();
    pollFrames ();
  }

  void next () {
    if (page_+1 < count_) go (page_+1);
    else {
      finished_= true;
      if (controls_) controls_->hide ();
      update ();
    }
  }

  void jumpDialog () {
    bool accepted= false;
    const int choice= QInputDialog::getInt (
      this, tr ("Go to slide"), tr ("Slide number:"),
      page_+1, 1, count_, 1, &accepted);
    if (accepted) go (choice-1);
    activateWindow ();
    setFocus (Qt::OtherFocusReason);
  }

  void openPanorama () {
    if (panorama_) { panorama_->raise (); return; }
    auto* dialog= new QDialog (this);
    panorama_= dialog;
    dialog->setAttribute (Qt::WA_DeleteOnClose);
    dialog->setWindowTitle (tr ("Slide panorama — select a slide"));
    dialog->resize (std::min (1000, width ()-30),
                    std::min (720, height ()-30));
    auto* layout= new QVBoxLayout (dialog);
    auto* list= new QListWidget (dialog);
    panoramaList_= list;
    list->setViewMode (QListView::IconMode);
    list->setIconSize (QSize (190, 108));
    list->setGridSize (QSize (212, 146));
    list->setMovement (QListView::Static);
    list->setResizeMode (QListView::Adjust);
    list->setWrapping (true);
    for (int i=0; i<count_; ++i) {
      auto* item= new QListWidgetItem (tr ("Slide %1").arg (i+1), list);
      item->setTextAlignment (Qt::AlignHCenter);
    }
    list->setCurrentRow (page_);
    layout->addWidget (list);
    auto* dismiss= new QPushButton (tr ("Close panorama"), dialog);
    layout->addWidget (dismiss);
    connect (dismiss, &QPushButton::clicked, dialog, &QDialog::close);
    connect (list, &QListWidget::itemActivated, dialog,
             [this, dialog, list] (QListWidgetItem* item) {
               go (list->row (item));
               dialog->close ();
             });
    connect (list, &QListWidget::itemClicked, dialog,
             [this, dialog, list] (QListWidgetItem* item) {
               go (list->row (item));
               dialog->close ();
             });
    connect (dialog, &QObject::destroyed, this, [this] {
      panorama_.clear ();
      panoramaList_.clear ();
      setFocus ();
    });
    syncPanorama ();
    dialog->open ();
  }

  void changeZoom (qreal factor, const QPointF& at) {
    if (!panZoom_ || finished_) return;
    const qreal old= zoom_;
    zoom_= std::clamp (zoom_*factor, qreal (1.0), qreal (6.0));
    pan_= (pan_-at+QPointF (width ()/2.0, height ()/2.0)) * (zoom_/old) +
          at-QPointF (width ()/2.0, height ()/2.0);
    if (zoom_ == 1.0) pan_= {};
    resizeDebounce_.start ();
    update ();
  }

protected:
  void paintEvent (QPaintEvent*) override {
    QPainter painter (this);
    painter.fillRect (rect (), QColor (14, 16, 20));
    if (finished_) {
      painter.setPen (Qt::white);
      QFont font= painter.font ();
      font.setPointSize (22);
      painter.setFont (font);
      painter.drawText (rect (), Qt::AlignCenter,
                        tr ("End of slideshow\nPress any key to exit"));
      return;
    }
    auto found= images_.constFind (page_);
    if (found == images_.cend () || found->pixels.isNull ()) {
      painter.setPen (Qt::white);
      painter.drawText (rect (), Qt::AlignCenter,
                        tr ("Rendering slide %1…").arg (page_+1));
      return;
    }
    const QImage& pixels= found->pixels;
    const double factor= std::min (double (width ())/pixels.width (),
                                   double (height ())/pixels.height ());
    const QSizeF imageSize (pixels.width ()*factor*zoom_,
                            pixels.height ()*factor*zoom_);
    const QPointF at ((width ()-imageSize.width ())/2 + pan_.x (),
                       (height ()-imageSize.height ())/2 + pan_.y ());
    painter.setRenderHint (QPainter::SmoothPixmapTransform);
    painter.drawImage (QRectF (at, imageSize), pixels);
  }

  void keyPressEvent (QKeyEvent* event) override {
    if (finished_) { close (); return; }
    const Qt::KeyboardModifiers mods= event->modifiers () &
      (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    if (mods == Qt::ControlModifier && event->key () == Qt::Key_G) {
      jumpDialog ();
      return;
    }
    if (mods != Qt::NoModifier) { event->ignore (); return; }
    switch (event->key ()) {
    case Qt::Key_Escape: close (); return;
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Right:
    case Qt::Key_Down:
    case Qt::Key_PageDown: next (); return;
    case Qt::Key_Left:
    case Qt::Key_Up:
    case Qt::Key_PageUp: go (std::max (0, page_-1)); return;
    case Qt::Key_Home: go (0); return;
    case Qt::Key_End: go (count_-1); return;
    default: event->accept (); return; // Presentation never edits the source.
    }
  }

  void contextMenuEvent (QContextMenuEvent* event) override {
    if (finished_) { event->accept (); return; }
    QMenu menu (this);
    QAction* first= menu.addAction (tr ("First slide"));
    QAction* previous= menu.addAction (tr ("Previous slide"));
    QAction* nextAction= menu.addAction (tr ("Next slide"));
    QAction* last= menu.addAction (tr ("Last slide"));
    first->setEnabled (page_ > 0);
    previous->setEnabled (page_ > 0);
    last->setEnabled (page_+1 < count_);
    menu.addSeparator ();
    QAction* jump= menu.addAction (tr ("Go to slide…  Ctrl+G"));
    QAction* panorama= menu.addAction (tr ("Panorama"));
    auto* slider= new QSlider (Qt::Horizontal, &menu);
    slider->setRange (1, count_);
    slider->setValue (page_+1);
    slider->setMinimumWidth (185);
    auto* progress= new QWidgetAction (&menu);
    progress->setDefaultWidget (slider);
    menu.addAction (progress);
    connect (slider, &QSlider::sliderReleased, &menu, [this, slider] {
      go (slider->value ()-1);
    });
    menu.addSeparator ();
    QAction* exit= menu.addAction (tr ("End slideshow  Esc"));
    const QAction* chosen= menu.exec (event->globalPos ());
    if (chosen == first) go (0);
    else if (chosen == previous) go (std::max (0, page_-1));
    else if (chosen == nextAction) this->next ();
    else if (chosen == last) go (count_-1);
    else if (chosen == jump) jumpDialog ();
    else if (chosen == panorama) openPanorama ();
    else if (chosen == exit) close ();
    event->accept ();
  }

  void mouseMoveEvent (QMouseEvent* event) override {
    if (panning_ && panZoom_) {
      pan_+= event->position ()-QPointF (lastPointer_);
      lastPointer_= event->position ().toPoint ();
      update ();
    }
    else showControlsAt (event->position ().toPoint ());
  }

  void mousePressEvent (QMouseEvent* event) override {
    if (panZoom_ && zoom_ > 1 && event->button () == Qt::LeftButton) {
      panning_= true;
      lastPointer_= event->position ().toPoint ();
      setCursor (Qt::ClosedHandCursor);
      event->accept ();
      return;
    }
    QWidget::mousePressEvent (event);
  }

  void mouseReleaseEvent (QMouseEvent* event) override {
    if (panning_ && event->button () == Qt::LeftButton) {
      panning_= false;
      unsetCursor ();
      event->accept ();
      return;
    }
    QWidget::mouseReleaseEvent (event);
  }

  void wheelEvent (QWheelEvent* event) override {
    if (panZoom_ && (event->modifiers () == Qt::ControlModifier || zoom_ > 1)) {
      changeZoom (std::pow (1.2, event->angleDelta ().y ()/120.0),
                  event->position ());
    }
    else if (event->angleDelta ().y () < 0) next ();
    else if (event->angleDelta ().y () > 0) go (std::max (0, page_-1));
    event->accept ();
  }

  bool event (QEvent* e) override {
    if (e->type () == QEvent::NativeGesture && panZoom_) {
      auto* native= static_cast<QNativeGestureEvent*> (e);
      if (native->gestureType () == Qt::ZoomNativeGesture) {
        changeZoom (1.0+native->value (), native->position ());
        return true;
      }
    }
    return QWidget::event (e);
  }

  void resizeEvent (QResizeEvent* event) override {
    QWidget::resizeEvent (event);
    positionControls ();
    resizeDebounce_.start ();
  }

  void closeEvent (QCloseEvent* event) override {
    if (raster_) raster_->retire ();
    raster_.reset ();
    // Restore the editor at the slide last presented, without permitting any
    // editing inside the separate read-only projection window.
    (void) buffer_actor::try_submit_to (
      actor_, actor_command_kind::activate_thumbnail_page, view_,
      ATHENA_NO_BLOB, ATHENA_NO_BLOB, EXECUTION_CAPABILITY_BUFFER,
      static_cast<std::uint64_t> (page_));
    QWidget::closeEvent (event);
  }

public:
  QTMSlideshowWindow (athena_actor_id actor, athena_view_id view,
                      int count, int initial):
    actor_ (actor), view_ (view), count_ (count),
    page_ (std::clamp (initial, 0, count-1)),
    panZoom_ (slideshow_option ("slideshow pan zoom", "off")),
    visibility_ (slideshow_setting ("slideshow controls visibility", "hover")),
    location_ (slideshow_setting ("slideshow controls location", "center")),
    raster_ (QTMRenderConnection::create ()) {
    static std::atomic<std::uint64_t> nextSession {1};
    session_= nextSession.fetch_add (1, std::memory_order_relaxed);
    setWindowTitle (tr ("ATHENA — Slideshow"));
    setAttribute (Qt::WA_DeleteOnClose);
    setFocusPolicy (Qt::StrongFocus);
    setMouseTracking (true);
    setMinimumSize (400, 260);
    // The surface is painted explicitly below. A style sheet on the top-level
    // QWidget cascades into child QInputDialogs: it made their backgrounds
    // dark while their inherited label/button text remained dark as well.
    // Keep native dialog palettes intact instead of theming the whole subtree.
    resize (1280, 720);
    resizeDebounce_.setSingleShot (true);
    resizeDebounce_.setInterval (180);
    connect (&resizeDebounce_, &QTimer::timeout, this, [this] {
      // Large-page recordings are generated only once the window size settles.
      if (pendingPage_ < 0) pollFrames ();
    });
    makeControls ();
    poll_.setInterval (60);
    connect (&poll_, &QTimer::timeout, this, [this] {
      if (!resizeDebounce_.isActive ()) pollFrames ();
    });
    poll_.start ();
    if (slideshow_setting ("slideshow display", "fullscreen") ==
        QStringLiteral ("window")) showNormal ();
    else showFullScreen ();
    activateWindow ();
    setFocus (Qt::OtherFocusReason);
    showControlsAt (QPoint (width ()/2, height ()/2));
    pollFrames ();
  }
};

} // namespace

void
qtm_slideshow_open (athena_actor_id actor, athena_view_id view,
                     int page_count, int initial_page) {
  if (actor == ATHENA_NO_ACTOR || view == ATHENA_NO_VIEW || page_count < 1)
    return;
  // Explicitly separate top-level window. Never reparent or replace the
  // original Buffer Editor, which remains a normal editable document.
  new QTMSlideshowWindow (actor, view, page_count, initial_page);
}
