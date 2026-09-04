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
#include "actor_transport.hpp"
#include "buffer_actor.hpp"
#include "scheme.hpp"
#include "scheme_execution_context.hpp"
#include "vault.hpp"
#include "qt_utilities.hpp"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHash>
#include <QKeyEvent>
#include <QMetaObject>
#include <QShowEvent>
#include <QThread>
#include <QVariant>
#include <algorithm>
#include <memory>
#include <mutex>
#include <unordered_map>
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

struct QuickSwitcherRequest {
  array<string> recentFiles;
  athena_actor_id actorId= ATHENA_NO_ACTOR;
  athena_view_id viewId= ATHENA_NO_VIEW;
  SchemeCapabilitySet capabilities= SCHEME_CAPABILITY_NONE;
};

std::mutex quickRequestMutex;
std::unordered_map<athena_resource_id, std::unique_ptr<QuickSwitcherRequest>>
  quickRequests;
athena_resource_id nextQuickRequestId= 1;

athena_resource_id
registerQuickRequest (array<string> recentFiles,
                      const SchemeExecutionContext* context) {
  for (int i= 0; i < N (recentFiles); ++i)
    recentFiles[i].ensure_transferable ();
  auto request= std::make_unique<QuickSwitcherRequest> ();
  request->recentFiles= std::move (recentFiles);
  if (context != nullptr) {
    request->actorId= context->actor_id;
    request->viewId= context->view_id;
    request->capabilities= context->capabilities;
  }
  std::lock_guard<std::mutex> guard (quickRequestMutex);
  athena_resource_id id= nextQuickRequestId++;
  if (id == 0) id= nextQuickRequestId++;
  quickRequests.emplace (id, std::move (request));
  return id;
}

tree runQuickSwitcher (array<string> recentFiles);

void
executeQuickRequest (athena_resource_id id) {
  std::unique_ptr<QuickSwitcherRequest> request;
  {
    std::lock_guard<std::mutex> guard (quickRequestMutex);
    auto found= quickRequests.find (id);
    if (found == quickRequests.end ()) return;
    request= std::move (found->second);
    quickRequests.erase (found);
  }
  tree result= runQuickSwitcher (std::move (request->recentFiles));
  if (result == UNINIT || request->actorId == ATHENA_NO_ACTOR) return;

  athena_continuation_id continuationId=
    actor_continuation_registry::instance ().store (
      [result= std::move (result)] () mutable {
        (void) call ("quick-switcher-complete", object (std::move (result)));
      });
  actor_command_ticket ticket= buffer_actor::submit_to (
    request->actorId, actor_command_kind::run_native_continuation,
    request->viewId, ATHENA_NO_BLOB, ATHENA_NO_BLOB,
    request->capabilities, continuationId);
  if (!ticket) {
    (void) actor_continuation_registry::instance ().discard (continuationId);
  }
}
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
  recentList= new QListWidget (this);
  tabs->addTab (rawList, "Raw");
  tabs->addTab (structuredList, "Structured");
  tabs->addTab (recentList, "Recents");
  layout->addWidget (tabs);

  searchEdit->installEventFilter (this);
  rawList->installEventFilter (this);
  structuredList->installEventFilter (this);
  recentList->installEventFilter (this);

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
  connect (recentList, &QListWidget::itemDoubleClicked,
           this, [this] (QListWidgetItem*) { acceptOpen (); });

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
  QHash<QString, int> entryByCanonicalPath;

  for (int i=0; i<N(files); i++) {
    url rel= delta (root * url (""), files[i]);
    if (suffix (rel) != "ath") continue;
    QString relPath= to_qstring (as_unix_string (rel));
    QString base= relPath.section ('/', -1);
    if (base.endsWith (".ath")) base.chop (4);

    Entry e;
    e.relPath= relPath;
    e.baseName= base;
    QString searchPath= relPath;
    if (searchPath.endsWith (".ath")) searchPath.chop (4);
    e.searchPath= from_qstring (searchPath);
    e.searchBase= from_qstring (base);
    e.mtime= vault_get_mtime (files[i]);
    entries.push_back (e);
    QString absolutePath= to_qstring (concretize (files[i]));
    QString canonicalPath= QFileInfo (absolutePath).canonicalFilePath ();
    if (canonicalPath.isEmpty ())
      canonicalPath= QDir::cleanPath (QFileInfo (absolutePath).absoluteFilePath ());
    entryByCanonicalPath.insert (canonicalPath, (int) entries.size () - 1);
  }

  for (int i=0; i<N(recentFiles); i++) {
    QString recentPath= to_qstring (recentFiles[i]);
    QString canonicalPath= QFileInfo (recentPath).canonicalFilePath ();
    if (canonicalPath.isEmpty ()) continue;
    auto found= entryByCanonicalPath.constFind (canonicalPath);
    if (found == entryByCanonicalPath.constEnd ()) continue;
    int index= found.value ();
    if (std::find (recentIndices.begin (), recentIndices.end (), index) ==
        recentIndices.end ())
      recentIndices.push_back (index);
  }

  rawDefaultIndices= recentIndices;

  std::vector<std::pair<int,int> > byMtime;
  for (int i=0; i<(int) entries.size (); i++) {
    if (std::find (rawDefaultIndices.begin (), rawDefaultIndices.end (), i) ==
        rawDefaultIndices.end ())
      byMtime.push_back (std::make_pair (-entries[i].mtime, i));
  }
  std::sort (byMtime.begin (), byMtime.end ());
  for (auto p : byMtime)
    rawDefaultIndices.push_back (p.second);
}

void
QTMQuickSwitcher::loadNamespaces () {
  namespaces= athena_namespaces_list ();
}

int
QTMQuickSwitcher::fuzzyScore (const Entry& e, string query) const {
  array<fuzzy_rank_field> fields;
  fields << fuzzy_rank_field (e.searchBase, 100);
  fields << fuzzy_rank_field (e.searchPath, 35);
  fuzzy_rank_result result= fuzzy_rank (query, fields);
  return result.matched ? result.score : -1;
}

int
QTMQuickSwitcher::fuzzyScore (string text, string query) const {
  array<fuzzy_rank_field> fields;
  fields << fuzzy_rank_field (text, 100);
  fuzzy_rank_result result= fuzzy_rank (query, fields);
  return result.matched ? result.score : -1;
}

void
QTMQuickSwitcher::updateList () {
  if (tabs->currentIndex () == 0) updateRawList ();
  else if (tabs->currentIndex () == 1) updateStructuredList ();
  else updateRecentList ();
}

void
QTMQuickSwitcher::updateRawList () {
  rawList->clear ();
  QString queryText= searchEdit->text ().trimmed ();
  string query= from_qstring (queryText);

  if (queryText.isEmpty ()) {
    prompt->setText ("Recent ATHENA vault files");
    int n= 0;
    for (int index : rawDefaultIndices) {
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

void
QTMQuickSwitcher::updateRecentList () {
  recentList->clear ();
  prompt->setText ("Recently opened ATHENA vault files");
  QString queryText= searchEdit->text ().trimmed ();
  string query= from_qstring (queryText);

  int n= 0;
  if (queryText.isEmpty ()) {
    for (int index: recentIndices) {
      QListWidgetItem* item= new QListWidgetItem (entries[index].relPath);
      item->setData (QuickTypeRole, QuickFile);
      item->setData (QuickPayloadRole, entries[index].relPath);
      item->setData (QuickCompletionRole, entries[index].relPath);
      recentList->addItem (item);
      if (++n >= quick_switcher_limit) break;
    }
  }
  else {
    std::vector<std::pair<int,int> > matches;
    for (int order=0; order<(int) recentIndices.size (); order++) {
      int index= recentIndices[order];
      int score= fuzzyScore (entries[index], query);
      if (score >= 0) matches.push_back (std::make_pair (-score, order));
    }
    std::sort (matches.begin (), matches.end ());
    for (auto match: matches) {
      int index= recentIndices[match.second];
      QListWidgetItem* item= new QListWidgetItem (entries[index].relPath);
      item->setData (QuickTypeRole, QuickFile);
      item->setData (QuickPayloadRole, entries[index].relPath);
      item->setData (QuickCompletionRole, entries[index].relPath);
      recentList->addItem (item);
      if (++n >= quick_switcher_limit) break;
    }
  }

  if (recentList->count () > 0) recentList->setCurrentRow (0);
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
  QString queryText= searchEdit->text ().trimmed ();
  string query= from_qstring (queryText);
  QString current= structuredCurrentNamespace ();

  QStringList namespaceNames;
  QStringList namespacePaths;
  std::vector<string> namespaceSearchNames;
  if (structuredParentChoice) {
    prompt->setText (QString ("Parents of namespace %1")
                     .arg (structuredParentChoiceFor));
    namespaceNames= structuredParentsOf (structuredParentChoiceFor);
    for (const QString& name: namespaceNames) {
      namespacePaths << name;
      namespaceSearchNames.push_back (from_qstring (name));
    }
  }
  else if (current.isEmpty ()) {
    prompt->setText ("Structured namespaces");
    for (const athena_namespace_definition& ns: namespaces) {
      QString name= to_qstring (ns.name);
      namespaceNames << name;
      namespacePaths << name;
      namespaceSearchNames.push_back (ns.name);
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
      namespaceSearchNames.push_back (ns.name);
    }
  }

  std::vector<std::pair<int,int> > nsMatches;
  for (int i=0; i<namespaceNames.size (); i++) {
    int score= fuzzyScore (namespaceSearchNames[i], query);
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
    if (queryText.isEmpty ()) {
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
        string relString= as_unix_string (rel);
        array<fuzzy_rank_field> fields;
        fields << fuzzy_rank_field (m.stem, 100);
        fields << fuzzy_rank_field (relString, 35);
        fuzzy_rank_result result= fuzzy_rank (query, fields);
        int score= result.matched ? result.score : -1;
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
  if (tabs->currentIndex () == 1) {
    acceptStructuredOpen ();
    return;
  }

  QListWidget* list= tabs->currentIndex () == 2 ? recentList : rawList;
  QListWidgetItem* item= list->currentItem ();
  if (item == nullptr && list->count () > 0) item= list->item (0);
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
  if (tabs->currentIndex () == 0) return rawList;
  if (tabs->currentIndex () == 1) return structuredList;
  return recentList;
}

void
QTMQuickSwitcher::switchTab () {
  tabs->setCurrentIndex ((tabs->currentIndex () + 1) % tabs->count ());
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
QTMQuickSwitcher::moveSelectionPage (int direction) {
  QListWidget* list= activeList ();
  int count= list->count ();
  if (count <= 0) return;

  int row= list->currentRow ();
  if (row < 0) row= direction > 0 ? 0 : count - 1;
  int rowHeight= list->sizeHintForRow (row);
  if (rowHeight <= 0) rowHeight= list->fontMetrics ().height ();
  int pageRows= std::max (1, list->viewport ()->height () /
                             std::max (1, rowHeight) - 1);
  row= std::clamp (row + direction * pageRows, 0, count - 1);
  list->setCurrentRow (row);
  list->scrollToItem (list->item (row), QAbstractItemView::PositionAtCenter);
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
          watched == structuredList || watched == recentList) {
        moveSelection (key->key () == Qt::Key_Up ? -1 : 1);
        return true;
      }
      return false;
    }
    if (key->key () == Qt::Key_PageUp ||
        key->key () == Qt::Key_PageDown) {
      if (watched == searchEdit || watched == rawList ||
          watched == structuredList || watched == recentList) {
        moveSelectionPage (key->key () == Qt::Key_PageUp ? -1 : 1);
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

namespace {

tree
runQuickSwitcher (array<string> recentFiles) {
  QWidget* parent= QApplication::activeWindow ();
  QTMQuickSwitcher switcher (parent, std::move (recentFiles));
  if (switcher.exec () == QDialog::Accepted) return switcher.getResult ();
  return UNINIT;
}

} // namespace

void
vault_quick_switcher (array<string> recentFiles) {
  QCoreApplication* app= QCoreApplication::instance ();
  if (app == nullptr) return;

  athena_resource_id requestId=
    registerQuickRequest (
      std::move (recentFiles), current_scheme_execution_context ());
  bool invoked= QMetaObject::invokeMethod (
    app, [requestId] () { executeQuickRequest (requestId); },
    Qt::QueuedConnection);
  if (!invoked) {
    std::lock_guard<std::mutex> guard (quickRequestMutex);
    quickRequests.erase (requestId);
  }
}
