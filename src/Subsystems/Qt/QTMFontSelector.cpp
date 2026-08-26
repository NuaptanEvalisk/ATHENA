/******************************************************************************
* MODULE     : QTMFontSelector.cpp
* DESCRIPTION: Native Qt font selector for ATHENA
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMFontSelector.hpp"

#include "font.hpp"
#include "qt_utilities.hpp"

#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

static QString
qtm_font_text (string s) {
  return to_qstring (s);
}

namespace {

struct SubfontRole {
  const char* key;
  const char* label;
  const char* tooltip;
};

const SubfontRole textSubfonts[]= {
  {"bold", "Bold", "Font used for bold text"},
  {"italic", "Italic", "Font used for italic text"},
  {"smallcaps", "Small caps", "Font used for small-capital text"},
  {"sansserif", "Sans serif", "Font used for sans-serif text"},
  {"typewriter", "Typewriter", "Font used for verbatim and monospaced text"}
};

const SubfontRole mathSubfonts[]= {
  {"math", "Mathematics", "Primary mathematics font"},
  {"greek", "Greek", "Font used for Greek mathematical letters"},
  {"bbb", "Blackboard bold", "Font used for blackboard-bold symbols"},
  {"cal", "Calligraphic", "Font used for calligraphic symbols"},
  {"frak", "Fraktur", "Font used for Fraktur symbols"}
};

QString
trimmedMainFamily (const QString& profile) {
  const QStringList parts= profile.split (',', Qt::SkipEmptyParts);
  for (const QString& raw: parts) {
    const QString part= raw.trimmed ();
    if (!part.contains ('=')) return part;
  }
  return profile.trimmed ();
}

QString
logicalMainFamily (const QString& family, const QString& style) {
  array<string> logical=
    logical_font_exact (from_qstring (family), from_qstring (style));
  return qtm_font_text (get_family (logical));
}

QComboBox*
makeSubfontSelector (QWidget* parent, const QStringList& families,
                     const SubfontRole& role) {
  QComboBox* combo= new QComboBox (parent);
  combo->setEditable (true);
  combo->setInsertPolicy (QComboBox::NoInsert);
  combo->addItem ("Automatic");
  combo->addItems (families);
  combo->setToolTip (role.tooltip);
  combo->setSizeAdjustPolicy (QComboBox::AdjustToMinimumContentsLengthWithIcon);
  combo->setMinimumContentsLength (18);
  QCompleter* completer= new QCompleter (families, combo);
  completer->setCaseSensitivity (Qt::CaseInsensitive);
  completer->setFilterMode (Qt::MatchContains);
  combo->setCompleter (completer);
  return combo;
}

void
addSubfontGroup (QGridLayout* layout, int column,
                 const QString& title, const SubfontRole* roles, int count,
                 QWidget* parent, const QStringList& families,
                 QMap<QString,QComboBox*>& selectors) {
  QGroupBox* group= new QGroupBox (title, parent);
  QGridLayout* grid= new QGridLayout (group);
  for (int i=0; i<count; ++i) {
    const SubfontRole& role= roles[i];
    QLabel* label= new QLabel (role.label, group);
    QComboBox* combo= makeSubfontSelector (group, families, role);
    label->setBuddy (combo);
    grid->addWidget (label, i, 0);
    grid->addWidget (combo, i, 1);
    selectors.insert (role.key, combo);
  }
  grid->setColumnStretch (1, 1);
  layout->addWidget (group, 0, column);
}

} // namespace

QTMFontSelector::QTMFontSelector (const QString& family, const QString& style,
                                  const QString& size,
                                  const QString& fontProfile,
                                  const QString& title, bool showSize,
                                  QWidget* parent)
  : QDialog (parent),
    familyFilter (new QLineEdit (this)),
    familyList (new QListWidget (this)),
    styleList (new QListWidget (this)),
    sizeList (new QListWidget (this)),
    preview (new QLabel (this))
{
  setWindowTitle (title.isEmpty () ? QString ("Font selector") : title);
  resize (860, 560);
  setMinimumSize (720, 460);

  familyFilter->setPlaceholderText ("Filter fonts");
  familyList->setUniformItemSizes (true);
  styleList->setUniformItemSizes (true);
  sizeList->setUniformItemSizes (true);
  sizeList->setMaximumWidth (120);
  loadFamilies ();

  preview->setText (
    "The quick brown fox jumps over the lazy dog.\n"
    "0123456789  ABC abc  \xce\xb1\xce\xb2\xce\xb3  \xe4\xb8\xad\xe6\x96\x87");
  preview->setAlignment (Qt::AlignCenter);
  preview->setWordWrap (true);
  preview->setMinimumHeight (120);
  preview->setFrameShape (QFrame::StyledPanel);

  QWidget* familyPane= new QWidget (this);
  QVBoxLayout* familyLayout= new QVBoxLayout (familyPane);
  familyLayout->setContentsMargins (0, 0, 0, 0);
  familyLayout->addWidget (new QLabel ("Family", familyPane));
  familyLayout->addWidget (familyFilter);
  familyLayout->addWidget (familyList);

  QWidget* stylePane= new QWidget (this);
  QVBoxLayout* styleLayout= new QVBoxLayout (stylePane);
  styleLayout->setContentsMargins (0, 0, 0, 0);
  styleLayout->addWidget (new QLabel ("Style", stylePane));
  styleLayout->addWidget (styleList);

  QWidget* sizePane= new QWidget (this);
  QVBoxLayout* sizeLayout= new QVBoxLayout (sizePane);
  sizeLayout->setContentsMargins (0, 0, 0, 0);
  sizeLayout->addWidget (new QLabel ("Size", sizePane));
  sizeLayout->addWidget (sizeList);

  QWidget* mainPage= new QWidget (this);
  QSplitter* splitter= new QSplitter (Qt::Horizontal, mainPage);
  splitter->addWidget (familyPane);
  splitter->addWidget (stylePane);
  if (showSize) splitter->addWidget (sizePane);
  else sizePane->hide ();
  splitter->setStretchFactor (0, 3);
  splitter->setStretchFactor (1, 2);
  splitter->setStretchFactor (2, 0);

  QVBoxLayout* mainPageLayout= new QVBoxLayout (mainPage);
  mainPageLayout->setContentsMargins (0, 0, 0, 0);
  mainPageLayout->setSpacing (10);
  mainPageLayout->addWidget (splitter, 1);
  mainPageLayout->addWidget (preview);

  QWidget* subfontPage= new QWidget (this);
  QGridLayout* subfontLayout= new QGridLayout (subfontPage);
  addSubfontGroup (subfontLayout, 0, "Text subfonts", textSubfonts,
                   sizeof (textSubfonts) / sizeof (textSubfonts[0]),
                   subfontPage, allFamilies, subfontSelectors);
  addSubfontGroup (subfontLayout, 1, "Math subfonts", mathSubfonts,
                   sizeof (mathSubfonts) / sizeof (mathSubfonts[0]),
                   subfontPage, allFamilies, subfontSelectors);
  subfontLayout->setColumnStretch (0, 1);
  subfontLayout->setColumnStretch (1, 1);
  subfontLayout->setRowStretch (1, 1);

  QTabWidget* tabs= new QTabWidget (this);
  tabs->addTab (mainPage, "Main font");
  tabs->addTab (subfontPage, "Subfonts");

  QDialogButtonBox* buttons=
    new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                          Qt::Horizontal, this);
  buttons->button (QDialogButtonBox::Ok)->setDefault (true);

  QVBoxLayout* mainLayout= new QVBoxLayout (this);
  mainLayout->setContentsMargins (12, 12, 12, 12);
  mainLayout->setSpacing (10);
  mainLayout->addWidget (tabs, 1);
  mainLayout->addWidget (buttons);

  populateFamilies (family);
  populateStyles (style);
  populateSizes (size);
  populateSubfonts (fontProfile);
  updatePreview ();

  connect (familyFilter, &QLineEdit::textChanged, this,
           [this] () {
             populateFamilies (selectedFamily ());
             populateStyles (QString ());
             updatePreview ();
           });
  connect (familyList, &QListWidget::currentTextChanged, this,
           [this] (const QString&) {
             populateStyles (QString ());
             updatePreview ();
           });
  connect (styleList, &QListWidget::currentTextChanged, this,
           [this] (const QString&) { updatePreview (); });
  connect (sizeList, &QListWidget::currentTextChanged, this,
           [this] (const QString&) { updatePreview (); });
  connect (styleList, &QListWidget::itemDoubleClicked, this,
           [this] () { accept (); });
  connect (familyList, &QListWidget::itemDoubleClicked, this,
           [this] () { accept (); });
  connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void
QTMFontSelector::populateSubfonts (const QString& fontProfile) {
  unknownAssignments.clear ();
  const QStringList parts= fontProfile.split (',', Qt::SkipEmptyParts);
  for (const QString& raw: parts) {
    const QString part= raw.trimmed ();
    const int equal= part.indexOf ('=');
    if (equal <= 0) continue;
    const QString key= part.left (equal).trimmed ();
    const QString value= part.mid (equal + 1).trimmed ();
    auto it= subfontSelectors.find (key);
    if (it == subfontSelectors.end ()) {
      unknownAssignments << part;
      continue;
    }
    QComboBox* combo= it.value ();
    if (combo->findText (value, Qt::MatchFixedString) < 0)
      combo->addItem (value);
    combo->setCurrentText (value.isEmpty () ? QString ("Automatic") : value);
  }
}

void
QTMFontSelector::loadFamilies () {
  allFamilies.clear ();
  array<string> families= font_database_families ();
  for (int i=0; i<N(families); i++)
    allFamilies << qtm_font_text (families[i]);
}

void
QTMFontSelector::populateFamilies (const QString& preferred) {
  QString keep= preferred.isEmpty () ? selectedFamily () : preferred;
  QString filter= familyFilter->text ().trimmed ();
  QSignalBlocker blocker (familyList);
  familyList->clear ();
  for (const QString& family: allFamilies)
    if (filter.isEmpty () || family.contains (filter, Qt::CaseInsensitive))
      familyList->addItem (family);
  selectListText (familyList, keep);
  if (familyList->currentRow () < 0 && familyList->count () > 0)
    familyList->setCurrentRow (0);
}

void
QTMFontSelector::populateStyles (const QString& preferred) {
  QString family= selectedFamily ();
  QString keep= preferred.isEmpty () ? selectedStyle () : preferred;
  QSignalBlocker blocker (styleList);
  styleList->clear ();
  if (!family.isEmpty ()) {
    array<string> styles= font_database_styles (from_qstring (family));
    for (int i=0; i<N(styles); i++)
      styleList->addItem (qtm_font_text (styles[i]));
  }
  if (styleList->count () == 0)
    styleList->addItem ("Regular");
  selectListText (styleList, keep);
  if (styleList->currentRow () < 0)
    selectListText (styleList, "Regular");
  if (styleList->currentRow () < 0 && styleList->count () > 0)
    styleList->setCurrentRow (0);
}

void
QTMFontSelector::populateSizes (const QString& preferred) {
  static const char* defaults[]= {
    "6", "7", "8", "9", "10", "11", "12", "14", "16", "18",
    "20", "24", "28", "32", "36", "48", "60", "72"
  };
  QSignalBlocker blocker (sizeList);
  sizeList->clear ();
  for (const char* value: defaults)
    sizeList->addItem (value);
  if (!preferred.isEmpty () &&
      sizeList->findItems (preferred, Qt::MatchExactly).isEmpty ())
    sizeList->addItem (preferred);
  selectListText (sizeList, preferred.isEmpty () ? QString ("10") : preferred);
}

void
QTMFontSelector::updatePreview () {
  QString family= selectedFamily ();
  QString style = selectedStyle ();
  QString size  = selectedSize ();
  bool ok= false;
  double pointSize= size.toDouble (&ok);
  if (!ok || pointSize <= 0.0) pointSize= 10.0;

  QFontDatabase db;
  QFont font= db.font (family, style, static_cast<int> (pointSize));
  font.setFamily (family);
  font.setPointSizeF (pointSize);
  preview->setFont (font);
}

void
QTMFontSelector::selectListText (QListWidget* list, const QString& text) {
  if (text.isEmpty ()) return;
  QList<QListWidgetItem*> exact= list->findItems (text, Qt::MatchExactly);
  if (!exact.isEmpty ()) {
    list->setCurrentItem (exact.first ());
    return;
  }
  for (int i=0; i<list->count (); i++)
    if (list->item (i)->text ().compare (text, Qt::CaseInsensitive) == 0) {
      list->setCurrentRow (i);
      return;
    }
}

QString
QTMFontSelector::selectedFamily () const {
  QListWidgetItem* item= familyList->currentItem ();
  return item == nullptr ? QString () : item->text ();
}

QString
QTMFontSelector::selectedStyle () const {
  QListWidgetItem* item= styleList->currentItem ();
  return item == nullptr ? QString () : item->text ();
}

QString
QTMFontSelector::selectedSize () const {
  QListWidgetItem* item= sizeList->currentItem ();
  return item == nullptr ? QString ("10") : item->text ();
}

QString
QTMFontSelector::selectedFontProfile () const {
  QStringList parts= unknownAssignments;
  for (auto it= subfontSelectors.constBegin ();
       it != subfontSelectors.constEnd (); ++it) {
    const QString value= it.value ()->currentText ().trimmed ();
    if (!value.isEmpty () && value != "Automatic")
      parts << it.key () + "=" + value;
  }
  parts << logicalMainFamily (selectedFamily (), selectedStyle ());
  return parts.join (',');
}

array<string>
native_font_selector_dialog (string family, string style,
                             string size, string font_profile, string title) {
  array<string> result;
  QTMFontSelector dialog (to_qstring (family), to_qstring (style),
                          to_qstring (size), to_qstring (font_profile),
                          to_qstring (title), true,
                          QApplication::activeWindow ());
  if (dialog.exec () != QDialog::Accepted)
    return result;
  result << from_qstring (dialog.selectedFamily ());
  result << from_qstring (dialog.selectedStyle ());
  result << from_qstring (dialog.selectedSize ());
  result << from_qstring (dialog.selectedFontProfile ());
  return result;
}

bool
native_font_profile_selector_dialog (string current, string title,
                                     string& selected, QWidget* parent) {
  string main= main_family (current == "" ? string ("roman") : current);
  array<string> logical= logical_font (main, "rm", "medium", "right");
  array<string> physical= search_font_exact (logical);
  QString family= N(physical) > 0 ? qtm_font_text (physical[0])
                                  : qtm_font_text (main);
  QString style= N(physical) > 1 ? qtm_font_text (physical[1]) : "Regular";
  QTMFontSelector dialog (family, style, "10", qtm_font_text (current),
                          qtm_font_text (title), false,
                          parent == nullptr ? QApplication::activeWindow ()
                                            : parent);
  if (dialog.exec () != QDialog::Accepted) return false;
  selected= from_qstring (dialog.selectedFontProfile ());
  return true;
}

QString
qtm_font_profile_summary (const QString& profile) {
  if (profile.trimmed ().isEmpty ()) return "Use document default";
  const QString main= trimmedMainFamily (profile);
  int overrides= 0;
  for (const QString& part: profile.split (',', Qt::SkipEmptyParts))
    if (part.contains ('=')) ++overrides;
  if (overrides == 0) return main;
  return QString ("%1 + %2 subfont%3")
    .arg (main).arg (overrides).arg (overrides == 1 ? "" : "s");
}
