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
#include <QString>
#include <QStringList>

class QLabel;
class QLineEdit;
class QListWidget;

class QTMFontSelector : public QDialog {
public:
  QTMFontSelector (const QString& family, const QString& style,
                   const QString& size, const QString& title,
                   QWidget* parent = nullptr);

  QString selectedFamily () const;
  QString selectedStyle () const;
  QString selectedSize () const;

private:
  void loadFamilies ();
  void populateFamilies (const QString& preferred);
  void populateStyles (const QString& preferred);
  void populateSizes (const QString& preferred);
  void updatePreview ();
  void selectListText (QListWidget* list, const QString& text);

  QStringList allFamilies;
  QLineEdit*  familyFilter;
  QListWidget* familyList;
  QListWidget* styleList;
  QListWidget* sizeList;
  QLabel* preview;
};

array<string> native_font_selector_dialog (string family, string style,
                                           string size, string title);

#endif // QTMFONTSELECTOR_HPP
