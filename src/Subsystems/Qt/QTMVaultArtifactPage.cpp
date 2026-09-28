/******************************************************************************
* MODULE     : QTMVaultArtifactPage.cpp
* DESCRIPTION: Shared artifact selector for vault link inserters
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMVaultArtifactPage.hpp"

#include "QTMVaultAnchorModel.hpp"
#include "QTMVaultLinkModel.hpp"
#include "QTMVaultPreviewBuilder.hpp"
#include "ATHENA/Data/new_buffer.hpp"
#include "ATHENA/Data/vault.hpp"
#include "convert.hpp"
#include "node_metadata.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"

#include <rapidfuzz/fuzz.hpp>

#include <QCheckBox>
#include <QAbstractButton>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QShowEvent>
#include <QSizePolicy>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <QWizard>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <unordered_set>

namespace fs= std::filesystem;

namespace {

constexpr int ArtifactRecordRole= Qt::UserRole + 41;

QString qstr (const std::string& value) {
  return QString::fromUtf8 (value.data (), (qsizetype) value.size ());
}

std::string stdstr (const string& value) {
  return std::string (as_charp (value), (size_t) N(value));
}

std::u32string codepoints (const QString& value) {
  QList<uint> ucs4= value.toUcs4 ();
  return std::u32string (ucs4.begin (), ucs4.end ());
}

QString normalized_search_text (QString value, bool caseInsensitive) {
  value= value.simplified ();
  return caseInsensitive ? value.toCaseFolded () : value;
}

QString artifact_kind (const AthenaArtifactRecord& record) {
  if (record.origin == "bold-text") return "Definition";
  QString type= qstr (record.type);
  if (type == "provable") return "Enunciation";
  if (type == "completion") return "Proof or solution";
  if (!type.isEmpty ()) {
    type[0]= type[0].toUpper ();
    return type;
  }
  return "Artifact";
}

url artifact_file (const AthenaArtifactRecord& record) {
  return vault_get_root () * url_unix (from_qstring (qstr (
    record.relative_path)));
}

bool live_buffer_for_file (url file, url& buffer) {
  file= concretize (file);
  array<url> buffers= get_all_buffers ();
  for (int i=0; i<N(buffers); i++) {
    url candidate= concretize (buffers[i]);
    if (candidate == file) {
      buffer= buffers[i];
      return true;
    }
  }
  return false;
}

struct ArtifactSource {
  url file;
  url buffer;
  tree document;
  tree body;
  bool live= false;
};

bool load_artifact_source (const AthenaArtifactRecord& record,
                           ArtifactSource& source, QString& error) {
  source= ArtifactSource ();
  source.file= artifact_file (record);
  source.live= live_buffer_for_file (source.file, source.buffer);
  try {
    if (source.live) {
      source.document= get_buffer_tree (source.buffer);
      source.body= extract (source.document, "body");
    }
    else {
      source.document= import_tree (source.file, "texmacs");
      if (source.document == "error" || is_func (source.document, _ERROR)) {
        error= "Could not load the artifact source document.";
        return false;
      }
      source.body= extract (source.document, "body");
      if (is_empty (source.body)) source.body= source.document;
    }
  }
  catch (...) {
    error= "Could not load the artifact source document.";
    return false;
  }
  if (!is_func (source.body, DOCUMENT)) {
    error= "The artifact source no longer has a document body.";
    return false;
  }
  return true;
}

bool pair_wraps_range (const TransclusionAnchorPair& pair,
                       path first, path last) {
  return path_less (pair.upperWhere, first) &&
         path_less (last, pair.lowerWhere);
}

int tightest_wrapping_pair (
  const std::vector<TransclusionAnchorPair>& pairs, path first, path last) {
  int best= -1;
  for (int i=0; i<(int) pairs.size (); i++) {
    if (!pair_wraps_range (pairs[i], first, last)) continue;
    if (best < 0 ||
        path_less (pairs[best].upperWhere, pairs[i].upperWhere) ||
        (pairs[i].upperWhere == pairs[best].upperWhere &&
         path_less (pairs[i].lowerWhere, pairs[best].lowerWhere)))
      best= i;
  }
  return best;
}


tree select_nested_paragraphs (tree value, path parent, int first, int last) {
  if (!is_nil (parent)) {
    int child= parent->item;
    if (!is_compound (value) || child < 0 || child >= N(value))
      return copy (value);
    tree result= copy (value);
    result[child]= select_nested_paragraphs (
      value[child], parent->next, first, last);
    return result;
  }
  if (!is_document (value)) return copy (value);
  tree selected (DOCUMENT);
  for (int i=first; i<=last && i<N(value); i++)
    if (i >= 0) selected << copy (value[i]);
  return selected;
}

tree build_paragraph_preview (tree body,
                              const AthenaArtifactParagraphLocation& location) {
  if (!location.nodes.empty ()) {
    tree selected (DOCUMENT);
    for (const auto& where: location.nodes) selected << copy (subtree (body, where));
    return selected;
  }
  if (is_nil (location.parent))
    return build_preview_from_anchor_range (
      body, path (location.first_child), path (location.last_child));
  int top= location.parent->item;
  if (top < 0 || top >= N(body)) return tree (DOCUMENT, "");
  tree block= select_nested_paragraphs (
    body[top], location.parent->next,
    location.first_child, location.last_child);
  return tree (DOCUMENT, compound ("marked", block));
}


bool resolve_enunciation (
  const AthenaArtifactRecord& record, const ArtifactSource& source,
  QTMVaultArtifactSelection& selection, QString& error) {
  std::vector<WikilinkAnchorEntry> anchors;
  collect_anchors (source.body, path (), anchors);
  std::vector<TransclusionAnchorPair> pairs=
    collect_transclusion_pairs (anchors);
  QString stem= qstr (record.anchor_stem);
  for (const TransclusionAnchorPair& pair: pairs)
    if (anchor_pair_key (pair.upper) == stem) {
      selection.upper_anchor= pair.upper;
      selection.lower_anchor= pair.lower;
      return true;
    }
  error= "The selected enunciation no longer has the anchors recorded in "
         "artifacts.db. Rebuild artifacts and try again.";
  return false;
}

bool resolve_paragraph (
  const AthenaArtifactRecord& record, ArtifactSource& source,
  QTMVaultArtifactSelection& selection, QString& error) {
  AthenaArtifactParagraphLocation location;
  std::string locateError;
  if (!athena_artifact_locate_paragraph (
        source.document, record, location, locateError)) {
    // Open buffers may contain unsaved changes absent from their document
    // wrapper; locate against the authoritative live body in that case.
    tree liveDocument (DOCUMENT);
    liveDocument << compound ("body", source.body);
    locateError.clear ();
    if (!athena_artifact_locate_paragraph (
          liveDocument, record, location, locateError)) {
      error= qstr (locateError) + ". Rebuild artifacts and try again.";
      return false;
    }
  }

  std::vector<WikilinkAnchorEntry> anchors;
  collect_anchors (source.body, path (), anchors);
  std::vector<TransclusionAnchorPair> pairs=
    collect_transclusion_pairs (anchors);
  path first= location.parent * location.first_child;
  path last= location.parent * location.last_child;
  int pairIndex= tightest_wrapping_pair (
    pairs, first, last);
  if (pairIndex >= 0) {
    selection.upper_anchor= pairs[pairIndex].upper;
    selection.lower_anchor= pairs[pairIndex].lower;
    return true;
  }

  error= "This legacy paragraph has no existing target. Upgrade the vault node model before linking it.";
  return false;
}

bool locate_artifact_preview (
  const AthenaArtifactRecord& record, tree& previewBody, QString& title,
  QString& error) {
  ArtifactSource source;
  if (!load_artifact_source (record, source, error)) return false;
  title= qstr (record.relative_path) + "  --  " + qstr (record.display_text);
  if (record.origin == "enunciation") {
    if (vault_get_node_model_version () >= 1) {
      path focus;
      std::string locate_error;
      tree document= source.document;
      if (!athena_artifact_locate_source (
            document, record, focus, locate_error)) {
        error= qstr (locate_error) + ". Rebuild artifacts and try again.";
        return false;
      }
      previewBody= build_preview_from_body (source.body, focus);
      return true;
    }
    QTMVaultArtifactSelection selection;
    if (!resolve_enunciation (record, source, selection, error)) return false;
    std::vector<WikilinkAnchorEntry> anchors;
    collect_anchors (source.body, path (), anchors);
    for (const TransclusionAnchorPair& pair:
         collect_transclusion_pairs (anchors))
      if (pair.upper == selection.upper_anchor &&
          pair.lower == selection.lower_anchor) {
        previewBody= build_preview_from_anchor_range (
          source.body, pair.upperWhere, pair.lowerWhere);
        return true;
      }
  }
  else if (record.origin == "bold-text") {
    AthenaArtifactParagraphLocation location;
    std::string locateError;
    tree document= source.document;
    if (!athena_artifact_locate_paragraph (
          document, record, location, locateError)) {
      error= qstr (locateError) + ". Rebuild artifacts and try again.";
      return false;
    }
    previewBody= build_paragraph_preview (source.body, location);
    return true;
  }
  error= "Unsupported artifact origin.";
  return false;
}

} // namespace

QTMVaultArtifactPage::QTMVaultArtifactPage (
  QTMVaultArtifactUsage usage2, const char* casePreference2,
  const char* fuzzyPreference2, QWidget* parent)
  : QWizardPage (parent), usage (usage2),
    casePreference (casePreference2), fuzzyPreference (fuzzyPreference2),
    preview (this), recordsLoaded (false), selectionAccepted (false) {
  setFinalPage (true);
  setTitle ("Select an artifact");
  setSubTitle (
    "Search the artifact index directly, preview a result, then insert it.");

  queryEdit= new QLineEdit (this);
  queryEdit->setPlaceholderText (
    "Search artifact names, types, and source files");
  caseInsensitiveCheck= new QCheckBox ("Case-insensitive", this);
  fuzzyCheck= new QCheckBox ("Fuzzy", this);
  caseInsensitiveCheck->setChecked (
    get_preference (casePreference, "off") == "on");
  fuzzyCheck->setChecked (
    get_preference (fuzzyPreference, "off") == "on");
  searchButton= new QPushButton ("&Search", this);
  statusLabel= new QLabel ("Loading artifacts...", this);
  resultList= new QListWidget (this);
  resultList->setAlternatingRowColors (true);
  resultList->setMinimumWidth (440);
  previewTitle= new QLabel ("Select an artifact to preview it.", this);
  previewTitle->setWordWrap (true);
  previewTitle->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Preferred);
  previewHost= new QWidget (this);
  previewHost->setMinimumHeight (360);
  previewHost->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Expanding);
  QVBoxLayout* previewLayout= new QVBoxLayout (previewHost);
  previewLayout->setContentsMargins (0, 0, 0, 0);

  QHBoxLayout* queryRow= new QHBoxLayout ();
  queryRow->addWidget (queryEdit, 1);
  queryRow->addWidget (caseInsensitiveCheck);
  queryRow->addWidget (fuzzyCheck);
  queryRow->addWidget (searchButton);

  QWidget* left= new QWidget (this);
  QVBoxLayout* leftLayout= new QVBoxLayout (left);
  leftLayout->setContentsMargins (0, 0, 0, 0);
  leftLayout->addLayout (queryRow);
  leftLayout->addWidget (statusLabel);
  leftLayout->addWidget (resultList, 1);

  QWidget* right= new QWidget (this);
  right->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Expanding);
  QVBoxLayout* rightLayout= new QVBoxLayout (right);
  rightLayout->setContentsMargins (0, 0, 0, 0);
  rightLayout->addWidget (previewTitle);
  rightLayout->addWidget (previewHost, 1);

  QSplitter* splitter= new QSplitter (Qt::Horizontal, this);
  splitter->addWidget (left);
  splitter->addWidget (right);
  splitter->setStretchFactor (0, 0);
  splitter->setStretchFactor (1, 1);
  splitter->setSizes (QList<int> () << 500 << 760);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->addWidget (splitter, 1);

  connect (searchButton, &QPushButton::clicked,
           this, [this] () { runSearch (); });
  connect (queryEdit, &QLineEdit::returnPressed,
           searchButton, &QPushButton::click);
  connect (caseInsensitiveCheck, &QCheckBox::toggled,
           this, [this] (bool enabled) {
             set_preference (casePreference, enabled ? "on" : "off");
             runSearch ();
           });
  connect (fuzzyCheck, &QCheckBox::toggled,
           this, [this] (bool enabled) {
             set_preference (fuzzyPreference, enabled ? "on" : "off");
             runSearch ();
           });
  connect (resultList, &QListWidget::currentItemChanged,
           this, [this] (QListWidgetItem*, QListWidgetItem*) {
             updatePreview ();
           });
  connect (resultList, &QListWidget::itemDoubleClicked,
           this, [this] (QListWidgetItem*) {
             if (wizard () != nullptr)
               wizard ()->button (QWizard::FinishButton)->click ();
           });
}

int
QTMVaultArtifactPage::nextId () const {
  return -1;
}

void
QTMVaultArtifactPage::setSelectionHandler (SelectionHandler handler) {
  selectionHandler= std::move (handler);
}

void
QTMVaultArtifactPage::initializePage () {
  QWizardPage::initializePage ();
  selectionAccepted= false;
  loadRecords ();
  runSearch ();
  queryEdit->setFocus ();
  QTimer::singleShot (0, this, [this] () {
    preview.ensureCreated (previewHost);
    updatePreview ();
    preview.refresh ();
  });
}

void
QTMVaultArtifactPage::showEvent (QShowEvent* event) {
  QWizardPage::showEvent (event);
  QTimer::singleShot (0, this, [this] () {
    preview.ensureCreated (previewHost);
    updatePreview ();
    preview.refresh ();
  });
}

void
QTMVaultArtifactPage::loadRecords () {
  if (recordsLoaded) return;
  recordsLoaded= true;
  std::string error;
  fs::path root (stdstr (concretize (vault_get_root ())));
  if (!athena_artifacts_query (root, records, error)) {
    records.clear ();
    statusLabel->setText ("Could not read artifacts.db: " + qstr (error));
  }
}

void
QTMVaultArtifactPage::runSearch () {
  resultList->clear ();
  if (!recordsLoaded) loadRecords ();
  QString query= normalized_search_text (
    queryEdit->text (), caseInsensitiveCheck->isChecked ());
  std::u32string query32= codepoints (query);
  bool fuzzy= fuzzyCheck->isChecked () && query32.size () >= 4;
  double cutoff= query32.size () <= 5 ? 75.0 : 80.0;
  std::optional<rapidfuzz::fuzz::CachedPartialRatio<char32_t>> scorer;
  if (fuzzy) scorer.emplace (query32);

  struct Match {
    int index;
    bool exact;
    double score;
  };
  std::vector<Match> matches;
  for (int i=0; i<(int) records.size (); i++) {
    const AthenaArtifactRecord& record= records[(size_t) i];
    QString haystack= artifact_kind (record) + " " +
      qstr (record.display_text) + " " + qstr (record.anchor_stem) + " " +
      qstr (record.relative_path);
    haystack= normalized_search_text (
      haystack, caseInsensitiveCheck->isChecked ());
    bool exact= query.isEmpty () || haystack.contains (query);
    double score= exact ? 100.0 : 0.0;
    if (!exact && scorer.has_value ())
      score= scorer->similarity (codepoints (haystack), cutoff);
    if (exact || score >= cutoff) matches.push_back ({i, exact, score});
  }
  std::sort (matches.begin (), matches.end (),
             [this] (const Match& a, const Match& b) {
               if (a.exact != b.exact) return a.exact;
               if (a.score != b.score) return a.score > b.score;
               const auto& ra= records[(size_t) a.index];
               const auto& rb= records[(size_t) b.index];
               if (ra.relative_path != rb.relative_path)
                 return ra.relative_path < rb.relative_path;
               return ra.document_order < rb.document_order;
             });

  for (const Match& match: matches) {
    const AthenaArtifactRecord& record= records[(size_t) match.index];
    QString label= artifact_kind (record) + ": " + qstr (record.display_text);
    QListWidgetItem* item= new QListWidgetItem (label, resultList);
    item->setData (ArtifactRecordRole, match.index);
    QString tip= qstr (record.relative_path);
    if (!match.exact) tip += "\nFuzzy score: " +
      QString::number (match.score, 'f', 1);
    item->setToolTip (tip);
  }
  statusLabel->setText (
    QString::number (matches.size ()) + " artifact(s) found.");
  if (resultList->count () > 0) resultList->setCurrentRow (0);
  else updatePreview ();
}

void
QTMVaultArtifactPage::updatePreview () {
  delete previewRequest.data ();
  QListWidgetItem* item= resultList->currentItem ();
  int index= item == nullptr ? -1 :
    item->data (ArtifactRecordRole).toInt ();
  if (index < 0 || index >= (int) records.size ()) {
    previewTitle->setText ("Select an artifact to preview it.");
    preview.ensureCreated (previewHost);
    preview.setBody (tree (DOCUMENT, ""));
    return;
  }
  const auto& record= records[(size_t) index];
  if (vault_get_node_model_version () >= 1) {
    QStringList ids;
    for (const auto& id: record.source_nodes) ids << qstr (id);
    if (ids.isEmpty ()) ids << qstr (record.source_uuid);
    previewTitle->setText ("Locating source...");
    previewRequest= vault_resolve_source_ids (this, ids,
      [this] (auto answer, QString error) {
        if (!error.isEmpty ()) { previewTitle->setText (error); return; }
        try {
          preview.ensureCreated (previewHost);
          preview.setBody (vault_source_preview (*answer));
          QStringList files;
          for (const auto& item: answer->items) {
            const auto file= qstr (item.candidates.front ().file);
            if (!files.contains (file)) files << file;
          }
          previewTitle->setText (files.join (", "));
        } catch (...) { previewTitle->setText ("Preview unavailable."); }
      });
    return;
  }
  tree body;
  QString title;
  QString error;
  if (!locate_artifact_preview (
        records[(size_t) index], body, title, error)) {
    previewTitle->setText (error);
    preview.ensureCreated (previewHost);
    preview.setBody (tree (DOCUMENT, ""));
    return;
  }
  previewTitle->setText (title);
  preview.ensureCreated (previewHost);
  preview.setBody (body);
}

bool
QTMVaultArtifactPage::resolveSelection (
  QTMVaultArtifactSelection& selection) {
  QListWidgetItem* item= resultList->currentItem ();
  int index= item == nullptr ? -1 :
    item->data (ArtifactRecordRole).toInt ();
  if (index < 0 || index >= (int) records.size ()) {
    QMessageBox::information (this, "Select an artifact",
                              "Select an artifact first.");
    return false;
  }
  const AthenaArtifactRecord& record= records[(size_t) index];
  if (vault_get_node_model_version () >= 1) {
    selection.display_text= qstr (record.display_text);
    selection.wikilink_uuid= qstr (record.source_uuid);
    if (record.origin == "enunciation") selection.source_uuids << qstr (record.source_uuid);
    else for (const auto& id: record.source_nodes) selection.source_uuids << qstr (id);
    if (usage == QTMVaultArtifactUsage::Transclusion && selection.source_uuids.isEmpty ()) {
      QMessageBox::warning (this, "Select an artifact",
        "The Artifact has no verified source UUID list. Rebuild artifacts and try again.");
      return false;
    }
    return true;
  }
  ArtifactSource source;
  QString error;
  if (!load_artifact_source (record, source, error)) {
    QMessageBox::critical (this, "Select an artifact", error);
    return false;
  }
  selection.relative_path= qstr (record.relative_path);
  selection.display_text= qstr (record.display_text);
  bool ok= (record.origin == "enunciation" ?
      resolve_enunciation (record, source, selection, error) :
      resolve_paragraph (record, source, selection, error));
  if (!ok) {
    if (!error.isEmpty ())
      QMessageBox::critical (this, "Select an artifact", error);
    return false;
  }
  return true;
}

bool
QTMVaultArtifactPage::validatePage () {
  if (selectionAccepted) return true;
  QTMVaultArtifactSelection selection;
  if (!resolveSelection (selection)) return false;
  if (selectionHandler && !selectionHandler (selection)) return false;
  selectionAccepted= true;
  return true;
}
