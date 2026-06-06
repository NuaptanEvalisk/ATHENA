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
#include <QDialogButtonBox>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>

static QString
qtm_font_text (string s) {
  return to_qstring (s);
}

QTMFontSelector::QTMFontSelector (const QString& family, const QString& style,
                                  const QString& size, const QString& title,
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

  QSplitter* splitter= new QSplitter (Qt::Horizontal, this);
  splitter->addWidget (familyPane);
  splitter->addWidget (stylePane);
  splitter->addWidget (sizePane);
  splitter->setStretchFactor (0, 3);
  splitter->setStretchFactor (1, 2);
  splitter->setStretchFactor (2, 0);

  QDialogButtonBox* buttons=
    new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                          Qt::Horizontal, this);
  buttons->button (QDialogButtonBox::Ok)->setDefault (true);

  QVBoxLayout* mainLayout= new QVBoxLayout (this);
  mainLayout->setContentsMargins (12, 12, 12, 12);
  mainLayout->setSpacing (10);
  mainLayout->addWidget (splitter, 1);
  mainLayout->addWidget (preview);
  mainLayout->addWidget (buttons);

  loadFamilies ();
  populateFamilies (family);
  populateStyles (style);
  populateSizes (size);
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

array<string>
native_font_selector_dialog (string family, string style,
                             string size, string title) {
  array<string> result;
  QTMFontSelector dialog (to_qstring (family), to_qstring (style),
                          to_qstring (size), to_qstring (title),
                          QApplication::activeWindow ());
  if (dialog.exec () != QDialog::Accepted)
    return result;
  result << from_qstring (dialog.selectedFamily ());
  result << from_qstring (dialog.selectedStyle ());
  result << from_qstring (dialog.selectedSize ());
  return result;
}
