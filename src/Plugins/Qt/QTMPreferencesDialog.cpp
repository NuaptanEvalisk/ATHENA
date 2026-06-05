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

#include "boot.hpp"
#include "font.hpp"
#include "scheme.hpp"
#include "tm_ostream.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>
#include <utility>
#include <vector>

namespace {

struct Choice {
  const char* value;
  const char* label;
};

using QStringChoice = std::pair<QString, QString>;

static QPointer<QTMPreferencesDialog> activePreferencesDialog;
static QPointer<QDialog> activePageSetupDialog;

static QString
to_qstring_pref (string s) {
  return QString::fromUtf8 (as_charp (s), N(s));
}

static string
from_qstring_pref (const QString& s) {
  QByteArray bytes= s.toUtf8 ();
  return string (bytes.constData ());
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
  (void) call ("notify-restart");
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
category_icon (QStyle* style, const QString& name) {
  if (name == "General")
    return pixmap_icon ("$ATHENA_PATH/misc/pixmaps/modern/32x32/settings/tm_prefs_general.png");
  if (name == "Keyboard")
    return pixmap_icon ("$ATHENA_PATH/misc/pixmaps/modern/32x32/settings/tm_prefs_keyboard.png");
  if (name == "Rendering")
    return pixmap_icon ("$ATHENA_PATH/misc/pixmaps/modern/20x20/mode/tm_view.svg");
  if (name == "Convert")
    return pixmap_icon ("$ATHENA_PATH/misc/pixmaps/modern/32x32/settings/tm_prefs_convert.png");
  if (name == "Vault")
    return pixmap_icon ("$ATHENA_PATH/misc/pixmaps/modern/20x20/mode/tm_link.svg");
  if (name == "Other")
    return pixmap_icon ("$ATHENA_PATH/misc/pixmaps/modern/32x32/settings/tm_prefs_other.png");
  if (name == "Editing")
    return style->standardIcon (QStyle::SP_FileIcon);
  return style->standardIcon (QStyle::SP_FileDialogDetailedView);
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
add_toggle (QFormLayout* form, const QString& title, const char* key) {
  QCheckBox* box= new QCheckBox;
  box->setChecked (pref_on (key));
  QObject::connect (box, &QCheckBox::toggled, [key] (bool on) {
    set_bool_pref (key, on);
  });
  form->addRow (label (title), box);
  return box;
}

static QLineEdit*
add_line_edit (QFormLayout* form, const QString& title, const char* key,
               const char* def= "", bool password= false) {
  QLineEdit* edit= new QLineEdit;
  edit->setText (pref (key, def));
  if (password) edit->setEchoMode (QLineEdit::Password);
  QObject::connect (edit, &QLineEdit::editingFinished, [key, edit] () {
    set_pref (key, edit->text ());
  });
  form->addRow (label (title), edit);
  return edit;
}

static QComboBox*
add_combo (QFormLayout* form, const QString& title, const char* key,
           const std::vector<Choice>& choices, const char* def= "default",
           bool restart= false) {
  QComboBox* combo= new QComboBox;
  QString cur= pref (key, def);
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
  QObject::connect (combo,
                    static_cast<void (QComboBox::*) (int)> (
                      &QComboBox::currentIndexChanged),
                    [key, combo, restart] (int index) {
    if (index >= 0) set_pref (key, combo->itemData (index).toString ());
    if (restart) notify_restart ();
  });
  form->addRow (label (title), combo);
  return combo;
}

static QComboBox*
add_qstring_combo (QFormLayout* form, const QString& title, const char* key,
                   const std::vector<QStringChoice>& choices,
                   const char* def= "default", bool restart= false) {
  QComboBox* combo= new QComboBox;
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
                    [key, combo, restart] (int index) {
    if (index < 0) return;
    set_pref (key, combo->itemData (index).toString ());
    if (restart) notify_restart ();
  });
  form->addRow (label (title), combo);
  return combo;
}

static QPushButton*
add_color_button (QFormLayout* form, const QString& title, const char* key,
                  bool optional) {
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

  if (!optional) form->addRow (label (title), button);
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
    form->addRow (label (title), row);
  }
  return button;
}

static QWidget*
tabbed (std::initializer_list<std::pair<QString, QWidget*> > tabs) {
  QTabWidget* tab= new QTabWidget;
  for (auto& p: tabs)
    tab->addTab (make_scroll_page (p.second), p.first);
  return tab;
}

static std::vector<QStringChoice>
basic_language_choices () {
  return {{"english", "English"}, {"french", "French"},
          {"german", "German"}, {"spanish", "Spanish"},
          {"italian", "Italian"}, {"portuguese", "Portuguese"},
          {"dutch", "Dutch"}, {"russian", "Russian"},
          {"ukrainian", "Ukrainian"}, {"chinese", "Chinese"},
          {"japanese", "Japanese"}, {"korean", "Korean"}};
}

static std::vector<QStringChoice>
script_choices () {
  std::vector<QStringChoice> choices;
  choices.push_back ({"none", "None"});
  (void) call ("lazy-plugin-force");
  list<string> scripts= as_list_string (call ("scripts-list"));
  for (list<string> it= scripts; !is_nil (it); it= it->next) {
    QString value= to_qstring_pref (it->item);
    QString label= to_qstring_pref (as_string (call ("scripts-name",
                                                     it->item)));
    choices.push_back ({value, label});
  }
  return choices;
}

using PrefValue = Choice;
using Preset = std::pair<const char*, std::vector<PrefValue> >;

static const std::vector<Preset>&
enunciation_presets () {
  static const std::vector<Preset> presets= {
    {"Solarized Light",
     {{"vault theorem color", "#eee8d5"},
      {"vault lemma color", "#e7f1df"},
      {"vault corollary color", "#ddeef2"},
      {"vault proposition color", "#e8e2f2"},
      {"vault axiom color", "#f4e1dc"},
      {"vault definition color", "#f1ead2"},
      {"vault notation color", "#dfeee8"},
      {"vault convention color", "#ebe6d6"},
      {"vault conjecture color", "#e0eaf2"},
      {"vault law color", "#f3e7c8"},
      {"vault remark color", "#e6ead8"},
      {"vault note color", "#dcecf0"},
      {"vault example color", "#e9efd5"},
      {"vault warning color", "#f5dfd6"},
      {"vault disambiguation color", "#e2e6f1"},
      {"vault acknowledgments color", "#f0e2ea"},
      {"vault exercise color", "#e4efd8"},
      {"vault problem color", "#dcebe3"},
      {"vault question color", "#f2e8d1"}}},
    {"Gruvbox Light",
     {{"vault theorem color", "#f1e5c0"},
      {"vault lemma color", "#e5ecc4"},
      {"vault corollary color", "#dce9d1"},
      {"vault proposition color", "#d7e7de"},
      {"vault axiom color", "#ead6cb"},
      {"vault definition color", "#f0dec2"},
      {"vault notation color", "#e8e0c5"},
      {"vault convention color", "#dfe4c8"},
      {"vault conjecture color", "#d8e2cf"},
      {"vault law color", "#ecd9bf"},
      {"vault remark color", "#eee8cc"},
      {"vault note color", "#d9e8dc"},
      {"vault example color", "#e3e9c2"},
      {"vault warning color", "#f1d7c7"},
      {"vault disambiguation color", "#dedde8"},
      {"vault acknowledgments color", "#ead9dd"},
      {"vault exercise color", "#e1edcb"},
      {"vault problem color", "#d8e5d6"},
      {"vault question color", "#efe0c8"}}},
    {"Catppuccin Latte",
     {{"vault theorem color", "#e8e4f4"},
      {"vault lemma color", "#dfe8f4"},
      {"vault corollary color", "#dcecf0"},
      {"vault proposition color", "#dceee8"},
      {"vault axiom color", "#f2e1e6"},
      {"vault definition color", "#f1e4d5"},
      {"vault notation color", "#e7ead7"},
      {"vault convention color", "#e4e8dc"},
      {"vault conjecture color", "#e6e1ef"},
      {"vault law color", "#f3dfd9"},
      {"vault remark color", "#ebe7d6"},
      {"vault note color", "#e1e7f2"},
      {"vault example color", "#e4eedf"},
      {"vault warning color", "#f4dfd4"},
      {"vault disambiguation color", "#e0e3f0"},
      {"vault acknowledgments color", "#efdfec"},
      {"vault exercise color", "#dfeee1"},
      {"vault problem color", "#dfe9ed"},
      {"vault question color", "#f0e7d8"}}},
    {"Everforest Light",
     {{"vault theorem color", "#e7e8c9"},
      {"vault lemma color", "#dce9c8"},
      {"vault corollary color", "#d8e8d0"},
      {"vault proposition color", "#d6e7dc"},
      {"vault axiom color", "#edd6c8"},
      {"vault definition color", "#eee0c2"},
      {"vault notation color", "#e1e6c7"},
      {"vault convention color", "#e5dfca"},
      {"vault conjecture color", "#d9e2db"},
      {"vault law color", "#f0dac4"},
      {"vault remark color", "#e9e4c8"},
      {"vault note color", "#d7e7df"},
      {"vault example color", "#e1eac9"},
      {"vault warning color", "#f0d4c6"},
      {"vault disambiguation color", "#dce1e3"},
      {"vault acknowledgments color", "#ead8da"},
      {"vault exercise color", "#d9eacb"},
      {"vault problem color", "#d7e5d7"},
      {"vault question color", "#ebe1c9"}}},
    {"Nord Light",
     {{"vault theorem color", "#e5e9f0"},
      {"vault lemma color", "#dfeaf2"},
      {"vault corollary color", "#dcecf0"},
      {"vault proposition color", "#dceeea"},
      {"vault axiom color", "#ece3ed"},
      {"vault definition color", "#f0e4d8"},
      {"vault notation color", "#e7ebdd"},
      {"vault convention color", "#e3e8e2"},
      {"vault conjecture color", "#e2e5f1"},
      {"vault law color", "#f1e0d5"},
      {"vault remark color", "#e8eadc"},
      {"vault note color", "#dde8f1"},
      {"vault example color", "#e2eddc"},
      {"vault warning color", "#f1ded4"},
      {"vault disambiguation color", "#e0e4ec"},
      {"vault acknowledgments color", "#ece0e9"},
      {"vault exercise color", "#dfece0"},
      {"vault problem color", "#dce9e7"},
      {"vault question color", "#eee6da"}}}
  };
  return presets;
}

static QStringList
enunciation_preset_names () {
  QStringList names;
  for (const Preset& preset: enunciation_presets ())
    names << preset.first;
  return names;
}

static QString
current_enunciation_preset () {
  QString current= pref ("enunciation color preset", "Solarized Light");
  if (enunciation_preset_names ().contains (current)) return current;
  return "Solarized Light";
}

static void
apply_enunciation_preset (const QString& name) {
  std::string wanted= name.toUtf8 ().constData ();
  for (const Preset& preset: enunciation_presets ()) {
    if (wanted != preset.first) continue;
    for (const PrefValue& value: preset.second)
      set_preference (value.value, value.label);
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
    pageStack (new QStackedWidget (this))
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
  f.setPointSize (f.pointSize () + 4);
  f.setBold (true);
  title->setFont (f);
  headerLayout->addWidget (title);
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
  addCategory ("Vault", buildVaultPage ());
  addCategory ("Other", buildOtherPage ());

  if (categoryList->count () > 0) categoryList->setCurrentRow (0);
  QObject::connect (categoryList, &QListWidget::currentRowChanged,
                    pageStack, &QStackedWidget::setCurrentIndex);
}

void
QTMPreferencesDialog::addCategory (const QString& name, QWidget* page) {
  QIcon icon= category_icon (style (), name);
  QListWidgetItem* item= new QListWidgetItem (icon, name, categoryList);
  item->setSizeHint (QSize (180, 40));
  pageStack->addWidget (page);
}

QWidget*
QTMPreferencesDialog::buildGeneralPage () {
  QWidget* basic= make_page ();
  QFormLayout* basicForm= add_section (basic, "Basic");
  add_qstring_combo (basicForm, "User interface language:", "language",
                     basic_language_choices ());
  add_combo (basicForm, "Complex actions:", "complex actions",
             {{"menus", "Through the menus"},
              {"popups", "Through popup windows"}});
  add_combo (basicForm, "Interactive questions:", "interactive questions",
             {{"footer", "On the footer"}, {"popup", "In popup windows"}});
  add_combo (basicForm, "Details in menus:", "detailed menus",
             {{"simple", "Simplified menus"}, {"detailed", "Detailed menus"}});
  add_combo (basicForm, "Buffer management:", "buffer management",
             {{"separate", "Documents in separate windows"},
              {"shared", "Multiple documents share window"},
              {"mdi", "Multiple documents in sub-windows (MDI)"},
              {"ads", "Advanced Docking System"}});
  add_toggle (basicForm, "Remember panes layout:",
              "remember ads panes layout");
  add_combo (basicForm, "Automatically save:", "autosave",
             {{"5", "5 sec"}, {"30", "30 sec"}, {"120", "120 sec"},
              {"300", "300 sec"}, {"0", "Disable"}});
  add_toggle (basicForm, "Autosave by default:", "autosave default");
  add_toggle (basicForm, "Use case-insensitive search:",
              "case-insensitive-match");
  finish_page (basic);

  QWidget* appearance= make_page ();
  QFormLayout* appearanceForm= add_section (appearance, "Appearance");
  add_combo (appearanceForm, "Look and feel:", "look and feel",
             {{"default", "Default"}, {"emacs", "Emacs"}, {"gnome", "Gnome"},
              {"kde", "KDE"}, {"macos", "Mac OS"}, {"windows", "Windows"}},
             "default", true);
  add_combo (appearanceForm, "User interface theme:", "gui theme",
             {{"default", "Default"}, {"light", "Bright"}, {"dark", "Dark"},
              {"native-light", "Native"}, {"", "Legacy"}}, "default", true);
  add_toggle (appearanceForm, "Use inertial scrolling:", "inertial scrolling");
  add_line_edit (appearanceForm, "Inertial momentum (0.80-0.99):",
                 "inertial scrolling friction", "0.95");
  add_line_edit (appearanceForm, "Inertial sensitivity multiplier:",
                 "inertial scrolling sensitivity", "1.0");
  add_toggle (appearanceForm, "Use multi-tabs:", "enable tab");
  add_toggle (appearanceForm, "Use print dialogue:", "gui:print dialogue");
  add_toggle (appearanceForm, "Disable window positioning:",
              "disable texmacs window positioning");
  add_toggle (appearanceForm, "New bibliography dialogue:",
              "gui:new bibliography dialogue");
  add_toggle (appearanceForm, "Show live statistics in central footer:",
              "gui:live-statistics");
  add_toggle (appearanceForm, "Use toast notifications:",
              "use toast notifications");
  finish_page (appearance);

  QWidget* fonts= make_page ();
  QFormLayout* styling= add_section (fonts, "Styling");
  add_toggle (styling, "New style fonts:", "new style fonts");
  add_toggle (styling, "Advanced font customization:",
              "advanced font customization");
  add_toggle (styling, "Show warning for font substitution:",
              "show font substitution warning");

  QGroupBox* preferredBox= new QGroupBox ("Preferred fonts", fonts);
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
  QPushButton* scan= new QPushButton ("Scan disk for fonts");
  QObject::connect (scan, &QPushButton::clicked, [] () {
    (void) call ("scan-disk-for-fonts");
  });
  maintenance->addRow (label ("Scan for system fonts:"), scan);
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
  add_combo (form, "Space bar in text mode:", "text spacebar",
             {{"default", "Default"},
              {"no multiple spaces", "No multiple spaces"},
              {"glue multiple spaces", "Glue multiple spaces"},
              {"allow multiple spaces", "Allow multiple spaces"}});
  add_combo (form, "Space bar in math mode:", "math spacebar",
             {{"default", "Default"},
              {"no spurious spaces", "No spurious spaces"},
              {"avoid spurious spaces", "Avoid spurious spaces"},
              {"allow spurious spaces", "Allow spurious spaces"}});
  add_combo (form, "Automatic quotes:", "automatic quotes",
             {{"default", "Default"}, {"none", "Disabled"},
              {"dutch", "Dutch"}, {"english", "English"},
              {"french", "French"}, {"german", "German"},
              {"spanish", "Spanish"}, {"swiss", "Swiss"}});
  add_combo (form, "Automatic brackets:", "automatic brackets",
             {{"off", "Disabled"}, {"on", "Enabled"},
              {"mathematics", "Inside mathematics"}});
  add_combo (form, "Cyrillic input method:", "cyrillic input method",
             {{"none", "None"}, {"translit", "Translit"},
              {"jcuken", "Jcuken"}, {"yawerty", "Yawerty"}});
  QPushButton* shortcuts= new QPushButton ("Edit keyboard shortcuts");
  QObject::connect (shortcuts, &QPushButton::clicked, [] () {
    (void) call ("open-shortcuts-editor", string (""), string (""));
  });
  form->addRow (label ("Advanced settings:"), shortcuts);
  finish_page (input);

  QWidget* remote= make_page ();
  QFormLayout* r= add_section (remote, "Remote Control");
  add_combo (r, "Left:", "ir-left", {{"pageup", "pageup"}, {"", ""}}, "");
  add_combo (r, "Right:", "ir-right", {{"pagedown", "pagedown"}, {"", ""}},
             "");
  add_combo (r, "Up:", "ir-up", {{"home", "home"}, {"", ""}}, "");
  add_combo (r, "Down:", "ir-down", {{"end", "end"}, {"", ""}}, "");
  add_combo (r, "Center:", "ir-center",
             {{"return", "return"}, {"S-return", "S-return"}, {"", ""}}, "");
  add_combo (r, "Play:", "ir-play", {{"F5", "F5"}, {"", ""}}, "");
  add_combo (r, "Pause:", "ir-pause", {{"escape", "escape"}, {"", ""}}, "");
  add_combo (r, "Menu:", "ir-menu", {{".", "."}, {"", ""}}, "");
  finish_page (remote);

  return tabbed ({{"Input", input}, {"Remote Control", remote}});
}

QWidget*
QTMPreferencesDialog::buildEditingPage () {
  QWidget* math= make_page ();
  QFormLayout* mk= add_section (math, "Math keyboard");
  add_toggle (mk, "Use spurious invisible operators:", "automatic invisible");
  add_toggle (mk, "Use shortcuts for missing invisible operators:",
              "manual insert missing invisible");
  add_toggle (mk, "Homoglyph substitutions:", "manual homoglyph correct");
  QFormLayout* mh= add_section (math, "Math hints and semantics");
  add_toggle (mh, "Semantic editing:", "semantic editing");
  add_toggle (mh, "Semantic selections:", "semantic selections");
  add_toggle (mh, "Semantic focus:", "semantic focus");
  finish_page (math);

  QWidget* programming= make_page ();
  QFormLayout* p= add_section (programming, "Programming");
  add_qstring_combo (p, "Scripting language:", "scripting language",
                     script_choices ());
  add_toggle (p, "Highlight matching brackets:", "prog:highlight brackets");
  add_toggle (p, "Automatic program brackets:", "prog:automatic brackets");
  add_toggle (p, "Use smart bracket selections:", "prog:select brackets");
  finish_page (programming);

  QWidget* text= make_page ();
  QFormLayout* t= add_section (text, "Text");
  add_toggle (t, "Show heading word counts:", "heading word counts");
  add_toggle (t, "Check spelling as you type:", "live spell checking");
  add_toggle (t, "Disable UNIX primary selection:",
              "disable unix primary selection");
  add_qstring_combo (t, "Custom dictionary language:",
                     "custom dictionary import language",
                     basic_language_choices ());
  QPushButton* import= new QPushButton ("Import");
  QObject::connect (import, &QPushButton::clicked, [] () {
    (void) call ("spell-live-import-custom-dictionary-from-preferences");
  });
  t->addRow (label ("Custom dictionary:"), import);
  finish_page (text);

  QWidget* importer= make_page ();
  QFormLayout* i= add_section (importer, "Formula Importer");
  add_toggle (i, "Recognize matrices and determinants disguised as arrays:",
              "latex->texmacs:matrix-recognition");
  add_toggle (i, "Treat 'align' as 'aligned':",
              "latex->texmacs:align-to-aligned");
  add_toggle (i, "Convert 'aligned' blocks into 'eqnarray' environments:",
              "latex->texmacs:aligned-to-eqnarray");
  add_toggle (i, "Parse operator d as differential d:",
              "latex->texmacs:operator-d-is-differential");
  add_toggle (i, "Parse Roman d as differential d:",
              "latex->texmacs:roman-d-is-differential");
  add_toggle (i, "Parse text d as differential d:",
              "latex->texmacs:text-d-is-differential");
  add_toggle (i, "Parse blackboard k as Bbbk:",
              "latex->texmacs:parse-bbbk");
  add_toggle (i, "Parse blackboard i as mathi:",
              "latex->texmacs:parse-bbbi-as-mathi");
  add_toggle (i, "Recognize operator names disguised as text:",
              "latex->texmacs:text-operators");
  finish_page (importer);

  return tabbed ({{"Maths", math}, {"Programming", programming},
                  {"Text", text}, {"Formula Importer", importer}});
}

QWidget*
QTMPreferencesDialog::buildRenderingPage () {
  QWidget* document= make_page ();
  QFormLayout* d= add_section (document, "Document");
  add_color_button (d, "Transclusion background:", "vault transclusion color",
                    true);
  add_combo (d, "Default CJK language:", "default cjk language",
             {{"chinese", "Chinese"}, {"japanese", "Japanese"},
              {"korean", "Korean"}, {"taiwanese", "Taiwanese"}});
  add_toggle (d, "Render exercises in smaller font:",
              "render solution in smaller font");
  add_toggle (d, "Number solutions:", "number solutions");
  add_color_button (d, "Cursor color:", "gui cursor color", false);
  add_color_button (d, "Selection color:", "gui selection color", false);
  add_color_button (d, "Focus box color:", "gui focus color", false);
  add_combo (d, "Focus box border:", "gui focus border width",
             {{"1", "1"}, {"2", "2"}, {"3", "3"}, {"4", "4"}, {"5", "5"},
              {"6", "6"}}, "1");
  add_color_button (d, "Unclicked link color:", "locus-color", false);
  add_color_button (d, "Clicked link color:", "visited-color", false);
  add_toggle (d, "Override white background:",
              "override white document background");
  add_color_button (d, "White background color:",
                    "white document background override color", false);
  add_combo (d, "Labels display:", "vault labels mode",
             {{"visible", "visible"}, {"small", "small"}, {"hidden", "hidden"}},
             "visible");
  add_toggle (d, "Persistent fit width:", "persistent fit width");
  add_toggle (d, "Alpha transparency:", "experimental alpha");
  add_toggle (d, "New style page breaking:", "new style page breaking");
  add_combo (d, "Document updates run:", "document update times",
             {{"1", "Once"}, {"2", "Twice"}, {"3", "Three times"}});
  add_toggle (d, "Fast environments:", "fast environments");
  finish_page (document);

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
  QHBoxLayout* presetLayout= new QHBoxLayout (presetRow);
  presetLayout->setContentsMargins (0, 0, 0, 0);
  presetLayout->addWidget (presetCombo, 1);
  presetLayout->addWidget (apply);
  preset->addRow (label ("Preset:"), presetRow);

  QFormLayout* en= add_section (colors, "Enunciations");
  add_color_button (en, "Theorem:", "vault theorem color", true);
  add_color_button (en, "Lemma:", "vault lemma color", true);
  add_color_button (en, "Corollary:", "vault corollary color", true);
  add_color_button (en, "Proposition:", "vault proposition color", true);
  add_color_button (en, "Axiom:", "vault axiom color", true);
  add_color_button (en, "Definition:", "vault definition color", true);
  add_color_button (en, "Notation:", "vault notation color", true);
  add_color_button (en, "Convention:", "vault convention color", true);
  add_color_button (en, "Conjecture:", "vault conjecture color", true);
  add_color_button (en, "Law:", "vault law color", true);

  QFormLayout* rem= add_section (colors, "Remarks and notes");
  add_color_button (rem, "Remark:", "vault remark color", true);
  add_color_button (rem, "Note:", "vault note color", true);
  add_color_button (rem, "Example:", "vault example color", true);
  add_color_button (rem, "Warning:", "vault warning color", true);
  add_color_button (rem, "Disambiguation:", "vault disambiguation color", true);
  add_color_button (rem, "Acknowledgments:", "vault acknowledgments color",
                    true);

  QFormLayout* ex= add_section (colors, "Exercises and proofs");
  add_color_button (ex, "Exercise:", "vault exercise color", true);
  add_color_button (ex, "Problem:", "vault problem color", true);
  add_color_button (ex, "Question:", "vault question color", true);
  add_color_button (ex, "Solution:", "vault solution color", true);
  add_color_button (ex, "Answer:", "vault answer color", true);
  add_color_button (ex, "Proof:", "vault proof color", true);
  add_color_button (ex, "Proof (Alternative):", "vault proof alternative color",
                    true);
  add_color_button (ex, "Proof (Standard):", "vault proof standard color",
                    true);
  finish_page (colors);

  return tabbed ({{"Document", document}, {"Enunciation Colors", colors}});
}

QWidget*
QTMPreferencesDialog::buildConversionPage () {
  QWidget* html= make_page ();
  QFormLayout* h1= add_section (html, "TeXmacs -> Html");
  add_toggle (h1, "Use CSS for more advanced formatting:",
              "texmacs->html:css");
  QCheckBox* mathjax= add_toggle (h1, "Export mathematical formulas as MathJax:",
                                  "texmacs->html:mathjax");
  QCheckBox* mathml= add_toggle (h1, "Export mathematical formulas as MathML:",
                                 "texmacs->html:mathml");
  QCheckBox* images= add_toggle (h1, "Export mathematical formulas as images:",
                                 "texmacs->html:images");
  auto exclusiveHtml= [mathjax, mathml, images] (QCheckBox* active,
                                                 const char* key, bool on) {
    set_bool_pref (key, on);
    if (!on) return;
    for (QCheckBox* box: {mathjax, mathml, images}) {
      if (box == active) continue;
      QSignalBlocker block (box);
      box->setChecked (false);
    }
    if (active != mathjax) set_bool_pref ("texmacs->html:mathjax", false);
    if (active != mathml) set_bool_pref ("texmacs->html:mathml", false);
    if (active != images) set_bool_pref ("texmacs->html:images", false);
  };
  QObject::connect (mathjax, &QCheckBox::toggled,
                    [=] (bool on) { exclusiveHtml (mathjax,
                                                   "texmacs->html:mathjax",
                                                   on); });
  QObject::connect (mathml, &QCheckBox::toggled,
                    [=] (bool on) { exclusiveHtml (mathml,
                                                   "texmacs->html:mathml",
                                                   on); });
  QObject::connect (images, &QCheckBox::toggled,
                    [=] (bool on) { exclusiveHtml (images,
                                                   "texmacs->html:images",
                                                   on); });
  add_combo (h1, "CSS stylesheet:", "texmacs->html:css-stylesheet",
             {{"---", "---"},
              {"https://www.texmacs.org/css/web-article.css",
               "web-article.css"},
              {"https://www.texmacs.org/css/web-article-dark.css",
               "web-article-dark.css"},
              {"https://www.texmacs.org/css/web-article-colored.css",
               "web-article-colored.css"},
              {"https://www.texmacs.org/css/web-article-dark-colored.css",
               "web-article-dark-colored.css"},
              {"", ""}}, "---");
  QFormLayout* h2= add_section (html, "Html -> TeXmacs");
  add_toggle (h2, "Try to import formulas using LaTeX annotations:",
              "mathml->texmacs:latex-annotations");
  finish_page (html);

  QWidget* latex= make_page ();
  QFormLayout* l1= add_section (latex, "LaTeX -> TeXmacs");
  add_toggle (l1, "Import sophisticated objects as pictures:",
              "latex->texmacs:fallback-on-pictures");
  QFormLayout* l2= add_section (latex, "TeXmacs -> LaTeX");
  add_toggle (l2, "Replace TeXmacs styles with no LaTeX equivalents:",
              "texmacs->latex:replace-style");
  add_toggle (l2, "Expand TeXmacs macros with no LaTeX equivalents:",
              "texmacs->latex:expand-macros");
  add_toggle (l2, "Expand user-defined macros:",
              "texmacs->latex:expand-user-macros");
  add_toggle (l2, "Export bibliographies as links:",
              "texmacs->latex:indirect-bib");
  add_toggle (l2, "Allow for macro definitions in preamble:",
              "texmacs->latex:use-macros");
  add_combo (l2, "Character encoding:", "texmacs->latex:encoding",
             {{"ascii", "Ascii"}, {"cork", "Cork with catcodes"},
              {"utf-8", "Utf-8 with inputenc"}});
  QFormLayout* l3= add_section (latex, "Conservative conversion options");
  QCheckBox* sourceTracking= new QCheckBox;
  sourceTracking->setChecked (pref_on ("latex->texmacs:source-tracking") ||
                              pref_on ("texmacs->latex:source-tracking"));
  QObject::connect (sourceTracking, &QCheckBox::toggled, [] (bool on) {
    set_bool_pref ("latex->texmacs:source-tracking", on);
    set_bool_pref ("texmacs->latex:source-tracking", on);
  });
  l3->addRow (label ("Keep track of source code:"), sourceTracking);
  QCheckBox* conservative= new QCheckBox;
  conservative->setChecked (pref_on ("latex->texmacs:conservative") &&
                            pref_on ("texmacs->latex:conservative"));
  QObject::connect (conservative, &QCheckBox::toggled, [] (bool on) {
    set_bool_pref ("latex->texmacs:conservative", on);
    set_bool_pref ("texmacs->latex:conservative", on);
  });
  l3->addRow (label ("Only convert changes with respect to tracked version:"),
              conservative);
  add_toggle (l3, "Guarantee transparent source tracking:",
              "latex->texmacs:transparent-source-tracking");
  add_toggle (l3, "Store tracking information in LaTeX files:",
              "texmacs->latex:attach-tracking-info");
  finish_page (latex);

  QWidget* bibtex= make_page ();
  QFormLayout* b1= add_section (bibtex, "BibTeX -> TeXmacs");
  add_combo (b1, "BibTeX command:", "bibtex command",
             {{"bibtex", "bibtex"}, {"biber", "biber"},
              {"biblatex", "biblatex"}, {"rubibtex", "rubibtex"}, {"", ""}});
  add_toggle (b1, "Only convert changes when re-importing:",
              "bibtex->texmacs:conservative");
  QFormLayout* b2= add_section (bibtex, "TeXmacs -> BibTeX");
  add_toggle (b2, "Only convert changes with respect to imported version:",
              "texmacs->bibtex:conservative");
  finish_page (bibtex);

  QWidget* verbatim= make_page ();
  QFormLayout* v1= add_section (verbatim, "TeXmacs -> Verbatim");
  add_toggle (v1, "Use line wrapping for lines longer than 80 characters:",
              "texmacs->verbatim:wrap");
  add_combo (v1, "Character encoding:", "texmacs->verbatim:encoding",
             {{"auto", "Automatic"}, {"cork", "Cork"},
              {"iso-8859-1", "Iso-8859-1"},
              {"iso-8859-2", "Iso-8859-2"}, {"utf-8", "Utf-8"}});
  QFormLayout* v2= add_section (verbatim, "Verbatim -> TeXmacs");
  add_toggle (v2, "Merge lines into paragraphs unless separated by blank lines:",
              "verbatim->texmacs:wrap");
  add_combo (v2, "Character encoding:", "verbatim->texmacs:encoding",
             {{"auto", "Automatic"}, {"cork", "Cork"},
              {"iso-8859-1", "Iso-8859-1"},
              {"iso-8859-2", "Iso-8859-2"}, {"utf-8", "Utf-8"}});
  finish_page (verbatim);

  QWidget* pdf= make_page ();
  QFormLayout* pdfForm= add_section (pdf, "TeXmacs -> Pdf/Postscript");
  if (scheme_bool ("supports-native-pdf?"))
    add_toggle (pdfForm, "Produce Pdf using native export filter:",
                "native pdf");
  if (scheme_bool ("supports-ghostscript?"))
    add_toggle (pdfForm, "Produce Postscript using native export filter:",
                "native postscript");
  add_toggle (pdfForm, "Expand beamer slides:",
              "texmacs->pdf:expand slides");
  add_toggle (pdfForm, "Generate DataArt cover image when exporting:",
              "texmacs->pdf:data-art cover");
  if (scheme_bool ("supports-native-pdf?")) {
    add_toggle (pdfForm, "Distill encapsulated Pdf files:",
                "texmacs->pdf:distill inclusion");
    add_toggle (pdfForm, "Check exported Pdf files for correctness:",
                "texmacs->pdf:check");
    add_combo (pdfForm, "Pdf version number:", "texmacs->pdf:version",
               {{"default", "default"}, {"1.4", "1.4"}, {"1.5", "1.5"},
                {"1.6", "1.6"}, {"1.7", "1.7"}}, "default");
  }
  finish_page (pdf);

  QWidget* image= make_page ();
  QFormLayout* im1= add_section (image, "TeXmacs -> Image");
  add_combo (im1, "Bitmap export resolution (dpi):",
             "texmacs->image:raster-resolution",
             {{"1200", "1200"}, {"600", "600"}, {"300", "300"},
              {"150", "150"}, {"", ""}}, "300");
  std::vector<Choice> formats;
  if (file_converter_exists ("x.pdf", "x.svg")) formats.push_back ({"svg", "Svg"});
  if (file_converter_exists ("x.pdf", "x.eps")) formats.push_back ({"eps", "Eps"});
  if (file_converter_exists ("x.pdf", "x.png")) formats.push_back ({"png", "Png"});
  if (file_converter_exists ("x.pdf", "x.tif")) formats.push_back ({"tif", "Tiff"});
  if (file_converter_exists ("x.pdf", "x.jpg")) formats.push_back ({"jpg", "Jpeg"});
  if (file_converter_exists ("x.pdf", "x.pdf")) formats.push_back ({"pdf", "Pdf"});
  if (formats.empty ()) formats.push_back ({"png", "Png"});
  add_combo (im1, "Clipboard image format:", "texmacs->image:format", formats,
             "png");
  QFormLayout* im2= add_section (image, "Image -> TeXmacs");
  add_toggle (im2, "Auto remove image background:",
              "image auto remove background");
  add_toggle (im2, "Use Inkscape for conversion from SVG:",
              "image->texmacs:svg-prefer-inkscape");
  finish_page (image);

  return tabbed ({{"Html", html}, {"LaTeX", latex}, {"BibTeX", bibtex},
                  {"Verbatim", verbatim}, {"Pdf", pdf}, {"Image", image}});
}

QWidget*
QTMPreferencesDialog::buildVaultPage () {
  QWidget* vault= make_page ();
  QFormLayout* v= add_section (vault, "Vault");
  add_combo (v, "Popup fuzzy search limit:", "vault fuzzy search limit",
             {{"1", "1"}, {"2", "2"}, {"3", "3"}, {"5", "5"},
              {"10", "10"}}, "3");
  add_toggle (v, "Auto load last vault:", "vault auto load last");
  add_toggle (v, "Report if last vault is unavailable:",
              "vault report missing last");
  add_toggle (v, "Show vault welcome page on start:", "vault welcome page");
  add_toggle (v, "Show vault explorer on startup:",
              "vault explorer show on startup");
  add_toggle (v, "Take preferences with vault:",
              "vault take preferences with vault");
  add_toggle (v, "Track current file in vault explorer:",
              "vault explorer track current file");
  add_toggle (v, "Use system trash for safe deletion:",
              "vault explorer use system trash");
  add_toggle (v, "Namespace explorer shows file matches only for leaf namespaces:",
              "vault namespace explorer leaf matches only");
  add_toggle (v, "Simplify hierarchy graphs:",
              "vault simplify hierarchy graphs");
  add_combo (v, "Max allowed number of full backups:",
             "vault max full backups",
             {{"Unlimited", "Unlimited"}, {"1", "1"}, {"2", "2"}, {"3", "3"},
              {"5", "5"}, {"10", "10"}, {"20", "20"}, {"50", "50"}},
             "Unlimited");
  add_combo (v, "Preservation of pre-save histories for file:",
             "vault pre-save history preservation",
             {{"Unlimited", "Unlimited"}, {"1 hour", "1 hour"},
              {"6 hours", "6 hours"}, {"1 day", "1 day"},
              {"3 days", "3 days"}, {"1 week", "1 week"},
              {"1 month", "1 month"}}, "1 week");
  add_toggle (v, "Collect orphan assets during vault maintenance:",
              "vault collect orphan assets");
  add_toggle (v, "Consume %s aggressively in sub-product naming template suggestion:",
              "vault subproduct consume string aggressively");

  QComboBox* vaultFont= new QComboBox;
  QStringList vaultFonts;
  vaultFonts << "" << "roman" << "stix" << "bonum" << "pagella" << "schola"
             << "termes";
  vaultFonts << preferred_fonts ();
  vaultFonts << font_families ();
  vaultFonts.removeDuplicates ();
  QString current= pref ("vault preferred font", "");
  if (!vaultFonts.contains (current)) vaultFonts << current;
  vaultFont->addItems (vaultFonts);
  vaultFont->setCurrentText (current);
  QObject::connect (vaultFont,
                    static_cast<void (QComboBox::*) (const QString&)> (
                      &QComboBox::currentTextChanged),
                    [] (const QString& value) {
    set_pref ("vault preferred font", value);
  });
  v->addRow (label ("Global preferred font for vault:"), vaultFont);

  QFormLayout* a= add_section (vault, "Anchors and Images");
  add_toggle (a, "Auto anchor enunciations on manual save:",
              "vault auto anchor enunciations on save");
  add_toggle (a, "Auto copy images to vault:",
              "vault auto copy images to vault");
  add_toggle (a, "Normalize image filename when inserting:",
              "vault normalize image filename when inserting");
  finish_page (vault);
  return make_scroll_page (vault);
}

QWidget*
QTMPreferencesDialog::buildOtherPage () {
  QWidget* ai= make_page ();
  QFormLayout* a= add_section (ai, "AI");
  add_combo (a, "AI engine:", "ai",
             {{"off", "off"}, {"chatgpt", "chatgpt"},
              {"gemini", "gemini"}, {"llama", "llama"},
              {"open-mistral-7b", "open-mistral-7b"}}, "off");
  add_line_edit (a, "OpenAI API key:", "openai api key", "", true);
  add_line_edit (a, "Gemini API key:", "gemini api key", "", true);
  add_line_edit (a, "Mistral API key:", "mistral api key", "", true);
  finish_page (ai);

  QWidget* security= make_page ();
  QFormLayout* s= add_section (security, "Security");
  add_combo (s, "Script execution:", "security",
             {{"accept no scripts", "Accept no scripts"},
              {"prompt on scripts", "Prompt on scripts"},
              {"accept all scripts", "Accept all scripts"}});
  add_toggle (s, "Encryption:", "experimental encryption");
  QLabel* note= new QLabel (
    "Wallet and GnuPG maintenance remain available through their dedicated "
    "commands while the Preferences UI moves to native Qt.");
  note->setWordWrap (true);
  security->layout ()->addWidget (note);
  finish_page (security);

  return tabbed ({{"AI", ai}, {"Security", security}});
}

void
qtm_preferences_dialog_show () {
  if (headless_mode) return;
  if (activePreferencesDialog) {
    activePreferencesDialog->raise ();
    activePreferencesDialog->activateWindow ();
    return;
  }
  activePreferencesDialog= new QTMPreferencesDialog (QApplication::activeWindow ());
  activePreferencesDialog->show ();
}

bool
qtm_preferences_dialog_open () {
  return !activePreferencesDialog.isNull ();
}

void
qtm_page_setup_dialog_show () {
  if (headless_mode) return;
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
