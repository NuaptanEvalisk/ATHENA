/******************************************************************************
* MODULE     : QTMPreferencesDialog.hpp
* DESCRIPTION: Native Qt preferences window for ATHENA
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMPREFERENCESDIALOG_HPP
#define QTMPREFERENCESDIALOG_HPP

#include <QDialog>
#include <QStringList>

#include <utility>
#include <vector>

class QListWidget;
class QCompleter;
class QLineEdit;
class QModelIndex;
class QStandardItemModel;
class QStackedWidget;

class QTMPreferencesDialog : public QDialog {
public:
  QTMPreferencesDialog (QWidget* parent= nullptr);
  QStringList exportMetadata () const;

private:
  void addCategory (const QString& name, QWidget* page);
  void rebuildSearchIndex ();
  void navigateToSearchResult (const QModelIndex& index);
  QWidget* buildGeneralPage ();
  QWidget* buildKeyboardPage ();
  QWidget* buildEditingPage ();
  QWidget* buildRenderingPage ();
  QWidget* buildConversionPage ();
  std::vector<std::pair<QString, QWidget*> > buildVaultCategories ();
  QWidget* buildOtherPage ();

  QListWidget*   categoryList;
  QStackedWidget* pageStack;
  QLineEdit* searchEdit;
  QCompleter* searchCompleter;
  QStandardItemModel* searchModel;
};

void qtm_preferences_dialog_show ();
bool qtm_preferences_dialog_open ();
int  qtm_preferences_export_privacy_dialog ();
QStringList qtm_preferences_export_metadata ();
void qtm_page_setup_dialog_show ();

#endif
