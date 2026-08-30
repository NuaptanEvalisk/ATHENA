/******************************************************************************
* MODULE     : QTMToast.cpp
* DESCRIPTION: Native Qt toast notifications for ATHENA
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMToast.hpp"

#include "qt_utilities.hpp"
#include "scheme.hpp"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

static QPointer<QFrame> activeToast;

static void position_toast (QFrame* toast, QWidget* parent);

class QTMToastFrame final: public QFrame {
public:
  explicit QTMToastFrame (QWidget* parent): QFrame (parent), anchor (parent) {
    if (anchor != nullptr) anchor->installEventFilter (this);
  }

protected:
  bool eventFilter (QObject* watched, QEvent* event) override {
    if (watched == anchor && event->type () == QEvent::Resize)
      position_toast (this, anchor);
    return QFrame::eventFilter (watched, event);
  }

private:
  QPointer<QWidget> anchor;
};

static QWidget*
toast_parent () {
  QWidget* parent= QApplication::activeWindow ();
  if (parent != nullptr && parent->isVisible ()) return parent;

  const QWidgetList widgets= QApplication::topLevelWidgets ();
  for (QWidget* widget: widgets)
    if (widget != nullptr && widget->isVisible () && widget->isWindow ())
      return widget;
  return nullptr;
}

static QString
to_qstring_toast (string s) {
  return QString::fromUtf8 (as_charp (s), N(s));
}

static void
position_toast (QFrame* toast, QWidget* parent) {
  toast->adjustSize ();
  const int margin= 18;
  QSize size= toast->sizeHint ();
  int x= parent->width () - size.width () - margin;
  int y= margin;
  if (x < margin) x= margin;
  toast->setGeometry (x, y, size.width (), size.height ());
}

} // namespace

bool
qtm_show_toast (string left, string right) {
  QWidget* parent= toast_parent ();
  if (parent == nullptr) return false;

  QString title= to_qstring_toast (right);
  QString body = to_qstring_toast (left);
  if (title.trimmed ().isEmpty () && body.trimmed ().isEmpty ()) return false;

  if (activeToast) activeToast->close ();

  QFrame* toast= new QTMToastFrame (parent);
  activeToast= toast;
  toast->setAttribute (Qt::WA_DeleteOnClose);
  toast->setWindowFlags (Qt::FramelessWindowHint);
  toast->setObjectName ("athenaToastNotification");
  toast->setMinimumWidth (380);
  toast->setMaximumWidth (560);
  toast->setStyleSheet (
    "#athenaToastNotification {"
    "background: rgba(248, 249, 252, 225);"
    "border: 1px solid rgba(34, 40, 49, 45);"
    "border-radius: 0px;"
    "}"
    "#athenaToastNotification QLabel {"
    "color: rgba(24, 28, 35, 245);"
    "background: transparent;"
    "}"
    "#athenaToastNotification QLabel#athenaToastTitle {"
    "font-weight: 600;"
    "}"
    "#athenaToastNotification QLabel#athenaToastBody {"
    "color: rgba(24, 28, 35, 210);"
    "}"
    "#athenaToastNotification QToolButton#athenaToastDismiss {"
    "background: transparent;"
    "border: 0px;"
    "border-left: 1px solid rgba(34, 40, 49, 45);"
    "border-radius: 0px;"
    "padding: 0px;"
    "}"
    "#athenaToastNotification QToolButton#athenaToastDismiss:hover {"
    "background: rgba(34, 40, 49, 18);"
    "}"
    "#athenaToastNotification QToolButton#athenaToastDismiss:pressed {"
    "background: rgba(34, 40, 49, 30);"
    "}");

  QHBoxLayout* outer= new QHBoxLayout (toast);
  outer->setContentsMargins (0, 0, 0, 0);
  outer->setSpacing (0);

  QWidget* content= new QWidget (toast);
  QVBoxLayout* layout= new QVBoxLayout (content);
  layout->setContentsMargins (16, 12, 16, 12);
  layout->setSpacing (4);
  outer->addWidget (content, 1);

  if (!title.trimmed ().isEmpty ()) {
    QLabel* titleLabel= new QLabel (title, content);
    titleLabel->setObjectName ("athenaToastTitle");
    titleLabel->setWordWrap (true);
    titleLabel->setMinimumWidth (304);
    titleLabel->setMaximumWidth (484);
    layout->addWidget (titleLabel);
  }

  if (!body.trimmed ().isEmpty ()) {
    QLabel* bodyLabel= new QLabel (body, content);
    bodyLabel->setObjectName ("athenaToastBody");
    bodyLabel->setWordWrap (true);
    bodyLabel->setMinimumWidth (304);
    bodyLabel->setMaximumWidth (484);
    layout->addWidget (bodyLabel);
  }

  QToolButton* dismiss= new QToolButton (toast);
  dismiss->setObjectName ("athenaToastDismiss");
  dismiss->setToolTip ("Dismiss notification");
  dismiss->setAccessibleName ("Dismiss notification");
  dismiss->setIcon (toast->style ()->standardIcon (
    QStyle::SP_TitleBarCloseButton));
  dismiss->setIconSize (QSize (18, 18));
  dismiss->setFixedWidth (44);
  dismiss->setSizePolicy (QSizePolicy::Fixed, QSizePolicy::Expanding);
  dismiss->setFocusPolicy (Qt::NoFocus);
  QObject::connect (dismiss, &QToolButton::clicked, toast, &QFrame::close);
  outer->addWidget (dismiss);

  position_toast (toast, parent);
  toast->raise ();
  toast->show ();

  QTimer::singleShot (3600, toast, [toast] () {
    if (toast != nullptr) toast->close ();
  });
  return true;
}
