/******************************************************************************
* MODULE     : QTMVaultTransclusionWizard.cpp
* DESCRIPTION: Vault transclusion insertion wizard
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMVaultTransclusionWizard.hpp"
#include "ATHENA/Features/athena_features.hpp"
#include "QTMCompletingComboBox.hpp"
#if ATHENA_ENABLE_PERSON_SUBSYSTEM
#include "QTMPersonsExplorer.hpp"
#include "ATHENA/Data/person_names.hpp"
#endif
#include "QTMVaultAnchorModel.hpp"
#include "QTMVaultArtifactPage.hpp"
#include "QTMVaultLinkModel.hpp"
#include "QTMVaultPreviewBuilder.hpp"
#include "QTMVaultPreviewWidget.hpp"
#include "QTMVaultSearch.hpp"
#include "QTMVaultSearchWorker.hpp"
#include "drd_mode.hpp"
#include "namespaces.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"
#include "tree_search.hpp"
#include "vault.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QEvent>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QShortcut>
#include <QShowEvent>
#include <QSizePolicy>
#include <QSplitter>
#include <QTimer>
#include <QVariant>
#include <QWizard>
#include <QWizardPage>
#include <algorithm>
#include <set>
#include <vector>

namespace {

static constexpr const char* transclusion_search_case_pref=
  "vault transclusion inserter case insensitive search";
static constexpr const char* transclusion_search_fuzzy_pref=
  "vault transclusion inserter fuzzy search";

static void
preserveCheckboxLabel (QCheckBox* checkbox) {
  checkbox->setSizePolicy (QSizePolicy::Minimum, QSizePolicy::Preferred);
}

static void
configurePreviewTitle (QLabel* label) {
  label->setWordWrap (true);
  label->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Preferred);
}

static void
setBoundedSubTitle (QWizardPage* page, const QString& prefix,
                    const QString& value) {
  QString full= prefix + value;
  int windowWidth= page->window () == nullptr ? 0 : page->window ()->width ();
  int available= std::clamp (windowWidth - 160, 480, 900);
  QFontMetrics metrics (page->font ());
  int valueWidth= std::max (160, available - metrics.horizontalAdvance (prefix));
  page->setSubTitle (prefix + metrics.elidedText (
    value, Qt::ElideMiddle, valueWidth));
  page->setToolTip (full);
}

enum TransclusionWizardPageId {
  TransclusionModePageId= 10,
  TransclusionFilePageId= 11,
  TransclusionKindPageId= 12,
  TransclusionEnunciationPageId= 13,
  TransclusionUpperPageId= 14,
  TransclusionLowerPageId= 15,
  TransclusionSearchPageId= 16,
  TransclusionArtifactPageId= 17
};

class QTMVaultTransclusionWizard;

class TransclusionModePage : public QWizardPage {
public:
  TransclusionModePage (QWidget* parent= nullptr);
  int nextId () const override;

  QRadioButton* fileFirstRadio;
  QRadioButton* searchRadio;
  QRadioButton* artifactRadio;
};

class TransclusionFilePage : public QWizardPage {
public:
  TransclusionFilePage (QWidget* parent= nullptr);
  void initializePage () override;
  bool validatePage () override;
  bool eventFilter (QObject* watched, QEvent* event) override;

  void updateList ();
  void moveSelection (int delta);
  bool completeFromSelection ();

  QLineEdit*   searchEdit;
  QListWidget* fileList;
};

class TransclusionKindPage : public QWizardPage {
public:
  TransclusionKindPage (QWidget* parent= nullptr);
  int nextId () const override;

  QRadioButton* enunciationRadio;
  QRadioButton* arbitraryRadio;
};

class TransclusionEnunciationPage : public QWizardPage {
public:
  TransclusionEnunciationPage (QWidget* parent= nullptr);
  int nextId () const override;
  void initializePage () override;
  bool validatePage () override;
  bool eventFilter (QObject* watched, QEvent* event) override;
  void showEvent (QShowEvent* event) override;

  void updateList ();
  void updatePreview ();
  bool acceptCurrentPair ();

  QLineEdit*   searchEdit;
  QCheckBox*   caseInsensitiveCheck;
  QCheckBox*   fuzzyCheck;
  QListWidget* pairList;
  QLabel*      previewTitle;
  QWidget*     previewHost;
  WikilinkPreview preview;
  tree fileBody;
  std::vector<TransclusionAnchorPair> pairs;
};

class TransclusionUpperPage : public QWizardPage {
public:
  TransclusionUpperPage (QWidget* parent= nullptr);
  int nextId () const override;
  void initializePage () override;
  bool validatePage () override;
  bool eventFilter (QObject* watched, QEvent* event) override;
  void showEvent (QShowEvent* event) override;

  void updateList ();
  void updatePreview ();

  QLineEdit*   searchEdit;
  QCheckBox*   caseInsensitiveCheck;
  QCheckBox*   fuzzyCheck;
  QListWidget* anchorList;
  QLabel*      previewTitle;
  QWidget*     previewHost;
  WikilinkPreview preview;
  tree fileBody;
  std::vector<WikilinkAnchorEntry> anchors;
};

class TransclusionLowerPage : public QWizardPage {
public:
  TransclusionLowerPage (QWidget* parent= nullptr);
  int nextId () const override;
  void initializePage () override;
  bool validatePage () override;
  bool eventFilter (QObject* watched, QEvent* event) override;
  void showEvent (QShowEvent* event) override;

  void updateList ();
  void updatePreview ();

  QLineEdit*   searchEdit;
  QCheckBox*   caseInsensitiveCheck;
  QCheckBox*   fuzzyCheck;
  QListWidget* anchorList;
  QLabel*      previewTitle;
  QWidget*     previewHost;
  WikilinkPreview preview;
  tree fileBody;
  std::vector<WikilinkAnchorEntry> anchors;
};

class TransclusionSearchPage : public QWizardPage {
public:
  TransclusionSearchPage (QWidget* parent= nullptr);
  int nextId () const override;
  void initializePage () override;
  bool validatePage () override;
  void showEvent (QShowEvent* event) override;

  void refreshNamespaces ();
  QString selectedNamespace () const;
  QString selectedEnunciation () const;
#if ATHENA_ENABLE_PERSON_SUBSYSTEM
  QString selectedPerson () const;
#endif
  bool caseInsensitiveSearch () const;
  bool fuzzySearch () const;
  void startSearch ();
  ~TransclusionSearchPage () override { if (searchTask) searchTask->cancelled= true; }
  void cleanupPage () override {
    ++searchGeneration;
    if (searchTask) searchTask->cancelled= true;
    stopButton->setEnabled (false);
    searchButton->setEnabled (true);
  }
  static int searchFile (tree body, url u, const tree& query,
                         const VaultSearchOptions& options,
                         std::vector<TransclusionSearchResult>& hits);
  void addResult (const TransclusionSearchResult& result);
  void updatePreview (QListWidgetItem* current);
  bool acceptCurrentResult ();

  QLineEdit*   queryEdit;
  QComboBox*   namespaceCombo;
  QComboBox*   enunciationCombo;
#if ATHENA_ENABLE_PERSON_SUBSYSTEM
  QComboBox*   personCombo;
#endif
  QCheckBox*   caseInsensitiveCheck;
  QCheckBox*   fuzzyCheck;
  QPushButton* searchButton;
  QPushButton* stopButton;
  QLabel*      statusLabel;
  QProgressBar* progress;
  QListWidget* resultList;
  QLabel*      previewTitle;
  QWidget*     previewHost;
  WikilinkPreview preview;
  std::vector<TransclusionSearchResult> results;
  std::shared_ptr<VaultSearchControl> searchTask;
  unsigned long searchGeneration= 0;
};

class QTMVaultTransclusionWizard : public QWizard {
public:
  QTMVaultTransclusionWizard (QWidget* parent= nullptr);
  void showEvent (QShowEvent* event) override;

  tree getResult () const;
  void setResult (const QString& relPath, const QString& anchorBegin,
                  const QString& anchorEnd, const QString& fileHint,
                  const QString& anchorHint);
  bool selectFileFromPage ();

  std::vector<WikilinkFileEntry> files;
  QString selectedRelPath;
  url     selectedFileUrl;
  QString fileHint;
  QString selectedAnchorBegin;
  QString selectedAnchorEnd;
  QString anchorHint;
  int     selectedUpperIndex;
  QString selectedUpperAnchor;
  path    selectedUpperWhere;
  bool    filesLoaded;
  bool    filesLoadScheduled;
  bool    resultAccepted;

  TransclusionModePage* modePage;
  TransclusionFilePage* filePage;
  TransclusionKindPage* kindPage;
  TransclusionEnunciationPage* enunciationPage;
  TransclusionUpperPage* upperPage;
  TransclusionLowerPage* lowerPage;
  TransclusionSearchPage* searchPage;
  QTMVaultArtifactPage* artifactPage;

  void loadFiles ();
  void scheduleLoadFiles ();
};

TransclusionModePage::TransclusionModePage (QWidget* parent)
  : QWizardPage (parent) {
  setTitle ("Insert transclusion");
  setSubTitle ("Choose how to locate the material to transclude.");

  fileFirstRadio= new QRadioButton ("Locate a file first", this);
  searchRadio= new QRadioButton ("Locate by search", this);
  artifactRadio= new QRadioButton ("Select an artifact", this);
  fileFirstRadio->setChecked (true);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->addWidget (fileFirstRadio);
  layout->addWidget (searchRadio);
  layout->addWidget (artifactRadio);
  layout->addStretch ();
}

int
TransclusionModePage::nextId () const {
  if (artifactRadio->isChecked ()) return TransclusionArtifactPageId;
  return searchRadio->isChecked () ? TransclusionSearchPageId :
    TransclusionFilePageId;
}

TransclusionFilePage::TransclusionFilePage (QWidget* parent)
  : QWizardPage (parent) {
  setTitle ("Select a file");
  setSubTitle ("Type to filter vault files; Tab or Enter completes the "
               "selected result, and Enter again continues.");

  searchEdit= new QLineEdit (this);
  searchEdit->setPlaceholderText ("Search .ath and .tm files");
  fileList= new QListWidget (this);
  fileList->setAlternatingRowColors (true);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->addWidget (searchEdit);
  layout->addWidget (fileList, 1);

  searchEdit->installEventFilter (this);
  fileList->installEventFilter (this);

  connect (searchEdit, &QLineEdit::textChanged,
           this, [this] (const QString&) { updateList (); });
  connect (fileList, &QListWidget::itemDoubleClicked,
           this, [this] (QListWidgetItem*) { wizard ()->next (); });
}

void
TransclusionFilePage::initializePage () {
  QWizardPage::initializePage ();
  static_cast<QTMVaultTransclusionWizard*> (wizard ())->scheduleLoadFiles ();
  updateList ();
  searchEdit->setFocus ();
}

void
TransclusionFilePage::updateList () {
  QTMVaultTransclusionWizard* w=
    static_cast<QTMVaultTransclusionWizard*> (wizard ());
  fileList->clear ();
  if (!w->filesLoaded) {
    QListWidgetItem* item= new QListWidgetItem ("Loading vault files...");
    item->setFlags (Qt::NoItemFlags);
    fileList->addItem (item);
    searchEdit->setEnabled (false);
    return;
  }
  searchEdit->setEnabled (true);
  string query= from_qstring (searchEdit->text ().trimmed ());

  std::vector<std::pair<int,int> > matches;
  for (int i=0; i<(int) w->files.size (); i++) {
    int score= fuzzy_file_score (w->files[i], query);
    if (score >= 0) matches.push_back (std::make_pair (-score, i));
  }
  std::sort (matches.begin (), matches.end (),
             [&] (const std::pair<int,int>& a,
                  const std::pair<int,int>& b) {
               const WikilinkFileEntry& fa= w->files[a.second];
               const WikilinkFileEntry& fb= w->files[b.second];
               if (fa.isCurrent != fb.isCurrent) return fa.isCurrent;
               if (a.first != b.first) return a.first < b.first;
               if (fa.mtime != fb.mtime) return fa.mtime > fb.mtime;
               return fa.relPath < fb.relPath;
             });

  const int limit= 200;
  int count= 0;
  for (auto m: matches) {
    const WikilinkFileEntry& e= w->files[m.second];
    QListWidgetItem* item= new QListWidgetItem (e.relPath);
    item->setData (WikilinkPayloadRole, e.relPath);
    item->setData (WikilinkIndexRole, m.second);
    item->setData (WikilinkCompletionRole, strip_known_extension (e.relPath));
    fileList->addItem (item);
    if (++count >= limit) break;
  }
  if (fileList->count () > 0) fileList->setCurrentRow (0);
}

void
TransclusionFilePage::moveSelection (int delta) {
  int count= fileList->count ();
  if (count <= 0) return;
  int row= fileList->currentRow ();
  if (row < 0) row= delta > 0 ? -1 : 0;
  row= (row + delta + count) % count;
  fileList->setCurrentRow (row);
}

bool
TransclusionFilePage::completeFromSelection () {
  return qtm_commit_list_completion (searchEdit, fileList,
                                     WikilinkCompletionRole);
}

bool
TransclusionFilePage::eventFilter (QObject* watched, QEvent* event) {
  if ((watched == searchEdit || watched == fileList) &&
      event->type () == QEvent::KeyPress) {
    QKeyEvent* key= static_cast<QKeyEvent*> (event);
    if (key->key () == Qt::Key_Up || key->key () == Qt::Key_Down) {
      moveSelection (key->key () == Qt::Key_Up ? -1 : 1);
      return true;
    }
    if ((key->key () == Qt::Key_Return || key->key () == Qt::Key_Enter) &&
        watched == searchEdit && completeFromSelection ())
      return true;
    if (key->key () == Qt::Key_Return || key->key () == Qt::Key_Enter) {
      wizard ()->next ();
      return true;
    }
    if (key->key () == Qt::Key_Tab && watched == searchEdit &&
        !(key->modifiers () & Qt::ShiftModifier) &&
        completeFromSelection ())
      return true;
  }
  return QWizardPage::eventFilter (watched, event);
}

bool
TransclusionFilePage::validatePage () {
  QTMVaultTransclusionWizard* w=
    static_cast<QTMVaultTransclusionWizard*> (wizard ());
  if (!w->filesLoaded) {
    QMessageBox::information (this, "Insert transclusion",
                              "Vault files are still loading.");
    return false;
  }
  return w->selectFileFromPage ();
}

TransclusionKindPage::TransclusionKindPage (QWidget* parent)
  : QWizardPage (parent) {
  setTitle ("Choose transclusion bounds");
  setSubTitle ("Choose a complete enunciation or arbitrary anchor bounds.");

  enunciationRadio= new QRadioButton ("Transclude an enunciation", this);
  arbitraryRadio= new QRadioButton ("Transclude between arbitrary anchors", this);
  enunciationRadio->setChecked (true);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->addWidget (enunciationRadio);
  layout->addWidget (arbitraryRadio);
  layout->addStretch ();
}

int
TransclusionKindPage::nextId () const {
  return arbitraryRadio->isChecked () ? TransclusionUpperPageId :
    TransclusionEnunciationPageId;
}

TransclusionEnunciationPage::TransclusionEnunciationPage (QWidget* parent)
  : QWizardPage (parent) {
  setFinalPage (true);
  setTitle ("Choose an enunciation");
  setSubTitle ("Only { anchors with a matching } anchor are listed.");

  searchEdit= new QLineEdit (this);
  searchEdit->setPlaceholderText ("Filter enunciation anchors");
  caseInsensitiveCheck= new QCheckBox ("Case-insensitive", this);
  fuzzyCheck= new QCheckBox ("Fuzzy", this);
  preserveCheckboxLabel (caseInsensitiveCheck);
  preserveCheckboxLabel (fuzzyCheck);
  caseInsensitiveCheck->setChecked (
    get_preference (transclusion_search_case_pref, "off") == "on");
  fuzzyCheck->setChecked (
    get_preference (transclusion_search_fuzzy_pref, "off") == "on");
  pairList= new QListWidget (this);
  pairList->setAlternatingRowColors (true);
  pairList->setMinimumWidth (500);
  previewTitle= new QLabel ("Select an enunciation to preview it.", this);
  configurePreviewTitle (previewTitle);
  previewHost= new QWidget (this);
  previewHost->setMinimumHeight (360);
  previewHost->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Expanding);
  QVBoxLayout* previewHostLayout= new QVBoxLayout (previewHost);
  previewHostLayout->setContentsMargins (0, 0, 0, 0);

  QWidget* left= new QWidget (this);
  QVBoxLayout* leftLayout= new QVBoxLayout (left);
  leftLayout->setContentsMargins (0, 0, 0, 0);
  leftLayout->addWidget (new QLabel ("Anchored ranges:", this));
  leftLayout->addWidget (searchEdit);
  QHBoxLayout* filters= new QHBoxLayout ();
  filters->addWidget (caseInsensitiveCheck);
  filters->addWidget (fuzzyCheck);
  filters->addStretch ();
  leftLayout->addLayout (filters);
  leftLayout->addWidget (pairList, 1);

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
  splitter->setSizes (QList<int> () << 520 << 800);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->addWidget (splitter, 1);

  searchEdit->installEventFilter (this);
  pairList->installEventFilter (this);
  connect (searchEdit, &QLineEdit::textChanged,
           this, [this] (const QString&) { updateList (); });
  connect (caseInsensitiveCheck, &QCheckBox::toggled,
           this, [this] (bool enabled) {
             set_preference (transclusion_search_case_pref,
                             enabled ? "on" : "off");
             updateList ();
           });
  connect (fuzzyCheck, &QCheckBox::toggled,
           this, [this] (bool enabled) {
             set_preference (transclusion_search_fuzzy_pref,
                             enabled ? "on" : "off");
             updateList ();
           });
  connect (pairList, &QListWidget::currentItemChanged,
           this, [this] (QListWidgetItem*, QListWidgetItem*) {
             updatePreview ();
           });
  connect (pairList, &QListWidget::itemDoubleClicked,
           this, [this] (QListWidgetItem*) {
             if (acceptCurrentPair ()) {
               QTMVaultTransclusionWizard* w=
                 static_cast<QTMVaultTransclusionWizard*> (wizard ());
               QTimer::singleShot (0, w, [w] () { w->accept (); });
             }
           });
}

int
TransclusionEnunciationPage::nextId () const {
  return -1;
}

void
TransclusionEnunciationPage::initializePage () {
  QWizardPage::initializePage ();
  QTMVaultTransclusionWizard* w=
    static_cast<QTMVaultTransclusionWizard*> (wizard ());
  setBoundedSubTitle (this, "Target file: ", w->selectedRelPath);
  pairs.clear ();
  fileBody= tree (DOCUMENT, "");
  try {
    fileBody= import_body_for_preview (w->selectedFileUrl);
    std::vector<WikilinkAnchorEntry> anchors;
    collect_anchors (fileBody, path (), anchors);
    pairs= collect_transclusion_pairs (anchors);
  }
  catch (...) {
    fileBody= tree (DOCUMENT, "Preview unavailable.");
  }
  updateList ();
  QTimer::singleShot (0, this, [this] () {
    preview.ensureCreated (previewHost);
    updatePreview ();
    preview.refresh ();
  });
  QTimer::singleShot (120, this, [this] () {
    preview.ensureCreated (previewHost);
    updatePreview ();
    preview.refresh ();
  });
  searchEdit->setFocus ();
}

void
TransclusionEnunciationPage::showEvent (QShowEvent* event) {
  QWizardPage::showEvent (event);
  QTimer::singleShot (0, this, [this] () {
    preview.ensureCreated (previewHost);
    updatePreview ();
    preview.refresh ();
  });
}

void
TransclusionEnunciationPage::updateList () {
  pairList->clear ();
  QString query= searchEdit->text ().trimmed ();
  std::vector<std::pair<int,int> > matches;
  for (int i=0; i<(int) pairs.size (); i++) {
    QString text= anchor_pair_key (pairs[i].upper);
    int score= list_filter_score (text, query,
                                  caseInsensitiveCheck->isChecked (),
                                  fuzzyCheck->isChecked ());
    if (score >= 0) matches.push_back (std::make_pair (-score, i));
  }
  std::sort (matches.begin (), matches.end (),
             [&] (const std::pair<int,int>& a,
                  const std::pair<int,int>& b) {
               if (a.first != b.first) return a.first < b.first;
               return pairs[a.second].upper < pairs[b.second].upper;
             });
  for (auto m: matches) {
    QListWidgetItem* item= new QListWidgetItem (
      anchor_pair_key (pairs[m.second].upper));
    item->setData (WikilinkIndexRole, m.second);
    pairList->addItem (item);
  }
  if (pairList->count () > 0) pairList->setCurrentRow (0);
  updatePreview ();
}

void
TransclusionEnunciationPage::updatePreview () {
  QTMVaultTransclusionWizard* w=
    static_cast<QTMVaultTransclusionWizard*> (wizard ());
  QListWidgetItem* item= pairList->currentItem ();
  int index= item == nullptr ? -1 : item->data (WikilinkIndexRole).toInt ();
  if (index < 0 || index >= (int) pairs.size ()) {
    previewTitle->setText ("Select an enunciation to preview it.");
    preview.ensureCreated (previewHost);
    preview.setBody (tree (DOCUMENT, ""));
    return;
  }
  const TransclusionAnchorPair& pair= pairs[index];
  previewTitle->setText (w->selectedRelPath + "  --  " + pair.upper);
  preview.ensureCreated (previewHost);
  preview.setBody (build_preview_from_anchor_range (
    fileBody, pair.upperWhere, pair.lowerWhere));
}

bool
TransclusionEnunciationPage::acceptCurrentPair () {
  QTMVaultTransclusionWizard* w=
    static_cast<QTMVaultTransclusionWizard*> (wizard ());
  QListWidgetItem* item= pairList->currentItem ();
  if (item == nullptr) {
    QMessageBox::information (this, "Insert transclusion",
                              "Select an enunciation first.");
    return false;
  }
  int index= item->data (WikilinkIndexRole).toInt ();
  if (index < 0 || index >= (int) pairs.size ()) return false;
  const TransclusionAnchorPair& pair= pairs[index];
  w->setResult (w->selectedRelPath, pair.upper, pair.lower, w->fileHint,
                pair.upper);
  return true;
}

bool
TransclusionEnunciationPage::eventFilter (QObject* watched, QEvent* event) {
  if ((watched == searchEdit || watched == pairList) &&
      event->type () == QEvent::KeyPress) {
    QKeyEvent* key= static_cast<QKeyEvent*> (event);
    if (key->key () == Qt::Key_Up || key->key () == Qt::Key_Down) {
      int count= pairList->count ();
      if (count <= 0) return true;
      int row= pairList->currentRow ();
      if (row < 0) row= key->key () == Qt::Key_Down ? -1 : 0;
      row= (row + (key->key () == Qt::Key_Up ? -1 : 1) + count) % count;
      pairList->setCurrentRow (row);
      return true;
    }
  }
  return QWizardPage::eventFilter (watched, event);
}

bool
TransclusionEnunciationPage::validatePage () {
  QTMVaultTransclusionWizard* w=
    static_cast<QTMVaultTransclusionWizard*> (wizard ());
  if (w->resultAccepted) return true;
  return acceptCurrentPair ();
}

TransclusionUpperPage::TransclusionUpperPage (QWidget* parent)
  : QWizardPage (parent) {
  setTitle ("Choose upper bound");
  setSubTitle ("Choose the first anchor in the transcluded range.");

  searchEdit= new QLineEdit (this);
  searchEdit->setPlaceholderText ("Filter anchors");
  caseInsensitiveCheck= new QCheckBox ("Case-insensitive", this);
  fuzzyCheck= new QCheckBox ("Fuzzy", this);
  preserveCheckboxLabel (caseInsensitiveCheck);
  preserveCheckboxLabel (fuzzyCheck);
  caseInsensitiveCheck->setChecked (
    get_preference (transclusion_search_case_pref, "off") == "on");
  fuzzyCheck->setChecked (
    get_preference (transclusion_search_fuzzy_pref, "off") == "on");
  anchorList= new QListWidget (this);
  anchorList->setAlternatingRowColors (true);
  anchorList->setMinimumWidth (500);
  previewTitle= new QLabel ("Select an anchor to preview it.", this);
  configurePreviewTitle (previewTitle);
  previewHost= new QWidget (this);
  previewHost->setMinimumHeight (360);
  previewHost->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Expanding);
  QVBoxLayout* previewHostLayout= new QVBoxLayout (previewHost);
  previewHostLayout->setContentsMargins (0, 0, 0, 0);

  QWidget* left= new QWidget (this);
  QVBoxLayout* leftLayout= new QVBoxLayout (left);
  leftLayout->setContentsMargins (0, 0, 0, 0);
  leftLayout->addWidget (new QLabel ("Upper bound:", this));
  leftLayout->addWidget (searchEdit);
  QHBoxLayout* filters= new QHBoxLayout ();
  filters->addWidget (caseInsensitiveCheck);
  filters->addWidget (fuzzyCheck);
  filters->addStretch ();
  leftLayout->addLayout (filters);
  leftLayout->addWidget (anchorList, 1);

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
  splitter->setSizes (QList<int> () << 520 << 800);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->addWidget (splitter, 1);

  searchEdit->installEventFilter (this);
  anchorList->installEventFilter (this);
  connect (searchEdit, &QLineEdit::textChanged,
           this, [this] (const QString&) { updateList (); });
  connect (caseInsensitiveCheck, &QCheckBox::toggled,
           this, [this] (bool enabled) {
             set_preference (transclusion_search_case_pref,
                             enabled ? "on" : "off");
             updateList ();
           });
  connect (fuzzyCheck, &QCheckBox::toggled,
           this, [this] (bool enabled) {
             set_preference (transclusion_search_fuzzy_pref,
                             enabled ? "on" : "off");
             updateList ();
           });
  connect (anchorList, &QListWidget::currentItemChanged,
           this, [this] (QListWidgetItem*, QListWidgetItem*) {
             updatePreview ();
           });
  connect (anchorList, &QListWidget::itemDoubleClicked,
           this, [this] (QListWidgetItem*) { wizard ()->next (); });
}

int
TransclusionUpperPage::nextId () const {
  return TransclusionLowerPageId;
}

void
TransclusionUpperPage::initializePage () {
  QWizardPage::initializePage ();
  QTMVaultTransclusionWizard* w=
    static_cast<QTMVaultTransclusionWizard*> (wizard ());
  setBoundedSubTitle (this, "Target file: ", w->selectedRelPath);
  anchors.clear ();
  fileBody= tree (DOCUMENT, "");
  try {
    fileBody= import_body_for_preview (w->selectedFileUrl);
    collect_anchors (fileBody, path (), anchors);
  }
  catch (...) {
    fileBody= tree (DOCUMENT, "Preview unavailable.");
  }
  updateList ();
  QTimer::singleShot (0, this, [this] () {
    preview.ensureCreated (previewHost);
    updatePreview ();
    preview.refresh ();
  });
  searchEdit->setFocus ();
}

void
TransclusionUpperPage::showEvent (QShowEvent* event) {
  QWizardPage::showEvent (event);
  QTimer::singleShot (0, this, [this] () {
    preview.ensureCreated (previewHost);
    updatePreview ();
    preview.refresh ();
  });
}

void
TransclusionUpperPage::updateList () {
  anchorList->clear ();
  QString query= searchEdit->text ().trimmed ();
  std::vector<std::pair<int,int> > matches;
  for (int i=0; i<(int) anchors.size (); i++) {
    int score= list_filter_score (anchors[i].anchor, query,
                                  caseInsensitiveCheck->isChecked (),
                                  fuzzyCheck->isChecked ());
    if (score >= 0) matches.push_back (std::make_pair (-score, i));
  }
  std::sort (matches.begin (), matches.end (),
             [&] (const std::pair<int,int>& a,
                  const std::pair<int,int>& b) {
               if (a.first != b.first) return a.first < b.first;
               return anchors[a.second].anchor < anchors[b.second].anchor;
             });
  for (auto m: matches) {
    QListWidgetItem* item= new QListWidgetItem (anchors[m.second].anchor);
    item->setData (WikilinkIndexRole, m.second);
    anchorList->addItem (item);
  }
  if (anchorList->count () > 0) anchorList->setCurrentRow (0);
  updatePreview ();
}

void
TransclusionUpperPage::updatePreview () {
  QTMVaultTransclusionWizard* w=
    static_cast<QTMVaultTransclusionWizard*> (wizard ());
  QListWidgetItem* item= anchorList->currentItem ();
  int index= item == nullptr ? -1 : item->data (WikilinkIndexRole).toInt ();
  if (index < 0 || index >= (int) anchors.size ()) {
    previewTitle->setText ("Select an anchor to preview it.");
    preview.ensureCreated (previewHost);
    preview.setBody (tree (DOCUMENT, ""));
    return;
  }
  previewTitle->setText (w->selectedRelPath + "  --  " +
                         anchors[index].anchor);
  preview.ensureCreated (previewHost);
  preview.setBody (build_preview_from_body (fileBody, anchors[index].where,
                                            nullptr, nullptr));
}

bool
TransclusionUpperPage::eventFilter (QObject* watched, QEvent* event) {
  if ((watched == searchEdit || watched == anchorList) &&
      event->type () == QEvent::KeyPress) {
    QKeyEvent* key= static_cast<QKeyEvent*> (event);
    if (key->key () == Qt::Key_Up || key->key () == Qt::Key_Down) {
      int count= anchorList->count ();
      if (count <= 0) return true;
      int row= anchorList->currentRow ();
      if (row < 0) row= key->key () == Qt::Key_Down ? -1 : 0;
      row= (row + (key->key () == Qt::Key_Up ? -1 : 1) + count) % count;
      anchorList->setCurrentRow (row);
      return true;
    }
  }
  return QWizardPage::eventFilter (watched, event);
}

bool
TransclusionUpperPage::validatePage () {
  QTMVaultTransclusionWizard* w=
    static_cast<QTMVaultTransclusionWizard*> (wizard ());
  QListWidgetItem* item= anchorList->currentItem ();
  if (item == nullptr) {
    QMessageBox::information (this, "Insert transclusion",
                              "Select an upper bound first.");
    return false;
  }
  int index= item->data (WikilinkIndexRole).toInt ();
  if (index < 0 || index >= (int) anchors.size ()) return false;
  w->selectedUpperIndex= index;
  w->selectedUpperAnchor= anchors[index].anchor;
  w->selectedUpperWhere= anchors[index].where;
  return true;
}

TransclusionLowerPage::TransclusionLowerPage (QWidget* parent)
  : QWizardPage (parent) {
  setFinalPage (true);
  setTitle ("Choose lower bound");
  setSubTitle ("Choose the final anchor in the transcluded range.");

  searchEdit= new QLineEdit (this);
  searchEdit->setPlaceholderText ("Filter anchors below the upper bound");
  caseInsensitiveCheck= new QCheckBox ("Case-insensitive", this);
  fuzzyCheck= new QCheckBox ("Fuzzy", this);
  preserveCheckboxLabel (caseInsensitiveCheck);
  preserveCheckboxLabel (fuzzyCheck);
  caseInsensitiveCheck->setChecked (
    get_preference (transclusion_search_case_pref, "off") == "on");
  fuzzyCheck->setChecked (
    get_preference (transclusion_search_fuzzy_pref, "off") == "on");
  anchorList= new QListWidget (this);
  anchorList->setAlternatingRowColors (true);
  anchorList->setMinimumWidth (500);
  previewTitle= new QLabel ("Select a lower bound to preview the range.", this);
  configurePreviewTitle (previewTitle);
  previewHost= new QWidget (this);
  previewHost->setMinimumHeight (360);
  previewHost->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Expanding);
  QVBoxLayout* previewHostLayout= new QVBoxLayout (previewHost);
  previewHostLayout->setContentsMargins (0, 0, 0, 0);

  QWidget* left= new QWidget (this);
  QVBoxLayout* leftLayout= new QVBoxLayout (left);
  leftLayout->setContentsMargins (0, 0, 0, 0);
  leftLayout->addWidget (new QLabel ("Lower bound:", this));
  leftLayout->addWidget (searchEdit);
  QHBoxLayout* filters= new QHBoxLayout ();
  filters->addWidget (caseInsensitiveCheck);
  filters->addWidget (fuzzyCheck);
  filters->addStretch ();
  leftLayout->addLayout (filters);
  leftLayout->addWidget (anchorList, 1);

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
  splitter->setSizes (QList<int> () << 520 << 800);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->addWidget (splitter, 1);

  searchEdit->installEventFilter (this);
  anchorList->installEventFilter (this);
  connect (searchEdit, &QLineEdit::textChanged,
           this, [this] (const QString&) { updateList (); });
  connect (caseInsensitiveCheck, &QCheckBox::toggled,
           this, [this] (bool enabled) {
             set_preference (transclusion_search_case_pref,
                             enabled ? "on" : "off");
             updateList ();
           });
  connect (fuzzyCheck, &QCheckBox::toggled,
           this, [this] (bool enabled) {
             set_preference (transclusion_search_fuzzy_pref,
                             enabled ? "on" : "off");
             updateList ();
           });
  connect (anchorList, &QListWidget::currentItemChanged,
           this, [this] (QListWidgetItem*, QListWidgetItem*) {
             updatePreview ();
           });
  connect (anchorList, &QListWidget::itemDoubleClicked,
           this, [this] (QListWidgetItem*) {
             if (validatePage ()) {
               QTMVaultTransclusionWizard* w=
                 static_cast<QTMVaultTransclusionWizard*> (wizard ());
               QTimer::singleShot (0, w, [w] () { w->accept (); });
             }
           });
}

int
TransclusionLowerPage::nextId () const {
  return -1;
}

void
TransclusionLowerPage::initializePage () {
  QWizardPage::initializePage ();
  QTMVaultTransclusionWizard* w=
    static_cast<QTMVaultTransclusionWizard*> (wizard ());
  setBoundedSubTitle (this, "Upper bound: ", w->selectedUpperAnchor);
  anchors.clear ();
  fileBody= tree (DOCUMENT, "");
  try {
    fileBody= import_body_for_preview (w->selectedFileUrl);
    std::vector<WikilinkAnchorEntry> all;
    collect_anchors (fileBody, path (), all);
    for (int i=0; i<(int) all.size (); i++)
      if (path_less (w->selectedUpperWhere, all[i].where))
        anchors.push_back (all[i]);
  }
  catch (...) {
    fileBody= tree (DOCUMENT, "Preview unavailable.");
  }
  updateList ();
  QTimer::singleShot (0, this, [this] () {
    preview.ensureCreated (previewHost);
    updatePreview ();
    preview.refresh ();
  });
  searchEdit->setFocus ();
}

void
TransclusionLowerPage::showEvent (QShowEvent* event) {
  QWizardPage::showEvent (event);
  QTimer::singleShot (0, this, [this] () {
    preview.ensureCreated (previewHost);
    updatePreview ();
    preview.refresh ();
  });
}

void
TransclusionLowerPage::updateList () {
  anchorList->clear ();
  QString query= searchEdit->text ().trimmed ();
  std::vector<std::pair<int,int> > matches;
  for (int i=0; i<(int) anchors.size (); i++) {
    int score= list_filter_score (anchors[i].anchor, query,
                                  caseInsensitiveCheck->isChecked (),
                                  fuzzyCheck->isChecked ());
    if (score >= 0) matches.push_back (std::make_pair (-score, i));
  }
  std::sort (matches.begin (), matches.end (),
             [&] (const std::pair<int,int>& a,
                  const std::pair<int,int>& b) {
               if (a.first != b.first) return a.first < b.first;
               return anchors[a.second].anchor < anchors[b.second].anchor;
             });
  for (auto m: matches) {
    QListWidgetItem* item= new QListWidgetItem (anchors[m.second].anchor);
    item->setData (WikilinkIndexRole, m.second);
    anchorList->addItem (item);
  }
  if (anchorList->count () > 0) anchorList->setCurrentRow (0);
  updatePreview ();
}

void
TransclusionLowerPage::updatePreview () {
  QTMVaultTransclusionWizard* w=
    static_cast<QTMVaultTransclusionWizard*> (wizard ());
  QListWidgetItem* item= anchorList->currentItem ();
  int index= item == nullptr ? -1 : item->data (WikilinkIndexRole).toInt ();
  if (index < 0 || index >= (int) anchors.size ()) {
    previewTitle->setText ("Select a lower bound to preview the range.");
    preview.ensureCreated (previewHost);
    preview.setBody (tree (DOCUMENT, ""));
    return;
  }
  previewTitle->setText (w->selectedUpperAnchor + "  ...  " +
                         anchors[index].anchor);
  preview.ensureCreated (previewHost);
  preview.setBody (build_preview_from_anchor_range (
    fileBody, w->selectedUpperWhere, anchors[index].where));
}

bool
TransclusionLowerPage::eventFilter (QObject* watched, QEvent* event) {
  if ((watched == searchEdit || watched == anchorList) &&
      event->type () == QEvent::KeyPress) {
    QKeyEvent* key= static_cast<QKeyEvent*> (event);
    if (key->key () == Qt::Key_Up || key->key () == Qt::Key_Down) {
      int count= anchorList->count ();
      if (count <= 0) return true;
      int row= anchorList->currentRow ();
      if (row < 0) row= key->key () == Qt::Key_Down ? -1 : 0;
      row= (row + (key->key () == Qt::Key_Up ? -1 : 1) + count) % count;
      anchorList->setCurrentRow (row);
      return true;
    }
  }
  return QWizardPage::eventFilter (watched, event);
}

bool
TransclusionLowerPage::validatePage () {
  QTMVaultTransclusionWizard* w=
    static_cast<QTMVaultTransclusionWizard*> (wizard ());
  if (w->resultAccepted) return true;
  QListWidgetItem* item= anchorList->currentItem ();
  if (item == nullptr) {
    QMessageBox::information (this, "Insert transclusion",
                              "Select a lower bound first.");
    return false;
  }
  int index= item->data (WikilinkIndexRole).toInt ();
  if (index < 0 || index >= (int) anchors.size ()) return false;
  if (!path_less (w->selectedUpperWhere, anchors[index].where)) {
    QMessageBox::warning (
      this, "Insert transclusion",
      "The lower bound must occur after the upper bound in the document.");
    return false;
  }
  w->setResult (w->selectedRelPath, w->selectedUpperAnchor,
                anchors[index].anchor, w->fileHint, anchors[index].anchor);
  return true;
}

TransclusionSearchPage::TransclusionSearchPage (QWidget* parent)
  : QWizardPage (parent) {
  setFinalPage (true);
  setTitle ("Locate by search");
  setSubTitle ("Search inside a kind of enunciation and transclude the selected result.");

  queryEdit= new QLineEdit (this);
  queryEdit->setPlaceholderText ("Search text");
  namespaceCombo= new QTMCompletingComboBox (this);
  namespaceCombo->setInsertPolicy (QComboBox::NoInsert);
  namespaceCombo->setMinimumWidth (300);
  namespaceCombo->lineEdit ()->setPlaceholderText ("All namespaces");
  namespaceCombo->lineEdit ()->setClearButtonEnabled (true);
  namespaceCombo->completer ()->setCaseSensitivity (Qt::CaseInsensitive);
  namespaceCombo->completer ()->setFilterMode (Qt::MatchContains);

  enunciationCombo= new QComboBox (this);
  enunciationCombo->addItem ("Any", "");
  for (const WikilinkEnunciationFilterEntry& entry: wikilink_enunciation_filters)
    enunciationCombo->addItem (entry.label, entry.tag);
  enunciationCombo->setMinimumWidth (190);
#if ATHENA_ENABLE_PERSON_SUBSYSTEM
  personCombo= new QComboBox (this);
  personCombo->setEditable (true);
  personCombo->setInsertPolicy (QComboBox::NoInsert);
  personCombo->lineEdit ()->setPlaceholderText ("Any person");
#endif
  caseInsensitiveCheck= new QCheckBox ("Case-insensitive", this);
  caseInsensitiveCheck->setChecked (
    get_preference (transclusion_search_case_pref, "off") == "on");
  fuzzyCheck= new QCheckBox ("Fuzzy", this);
  preserveCheckboxLabel (caseInsensitiveCheck);
  preserveCheckboxLabel (fuzzyCheck);
  fuzzyCheck->setChecked (
    get_preference (transclusion_search_fuzzy_pref, "off") == "on");

  searchButton= new QPushButton ("Search", this);
  stopButton= new QPushButton ("Stop", this);
  stopButton->setEnabled (false);
  statusLabel= new QLabel (this);
  progress= new QProgressBar (this);
  progress->setRange (0, 1);
  progress->setValue (0);
  resultList= new QListWidget (this);
  resultList->setAlternatingRowColors (true);
  previewTitle= new QLabel ("Select a search result to preview it.", this);
  configurePreviewTitle (previewTitle);
  previewHost= new QWidget (this);
  previewHost->setMinimumHeight (420);
  previewHost->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Expanding);
  QVBoxLayout* previewHostLayout= new QVBoxLayout (previewHost);
  previewHostLayout->setContentsMargins (0, 0, 0, 0);

  QHBoxLayout* searchRow= new QHBoxLayout ();
  searchRow->addWidget (queryEdit, 1);
  searchRow->addWidget (searchButton);
  searchRow->addWidget (stopButton);

  QGridLayout* filters= new QGridLayout ();
  filters->setColumnStretch (1, 1);
  filters->setColumnStretch (3, 1);
  filters->addWidget (new QLabel ("Within namespace:", this), 0, 0);
  filters->addWidget (namespaceCombo, 0, 1);
  filters->addWidget (new QLabel ("Enunciation:", this), 0, 2);
  filters->addWidget (enunciationCombo, 0, 3);
#if ATHENA_ENABLE_PERSON_SUBSYSTEM
  filters->addWidget (new QLabel ("Person:", this), 1, 0);
  filters->addWidget (personCombo, 1, 1);
#endif
  filters->addWidget (new QLabel ("Matching:", this), 2, 0);
  QHBoxLayout* matching= new QHBoxLayout ();
  matching->setContentsMargins (0, 0, 0, 0);
  matching->addWidget (caseInsensitiveCheck);
  matching->addSpacing (12);
  matching->addWidget (fuzzyCheck);
  matching->addStretch ();
  filters->addLayout (matching, 2, 1, 1, 3);

  QWidget* left= new QWidget (this);
  QVBoxLayout* leftLayout= new QVBoxLayout (left);
  leftLayout->setContentsMargins (0, 0, 0, 0);
  leftLayout->addWidget (new QLabel ("Enunciations:", this));
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
  splitter->setSizes (QList<int> () << 430 << 830);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->addLayout (searchRow);
  layout->addLayout (filters);
  layout->addWidget (statusLabel);
  layout->addWidget (progress);
  layout->addWidget (splitter, 1);
  setTabOrder (queryEdit, searchButton);
  setTabOrder (searchButton, namespaceCombo);
  setTabOrder (namespaceCombo, enunciationCombo);
#if ATHENA_ENABLE_PERSON_SUBSYSTEM
  setTabOrder (enunciationCombo, personCombo);
  setTabOrder (personCombo, caseInsensitiveCheck);
#else
  setTabOrder (enunciationCombo, caseInsensitiveCheck);
#endif
  setTabOrder (caseInsensitiveCheck, fuzzyCheck);
  setTabOrder (fuzzyCheck, resultList);

  connect (searchButton, &QPushButton::clicked,
           this, [this] () { startSearch (); });
  connect (stopButton, &QPushButton::clicked,
           this, [this] () {
             if (searchTask) searchTask->cancelled= true;
             stopButton->setEnabled (false);
             statusLabel->setText ("Stopping search...");
           });
  QShortcut* searchShortcut= new QShortcut (QKeySequence ("Alt+S"), this);
  searchShortcut->setContext (Qt::WidgetWithChildrenShortcut);
  connect (searchShortcut, &QShortcut::activated,
           this, [this] () { startSearch (); });
  QShortcut* stopShortcut= new QShortcut (QKeySequence ("Alt+T"), this);
  stopShortcut->setContext (Qt::WidgetWithChildrenShortcut);
  connect (stopShortcut, &QShortcut::activated,
           this, [this] () {
             if (stopButton->isEnabled ()) stopButton->click ();
           });
  auto insertCurrent= [this] () {
    if (resultList->currentItem () == nullptr) return;
    if (acceptCurrentResult ()) {
      QTMVaultTransclusionWizard* w=
        static_cast<QTMVaultTransclusionWizard*> (wizard ());
      QTimer::singleShot (0, w, [w] () { w->accept (); });
    }
  };
  QShortcut* insertShortcut= new QShortcut (QKeySequence (Qt::Key_Return),
                                             this);
  insertShortcut->setContext (Qt::WidgetWithChildrenShortcut);
  connect (insertShortcut, &QShortcut::activated, this, insertCurrent);
  QShortcut* keypadInsertShortcut=
    new QShortcut (QKeySequence (Qt::Key_Enter), this);
  keypadInsertShortcut->setContext (Qt::WidgetWithChildrenShortcut);
  connect (keypadInsertShortcut, &QShortcut::activated, this, insertCurrent);
  connect (caseInsensitiveCheck, &QCheckBox::toggled,
           this, [] (bool on) {
             set_preference (transclusion_search_case_pref,
                             on ? string ("on") : string ("off"));
           });
  connect (fuzzyCheck, &QCheckBox::toggled,
           this, [] (bool on) {
             set_preference (transclusion_search_fuzzy_pref,
                             on ? string ("on") : string ("off"));
           });
  connect (resultList, &QListWidget::currentItemChanged,
           this, [this] (QListWidgetItem* current, QListWidgetItem*) {
             updatePreview (current);
           });
  connect (resultList, &QListWidget::itemDoubleClicked,
           this, [this] (QListWidgetItem*) {
             if (acceptCurrentResult ()) {
               QTMVaultTransclusionWizard* w=
                 static_cast<QTMVaultTransclusionWizard*> (wizard ());
               QTimer::singleShot (0, w, [w] () { w->accept (); });
             }
           });
}

int
TransclusionSearchPage::nextId () const {
  return -1;
}

void
TransclusionSearchPage::initializePage () {
  QWizardPage::initializePage ();
  refreshNamespaces ();
  caseInsensitiveCheck->setChecked (
    get_preference (transclusion_search_case_pref, "off") == "on");
  fuzzyCheck->setChecked (
    get_preference (transclusion_search_fuzzy_pref, "off") == "on");
  queryEdit->setFocus ();
}

void
TransclusionSearchPage::showEvent (QShowEvent* event) {
  QWizardPage::showEvent (event);
  QTimer::singleShot (0, this, [this] () {
    preview.ensureCreated (previewHost);
    updatePreview (resultList->currentItem ());
    preview.refresh ();
  });
  QTimer::singleShot (120, this, [this] () {
    preview.ensureCreated (previewHost);
    updatePreview (resultList->currentItem ());
    preview.refresh ();
  });
}

void
TransclusionSearchPage::refreshNamespaces () {
  QString current= namespaceCombo == nullptr ? QString () :
    namespaceCombo->currentText ().trimmed ();
  QStringList names;
  for (const athena_namespace_definition& ns: athena_namespaces_list ())
    names << to_qstring (ns.name);
  names.removeDuplicates ();
  names.sort (Qt::CaseInsensitive);
  namespaceCombo->clear ();
  namespaceCombo->addItem (QString ());
  namespaceCombo->addItems (names);
  namespaceCombo->setCurrentText (current);

#if ATHENA_ENABLE_PERSON_SUBSYSTEM
  QString person= personCombo->currentText ().trimmed ();
  personCombo->clear ();
  personCombo->addItem (QString ());
  personCombo->addItems (qtm_vault_person_names ());
  personCombo->setCurrentText (person);
#endif
}

QString
TransclusionSearchPage::selectedNamespace () const {
  return namespaceCombo == nullptr ? QString () :
    namespaceCombo->currentText ().trimmed ();
}

QString
TransclusionSearchPage::selectedEnunciation () const {
  return enunciationCombo == nullptr ? QString () :
    enunciationCombo->currentData ().toString ().trimmed ();
}

#if ATHENA_ENABLE_PERSON_SUBSYSTEM
QString
TransclusionSearchPage::selectedPerson () const {
  return personCombo == nullptr ? QString () :
    personCombo->currentText ().trimmed ();
}
#endif

bool
TransclusionSearchPage::caseInsensitiveSearch () const {
  return caseInsensitiveCheck != nullptr && caseInsensitiveCheck->isChecked ();
}

bool
TransclusionSearchPage::fuzzySearch () const {
  return fuzzyCheck != nullptr && fuzzyCheck->isChecked ();
}

int
TransclusionSearchPage::searchFile (
  tree body, url u, const tree& query, const VaultSearchOptions& options,
  std::vector<TransclusionSearchResult>& hits)
{
  try {
#if ATHENA_ENABLE_PERSON_SUBSYSTEM
    QString person= options.person;
    if (!person.isEmpty () &&
        !athena_tree_contains_person_text (body, from_qstring (person)))
      return 0;
#endif
    std::vector<WikilinkAnchorEntry> anchors;
    collect_anchors (body, path (), anchors);
    std::vector<TransclusionAnchorPair> pairs=
      collect_transclusion_pairs (anchors);
    QString tag= options.enunciation;
    if (tag.isEmpty ()) {
      std::vector<TransclusionAnchorPair> headings=
        collect_heading_anchor_targets (body, path ());
      pairs.insert (pairs.end (), headings.begin (), headings.end ());
    }

    int matched= 0;
    int oldMode= set_access_mode (DRD_ACCESS_SOURCE);
    try {
      bool caseInsensitive= options.caseInsensitive;
      bool fuzzy= options.fuzzy;
      for (const TransclusionAnchorPair& pair: pairs) {
        if (vault_search_cancelled ()) break;
        if (!tag.isEmpty () &&
            !anchor_pair_matches_enunciation (pair, tag))
          continue;
        tree range= build_preview_from_anchor_range (
          body, pair.upperWhere, pair.lowerWhere, nullptr, nullptr, false);
        std::vector<VaultContentMatch> matches;
        constexpr int matchLimit= 200;
        append_content_matches (matches, range, query, path (), matchLimit,
                                caseInsensitive, fuzzy);
        if (tag.isEmpty () && (int) matches.size () < matchLimit)
          append_heading_matches (matches, range, query, path (),
                                  matchLimit - (int) matches.size (),
                                  caseInsensitive, fuzzy);
        if (matches.empty ()) continue;
        score_search_match_titles (range, query, path (), matches,
                                   caseInsensitive, fuzzy);
        std::stable_sort (matches.begin (), matches.end (),
                          vault_search_match_precedes);

        TransclusionSearchResult result;
        result.file= u;
        result.upper= pair.upper;
        result.lower= pair.lower;
        result.upperWhere= pair.upperWhere;
        result.lowerWhere= pair.lowerWhere;
        result.exact= matches[0].exact;
        result.score= matches[0].score;
        result.titleMatchScore= matches[0].titleMatchScore;
        hits.push_back (result);
        matched++;
      }
    }
    catch (...) {
      set_access_mode (oldMode);
      throw;
    }
    set_access_mode (oldMode);

    if (matched <= 0) return 0;
    url root= url_system (from_qstring (options.root));
    QString rel= to_qstring (as_unix_string (delta (root * url (""), u)));
    for (int i=0; i<matched; i++) {
      hits[hits.size () - matched + i].relPath= rel;
      hits[hits.size () - matched + i].occurrence= i + 1;
      hits[hits.size () - matched + i].fileHits= matched;
    }
    return matched;
  }
  catch (...) {
    return 0;
  }
}

void
TransclusionSearchPage::startSearch () {
  if (!searchButton->isEnabled ()) return;
  searchButton->setEnabled (false);
  stopButton->setEnabled (true);
  auto finishSearch= [this] () {
    stopButton->setEnabled (false);
    searchButton->setEnabled (true);
  };
  results.clear ();
  resultList->clear ();
  previewTitle->setText ("Select a search result to preview it.");
  preview.ensureCreated (previewHost);
  preview.setBody (tree (DOCUMENT, ""));
  progress->setRange (0, 1);
  progress->setValue (0);

  QString queryText= queryEdit->text ().trimmed ();
  if (queryText.isEmpty ()) {
    statusLabel->setText ("Enter a non-empty search string.");
    finishSearch ();
    return;
  }

  std::vector<url> files;
  refreshNamespaces ();
  QString ns= selectedNamespace ();
  if (ns.isEmpty ()) {
    array<url> all= vault_get_all_files ();
    for (int i=0; i<N(all); i++) {
      string suf= suffix (all[i]);
      if (suf == "ath" || suf == "tm") files.push_back (all[i]);
    }
  }
  else {
    string error;
    std::shared_ptr<const athena_namespace_definition> def;
    if (!athena_namespace_get (from_qstring (ns), def)) {
      QMessageBox::warning (this, "Insert transclusion",
                            "Unknown namespace: " + ns);
      finishSearch ();
      return;
    }
    namespace_records<athena_namespace_match> members=
      athena_namespace_members (from_qstring (ns), error);
    if (error != "")
      QMessageBox::warning (this, "Insert transclusion",
                            "Namespace warning: " + to_qstring (error));
    std::set<std::string> seen;
    for (const athena_namespace_match& m: members) {
      string suf= suffix (m.file_url ());
      if (suf != "ath" && suf != "tm") continue;
      std::string key= to_qstring (m.file_path).toStdString ();
      if (!seen.insert (key).second) continue;
      files.push_back (m.file_url ());
    }
  }

  std::sort (files.begin (), files.end (),
             [] (const url& a, const url& b) {
               return as_unix_string (a) < as_unix_string (b);
             });

  VaultSearchOptions options;
  options.query= queryText;
  options.root= to_qstring (as_system_string (vault_get_root ()));
  options.enunciation= selectedEnunciation ();
  options.caseInsensitive= caseInsensitiveSearch ();
  options.fuzzy= fuzzySearch ();
#if ATHENA_ENABLE_PERSON_SUBSYSTEM
  options.person= selectedPerson ();
#endif
  std::vector<std::string> paths;
  paths.reserve (files.size ());
  for (const url& file: files)
    paths.push_back (to_qstring (concretize (file)).toStdString ());
  if (searchTask) searchTask->cancelled= true;
  const auto generation= ++searchGeneration;
  searchTask= start_vault_search<TransclusionSearchResult> (
    this, std::move (paths), std::move (options), &TransclusionSearchPage::searchFile,
    [this, generation] (const VaultSearchProgress& p) {
      if (generation != searchGeneration) return;
      progress->setRange (0, p.total);
      progress->setValue (p.completed);
      statusLabel->setText (p.inspecting ?
        QString ("Inspecting %1/%2 candidate files; %3 range(s) in %4 file(s).")
          .arg (p.completed).arg (p.total).arg (p.hits).arg (p.matchedFiles) :
        QString ("Prefiltering source %1/%2; %3 candidate file(s).")
          .arg (p.completed).arg (p.total).arg (p.candidates));
    },
    [this, finishSearch, generation] (std::vector<TransclusionSearchResult> collected,
                          const VaultSearchProgress& p, bool stopped) {
      if (generation != searchGeneration) return;
      statusLabel->setText (stopped ?
        QString ("Search stopped after %1/%2 files; %3 range(s) in %4 file(s).")
          .arg (p.completed).arg (p.total).arg (p.hits).arg (p.matchedFiles) :
        QString ("%1 range(s) in %2 file(s); structurally inspected %3 files.")
          .arg (p.hits).arg (p.matchedFiles).arg (p.completed));
      std::stable_sort (
        collected.begin (), collected.end (),
        [] (const TransclusionSearchResult& a,
            const TransclusionSearchResult& b) {
          if (a.titleMatchScore != b.titleMatchScore)
            return a.titleMatchScore > b.titleMatchScore;
          if (a.exact != b.exact) return a.exact;
          if (a.exact) return false;
          if (a.score != b.score) return a.score > b.score;
          int pathOrder= QString::compare (a.relPath, b.relPath,
                                          Qt::CaseSensitive);
          if (pathOrder != 0) return pathOrder < 0;
          return path_less (a.upperWhere, b.upperWhere);
        });
      for (const TransclusionSearchResult& hit: collected) addResult (hit);
      if (resultList->count () > 0) {
        resultList->setCurrentRow (0);
        resultList->setFocus ();
      }
      else queryEdit->setFocus ();
      finishSearch ();
    });
}

void
TransclusionSearchPage::addResult (const TransclusionSearchResult& result) {
  results.push_back (result);
  QListWidgetItem* item= new QListWidgetItem (
    QString ("%1 (%2)  %3")
      .arg (result.relPath)
      .arg (result.occurrence)
      .arg (clean_anchor_display (result.upper)));
  item->setData (WikilinkIndexRole, (int) results.size () - 1);
  QString tooltip= QString ("%1\nOccurrence %2 of %3\n%4 ... %5")
      .arg (result.relPath)
      .arg (result.occurrence)
      .arg (result.fileHits)
      .arg (result.upper)
      .arg (result.lower);
  if (!result.exact)
    tooltip += QString ("\nFuzzy match: %1%").arg (result.score, 0, 'f', 1);
  item->setToolTip (tooltip);
  resultList->addItem (item);
}

void
TransclusionSearchPage::updatePreview (QListWidgetItem* current) {
  if (current == nullptr) {
    previewTitle->setText ("Select a search result to preview it.");
    preview.ensureCreated (previewHost);
    preview.setBody (tree (DOCUMENT, ""));
    return;
  }
  int index= current->data (WikilinkIndexRole).toInt ();
  if (index < 0 || index >= (int) results.size ()) return;
  const TransclusionSearchResult& result= results[index];
  previewTitle->setText (
    QString ("%1  (%2 of %3)")
      .arg (result.relPath)
      .arg (result.occurrence)
      .arg (result.fileHits));
  try {
    tree body= import_body_for_preview (result.file);
    preview.ensureCreated (previewHost);
    preview.setBody (build_preview_from_anchor_range (
      body, result.upperWhere, result.lowerWhere));
  }
  catch (...) {
    preview.ensureCreated (previewHost);
    preview.setBody (tree (DOCUMENT, "Preview unavailable."));
  }
}

bool
TransclusionSearchPage::acceptCurrentResult () {
  QTMVaultTransclusionWizard* w=
    static_cast<QTMVaultTransclusionWizard*> (wizard ());
  if (w->resultAccepted) return true;
  QListWidgetItem* item= resultList->currentItem ();
  if (item == nullptr) {
    QMessageBox::information (this, "Insert transclusion",
                              "Select a search result first.");
    return false;
  }
  int index= item->data (WikilinkIndexRole).toInt ();
  if (index < 0 || index >= (int) results.size ()) return false;
  const TransclusionSearchResult& result= results[index];
  w->setResult (result.relPath, result.upper, result.lower,
                file_display_stem (result.relPath), result.upper);
  return true;
}

bool
TransclusionSearchPage::validatePage () {
  QTMVaultTransclusionWizard* w=
    static_cast<QTMVaultTransclusionWizard*> (wizard ());
  if (w->resultAccepted) return true;
  return acceptCurrentResult ();
}

QTMVaultTransclusionWizard::QTMVaultTransclusionWizard (QWidget* parent)
  : QWizard (parent), selectedUpperIndex (-1), filesLoaded (false),
    filesLoadScheduled (false), resultAccepted (false) {
  setWindowTitle ("Insert Transclusion");
  resize (1220, 780);
  setOption (QWizard::NoBackButtonOnStartPage, true);

  modePage= new TransclusionModePage (this);
  filePage= new TransclusionFilePage (this);
  kindPage= new TransclusionKindPage (this);
  enunciationPage= new TransclusionEnunciationPage (this);
  upperPage= new TransclusionUpperPage (this);
  lowerPage= new TransclusionLowerPage (this);
  searchPage= new TransclusionSearchPage (this);
  artifactPage= new QTMVaultArtifactPage (
    QTMVaultArtifactUsage::Transclusion, transclusion_search_case_pref,
    transclusion_search_fuzzy_pref, this);
  artifactPage->setSelectionHandler (
    [this] (const QTMVaultArtifactSelection& selection) {
      setResult (selection.relative_path, selection.upper_anchor,
                 selection.lower_anchor,
                 file_display_stem (selection.relative_path),
                 selection.upper_anchor);
    });

  setPage (TransclusionModePageId, modePage);
  setPage (TransclusionFilePageId, filePage);
  setPage (TransclusionKindPageId, kindPage);
  setPage (TransclusionEnunciationPageId, enunciationPage);
  setPage (TransclusionUpperPageId, upperPage);
  setPage (TransclusionLowerPageId, lowerPage);
  setPage (TransclusionSearchPageId, searchPage);
  setPage (TransclusionArtifactPageId, artifactPage);
  setStartId (TransclusionModePageId);
}

void
QTMVaultTransclusionWizard::showEvent (QShowEvent* event) {
  QWizard::showEvent (event);
  scheduleLoadFiles ();
}

void
QTMVaultTransclusionWizard::loadFiles () {
  files= load_vault_link_files ();
  filesLoaded= true;
}

void
QTMVaultTransclusionWizard::scheduleLoadFiles () {
  if (filesLoaded || filesLoadScheduled) return;
  filesLoadScheduled= true;
  QTimer::singleShot (0, this, [this] () {
    if (filesLoaded) return;
    loadFiles ();
    if (filePage != nullptr) filePage->updateList ();
  });
}

void
QTMVaultTransclusionWizard::setResult (const QString& relPath,
                                       const QString& anchorBegin,
                                       const QString& anchorEnd,
                                       const QString& fileHint2,
                                       const QString& anchorHint2) {
  selectedRelPath= relPath;
  selectedAnchorBegin= anchorBegin;
  selectedAnchorEnd= anchorEnd;
  fileHint= fileHint2;
  anchorHint= anchorHint2;
  resultAccepted= true;
}

bool
QTMVaultTransclusionWizard::selectFileFromPage () {
  QListWidgetItem* item= filePage->fileList->currentItem ();
  if (item == nullptr && filePage->fileList->count () > 0)
    item= filePage->fileList->item (0);
  if (item == nullptr) {
    QMessageBox::information (this, "Insert transclusion",
                              "Select a file first.");
    return false;
  }
  int index= item->data (WikilinkIndexRole).toInt ();
  if (index < 0 || index >= (int) files.size ()) return false;
  selectedRelPath= files[index].relPath;
  selectedFileUrl= files[index].file;
  QString typed= filePage->searchEdit->text ().trimmed ();
  fileHint= typed.isEmpty () ? files[index].stem : typed;
  selectedAnchorBegin.clear ();
  selectedAnchorEnd.clear ();
  anchorHint.clear ();
  selectedUpperIndex= -1;
  selectedUpperAnchor.clear ();
  selectedUpperWhere= path ();
  resultAccepted= false;
  return true;
}

tree
QTMVaultTransclusionWizard::getResult () const {
  if (!resultAccepted) return UNINIT;
  tree res (TUPLE);
  res << tree (from_qstring (selectedRelPath));
  res << tree (from_qstring (selectedAnchorBegin));
  res << tree (from_qstring (selectedAnchorEnd));
  res << tree (from_qstring (fileHint));
  res << tree (from_qstring (anchorHint));
  return res;
}

} // namespace

tree
qtm_vault_choose_transclusion (QWidget* parent) {
  QTMVaultTransclusionWizard wizard (parent);
  if (wizard.exec () == QDialog::Accepted) return wizard.getResult ();
  return UNINIT;
}
