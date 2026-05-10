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

QTMQuickSwitcher::QTMQuickSwitcher (QWidget* parent, array<string> recentFiles)
  : QDialog (parent), resultAccepted (false) {
  setWindowTitle ("Quick switcher");
  setWindowFlags (windowFlags () | Qt::Tool);
  resize (1775, 550);

  layout = new QVBoxLayout (this);
  prompt = new QLabel ("Type to switch ATHENA vault files", this);
  layout->addWidget (prompt);

  searchEdit = new QLineEdit (this);
  searchEdit->setPlaceholderText ("Search .ath files");
  layout->addWidget (searchEdit);

  resultList = new QListWidget (this);
  layout->addWidget (resultList);

  searchEdit->installEventFilter (this);
  resultList->installEventFilter (this);

  loadFiles (recentFiles);
  updateList ();

  connect (searchEdit, &QLineEdit::textChanged,
           this, [this] (const QString&) { updateList (); });
  connect (resultList, &QListWidget::itemDoubleClicked,
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

void
QTMQuickSwitcher::updateList () {
  resultList->clear ();
  QString query= searchEdit->text ().trimmed ().toLower ();

  if (query.isEmpty ()) {
    prompt->setText ("Recent ATHENA vault files");
    int n= 0;
    for (int index : recentIndices) {
      QListWidgetItem* item= new QListWidgetItem (entries[index].relPath);
      item->setData (Qt::UserRole, entries[index].relPath);
      resultList->addItem (item);
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
      item->setData (Qt::UserRole, e.relPath);
      resultList->addItem (item);
      if (++n >= quick_switcher_limit) break;
    }
  }

  if (resultList->count () > 0) resultList->setCurrentRow (0);
}

void
QTMQuickSwitcher::acceptOpen () {
  QListWidgetItem* item= resultList->currentItem ();
  if (item == nullptr && resultList->count () > 0) item= resultList->item (0);
  if (item == nullptr) return;

  action= "open";
  result= item->data (Qt::UserRole).toString ();
  resultAccepted= true;
  accept ();
}

void
QTMQuickSwitcher::acceptCreate () {
  if (resultList->count () > 0) {
    acceptOpen ();
    return;
  }

  QString query= searchEdit->text ().trimmed ();
  if (query.isEmpty ()) return;

  action= "create";
  result= query;
  resultAccepted= true;
  accept ();
}

void
QTMQuickSwitcher::completeFromSelection () {
  QListWidgetItem* item= resultList->currentItem ();
  if (item == nullptr && resultList->count () > 0) item= resultList->item (0);
  if (item == nullptr) return;

  QString completion= item->data (Qt::UserRole).toString ();
  if (completion.endsWith (".ath")) completion.chop (4);
  searchEdit->setText (completion);
  searchEdit->setCursorPosition (completion.length ());
}

bool
QTMQuickSwitcher::eventFilter (QObject* watched, QEvent* event) {
  if (event->type () == QEvent::KeyPress) {
    QKeyEvent* key= static_cast<QKeyEvent*> (event);
    if (key->key () == Qt::Key_Up || key->key () == Qt::Key_Down) {
      if (watched == searchEdit) {
        QApplication::sendEvent (resultList, event);
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
