/******************************************************************************
* MODULE     : QTMVaultPreviewWidget.cpp
* DESCRIPTION: Embedded TeXmacs preview widget for vault link dialogs
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMVaultPreviewWidget.hpp"
#include "QTMVaultPreviewBuilder.hpp"
#include "qt_gui.hpp"
#include "qt_utilities.hpp"
#include "qt_widget.hpp"
#include "server.hpp"
#include "tm_ostream.hpp"
#include "tm_window.hpp"
#include "new_view.hpp"
#include "actor_ui_bridge.hpp"
#include <QLabel>
#include <QMouseEvent>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>

WikilinkPreview::WikilinkPreview (QObject* parent)
  : QObject (parent),
    previewBody (tree (DOCUMENT, "")),
    previewParent (nullptr),
    previewQtWidget (nullptr),
    previewTexmacsWidget (nullptr),
    previewWidth (0),
    previewZoom (0.0),
    recreating (false) {}

WikilinkPreview::~WikilinkPreview () {
  destroyPreview ();
}

void
WikilinkPreview::destroyPreview () {
  if (previewQtWidget != nullptr) {
    if (previewQtWidget->parentWidget () != nullptr &&
        previewQtWidget->parentWidget ()->layout () != nullptr)
      previewQtWidget->parentWidget ()->layout ()->removeWidget (
        previewQtWidget);
    previewQtWidget->hide ();
    previewQtWidget->deleteLater ();
  }
  if (!is_nil (previewWidget)) {
    try { send_destroy (previewWidget); }
    catch (...) {}
  }
  if (previewParent != nullptr)
    previewParent->removeEventFilter (this);
  previewWidget= widget ();
  previewParent= nullptr;
  previewQtWidget= nullptr;
  previewTexmacsWidget= nullptr;
  previewWidth= 0;
  previewZoom= 0.0;
}

QWidget*
WikilinkPreview::ensureCreated (QWidget* parent) {
  if (previewQtWidget != nullptr) return previewQtWidget;
  if (parent == nullptr) return nullptr;
  previewParent= parent;
  previewParent->installEventFilter (this);
  recreatePreview ();
  return previewQtWidget;
}

void
WikilinkPreview::setBody (tree body) {
  previewBody= body;
  if (previewParent != nullptr) recreatePreview ();
}

void
WikilinkPreview::refresh () {
  refreshLayout ();
}

bool
WikilinkPreview::eventFilter (QObject* watched, QEvent* event) {
  if (event != nullptr && event->type () == QEvent::Resize &&
      (watched == previewParent || isPreviewWatchedObject (watched))) {
    QTimer::singleShot (0, this, [this] () { refreshLayoutNow (); });
  }
  if (event != nullptr && isPreviewWatchedObject (watched)) {
    switch (event->type ()) {
      case QEvent::ContextMenu:
        event->accept ();
        return true;
      case QEvent::MouseButtonPress:
      case QEvent::MouseButtonRelease:
      case QEvent::MouseButtonDblClick:
      case QEvent::MouseMove:
      {
        QMouseEvent* mouse= static_cast<QMouseEvent*> (event);
        if (mouse->button () == Qt::RightButton ||
            (mouse->buttons () & Qt::RightButton) != 0) {
          event->accept ();
          return true;
        }
        break;
      }
      default:
        break;
    }
  }
  return QObject::eventFilter (watched, event);
}

void
WikilinkPreview::installPreviewEventFilter (QWidget* root) {
  if (root == nullptr) return;
  root->installEventFilter (this);
  root->setContextMenuPolicy (Qt::NoContextMenu);
  root->setFocusPolicy (Qt::NoFocus);
  QList<QWidget*> children= root->findChildren<QWidget*> ();
  for (QWidget* child : children) {
    if (child == nullptr) continue;
    child->installEventFilter (this);
    child->setContextMenuPolicy (Qt::NoContextMenu);
    child->setFocusPolicy (Qt::NoFocus);
  }
}

bool
WikilinkPreview::isPreviewWatchedObject (QObject* watched) const {
  for (QObject* obj= watched; obj != nullptr; obj= obj->parent ())
    if (obj == previewQtWidget) return true;
  return false;
}

void
WikilinkPreview::showFallbackPreview (QWidget* parent) {
  if (parent == nullptr) return;
  QLabel* label= new QLabel ("Preview unavailable.", parent);
  label->setAlignment (Qt::AlignCenter);
  label->setWordWrap (true);
  label->setFocusPolicy (Qt::NoFocus);
  label->setContextMenuPolicy (Qt::NoContextMenu);
  label->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
  if (parent->layout () != nullptr) parent->layout ()->addWidget (label);
  previewQtWidget= label;
  previewTexmacsWidget= nullptr;
  previewWidget= widget ();
  previewWidth= currentPreviewWidth ();
  previewZoom= currentPreviewZoom ();
  installPreviewEventFilter (label);
  label->show ();
}

SI
WikilinkPreview::currentPreviewWidth () const {
  if (previewParent == nullptr) return 0;
  int w= previewParent->contentsRect ().width ();
  if (w <= 0) w= previewParent->width ();
  if (w <= 0) return 0;
  return from_qsize (QSize (w, 1)).x1;
}

double
WikilinkPreview::currentPreviewZoom () const {
  tm_view view= concrete_view (get_current_view_safe ());
  actor_ui_endpoint* endpoint= view == nullptr ? nullptr :
    find_actor_ui_endpoint (view->runtime_id);
  return get_retina_zoom () * (endpoint == nullptr ? 1.0 :
                               endpoint->zoom_factor ());
}

void
WikilinkPreview::recreatePreview () {
  QWidget* parent= previewParent;
  if (parent == nullptr || recreating) return;
  struct flag_guard {
    bool& flag;
    flag_guard (bool& flag2) : flag (flag2) { flag= true; }
    ~flag_guard () { flag= false; }
  } guard (recreating);

  if (previewQtWidget != nullptr) {
    if (previewQtWidget->parentWidget () != nullptr &&
        previewQtWidget->parentWidget ()->layout () != nullptr)
      previewQtWidget->parentWidget ()->layout ()->removeWidget (
        previewQtWidget);
    previewQtWidget->hide ();
    previewQtWidget->deleteLater ();
    previewQtWidget= nullptr;
    previewTexmacsWidget= nullptr;
  }
  if (!is_nil (previewWidget)) {
    try { send_destroy (previewWidget); }
    catch (...) {}
    previewWidget= widget ();
  }

  tree style= compound ("style", tuple ("generic"));
  previewWidth= currentPreviewWidth ();
  previewZoom= currentPreviewZoom ();
  try {
    previewWidget= texmacs_output_widget (
      apply_vault_preferred_font_to_preview (previewBody), style,
      previewWidth, previewZoom);
    QWidget* qwid= concrete (previewWidget)->as_qwidget (parent);
    previewQtWidget= qwid;
    previewTexmacsWidget= qobject_cast<QTMWidget*> (qwid);
    if (previewTexmacsWidget == nullptr)
      previewTexmacsWidget= qwid->findChild<QTMWidget*> ();

    installPreviewEventFilter (qwid);
    qwid->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
    if (parent->layout () != nullptr) parent->layout ()->addWidget (qwid);
    qwid->show ();
  }
  catch (string msg) {
    std_error << "vault preview: failed to typeset preview: " << msg << LF;
    showFallbackPreview (parent);
  }
  catch (...) {
    std_error << "vault preview: failed to typeset preview" << LF;
    showFallbackPreview (parent);
  }
  refreshLayout ();
}

void
WikilinkPreview::refreshLayoutNow () {
  if (previewQtWidget == nullptr) return;
  SI width= currentPreviewWidth ();
  SI delta= width > previewWidth ? width - previewWidth :
    previewWidth - width;
  double zoom= currentPreviewZoom ();
  double zoomDelta= zoom > previewZoom ? zoom - previewZoom :
    previewZoom - zoom;
  if ((width > 0 && (previewWidth <= 0 || delta > 8 * PIXEL)) ||
      zoomDelta > 0.001) {
    recreatePreview ();
    return;
  }

  installPreviewEventFilter (previewQtWidget);
  previewQtWidget->show ();
  previewQtWidget->updateGeometry ();
  previewQtWidget->update ();

  QTMWidget* tmWidget= previewTexmacsWidget;
  if (tmWidget != nullptr) {
    tmWidget->setFocusPolicy (Qt::NoFocus);
    tmWidget->updateGeometry ();
    tmWidget->show ();
    tmWidget->update ();
    if (tmWidget->viewport () != nullptr) {
      tmWidget->viewport ()->setFocusPolicy (Qt::NoFocus);
      tmWidget->viewport ()->show ();
      if (tmWidget->viewport ()->layout () != nullptr)
        tmWidget->viewport ()->layout ()->activate ();
    }
    if (tmWidget->surface () != nullptr) {
      tmWidget->surface ()->setFocusPolicy (Qt::NoFocus);
      tmWidget->surface ()->show ();
      tmWidget->surface ()->updateGeometry ();
      tmWidget->surface ()->update ();
    }
  }

  if (the_gui != nullptr) the_gui->force_update ();
  if (tmWidget != nullptr) tmWidget->refreshEmbeddedBackingStore ();
}

void
WikilinkPreview::refreshLayout () {
  refreshLayoutNow ();
  QTimer::singleShot (0, this, [this] () { refreshLayoutNow (); });
}
