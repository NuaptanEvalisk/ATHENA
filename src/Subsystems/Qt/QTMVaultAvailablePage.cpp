/******************************************************************************
* MODULE     : QTMVaultAvailablePage.cpp
* DESCRIPTION: Cancellable source-preserving wikilink enumeration on SearchWorkers
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "QTMVaultAvailablePage.hpp"
#include "QTMVaultAnchorModel.hpp"
#include "QTMVaultLinkModel.hpp"
#include "QTMVaultPreviewBuilder.hpp"
#include "QTMVaultSearchWorker.hpp"
#include "ATHENA/Data/transclusion_cache.hpp"
#include "convert.hpp"
#include "new_buffer.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <QWizard>
#include <algorithm>

struct QTMVaultAvailablePage::Task {
  std::atomic<bool> cancelled {false}, done {false};
  AvailableEnunciations result;
  QString error;
};

QTMVaultAvailablePage::QTMVaultAvailablePage (QWidget* parent):
  QWizardPage (parent), source (get_current_buffer_safe ()),
  relative (current_vault_relative_document ()), context (vault_capture_context ()),
  preview (this) {
  setTitle ("Available in current document");
  query= new QLineEdit (this);
  query->setPlaceholderText ("Filter enunciations");
  display= new QLineEdit (this);
  display->setPlaceholderText ("Link text");
  kind= new QComboBox (this);
  kind->addItem ("All enunciations", "");
  for (const auto& entry: wikilink_enunciation_filters)
    kind->addItem (entry.label, normalized_enunciation_tag (entry.tag));
  caseInsensitive= new QCheckBox ("Case-insensitive", this);
  caseInsensitive->setChecked (true);
  fuzzy= new QCheckBox ("Fuzzy", this);
  stop= new QPushButton ("Stop", this);
  status= new QLabel (this);
  status->setWordWrap (true);
  progress= new QProgressBar (this);
  list= new QListWidget (this);
  list->setAlternatingRowColors (true);
  previewTitle= new QLabel (this);
  previewTitle->setWordWrap (true);
  auto* layout= new QVBoxLayout (this);
  auto* filters= new QHBoxLayout;
  filters->addWidget (query, 1);
  filters->addWidget (kind);
  filters->addWidget (caseInsensitive);
  filters->addWidget (fuzzy);
  filters->addWidget (stop);
  layout->addLayout (filters);
  layout->addWidget (status);
  layout->addWidget (progress);
  auto* splitter= new QSplitter (this);
  list->setMinimumWidth (240);
  splitter->addWidget (list);
  auto* right= new QWidget (splitter);
  right->setMinimumWidth (320);
  auto* rightLayout= new QVBoxLayout (right);
  rightLayout->setContentsMargins (0, 0, 0, 0);
  rightLayout->addWidget (previewTitle);
  previewHost= new QWidget (right);
  previewHost->setObjectName ("availableEnunciationPreview");
  previewHost->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Expanding);
  auto* previewLayout= new QVBoxLayout (previewHost);
  previewLayout->setContentsMargins (0, 0, 0, 0);
  rightLayout->addWidget (previewHost, 1);
  splitter->addWidget (right);
  splitter->setStretchFactor (0, 1);
  splitter->setStretchFactor (1, 2);
  splitter->setSizes ({400, 760});
  layout->addWidget (splitter, 1);
  layout->addWidget (display);
  timer= new QTimer (this);
  timer->setInterval (75);
  connect (query, &QLineEdit::textChanged, this, [this] { filter (); });
  connect (kind, &QComboBox::currentIndexChanged, this, [this] { filter (); });
  connect (caseInsensitive, &QCheckBox::toggled, this, [this] { filter (); });
  connect (fuzzy, &QCheckBox::toggled, this, [this] { filter (); });
  connect (list, &QListWidget::currentRowChanged, this, [this] { showPreview (); });
  connect (list, &QListWidget::itemDoubleClicked, this, [this] {
    if (validatePage ()) wizard ()->accept ();
  });
  connect (stop, &QPushButton::clicked, this, [this] {
    if (task) task->cancelled= true;
    stop->setEnabled (false);
    status->setText ("Stopping...");
  });
  connect (timer, &QTimer::timeout, this, [this] {
    if (!task || !task->done.load (std::memory_order_acquire)) return;
    timer->stop ();
    stop->setEnabled (false);
    progress->setRange (0, 1); progress->setValue (1);
    if (!vault_context_is_current (context)) task->error= "The originating vault has closed.";
    if (!task->error.isEmpty ()) {
      status->setText (task->error);
      emit completeChanged ();
      return;
    }
    entries= std::move (task->result.entries);
    QString text= QString ("%1 available enunciation(s)").arg (entries.size ());
    if (task->cancelled) text += " (stopped)";
    if (relative.isEmpty ()) text += "; save this document in the vault to link its own enunciations";
    if (!task->result.warnings.isEmpty ())
      text += QString ("; %1 unresolved or limited range(s)").arg (task->result.warnings.size ());
    status->setText (text);
    status->setToolTip (task->result.warnings.join ('\n'));
    filter ();
  });
}

QTMVaultAvailablePage::~QTMVaultAvailablePage () { if (task) task->cancelled= true; }

void QTMVaultAvailablePage::setSelectionHandler (QTMVaultArtifactPage::SelectionHandler handler) {
  selectionHandler= std::move (handler);
}

void QTMVaultAvailablePage::cleanupPage () {
  timer->stop ();
  if (task) task->cancelled= true;
}

void QTMVaultAvailablePage::initializePage () {
  cleanupPage ();
  entries.clear (); list->clear (); display->clear ();
  task= std::make_shared<Task> ();
  stop->setEnabled (false);
  if (!context || !vault_context_is_current (context) || is_none (source)) {
    status->setText ("No originating document in an open vault.");
    return;
  }
  tree body= get_buffer_body (source);
  string snapshot= tree_to_scheme (body);
  auto bytes= std::make_shared<const std::string> (snapshot.data (), N (snapshot));
  preview.ensureCreated (previewHost);
  auto job= task;
  auto vault= context;
  const auto path= relative;
  status->setText ("Inspecting document and transclusions...");
  status->setToolTip ({});
  progress->setRange (0, 0);
  stop->setEnabled (true);
  vault_search_workers ().setMaxThreadCount (vault_search_worker_count ());
  vault_search_workers ().start (QRunnable::create ([job, vault, bytes, path] {
    VaultSearchCancellationScope cancellation (&job->cancelled);
    try {
      AthenaVaultMapSqlite map;
      auto locate= [&] (const std::string& uuid, AthenaVaultMapNode& node) {
        std::string error;
        if (!map.valid () && !map.open_read_only (vault->map_db, error))
          throw std::runtime_error (error);
        bool found= false;
        if (!map.get_node (uuid, node, found, error)) throw std::runtime_error (error);
        return found;
      };
      const QString root= QFileInfo (QString::fromStdString (vault->root.string ())).canonicalFilePath ();
      auto load= [&] (const QString& relative) {
        const QString file= QFileInfo (QDir (root).filePath (relative)).canonicalFilePath ();
        const QString local= QDir (root).relativeFilePath (file);
        if (root.isEmpty () || file.isEmpty () || QDir::isAbsolutePath (relative) ||
            local == ".." || local.startsWith ("../") || QDir::isAbsolutePath (local))
          throw std::runtime_error ("Transclusion source is missing or outside the vault");
        return vault_search_read_body (file);
      };
      job->result= collect_available_enunciations (bytes, path, locate, load, job->cancelled);
    }
    catch (const std::exception& error) { job->error= QString::fromUtf8 (error.what ()); }
    catch (...) { job->error= "Could not enumerate available enunciations."; }
    job->done.store (true, std::memory_order_release);
  }));
  timer->start ();
  query->setFocus ();
}

void QTMVaultAvailablePage::filter () {
  list->clear ();
  std::vector<std::pair<int, std::size_t>> matches;
  const QString tag= kind->currentData ().toString ();
  for (std::size_t i= 0; i<entries.size (); ++i) {
    const auto& entry= entries[i];
    if (!tag.isEmpty () && tag != entry.tag) continue;
    const int score= list_filter_score (entry.title + " " + entry.relative_path,
      query->text ().trimmed (), caseInsensitive->isChecked (), fuzzy->isChecked ());
    if (score >= 0) matches.emplace_back (-score, i);
  }
  std::stable_sort (matches.begin (), matches.end ());
  for (const auto& match: matches) {
    const auto& entry= entries[match.second];
    auto* item= new QListWidgetItem (entry.title + "\n" + entry.relative_path, list);
    item->setData (Qt::UserRole, static_cast<qulonglong> (match.second));
    item->setToolTip (entry.relative_path + "\n" + entry.upper);
  }
  if (list->count ()) list->setCurrentRow (0);
  emit completeChanged ();
}

void QTMVaultAvailablePage::showPreview () {
  auto* item= list->currentItem ();
  if (!item) { preview.setBody (tree (DOCUMENT, "")); emit completeChanged (); return; }
  const auto& entry= entries.at (item->data (Qt::UserRole).toULongLong ());
  tree body= scheme_to_tree (string (entry.source_body->data (), entry.source_body->size ()));
  tree range= athena_transclusion_source_range (body, from_qstring (entry.upper), from_qstring (entry.lower));
  previewTitle->setText (entry.relative_path);
  range= rebase_preview_images (range, head (url_system (
    from_qstring_utf8 (QDir (QString::fromStdString (context->root.string ())).filePath (entry.relative_path)))));
  preview.setBody (apply_vault_preferred_font_to_preview (range));
  display->setText (entry.title);
  emit completeChanged ();
}

bool QTMVaultAvailablePage::isComplete () const {
  return task && task->done.load (std::memory_order_acquire) && task->error.isEmpty () &&
    list->currentItem () && vault_context_is_current (context);
}

bool QTMVaultAvailablePage::validatePage () {
  if (!isComplete () || !selectionHandler) return false;
  const auto index= list->currentItem ()->data (Qt::UserRole).toULongLong ();
  const auto& entry= entries.at (index);
  selectionHandler ({entry.relative_path, entry.upper, entry.lower,
    display->text ().trimmed ().isEmpty () ? entry.title : display->text ().trimmed ()});
  return true;
}
