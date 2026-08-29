/******************************************************************************
* MODULE     : QTMArtifactsPane.cpp
* DESCRIPTION: Artifact browser and build commands
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
******************************************************************************/

#include "QTMArtifactsPane.hpp"

#include "QTMDelegationClient.hpp"
#include "QTMMainTabWindow.hpp"
#include "ATHENA/Data/new_buffer.hpp"
#include "ATHENA/Data/namespaces.hpp"
#include "ATHENA/Data/vault.hpp"
#include "boot.hpp"
#include "convert.hpp"
#include "scheme.hpp"
#include "qt_utilities.hpp"

#include <DockWidget.h>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSizePolicy>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <filesystem>
#include <set>

namespace fs= std::filesystem;

namespace {

QTMArtifactsPane* artifacts_widget= nullptr;
ads::CDockWidget* artifacts_dock= nullptr;

std::string std_string (string value) {
  return std::string (as_charp (value), (size_t) N(value));
}

QString qstr (const std::string& value) {
  return QString::fromUtf8 (value.data (), (qsizetype) value.size ());
}

string tmstr (const std::string& value) {
  return string (value.data (), (int) value.size ());
}

fs::path active_root () {
  return vault_active () ? fs::path (std_string (concretize (vault_get_root ())))
                         : fs::path ();
}

void execute_open (const AthenaArtifactRecord& record) {
  fs::path absolute= active_root () / fs::path (record.relative_path);
  url file= url_system (tmstr (absolute.string ()));
  try {
    tree document= concrete_buffer (file) != nullptr
      ? get_buffer_tree (file) : import_tree (file, "texmacs");
    path source_path;
    std::string error;
    if (athena_artifact_locate_source (
          document, record, source_path, error)) {
      array<object> cmd;
      cmd << symbol_object ("artifact-jump-to-position") << object (file)
          << list_object (symbol_object ("quote"), object (source_path));
      exec_delayed (scheme_cmd (as_list_object (cmd)));
      return;
    }
    std_warning << "Could not locate artifact in source: " << error.c_str ()
                << LF;
  }
  catch (...) {
    std_warning << "Could not read artifact source for navigation" << LF;
  }
  std::string stale_error;
  if (!athena_artifacts_mark_document_stale (
        active_root (), record.relative_path, stale_error))
    std_warning << "Could not schedule stale artifact document for rebuild: "
                << stale_error.c_str () << LF;
  array<object> cmd;
  cmd << symbol_object ("artifact-navigation-failed")
      << object (utf8_to_cork (tmstr (record.relative_path)));
  exec_delayed (scheme_cmd (as_list_object (cmd)));
}

void build_with_dialog (bool current_only) {
  constexpr int dialog_width= 620;
  if (!vault_active ()) {
    QMessageBox::warning (QApplication::activeWindow (), "Build artifacts",
                          "No active vault. Please load a vault first.");
    return;
  }
  QDialog dialog (QApplication::activeWindow ());
  dialog.setWindowTitle (current_only ? "Build artifacts for current document"
                                      : "Build artifacts for entire vault");
  dialog.setModal (true);
  dialog.setFixedWidth (dialog_width);
  QVBoxLayout* layout= new QVBoxLayout (&dialog);
  QLabel* status= new QLabel ("Preparing artifact databases...", &dialog);
  status->setWordWrap (true);
  status->setMinimumWidth (0);
  status->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Preferred);
  QProgressBar* progress= new QProgressBar (&dialog);
  progress->setRange (0, 1);
  progress->setValue (0);
  QPushButton* cancel= new QPushButton ("Cancel", &dialog);
  QLabel* delegatedState= new QLabel (&dialog);
  delegatedState->setTextFormat (Qt::RichText);
  delegatedState->hide ();
  layout->addWidget (status);
  layout->addWidget (progress);
  layout->addWidget (delegatedState);
  layout->addWidget (cancel, 0, Qt::AlignRight);
  bool cancelled= false;
  QObject::connect (cancel, &QPushButton::clicked, [&] () { cancelled= true; });
  dialog.adjustSize ();
  dialog.setFixedWidth (dialog_width);
  dialog.show ();
  QApplication::processEvents ();

  AthenaArtifactsBuildResult result;
  std::string error;
  AthenaArtifactsBuildOptions options;
  if (get_preference ("artifact definition span delegation enabled", "off") ==
      "on") {
    QTMDelegationServer server;
    QString configured= qstr (std_string (
      get_preference ("delegation server", "")));
    if (!qtm_delegation_selected_server (configured, server)) {
      dialog.close ();
      QMessageBox::critical (
        QApplication::activeWindow (), "Build artifacts",
        "Artifact delegation is enabled, but no delegation server is "
        "configured.");
      return;
    }
    options.range_selector=
      [server] (const std::vector<AthenaArtifactRangeRequest>& requests,
                std::vector<std::vector<int>>& results,
                const AthenaArtifactRangeSelectionProgress& update,
                std::string& selectorError) {
        QString qerror;
        bool ok= qtm_delegation_select_artifact_ranges (
          server, requests, results,
          [&] (size_t completed, size_t total, size_t queued, size_t running) {
            return !update || update (completed, total, queued, running);
          }, &qerror);
        if (!ok) selectorError= qerror.toStdString ();
        return ok;
      };
  }
  bool ok= athena_artifacts_build_active_vault (
    current_only,
    [&] (const AthenaArtifactsProgressEvent& event) {
      if (event.total == 0) progress->setRange (0, 0);
      else {
        progress->setRange (0, std::max (1, (int) event.total));
        progress->setValue ((int) event.current);
      }
      QString path= qstr (event.path);
      QString detail= qstr (event.detail);
      QString message;
      switch (event.phase) {
      case AthenaArtifactsBuildPhase::Preparing:
        message= "Preparing artifact databases..."; break;
      case AthenaArtifactsBuildPhase::Extracting:
        message= path.isEmpty () ? "Extracting document structures..."
                                 : QString ("Extracting %1").arg (path);
        break;
      case AthenaArtifactsBuildPhase::SelectingDefinitionRanges:
        message=
          detail.isEmpty ()
            ? "Selecting semantic definition ranges..."
            : QString ("Selecting definition range for %1 in %2")
                .arg (detail, path);
        break;
      case AthenaArtifactsBuildPhase::WritingDatabase:
        message= path.isEmpty () ? "Writing artifact databases..."
                                 : QString ("Writing %1").arg (path);
        break;
      case AthenaArtifactsBuildPhase::Complete:
        message= "Artifact generation complete"; break;
      }
      status->setText (message);
      status->setToolTip (message);
      dialog.adjustSize ();
      dialog.setFixedWidth (dialog_width);
      if (event.delegated_queued || event.delegated_running) {
        delegatedState->setText (
          QString ("<span style='color:#7b7b7b'>Queued %1</span> &nbsp; "
                   "<span style='color:#b36b00'>Running %2</span> &nbsp; "
                   "<span style='color:#15803d'>Completed %3</span>")
            .arg ((qulonglong) event.delegated_queued)
            .arg ((qulonglong) event.delegated_running)
            .arg ((qulonglong) event.current));
        delegatedState->show ();
      }
      QApplication::processEvents (QEventLoop::AllEvents, 50);
      return !cancelled;
    }, result, error, options);
  dialog.close ();
  if (!ok) {
    if (!cancelled)
      QMessageBox::critical (QApplication::activeWindow (), "Build artifacts",
                             qstr (error));
    return;
  }
  if (artifacts_widget) artifacts_widget->refresh ();
  QMessageBox::information (
    QApplication::activeWindow (), "Build artifacts",
    QString ("Indexed %1 artifact(s): %2 enunciation(s) and %3 bold-text "
             "definition(s). Rebuilt %4 document(s); purged %5 deleted "
             "document(s).")
      .arg ((qulonglong) result.artifacts)
      .arg ((qulonglong) result.enunciations)
      .arg ((qulonglong) result.bold_texts)
      .arg ((qulonglong) result.documents_changed)
      .arg ((qulonglong) result.documents_deleted));
}

} // namespace

QTMArtifactsPane::QTMArtifactsPane (QWidget* parent): QWidget (parent) {
  QVBoxLayout* outer= new QVBoxLayout (this);
  QHBoxLayout* controls= new QHBoxLayout;
  scope= new QComboBox (this);
  scope->addItems ({"Current document", "Entire vault", "Within namespace"});
  namespaceSelector= new QComboBox (this);
  namespaceSelector->setEnabled (false);
  search= new QLineEdit (this);
  search->setPlaceholderText ("Search artifacts");
  controls->addWidget (new QLabel ("Show:", this));
  controls->addWidget (scope);
  controls->addWidget (namespaceSelector, 1);
  controls->addWidget (search, 2);
  outer->addLayout (controls);

  table= new QTableWidget (0, 4, this);
  table->setHorizontalHeaderLabels ({"Type", "Origin", "Artifact", "File"});
  table->setSelectionBehavior (QAbstractItemView::SelectRows);
  table->setSelectionMode (QAbstractItemView::SingleSelection);
  table->setEditTriggers (QAbstractItemView::NoEditTriggers);
  table->verticalHeader ()->hide ();
  table->horizontalHeader ()->setSectionResizeMode (0, QHeaderView::ResizeToContents);
  table->horizontalHeader ()->setSectionResizeMode (1, QHeaderView::ResizeToContents);
  table->horizontalHeader ()->setSectionResizeMode (2, QHeaderView::Stretch);
  table->horizontalHeader ()->setSectionResizeMode (3, QHeaderView::Stretch);
  outer->addWidget (table, 1);

  connect (scope, &QComboBox::currentIndexChanged, this, [this] (int index) {
    namespaceSelector->setEnabled (index == 2);
    applyFilter ();
  });
  connect (namespaceSelector, &QComboBox::currentIndexChanged,
           this, [this] { applyFilter (); });
  connect (search, &QLineEdit::textChanged, this, [this] { applyFilter (); });
  connect (table, &QTableWidget::cellDoubleClicked,
           this, [this] (int row, int) { openRow (row); });

  currentWatcher= new QTimer (this);
  currentWatcher->setInterval (500);
  connect (currentWatcher, &QTimer::timeout, this, [this] {
    QString current= currentRelativePath ();
    if (current != lastCurrentPath) {
      lastCurrentPath= current;
      if (scope->currentIndex () == 0) applyFilter ();
    }
  });
  currentWatcher->start ();
  refresh ();
}

QString
QTMArtifactsPane::currentRelativePath () const {
  if (!vault_active ()) return {};
  fs::path root= active_root ();
  fs::path current (std_string (concretize (get_current_buffer_safe ())));
  std::error_code ec;
  fs::path rel= fs::relative (current, root, ec);
  if (ec || rel.string ().rfind ("..", 0) == 0) return {};
  return qstr (rel.generic_string ());
}

void
QTMArtifactsPane::refreshNamespaces () {
  QString selected= namespaceSelector->currentText ();
  namespaceSelector->blockSignals (true);
  namespaceSelector->clear ();
  for (const athena_namespace_definition& ns: athena_namespaces_list ())
    namespaceSelector->addItem (to_qstring (ns.name));
  int index= namespaceSelector->findText (selected);
  if (index >= 0) namespaceSelector->setCurrentIndex (index);
  namespaceSelector->blockSignals (false);
}

void
QTMArtifactsPane::refresh () {
  records.clear ();
  if (vault_active ()) {
    std::string error;
    if (!athena_artifacts_query (active_root (), records, error))
      QMessageBox::warning (this, "Artifacts", qstr (error));
  }
  refreshNamespaces ();
  lastCurrentPath= currentRelativePath ();
  applyFilter ();
}

void
QTMArtifactsPane::applyFilter () {
  std::set<std::string> namespaceFiles;
  if (scope->currentIndex () == 2 && !namespaceSelector->currentText ().isEmpty ()) {
    string error;
    std::vector<athena_namespace_match> members= athena_namespace_members (
      from_qstring (namespaceSelector->currentText ()), error);
    fs::path root= active_root ();
    for (const athena_namespace_match& member: members) {
      fs::path path (std_string (concretize (member.file)));
      std::error_code ec;
      fs::path rel= fs::relative (path, root, ec);
      if (!ec) namespaceFiles.insert (rel.generic_string ());
    }
  }
  QString query= search->text ().trimmed ();
  QString current= currentRelativePath ();
  table->setRowCount (0);
  for (size_t index=0; index<records.size (); index++) {
    const AthenaArtifactRecord& record= records[index];
    if (scope->currentIndex () == 0 && qstr (record.relative_path) != current)
      continue;
    if (scope->currentIndex () == 2 &&
        !namespaceFiles.count (record.relative_path)) continue;
    QString haystack= qstr (record.display_text + " " + record.type + " " +
                            record.relative_path);
    if (!query.isEmpty () && !haystack.contains (query, Qt::CaseInsensitive))
      continue;
    int row= table->rowCount ();
    table->insertRow (row);
    QString origin= record.origin == "bold-text" ? "Bold text" : "Enunciation";
    QString display= qstr (record.display_text);
    if (display.isEmpty ()) display= qstr (record.anchor_stem);
    QStringList values= {qstr (record.type), origin, display,
                          qstr (record.relative_path)};
    for (int column=0; column<4; column++) {
      QTableWidgetItem* item= new QTableWidgetItem (values[column]);
      item->setData (Qt::UserRole, (qulonglong) index);
      table->setItem (row, column, item);
    }
  }
}

void
QTMArtifactsPane::openRow (int row) {
  QTableWidgetItem* item= table->item (row, 0);
  if (!item) return;
  size_t index= (size_t) item->data (Qt::UserRole).toULongLong ();
  if (index < records.size ()) execute_open (records[index]);
}

void
artifacts_pane_show () {
  if (!vault_active ()) {
    QMessageBox::warning (QApplication::activeWindow (), "Artifacts",
                          "No active vault. Please load a vault first.");
    return;
  }
  QTMMainTabWindow* window= QTMMainTabWindow::topTabWindow ();
  if (!window || !window->dockManager ()) return;
  if (!artifacts_widget) {
    artifacts_widget= new QTMArtifactsPane;
    QObject::connect (artifacts_widget, &QObject::destroyed, [] {
      artifacts_widget= nullptr; artifacts_dock= nullptr;
    });
  }
  artifacts_widget->refresh ();
  if (!artifacts_dock) {
    artifacts_dock= new ads::CDockWidget ("Artifacts");
    artifacts_dock->setObjectName ("athena-artifacts");
    artifacts_dock->setWidget (artifacts_widget,
                               ads::CDockWidget::ForceNoScrollArea);
    artifacts_dock->setFeature (ads::CDockWidget::DockWidgetDeleteOnClose,
                                false);
    QObject::connect (artifacts_dock, &QObject::destroyed,
                      [] { artifacts_dock= nullptr; });
  }
  window->showAdsDockWidget (artifacts_dock, ads::RightDockWidgetArea);
}

void artifacts_build_entire_vault () { build_with_dialog (false); }
void artifacts_build_current_document () { build_with_dialog (true); }

bool
artifacts_open_uuid (string uuid) {
  if (!vault_active ()) return false;
  AthenaArtifactRecord record;
  bool found= false;
  std::string error;
  if (!athena_artifact_query_uuid (active_root (), std_string (uuid), record,
                                   found, error)) {
    std_warning << "Could not resolve artifact UUID: " << error.c_str () << LF;
    return false;
  }
  if (!found) return false;
  execute_open (record);
  return true;
}
