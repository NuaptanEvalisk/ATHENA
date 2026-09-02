/******************************************************************************
* MODULE     : QTMPersonsExplorer.cpp
* DESCRIPTION: Persons explorer for semantic person tags in an ATHENA vault
* COPYRIGHT  : (C) 2026  Nuaptan
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMPersonsExplorer.hpp"

#include "ATHENA/Data/person_names.hpp"
#include "QTMMainTabWindow.hpp"
#include "convert.hpp"
#include "new_buffer.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"
#include "vault.hpp"

#include <DockWidget.h>
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QPointer>
#include <QSizeGrip>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <set>

namespace {

QPointer<QTMPersonsExplorer> persons_explorer_widget;
QPointer<ads::CDockWidget> persons_explorer_dock;
QString cached_person_root;
QStringList cached_person_names;
bool cached_person_names_valid= false;
quint64 cached_person_files_fingerprint= 0;

tree
person_document_body (url file) {
  tree document= import_tree (file, "texmacs");
  tree body= extract (document, "body");
  return is_empty (body) ? document : body;
}

bool
raw_file_has_person_tag (url file) {
  QFile input (to_qstring (concretize (file)));
  if (!input.open (QIODevice::ReadOnly)) return true;
  return input.readAll ().contains ("<person|");
}

quint64
person_files_fingerprint (const array<url>& files) {
  quint64 fingerprint= 1469598103934665603ULL;
  for (int i=0; i<N(files); i++) {
    if (suffix (files[i]) != "ath") continue;
    QFileInfo info (to_qstring (concretize (files[i])));
    fingerprint ^= qHash (info.absoluteFilePath ());
    fingerprint *= 1099511628211ULL;
    fingerprint ^= (quint64) info.size ();
    fingerprint *= 1099511628211ULL;
    fingerprint ^= (quint64) info.lastModified ().toMSecsSinceEpoch ();
    fingerprint *= 1099511628211ULL;
  }
  return fingerprint;
}

void
set_cached_person_names (const QMap<QString,
                         std::vector<QTMPersonOccurrence>>& index) {
  cached_person_root= vault_active ()
    ? to_qstring (concretize (vault_get_root ())) : QString ();
  cached_person_names= index.keys ();
  cached_person_names.sort (Qt::CaseInsensitive);
  cached_person_files_fingerprint=
    vault_active () ? person_files_fingerprint (vault_get_all_files ()) : 0;
  cached_person_names_valid= true;
}

} // namespace

QStringList
qtm_vault_person_names () {
  if (!vault_active ()) return {};
  QString root= to_qstring (concretize (vault_get_root ()));
  array<url> files= vault_get_all_files ();
  quint64 fingerprint= person_files_fingerprint (files);
  if (cached_person_names_valid && root == cached_person_root &&
      fingerprint == cached_person_files_fingerprint)
    return cached_person_names;

  std::set<QString> names;
  for (int i=0; i<N(files); i++) {
    if (suffix (files[i]) != "ath" || !raw_file_has_person_tag (files[i]))
      continue;
    try {
      std::vector<string> found=
        athena_collect_person_names (person_document_body (files[i]));
      for (const string& name: found) names.insert (to_qstring (name));
    }
    catch (...) {}
  }

  cached_person_root= root;
  cached_person_names.clear ();
  for (const QString& name: names) cached_person_names << name;
  cached_person_names.sort (Qt::CaseInsensitive);
  cached_person_files_fingerprint= fingerprint;
  cached_person_names_valid= true;
  return cached_person_names;
}

QTMPersonsExplorer::QTMPersonsExplorer (QWidget* parent)
  : QWidget (parent), scanIndex (0) {
  setMinimumSize (560, 360);

  refreshButton= new QPushButton ("Refresh", this);
  status= new QLabel (this);
  people= new QListWidget (this);
  occurrences= new QListWidget (this);
  floatingSizeGrip= new QSizeGrip (this);
  floatingSizeGrip->hide ();

  people->setAlternatingRowColors (true);
  occurrences->setAlternatingRowColors (true);
  people->setMinimumWidth (220);

  QWidget* peoplePane= new QWidget (this);
  QVBoxLayout* peopleLayout= new QVBoxLayout (peoplePane);
  peopleLayout->setContentsMargins (0, 0, 0, 0);
  peopleLayout->addWidget (new QLabel ("Persons", peoplePane));
  peopleLayout->addWidget (people, 1);

  QWidget* occurrencePane= new QWidget (this);
  QVBoxLayout* occurrenceLayout= new QVBoxLayout (occurrencePane);
  occurrenceLayout->setContentsMargins (0, 0, 0, 0);
  occurrenceLayout->addWidget (
    new QLabel ("Occurrences in the current vault", occurrencePane));
  occurrenceLayout->addWidget (occurrences, 1);

  splitter= new QSplitter (Qt::Horizontal, this);
  splitter->addWidget (peoplePane);
  splitter->addWidget (occurrencePane);
  splitter->setStretchFactor (0, 0);
  splitter->setStretchFactor (1, 1);
  splitter->setSizes ({260, 540});

  QHBoxLayout* top= new QHBoxLayout ();
  top->addWidget (refreshButton);
  top->addWidget (status, 1);

  QHBoxLayout* grip= new QHBoxLayout ();
  grip->setContentsMargins (0, 0, 0, 0);
  grip->addStretch ();
  grip->addWidget (floatingSizeGrip);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->setContentsMargins (8, 8, 8, 8);
  layout->addLayout (top);
  layout->addWidget (splitter, 1);
  layout->addLayout (grip);

  scanTimer= new QTimer (this);
  scanTimer->setInterval (0);
  connect (scanTimer, &QTimer::timeout, this,
           [this] () { scanChunk (); });
  connect (refreshButton, &QPushButton::clicked, this,
           [this] () { refresh (); });
  connect (people, &QListWidget::currentItemChanged, this,
           [this] (QListWidgetItem* current, QListWidgetItem*) {
             showOccurrences (current);
           });
  connect (occurrences, &QListWidget::itemClicked, this,
           [this] (QListWidgetItem* item) { openOccurrence (item); });

  refresh ();
}

QSize
QTMPersonsExplorer::sizeHint () const {
  return QSize (820, 560);
}

void
QTMPersonsExplorer::setFloatingResizeGripVisible (bool visible) {
  floatingSizeGrip->setVisible (visible);
}

QString
QTMPersonsExplorer::relativePath (url file) const {
  return to_qstring (as_unix_string (
    delta (vault_get_root () * url (""), file)));
}

void
QTMPersonsExplorer::refresh () {
  scanTimer->stop ();
  scanFiles.clear ();
  scanIndex= 0;
  index.clear ();
  people->clear ();
  occurrences->clear ();

  if (!vault_active ()) {
    status->setText ("No active vault.");
    return;
  }
  array<url> files= vault_get_all_files ();
  for (int i=0; i<N(files); i++)
    if (suffix (files[i]) == "ath") scanFiles.push_back (files[i]);
  std::sort (scanFiles.begin (), scanFiles.end (),
             [this] (const url& a, const url& b) {
               return relativePath (a) < relativePath (b);
             });

  status->setText (
    QString ("Scanning 0/%1 files...").arg ((int) scanFiles.size ()));
  refreshButton->setEnabled (false);
  scanTimer->start ();
}

void
QTMPersonsExplorer::scanChunk () {
  const int chunk_size= 8;
  int end= std::min (scanIndex + chunk_size, (int) scanFiles.size ());
  for (; scanIndex<end; scanIndex++) {
    url file= scanFiles[(size_t) scanIndex];
    if (!raw_file_has_person_tag (file)) continue;
    try {
      std::vector<athena_person_occurrence> found=
        athena_collect_person_occurrences (person_document_body (file));
      QString rel= relativePath (file);
      for (const athena_person_occurrence& occurrence: found) {
        QString name= to_qstring (occurrence.name);
        index[name].push_back ({rel, file, occurrence.where});
      }
    }
    catch (...) {}
  }

  status->setText (
    QString ("Scanning %1/%2 files; %3 person(s).")
      .arg (scanIndex).arg ((int) scanFiles.size ()).arg (index.size ()));
  if (scanIndex >= (int) scanFiles.size ()) finishScan ();
}

void
QTMPersonsExplorer::finishScan () {
  scanTimer->stop ();
  refreshButton->setEnabled (true);
  people->clear ();
  QStringList names= index.keys ();
  names.sort (Qt::CaseInsensitive);
  for (const QString& name: names) {
    QListWidgetItem* item= new QListWidgetItem (
      QString ("%1 (%2)").arg (name).arg ((int) index[name].size ()), people);
    item->setData (Qt::UserRole, name);
  }
  set_cached_person_names (index);
  status->setText (
    QString ("%1 person(s), %2 tagged occurrence(s) in %3 file(s).")
      .arg (index.size ())
      .arg ([this] () {
        int count= 0;
        for (auto it=index.cbegin (); it!=index.cend (); ++it)
          count += (int) it.value ().size ();
        return count;
      } ())
      .arg ((int) scanFiles.size ()));
  if (people->count () > 0) people->setCurrentRow (0);
}

void
QTMPersonsExplorer::showOccurrences (QListWidgetItem* person) {
  occurrences->clear ();
  if (person == nullptr) return;
  QString name= person->data (Qt::UserRole).toString ();
  const std::vector<QTMPersonOccurrence>& found= index[name];
  for (int i=0; i<(int) found.size (); i++) {
    QListWidgetItem* item= new QListWidgetItem (
      QString ("%1  (%2)").arg (found[(size_t) i].relativePath).arg (i + 1),
      occurrences);
    item->setData (Qt::UserRole, i);
    item->setToolTip (found[(size_t) i].relativePath);
  }
  occurrences->setProperty ("athena-person-name", name);
  if (occurrences->count () > 0) occurrences->setCurrentRow (0);
}

void
QTMPersonsExplorer::openOccurrence (QListWidgetItem* occurrence) {
  if (occurrence == nullptr) return;
  QString name= occurrences->property ("athena-person-name").toString ();
  int selected= occurrence->data (Qt::UserRole).toInt ();
  const std::vector<QTMPersonOccurrence>& found= index[name];
  if (selected < 0 || selected >= (int) found.size ()) return;

  const QTMPersonOccurrence& target= found[(size_t) selected];
  path position= target.where * 0 * 0;
  array<object> cmd;
  cmd << symbol_object ("global-search-open-result")
      << object (target.file)
      << list_object (symbol_object ("quote"), object (position));
  exec_delayed (scheme_cmd (as_list_object (cmd)));
}

void
persons_explorer_show () {
  if (!vault_active ()) {
    QMessageBox::warning (QApplication::activeWindow (), "Persons Explorer",
                          "No active vault. Please load a vault first.");
    return;
  }
  QTMMainTabWindow* window= QTMMainTabWindow::topTabWindow ();
  if (window == nullptr || window->dockManager () == nullptr) return;

  if (persons_explorer_widget == nullptr) {
    persons_explorer_widget= new QTMPersonsExplorer ();
    QObject::connect (persons_explorer_widget, &QObject::destroyed, [] () {
      persons_explorer_widget= nullptr;
      persons_explorer_dock= nullptr;
    });
  }
  else persons_explorer_widget->refresh ();

  if (persons_explorer_dock == nullptr) {
    persons_explorer_dock= new ads::CDockWidget ("Persons Explorer");
    persons_explorer_dock->setObjectName ("athena-persons-explorer");
    persons_explorer_dock->setWidget (persons_explorer_widget);
    persons_explorer_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QObject::connect (
      persons_explorer_dock, &ads::CDockWidget::topLevelChanged,
      persons_explorer_widget, [] (bool floating) {
        if (persons_explorer_widget != nullptr)
          persons_explorer_widget->setFloatingResizeGripVisible (floating);
      });
    window->showAdsDockWidget (persons_explorer_dock,
                               ads::RightDockWidgetArea);
  }
  window->showAdsDockWidget (persons_explorer_dock,
                             ads::RightDockWidgetArea);
  persons_explorer_widget->setFloatingResizeGripVisible (
    persons_explorer_dock->isInFloatingContainer ());
}
