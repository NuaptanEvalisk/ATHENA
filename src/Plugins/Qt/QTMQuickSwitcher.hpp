/******************************************************************************
* MODULE     : QTMQuickSwitcher.hpp
* DESCRIPTION: Qt quick switcher for ATHENA vault files
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMQUICKSWITCHER_HPP
#define QTMQUICKSWITCHER_HPP

#include "tree.hpp"
#include "string.hpp"
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QString>
#include <QVBoxLayout>
#include <vector>

class QEvent;
class QKeyEvent;
class QShowEvent;

class QTMQuickSwitcher : public QDialog {
public:
  QTMQuickSwitcher (QWidget* parent, array<string> recentFiles);

  tree getResult ();

protected:
  bool eventFilter (QObject* watched, QEvent* event) override;
  void showEvent (QShowEvent* event) override;

private:
  struct Entry {
    QString relPath;
    QString baseName;
    QString searchPath;
    QString searchBase;
    int     mtime;
  };

  void loadFiles (array<string> recentFiles);
  void updateList ();
  void acceptOpen ();
  void acceptCreate ();
  void completeFromSelection ();
  int  fuzzyScore (const Entry& e, const QString& query) const;
  int  fuzzySubsequenceScore (const QString& text, const QString& query) const;

  QVBoxLayout* layout;
  QLabel*      prompt;
  QLineEdit*   searchEdit;
  QListWidget* resultList;

  std::vector<Entry> entries;
  std::vector<int>   recentIndices;

  QString action;
  QString result;
  bool    resultAccepted;
};

tree vault_quick_switcher (array<string> recentFiles);

#endif // QTMQUICKSWITCHER_HPP
