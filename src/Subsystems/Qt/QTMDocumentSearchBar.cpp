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
#include "qt_utilities.hpp"

#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QToolButton>

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
  QFrame (owner), canvas (owner), queryEdit (new QLineEdit (this)),
  caseSensitive (new QCheckBox (tr ("Match case"), this)),
  resultLabel (new QLabel (this)) {
  setObjectName (QStringLiteral ("athenaDocumentSearchBar"));
  setFrameShape (QFrame::StyledPanel);
  setAutoFillBackground (true);

  auto* layout= new QHBoxLayout (this);
  layout->setContentsMargins (8, 5, 8, 5);
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
  return button;
}

void
QTMDocumentSearchBar::open (editor ed) {
  if (is_nil (ed)) return;
  if (!is_nil (searchEditor) && searchEditor != ed)
    searchEditor->document_search_clear ();
  searchEditor= ed;
  show ();
  raise ();
  positionBar ();
  updateSearch ();
  QTimer::singleShot (0, queryEdit, [this] {
    queryEdit->setFocus (Qt::ShortcutFocusReason);
    queryEdit->selectAll ();
  });
}

void
QTMDocumentSearchBar::closeSearch () {
  if (!is_nil (searchEditor)) searchEditor->document_search_clear ();
  hide ();
  if (canvas != nullptr) canvas->setFocus (Qt::ShortcutFocusReason);
}

void
QTMDocumentSearchBar::updateSearch () {
  if (is_nil (searchEditor)) return;
  QByteArray bytes= queryEdit->text ().toUtf8 ();
  string query (bytes.constData (), bytes.size ());
  searchEditor->document_search (tree (query), !caseSensitive->isChecked ());
  updateResultLabel ();
}

void
QTMDocumentSearchBar::navigate (bool forward, bool extreme) {
  if (is_nil (searchEditor)) return;
  searchEditor->document_search_navigate (forward, extreme);
  updateResultLabel ();
}

void
QTMDocumentSearchBar::updateResultLabel () {
  int current= is_nil (searchEditor) ? 0 : searchEditor->document_search_current ();
  int total= is_nil (searchEditor) ? 0 : searchEditor->document_search_total ();
  resultLabel->setText (total == 0
    ? tr ("No matches")
    : QStringLiteral ("%1 / %2").arg (current).arg (total));
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
  else if (watched == queryEdit && event->type () == QEvent::KeyPress) {
    auto* key= static_cast<QKeyEvent*> (event);
    if (key->key () == Qt::Key_Escape) {
      closeSearch ();
      return true;
    }
    if (key->key () == Qt::Key_Return || key->key () == Qt::Key_Enter) {
      navigate (!(key->modifiers () & Qt::ShiftModifier));
      return true;
    }
    if (key->key () == Qt::Key_F3) {
      navigate (!(key->modifiers () & Qt::ShiftModifier));
      return true;
    }
    if (key->key () == Qt::Key_F &&
        key->modifiers ().testFlag (Qt::ControlModifier)) {
      queryEdit->selectAll ();
      return true;
    }
  }
  return QFrame::eventFilter (watched, event);
}

void
QTMDocumentSearchBar::showForCurrentEditor () {
  QTMWidget* canvas= QTMWidget::getLastFocusedWidget ();
  if (canvas == nullptr) return;
  barForCanvas (canvas, true)->open (get_current_editor ());
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
void document_search_next (bool forward) {
  QTMDocumentSearchBar::navigateCurrent (forward);
}
void document_search_close () { QTMDocumentSearchBar::closeCurrent (); }
