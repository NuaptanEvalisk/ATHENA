/******************************************************************************
* MODULE     : QTMFontSelector.hpp
* DESCRIPTION: Native Qt font selector for ATHENA
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMFONTSELECTOR_HPP
#define QTMFONTSELECTOR_HPP

#include "array.hpp"
#include "string.hpp"

#include <QDialog>
#include <QMap>
#include <QString>
#include <QStringList>

class QLabel;
class QLineEdit;
class QListWidget;
class QComboBox;

class QTMFontSelector : public QDialog {
public:
  QTMFontSelector (const QString& family, const QString& style,
                   const QString& size, const QString& fontProfile,
                   const QString& title, bool showSize= true,
                   QWidget* parent = nullptr);

  QString selectedFamily () const;
  QString selectedStyle () const;
  QString selectedSize () const;
  QString selectedFontProfile () const;

private:
  void loadFamilies ();
  void populateFamilies (const QString& preferred);
  void populateStyles (const QString& preferred);
  void populateSizes (const QString& preferred);
  void populateSubfonts (const QString& fontProfile);
  void updatePreview ();
  void updateCjkCoverage ();
  void updateSubfontPreview (const QString& key);
  void updateSubfontPreviews ();
  void selectListText (QListWidget* list, const QString& text);

  QStringList allFamilies;
  QLineEdit*  familyFilter;
  QListWidget* familyList;
  QListWidget* styleList;
  QListWidget* sizeList;
  QLabel* preview;
  QLabel* cjkCoverage;
  QMap<QString,QComboBox*> subfontSelectors;
  QMap<QString,QLabel*> subfontPreviews;
  QStringList unknownAssignments;
};

array<string> native_font_selector_dialog (string family, string style,
                                           string size, string font_profile,
                                           string title);

bool native_font_profile_selector_dialog (string current, string title,
                                          string& selected,
                                          QWidget* parent= nullptr);
QString qtm_font_profile_summary (const QString& profile);

#endif // QTMFONTSELECTOR_HPP
