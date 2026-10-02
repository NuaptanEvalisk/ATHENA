/******************************************************************************
* MODULE     : QTMPreferencesDialog.cpp
* DESCRIPTION: Native Qt preferences window for ATHENA
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMPreferencesDialog.hpp"
#include "QTMPluginUi.hpp"
#include "QTMAudmap.hpp"
#include "ATHENA/Features/athena_features.hpp"
#include "ATHENA/Data/materials_engine.hpp"
#include "QTMESCSymbolPicker.hpp"
#include "QTMDocumentPersistence.hpp"
#include "QTMFontSelector.hpp"
#include "QTMMainTabWindow.hpp"
#include "QTMWidget.hpp"
#include "QTMReverseHierarchyGraph.hpp"
#include "qt_tm_widget.hpp"
#include "QTMVaultInfoModel.hpp"
#include "QTMDelegationClient.hpp"
#include "QTMVaultSearchWorker.hpp"
#include "GoogleOAuth.hpp"
#include "GoogleTasksClient.hpp"

#include "rag_index.hpp"
#include "boot.hpp"
#include "font.hpp"
#include "file.hpp"
#include "namespaces.hpp"
#include "scheme.hpp"
#include "tm_ostream.hpp"
#include "qt_utilities.hpp"
#include "vault.hpp"

#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QShortcut>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QTextEdit>
#include <QTextCursor>
#include <QThread>
#include <QToolButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWizard>
#include <QWizardPage>

#include <cmath>
#include <atomic>
#include <functional>
#include <utility>
#include <vector>

namespace {

enum PreferenceSearchRole {
  preference_category_role= Qt::UserRole + 1,
  preference_tab_role,
  preference_target_role,
  preference_scroll_area_role
};

struct Choice {
  const char* value;
  const char* label;
};

using QStringChoice = std::pair<QString, QString>;

static QPointer<QTMPreferencesDialog> activePreferencesDialog;
static QPointer<QDialog> activePageSetupDialog;
static std::atomic<bool> activePreferencesDialogOpen {false};
static bool collectingPreferencesMetadata= false;
static const char* preferenceKeyProperty= "athenaPreferenceKey";

static void
mark_preference_control (QWidget* widget, const char* key) {
  widget->setProperty (preferenceKeyProperty, QString::fromUtf8 (key));
}

static QString
to_qstring_pref (string s) {
  return QString::fromUtf8 (as_charp (s), N(s));
}

static string
from_qstring_pref (const QString& s) {
  QByteArray bytes= s.toUtf8 ();
  return string (bytes.constData ());
}

static user_preference_ui_definition
preference_ui (const char* key) {
  user_preference_ui_definition definition;
  if (!get_user_preference_ui_definition (string (key), definition))
    qFatal ("Missing preference UI definition for %s", key);
  return definition;
}

static QString
preference_ui_label (const user_preference_ui_definition& definition) {
  return to_qstring_pref (definition.label) + ":";
}

static QString
pref (const char* key, const char* def= "default") {
  return to_qstring_pref (get_preference (string (key), string (def)));
}

static QString
pref (const char* key, const QString& def) {
  return to_qstring_pref (get_preference (string (key),
                                          from_qstring_pref (def)));
}

static QStringList
namespace_names_pref () {
  QStringList names;
  for (const athena_namespace_definition& ns: athena_namespaces_list ())
    names << to_qstring_pref (ns.name);
  names.removeDuplicates ();
  names.sort (Qt::CaseInsensitive);
  return names;
}

static bool
pref_on (const char* key) {
  return pref (key, "off") == "on";
}

static void
set_pref (const char* key, const QString& value) {
  set_preference (string (key), from_qstring_pref (value));
}

static void
set_bool_pref (const char* key, bool on) {
  set_preference (string (key), on? string ("on"): string ("off"));
}

static void
notify_restart () {
  QWidget* parent= activePreferencesDialog?
                   static_cast<QWidget*> (activePreferencesDialog.data ()):
                   QApplication::activeWindow ();
  QMessageBox::information (
    parent, QObject::tr ("Restart ATHENA"),
    QObject::tr ("Restart ATHENA in order to let the new setting take "
                 "effect."));
}

static QString
codex_home_path () {
  QString configured= pref ("codex home", "");
  if (!configured.isEmpty ()) return QDir::cleanPath (configured);
  QString athenaHome= qEnvironmentVariable ("ATHENA_HOME_PATH");
  if (athenaHome.isEmpty ())
    athenaHome= QDir::home ().filePath (".ATHENA");
  return QDir (athenaHome).filePath ("codex");
}

static QString
codex_executable () {
#ifdef Q_OS_WIN
  const QString name= "codex.exe";
#else
  const QString name= "codex";
#endif
  QString bundled= QDir (QCoreApplication::applicationDirPath ())
                     .filePath (name);
  if (QFileInfo (bundled).isExecutable ()) return bundled;
  return QStandardPaths::findExecutable (name);
}

static QProcessEnvironment
codex_environment (const QString& home) {
  QProcessEnvironment env= QProcessEnvironment::systemEnvironment ();
  env.insert ("CODEX_HOME", home);
  return env;
}

static void
show_codex_login (QWidget* parent, const QString& home,
                  const std::function<void ()>& finished) {
  const QString executable= codex_executable ();
  if (executable.isEmpty ()) {
    QMessageBox::warning (parent, QObject::tr ("OpenAI Codex login"),
                          QObject::tr ("The Codex executable was not found."));
    return;
  }
  QDir ().mkpath (home);
  QDialog* dialog= new QDialog (parent);
  dialog->setAttribute (Qt::WA_DeleteOnClose);
  dialog->setWindowTitle (QObject::tr ("OpenAI Codex login"));
  dialog->resize (680, 420);
  QVBoxLayout* layout= new QVBoxLayout (dialog);
  QLabel* status= new QLabel (
    QObject::tr ("Codex will open the OpenAI sign-in page in your browser."),
    dialog);
  status->setWordWrap (true);
  QTextEdit* output= new QTextEdit (dialog);
  output->setReadOnly (true);
  output->setFont (QFontDatabase::systemFont (QFontDatabase::FixedFont));
  QDialogButtonBox* buttons= new QDialogButtonBox (
    QDialogButtonBox::Cancel, dialog);
  layout->addWidget (status);
  layout->addWidget (output, 1);
  layout->addWidget (buttons);

  QProcess* process= new QProcess (dialog);
  process->setProcessEnvironment (codex_environment (home));
  process->setProcessChannelMode (QProcess::MergedChannels);
  QObject::connect (process, &QProcess::readyRead, dialog, [=] () {
    output->moveCursor (QTextCursor::End);
    output->insertPlainText (QString::fromUtf8 (process->readAll ()));
    output->moveCursor (QTextCursor::End);
  });
  QObject::connect (buttons, &QDialogButtonBox::rejected, dialog, [=] () {
    if (process->state () != QProcess::NotRunning) process->kill ();
    dialog->reject ();
  });
  QObject::connect (
    process, qOverload<int,QProcess::ExitStatus> (&QProcess::finished), dialog,
    [=] (int code, QProcess::ExitStatus exitStatus) {
      const bool ok= exitStatus == QProcess::NormalExit && code == 0;
      status->setText (ok? QObject::tr ("OpenAI Codex login completed."):
                            QObject::tr ("OpenAI Codex login failed."));
      buttons->setStandardButtons (QDialogButtonBox::Close);
      QObject::disconnect (buttons, nullptr, dialog, nullptr);
      QObject::connect (buttons, &QDialogButtonBox::rejected,
                        dialog, &QDialog::accept);
      if (finished) finished ();
    });
  process->start (executable, {"login"});
  dialog->show ();
}

static QString
current_vault_root_qstring () {
  if (!vault_active ()) return QString ();
  return to_qstring_pref (concretize (vault_get_root ()));
}

static bool
scheme_bool (const char* name) {
  return as_bool (call (name));
}

static QStringList
font_families () {
  QStringList result;
  array<string> xs= font_database_families ();
  for (int i=0; i<N(xs); i++) result << to_qstring_pref (xs[i]);
  result.removeDuplicates ();
  result.sort ();
  return result;
}

static QStringList
preferred_fonts () {
  QStringList result;
  list<string> xs= as_list_string (call ("get-user-preferred-fonts"));
  for (list<string> it= xs; !is_nil (it); it= it->next)
    result << to_qstring_pref (it->item);
  return result;
}

static QString
random_hex_token (int bytes) {
  static const char* hex= "0123456789abcdef";
  QString out;
  out.reserve (bytes * 2);
  for (int i=0; i<bytes; i++) {
    int value= QRandomGenerator::global ()->bounded (256);
    out.append (QChar (hex[(value >> 4) & 15]));
    out.append (QChar (hex[value & 15]));
  }
  return out;
}

static void
set_preferred_fonts (const QStringList& fonts) {
  array<object> xs;
  for (const QString& f: fonts) xs << object (from_qstring_pref (f));
  set_preference ("preferred fonts", object_to_string (as_list_object (xs)));
}

static void
button_set_color (QPushButton* button, const QString& value) {
  QString shown= value.isEmpty ()? QString ("default"): value;
  button->setText (shown);
  if (value.isEmpty () || value == "none" || value == "default") {
    button->setStyleSheet ("");
    return;
  }
  QColor c (value);
  if (!c.isValid ()) {
    button->setStyleSheet ("");
    return;
  }
  button->setStyleSheet (
    QString ("QPushButton { background: %1; color: %2; }")
      .arg (c.name (), c.lightness () < 128? "white": "black"));
}

static QIcon
pixmap_icon (const char* path) {
  url u= resolve (url (path));
  if (is_none (u)) return QIcon ();
  return QIcon (to_qstring_pref (concretize (u)));
}

static QIcon
category_icon (const QString& name) {
  auto libreoffice_icon= [] (const char* icon_name) {
    std::string path=
      "$ATHENA_PATH/misc/icons/libreoffice/colibre/cmd/32/";
    path += icon_name;
    path += ".svg";
    return pixmap_icon (path.c_str ());
  };
  if (name == "General")
    return libreoffice_icon ("configuredialog");
  if (name == "Keyboard")
    return libreoffice_icon ("assignmacro");
  if (name == "Editing")
    return libreoffice_icon ("editdoc");
  if (name == "Rendering")
    return libreoffice_icon ("printpreview");
  if (name == "Convert")
    return libreoffice_icon ("transformdialog");
  if (name == "Vault")
    return libreoffice_icon ("open");
  if (name == "Knowledge")
    return libreoffice_icon ("navigator");
  if (name == "Materials")
    return libreoffice_icon ("bibliographycomponent");
  if (name == "Plugins")
    return QIcon::fromTheme ("preferences-plugin", libreoffice_icon ("configuredialog"));
  if (name == "Other")
    return libreoffice_icon ("optionstreedialog");
  return QIcon ();
}

static QWidget*
make_scroll_page (QWidget* content) {
  QScrollArea* area= new QScrollArea;
  area->setWidget (content);
  area->setWidgetResizable (true);
  area->setFrameShape (QFrame::NoFrame);
  return area;
}

static QWidget*
make_page () {
  QWidget* page= new QWidget;
  QVBoxLayout* layout= new QVBoxLayout (page);
  layout->setContentsMargins (24, 18, 24, 18);
  layout->setSpacing (16);
  return page;
}

static QFormLayout*
add_section (QWidget* page, const QString& title) {
  QGroupBox* box= new QGroupBox (title, page);
  QFormLayout* form= new QFormLayout (box);
  form->setFieldGrowthPolicy (QFormLayout::ExpandingFieldsGrow);
  form->setRowWrapPolicy (QFormLayout::DontWrapRows);
  form->setLabelAlignment (Qt::AlignLeft | Qt::AlignVCenter);
  form->setFormAlignment (Qt::AlignTop);
  form->setHorizontalSpacing (24);
  form->setVerticalSpacing (10);
  page->layout ()->addWidget (box);
  return form;
}

static void
finish_page (QWidget* page) {
  page->layout ()->addItem (
    new QSpacerItem (1, 1, QSizePolicy::Minimum, QSizePolicy::Expanding));
}

static QLabel*
label (const QString& text) {
  QLabel* l= new QLabel (text);
  l->setWordWrap (true);
  l->setMinimumWidth (360);
  l->setMaximumWidth (460);
  l->setSizePolicy (QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
  return l;
}

static QCheckBox*
add_toggle (QFormLayout* form, const char* key) {
  const user_preference_ui_definition definition= preference_ui (key);
  if (definition.control != "toggle")
    qFatal ("Preference %s is not declared as a toggle", key);
  QCheckBox* box= new QCheckBox;
  mark_preference_control (box, key);
  box->setChecked (pref_on (key));
  QObject::connect (box, &QCheckBox::toggled, [key] (bool on) {
    set_bool_pref (key, on);
  });
  form->addRow (label (preference_ui_label (definition)), box);
  return box;
}

static QLineEdit*
add_line_edit (QFormLayout* form, const char* key) {
  const user_preference_ui_definition definition= preference_ui (key);
  if (definition.control != "text" && definition.control != "password")
    qFatal ("Preference %s is not declared as a text control", key);
  QLineEdit* edit= new QLineEdit;
  mark_preference_control (edit, key);
  edit->setText (pref (key, ""));
  if (definition.control == "password") edit->setEchoMode (QLineEdit::Password);
  QObject::connect (edit, &QLineEdit::editingFinished, [key, edit] () {
    set_pref (key, edit->text ());
  });
  form->addRow (label (preference_ui_label (definition)), edit);
  return edit;
}

static QComboBox*
add_combo (QFormLayout* form, const char* key) {
  const user_preference_ui_definition definition= preference_ui (key);
  if (definition.control != "choice")
    qFatal ("Preference %s is not declared as a choice control", key);
  QComboBox* combo= new QComboBox;
  mark_preference_control (combo, key);
  QString cur= pref (key, "");
  int curIndex= -1;
  for (size_t i=0; i<definition.choices.size (); i++) {
    const QString value= to_qstring_pref (definition.choices[i].value);
    combo->addItem (to_qstring_pref (definition.choices[i].label), value);
    if (cur == value) curIndex= (int) i;
  }
  if (curIndex < 0 && !cur.isEmpty ()) {
    combo->addItem (cur, cur);
    curIndex= combo->count () - 1;
  }
  if (curIndex >= 0) combo->setCurrentIndex (curIndex);
  QObject::connect (combo,
                    static_cast<void (QComboBox::*) (int)> (
                      &QComboBox::currentIndexChanged),
                    [key, combo, definition] (int index) {
    if (index >= 0) set_pref (key, combo->itemData (index).toString ());
    if (definition.restart) notify_restart ();
  });
  form->addRow (label (preference_ui_label (definition)), combo);
  return combo;
}

static QComboBox*
add_dynamic_combo (QFormLayout* form, const char* key,
                   const std::vector<Choice>& choices) {
  const user_preference_ui_definition definition= preference_ui (key);
  if (definition.control != "native" || definition.provider != "dynamic-choice")
    qFatal ("Preference %s is not declared as a dynamic choice", key);
  QComboBox* combo= new QComboBox;
  mark_preference_control (combo, key);
  QString cur= pref (key, "");
  int curIndex= -1;
  for (size_t i=0; i<choices.size (); i++) {
    combo->addItem (choices[i].label, QString (choices[i].value));
    if (cur == choices[i].value) curIndex= (int) i;
  }
  if (curIndex < 0 && !cur.isEmpty ()) {
    combo->addItem (cur, cur);
    curIndex= combo->count () - 1;
  }
  if (curIndex >= 0) combo->setCurrentIndex (curIndex);
  QObject::connect (
    combo, qOverload<int> (&QComboBox::currentIndexChanged),
    [key, combo, definition] (int index) {
      if (index < 0) return;
      set_pref (key, combo->itemData (index).toString ());
      if (definition.restart) notify_restart ();
    });
  form->addRow (label (preference_ui_label (definition)), combo);
  return combo;
}

static QComboBox*
add_dynamic_qstring_combo (QFormLayout* form, const char* key,
                           const std::vector<QStringChoice>& choices) {
  const user_preference_ui_definition definition= preference_ui (key);
  if (definition.control != "native" || definition.provider != "dynamic-choice")
    qFatal ("Preference %s is not declared as a dynamic choice", key);
  QComboBox* combo= new QComboBox;
  mark_preference_control (combo, key);
  QString cur= pref (key, "");
  int curIndex= -1;
  for (size_t i=0; i<choices.size (); i++) {
    combo->addItem (choices[i].second, choices[i].first);
    if (cur == choices[i].first) curIndex= (int) i;
  }
  if (curIndex < 0 && !cur.isEmpty ()) {
    combo->addItem (cur, cur);
    curIndex= combo->count () - 1;
  }
  if (curIndex >= 0) combo->setCurrentIndex (curIndex);
  QObject::connect (combo,
                    static_cast<void (QComboBox::*) (int)> (
                      &QComboBox::currentIndexChanged),
                    [key, combo, definition] (int index) {
    if (index < 0) return;
    set_pref (key, combo->itemData (index).toString ());
    if (definition.restart) notify_restart ();
  });
  form->addRow (label (preference_ui_label (definition)), combo);
  return combo;
}

static QPushButton*
add_color_button (QFormLayout* form, const char* key) {
  const user_preference_ui_definition definition= preference_ui (key);
  const bool optional= definition.control == "optional-color";
  if (definition.control != "color" && !optional)
    qFatal ("Preference %s is not declared as a color control", key);
  QPushButton* button= new QPushButton;
  button_set_color (button, pref (key, optional? "none": ""));
  QObject::connect (button, &QPushButton::clicked, [button, key, optional] () {
    QString old= pref (key, optional? "none": "");
    QColor initial= QColor (old);
    QColor c= QColorDialog::getColor (initial.isValid ()? initial: QColor (),
                                      button, "Choose color");
    if (!c.isValid ()) return;
    set_pref (key, c.name ());
    button_set_color (button, c.name ());
  });

  if (!optional) {
    mark_preference_control (button, key);
    form->addRow (label (preference_ui_label (definition)), button);
  }
  else {
    QWidget* row= new QWidget;
    QHBoxLayout* layout= new QHBoxLayout (row);
    layout->setContentsMargins (0, 0, 0, 0);
    layout->addWidget (button, 1);
    QPushButton* none= new QPushButton ("None", row);
    QObject::connect (none, &QPushButton::clicked, [button, key] () {
      set_pref (key, "none");
      button_set_color (button, "none");
    });
    layout->addWidget (none);
    mark_preference_control (row, key);
    form->addRow (label (preference_ui_label (definition)), row);
  }
  return button;
}

static QLineEdit*
add_path_chooser_row (QFormLayout* form, const QString& title,
                      const QString& value, QPushButton*& choose) {
  QWidget* row= new QWidget;
  QHBoxLayout* layout= new QHBoxLayout (row);
  layout->setContentsMargins (0, 0, 0, 0);
  QLineEdit* edit= new QLineEdit (value, row);
  choose= new QPushButton ("Choose...", row);
  layout->addWidget (edit, 1);
  layout->addWidget (choose);
  form->addRow (label (title), row);
  return edit;
}

static QWidget*
tabbed (std::initializer_list<std::pair<QString, QWidget*> > tabs) {
  QTabWidget* tab= new QTabWidget;
  for (auto& p: tabs)
    tab->addTab (make_scroll_page (p.second), p.first);
  return tab;
}

static std::vector<QStringChoice>
document_language_choices () {
  return {{"english", "English"}, {"french", "French"},
          {"german", "German"}, {"spanish", "Spanish"},
          {"italian", "Italian"}, {"portuguese", "Portuguese"},
          {"dutch", "Dutch"}, {"russian", "Russian"},
          {"ukrainian", "Ukrainian"}, {"chinese", "Chinese"},
          {"japanese", "Japanese"}, {"korean", "Korean"}};
}

struct EnunciationPreset {
  QString name;
  std::vector<QStringChoice> preferences;
};

static url
enunciation_presets_url () {
  return url ("$ATHENA_PATH/misc/preferences/enunciation-presets.json");
}

static void
warn_enunciation_presets (const QString& message) {
  std_warning << "enunciation preset warning: "
              << from_qstring_pref (message) << "\n";
}

static std::vector<EnunciationPreset>
load_enunciation_presets () {
  const url source= enunciation_presets_url ();
  string text;
  if (!exists (source) || load_string (source, text, false)) {
    warn_enunciation_presets (
      "Cannot read " + to_qstring_pref (concretize (source)));
    return {};
  }

  c_string bytes (text);
  QJsonParseError error;
  const QJsonDocument document= QJsonDocument::fromJson (
    QByteArray (bytes, N(text)), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject ()) {
    warn_enunciation_presets ("Invalid JSON: " + error.errorString ());
    return {};
  }

  const QJsonObject root= document.object ();
  if (root.value ("version").toInt (-1) != 1 ||
      !root.value ("presets").isArray ()) {
    warn_enunciation_presets (
      "enunciation-presets.json must contain version 1 and a presets array");
    return {};
  }

  std::vector<EnunciationPreset> result;
  QSet<QString> names;
  QSet<QString> expectedKeys;
  bool haveExpectedKeys= false;
  for (const QJsonValue& value: root.value ("presets").toArray ()) {
    if (!value.isObject ()) {
      warn_enunciation_presets ("Every preset must be an object");
      return {};
    }
    const QJsonObject object= value.toObject ();
    const QString name= object.value ("name").toString ().trimmed ();
    if (name.isEmpty () || names.contains (name) ||
        !object.value ("preferences").isObject ()) {
      warn_enunciation_presets (
        "Every preset needs a unique name and a preferences object");
      return {};
    }

    const QJsonObject preferences= object.value ("preferences").toObject ();
    if (preferences.isEmpty ()) {
      warn_enunciation_presets ("Preset " + name + " has no preferences");
      return {};
    }

    EnunciationPreset preset;
    preset.name= name;
    QSet<QString> keys;
    for (auto it= preferences.begin (); it != preferences.end (); ++it) {
      const QString key= it.key ();
      if (!key.startsWith ("vault ") || !key.endsWith (" color") ||
          !it.value ().isString ()) {
        warn_enunciation_presets (
          "Preset " + name + " contains an invalid color preference");
        return {};
      }
      const QString color= it.value ().toString ();
      if (!QColor (color).isValid ()) {
        warn_enunciation_presets (
          "Preset " + name + " contains an invalid color: " + color);
        return {};
      }
      keys.insert (key);
      preset.preferences.emplace_back (key, color);
    }

    if (!haveExpectedKeys) {
      expectedKeys= keys;
      haveExpectedKeys= true;
    }
    else if (keys != expectedKeys) {
      warn_enunciation_presets (
        "All enunciation presets must define the same preference keys");
      return {};
    }
    names.insert (name);
    result.push_back (std::move (preset));
  }

  if (result.empty ())
    warn_enunciation_presets ("enunciation-presets.json contains no presets");
  return result;
}

static const std::vector<EnunciationPreset>&
enunciation_presets () {
  static const std::vector<EnunciationPreset> presets=
    load_enunciation_presets ();
  return presets;
}

static QStringList
enunciation_preset_names () {
  QStringList names;
  for (const EnunciationPreset& preset: enunciation_presets ())
    names << preset.name;
  return names;
}

static QString
current_enunciation_preset () {
  const QString current= pref ("enunciation color preset");
  const QStringList names= enunciation_preset_names ();
  if (names.contains (current) || names.isEmpty ()) return current;
  return names.first ();
}

static void
apply_enunciation_preset (const QString& name) {
  for (const EnunciationPreset& preset: enunciation_presets ()) {
    if (name != preset.name) continue;
    for (const QStringChoice& preference: preset.preferences)
      set_preference (from_qstring_pref (preference.first),
                      from_qstring_pref (preference.second));
    return;
  }
}

static bool
file_converter_exists (const QString& from, const QString& to) {
  return as_bool (call ("file-converter-exists?", from_qstring_pref (from),
                        from_qstring_pref (to)));
}

static QString
scheme_string (const char* name, const char* fallback) {
  string value= as_string (call (name));
  if (value == "") value= string (fallback);
  return to_qstring_pref (value);
}

static void
add_page_setup_combo (QFormLayout* form, const QString& title,
                      const char* key,
                      const std::vector<QStringChoice>& choices,
                      const QString& def) {
  QComboBox* combo= new QComboBox;
  mark_preference_control (combo, key);
  QString cur= pref (key, def);
  int curIndex= -1;
  for (size_t i=0; i<choices.size (); i++) {
    combo->addItem (choices[i].second, choices[i].first);
    if (cur == choices[i].first) curIndex= (int) i;
  }
  if (curIndex < 0 && !cur.isEmpty ()) {
    combo->addItem (cur, cur);
    curIndex= combo->count () - 1;
  }
  if (curIndex >= 0) combo->setCurrentIndex (curIndex);
  QObject::connect (combo,
                    static_cast<void (QComboBox::*) (int)> (
                      &QComboBox::currentIndexChanged),
                    [key, combo] (int index) {
    if (index < 0) return;
    set_pref (key, combo->itemData (index).toString ());
  });
  form->addRow (label (title), combo);
}

} // namespace

QTMPreferencesDialog::QTMPreferencesDialog (QWidget* parent)
  : QDialog (parent), categoryList (new QListWidget (this)),
    pageStack (new QStackedWidget (this)), searchEdit (new QLineEdit (this)),
    searchCompleter (new QCompleter (this)),
    searchModel (new QStandardItemModel (this))
{
  setWindowTitle ("Preferences");
  resize (980, 680);
  setAttribute (Qt::WA_DeleteOnClose, true);

  QVBoxLayout* outer= new QVBoxLayout (this);
  outer->setContentsMargins (0, 0, 0, 0);
  outer->setSpacing (0);

  QFrame* header= new QFrame (this);
  header->setFrameShape (QFrame::NoFrame);
  QVBoxLayout* headerLayout= new QVBoxLayout (header);
  headerLayout->setContentsMargins (20, 16, 20, 12);
  QLabel* title= new QLabel ("Preferences", header);
  QFont f= title->font ();
  QFontInfo titleInfo (f);
  qt_set_font_size (f, max (1, (int) floor (titleInfo.pointSizeF () + 4.5)));
  f.setBold (true);
  title->setFont (f);
  headerLayout->addWidget (title);
  searchEdit->setPlaceholderText ("Search preferences...");
  searchEdit->setClearButtonEnabled (true);
  searchEdit->setMaximumWidth (620);
  searchCompleter->setModel (searchModel);
  searchCompleter->setCaseSensitivity (Qt::CaseInsensitive);
  searchCompleter->setFilterMode (Qt::MatchContains);
  searchCompleter->setCompletionMode (QCompleter::PopupCompletion);
  searchCompleter->setMaxVisibleItems (14);
  searchEdit->setCompleter (searchCompleter);
  headerLayout->addWidget (searchEdit);
  outer->addWidget (header);

  QFrame* line= new QFrame (this);
  line->setFrameShape (QFrame::HLine);
  outer->addWidget (line);

  QWidget* body= new QWidget (this);
  QHBoxLayout* bodyLayout= new QHBoxLayout (body);
  bodyLayout->setContentsMargins (0, 0, 0, 0);
  bodyLayout->setSpacing (0);

  categoryList->setFixedWidth (210);
  categoryList->setFrameShape (QFrame::NoFrame);
  categoryList->setIconSize (QSize (22, 22));
  categoryList->setSpacing (2);
  categoryList->setUniformItemSizes (true);
  categoryList->setStyleSheet (
    "QListWidget { background: palette(window); padding: 10px 8px; }"
    "QListWidget::item { padding: 9px 10px; border-radius: 6px; }"
    "QListWidget::item:selected { background: palette(highlight);"
    " color: palette(highlighted-text); }");

  bodyLayout->addWidget (categoryList);

  QFrame* vline= new QFrame (body);
  vline->setFrameShape (QFrame::VLine);
  bodyLayout->addWidget (vline);

  bodyLayout->addWidget (pageStack, 1);
  outer->addWidget (body, 1);

  addCategory ("General", buildGeneralPage ());
  addCategory ("Keyboard", buildKeyboardPage ());
  addCategory ("Editing", buildEditingPage ());
  addCategory ("Rendering", buildRenderingPage ());
  addCategory ("Convert", buildConversionPage ());
  for (const auto& category: buildVaultCategories ())
    addCategory (category.first, category.second);
  addCategory ("Plugins", tabbed ({{"Installed", qtm_plugin_preferences (qtm_plugin_manager ())}}));
  addCategory ("Other", buildOtherPage ());
  rebuildSearchIndex ();

  if (categoryList->count () > 0) categoryList->setCurrentRow (0);
  QObject::connect (categoryList, &QListWidget::currentRowChanged,
                    pageStack, &QStackedWidget::setCurrentIndex);
  QObject::connect (
    searchCompleter,
    qOverload<const QModelIndex&> (&QCompleter::activated), this,
    [this] (const QModelIndex& index) { navigateToSearchResult (index); });
  QShortcut* findShortcut= new QShortcut (QKeySequence::Find, this);
  QObject::connect (findShortcut, &QShortcut::activated, searchEdit,
                    [this] () {
    searchEdit->setFocus (Qt::ShortcutFocusReason);
    searchEdit->selectAll ();
  });
}

void
QTMPreferencesDialog::addCategory (const QString& name, QWidget* page) {
  QIcon icon= category_icon (name);
  QListWidgetItem* item= new QListWidgetItem (icon, name, categoryList);
  item->setSizeHint (QSize (180, 40));
  pageStack->addWidget (page);
}

QStringList
QTMPreferencesDialog::exportMetadata () const {
  QStringList result;
  QSet<QString> present;
  for (QWidget* widget: findChildren<QWidget*> ()) {
    const QString key= widget->property (preferenceKeyProperty).toString ();
    if (!key.isEmpty ()) present.insert (key);
  }
  for (const user_preference_ui_definition& definition:
       get_user_preference_ui_definitions ()) {
    const QString key= to_qstring_pref (definition.key);
    if (!present.contains (key)) continue;
    result << to_qstring_pref (definition.category)
           << to_qstring_pref (definition.tab)
           << to_qstring_pref (definition.section)
           << to_qstring_pref (definition.label)
           << key;
  }
  return result;
}

void
QTMPreferencesDialog::rebuildSearchIndex () {
  searchModel->clear ();
  for (int categoryIndex=0; categoryIndex<pageStack->count ();
       categoryIndex++) {
    const QString category= categoryList->item (categoryIndex)->text ();
    QTabWidget* tabs= qobject_cast<QTabWidget*> (
      pageStack->widget (categoryIndex));
    if (tabs == nullptr) continue;
    for (int tabIndex=0; tabIndex<tabs->count (); tabIndex++) {
      QScrollArea* scroll= qobject_cast<QScrollArea*> (
        tabs->widget (tabIndex));
      if (scroll == nullptr || scroll->widget () == nullptr) continue;
      const QString tab= tabs->tabText (tabIndex);
      const QList<QGroupBox*> sections=
        scroll->widget ()->findChildren<QGroupBox*> (
          QString (), Qt::FindDirectChildrenOnly);
      for (QGroupBox* section: sections) {
        const QString sectionTitle= section->title ();
        QFormLayout* form= qobject_cast<QFormLayout*> (section->layout ());
        if (form == nullptr) {
          const QString key=
            section->property (preferenceKeyProperty).toString ();
          user_preference_ui_definition definition;
          const bool catalogEntry= !key.isEmpty () &&
            get_user_preference_ui_definition (
              from_qstring_pref (key), definition);
          const QString setting= catalogEntry?
            to_qstring_pref (definition.label): sectionTitle;
          const QString path= catalogEntry?
            to_qstring_pref (definition.category) + " > " +
              to_qstring_pref (definition.tab) + " > " +
              to_qstring_pref (definition.section):
            category + " > " + tab;
          QStandardItem* item= new QStandardItem (
            setting + "  —  " + path);
          item->setData (categoryIndex, preference_category_role);
          item->setData (tabIndex, preference_tab_role);
          item->setData (QVariant::fromValue<QObject*> (section),
                         preference_target_role);
          item->setData (QVariant::fromValue<QObject*> (scroll),
                         preference_scroll_area_role);
          searchModel->appendRow (item);
          continue;
        }
        for (int row=0; row<form->rowCount (); row++) {
          QLayoutItem* labelItem= form->itemAt (row, QFormLayout::LabelRole);
          QLayoutItem* fieldItem= form->itemAt (row, QFormLayout::FieldRole);
          QLabel* settingLabel= labelItem == nullptr? nullptr:
            qobject_cast<QLabel*> (labelItem->widget ());
          if (settingLabel == nullptr) continue;
          QWidget* target= fieldItem == nullptr? settingLabel:
            fieldItem->widget ();
          if (target == nullptr) target= settingLabel;
          const QString key=
            target->property (preferenceKeyProperty).toString ();
          user_preference_ui_definition definition;
          const bool catalogEntry= !key.isEmpty () &&
            get_user_preference_ui_definition (
              from_qstring_pref (key), definition);
          QString setting= catalogEntry? to_qstring_pref (definition.label):
            settingLabel->text ().trimmed ();
          setting.remove ('&');
          if (setting.endsWith (':')) setting.chop (1);
          if (setting.isEmpty ()) continue;
          QString path;
          if (catalogEntry) {
            path= to_qstring_pref (definition.category) + " > " +
                  to_qstring_pref (definition.tab);
            const QString declaredSection= to_qstring_pref (definition.section);
            if (!declaredSection.isEmpty ()) path += " > " + declaredSection;
          }
          else {
            path= category + " > " + tab;
            if (!sectionTitle.isEmpty ()) path += " > " + sectionTitle;
          }
          QStandardItem* item= new QStandardItem (
            setting + "  —  " + path);
          item->setToolTip (path);
          item->setData (categoryIndex, preference_category_role);
          item->setData (tabIndex, preference_tab_role);
          item->setData (QVariant::fromValue<QObject*> (target),
                         preference_target_role);
          item->setData (QVariant::fromValue<QObject*> (scroll),
                         preference_scroll_area_role);
          searchModel->appendRow (item);
        }
      }
    }
  }
}

void
QTMPreferencesDialog::navigateToSearchResult (const QModelIndex& index) {
  if (!index.isValid ()) return;
  const int categoryIndex= index.data (preference_category_role).toInt ();
  const int tabIndex= index.data (preference_tab_role).toInt ();
  QObject* targetObject=
    index.data (preference_target_role).value<QObject*> ();
  QObject* scrollObject=
    index.data (preference_scroll_area_role).value<QObject*> ();
  QWidget* target= qobject_cast<QWidget*> (targetObject);
  QScrollArea* scroll= qobject_cast<QScrollArea*> (scrollObject);
  if (categoryIndex < 0 || categoryIndex >= pageStack->count ()) return;
  categoryList->setCurrentRow (categoryIndex);
  QTabWidget* tabs= qobject_cast<QTabWidget*> (
    pageStack->widget (categoryIndex));
  if (tabs != nullptr && tabIndex >= 0 && tabIndex < tabs->count ())
    tabs->setCurrentIndex (tabIndex);
  QPointer<QWidget> guardedTarget= target;
  QPointer<QScrollArea> guardedScroll= scroll;
  QTimer::singleShot (0, this, [guardedTarget, guardedScroll] () {
    if (guardedTarget == nullptr || guardedScroll == nullptr) return;
    guardedScroll->ensureWidgetVisible (guardedTarget, 32, 32);
    QWidget* focusTarget= guardedTarget;
    if (focusTarget->focusPolicy () == Qt::NoFocus) {
      const QList<QWidget*> children= focusTarget->findChildren<QWidget*> ();
      for (QWidget* child: children)
        if (child->focusPolicy () != Qt::NoFocus) {
          focusTarget= child;
          break;
        }
    }
    if (focusTarget->focusPolicy () != Qt::NoFocus)
      focusTarget->setFocus (Qt::ShortcutFocusReason);
  });
}

QWidget*
QTMPreferencesDialog::buildGeneralPage () {
  QWidget* basic= make_page ();
  QFormLayout* basicForm= add_section (basic, "Basic");
  add_toggle (basicForm, "check for updates");
  add_toggle (basicForm, "remember ads panes layout");
  QCheckBox* middleClickAdsTabs=
    add_toggle (basicForm, "middle click closes ads tab");
  QObject::connect (middleClickAdsTabs, &QCheckBox::toggled,
                    [] () { qtm_apply_ads_tab_close_preferences (); });
  QComboBox* saveMode= add_combo (basicForm, "document save mode");
  QComboBox* realtimeInterval= add_combo (basicForm, "realtime save interval");
  QObject::connect (saveMode, qOverload<int> (&QComboBox::currentIndexChanged),
                    [] { qtm_document_persistence_preferences_changed (); });
  QObject::connect (realtimeInterval,
                    qOverload<int> (&QComboBox::currentIndexChanged),
                    [] { qtm_document_persistence_preferences_changed (); });
  add_combo (basicForm, "autosave");
  add_toggle (basicForm, "case-insensitive-match");

  QFormLayout* historyForm= add_section (basic, "Document history");
  add_toggle (historyForm, "document history manual save");
  add_combo (historyForm, "document history interval");
  add_combo (historyForm, "document history preservation");
  finish_page (basic);

  QWidget* appearance= make_page ();
  QFormLayout* appearanceForm= add_section (appearance, "Appearance");
  add_combo (appearanceForm, "look and feel");
  add_toggle (appearanceForm, "text toolbar");
  QCheckBox* hideToolbars=
    add_toggle (appearanceForm, "hide toolbars when not using them");
  QObject::connect (hideToolbars, &QCheckBox::toggled,
                    [] () {
                      qt_tm_widget_rep::refreshAllToolbarPreferences ();
                    });
  QCheckBox* blinkingCursor=
    add_toggle (appearanceForm, "blinking cursor");
  QObject::connect (blinkingCursor, &QCheckBox::toggled,
                    [] () { QTMWidget::refreshAllCursorBlinking (); });
  add_toggle (appearanceForm, "inertial scrolling");
  add_line_edit (appearanceForm, "inertial scrolling friction");
  add_line_edit (appearanceForm, "inertial scrolling sensitivity");
  add_toggle (appearanceForm, "gui:print dialogue");
  add_toggle (appearanceForm, "disable texmacs window positioning");
  add_toggle (appearanceForm, "gui:live-statistics");
  add_line_edit (appearanceForm, "gui:live-statistics-format");
  finish_page (appearance);

  QWidget* fonts= make_page ();
  QFormLayout* styling= add_section (fonts, "Styling");
  add_toggle (styling, "show font substitution warning");

  QGroupBox* preferredBox= new QGroupBox ("Preferred fonts", fonts);
  mark_preference_control (preferredBox, "preferred fonts");
  QVBoxLayout* preferredLayout= new QVBoxLayout (preferredBox);
  QListWidget* fontList= new QListWidget (preferredBox);
  preferredLayout->addWidget (fontList);
  auto refreshFonts= [fontList] () {
    fontList->clear ();
    QStringList fonts= preferred_fonts ();
    for (const QString& font: fonts) fontList->addItem (font);
    if (fonts.isEmpty ()) fontList->addItem ("No preferred fonts added yet.");
  };
  refreshFonts ();
  QHBoxLayout* fontButtons= new QHBoxLayout;
  QPushButton* addFont= new QPushButton ("Add font", preferredBox);
  QPushButton* removeFont= new QPushButton ("Remove selected", preferredBox);
  fontButtons->addWidget (addFont);
  fontButtons->addWidget (removeFont);
  fontButtons->addStretch (1);
  preferredLayout->addLayout (fontButtons);
  QObject::connect (addFont, &QPushButton::clicked, [refreshFonts, fontList] () {
    QStringList families= font_families ();
    bool ok= false;
    QString selected= QInputDialog::getItem (
      fontList, "Add font", "Font:", families, 0, true, &ok);
    if (!ok || selected.trimmed ().isEmpty ()) return;
    QStringList fonts= preferred_fonts ();
    if (!fonts.contains (selected)) fonts << selected;
    set_preferred_fonts (fonts);
    refreshFonts ();
  });
  QObject::connect (removeFont, &QPushButton::clicked,
                    [refreshFonts, fontList] () {
    QListWidgetItem* item= fontList->currentItem ();
    if (item == nullptr) return;
    QString selected= item->text ();
    QStringList fonts= preferred_fonts ();
    fonts.removeAll (selected);
    set_preferred_fonts (fonts);
    refreshFonts ();
  });
  fonts->layout ()->addWidget (preferredBox);

  QFormLayout* maintenance= add_section (fonts, "Maintenance");
  QPushButton* scan= new QPushButton ("Refresh system fonts");
  QObject::connect (scan, &QPushButton::clicked, [] () {
    (void) call ("scan-disk-for-fonts");
  });
  maintenance->addRow (label ("System font catalog:"), scan);
  QPushButton* clear= new QPushButton ("Clear font cache");
  QObject::connect (clear, &QPushButton::clicked, [] () {
    (void) call ("clear-font-cache");
  });
  maintenance->addRow (label ("Clear local font cache:"), clear);
  finish_page (fonts);

  return tabbed ({{"Basic", basic}, {"Appearance", appearance},
                  {"Fonts", fonts}});
}

QWidget*
QTMPreferencesDialog::buildKeyboardPage () {
  QWidget* input= make_page ();
  QFormLayout* form= add_section (input, "Input");
  add_combo (form, "text spacebar");
  add_combo (form, "math spacebar");
  add_combo (form, "automatic quotes");
  add_combo (form, "automatic brackets");
  QPushButton* shortcuts= new QPushButton ("Edit keyboard shortcuts");
  QObject::connect (shortcuts, &QPushButton::clicked, [] () {
    (void) call ("open-shortcuts-editor", string (""), string (""));
  });
  form->addRow (label ("Advanced settings:"), shortcuts);
  finish_page (input);

  QWidget* remote= make_page ();
  QFormLayout* r= add_section (remote, "Remote Control");
  add_combo (r, "ir-left");
  add_combo (r, "ir-right");
  add_combo (r, "ir-up");
  add_combo (r, "ir-down");
  add_combo (r, "ir-center");
  add_combo (r, "ir-play");
  add_combo (r, "ir-pause");
  add_combo (r, "ir-menu");
  finish_page (remote);

  return tabbed ({{"Input", input}, {"Remote Control", remote}});
}

QWidget*
QTMPreferencesDialog::buildEditingPage () {
  QWidget* math= make_page ();
  QFormLayout* mk= add_section (math, "Math keyboard");
  add_toggle (mk, "automatic invisible");
  add_toggle (mk, "manual insert missing invisible");
  add_toggle (mk, "manual homoglyph correct");
  QFormLayout* qs= add_section (math, "Quick symbol inserter");
  QPushButton* editEscSymbols= new QPushButton ("Edit symbols");
  QObject::connect (editEscSymbols, &QPushButton::clicked, [] () {
    escape_symbol_configurator_show ();
  });
  qs->addRow (label ("ESC quick inserter:"), editEscSymbols);
  QFormLayout* mh= add_section (math, "Math hints and semantics");
  add_toggle (mh, "semantic editing");
  add_toggle (mh, "semantic selections");
  add_toggle (mh, "semantic focus");
  finish_page (math);

  QWidget* programming= make_page ();
  QFormLayout* p= add_section (programming, "Programming");
  add_toggle (p, "prog:highlight brackets");
  add_toggle (p, "prog:automatic brackets");
  add_toggle (p, "prog:select brackets");
  finish_page (programming);

  QWidget* text= make_page ();
  QFormLayout* t= add_section (text, "Text");
  add_toggle (t, "heading word counts");
  add_toggle (t, "live spell checking");
  add_toggle (t, "realtime text autocompletion");
  add_combo (t, "text autocompletion sorting");
  add_combo (t, "text autocompletion accept key");
  add_toggle (t, "disable unix primary selection");
  add_combo (t, "document update times");
  add_dynamic_qstring_combo (t, "custom dictionary import language", document_language_choices ());
  QPushButton* import= new QPushButton ("Import");
  QObject::connect (import, &QPushButton::clicked, [] () {
    (void) call ("spell-live-import-custom-dictionary-from-preferences");
  });
  t->addRow (label ("Custom dictionary:"), import);
  finish_page (text);

  QWidget* source= make_page ();
  QFormLayout* s= add_section (source, "Source tree");
  add_combo (s, "source tree style");
  add_combo (s, "source tree special rendering");
  add_combo (s, "source tree compactification");
  add_combo (s, "source tree closing style");
  finish_page (source);

  QWidget* importer= make_page ();
  QFormLayout* i= add_section (importer, "Formula Importer");
  add_toggle (i, "latex->texmacs:matrix-recognition");
  add_toggle (i, "latex->texmacs:align-to-aligned");
  add_toggle (i, "latex->texmacs:aligned-to-eqnarray");
  add_toggle (i, "latex->texmacs:operator-d-is-differential");
  add_toggle (i, "latex->texmacs:roman-d-is-differential");
  add_toggle (i, "latex->texmacs:text-d-is-differential");
  add_toggle (i, "latex->texmacs:parse-bbbk");
  add_toggle (i, "latex->texmacs:parse-bbbi-as-mathi");
  add_toggle (i, "latex->texmacs:text-operators");
  add_toggle (i, "latex->texmacs:intelligent-formula-cleaner");
  add_line_edit (i, "latex->texmacs:intelligent-formula-cleaner-model");
  finish_page (importer);

  return tabbed ({{"Maths", math}, {"Programming", programming},
                  {"Text", text}, {"Source", source},
                  {"Formula Importer", importer}});
}

QWidget*
QTMPreferencesDialog::buildRenderingPage () {
  QWidget* components= make_page ();
  QFormLayout* c= add_section (components, "Components and Layout");
  add_combo (c, "vault labels mode");
  add_toggle (c, "new style page breaking");
  add_toggle (c, "render solution in smaller font");
  add_toggle (c, "number solutions");
  finish_page (components);

  QWidget* documentColors= make_page ();
  QFormLayout* dc= add_section (documentColors, "Document Colors");
  add_color_button (dc, "gui cursor color");
  add_color_button (dc, "gui selection color");
  add_color_button (dc, "gui focus color");
  add_combo (dc, "gui focus border width");
  add_color_button (dc, "locus-color");
  add_color_button (dc, "visited-color");
  add_toggle (dc, "enable radioactive links");
  add_color_button (dc, "radioactive-link-color");
  add_toggle (dc, "override white document background");
  add_color_button (dc, "white document background override color");
  add_color_button (dc, "vault transclusion color");
  add_toggle (dc, "experimental alpha");
  finish_page (documentColors);

  QWidget* misc= make_page ();
  QFormLayout* m= add_section (misc, "Misc");
  add_combo (m, "default cjk language");
  add_toggle (m, "persistent fit width");
  QFormLayout* toc= add_section (misc, "Table of Contents");
  add_toggle (toc, "fold table of contents in reflow");
  QFormLayout* graphs= add_section (misc, "Graphs");
  QCheckBox* elasticGraphs= add_toggle (graphs, "interactive elastic graphs");
  QObject::connect (elasticGraphs, &QCheckBox::toggled, [] () {
    hierarchy_graph_interactivity_changed ();
  });
  finish_page (misc);

  QWidget* colors= make_page ();
  QFormLayout* preset= add_section (colors, "Presets");
  QStringList presets= enunciation_preset_names ();
  QComboBox* presetCombo= new QComboBox;
  presetCombo->addItems (presets);
  QString currentPreset= current_enunciation_preset ();
  int presetIndex= presetCombo->findText (currentPreset);
  if (presetIndex >= 0) presetCombo->setCurrentIndex (presetIndex);
  QObject::connect (presetCombo,
                    static_cast<void (QComboBox::*) (int)> (
                      &QComboBox::currentIndexChanged),
                    [presetCombo] (int index) {
    if (index >= 0) set_pref ("enunciation color preset",
                              presetCombo->itemText (index));
  });
  QPushButton* apply= new QPushButton ("Apply");
  QObject::connect (apply, &QPushButton::clicked, [presetCombo] () {
    apply_enunciation_preset (presetCombo->currentText ());
  });
  QWidget* presetRow= new QWidget;
  mark_preference_control (presetRow, "enunciation color preset");
  QHBoxLayout* presetLayout= new QHBoxLayout (presetRow);
  presetLayout->setContentsMargins (0, 0, 0, 0);
  presetLayout->addWidget (presetCombo, 1);
  presetLayout->addWidget (apply);
  preset->addRow (label ("Preset:"), presetRow);

  QFormLayout* en= add_section (colors, "Enunciations");
  add_color_button (en, "vault theorem color");
  add_color_button (en, "vault lemma color");
  add_color_button (en, "vault corollary color");
  add_color_button (en, "vault proposition color");
  add_color_button (en, "vault axiom color");
  add_color_button (en, "vault definition color");
  add_color_button (en, "vault notation color");
  add_color_button (en, "vault convention color");
  add_color_button (en, "vault conjecture color");
  add_color_button (en, "vault law color");

  QFormLayout* rem= add_section (colors, "Remarks and notes");
  add_color_button (rem, "vault remark color");
  add_color_button (rem, "vault note color");
  add_color_button (rem, "vault example color");
  add_color_button (rem, "vault warning color");
  add_color_button (rem, "vault disambiguation color");
  add_color_button (rem, "vault acknowledgments color");

  QFormLayout* ex= add_section (colors, "Exercises and proofs");
  add_color_button (ex, "vault exercise color");
  add_color_button (ex, "vault problem color");
  add_color_button (ex, "vault question color");
  add_color_button (ex, "vault solution color");
  add_color_button (ex, "vault answer color");
  add_color_button (ex, "vault proof color");
  add_color_button (ex, "vault proof alternative color");
  add_color_button (ex, "vault proof standard color");
  finish_page (colors);

  return tabbed ({{"Components and Layout", components},
                  {"Document Colors", documentColors},
                  {"Misc", misc},
                  {"Enunciation Colors", colors}});
}

QWidget*
QTMPreferencesDialog::buildConversionPage () {
  QWidget* html= make_page ();
  QFormLayout* h1= add_section (html, "ATHENA → HTML");
  add_toggle (h1, "texmacs->html:css");
  QCheckBox* mathjax= add_toggle (h1, "texmacs->html:mathjax");
  QCheckBox* images= add_toggle (h1, "texmacs->html:images");
  auto exclusiveHtml= [mathjax, images] (QCheckBox* active,
                                         const char* key, bool on) {
    set_bool_pref (key, on);
    if (!on) return;
    for (QCheckBox* box: {mathjax, images}) {
      if (box == active) continue;
      QSignalBlocker block (box);
      box->setChecked (false);
    }
    if (active != mathjax) set_bool_pref ("texmacs->html:mathjax", false);
    if (active != images) set_bool_pref ("texmacs->html:images", false);
  };
  QObject::connect (mathjax, &QCheckBox::toggled,
                    [=] (bool on) { exclusiveHtml (mathjax,
                                                   "texmacs->html:mathjax",
                                                   on); });
  QObject::connect (images, &QCheckBox::toggled,
                    [=] (bool on) { exclusiveHtml (images,
                                                   "texmacs->html:images",
                                                   on); });
  QFormLayout* h2= add_section (html, "HTML → ATHENA");
  add_toggle (h2, "mathml->texmacs:latex-annotations");
  finish_page (html);

  QWidget* latex= make_page ();
  QFormLayout* l1= add_section (latex, "LaTeX → ATHENA");
  add_toggle (l1, "latex->texmacs:fallback-on-pictures");
  QFormLayout* l2= add_section (latex, "ATHENA → LaTeX");
  add_toggle (l2, "texmacs->latex:replace-style");
  add_toggle (l2, "texmacs->latex:expand-macros");
  add_toggle (l2, "texmacs->latex:expand-user-macros");
  add_toggle (l2, "texmacs->latex:use-macros");
  add_combo (l2, "texmacs->latex:encoding");
  QFormLayout* l3= add_section (latex, "Conservative conversion options");
  QCheckBox* sourceTracking= new QCheckBox;
  mark_preference_control (sourceTracking,
                           "latex->texmacs:source-tracking");
  sourceTracking->setChecked (pref_on ("latex->texmacs:source-tracking") ||
                              pref_on ("texmacs->latex:source-tracking"));
  QObject::connect (sourceTracking, &QCheckBox::toggled, [] (bool on) {
    set_bool_pref ("latex->texmacs:source-tracking", on);
    set_bool_pref ("texmacs->latex:source-tracking", on);
  });
  l3->addRow (label ("Keep track of source code:"), sourceTracking);
  QCheckBox* conservative= new QCheckBox;
  mark_preference_control (conservative, "latex->texmacs:conservative");
  conservative->setChecked (pref_on ("latex->texmacs:conservative") &&
                            pref_on ("texmacs->latex:conservative"));
  QObject::connect (conservative, &QCheckBox::toggled, [] (bool on) {
    set_bool_pref ("latex->texmacs:conservative", on);
    set_bool_pref ("texmacs->latex:conservative", on);
  });
  l3->addRow (label ("Only convert changes with respect to tracked version:"),
              conservative);
  add_toggle (l3, "latex->texmacs:transparent-source-tracking");
  add_toggle (l3, "texmacs->latex:attach-tracking-info");
  finish_page (latex);

  QWidget* verbatim= make_page ();
  QFormLayout* v1= add_section (verbatim, "ATHENA → Verbatim");
  add_toggle (v1, "texmacs->verbatim:wrap");
  if (get_preference ("texmacs->verbatim:encoding") == "cork")
    set_preference ("texmacs->verbatim:encoding", "utf-8");
  add_combo (v1, "texmacs->verbatim:encoding");
  QFormLayout* v2= add_section (verbatim, "Verbatim → ATHENA");
  add_toggle (v2, "verbatim->texmacs:wrap");
  if (get_preference ("verbatim->texmacs:encoding") == "cork")
    set_preference ("verbatim->texmacs:encoding", "utf-8");
  add_combo (v2, "verbatim->texmacs:encoding");
  finish_page (verbatim);

  QWidget* pdf= make_page ();
  QFormLayout* pdfForm= add_section (pdf, "ATHENA → Pdf/Postscript");
  add_toggle (pdfForm, "texmacs->pdf:expand slides");
  add_toggle (pdfForm, "texmacs->pdf:data-art cover");
  {
    add_combo (pdfForm, "texmacs->pdf:version");
  }
  finish_page (pdf);

  QWidget* image= make_page ();
  QFormLayout* im1= add_section (image, "ATHENA → Image");
  add_combo (im1, "texmacs->image:raster-resolution");
  std::vector<Choice> formats;
  if (file_converter_exists ("x.pdf", "x.svg")) formats.push_back ({"svg", "Svg"});
  if (file_converter_exists ("x.pdf", "x.eps")) formats.push_back ({"eps", "Eps"});
  if (file_converter_exists ("x.pdf", "x.png")) formats.push_back ({"png", "Png"});
  if (file_converter_exists ("x.pdf", "x.tif")) formats.push_back ({"tif", "Tiff"});
  if (file_converter_exists ("x.pdf", "x.jpg")) formats.push_back ({"jpg", "Jpeg"});
  if (file_converter_exists ("x.pdf", "x.pdf")) formats.push_back ({"pdf", "Pdf"});
  if (formats.empty ()) formats.push_back ({"png", "Png"});
  add_dynamic_combo (im1, "texmacs->image:format", formats);
  QFormLayout* im2= add_section (image, "Image → ATHENA");
  add_toggle (im2, "image auto remove background");
  add_toggle (im2, "image->texmacs:svg-prefer-inkscape");
  finish_page (image);

  return tabbed ({{"Html", html}, {"LaTeX", latex},
                  {"Verbatim", verbatim}, {"Pdf", pdf}, {"Image", image}});
}

std::vector<std::pair<QString, QWidget*> >
QTMPreferencesDialog::buildVaultCategories () {
  QWidget* general= make_page ();
  QFormLayout* g= add_section (general, "General");
  add_toggle (g, "vault auto load last");
  add_toggle (g, "vault report missing last");
  add_toggle (g, "vault welcome page");
  add_toggle (g, "vault explorer show on startup");
  add_toggle (g, "vault take preferences with vault");
  finish_page (general);

  QWidget* navigation= make_page ();
  QFormLayout* nav= add_section (navigation, "Navigation");
  add_toggle (nav, "vault explorer track current file");
  add_toggle (nav, "vault explorer use system trash");
  add_toggle (nav, "vault global search case insensitive search");
  add_toggle (nav, "vault global search fuzzy search");
  add_combo (nav, "vault preferred initial neighborhood");
  finish_page (navigation);

  QWidget* namespaces= make_page ();
  QFormLayout* n= add_section (namespaces, "Namespaces");
  add_toggle (n, "vault namespace explorer leaf matches only");
  add_toggle (n, "vault namespace explorer from root namespace");
  add_toggle (n, "vault namespace explorer simplify hierarchy");
  add_toggle (n, "vault simplify hierarchy graphs");
  add_toggle (n, "vault subproduct consume string aggressively");
  finish_page (namespaces);

  QWidget* wikilinks= make_page ();
  QFormLayout* wt= add_section (wikilinks, "Wikilinks and Transclusion");
  QSpinBox* searchWorkers= new QSpinBox;
  mark_preference_control (searchWorkers, "vault link search workers");
  searchWorkers->setRange (1, vault_search_worker_limit ());
  searchWorkers->setValue (vault_search_worker_count ());
  searchWorkers->setKeyboardTracking (false);
  QObject::connect (searchWorkers, &QSpinBox::valueChanged, searchWorkers,
                    [] (int count) {
    set_pref ("vault link search workers", QString::number (count));
  });
  wt->addRow (label ("Search workers:"), searchWorkers);
  add_toggle (wt, "vault wikilink inserter case insensitive search");
  add_toggle (wt, "vault transclusion inserter case insensitive search");
  add_toggle (wt, "vault wikilink inserter fuzzy search");
  add_toggle (wt, "vault transclusion inserter fuzzy search");
  add_line_edit (wt, "vault wikilink display template file");
  add_line_edit (wt, "vault wikilink display template heading");
  add_line_edit (wt, "vault wikilink display template anchor");
  finish_page (wikilinks);

  QWidget* artifacts= make_page ();
  QFormLayout* af= add_section (artifacts, "Artifactization");
  QLabel* filterExplanation= new QLabel (
    "Complete candidate artifact names in this vault-specific list are "
    "ignored. Double-click an entry to edit it. The backing .lst file stores "
    "one entry per line.", artifacts);
  filterExplanation->setWordWrap (true);
  af->addRow (filterExplanation);
  QListWidget* titleFilter= new QListWidget (artifacts);
  titleFilter->setMinimumHeight (280);
  titleFilter->setEditTriggers (QAbstractItemView::DoubleClicked |
                                QAbstractItemView::EditKeyPressed);
  af->addRow (label ("Rejected artifact names:"), titleFilter);
  QWidget* filterButtons= new QWidget (artifacts);
  QHBoxLayout* filterButtonLayout= new QHBoxLayout (filterButtons);
  filterButtonLayout->setContentsMargins (0, 0, 0, 0);
  QPushButton* addFilterEntry= new QPushButton ("Add entry", filterButtons);
  QPushButton* removeFilterEntry=
    new QPushButton ("Remove selected", filterButtons);
  filterButtonLayout->addWidget (addFilterEntry);
  filterButtonLayout->addWidget (removeFilterEntry);
  filterButtonLayout->addStretch (1);
  af->addRow (filterButtons);

  auto reloadTitleFilter= [titleFilter, artifacts] () {
    QStringList entries;
    QString error;
    if (!qtm_artifact_title_filter_read (entries, &error)) {
      QMessageBox::warning (artifacts, "Artifactization", error);
      return false;
    }
    QSignalBlocker blocker (titleFilter);
    titleFilter->clear ();
    for (const QString& entry: entries) {
      QListWidgetItem* item= new QListWidgetItem (entry, titleFilter);
      item->setFlags (item->flags () | Qt::ItemIsEditable);
    }
    return true;
  };
  auto saveTitleFilter= [titleFilter, artifacts] () {
    QStringList entries;
    QSignalBlocker blocker (titleFilter);
    for (int row=0; row<titleFilter->count (); row++) {
      QString entry= titleFilter->item (row)->text ().trimmed ();
      titleFilter->item (row)->setText (entry);
      if (!entry.isEmpty ()) entries << entry;
    }
    QString error;
    if (!qtm_artifact_title_filter_write (entries, &error)) {
      QMessageBox::warning (artifacts, "Artifactization", error);
      return false;
    }
    try { (void) call ("update-current-buffer"); }
    catch (...) {}
    return true;
  };
  if (qtm_vault_info_available ()) {
    reloadTitleFilter ();
    QObject::connect (titleFilter, &QListWidget::itemChanged, artifacts,
                      [saveTitleFilter] (QListWidgetItem*) {
                        saveTitleFilter ();
                      });
    QObject::connect (addFilterEntry, &QPushButton::clicked, artifacts,
                      [=] () {
      bool ok= false;
      QString entry= QInputDialog::getText (
        artifacts, "Add rejected artifact name", "Name or phrase:",
        QLineEdit::Normal, QString (), &ok).trimmed ();
      if (!ok || entry.isEmpty ()) return;
      QListWidgetItem* item= new QListWidgetItem (entry, titleFilter);
      item->setFlags (item->flags () | Qt::ItemIsEditable);
      saveTitleFilter ();
    });
    QObject::connect (removeFilterEntry, &QPushButton::clicked, artifacts,
                      [=] () {
      delete titleFilter->takeItem (titleFilter->currentRow ());
      saveTitleFilter ();
    });
  }
  else {
    titleFilter->setEnabled (false);
    addFilterEntry->setEnabled (false);
    removeFilterEntry->setEnabled (false);
    titleFilter->addItem ("No active vault.");
  }
  finish_page (artifacts);

  QWidget* materials= make_page ();
  QFormLayout* mm= add_section (materials, "Materials");
  QLabel* providerNotice= new QLabel (
    "Local extraction is always performed first. Enabled metadata providers "
    "receive only identifiers such as DOI, ISBN, arXiv ID, or PMID; document "
    "files are never uploaded.", materials);
  providerNotice->setWordWrap (true);
  mm->addRow (providerNotice);
  add_line_edit (mm, "materials local metadata extractor");
  add_line_edit (mm, "materials local text extractor");
  int logicalProcessors= std::max (1, QThread::idealThreadCount ());
  std::vector<QStringChoice> importParallelism;
  importParallelism.emplace_back (
    "auto", QString ("Automatic (%1 workers)").arg (logicalProcessors));
  for (int workers=1; workers<=logicalProcessors; ++workers)
    importParallelism.emplace_back (
      QString::number (workers),
      workers == 1 ? QString ("1 worker")
                   : QString ("%1 workers").arg (workers));
  add_dynamic_qstring_combo (mm, "materials import parallelism", importParallelism);
  add_toggle (mm, "materials provider crossref");
  add_toggle (mm, "materials provider openalex");
  add_toggle (mm, "materials provider open library");
  add_toggle (mm, "materials provider google books");
  add_toggle (mm, "materials provider arxiv");
  add_toggle (mm, "materials provider pubmed");
  add_line_edit (mm, "materials provider contact email");
  std::vector<MaterialCslStyle> cslStyles;
  std::string cslError;
  std::vector<QStringChoice> cslChoices;
  if (athena_materials_list_csl_styles (cslStyles, cslError)) {
    bool currentSupported= false;
    QString current= pref ("materials csl style", "springer-mathphys");
    for (const MaterialCslStyle& style: cslStyles) {
      QString name= QString::fromUtf8 (
        style.name.data (), (qsizetype) style.name.size ());
      QString title= QString::fromUtf8 (
        style.title.data (), (qsizetype) style.title.size ());
      cslChoices.emplace_back (name, title + " (" + name + ")");
      if (current == name) currentSupported= true;
    }
    if (!currentSupported)
      set_pref ("materials csl style", "springer-mathphys");
  }
  else cslChoices.emplace_back (
    "springer-mathphys", "Springer - MathPhys (numeric, brackets)");
  QComboBox* cslCombo= add_dynamic_qstring_combo (mm, "materials csl style", cslChoices);
  cslCombo->setEditable (true);
  cslCombo->setInsertPolicy (QComboBox::NoInsert);
  cslCombo->setMaxVisibleItems (18);
  cslCombo->completer ()->setCaseSensitivity (Qt::CaseInsensitive);
  cslCombo->completer ()->setFilterMode (Qt::MatchContains);
  QObject::connect (cslCombo->lineEdit (), &QLineEdit::editingFinished,
                    [cslCombo] () {
    int index= cslCombo->findText (
      cslCombo->currentText (), Qt::MatchFixedString);
    if (index >= 0) cslCombo->setCurrentIndex (index);
    else if (cslCombo->currentIndex () >= 0)
      cslCombo->setEditText (
        cslCombo->itemText (cslCombo->currentIndex ()));
  });
  if (!cslError.empty ()) {
    cslCombo->setEnabled (false);
    cslCombo->setToolTip (
      "Could not load CSL styles: " + QString::fromStdString (cslError));
  }
  finish_page (materials);

#if ATHENA_ENABLE_PERSON_SUBSYSTEM
  QWidget* persons= make_page ();
  QFormLayout* pn= add_section (persons, "Persons");
  add_toggle (pn, "vault normalize person names on save");
  finish_page (persons);
#endif

  QWidget* maintenance= make_page ();
  QFormLayout* mt= add_section (maintenance, "Maintenance");
  add_combo (mt, "vault max full backups");
  add_combo (mt, "vault maintenance worker processes");
  add_toggle (mt, "vault maintenance update table of contents");
  add_toggle (mt, "vault maintenance remove redundant block wikilinks");
  add_toggle (mt, "vault maintenance continuous rag");
  add_combo (mt, "vault maintenance rag delegation fallback");
  add_toggle (mt, "vault collect orphan assets");
  add_toggle (mt, "vault generate maintenance summary page");
  add_combo (mt, "vault maintenance summaries to keep");
  finish_page (maintenance);

  QWidget* backup= make_page ();
  QFormLayout* bk= add_section (backup, "Backup dispatchers");
  QLabel* backupDescription= new QLabel (
    "Each dispatcher maintains a one-way mirror of the whole active vault. "
    "The destination may be an absolute local directory, a ~/ path, or an "
    "rsync/SSH destination "
    "such as user@host:/path. Files removed from the vault are also removed "
    "from the mirror. .backup and .athena/rag-backup-* are excluded.", backup);
  backupDescription->setWordWrap (true);
  bk->addRow (backupDescription);

  QTableWidget* dispatcherTable= new QTableWidget (backup);
  dispatcherTable->setColumnCount (2);
  dispatcherTable->setHorizontalHeaderLabels ({"Destination", "Trigger"});
  dispatcherTable->horizontalHeader ()->setSectionResizeMode (
    0, QHeaderView::Stretch);
  dispatcherTable->horizontalHeader ()->setSectionResizeMode (
    1, QHeaderView::ResizeToContents);
  dispatcherTable->setSelectionBehavior (QAbstractItemView::SelectRows);
  dispatcherTable->setSelectionMode (QAbstractItemView::ExtendedSelection);
  dispatcherTable->setMinimumHeight (220);

  auto triggerCombo= [dispatcherTable] (const QString& trigger) {
    QComboBox* combo= new QComboBox (dispatcherTable);
    combo->addItem ("Every successful save", "realtime");
    combo->addItem ("Vault maintenance", "maintenance");
    combo->addItem ("Idle for 5 minutes", "idle");
    int index= combo->findData (trigger);
    combo->setCurrentIndex (index >= 0 ? index : 0);
    return combo;
  };

  QVector<QTMBackupDispatcher> configuredDispatchers;
  QString dispatcherReadError;
  bool dispatchersAvailable= qtm_backup_dispatchers_read (
    configuredDispatchers, &dispatcherReadError);
  if (dispatchersAvailable) {
    for (const QTMBackupDispatcher& dispatcher: configuredDispatchers) {
      int row= dispatcherTable->rowCount ();
      dispatcherTable->insertRow (row);
      QTableWidgetItem* destinationItem=
        new QTableWidgetItem (dispatcher.destination);
      destinationItem->setData (Qt::UserRole, dispatcher.destination);
      dispatcherTable->setItem (row, 0, destinationItem);
      dispatcherTable->setCellWidget (
        row, 1, triggerCombo (dispatcher.trigger));
    }
  }
  else {
    dispatcherTable->setEnabled (false);
    backupDescription->setText (dispatcherReadError.isEmpty ()
      ? "No active vault." : dispatcherReadError);
  }
  bk->addRow (dispatcherTable);

  QWidget* dispatcherButtons= new QWidget (backup);
  QHBoxLayout* dispatcherButtonLayout= new QHBoxLayout (dispatcherButtons);
  dispatcherButtonLayout->setContentsMargins (0, 0, 0, 0);
  QPushButton* addDispatcher= new QPushButton ("Add destination...", backup);
  QPushButton* browseDispatcher= new QPushButton ("Add local folder...", backup);
  QPushButton* removeDispatcher= new QPushButton ("Remove selected", backup);
  dispatcherButtonLayout->addWidget (addDispatcher);
  dispatcherButtonLayout->addWidget (browseDispatcher);
  dispatcherButtonLayout->addWidget (removeDispatcher);
  dispatcherButtonLayout->addStretch (1);
  addDispatcher->setEnabled (dispatchersAvailable);
  browseDispatcher->setEnabled (dispatchersAvailable);
  removeDispatcher->setEnabled (dispatchersAvailable);
  bk->addRow (dispatcherButtons);

  auto saveDispatchers= [backup, dispatcherTable] () {
    QVector<QTMBackupDispatcher> dispatchers;
    for (int row=0; row<dispatcherTable->rowCount (); ++row) {
      QTableWidgetItem* destination= dispatcherTable->item (row, 0);
      QComboBox* trigger= qobject_cast<QComboBox*> (
        dispatcherTable->cellWidget (row, 1));
      dispatchers.push_back (
        {destination == nullptr ? QString () : destination->text (),
         trigger == nullptr ? QString ("realtime")
                            : trigger->currentData ().toString ()});
    }
    QString error;
    if (!qtm_backup_dispatchers_write (dispatchers, &error)) {
      QMessageBox::warning (backup, "Backup dispatchers", error);
      return false;
    }
    return true;
  };

  auto appendDispatcher= [=] (const QString& destination) {
    if (destination.trimmed ().isEmpty ()) return;
    int row= dispatcherTable->rowCount ();
    QComboBox* combo= nullptr;
    {
      QSignalBlocker blocker (dispatcherTable);
      dispatcherTable->insertRow (row);
      QTableWidgetItem* destinationItem=
        new QTableWidgetItem (destination.trimmed ());
      destinationItem->setData (Qt::UserRole, destination.trimmed ());
      dispatcherTable->setItem (row, 0, destinationItem);
      combo= triggerCombo ("realtime");
      dispatcherTable->setCellWidget (row, 1, combo);
    }
    QObject::connect (
      combo, QOverload<int>::of (&QComboBox::currentIndexChanged),
      backup, [saveDispatchers] (int) { saveDispatchers (); });
    if (!saveDispatchers ()) dispatcherTable->removeRow (row);
  };

  for (int row=0; row<dispatcherTable->rowCount (); ++row) {
    QComboBox* combo= qobject_cast<QComboBox*> (
      dispatcherTable->cellWidget (row, 1));
    if (combo != nullptr)
      QObject::connect (
        combo, QOverload<int>::of (&QComboBox::currentIndexChanged),
        backup, [saveDispatchers] (int) { saveDispatchers (); });
  }
  QObject::connect (
    dispatcherTable, &QTableWidget::cellChanged, backup,
    [dispatcherTable, saveDispatchers] (int row, int column) {
      if (column != 0) return;
      QTableWidgetItem* item= dispatcherTable->item (row, column);
      if (item == nullptr) return;
      QString previous= item->data (Qt::UserRole).toString ();
      if (!saveDispatchers ()) {
        QSignalBlocker blocker (dispatcherTable);
        item->setText (previous);
        return;
      }
      QString normalized= item->text ().trimmed ();
      {
        QSignalBlocker blocker (dispatcherTable);
        item->setText (normalized);
      }
      item->setData (Qt::UserRole, normalized);
    });
  QObject::connect (addDispatcher, &QPushButton::clicked, backup, [=] () {
    bool ok= false;
    QString destination= QInputDialog::getText (
      backup, "Add backup destination", "Destination:",
      QLineEdit::Normal, QString (), &ok);
    if (ok) appendDispatcher (destination);
  });
  QObject::connect (browseDispatcher, &QPushButton::clicked, backup, [=] () {
    QString destination= QFileDialog::getExistingDirectory (
      backup, "Choose backup destination", QDir::homePath ());
    if (!destination.isEmpty ()) appendDispatcher (destination);
  });
  QObject::connect (removeDispatcher, &QPushButton::clicked, backup, [=] () {
    QList<int> rows;
    for (QTableWidgetItem* item: dispatcherTable->selectedItems ())
      if (!rows.contains (item->row ())) rows << item->row ();
    std::sort (rows.begin (), rows.end (), std::greater<int> ());
    for (int row: rows) dispatcherTable->removeRow (row);
    saveDispatchers ();
  });
  finish_page (backup);

  QWidget* anchors= make_page ();
  QFormLayout* a= add_section (anchors, "Images");
  add_toggle (a, "vault auto copy images to vault");
  add_toggle (a, "vault normalize image filename when inserting");
  add_combo (a, "pasted internet image handling");
  finish_page (anchors);

  QWidget* info= make_page ();
  QFormLayout* vi= add_section (info, "Vault Info");
  QTMVaultfileInfo vaultInfo;
  if (!qtm_vaultfile_read (vaultInfo)) {
    QLabel* inactive= new QLabel ("No active vault.", info);
    inactive->setWordWrap (true);
    vi->addRow (inactive);
  }
  else {
    QLineEdit* vaultName= new QLineEdit (vaultInfo.name, info);
    vi->addRow (label ("Vault name:"), vaultName);

    QPushButton* chooseMap= nullptr;
    QLineEdit* mapPath= add_path_chooser_row (
      vi, "Map database path:", vaultInfo.mapPath, chooseMap);
    QPushButton* choosePrefs= nullptr;
    QLineEdit* preferencesPath= add_path_chooser_row (
      vi, "Local preferences path:", vaultInfo.preferencesPath, choosePrefs);
    QPushButton* chooseNamespace= nullptr;
    QLineEdit* namespacePath= add_path_chooser_row (
      vi, "Namespace database path:", vaultInfo.namespaceDbPath,
      chooseNamespace);
    QPushButton* chooseStartup= nullptr;
    QLineEdit* startupPage= add_path_chooser_row (
      vi, "Startup page:", vaultInfo.startupPage, chooseStartup);
    QPushButton* chooseOneTimeStartup= nullptr;
    QLineEdit* oneTimeStartupPage= add_path_chooser_row (
      vi, "One-time startup page:", vaultInfo.oneTimeStartupPage,
      chooseOneTimeStartup);
    QPushButton* chooseSummaryFolder= nullptr;
    QLineEdit* maintenanceSummaryPath= add_path_chooser_row (
      vi, "Maintenance summary folder:", vaultInfo.maintenanceSummaryPath,
      chooseSummaryFolder);
    QPushButton* chooseRagIndex= nullptr;
    QLineEdit* ragIndexPath= add_path_chooser_row (
      vi, "RAG index database path:", vaultInfo.ragIndexPath,
      chooseRagIndex);
    QPushButton* chooseWebsites= nullptr;
    QLineEdit* websitesPath= add_path_chooser_row (
      vi, "Website registry path:", vaultInfo.websitesPath,
      chooseWebsites);
    QPushButton* chooseMaterialsDb= nullptr;
    QLineEdit* materialsDbPath= add_path_chooser_row (
      vi, "Materials database path:", vaultInfo.materialsDbPath,
      chooseMaterialsDb);
    QPushButton* chooseMaterialsDirectory= nullptr;
    QLineEdit* materialsDirectory= add_path_chooser_row (
      vi, "Stored materials folder:", vaultInfo.materialsDirectory,
      chooseMaterialsDirectory);
    QPushButton* chooseArtifactTitleFilter= nullptr;
    QLineEdit* artifactTitleFilterPath= add_path_chooser_row (
      vi, "Artifact title filter path:", vaultInfo.artifactTitleFilterPath,
      chooseArtifactTitleFilter);
    QComboBox* rootNamespace= new QComboBox (info);
    rootNamespace->addItem ("None", "");
    QStringList namespaceNames= namespace_names_pref ();
    for (const QString& name: namespaceNames)
      rootNamespace->addItem (name, name);
    if (!vaultInfo.rootNamespace.isEmpty () &&
        !namespaceNames.contains (vaultInfo.rootNamespace)) {
      rootNamespace->addItem (
        vaultInfo.rootNamespace + " (missing)", vaultInfo.rootNamespace);
    }
    int rootIndex= rootNamespace->findData (vaultInfo.rootNamespace);
    if (rootIndex >= 0) rootNamespace->setCurrentIndex (rootIndex);
    vi->addRow (label ("Root namespace:"), rootNamespace);

    auto saveVaultfile= [info, vaultName, mapPath, preferencesPath,
                         namespacePath, startupPage,
                         oneTimeStartupPage, maintenanceSummaryPath,
                         ragIndexPath, websitesPath, materialsDbPath,
                         materialsDirectory, artifactTitleFilterPath,
                         rootNamespace] () {
      QTMVaultfileInfo next;
      next.name= vaultName->text ();
      next.mapPath= mapPath->text ();
      next.preferencesPath= preferencesPath->text ();
      next.namespaceDbPath= namespacePath->text ();
      next.startupPage= startupPage->text ();
      next.oneTimeStartupPage= oneTimeStartupPage->text ();
      next.maintenanceSummaryPath= maintenanceSummaryPath->text ();
      next.ragIndexPath= ragIndexPath->text ();
      next.websitesPath= websitesPath->text ();
      next.materialsDbPath= materialsDbPath->text ();
      next.materialsDirectory= materialsDirectory->text ();
      next.artifactTitleFilterPath= artifactTitleFilterPath->text ();
      next.rootNamespace= rootNamespace->currentData ().toString ();
      QString error;
      if (!qtm_vaultfile_write (next, &error)) {
        QMessageBox::warning (info, "Vault Info", error);
        return;
      }
      mapPath->setText (qtm_clean_vault_relative_path (next.mapPath));
      preferencesPath->setText (
        qtm_clean_vault_relative_path (next.preferencesPath));
      namespacePath->setText (
        qtm_clean_vault_relative_path (next.namespaceDbPath));
      startupPage->setText (qtm_clean_vault_target (next.startupPage));
      oneTimeStartupPage->setText (
        qtm_clean_vault_target (next.oneTimeStartupPage));
      maintenanceSummaryPath->setText (
        qtm_clean_vault_relative_path (next.maintenanceSummaryPath));
      ragIndexPath->setText (
        qtm_clean_vault_relative_path (next.ragIndexPath));
      websitesPath->setText (
        qtm_clean_vault_relative_path (next.websitesPath));
      materialsDbPath->setText (
        qtm_clean_vault_relative_path (next.materialsDbPath));
      materialsDirectory->setText (
        qtm_clean_vault_relative_path (next.materialsDirectory));
      artifactTitleFilterPath->setText (
        qtm_clean_vault_relative_path (next.artifactTitleFilterPath));
    };

    auto choosePath= [info, saveVaultfile] (QLineEdit* edit,
                                           const QString& title,
                                           bool existing) {
      QString root= qtm_vault_root_path ();
      QString initial= edit->text ().trimmed ().isEmpty ()
        ? root : QDir (root).absoluteFilePath (edit->text ().trimmed ());
      QString selected= existing
        ? QFileDialog::getOpenFileName (
            info, title, initial, "ATHENA documents (*.ath *.tm);;All files (*)")
        : QFileDialog::getSaveFileName (info, title, initial, "All files (*)");
      if (selected.isEmpty ()) return;
      QString rel= qtm_vault_relative_from_selected_path (selected);
      if (!qtm_valid_vault_relative_path (rel)) {
        QMessageBox::warning (
          info, "Vault Info",
          "Selected file must be inside the vault. Vaultfile.json stores "
          "paths relative to the vault root.");
        return;
      }
      edit->setText (rel);
      saveVaultfile ();
    };

    auto chooseFolder= [info, saveVaultfile] (QLineEdit* edit,
                                             const QString& title) {
      QString root= qtm_vault_root_path ();
      QString initial= edit->text ().trimmed ().isEmpty ()
        ? root : QDir (root).absoluteFilePath (edit->text ().trimmed ());
      QString selected= QFileDialog::getExistingDirectory (info, title, initial);
      if (selected.isEmpty ()) return;
      QString rel= qtm_vault_relative_from_selected_path (selected);
      if (!qtm_valid_optional_vault_relative_path (rel)) {
        QMessageBox::warning (
          info, "Vault Info",
          "Selected folder must be inside the vault. Vaultfile.json stores "
          "paths relative to the vault root.");
        return;
      }
      edit->setText (rel);
      saveVaultfile ();
    };

    QObject::connect (vaultName, &QLineEdit::editingFinished, saveVaultfile);
    QObject::connect (mapPath, &QLineEdit::editingFinished, saveVaultfile);
    QObject::connect (preferencesPath, &QLineEdit::editingFinished,
                      saveVaultfile);
    QObject::connect (namespacePath, &QLineEdit::editingFinished,
                      saveVaultfile);
    QObject::connect (startupPage, &QLineEdit::editingFinished,
                      saveVaultfile);
    QObject::connect (oneTimeStartupPage, &QLineEdit::editingFinished,
                      saveVaultfile);
    QObject::connect (maintenanceSummaryPath, &QLineEdit::editingFinished,
                      saveVaultfile);
    QObject::connect (ragIndexPath, &QLineEdit::editingFinished,
                      saveVaultfile);
    QObject::connect (websitesPath, &QLineEdit::editingFinished,
                      saveVaultfile);
    QObject::connect (materialsDbPath, &QLineEdit::editingFinished,
                      saveVaultfile);
    QObject::connect (materialsDirectory, &QLineEdit::editingFinished,
                      saveVaultfile);
    QObject::connect (artifactTitleFilterPath, &QLineEdit::editingFinished,
                      saveVaultfile);
    QObject::connect (
      rootNamespace, QOverload<int>::of (&QComboBox::currentIndexChanged),
      [saveVaultfile] (int) { saveVaultfile (); });
    QObject::connect (chooseMap, &QPushButton::clicked,
                      [=] () { choosePath (mapPath, "Choose map database",
                                           false); });
    QObject::connect (choosePrefs, &QPushButton::clicked,
                      [=] () { choosePath (preferencesPath,
                                           "Choose local preferences file",
                                           false); });
    QObject::connect (chooseNamespace, &QPushButton::clicked,
                      [=] () { choosePath (namespacePath,
                                           "Choose namespace database",
                                           false); });
    QObject::connect (chooseStartup, &QPushButton::clicked,
                      [=] () { choosePath (startupPage,
                                           "Choose startup page", true); });
    QObject::connect (chooseOneTimeStartup, &QPushButton::clicked,
                      [=] () { choosePath (oneTimeStartupPage,
                                           "Choose one-time startup page",
                                           true); });
    QObject::connect (chooseSummaryFolder, &QPushButton::clicked,
                      [=] () { chooseFolder (maintenanceSummaryPath,
                                             "Choose maintenance summary folder"); });
    QObject::connect (chooseRagIndex, &QPushButton::clicked,
                      [=] () { choosePath (ragIndexPath,
                                           "Choose RAG index database",
                                           false); });
    QObject::connect (chooseWebsites, &QPushButton::clicked,
                      [=] () { choosePath (websitesPath,
                                           "Choose website registry",
                                           false); });
    QObject::connect (chooseMaterialsDb, &QPushButton::clicked,
                      [=] () { choosePath (materialsDbPath,
                                           "Choose Materials database",
                                           false); });
    QObject::connect (chooseMaterialsDirectory, &QPushButton::clicked,
                      [=] () { chooseFolder (materialsDirectory,
                                             "Choose stored materials folder"); });
    QObject::connect (chooseArtifactTitleFilter, &QPushButton::clicked,
                      [=] () { choosePath (artifactTitleFilterPath,
                                           "Choose artifact title filter",
                                           false); });
  }

  QWidget* vaultFontRow= new QWidget (info);
  mark_preference_control (vaultFontRow, "vault preferred font");
  QHBoxLayout* vaultFontLayout= new QHBoxLayout (vaultFontRow);
  vaultFontLayout->setContentsMargins (0, 0, 0, 0);
  QLineEdit* vaultFont= new QLineEdit (vaultFontRow);
  vaultFont->setReadOnly (true);
  vaultFont->setText (
    qtm_font_profile_summary (pref ("vault preferred font", "")));
  QPushButton* configureVaultFont= new QPushButton ("Configure...", vaultFontRow);
  QPushButton* clearVaultFont= new QPushButton ("Clear", vaultFontRow);
  vaultFontLayout->addWidget (vaultFont, 1);
  vaultFontLayout->addWidget (configureVaultFont);
  vaultFontLayout->addWidget (clearVaultFont);
  QObject::connect (configureVaultFont, &QPushButton::clicked,
                    [vaultFont, info] () {
    string selected;
    if (!native_font_profile_selector_dialog (
          from_qstring_pref (pref ("vault preferred font", "")),
          "Vault font configuration", selected, info))
      return;
    QString value= to_qstring_pref (selected);
    set_pref ("vault preferred font", value);
    vaultFont->setText (qtm_font_profile_summary (value));
  });
  QObject::connect (clearVaultFont, &QPushButton::clicked,
                    [vaultFont] () {
    set_pref ("vault preferred font", "");
    vaultFont->setText (qtm_font_profile_summary (QString ()));
  });
  vi->addRow (label ("Global preferred font for vault:"), vaultFontRow);
  finish_page (info);

  QWidget* vault= tabbed ({{"General", general},
                           {"Maintenance", maintenance},
                           {"Backup", backup},
                           {"Vault Info", info}});
  QWidget* knowledge= tabbed ({{"Navigation", navigation},
                               {"Namespaces", namespaces},
                               {"Wikilinks and Transclusion", wikilinks},
                               {"Artifacts", artifacts},
#if ATHENA_ENABLE_PERSON_SUBSYSTEM
                               {"Persons", persons},
#endif
                               {"Anchors and Images", anchors}});
  QWidget* materialLibrary= tabbed ({{"Sources and Citations", materials}});
  return {{"Vault", vault},
          {"Knowledge", knowledge},
          {"Materials", materialLibrary}};
}

QWidget*
QTMPreferencesDialog::buildOtherPage () {
  QWidget* ai= make_page ();
  QFormLayout* a= add_section (ai, "AI");
  QLineEdit* codexHome= new QLineEdit (codex_home_path (), ai);
  QPushButton* chooseCodexHome= new QPushButton ("Browse...", ai);
  QWidget* codexHomeRow= new QWidget (ai);
  mark_preference_control (codexHomeRow, "codex home");
  QHBoxLayout* codexHomeLayout= new QHBoxLayout (codexHomeRow);
  codexHomeLayout->setContentsMargins (0, 0, 0, 0);
  codexHomeLayout->addWidget (codexHome, 1);
  codexHomeLayout->addWidget (chooseCodexHome);
  a->addRow (label ("Codex home:"), codexHomeRow);

  QLabel* codexStatus= new QLabel (ai);
  codexStatus->setWordWrap (true);
  auto refreshCodexStatus= [codexStatus] () {
    QString executable= codex_executable ();
    codexStatus->setText (
      executable.isEmpty ()?
        "Codex executable not found. Install Codex or use a build that bundles it.":
        QString ("Codex executable: %1").arg (executable));
  };
  refreshCodexStatus ();
  a->addRow (label ("Status:"), codexStatus);

  QWidget* codexButtons= new QWidget (ai);
  QHBoxLayout* codexButtonLayout= new QHBoxLayout (codexButtons);
  codexButtonLayout->setContentsMargins (0, 0, 0, 0);
  QPushButton* codexLogin= new QPushButton ("OpenAI Codex login", ai);
  QPushButton* codexLoginStatus= new QPushButton ("Check login status", ai);
  codexButtonLayout->addWidget (codexLogin);
  codexButtonLayout->addWidget (codexLoginStatus);
  codexButtonLayout->addStretch (1);
  a->addRow (label ("Authentication:"), codexButtons);

  QObject::connect (codexHome, &QLineEdit::editingFinished, [=] () {
    QString value= QDir::cleanPath (codexHome->text ().trimmed ());
    codexHome->setText (value);
    set_pref ("codex home", value);
  });
  QObject::connect (chooseCodexHome, &QPushButton::clicked, [=] () {
    QString selected= QFileDialog::getExistingDirectory (
      ai, "Choose Codex home", codexHome->text ());
    if (selected.isEmpty ()) return;
    codexHome->setText (QDir::cleanPath (selected));
    set_pref ("codex home", codexHome->text ());
  });
  QObject::connect (codexLogin, &QPushButton::clicked, [=] () {
    set_pref ("codex home", codexHome->text ());
    show_codex_login (ai, codexHome->text (), refreshCodexStatus);
  });
  QObject::connect (codexLoginStatus, &QPushButton::clicked, [=] () {
    QString executable= codex_executable ();
    if (executable.isEmpty ()) {
      codexStatus->setText ("Codex executable not found.");
      return;
    }
    QProcess process;
    process.setProcessEnvironment (codex_environment (codexHome->text ()));
    process.setProcessChannelMode (QProcess::MergedChannels);
    process.start (executable, {"login", "status"});
    if (!process.waitForFinished (10000)) {
      process.kill ();
      codexStatus->setText ("Timed out while checking Codex login status.");
      return;
    }
    QString result= QString::fromUtf8 (process.readAll ()).trimmed ();
    codexStatus->setText (result.isEmpty ()?
      QString ("Codex login status exited with code %1.")
        .arg (process.exitCode ()): result);
  });
  finish_page (ai);

  QWidget* debugging= make_page ();
  QFormLayout* debugGeneral= add_section (debugging, "General");
  add_toggle (debugGeneral, "debug scheme backtraces");

  QFormLayout* console= add_section (debugging, "Error Messages");
  add_toggle (console, "open console on errors");
  add_toggle (console, "open console on warnings");

  QFormLayout* performance= add_section (debugging, "Rendering Performance");
  QCheckBox* performanceMonitor= add_toggle (performance, "rendering performance monitor");
  QObject::connect (performanceMonitor, &QCheckBox::toggled,
                    [] () {
                      QTMWidget::refreshAllPerformanceMonitors ();
                    });
  QLabel* performanceNote= new QLabel (
    "The semi-transparent HUD is shown at the lower-left of every document "
    "editor. FPS counts completed canvas paints; editing latency measures "
    "from keyboard or input-method delivery to the next completed paint. "
    "HUD-only refreshes are excluded from both measurements.", debugging);
  performanceNote->setWordWrap (true);
  debugging->layout ()->addWidget (performanceNote);
  finish_page (debugging);

  QWidget* security= make_page ();
  QFormLayout* s= add_section (security, "Security");
  add_combo (s, "security");

  QFormLayout* rd= add_section (security, "ATHENA Delegation");
  QListWidget* ragServers= new QListWidget (security);
  mark_preference_control (ragServers, "delegation server");
  ragServers->setMinimumHeight (120);
  auto reloadRagServers= [ragServers] () {
    ragServers->clear ();
    QVector<QTMDelegationServer> servers= qtm_delegation_servers ();
    QString configured= pref ("delegation server", "");
    int selected= -1;
    for (const QTMDelegationServer& server: servers) {
      QListWidgetItem* item= new QListWidgetItem (
        server.name + " - " + server.url, ragServers);
      item->setData (Qt::UserRole, server.url);
      item->setToolTip ("Fingerprint: " + server.fingerprint);
      if (server.url == configured) selected= ragServers->count () - 1;
    }
    if (selected < 0 && ragServers->count () > 0) selected= 0;
    if (selected >= 0) ragServers->setCurrentRow (selected);
  };
  reloadRagServers ();
  rd->addRow (label ("Delegation servers:"), ragServers);
  QObject::connect (ragServers, &QListWidget::currentItemChanged,
                    [] (QListWidgetItem* current) {
    if (current != nullptr)
      set_pref ("delegation server",
                current->data (Qt::UserRole).toString ());
  });
  QWidget* ragServerButtons= new QWidget (security);
  QHBoxLayout* ragServerButtonLayout= new QHBoxLayout (ragServerButtons);
  ragServerButtonLayout->setContentsMargins (0, 0, 0, 0);
  QPushButton* addRagServer= new QPushButton ("Add server", ragServerButtons);
  QPushButton* deleteRagServer= new QPushButton ("Delete", ragServerButtons);
  QPushButton* checkRagAuth= new QPushButton ("Check authentication",
                                               ragServerButtons);
  QPushButton* runRagDelegation= new QPushButton ("Run delegated embedding",
                                                  ragServerButtons);
  ragServerButtonLayout->addWidget (addRagServer);
  ragServerButtonLayout->addWidget (deleteRagServer);
  ragServerButtonLayout->addWidget (checkRagAuth);
  ragServerButtonLayout->addWidget (runRagDelegation);
  ragServerButtonLayout->addStretch (1);
  rd->addRow (label ("Server management:"), ragServerButtons);
  QLabel* ragDelegationNote= new QLabel (
    "ATHENA Delegation stores server trust and local private keys in " +
    qtm_delegation_config_dir () +
    ". Server enrollment creates a pending client key; the server "
    "administrator still has to accept it.");
  ragDelegationNote->setWordWrap (true);
  security->layout ()->addWidget (ragDelegationNote);
  QGroupBox* ragDelegationBox= qobject_cast<QGroupBox*> (rd->parentWidget ());
  auto refreshRagDelegationEnabled= [ragDelegationBox] () {
    if (ragDelegationBox)
      ragDelegationBox->setEnabled (
        pref_on ("rag delegation enabled") ||
        pref_on ("artifact definition span delegation enabled"));
  };
  refreshRagDelegationEnabled ();
  QObject::connect (addRagServer, &QPushButton::clicked,
                    [security, reloadRagServers] () {
    QDialog dialog (security);
    dialog.setWindowTitle ("Add ATHENA Delegation Server");
    QFormLayout form (&dialog);
    QLineEdit address;
    address.setPlaceholderText ("https://example.org or 192.168.1.2");
    QLineEdit port;
    port.setText ("8765");
    form.addRow ("Address:", &address);
    form.addRow ("Port:", &port);
    QDialogButtonBox buttons (QDialogButtonBox::Ok |
                              QDialogButtonBox::Cancel, &dialog);
    form.addRow (&buttons);
    QObject::connect (&buttons, &QDialogButtonBox::accepted,
                      &dialog, &QDialog::accept);
    QObject::connect (&buttons, &QDialogButtonBox::rejected,
                      &dialog, &QDialog::reject);
    if (dialog.exec () != QDialog::Accepted) return;
    QString base= address.text ().trimmed ();
    if (base.isEmpty ()) return;
    if (!base.contains ("://")) base= "http://" + base;
    QUrl url (base);
    if (!port.text ().trimmed ().isEmpty ())
      url.setPort (port.text ().trimmed ().toInt ());
    QTMDelegationServer server;
    QString error;
    if (!qtm_delegation_fetch_identity (url.toString (), server,
                                            &error)) {
      QMessageBox::warning (security, "ATHENA Delegation", error);
      return;
    }
    QMessageBox::StandardButton trust= QMessageBox::question (
      security, "Trust ATHENA Delegation Server",
      "ATHENA Delegation Server fingerprint:\n\n" + server.fingerprint +
      "\n\nTrust and pin this server key?");
    if (trust != QMessageBox::Yes) return;
    QString status;
    if (!qtm_delegation_enroll (server, &status, &error)) {
      QMessageBox::warning (security, "ATHENA Delegation", error);
      return;
    }
    QVector<QTMDelegationServer> servers= qtm_delegation_servers ();
    bool replaced= false;
    for (QTMDelegationServer& existing: servers)
      if (existing.url == server.url) {
        existing= server;
        replaced= true;
      }
    if (!replaced) servers << server;
    if (!qtm_delegation_save_servers (servers, &error)) {
      QMessageBox::warning (security, "ATHENA Delegation", error);
      return;
    }
    reloadRagServers ();
    QMessageBox::information (
      security, "ATHENA Delegation",
      "Enrollment submitted. Server authentication status: " + status + ".");
  });
  QObject::connect (deleteRagServer, &QPushButton::clicked,
                    [ragServers, reloadRagServers, security] () {
    QListWidgetItem* item= ragServers->currentItem ();
    if (item == nullptr) return;
    QString url= item->data (Qt::UserRole).toString ();
    QVector<QTMDelegationServer> servers= qtm_delegation_servers ();
    servers.erase (std::remove_if (
      servers.begin (), servers.end (),
      [url] (const QTMDelegationServer& s) { return s.url == url; }),
      servers.end ());
    QString error;
    if (!qtm_delegation_save_servers (servers, &error)) {
      QMessageBox::warning (security, "ATHENA Delegation", error);
      return;
    }
    reloadRagServers ();
  });
  auto selectedRagServer= [ragServers] (
    QTMDelegationServer& out) -> bool {
    QListWidgetItem* item= ragServers->currentItem ();
    if (item == nullptr) return false;
    QString url= item->data (Qt::UserRole).toString ();
    for (const QTMDelegationServer& server: qtm_delegation_servers ())
      if (server.url == url) {
        out= server;
        return true;
      }
    return false;
  };
  QObject::connect (checkRagAuth, &QPushButton::clicked,
                    [security, selectedRagServer] () {
    QTMDelegationServer server;
    if (!selectedRagServer (server)) return;
    QString status, error;
    if (!qtm_delegation_check_auth (server, &status, &error)) {
      QMessageBox::warning (security, "ATHENA Delegation", error);
      return;
    }
    QMessageBox::information (
      security, "ATHENA Delegation",
      "Server authentication status: " + status + ".");
  });
  QObject::connect (runRagDelegation, &QPushButton::clicked,
                    [security, selectedRagServer] () {
    QTMDelegationServer server;
    if (!selectedRagServer (server)) return;
    QString root= current_vault_root_qstring ();
    if (root.isEmpty ()) {
      QMessageBox::warning (security, "RAG Delegation",
                            "No active vault is loaded.");
      return;
    }
    QString dbPath= QString::fromStdString (
      athena::rag::rag_default_db_path (root.toStdString ()));
    QString embeddingModel= pref ("rag embedding model", "");
    if (pref ("rag realtime npu enabled", "off") == "on") {
      QString tokenizer= pref ("rag npu tokenizer gguf", "").trimmed ();
      if (!tokenizer.isEmpty ()) embeddingModel= tokenizer;
    }
    QString summary, error;
    if (!qtm_delegation_run_embedding (
          server, root, dbPath, embeddingModel,
          pref ("rag embedding device", "auto"), &summary, &error)) {
      QMessageBox::warning (security, "RAG Delegation", error);
      return;
    }
    QMessageBox::information (security, "RAG Delegation", summary);
  });
  finish_page (security);

  QWidget* connectivity= make_page ();
  QFormLayout* google= add_section (connectivity, "Google Tasks");
  QLineEdit* clientId= add_line_edit (google, "google oauth client id");
  QLineEdit* clientSecret= add_line_edit (google, "google oauth client secret");
  QComboBox* cloudTodoList= new QComboBox (connectivity);
  mark_preference_control (cloudTodoList,
                           "google tasks cloud todo list id");
  cloudTodoList->addItem ("Default task list", "");
  google->addRow (label ("Cloud todo task list:"), cloudTodoList);
  QLabel* googleStatus= new QLabel (connectivity);
  googleStatus->setWordWrap (true);
  auto refreshCloudTodoLists= [cloudTodoList] () {
    QString selected= to_qstring_pref (
      get_preference ("google tasks cloud todo list id", ""));
    QSignalBlocker initialBlocker (cloudTodoList);
    cloudTodoList->clear ();
    cloudTodoList->addItem ("Default task list", "");
    if (!GoogleOAuth::instance ().hasRefreshToken ()) {
      cloudTodoList->setCurrentIndex (0);
      return;
    }
    QPointer<QComboBox> combo (cloudTodoList);
    GoogleTasksClient::instance ().listTaskLists (
      [combo, selected] (const QVector<GoogleTaskList>& lists,
                         const QString&) {
        if (combo.isNull ()) return;
        QSignalBlocker blocker (combo);
        int restore= 0;
        for (const GoogleTaskList& list: lists) {
          int index= combo->count ();
          combo->addItem (
            list.title.isEmpty ()? list.id: list.title, list.id);
          if (list.id == selected) restore= index;
        }
        combo->setCurrentIndex (restore);
      });
  };
  QObject::connect (cloudTodoList,
                    static_cast<void (QComboBox::*) (int)> (
                      &QComboBox::currentIndexChanged),
                    [cloudTodoList] (int index) {
    if (index < 0) return;
    set_preference ("google tasks cloud todo list id",
                    from_qstring_pref (
                      cloudTodoList->itemData (index).toString ()));
  });
  auto refreshGoogleStatus= [googleStatus, refreshCloudTodoLists] () {
    if (GoogleOAuth::instance ().clientId ().trimmed ().isEmpty ())
      googleStatus->setText (
        "Create a Google Cloud OAuth client of type Desktop app, then paste "
        "its client ID here.");
    else if (GoogleOAuth::instance ().hasRefreshToken ())
      googleStatus->setText ("Google Tasks is connected.");
    else
      googleStatus->setText ("Google Tasks is not connected.");
    refreshCloudTodoLists ();
  };
  QWidget* googleButtons= new QWidget (connectivity);
  QHBoxLayout* googleButtonLayout= new QHBoxLayout (googleButtons);
  googleButtonLayout->setContentsMargins (0, 0, 0, 0);
  QPushButton* connectGoogle= new QPushButton ("Connect to Google", googleButtons);
  QPushButton* disconnectGoogle= new QPushButton ("Disconnect", googleButtons);
  googleButtonLayout->addWidget (connectGoogle);
  googleButtonLayout->addWidget (disconnectGoogle);
  googleButtonLayout->addStretch (1);
  google->addRow (label ("Connection status:"), googleStatus);
  google->addRow (label ("Google account:"), googleButtons);
  QObject::connect (connectGoogle, &QPushButton::clicked,
                    [clientId, clientSecret, googleStatus, connectGoogle,
                     disconnectGoogle, refreshGoogleStatus] () {
    GoogleOAuth::instance ().setClientId (clientId->text ());
    GoogleOAuth::instance ().setClientSecret (clientSecret->text ());
    connectGoogle->setEnabled (false);
    disconnectGoogle->setEnabled (false);
    googleStatus->setText ("Opening browser for Google authorization...");
    GoogleOAuth::instance ().authorizeTasks (
      connectGoogle, [googleStatus, connectGoogle, disconnectGoogle,
                      refreshGoogleStatus] (bool, const QString& message) {
        googleStatus->setText (message);
        connectGoogle->setEnabled (true);
        disconnectGoogle->setEnabled (true);
        if (GoogleOAuth::instance ().hasRefreshToken ())
          refreshGoogleStatus ();
      });
  });
  QObject::connect (disconnectGoogle, &QPushButton::clicked,
                    [refreshGoogleStatus] () {
    GoogleOAuth::instance ().forgetTokens ();
    set_preference ("google tasks cloud todo list id", "");
    refreshGoogleStatus ();
  });
  if (!collectingPreferencesMetadata) refreshGoogleStatus ();
  else googleStatus->setText ("Google Tasks status is not queried while "
                              "collecting preference metadata.");

  QFormLayout* rag= add_section (connectivity, "Continuous RAG");
  add_line_edit (rag, "rag mcp port");
  QCheckBox* delegationEnabled= add_toggle (rag, "rag delegation enabled");
  QObject::connect (delegationEnabled, &QCheckBox::toggled,
                    [refreshRagDelegationEnabled] () {
    refreshRagDelegationEnabled ();
  });
  QFormLayout* artifactDelegation= add_section (
    connectivity, "Artifact Generation");
  QCheckBox* artifactDelegationEnabled= add_toggle (artifactDelegation, "artifact definition span delegation enabled");
  QObject::connect (artifactDelegationEnabled, &QCheckBox::toggled,
                    [refreshRagDelegationEnabled] () {
    refreshRagDelegationEnabled ();
  });
  QPushButton* chooseEmbedding= nullptr;
  QLineEdit* embeddingModel= add_path_chooser_row (
    rag, "Embedding model path:", pref ("rag embedding model", ""),
    chooseEmbedding);
  mark_preference_control (embeddingModel->parentWidget (),
                           "rag embedding model");
  QObject::connect (embeddingModel, &QLineEdit::editingFinished,
                    [embeddingModel] () {
    set_pref ("rag embedding model", embeddingModel->text ().trimmed ());
  });
  QObject::connect (chooseEmbedding, &QPushButton::clicked,
                    [connectivity, embeddingModel] () {
    QString selected= QFileDialog::getOpenFileName (
      connectivity, "Choose embedding GGUF model",
      embeddingModel->text ().trimmed (),
      "GGUF models (*.gguf);;All files (*)");
    if (selected.isEmpty ()) return;
    embeddingModel->setText (selected);
    set_pref ("rag embedding model", selected);
  });
  add_combo (rag, "rag embedding device");
  QCheckBox* realtimeNpu= add_toggle (rag, "rag realtime npu enabled");
  QComboBox* npuPowerPolicy= add_combo (rag, "rag realtime npu power policy");
  QPushButton* chooseNpuModel= nullptr;
  QLineEdit* npuModel= add_path_chooser_row (
    rag, "NPU BGE-M3 OpenVINO model:", pref ("rag npu openvino model", ""),
    chooseNpuModel);
  mark_preference_control (npuModel->parentWidget (), "rag npu openvino model");
  QObject::connect (npuModel, &QLineEdit::editingFinished,
                    [npuModel] () {
    set_pref ("rag npu openvino model", npuModel->text ().trimmed ());
  });
  QObject::connect (chooseNpuModel, &QPushButton::clicked,
                    [connectivity, npuModel] () {
    QString selected= QFileDialog::getOpenFileName (
      connectivity, "Choose BGE-M3 OpenVINO encoder model",
      npuModel->text ().trimmed (),
      "OpenVINO / ONNX models (*.xml *.onnx);;All files (*)");
    if (selected.isEmpty ()) return;
    npuModel->setText (selected);
    set_pref ("rag npu openvino model", selected);
  });
  QPushButton* chooseNpuTokenizer= nullptr;
  QLineEdit* npuTokenizer= add_path_chooser_row (
    rag, "NPU tokenizer GGUF:", pref ("rag npu tokenizer gguf", ""),
    chooseNpuTokenizer);
  mark_preference_control (npuTokenizer->parentWidget (),
                           "rag npu tokenizer gguf");
  QObject::connect (npuTokenizer, &QLineEdit::editingFinished,
                    [npuTokenizer] () {
    set_pref ("rag npu tokenizer gguf", npuTokenizer->text ().trimmed ());
  });
  QObject::connect (chooseNpuTokenizer, &QPushButton::clicked,
                    [connectivity, npuTokenizer] () {
    QString selected= QFileDialog::getOpenFileName (
      connectivity, "Choose BGE-M3 tokenizer GGUF",
      npuTokenizer->text ().trimmed (),
      "GGUF models (*.gguf);;All files (*)");
    if (selected.isEmpty ()) return;
    npuTokenizer->setText (selected);
    set_pref ("rag npu tokenizer gguf", selected);
  });
  auto refreshRealtimeNpuControls=
    [realtimeNpu, npuPowerPolicy, npuModel, chooseNpuModel,
     npuTokenizer, chooseNpuTokenizer] () {
      const bool enabled= realtimeNpu->isChecked ();
      npuPowerPolicy->setEnabled (enabled);
      npuModel->setEnabled (enabled);
      chooseNpuModel->setEnabled (enabled);
      npuTokenizer->setEnabled (enabled);
      chooseNpuTokenizer->setEnabled (enabled);
    };
  QObject::connect (realtimeNpu, &QCheckBox::toggled,
                    [refreshRealtimeNpuControls] () {
    refreshRealtimeNpuControls ();
  });
  refreshRealtimeNpuControls ();
  QLineEdit* bearerToken= add_line_edit (rag, "rag mcp bearer token");
  QWidget* tokenButtons= new QWidget (connectivity);
  QHBoxLayout* tokenLayout= new QHBoxLayout (tokenButtons);
  tokenLayout->setContentsMargins (0, 0, 0, 0);
  QPushButton* generateToken= new QPushButton ("Generate token", tokenButtons);
  QPushButton* copyToken= new QPushButton ("Copy token", tokenButtons);
  tokenLayout->addWidget (generateToken);
  tokenLayout->addWidget (copyToken);
  tokenLayout->addStretch (1);
  rag->addRow (label ("Token management:"), tokenButtons);
  QObject::connect (generateToken, &QPushButton::clicked,
                    [bearerToken] () {
    QString token= random_hex_token (32);
    bearerToken->setText (token);
    set_pref ("rag mcp bearer token", token);
  });
  QObject::connect (copyToken, &QPushButton::clicked,
                    [bearerToken] () {
    QApplication::clipboard ()->setText (bearerToken->text ());
  });
  QLabel* ragNote= new QLabel (
    "Start the local read-only MCP server with ATHENA.bin -H --rag-server "
    "<vault-root>. The endpoint is http://127.0.0.1:<port>/mcp and requires "
    "the bearer token above.");
  ragNote->setWordWrap (true);
  connectivity->layout ()->addWidget (ragNote);
  finish_page (connectivity);

  return tabbed ({{"AI", ai}, {"Connectivity", connectivity},
                  {"Security", security}, {"Debugging", debugging}});
}

void
qtm_preferences_dialog_show () {
  if (headless_mode) return;
  if (qt_defer_to_main_thread (qtm_preferences_dialog_show)) return;
  if (activePreferencesDialog) {
    activePreferencesDialog->raise ();
    activePreferencesDialog->activateWindow ();
    return;
  }
  activePreferencesDialog= new QTMPreferencesDialog (QApplication::activeWindow ());
  activePreferencesDialogOpen.store (true, std::memory_order_release);
  QObject::connect (activePreferencesDialog, &QObject::destroyed, [] () {
    activePreferencesDialogOpen.store (false, std::memory_order_release);
  });
  activePreferencesDialog->show ();
}

bool
qtm_preferences_dialog_open () {
  return activePreferencesDialogOpen.load (std::memory_order_acquire);
}

int
qtm_preferences_export_privacy_dialog () {
  if (headless_mode) return 0;
  QMessageBox dialog (QMessageBox::Question, "View all preferences",
    "The preferences export may contain access tokens, authentication "
    "information, and other credentials. Redact sensitive values?",
    QMessageBox::NoButton, QApplication::activeWindow ());
  dialog.setInformativeText (
    "Redaction is recommended before sharing the generated ATHENA document.");
  QPushButton* redact= dialog.addButton ("Redact sensitive values",
                                         QMessageBox::AcceptRole);
  QPushButton* include= dialog.addButton ("Include sensitive values",
                                          QMessageBox::DestructiveRole);
  dialog.addButton (QMessageBox::Cancel);
  dialog.setDefaultButton (redact);
  dialog.exec ();
  if (dialog.clickedButton () == redact) return 1;
  if (dialog.clickedButton () == include) return 2;
  return 0;
}

QStringList
qtm_preferences_export_metadata () {
  if (activePreferencesDialog)
    return activePreferencesDialog->exportMetadata ();
  collectingPreferencesMetadata= true;
  QTMPreferencesDialog dialog;
  collectingPreferencesMetadata= false;
  return dialog.exportMetadata ();
}

void
qtm_page_setup_dialog_show () {
  if (headless_mode) return;
  if (qt_defer_to_main_thread (qtm_page_setup_dialog_show)) return;
  if (activePageSetupDialog) {
    activePageSetupDialog->raise ();
    activePageSetupDialog->activateWindow ();
    return;
  }

  QDialog* dialog= new QDialog (QApplication::activeWindow ());
  activePageSetupDialog= dialog;
  dialog->setAttribute (Qt::WA_DeleteOnClose, true);
  dialog->setWindowTitle ("Page setup");
  dialog->resize (520, 260);

  QVBoxLayout* outer= new QVBoxLayout (dialog);
  outer->setContentsMargins (20, 18, 20, 18);
  outer->setSpacing (12);

  QFormLayout* form= new QFormLayout;
  form->setFieldGrowthPolicy (QFormLayout::ExpandingFieldsGrow);
  form->setRowWrapPolicy (QFormLayout::DontWrapRows);
  form->setHorizontalSpacing (24);
  form->setVerticalSpacing (10);
  outer->addLayout (form);

  add_page_setup_combo (
    form, "Preview command:", "preview command",
    {{"default", "default"}, {"ggv", "ggv"}, {"ghostview", "ghostview"},
     {"gv", "gv"}, {"kghostview", "kghostview"}, {"open", "open"},
     {"", ""}},
    "default");

  QString printingDefault= scheme_string ("get-default-printing-command",
                                          "lpr");
  std::vector<QStringChoice> printingChoices= {
    {printingDefault, printingDefault}, {"lpr", "lpr"}, {"lp", "lp"},
    {"pdq", "pdq"}, {"", ""}};
  add_page_setup_combo (form, "Printing command:", "printing command",
                        printingChoices, printingDefault);

  add_page_setup_combo (
    form, "Paper type:", "paper type",
    {{"default", "default"}, {"A3", "A3"}, {"A4", "A4"}, {"A5", "A5"},
     {"B4", "B4"}, {"B5", "B5"}, {"B6", "B6"}, {"Letter", "Letter"},
     {"Legal", "Legal"}, {"Executive", "Executive"}, {"", ""}},
    scheme_string ("get-default-paper-size", "A4"));

  add_page_setup_combo (
    form, "Printer dpi:", "printer dpi",
    {{"150", "150"}, {"200", "200"}, {"300", "300"}, {"400", "400"},
     {"600", "600"}, {"800", "800"}, {"1200", "1200"},
     {"2400", "2400"}, {"", ""}},
    "1200");

  outer->addStretch (1);
  QDialogButtonBox* buttons= new QDialogButtonBox (QDialogButtonBox::Close,
                                                   dialog);
  QObject::connect (buttons, &QDialogButtonBox::rejected,
                    dialog, &QDialog::close);
  outer->addWidget (buttons);

  QObject::connect (dialog, &QObject::destroyed, [] () {
    activePageSetupDialog= nullptr;
  });
  dialog->show ();
}
