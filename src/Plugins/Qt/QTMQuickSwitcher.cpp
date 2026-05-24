/******************************************************************************
* MODULE     : QTMQuickSwitcher.cpp
* DESCRIPTION: Qt quick switcher for ATHENA vault files
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMQuickSwitcher.hpp"
#include "vault.hpp"
#include "qt_utilities.hpp"
#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QShowEvent>
#include <QVariant>
#include <algorithm>
#include <utility>

static const int quick_switcher_limit= 100;

namespace {
enum QuickItemType {
  QuickFile= 0,
  QuickNamespace= 1,
  QuickUp= 2
};

enum QuickRoles {
  QuickTypeRole= Qt::UserRole,
  QuickPayloadRole,
  QuickCompletionRole,
  QuickNamespacePathRole
};
}

QTMQuickSwitcher::QTMQuickSwitcher (QWidget* parent, array<string> recentFiles)
  : QDialog (parent),
    structuredParentChoice (false),
    resultAccepted (false) {
  setWindowTitle ("Quick switcher");
  setWindowFlags (windowFlags () | Qt::Tool);
  resize (1775, 550);

  layout = new QVBoxLayout (this);
  prompt = new QLabel ("Type to switch ATHENA vault files", this);
  layout->addWidget (prompt);

  searchEdit = new QLineEdit (this);
  searchEdit->setPlaceholderText ("Search .ath files");
  layout->addWidget (searchEdit);

  tabs= new QTabWidget (this);
  rawList= new QListWidget (this);
  structuredList= new QListWidget (this);
  tabs->addTab (rawList, "Raw");
  tabs->addTab (structuredList, "Structured");
  layout->addWidget (tabs);

  searchEdit->installEventFilter (this);
  rawList->installEventFilter (this);
  structuredList->installEventFilter (this);

  loadFiles (recentFiles);
  loadNamespaces ();
  updateList ();

  connect (searchEdit, &QLineEdit::textChanged,
           this, [this] (const QString&) { updateList (); });
  connect (tabs, &QTabWidget::currentChanged,
           this, [this] (int) { updateList (); });
  connect (rawList, &QListWidget::itemDoubleClicked,
           this, [this] (QListWidgetItem*) { acceptOpen (); });
  connect (structuredList, &QListWidget::itemDoubleClicked,
           this, [this] (QListWidgetItem*) { acceptStructuredOpen (); });

  searchEdit->setFocus ();
}

void
QTMQuickSwitcher::showEvent (QShowEvent* event) {
  QDialog::showEvent (event);
  QWidget* w= parentWidget ();
  if (w == nullptr) w= QApplication::activeWindow ();
  if (w != nullptr) {
    QRect r= geometry ();
    r.moveCenter (w->geometry ().center ());
    move (r.topLeft ());
  }
}

void
QTMQuickSwitcher::loadFiles (array<string> recentFiles) {
  url root= vault_get_root ();
  array<url> files= vault_get_all_files ();

  for (int i=0; i<N(files); i++) {
    url rel= delta (root * url (""), files[i]);
    if (suffix (rel) != "ath") continue;
    QString relPath= to_qstring (as_unix_string (rel));
    QString base= relPath.section ('/', -1);
    if (base.endsWith (".ath")) base.chop (4);

    Entry e;
    e.relPath= relPath;
    e.baseName= base;
    e.searchPath= relPath;
    if (e.searchPath.endsWith (".ath")) e.searchPath.chop (4);
    e.searchPath= e.searchPath.toLower ();
    e.searchBase= base.toLower ();
    e.mtime= vault_get_mtime (files[i]);
    entries.push_back (e);
  }

  for (int i=0; i<N(recentFiles); i++) {
    QString recent= to_qstring (recentFiles[i]);
    for (int j=0; j<(int) entries.size (); j++) {
      if (entries[j].relPath == recent &&
          std::find (recentIndices.begin (), recentIndices.end (), j) ==
            recentIndices.end ()) {
        recentIndices.push_back (j);
        break;
      }
    }
  }

  std::vector<std::pair<int,int> > byMtime;
  for (int i=0; i<(int) entries.size (); i++) {
    if (std::find (recentIndices.begin (), recentIndices.end (), i) ==
        recentIndices.end ())
      byMtime.push_back (std::make_pair (-entries[i].mtime, i));
  }
  std::sort (byMtime.begin (), byMtime.end ());
  for (auto p : byMtime)
    recentIndices.push_back (p.second);
}

void
QTMQuickSwitcher::loadNamespaces () {
  string error;
  athena_namespace_refresh_derived (error);
  namespaces= athena_namespaces_list ();
}

int
QTMQuickSwitcher::fuzzySubsequenceScore (const QString& text,
                                         const QString& query) const {
  int qi= 0;
  int spread= 0;
  int first= -1;
  for (int i=0; i<text.length () && qi<query.length (); i++) {
    if (text[i] == query[qi]) {
      if (first < 0) first= i;
      spread= i - first;
      qi++;
    }
  }
  if (qi != query.length ()) return -1;
  return 50000 - (10 * spread) - text.length ();
}

int
QTMQuickSwitcher::fuzzyScore (const Entry& e, const QString& query) const {
  if (query.isEmpty ()) return 0;
  if (e.searchBase == query) return 100000;
  if (e.searchBase.startsWith (query)) return 90000 - e.searchBase.length ();
  if (e.searchBase.contains (query)) return 80000 - e.searchBase.length ();

  int baseFuzzy= fuzzySubsequenceScore (e.searchBase, query);
  if (baseFuzzy >= 0) return baseFuzzy;

  if (e.searchPath.startsWith (query)) return 40000 - e.searchPath.length ();
  if (e.searchPath.contains (query)) return 30000 - e.searchPath.length ();

  int pathFuzzy= fuzzySubsequenceScore (e.searchPath, query);
  if (pathFuzzy >= 0) return pathFuzzy - 30000;
  return -1;
}

int
QTMQuickSwitcher::fuzzyScore (const QString& text, const QString& query) const {
  QString normalized= text.toLower ();
  if (query.isEmpty ()) return 0;
  if (normalized == query) return 100000;
  if (normalized.startsWith (query)) return 90000 - normalized.length ();
  if (normalized.contains (query)) return 80000 - normalized.length ();
  return fuzzySubsequenceScore (normalized, query);
}

void
QTMQuickSwitcher::updateList () {
  if (tabs->currentIndex () == 0) updateRawList ();
  else updateStructuredList ();
}

void
QTMQuickSwitcher::updateRawList () {
  rawList->clear ();
  QString query= searchEdit->text ().trimmed ().toLower ();

  if (query.isEmpty ()) {
    prompt->setText ("Recent ATHENA vault files");
    int n= 0;
    for (int index : recentIndices) {
      QListWidgetItem* item= new QListWidgetItem (entries[index].relPath);
      item->setData (QuickTypeRole, QuickFile);
      item->setData (QuickPayloadRole, entries[index].relPath);
      item->setData (QuickCompletionRole, entries[index].relPath);
      rawList->addItem (item);
      if (++n >= quick_switcher_limit) break;
    }
  }
  else {
    prompt->setText ("Matching ATHENA vault files");
    std::vector<std::pair<int,int> > matches;
    for (int i=0; i<(int) entries.size (); i++) {
      int score= fuzzyScore (entries[i], query);
      if (score >= 0) matches.push_back (std::make_pair (-score, i));
    }
    std::sort (matches.begin (), matches.end ());
    int n= 0;
    for (auto m : matches) {
      const Entry& e= entries[m.second];
      QListWidgetItem* item= new QListWidgetItem (e.relPath);
      item->setData (QuickTypeRole, QuickFile);
      item->setData (QuickPayloadRole, e.relPath);
      item->setData (QuickCompletionRole, e.relPath);
      rawList->addItem (item);
      if (++n >= quick_switcher_limit) break;
    }
  }

  if (rawList->count () > 0) rawList->setCurrentRow (0);
}

QString
QTMQuickSwitcher::structuredCurrentNamespace () const {
  return structuredPath.isEmpty () ? QString () : structuredPath.last ();
}

QStringList
QTMQuickSwitcher::structuredParentsOf (const QString& name) const {
  QStringList out;
  for (const athena_namespace_definition& ns: namespaces) {
    if (to_qstring (ns.name) != name) continue;
    for (int i=0; i<N(ns.parents); i++) {
      QString parent= to_qstring (ns.parents[i]);
      if (!out.contains (parent)) out << parent;
    }
    for (int i=0; i<N(ns.derived_parents); i++) {
      QString parent= to_qstring (ns.derived_parents[i]);
      if (!out.contains (parent)) out << parent;
    }
    break;
  }
  out.sort ();
  return out;
}

QString
QTMQuickSwitcher::structuredNamespaceUrl (const QStringList& path) const {
  return QString ("tmfs://ns/") + path.join ("/");
}

void
QTMQuickSwitcher::updateStructuredList () {
  structuredList->clear ();
  QString query= searchEdit->text ().trimmed ().toLower ();
  QString current= structuredCurrentNamespace ();

  QStringList namespaceNames;
  QStringList namespacePaths;
  if (structuredParentChoice) {
    prompt->setText (QString ("Parents of namespace %1")
                     .arg (structuredParentChoiceFor));
    namespaceNames= structuredParentsOf (structuredParentChoiceFor);
    for (const QString& name: namespaceNames)
      namespacePaths << name;
  }
  else if (current.isEmpty ()) {
    prompt->setText ("Structured namespaces");
    for (const athena_namespace_definition& ns: namespaces) {
      QString name= to_qstring (ns.name);
      namespaceNames << name;
      namespacePaths << name;
    }
  }
  else {
    prompt->setText (QString ("Namespace %1").arg (structuredPath.join (" / ")));
    for (const athena_namespace_definition& ns: namespaces) {
      bool child= false;
      for (int i=0; i<N(ns.parents); i++)
        if (to_qstring (ns.parents[i]) == current) child= true;
      for (int i=0; i<N(ns.derived_parents); i++)
        if (to_qstring (ns.derived_parents[i]) == current) child= true;
      if (!child) continue;
      QString name= to_qstring (ns.name);
      namespaceNames << name;
      QStringList path= structuredPath;
      path << name;
      namespacePaths << path.join ("/");
    }
  }

  std::vector<std::pair<int,int> > nsMatches;
  for (int i=0; i<namespaceNames.size (); i++) {
    int score= fuzzyScore (namespaceNames[i], query);
    if (score >= 0) nsMatches.push_back (std::make_pair (-score, i));
  }
  std::sort (nsMatches.begin (), nsMatches.end (),
             [&] (const std::pair<int,int>& a,
                  const std::pair<int,int>& b) {
               if (a.first != b.first) return a.first < b.first;
               return namespaceNames[a.second] < namespaceNames[b.second];
             });

  int n= 0;
  for (auto m: nsMatches) {
    QString name= namespaceNames[m.second];
    QStringList path= namespacePaths[m.second].split ("/", Qt::SkipEmptyParts);
    QListWidgetItem* item= new QListWidgetItem (name + "/");
    item->setData (QuickTypeRole, QuickNamespace);
    item->setData (QuickPayloadRole, name);
    item->setData (QuickCompletionRole, name);
    item->setData (QuickNamespacePathRole, path);
    structuredList->addItem (item);
    if (++n >= quick_switcher_limit) break;
  }

  if (!structuredParentChoice && !current.isEmpty () && n < quick_switcher_limit) {
    string error;
    std::vector<athena_namespace_match> members=
      athena_namespace_members (from_qstring (current), error);
    url root= vault_get_root ();
    if (query.isEmpty ()) {
      for (const athena_namespace_match& m: members) {
        url rel= delta (root * url (""), m.file);
        QString relPath= to_qstring (as_unix_string (rel));
        QListWidgetItem* item= new QListWidgetItem (relPath);
        item->setData (QuickTypeRole, QuickFile);
        item->setData (QuickPayloadRole, relPath);
        item->setData (QuickCompletionRole, relPath);
        structuredList->addItem (item);
        if (++n >= quick_switcher_limit) break;
      }
    }
    else {
      std::vector<std::pair<int,int> > matches;
      for (int i=0; i<(int) members.size (); i++) {
        const athena_namespace_match& m= members[i];
        url rel= delta (root * url (""), m.file);
        QString relPath= to_qstring (as_unix_string (rel));
        QString stem= to_qstring (m.stem);
        int score= std::max (fuzzyScore (stem, query),
                             fuzzyScore (relPath, query));
        if (score >= 0) matches.push_back (std::make_pair (-score, i));
      }
      std::sort (matches.begin (), matches.end (),
                 [&] (const std::pair<int,int>& a,
                      const std::pair<int,int>& b) {
                   if (a.first != b.first) return a.first < b.first;
                   const athena_namespace_match& ma= members[a.second];
                   const athena_namespace_match& mb= members[b.second];
                   QString sa= to_qstring (ma.stem);
                   QString sb= to_qstring (mb.stem);
                   if (sa != sb) return sa < sb;
                   return a.second < b.second;
                 });
      for (auto match: matches) {
        const athena_namespace_match& m= members[match.second];
        url rel= delta (root * url (""), m.file);
        QString relPath= to_qstring (as_unix_string (rel));
        QListWidgetItem* item= new QListWidgetItem (relPath);
        item->setData (QuickTypeRole, QuickFile);
        item->setData (QuickPayloadRole, relPath);
        item->setData (QuickCompletionRole, relPath);
        structuredList->addItem (item);
        if (++n >= quick_switcher_limit) break;
      }
    }
  }

  QListWidgetItem* up= new QListWidgetItem ("..");
  up->setData (QuickTypeRole, QuickUp);
  up->setData (QuickPayloadRole, QString (".."));
  up->setData (QuickCompletionRole, QString ());
  structuredList->addItem (up);

  if (structuredList->count () > 0) structuredList->setCurrentRow (0);
}

void
QTMQuickSwitcher::acceptOpen () {
  if (tabs->currentIndex () != 0) {
    acceptStructuredOpen ();
    return;
  }

  QListWidgetItem* item= rawList->currentItem ();
  if (item == nullptr && rawList->count () > 0) item= rawList->item (0);
  if (item == nullptr) return;

  action= "open";
  result= item->data (QuickPayloadRole).toString ();
  resultAccepted= true;
  accept ();
}

void
QTMQuickSwitcher::acceptCreate () {
  if (tabs->currentIndex () == 1) {
    QListWidgetItem* item= structuredList->currentItem ();
    if (item == nullptr && structuredList->count () > 0)
      item= structuredList->item (0);
    if (item != nullptr &&
        item->data (QuickTypeRole).toInt () == QuickNamespace) {
      openStructuredNamespaceInfo ();
      return;
    }
  }

  QString query= searchEdit->text ().trimmed ();
  if (query.isEmpty ()) return;

  action= "create";
  result= query;
  resultAccepted= true;
  accept ();
}

void
QTMQuickSwitcher::acceptStructuredOpen () {
  QListWidgetItem* item= structuredList->currentItem ();
  if (item == nullptr && structuredList->count () > 0)
    item= structuredList->item (0);
  if (item == nullptr) return;

  int type= item->data (QuickTypeRole).toInt ();
  if (type == QuickFile) {
    action= "open";
    result= item->data (QuickPayloadRole).toString ();
    resultAccepted= true;
    accept ();
    return;
  }
  descendStructuredNamespace (item);
}

void
QTMQuickSwitcher::openStructuredNamespaceInfo () {
  QListWidgetItem* item= structuredList->currentItem ();
  if (item == nullptr && structuredList->count () > 0)
    item= structuredList->item (0);
  if (item == nullptr ||
      item->data (QuickTypeRole).toInt () != QuickNamespace)
    return;

  QStringList path= item->data (QuickNamespacePathRole).toStringList ();
  if (path.isEmpty ()) return;
  action= "open-url";
  result= structuredNamespaceUrl (path);
  resultAccepted= true;
  accept ();
}

void
QTMQuickSwitcher::descendStructuredNamespace (QListWidgetItem* item) {
  int type= item->data (QuickTypeRole).toInt ();
  if (type == QuickUp) {
    if (structuredPath.isEmpty ()) {
      structuredParentChoice= false;
      structuredParentChoiceFor.clear ();
    }
    else {
      structuredParentChoice= true;
      structuredParentChoiceFor= structuredPath.last ();
    }
    searchEdit->clear ();
    updateStructuredList ();
    return;
  }
  if (type != QuickNamespace) return;

  structuredPath= item->data (QuickNamespacePathRole).toStringList ();
  structuredParentChoice= false;
  structuredParentChoiceFor.clear ();
  searchEdit->clear ();
  updateStructuredList ();
}

QListWidget*
QTMQuickSwitcher::activeList () const {
  return tabs->currentIndex () == 0 ? rawList : structuredList;
}

void
QTMQuickSwitcher::switchTab () {
  tabs->setCurrentIndex (tabs->currentIndex () == 0 ? 1 : 0);
}

void
QTMQuickSwitcher::moveSelection (int delta) {
  QListWidget* list= activeList ();
  int count= list->count ();
  if (count <= 0) return;

  int row= list->currentRow ();
  if (row < 0) row= delta > 0 ? -1 : 0;
  row= (row + delta + count) % count;
  list->setCurrentRow (row);
}

void
QTMQuickSwitcher::completeFromSelection () {
  QListWidget* list= activeList ();
  QListWidgetItem* item= list->currentItem ();
  if (item == nullptr && list->count () > 0) item= list->item (0);
  if (item == nullptr) return;
  if (item->data (QuickTypeRole).toInt () == QuickUp) return;

  QString completion= item->data (QuickCompletionRole).toString ();
  if (completion.isEmpty ()) return;
  if (completion.endsWith (".ath")) completion.chop (4);
  searchEdit->setText (completion);
  searchEdit->setCursorPosition (completion.length ());
}

bool
QTMQuickSwitcher::eventFilter (QObject* watched, QEvent* event) {
  if (event->type () == QEvent::KeyPress) {
    QKeyEvent* key= static_cast<QKeyEvent*> (event);
    if (key->key () == Qt::Key_Up || key->key () == Qt::Key_Down) {
      if (watched == searchEdit || watched == rawList ||
          watched == structuredList) {
        moveSelection (key->key () == Qt::Key_Up ? -1 : 1);
        return true;
      }
      return false;
    }
    if (key->key () == Qt::Key_Return || key->key () == Qt::Key_Enter) {
      if (key->modifiers () & Qt::ShiftModifier) acceptCreate ();
      else acceptOpen ();
      return true;
    }
    if (key->key () == Qt::Key_Tab) {
      if (key->modifiers () & Qt::ControlModifier) {
        switchTab ();
        return true;
      }
      completeFromSelection ();
      return true;
    }
  }
  return QDialog::eventFilter (watched, event);
}

tree
QTMQuickSwitcher::getResult () {
  if (!resultAccepted) return UNINIT;
  tree res (TUPLE);
  res << tree (from_qstring (action));
  res << tree (from_qstring (result));
  return res;
}

tree
vault_quick_switcher (array<string> recentFiles) {
  QWidget* parent= QApplication::activeWindow ();
  QTMQuickSwitcher switcher (parent, recentFiles);
  if (switcher.exec () == QDialog::Accepted) return switcher.getResult ();
  return UNINIT;
}
