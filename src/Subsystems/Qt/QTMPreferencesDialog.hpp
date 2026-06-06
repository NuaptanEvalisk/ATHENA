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

class QListWidget;
class QStackedWidget;

class QTMPreferencesDialog : public QDialog {
public:
  QTMPreferencesDialog (QWidget* parent= nullptr);

private:
  void addCategory (const QString& name, QWidget* page);
  QWidget* buildGeneralPage ();
  QWidget* buildKeyboardPage ();
  QWidget* buildEditingPage ();
  QWidget* buildRenderingPage ();
  QWidget* buildConversionPage ();
  QWidget* buildVaultPage ();
  QWidget* buildOtherPage ();

  QListWidget*   categoryList;
  QStackedWidget* pageStack;
};

void qtm_preferences_dialog_show ();
bool qtm_preferences_dialog_open ();
void qtm_page_setup_dialog_show ();

#endif
