/******************************************************************************
* MODULE     : QTMESCSymbolPicker.cpp
* DESCRIPTION: Mathematica-style ESC symbol picker for ATHENA
* COPYRIGHT  : (C) 2026 Nuaptan
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMESCSymbolPicker.hpp"

#include "QTMWidget.hpp"
#include "qt_utilities.hpp"

#include <QApplication>
#include <QCursor>
#include <QDialog>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScreen>
#include <QVBoxLayout>
#include <string>
#include <vector>

struct esc_symbol_entry {
  std::string key;
  std::string action;
  std::string preview;
  std::string notation;
  std::string description;
};

static QString
to_qstring_std (const std::string& s) {
  return QString::fromStdString (s);
}

static std::vector<esc_symbol_entry>
esc_symbol_entries () {
  std::vector<esc_symbol_entry> xs = {
    { "a",     "alpha",       "α",     "<alpha>",      "alpha" },
    { "b",     "beta",        "β",     "<beta>",       "beta" },
    { "q",     "theta",       "θ",     "<theta>",      "theta" },
    { "ii",    "mathi",       "ⅈ",     "<mathi>",      "imaginary unit" },
    { "ee",    "mathe",       "ⅇ",     "<mathe>",      "Euler constant" },
    { "oo",    "infty",       "∞",     "<infty>",      "infinity" },
    { "pd",    "partial",     "∂",     "<partial>",    "partial" },
    { "dd",    "mathd",       "ⅆ",     "<mathd>",      "differential d" },
    { "kk",    "bbb-k",       "𝕜",     "<bbb-k>",      "blackboard k" },
    { "dx",    "tree:dx",     "d/dx",  "d/dx",         "derivative with respect to x" },
    { "dt",    "tree:dt",     "d/dt",  "d/dt",         "derivative with respect to t" },
    { "dag",   "dagger",      "†",     "<dag>",        "dagger" },
    { "oc",    "#2103",       "℃",     "<#2103>",      "Celsius" },
    { "-1",    "tree:inv",    "^-1",   "^{-1}",        "inverse superscript" },
    { "op",    "tree:op",     "^op",   "^{op}",        "opposite superscript" },
    { "uconv", "Rightarrow",  "⇒",     "<Rightarrow>", "double right arrow" },
    { "11",    "bbb-1",       "𝟙",     "<bbb-1>",      "blackboard 1" },
    { "ds1",   "bbb-1",       "𝟙",     "<bbb-1>",      "blackboard 1" },
    { "id",    "tree:id",     "id",    "\\mathrm{id}", "identity" },
    { "ve",    "varepsilon",  "ε",     "<varepsilon>", "varepsilon" },
    { "vp",    "tree:varphi", "φ",     "<varphi>",     "varphi" },
    { "vq",    "vartheta",    "ϑ",     "<vartheta>",   "vartheta" },
    { "es",    "emptyset",    "∅",     "<emptyset>",   "empty set" }
  };

  for (char c= 'a'; c <= 'z'; c++) {
    static const char* names[] = {
      "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
      "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"
    };
    int i= c - 'a';
    std::string letter= names[i];
    xs.push_back ({ "ds" + std::string (1, c),
                    "bbb-" + letter,
                    letter,
                    "<bbb-" + letter + ">",
                    "blackboard " + letter });
  }
  return xs;
}

static QPoint
escape_picker_position (const QSize& size) {
  QPoint p;
  if (QTMWidget* widget= QTMWidget::getLastFocusedWidget ())
    p= widget->cursorGlobalPos ();
  else if (QWidget* active= QApplication::activeWindow ())
    p= active->mapToGlobal (active->rect ().center ());
  else
    p= QCursor::pos ();

  QRect r (p, size);
  QScreen* screen= QGuiApplication::screenAt (p);
  if (screen != nullptr) {
    QRect g= screen->availableGeometry ();
    if (r.right () > g.right ()) r.moveRight (g.right ());
    if (r.left () < g.left ()) r.moveLeft (g.left ());
    if (r.bottom () > g.bottom ()) r.moveBottom (g.bottom ());
    if (r.top () < g.top ()) r.moveTop (g.top ());
  }
  return r.topLeft ();
}

class QTMESCSymbolPicker : public QDialog {
public:
  explicit QTMESCSymbolPicker (QWidget* parent= nullptr)
    : QDialog (parent),
      searchEdit (new QLineEdit (this)),
      list (new QListWidget (this)),
      entries (esc_symbol_entries ()) {
    setWindowFlags (Qt::Popup | Qt::FramelessWindowHint);
    setAttribute (Qt::WA_DeleteOnClose, false);

    QVBoxLayout* layout= new QVBoxLayout (this);
    layout->setContentsMargins (8, 8, 8, 8);
    layout->setSpacing (6);

    QLabel* title= new QLabel ("Symbol", this);
    QFont titleFont= title->font ();
    titleFont.setBold (true);
    title->setFont (titleFont);
    layout->addWidget (title);

    searchEdit->setPlaceholderText ("Type a symbol key...");
    layout->addWidget (searchEdit);
    layout->addWidget (list);

    list->setFrameShape (QFrame::NoFrame);
    list->setSelectionMode (QAbstractItemView::SingleSelection);
    list->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);

    connect (searchEdit, &QLineEdit::textChanged,
             this, [this] () { refresh (); });
    connect (searchEdit, &QLineEdit::returnPressed,
             this, [this] () { acceptCurrent (); });
    connect (list, &QListWidget::itemDoubleClicked,
             this, [this] (QListWidgetItem*) { acceptCurrent (); });

    searchEdit->installEventFilter (this);
    list->installEventFilter (this);
    refresh ();
    resize (360, 320);
  }

  string selectedSymbol () const { return selected; }

protected:
  bool eventFilter (QObject* watched, QEvent* event) override {
    if (event->type () != QEvent::KeyPress)
      return QDialog::eventFilter (watched, event);
    QKeyEvent* key= static_cast<QKeyEvent*> (event);
    if (key->key () == Qt::Key_Escape) {
      acceptCurrent ();
      return true;
    }
    if (key->key () == Qt::Key_Return || key->key () == Qt::Key_Enter) {
      acceptCurrent ();
      return true;
    }
    if (key->key () == Qt::Key_Up || key->key () == Qt::Key_Down) {
      moveSelection (key->key () == Qt::Key_Up ? -1 : 1);
      return true;
    }
    return QDialog::eventFilter (watched, event);
  }

  void showEvent (QShowEvent* event) override {
    QDialog::showEvent (event);
    move (escape_picker_position (size ()));
    searchEdit->setFocus ();
    searchEdit->selectAll ();
  }

private:
  void refresh () {
    QString q= searchEdit->text ().trimmed ().toLower ();
    list->clear ();
    addMatchingItems (q, true);
    addMatchingItems (q, false);
    if (list->count () > 0) list->setCurrentRow (0);
  }

  void addMatchingItems (const QString& q, bool exact) {
    for (const esc_symbol_entry& e: entries) {
      QString key= to_qstring_std (e.key).toLower ();
      if (exact && q != key) continue;
      if (!exact && q == key) continue;
      QString haystack= QString ("%1 %2 %3 %4")
        .arg (to_qstring_std (e.key))
        .arg (to_qstring_std (e.notation))
        .arg (to_qstring_std (e.preview))
        .arg (to_qstring_std (e.description))
        .toLower ();
      if (!q.isEmpty () && !haystack.contains (q)) continue;
      QListWidgetItem* item= new QListWidgetItem (
        QString ("%1    %2    %3    %4")
          .arg (to_qstring_std (e.key))
          .arg (to_qstring_std (e.preview))
          .arg (to_qstring_std (e.notation))
          .arg (to_qstring_std (e.description)));
      item->setData (Qt::UserRole, to_qstring_std (e.action));
      list->addItem (item);
    }
  }

  void moveSelection (int delta) {
    int n= list->count ();
    if (n == 0) return;
    int row= list->currentRow ();
    if (row < 0) row= 0;
    row= (row + delta + n) % n;
    list->setCurrentRow (row);
  }

  void acceptCurrent () {
    if (searchEdit->text ().trimmed ().isEmpty ()) {
      selected= "cdots";
      accept ();
      return;
    }
    QListWidgetItem* item= list->currentItem ();
    if (item == nullptr) {
      reject ();
      return;
    }
    selected= from_qstring (item->data (Qt::UserRole).toString ());
    accept ();
  }

  QLineEdit* searchEdit;
  QListWidget* list;
  std::vector<esc_symbol_entry> entries;
  string selected;
};

string
escape_symbol_picker_dialog () {
  QTMESCSymbolPicker picker (QApplication::activeWindow ());
  if (picker.exec () != QDialog::Accepted) return "";
  return picker.selectedSymbol ();
}
