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
#include "namespaces.hpp"
#include "fuzzy_rank.hpp"
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QString>
#include <QStringList>
#include <QTabWidget>
#include <QVBoxLayout>
#include <vector>

class QEvent;
class QKeyEvent;
class QShowEvent;

class QTMQuickSwitcher : public QDialog {
public:
  QTMQuickSwitcher (QWidget* parent, const array<string>& recentFiles);

  tree getResult ();

protected:
  bool eventFilter (QObject* watched, QEvent* event) override;
  void showEvent (QShowEvent* event) override;

private:
  struct Entry {
    QString relPath;
    QString baseName;
    string  searchPath;
    string  searchBase;
    int     mtime;
  };

  void loadFiles (const array<string>& recentFiles);
  void loadNamespaces ();
  void updateList ();
  void updateRawList ();
  void updateRecentList ();
  void updateStructuredList ();
  void acceptOpen ();
  void acceptCreate ();
  void acceptStructuredOpen ();
  void openStructuredNamespaceInfo ();
  void descendStructuredNamespace (QListWidgetItem* item);
  void completeFromSelection ();
  void switchTab ();
  void moveSelection (int delta);
  void moveSelectionPage (int direction);
  QListWidget* activeList () const;
  int  fuzzyScore (const Entry& e, string query) const;
  int  fuzzyScore (string text, string query) const;
  QString structuredCurrentNamespace () const;
  QStringList structuredParentsOf (const QString& name) const;
  QString structuredNamespaceUrl (const QStringList& path) const;

  QVBoxLayout* layout;
  QLabel*      prompt;
  QLineEdit*   searchEdit;
  QTabWidget*  tabs;
  QListWidget* rawList;
  QListWidget* structuredList;
  QListWidget* recentList;

  std::vector<Entry> entries;
  std::vector<int>   recentIndices;
  std::vector<int>   rawDefaultIndices;
  namespace_records<athena_namespace_definition> namespaces;
  QStringList structuredPath;
  bool        structuredParentChoice;
  QString     structuredParentChoiceFor;

  QString action;
  QString result;
  bool    resultAccepted;
};

void vault_quick_switcher (array<string>& recentFiles);

#endif // QTMQUICKSWITCHER_HPP
