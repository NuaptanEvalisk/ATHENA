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

#include "file.hpp"
#include "QTMWidget.hpp"
#include "qt_utilities.hpp"

#include <QApplication>
#include <QByteArray>
#include <QCursor>
#include <QDialog>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
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

struct esc_symbol_data {
  bool loaded= false;
  bool warned= false;
  std::string default_action;
  std::vector<esc_symbol_entry> entries;
};

static esc_symbol_data&
escape_symbol_data () {
  static esc_symbol_data data;
  return data;
}

static QString
to_qstring_std (const std::string& s) {
  return QString::fromStdString (s);
}

static void
warn_escape_symbol_data (const std::string& message) {
  esc_symbol_data& data= escape_symbol_data ();
  if (data.warned) return;
  data.warned= true;
  std_warning << "escape symbol picker warning: "
              << string (message.c_str ()) << "\n";
}

static std::string
json_string (const QJsonObject& obj, const char* field) {
  QJsonValue value= obj.value (field);
  if (!value.isString ()) return "";
  return value.toString ().toStdString ();
}

void
initialize_escape_symbol_picker_data () {
  esc_symbol_data& data= escape_symbol_data ();
  if (data.loaded) return;
  data.loaded= true;

  string text;
  if (load_string (url ("$ATHENA_PATH/misc/input/escape-symbol-picker.json"),
                   text, false)) {
    warn_escape_symbol_data ("cannot read $ATHENA_PATH/misc/input/escape-symbol-picker.json");
    return;
  }

  c_string bytes (text);
  QJsonParseError error;
  QJsonDocument doc= QJsonDocument::fromJson (QByteArray (bytes, N(text)),
                                              &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject ()) {
    warn_escape_symbol_data ("invalid JSON in escape-symbol-picker.json");
    return;
  }

  QJsonObject root= doc.object ();
  data.default_action= json_string (root, "default_action");
  QJsonValue entries_value= root.value ("entries");
  if (data.default_action.empty () || !entries_value.isArray ()) {
    warn_escape_symbol_data ("escape-symbol-picker.json has invalid top-level fields");
    data.default_action.clear ();
    return;
  }

  std::vector<esc_symbol_entry> entries;
  for (const QJsonValue& value: entries_value.toArray ()) {
    if (!value.isObject ()) continue;
    QJsonObject obj= value.toObject ();
    esc_symbol_entry e {
      json_string (obj, "key"),
      json_string (obj, "action"),
      json_string (obj, "preview"),
      json_string (obj, "notation"),
      json_string (obj, "description")
    };
    if (e.key.empty () || e.action.empty ()) continue;
    entries.push_back (e);
  }

  if (entries.empty ()) {
    warn_escape_symbol_data ("escape-symbol-picker.json contains no valid entries");
    data.default_action.clear ();
    return;
  }
  data.entries= entries;
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
      entries (escape_symbol_data ().entries),
      defaultAction (escape_symbol_data ().default_action) {
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
    QString q= searchEdit->text ().trimmed ();
    list->clear ();
    addMatchingItems (q, true);
    addMatchingItems (q, false);
    if (list->count () > 0) list->setCurrentRow (0);
  }

  void addMatchingItems (const QString& q, bool exact) {
    QString qLower= q.toLower ();
    for (const esc_symbol_entry& e: entries) {
      QString key= to_qstring_std (e.key);
      QString keyLower= key.toLower ();
      if (exact && q != key) continue;
      if (!exact && qLower == keyLower) continue;
      QString haystack= QString ("%1 %2 %3 %4")
        .arg (to_qstring_std (e.key))
        .arg (to_qstring_std (e.notation))
        .arg (to_qstring_std (e.preview))
        .arg (to_qstring_std (e.description))
        .toLower ();
      if (!qLower.isEmpty () && !haystack.contains (qLower)) continue;
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
      selected= string (defaultAction.c_str ());
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
  std::string defaultAction;
  string selected;
};

string
escape_symbol_picker_dialog () {
  initialize_escape_symbol_picker_data ();
  QTMESCSymbolPicker picker (QApplication::activeWindow ());
  if (picker.exec () != QDialog::Accepted) return "";
  return picker.selectedSymbol ();
}
