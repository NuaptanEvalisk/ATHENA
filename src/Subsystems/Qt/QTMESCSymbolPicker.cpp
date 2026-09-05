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

#include "actor_transport.hpp"
#include "buffer_actor.hpp"
#include "file.hpp"
#include "QTMWidget.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"
#include "scheme_execution_context.hpp"

#include <QApplication>
#include <QAbstractItemView>
#include <QByteArray>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QFormLayout>
#include <QHeaderView>
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
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSet>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QVBoxLayout>
#include <algorithm>
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

static url
escape_symbol_builtin_config_url () {
  return url ("$ATHENA_PATH/misc/input/escape-symbol-picker.json");
}

static url
escape_symbol_user_config_url () {
  return url ("$ATHENA_HOME_PATH/misc/input/escape-symbol-picker.json");
}

static QString
escape_symbol_user_config_path () {
  return to_qstring (concretize (escape_symbol_user_config_url ()));
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

static bool
parse_escape_symbol_root (const string& text, QJsonObject& root,
                          QString* errorOut = nullptr) {
  c_string bytes (text);
  QJsonParseError error;
  QJsonDocument doc= QJsonDocument::fromJson (QByteArray (bytes, N(text)),
                                              &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject ()) {
    if (errorOut != nullptr)
      *errorOut= "Invalid JSON: " + error.errorString ();
    return false;
  }

  QJsonObject obj= doc.object ();
  if (!obj.value ("default_action").isString () ||
      !obj.value ("entries").isArray ()) {
    if (errorOut != nullptr)
      *errorOut= "The JSON root must contain default_action and entries.";
    return false;
  }
  root= obj;
  return true;
}

static bool
load_escape_symbol_json_root (url config, QJsonObject& root,
                              QString* errorOut = nullptr) {
  if (!exists (config)) {
    if (errorOut != nullptr)
      *errorOut= "Cannot read " + to_qstring (concretize (config));
    return false;
  }

  string text;
  if (load_string (config, text, false)) {
    if (errorOut != nullptr)
      *errorOut= "Cannot read " + to_qstring (concretize (config));
    return false;
  }
  return parse_escape_symbol_root (text, root, errorOut);
}

static bool
load_escape_symbol_editor_root (QJsonObject& root, QString* warningOut) {
  QString warning;
  if (load_escape_symbol_json_root (escape_symbol_user_config_url (), root,
                                    &warning))
    return true;
  if (warningOut != nullptr && !warning.startsWith ("Cannot read "))
    *warningOut= warning;
  return load_escape_symbol_json_root (escape_symbol_builtin_config_url (),
                                      root, warningOut);
}

static void
invalidate_escape_symbol_data () {
  esc_symbol_data& data= escape_symbol_data ();
  data.loaded= false;
  data.warned= false;
  data.default_action.clear ();
  data.entries.clear ();
}

void
initialize_escape_symbol_picker_data () {
  if (qt_defer_to_main_thread (initialize_escape_symbol_picker_data)) return;
  esc_symbol_data& data= escape_symbol_data ();
  if (data.loaded) return;
  data.loaded= true;
  data.default_action.clear ();
  data.entries.clear ();

  QJsonObject root;
  QString userError;
  if (!load_escape_symbol_json_root (escape_symbol_user_config_url (), root,
                                     &userError)) {
    if (!userError.startsWith ("Cannot read "))
      warn_escape_symbol_data (userError.toStdString ());
    QString builtinError;
    if (!load_escape_symbol_json_root (escape_symbol_builtin_config_url (),
                                       root, &builtinError)) {
      warn_escape_symbol_data (builtinError.toStdString ());
      return;
    }
  }

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

class QTMESCSymbolConfigurator : public QDialog {
public:
  explicit QTMESCSymbolConfigurator (QWidget* parent= nullptr)
    : QDialog (parent),
      defaultActionEdit (new QLineEdit (this)),
      table (new QTableWidget (this)) {
    setWindowTitle ("Quick Symbol Inserter");
    resize (900, 560);

    QVBoxLayout* layout= new QVBoxLayout (this);
    QFormLayout* form= new QFormLayout ();
    defaultActionEdit->setPlaceholderText ("Action inserted when no key is typed");
    form->addRow ("Default action:", defaultActionEdit);
    layout->addLayout (form);

    table->setColumnCount (5);
    table->setHorizontalHeaderLabels (
      QStringList () << "Key" << "Action" << "Preview"
                     << "Notation" << "Description");
    table->horizontalHeader ()->setSectionResizeMode (0, QHeaderView::ResizeToContents);
    table->horizontalHeader ()->setSectionResizeMode (1, QHeaderView::Stretch);
    table->horizontalHeader ()->setSectionResizeMode (2, QHeaderView::ResizeToContents);
    table->horizontalHeader ()->setSectionResizeMode (3, QHeaderView::Stretch);
    table->horizontalHeader ()->setSectionResizeMode (4, QHeaderView::Stretch);
    table->verticalHeader ()->setVisible (false);
    table->setSelectionBehavior (QAbstractItemView::SelectRows);
    table->setSelectionMode (QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors (true);
    layout->addWidget (table, 1);

    QHBoxLayout* tools= new QHBoxLayout ();
    QPushButton* addButton= new QPushButton ("Add", this);
    QPushButton* removeButton= new QPushButton ("Remove", this);
    QPushButton* upButton= new QPushButton ("Move up", this);
    QPushButton* downButton= new QPushButton ("Move down", this);
    QPushButton* resetButton= new QPushButton ("Restore bundled defaults", this);
    tools->addWidget (addButton);
    tools->addWidget (removeButton);
    tools->addWidget (upButton);
    tools->addWidget (downButton);
    tools->addStretch ();
    tools->addWidget (resetButton);
    layout->addLayout (tools);

    QDialogButtonBox* buttons= new QDialogButtonBox (
      QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    layout->addWidget (buttons);

    connect (addButton, &QPushButton::clicked,
             this, [this] () { addRow (); });
    connect (removeButton, &QPushButton::clicked,
             this, [this] () { removeSelectedRows (); });
    connect (upButton, &QPushButton::clicked,
             this, [this] () { moveSelectedRow (-1); });
    connect (downButton, &QPushButton::clicked,
             this, [this] () { moveSelectedRow (1); });
    connect (resetButton, &QPushButton::clicked,
             this, [this] () { restoreBundledDefaults (); });
    connect (buttons, &QDialogButtonBox::accepted,
             this, [this] () { saveAndAccept (); });
    connect (buttons, &QDialogButtonBox::rejected,
             this, &QDialog::reject);

    loadInitial ();
  }

private:
  QString cellText (int row, int col) const {
    QTableWidgetItem* item= table->item (row, col);
    return item == nullptr ? QString () : item->text ().trimmed ();
  }

  void setCellText (int row, int col, const QString& text) {
    QTableWidgetItem* item= new QTableWidgetItem (text);
    table->setItem (row, col, item);
  }

  void addRow (const QJsonObject& obj= QJsonObject ()) {
    int row= table->rowCount ();
    table->insertRow (row);
    setCellText (row, 0, obj.value ("key").toString ());
    setCellText (row, 1, obj.value ("action").toString ());
    setCellText (row, 2, obj.value ("preview").toString ());
    setCellText (row, 3, obj.value ("notation").toString ());
    setCellText (row, 4, obj.value ("description").toString ());
    table->setCurrentCell (row, 0);
  }

  void loadRoot (const QJsonObject& root) {
    defaultActionEdit->setText (root.value ("default_action").toString ());
    table->setRowCount (0);
    for (const QJsonValue& value: root.value ("entries").toArray ())
      if (value.isObject ()) addRow (value.toObject ());
    if (table->rowCount () > 0) table->setCurrentCell (0, 0);
  }

  void loadInitial () {
    QJsonObject root;
    QString warning;
    if (!load_escape_symbol_editor_root (root, &warning)) {
      QMessageBox::warning (this, "Quick Symbol Inserter", warning);
      root["default_action"]= "cdots";
      root["entries"]= QJsonArray ();
    }
    else if (!warning.isEmpty ()) {
      QMessageBox::warning (
        this, "Quick Symbol Inserter",
        "The user quick symbol inserter JSON is invalid; loaded bundled "
        "defaults instead.\n\n" + warning);
    }
    loadRoot (root);
  }

  void removeSelectedRows () {
    QList<QTableWidgetSelectionRange> ranges= table->selectedRanges ();
    if (ranges.isEmpty ()) return;
    int first= ranges.first ().topRow ();
    int last= ranges.first ().bottomRow ();
    for (int row= last; row >= first; row--) table->removeRow (row);
    if (table->rowCount () > 0)
      table->setCurrentCell (std::min (first, table->rowCount () - 1), 0);
  }

  void moveSelectedRow (int delta) {
    int row= table->currentRow ();
    int target= row + delta;
    if (row < 0 || target < 0 || target >= table->rowCount ()) return;

    QStringList values;
    for (int col=0; col<table->columnCount (); col++)
      values << cellText (row, col);
    QStringList targetValues;
    for (int col=0; col<table->columnCount (); col++)
      targetValues << cellText (target, col);
    for (int col=0; col<table->columnCount (); col++) {
      setCellText (target, col, values[col]);
      setCellText (row, col, targetValues[col]);
    }
    table->setCurrentCell (target, 0);
  }

  void restoreBundledDefaults () {
    if (QMessageBox::question (
          this, "Quick Symbol Inserter",
          "Replace the table with the bundled default symbols?") !=
        QMessageBox::Yes)
      return;
    QJsonObject root;
    QString error;
    if (!load_escape_symbol_json_root (escape_symbol_builtin_config_url (),
                                       root, &error)) {
      QMessageBox::warning (this, "Quick Symbol Inserter", error);
      return;
    }
    loadRoot (root);
  }

  bool rootFromTable (QJsonObject& root) {
    QString defaultAction= defaultActionEdit->text ().trimmed ();
    if (defaultAction.isEmpty ()) {
      QMessageBox::warning (this, "Quick Symbol Inserter",
                            "Default action cannot be empty.");
      return false;
    }

    QJsonArray entries;
    QSet<QString> keys;
    for (int row=0; row<table->rowCount (); row++) {
      QString key= cellText (row, 0);
      QString action= cellText (row, 1);
      if (key.isEmpty () && action.isEmpty () &&
          cellText (row, 2).isEmpty () && cellText (row, 3).isEmpty () &&
          cellText (row, 4).isEmpty ())
        continue;
      if (key.isEmpty () || action.isEmpty ()) {
        QMessageBox::warning (
          this, "Quick Symbol Inserter",
          QString ("Row %1 must have both Key and Action.").arg (row + 1));
        return false;
      }
      if (keys.contains (key)) {
        QMessageBox::warning (
          this, "Quick Symbol Inserter",
          QString ("Duplicate key: %1").arg (key));
        return false;
      }
      keys.insert (key);

      QJsonObject entry;
      entry["key"]= key;
      entry["action"]= action;
      entry["preview"]= cellText (row, 2);
      entry["notation"]= cellText (row, 3);
      entry["description"]= cellText (row, 4);
      entries.append (entry);
    }

    if (entries.isEmpty ()) {
      QMessageBox::warning (this, "Quick Symbol Inserter",
                            "At least one symbol entry is required.");
      return false;
    }

    root["default_action"]= defaultAction;
    root["entries"]= entries;
    return true;
  }

  void saveAndAccept () {
    QJsonObject root;
    if (!rootFromTable (root)) return;

    QString path= escape_symbol_user_config_path ();
    QDir dir (QFileInfo (path).absolutePath ());
    if (!dir.exists () && !dir.mkpath (".")) {
      QMessageBox::warning (this, "Quick Symbol Inserter",
                            "Could not create " + dir.absolutePath ());
      return;
    }

    QByteArray bytes= QJsonDocument (root).toJson (QJsonDocument::Indented);
    string out (bytes.constData (), bytes.size ());
    if (save_string (escape_symbol_user_config_url (), out, false)) {
      QMessageBox::warning (this, "Quick Symbol Inserter",
                            "Could not write " + path);
      return;
    }
    invalidate_escape_symbol_data ();
    accept ();
  }

  QLineEdit* defaultActionEdit;
  QTableWidget* table;
};

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

static string
run_escape_symbol_picker () {
  Q_ASSERT (QThread::currentThread () == qApp->thread ());
  initialize_escape_symbol_picker_data ();
  QTMESCSymbolPicker picker (QApplication::activeWindow ());
  if (picker.exec () != QDialog::Accepted) return "";
  return picker.selectedSymbol ();
}

string
escape_symbol_picker_dialog () {
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context != nullptr && context->actor_id != ATHENA_NO_ACTOR &&
      context->view_id != ATHENA_NO_VIEW) {
    const athena_actor_id actor= context->actor_id;
    const athena_view_id view= context->view_id;
    const SchemeCapabilitySet capabilities= context->capabilities;
    qt_post_to_main_thread ([actor, view, capabilities] {
      string action= run_escape_symbol_picker ();
      if (action == "") return;
      action.ensure_transferable ();
      athena_continuation_id id=
        actor_continuation_registry::instance ().store (
          [action= std::move (action)] () mutable {
            const auto* context= current_scheme_execution_context ();
            if (context != nullptr && context->editor != nullptr)
              (void) call ("escape-symbol-insert", object (std::move (action)));
          });
      if (!buffer_actor::submit_to (
            actor, actor_command_kind::run_native_continuation, view,
            ATHENA_NO_BLOB, ATHENA_NO_BLOB, capabilities, id))
        (void) actor_continuation_registry::instance ().discard (id);
    });
    // The Scheme caller returns now; insertion resumes on its original actor.
    return "";
  }
  return run_escape_symbol_picker ();
}

void
escape_symbol_configurator_show () {
  if (qt_defer_to_main_thread (escape_symbol_configurator_show)) return;
  QTMESCSymbolConfigurator dialog (QApplication::activeWindow ());
  dialog.exec ();
}
