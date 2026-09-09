/******************************************************************************
* MODULE     : QTMDocumentSearchBar.cpp
* DESCRIPTION: Native in-document search UI
* COPYRIGHT  : (C) 2026 Nuaptan
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "QTMDocumentSearchBar.hpp"

#include "QTMApplication.hpp"
#include "QTMWidget.hpp"
#include "boot.hpp"
#include "new_view.hpp"
#include "native_interfaces.hpp"
#include "buffer_actor.hpp"
#include "tm_buffer.hpp"
#include "tm_window.hpp"
#include "qt_actor_widget.hpp"
#include "qt_utilities.hpp"

#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int search_bar_margin= 12;

QTMDocumentSearchBar*
barForCanvas (QTMWidget* canvas, bool create) {
  if (canvas == nullptr) return nullptr;
  QTMDocumentSearchBar* bar= nullptr;
  for (QObject* child: canvas->children ()) {
    bar= dynamic_cast<QTMDocumentSearchBar*> (child);
    if (bar != nullptr) break;
  }
  if (bar == nullptr && create) bar= new QTMDocumentSearchBar (canvas);
  return bar;
}

QTMDocumentSearchBar*
activeBar () {
  QTMWidget* canvas= QTMWidget::getLastFocusedWidget ();
  QTMDocumentSearchBar* bar= barForCanvas (canvas, false);
  return bar != nullptr && bar->isVisible () ? bar : nullptr;
}
}

QTMDocumentSearchBar::QTMDocumentSearchBar (QTMWidget* owner):
  QFrame (owner), canvas (owner), dispatchTimer (new QTimer (this)),
  queryEdit (new QLineEdit (this)),
  caseSensitive (new QCheckBox (tr ("Match case"), this)),
  resultLabel (new QLabel (this)) {
  setObjectName (QStringLiteral ("athenaDocumentSearchBar"));
  setFrameShape (QFrame::StyledPanel);
  setAutoFillBackground (true);
  if (auto* proxy= dynamic_cast<qt_actor_widget_rep*> (owner->tm_widget ())) {
    actorId= proxy->actor_id ();
    viewId= proxy->view_id ();
  }
  dispatchTimer->setSingleShot (true);
  connect (dispatchTimer, &QTimer::timeout, this,
           [this] { dispatchPending (); });

  auto* rows= new QVBoxLayout (this);
  rows->setContentsMargins (8, 5, 8, 5);
  rows->setSpacing (5);
  auto* layout= new QHBoxLayout ();
  rows->addLayout (layout);
  layout->setSpacing (5);

  auto* prompt= new QLabel (tr ("Find:"), this);
  prompt->setBuddy (queryEdit);
  queryEdit->setObjectName (QStringLiteral ("athenaDocumentSearchInput"));
  queryEdit->setClearButtonEnabled (true);
  queryEdit->setMinimumWidth (260);
  queryEdit->setPlaceholderText (tr ("Search this document"));
  caseSensitive->setChecked (
    get_preference ("case-insensitive-match", "off") != "on");

  QToolButton* first= makeButton (
    tmapp ()->icon_manager ().getIcon ("tm_search_first.xpm"),
    tr ("First match"));
  QToolButton* previous= makeButton (
    tmapp ()->icon_manager ().getIcon ("tm_search_previous.xpm"),
    tr ("Previous match (Shift+Enter)"));
  QToolButton* next= makeButton (
    tmapp ()->icon_manager ().getIcon ("tm_search_next.xpm"),
    tr ("Next match (Enter)"));
  QToolButton* last= makeButton (
    tmapp ()->icon_manager ().getIcon ("tm_search_last.xpm"),
    tr ("Last match"));
  QToolButton* close= makeButton (
    tmapp ()->icon_manager ().getIcon ("tm_close_tool.xpm"),
    tr ("Close search (Escape)"));

  resultLabel->setMinimumWidth (64);
  resultLabel->setAlignment (Qt::AlignCenter);

  layout->addWidget (prompt);
  layout->addWidget (queryEdit, 1);
  layout->addWidget (caseSensitive);
  layout->addWidget (resultLabel);
  layout->addWidget (first);
  layout->addWidget (previous);
  layout->addWidget (next);
  layout->addWidget (last);
  layout->addWidget (close);

  replaceRow= new QWidget (this);
  auto* replacementLayout= new QHBoxLayout (replaceRow);
  replacementLayout->setContentsMargins (0, 0, 0, 0);
  replacementLayout->setSpacing (5);
  replacementEdit= new QLineEdit (replaceRow);
  replacementEdit->setObjectName (QStringLiteral ("athenaDocumentReplaceInput"));
  replacementEdit->setClearButtonEnabled (true);
  auto* replacementPrompt= new QLabel (tr ("Replace with:"), replaceRow);
  replacementPrompt->setBuddy (replacementEdit);
  replacementLabel= new QLabel (replaceRow);
  replaceOne= makeButton (
    tmapp ()->icon_manager ().getIcon ("tm_replace_one.xpm"),
    tr ("Replace match (Enter in replacement field)"));
  replaceOne->setObjectName (QStringLiteral ("athenaDocumentReplaceOne"));
  replaceAll= makeButton (
    tmapp ()->icon_manager ().getIcon ("tm_replace_all.xpm"),
    tr ("Replace all matches (Ctrl+Enter)"));
  replaceAll->setObjectName (QStringLiteral ("athenaDocumentReplaceAll"));
  replacementLayout->addWidget (replacementPrompt);
  replacementLayout->addWidget (replacementEdit, 1);
  replacementLayout->addWidget (replacementLabel);
  replacementLayout->addWidget (replaceOne);
  replacementLayout->addWidget (replaceAll);
  rows->addWidget (replaceRow);
  replaceRow->hide ();
  replacementEdit->installEventFilter (this);
  connect (replaceOne, &QToolButton::clicked, this,
           [this] { replaceMatches (false); });
  connect (replaceAll, &QToolButton::clicked, this,
           [this] { replaceMatches (true); });

  connect (queryEdit, &QLineEdit::textChanged, this,
           [this] { updateSearch (); });
  connect (caseSensitive, &QCheckBox::toggled, this,
           [this] { updateSearch (); });
  connect (first, &QToolButton::clicked, this,
           [this] { navigate (false, true); });
  connect (previous, &QToolButton::clicked, this,
           [this] { navigate (false); });
  connect (next, &QToolButton::clicked, this,
           [this] { navigate (true); });
  connect (last, &QToolButton::clicked, this,
           [this] { navigate (true, true); });
  connect (close, &QToolButton::clicked, this,
           [this] { closeSearch (); });

  queryEdit->installEventFilter (this);
  caseSensitive->installEventFilter (this);
  owner->installEventFilter (this);
  hide ();
}

QToolButton*
QTMDocumentSearchBar::makeButton (const QIcon& icon,
                                  const QString& tooltip) {
  auto* button= new QToolButton (this);
  button->setAutoRaise (true);
  button->setIcon (icon);
  button->setToolTip (tooltip);
  button->installEventFilter (this);
  return button;
}

void
QTMDocumentSearchBar::open (bool replace) {
  if (actorId == ATHENA_NO_ACTOR) return;
  replaceRow->setVisible (replace);
  replacementLabel->clear ();
  layout ()->invalidate ();
  layout ()->activate ();
  show ();
  raise ();
  positionBar ();
  updateSearch ();
  QTimer::singleShot (0, queryEdit, [this] {
    if (!isVisible ()) return;
    queryEdit->setFocus (Qt::ShortcutFocusReason);
    queryEdit->selectAll ();
  });
}

void
QTMDocumentSearchBar::closeSearch () {
  dispatchTimer->stop ();
  pending.clear ();
  pending.push_back ({actor_command_kind::document_search_clear,
                     ++generation, {}});
  dispatchPending ();
  hide ();
  if (canvas != nullptr) canvas->setFocus (Qt::ShortcutFocusReason);
}

void
QTMDocumentSearchBar::updateSearch () {
  if (!isVisible () || actorId == ATHENA_NO_ACTOR) return;
  // At most one search is running; replace only adjacent unsent queries so
  // navigation and clear remain ordered against the query they apply to.
  if (!pending.empty () &&
      pending.back ().kind == actor_command_kind::document_search_update)
    pending.pop_back ();
  pending.push_back ({actor_command_kind::document_search_update,
                     ++generation, queryEdit->text (),
                     !caseSensitive->isChecked ()});
  resultLabel->setText (tr ("Searching..."));
  replacementLabel->clear ();
  replaceOne->setEnabled (false);
  replaceAll->setEnabled (false);
  dispatchTimer->start (120);
}

void
QTMDocumentSearchBar::replaceMatches (bool all) {
  if (!isVisible () || !replaceRow->isVisible () ||
      actorId == ATHENA_NO_ACTOR || queryEdit->text ().isEmpty ()) return;
  dispatchTimer->stop ();
  pending.push_back ({actor_command_kind::document_replace,
                     ++generation, replacementEdit->text (), all});
  replaceOne->setEnabled (false);
  replaceAll->setEnabled (false);
  replacementLabel->setText (tr ("Replacing..."));
  dispatchPending ();
}

void
QTMDocumentSearchBar::navigate (bool forward, bool extreme) {
  if (!isVisible () || actorId == ATHENA_NO_ACTOR) return;
  dispatchTimer->stop ();
  pending.push_back ({actor_command_kind::document_search_navigate,
                     ++generation, {}, forward, extreme});
  dispatchPending ();
}

void
QTMDocumentSearchBar::dispatchPending () {
  if (inFlight != 0 || pending.empty ()) return;
  tm_view view= concrete_runtime_view (viewId);
  if (view == nullptr || view->buf == nullptr || view->buf->actor == nullptr ||
      view->buf->actor->id () != actorId) {
    pending.clear ();
    return;
  }
  const Pending& request= pending.front ();
  athena_blob_id payload= ATHENA_NO_BLOB;
  if (request.kind == actor_command_kind::document_search_update ||
      request.kind == actor_command_kind::document_replace) {
    payload= actor_text_registry::instance ().store (
      from_qstring (request.text));
  }
  auto ticket= view->buf->actor->try_submit (
    request.kind, viewId, payload, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER,
    request.generation, request.flag1, request.flag2);
  if (!ticket) {
    if (payload != ATHENA_NO_BLOB)
      actor_text_registry::instance ().discard (payload);
    dispatchTimer->start (20);
    return;
  }
  inFlight= request.generation;
  pending.pop_front ();
}

void
QTMDocumentSearchBar::acceptState (QTMWidget* canvas, athena_view_id view,
                                  std::uint64_t serial, int current, int total,
                                  int replacementStatus) {
  auto* bar= barForCanvas (canvas, false);
  if (bar == nullptr || bar->viewId != view || bar->inFlight != serial) return;
  bar->inFlight= 0;
  if (serial == bar->generation && bar->isVisible ()) {
    bar->currentResult= current;
    bar->totalResults= total;
    bar->updateResultLabel ();
    bar->replaceOne->setEnabled (total > 0 && replacementStatus != 1);
    bar->replaceAll->setEnabled (total > 0 && replacementStatus != 1);
    if (replacementStatus == 1)
      bar->replacementLabel->setText (tr ("Read-only document"));
    else if (replacementStatus >= 2)
      bar->replacementLabel->setText (
        tr ("Replaced %1").arg (replacementStatus - 2));
  }
  if (!bar->dispatchTimer->isActive ()) bar->dispatchPending ();
}

void
QTMDocumentSearchBar::updateResultLabel () {
  resultLabel->setText (totalResults == 0
    ? tr ("No matches")
    : QStringLiteral ("%1 / %2").arg (currentResult).arg (totalResults));
}

void
QTMDocumentSearchBar::positionBar () {
  if (canvas == nullptr) return;
  int available= qMax (200, canvas->width () - 2 * search_bar_margin);
  int width= qMin (available, 900);
  QSize hint= sizeHint ();
  resize (width, hint.height ());
  move ((canvas->width () - this->width ()) / 2,
        canvas->height () - height () - search_bar_margin);
}

bool
QTMDocumentSearchBar::eventFilter (QObject* watched, QEvent* event) {
  if (watched == canvas && event->type () == QEvent::Resize) {
    positionBar ();
  }
  else if (watched != canvas && event->type () == QEvent::KeyPress) {
    auto* key= static_cast<QKeyEvent*> (event);
    if (key->key () == Qt::Key_Escape) {
      closeSearch ();
      return true;
    }
    if (key->key () == Qt::Key_Return || key->key () == Qt::Key_Enter) {
      if (replaceRow->isVisible () &&
          (watched == replacementEdit ||
           key->modifiers ().testFlag (Qt::ControlModifier)))
        replaceMatches (key->modifiers ().testFlag (Qt::ControlModifier));
      else navigate (!(key->modifiers () & Qt::ShiftModifier));
      return true;
    }
    if (key->key () == Qt::Key_F3) {
      navigate (!(key->modifiers () & Qt::ShiftModifier));
      return true;
    }
    if ((key->key () == Qt::Key_F || key->key () == Qt::Key_H) &&
        key->modifiers ().testFlag (Qt::ControlModifier)) {
      open (key->key () == Qt::Key_H);
      return true;
    }
  }
  return QFrame::eventFilter (watched, event);
}

void
QTMDocumentSearchBar::showForCurrentEditor (bool replace) {
  QTMWidget* canvas= QTMWidget::getLastFocusedWidget ();
  if (canvas == nullptr) return;
  barForCanvas (canvas, true)->open (replace);
}

void
QTMDocumentSearchBar::navigateCurrent (bool forward) {
  if (QTMDocumentSearchBar* bar= activeBar ()) bar->navigate (forward);
}

void
QTMDocumentSearchBar::closeCurrent () {
  if (QTMDocumentSearchBar* bar= activeBar ()) bar->closeSearch ();
}

void document_search_open () { QTMDocumentSearchBar::showForCurrentEditor (); }
void document_replace_open () {
  QTMDocumentSearchBar::showForCurrentEditor (true);
}
void document_search_next (bool forward) {
  if (forward)
    athena_dispatch_ui ([] { QTMDocumentSearchBar::navigateCurrent (true); });
  else
    athena_dispatch_ui ([] { QTMDocumentSearchBar::navigateCurrent (false); });
}
void document_search_close () { QTMDocumentSearchBar::closeCurrent (); }
