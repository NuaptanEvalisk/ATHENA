/******************************************************************************
* MODULE     : QTMVaultWikilinkWizard.cpp
* DESCRIPTION: Vault wikilink insertion wizard
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMVaultWikilinkWizard.hpp"
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
#include <QDir>
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
#include <QRegularExpression>
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

static constexpr const char* wikilink_search_case_pref=
  "vault wikilink inserter case insensitive search";
static constexpr const char* wikilink_search_fuzzy_pref=
  "vault wikilink inserter fuzzy search";
static constexpr const char* wikilink_display_template_file_pref=
  "vault wikilink display template file";
static constexpr const char* wikilink_display_template_heading_pref=
  "vault wikilink display template heading";
static constexpr const char* wikilink_display_template_anchor_pref=
  "vault wikilink display template anchor";

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

struct WikilinkDisplayContext {
  QString kind;
  QString type;
  QString content;
  QString relPath;
  QString stem;
  QString absolutePath;
};

static int
path_top_index (path p) {
  if (is_nil (p)) return 0;
  return p->item;
}

static QString
titlecase_first (QString s) {
  if (s.isEmpty ()) return s;
  s[0]= s[0].toUpper ();
  return s;
}

static QString
locase_first (QString s) {
  if (s.isEmpty ()) return s;
  s[0]= s[0].toLower ();
  return s;
}

static QString
absolute_vault_path (const QString& relPath) {
  QString root= to_qstring (concretize (vault_get_root ()));
  if (root.isEmpty ()) return relPath;
  return QDir (root).absoluteFilePath (relPath);
}

static WikilinkDisplayContext
wikilink_display_context (const QString& relPath, const QString& anchor) {
  WikilinkDisplayContext c;
  c.kind= "anchor";
  c.type= "anchor";
  c.relPath= relPath;
  c.stem= file_display_stem (relPath);
  c.absolutePath= absolute_vault_path (relPath);

  QString key= anchor_pair_key (anchor);
  if (anchor.trimmed ().isEmpty ()) {
    c.kind= "file";
    c.type= "file";
    c.content= c.stem;
    return c;
  }

  QRegularExpression headingRe ("^H([1-5])\\s+(.+)$");
  QRegularExpressionMatch headingMatch= headingRe.match (key);
  if (headingMatch.hasMatch ()) {
    c.kind= "heading";
    c.type= "H" + headingMatch.captured (1);
    c.content= headingMatch.captured (2).trimmed ();
    return c;
  }

  int colon= key.indexOf (":");
  if (colon >= 0) {
    c.type= key.left (colon).trimmed ().toLower ();
    c.content= key.mid (colon + 1).trimmed ();
  }
  else {
    c.content= key.trimmed ();
  }
  if (c.content.isEmpty ()) c.content= clean_anchor_display (anchor);
  return c;
}

static QString
wikilink_display_template_for_kind (const QString& kind) {
  const char* key= wikilink_display_template_anchor_pref;
  const char* fallback= "%c";
  if (kind == "file") {
    key= wikilink_display_template_file_pref;
    fallback= "%f";
  }
  else if (kind == "heading") {
    key= wikilink_display_template_heading_pref;
    fallback= "%c";
  }
  return to_qstring (get_preference (key, fallback));
}

static QString
apply_wikilink_display_template (const QString& templ,
                                 const WikilinkDisplayContext& c) {
  QString out;
  out.reserve (templ.size () + c.content.size ());
  for (int i=0; i<templ.size (); i++) {
    QChar ch= templ[i];
    if (ch != '%' || i + 1 >= templ.size ()) {
      out += ch;
      continue;
    }
    QChar code= templ[++i];
    if (code == '%') out += '%';
    else if (code == 't') out += c.type;
    else if (code == 'T') out += titlecase_first (c.type);
    else if (code == 'f') out += c.stem;
    else if (code == 'F') out += c.relPath;
    else if (code == 'p') out += c.absolutePath;
    else if (code == 'c') out += c.content;
    else if (code == 'C') out += titlecase_first (c.content);
    else if (code == 's') out += locase_first (c.content);
    else {
      out += '%';
      out += code;
    }
  }
  return out.trimmed ();
}

static QString
default_wikilink_display_text (const QString& relPath,
                               const QString& anchor) {
  WikilinkDisplayContext c= wikilink_display_context (relPath, anchor);
  QString text= apply_wikilink_display_template (
    wikilink_display_template_for_kind (c.kind), c);
  if (!text.isEmpty ()) return text;
  if (c.kind == "file") return c.stem;
  if (!c.content.isEmpty ()) return c.content;
  return clean_anchor_display (anchor);
}

static bool
is_heading_anchor_key (const QString& key) {
  static const QRegularExpression headingRe ("^H[1-6]\\s+.+$");
  return headingRe.match (key).hasMatch ();
}

static bool
is_aofm_paragraph_anchor_pair (const TransclusionAnchorPair& pair) {
  QString key= anchor_pair_key (pair.upper);
  if (key.isEmpty ()) return false;
  if (is_heading_anchor_key (key)) return false;
  return !key.contains (":");
}

static void
collect_aofm_paragraph_matches (std::vector<VaultContentMatch>& out, tree body,
                                tree query, path base, int limit,
                                bool caseInsensitive, bool fuzzy) {
  if (limit <= 0 || is_atomic (body)) return;
  if (!is_func (body, DOCUMENT)) {
    append_content_matches (out, body, query, base, limit,
                            caseInsensitive, fuzzy);
    return;
  }

  std::vector<WikilinkAnchorEntry> anchors;
  collect_anchors (body, base, anchors);
  std::vector<TransclusionAnchorPair> pairs=
    collect_transclusion_pairs (anchors);

  int found= 0;
  for (const TransclusionAnchorPair& pair: pairs) {
    if (found >= limit) return;
    if (!is_aofm_paragraph_anchor_pair (pair)) continue;

    int first= path_top_index (pair.upperWhere) + 1;
    int last = path_top_index (pair.lowerWhere);
    if (first < 0) first= 0;
    if (last > N(body)) last= N(body);
    if (first >= last) continue;

    for (int i= first; i<last; i++) {
      if (found >= limit) break;
      size_t before= out.size ();
      append_content_matches (out, body[i], query, base * i, limit - found,
                              caseInsensitive, fuzzy);
      found += (int) (out.size () - before);
    }
  }
}

enum WikilinkWizardPageId {
  WikilinkModePageId= 0,
  WikilinkFilePageId= 1,
  WikilinkAnchorPageId= 2,
  WikilinkSearchPageId= 3,
  WikilinkArtifactPageId= 4
};

class QTMVaultWikilinkWizard;

class WikilinkModePage : public QWizardPage {
public:
  WikilinkModePage (QWidget* parent= nullptr);
  int nextId () const override;

  QRadioButton* fileFirstRadio;
  QRadioButton* searchRadio;
  QRadioButton* artifactRadio;
};

class WikilinkFilePage : public QWizardPage {
public:
  WikilinkFilePage (QWidget* parent= nullptr);
  void initializePage () override;
  bool validatePage () override;
  bool eventFilter (QObject* watched, QEvent* event) override;

  void updateList ();
  void moveSelection (int delta);
  bool completeFromSelection ();

  QLineEdit*   searchEdit;
  QListWidget* fileList;
};

class WikilinkAnchorPage : public QWizardPage {
public:
  WikilinkAnchorPage (QWidget* parent= nullptr);
  int nextId () const override;
  void initializePage () override;
  bool validatePage () override;
  bool eventFilter (QObject* watched, QEvent* event) override;
  void showEvent (QShowEvent* event) override;

  void updateList ();
  void updateCurrentPreview ();
  void updateDefaultDisplayText ();
  void moveFieldFocus (bool backward);

  QLineEdit*   searchEdit;
  QCheckBox*   caseInsensitiveCheck;
  QCheckBox*   fuzzyCheck;
  QListWidget* anchorList;
  QLineEdit*   displayEdit;
  QLabel*      previewTitle;
  QWidget*     previewHost;
  WikilinkPreview preview;
  std::vector<WikilinkAnchorEntry> anchors;
  tree        fileBody;
  bool        displayTouched;
};

class WikilinkSearchPage : public QWizardPage {
public:
  WikilinkSearchPage (QWidget* parent= nullptr);
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
  ~WikilinkSearchPage () override { if (searchTask) searchTask->cancelled= true; }
  void cleanupPage () override {
    ++searchGeneration;
    if (searchTask) searchTask->cancelled= true;
    stopButton->setEnabled (false);
    searchButton->setEnabled (true);
  }
  static int searchFile (tree body, url u, const tree& query,
                         const VaultSearchOptions& options,
                         std::vector<WikilinkSearchResult>& hits);
  void addResult (const WikilinkSearchResult& result);
  void updatePreview (QListWidgetItem* current);
  void updateDefaultDisplayText ();
  bool chooseAnchorItem (QListWidgetItem* item);
  void acceptAnchorItem (QListWidgetItem* item);

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
  QListWidget* anchorList;
  QLineEdit*   displayEdit;
  QPushButton* insertButton;
  QLabel*      previewTitle;
  QWidget*     previewHost;
  WikilinkPreview preview;
  std::vector<WikilinkSearchResult> results;
  std::vector<WikilinkAnchorEntry> currentAnchors;
  bool        displayTouched;
  std::shared_ptr<VaultSearchControl> searchTask;
  unsigned long searchGeneration= 0;
};

class QTMVaultWikilinkWizard : public QWizard {
public:
  QTMVaultWikilinkWizard (QWidget* parent= nullptr);
  void showEvent (QShowEvent* event) override;

  tree getResult () const;
  void setResult (const QString& relPath, const QString& anchor,
                  const QString& fileHint, const QString& anchorHint,
                  const QString& displayText);
  bool selectFileFromPage ();
  bool finishFileFirst ();

  std::vector<WikilinkFileEntry> files;
  QString selectedRelPath;
  url     selectedFileUrl;
  QString fileHint;
  QString selectedAnchor;
  QString anchorHint;
  QString displayText;
  bool    filesLoaded;
  bool    filesLoadScheduled;
  bool    resultAccepted;

  WikilinkModePage*   modePage;
  WikilinkFilePage*   filePage;
  WikilinkAnchorPage* anchorPage;
  WikilinkSearchPage* searchPage;
  QTMVaultArtifactPage* artifactPage;

  void loadFiles ();
  void scheduleLoadFiles ();
};

WikilinkModePage::WikilinkModePage (QWidget* parent)
  : QWizardPage (parent) {
  setTitle ("Insert wikilink");
  setSubTitle ("Choose how to locate the target.");

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
WikilinkModePage::nextId () const {
  if (artifactRadio->isChecked ()) return WikilinkArtifactPageId;
  return searchRadio->isChecked () ? WikilinkSearchPageId :
    WikilinkFilePageId;
}

WikilinkFilePage::WikilinkFilePage (QWidget* parent)
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
WikilinkFilePage::initializePage () {
  QWizardPage::initializePage ();
  static_cast<QTMVaultWikilinkWizard*> (wizard ())->scheduleLoadFiles ();
  updateList ();
  searchEdit->setFocus ();
}

void
WikilinkFilePage::updateList () {
  QTMVaultWikilinkWizard* w=
    static_cast<QTMVaultWikilinkWizard*> (wizard ());
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
WikilinkFilePage::moveSelection (int delta) {
  int count= fileList->count ();
  if (count <= 0) return;
  int row= fileList->currentRow ();
  if (row < 0) row= delta > 0 ? -1 : 0;
  row= (row + delta + count) % count;
  fileList->setCurrentRow (row);
}

bool
WikilinkFilePage::completeFromSelection () {
  return qtm_commit_list_completion (searchEdit, fileList,
                                     WikilinkCompletionRole);
}

bool
WikilinkFilePage::eventFilter (QObject* watched, QEvent* event) {
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
WikilinkFilePage::validatePage () {
  QTMVaultWikilinkWizard* w=
    static_cast<QTMVaultWikilinkWizard*> (wizard ());
  if (!w->filesLoaded) {
    QMessageBox::information (this, "Insert wikilink",
                              "Vault files are still loading.");
    return false;
  }
  return w->selectFileFromPage ();
}

WikilinkAnchorPage::WikilinkAnchorPage (QWidget* parent)
  : QWizardPage (parent), displayTouched (false) {
  setFinalPage (true);
  setTitle ("Choose an anchor and display text");
  setSubTitle ("Choose an optional label in the file, preview the context, then insert.");

  searchEdit= new QLineEdit (this);
  searchEdit->setPlaceholderText ("Filter anchors; leave empty for the whole file");
  caseInsensitiveCheck= new QCheckBox ("Case-insensitive", this);
  fuzzyCheck= new QCheckBox ("Fuzzy", this);
  preserveCheckboxLabel (caseInsensitiveCheck);
  preserveCheckboxLabel (fuzzyCheck);
  caseInsensitiveCheck->setChecked (
    get_preference (wikilink_search_case_pref, "off") == "on");
  fuzzyCheck->setChecked (
    get_preference (wikilink_search_fuzzy_pref, "off") == "on");
  anchorList= new QListWidget (this);
  anchorList->setAlternatingRowColors (true);
  anchorList->setTabKeyNavigation (false);
  anchorList->setMinimumWidth (500);
  displayEdit= new QLineEdit (this);
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
  leftLayout->addWidget (new QLabel ("Anchor:", this));
  leftLayout->addWidget (searchEdit);
  QHBoxLayout* filters= new QHBoxLayout ();
  filters->addWidget (caseInsensitiveCheck);
  filters->addWidget (fuzzyCheck);
  filters->addStretch ();
  leftLayout->addLayout (filters);
  leftLayout->addWidget (anchorList, 1);
  leftLayout->addWidget (new QLabel ("Display text:", this));
  leftLayout->addWidget (displayEdit);

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
  splitter->setSizes (QList<int> () << 560 << 800);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->addWidget (splitter, 1);

  searchEdit->installEventFilter (this);
  anchorList->installEventFilter (this);
  displayEdit->installEventFilter (this);
  setTabOrder (searchEdit, anchorList);
  setTabOrder (anchorList, displayEdit);

  connect (searchEdit, &QLineEdit::textChanged,
           this, [this] (const QString&) { updateList (); });
  connect (caseInsensitiveCheck, &QCheckBox::toggled,
           this, [this] (bool enabled) {
             set_preference (wikilink_search_case_pref,
                             enabled ? "on" : "off");
             updateList ();
           });
  connect (fuzzyCheck, &QCheckBox::toggled,
           this, [this] (bool enabled) {
             set_preference (wikilink_search_fuzzy_pref,
                             enabled ? "on" : "off");
             updateList ();
           });
  connect (displayEdit, &QLineEdit::textEdited,
           this, [this] (const QString&) { displayTouched= true; });
  connect (anchorList, &QListWidget::currentItemChanged,
           this, [this] (QListWidgetItem*, QListWidgetItem*) {
             updateCurrentPreview ();
             updateDefaultDisplayText ();
           });
  connect (anchorList, &QListWidget::itemDoubleClicked,
           this, [this] (QListWidgetItem*) {
             QTMVaultWikilinkWizard* w=
               static_cast<QTMVaultWikilinkWizard*> (wizard ());
             if (w->finishFileFirst ())
               QTimer::singleShot (0, w, [w] () { w->accept (); });
           });
}

int
WikilinkAnchorPage::nextId () const {
  return -1;
}

void
WikilinkAnchorPage::initializePage () {
  QWizardPage::initializePage ();
  QTMVaultWikilinkWizard* w=
    static_cast<QTMVaultWikilinkWizard*> (wizard ());
  setBoundedSubTitle (this, "Target file: ", w->selectedRelPath);

  anchors.clear ();
  fileBody= tree (DOCUMENT, "");
  displayTouched= false;
  displayEdit->clear ();

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
    updateCurrentPreview ();
    preview.refresh ();
  });
  QTimer::singleShot (80, this, [this] () {
    preview.ensureCreated (previewHost);
    updateCurrentPreview ();
    preview.refresh ();
  });
  searchEdit->setFocus ();
}

void
WikilinkAnchorPage::showEvent (QShowEvent* event) {
  QWizardPage::showEvent (event);
  QTimer::singleShot (0, this, [this] () {
    preview.ensureCreated (previewHost);
    updateCurrentPreview ();
    preview.refresh ();
  });
  QTimer::singleShot (120, this, [this] () {
    preview.ensureCreated (previewHost);
    updateCurrentPreview ();
    preview.refresh ();
  });
}

void
WikilinkAnchorPage::updateList () {
  anchorList->clear ();
  QString query= searchEdit->text ().trimmed ();

  QListWidgetItem* whole= new QListWidgetItem ("(whole file)");
  whole->setData (WikilinkIndexRole, -1);
  whole->setData (WikilinkPayloadRole, QString ());
  anchorList->addItem (whole);

  std::set<int> pairedLower;
  std::set<int> enunciationUpper;
  for (const TransclusionAnchorPair& pair:
       collect_transclusion_pairs (anchors)) {
    if (!anchor_pair_is_enunciation (pair)) continue;
    pairedLower.insert (pair.lowerIndex);
    enunciationUpper.insert (pair.upperIndex);
  }

  std::vector<std::pair<int,int> > matches;
  for (int i=0; i<(int) anchors.size (); i++) {
    if (pairedLower.count (i) != 0) continue;
    QString text= enunciationUpper.count (i) != 0 ?
      anchor_pair_key (anchors[i].anchor) : anchors[i].anchor;
    int score= list_filter_score (text, query,
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
    QString text= enunciationUpper.count (m.second) != 0 ?
      anchor_pair_key (anchors[m.second].anchor) : anchors[m.second].anchor;
    QListWidgetItem* item= new QListWidgetItem (text);
    item->setData (WikilinkIndexRole, m.second);
    item->setData (WikilinkPayloadRole, anchors[m.second].anchor);
    anchorList->addItem (item);
  }
  if (anchorList->count () > 0) anchorList->setCurrentRow (0);
  updateCurrentPreview ();
  updateDefaultDisplayText ();
}

void
WikilinkAnchorPage::updateCurrentPreview () {
  QTMVaultWikilinkWizard* w=
    static_cast<QTMVaultWikilinkWizard*> (wizard ());
  QListWidgetItem* item= anchorList->currentItem ();
  int index= item == nullptr ? -1 : item->data (WikilinkIndexRole).toInt ();
  path focus= path ();
  QString title= w->selectedRelPath;
  if (index >= 0 && index < (int) anchors.size ()) {
    focus= anchors[index].where;
    title += "  --  " + anchors[index].anchor;
  }
  previewTitle->setText (title);
  preview.ensureCreated (previewHost);
  preview.setBody (build_preview_from_body (fileBody, focus, nullptr, nullptr));
}

void
WikilinkAnchorPage::updateDefaultDisplayText () {
  if (displayTouched) return;
  QTMVaultWikilinkWizard* w=
    static_cast<QTMVaultWikilinkWizard*> (wizard ());
  QListWidgetItem* item= anchorList->currentItem ();
  QString anchor= item == nullptr ? QString () :
    item->data (WikilinkPayloadRole).toString ();
  displayEdit->setText (
    default_wikilink_display_text (w->selectedRelPath, anchor));
}

void
WikilinkAnchorPage::moveFieldFocus (bool backward) {
  QWidget* focus= QApplication::focusWidget ();
  if (backward) {
    if (focus == displayEdit)
      anchorList->setFocus ();
    else if (focus == anchorList)
      searchEdit->setFocus ();
    else
      displayEdit->setFocus ();
  }
  else {
    if (focus == searchEdit)
      anchorList->setFocus ();
    else if (focus == anchorList)
      displayEdit->setFocus ();
    else
      searchEdit->setFocus ();
  }
}

bool
WikilinkAnchorPage::eventFilter (QObject* watched, QEvent* event) {
  if ((watched == searchEdit || watched == anchorList || watched == displayEdit) &&
      event->type () == QEvent::KeyPress) {
    QKeyEvent* key= static_cast<QKeyEvent*> (event);
    if (key->key () == Qt::Key_Tab || key->key () == Qt::Key_Backtab) {
      moveFieldFocus (key->key () == Qt::Key_Backtab ||
                      (key->modifiers () & Qt::ShiftModifier));
      return true;
    }
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
WikilinkAnchorPage::validatePage () {
  QTMVaultWikilinkWizard* w=
    static_cast<QTMVaultWikilinkWizard*> (wizard ());
  return w->finishFileFirst ();
}

WikilinkSearchPage::WikilinkSearchPage (QWidget* parent)
  : QWizardPage (parent), displayTouched (false) {
  setFinalPage (true);
  setTitle ("Locate by search");
  setSubTitle ("Search the vault, then click a usable { anchor from the preview.");

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
  enunciationCombo->addItem ("Paragraph", "__athena_paragraph__");
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
    get_preference (wikilink_search_case_pref, "off") == "on");
  fuzzyCheck= new QCheckBox ("Fuzzy", this);
  preserveCheckboxLabel (caseInsensitiveCheck);
  preserveCheckboxLabel (fuzzyCheck);
  fuzzyCheck->setChecked (
    get_preference (wikilink_search_fuzzy_pref, "off") == "on");

  searchButton= new QPushButton ("Search", this);
  stopButton= new QPushButton ("Stop", this);
  stopButton->setEnabled (false);
  statusLabel= new QLabel (this);
  progress= new QProgressBar (this);
  progress->setRange (0, 1);
  progress->setValue (0);
  resultList= new QListWidget (this);
  resultList->setAlternatingRowColors (true);
  resultList->setTabKeyNavigation (false);
  anchorList= new QListWidget (this);
  anchorList->setAlternatingRowColors (true);
  anchorList->setTabKeyNavigation (false);
  displayEdit= new QLineEdit (this);
  insertButton= new QPushButton ("Insert selected anchor", this);
  previewTitle= new QLabel ("Select a search result to preview it.", this);
  configurePreviewTitle (previewTitle);
  previewHost= new QWidget (this);
  previewHost->setMinimumHeight (360);
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
  leftLayout->addWidget (new QLabel ("Occurrences:", this));
  leftLayout->addWidget (resultList, 1);
  leftLayout->addWidget (new QLabel ("Usable wikilink anchors in preview:", this));
  leftLayout->addWidget (anchorList, 1);
  leftLayout->addWidget (new QLabel ("Display text:", this));
  leftLayout->addWidget (displayEdit);
  leftLayout->addWidget (insertButton);

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
  splitter->setSizes (QList<int> () << 430 << 770);

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
  setTabOrder (resultList, anchorList);
  setTabOrder (anchorList, displayEdit);
  setTabOrder (displayEdit, insertButton);

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
    if (anchorList->currentItem () != nullptr)
      acceptAnchorItem (anchorList->currentItem ());
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
             set_preference (wikilink_search_case_pref,
                             on ? string ("on") : string ("off"));
           });
  connect (fuzzyCheck, &QCheckBox::toggled,
           this, [] (bool on) {
             set_preference (wikilink_search_fuzzy_pref,
                             on ? string ("on") : string ("off"));
           });
  connect (resultList, &QListWidget::currentItemChanged,
           this, [this] (QListWidgetItem* current, QListWidgetItem*) {
             updatePreview (current);
           });
  connect (anchorList, &QListWidget::currentItemChanged,
           this, [this] (QListWidgetItem*, QListWidgetItem*) {
             updateDefaultDisplayText ();
           });
  connect (displayEdit, &QLineEdit::textEdited,
           this, [this] (const QString&) { displayTouched= true; });
  connect (insertButton, &QPushButton::clicked,
           this, [this] () { acceptAnchorItem (anchorList->currentItem ()); });
  connect (anchorList, &QListWidget::itemDoubleClicked,
           this, [this] (QListWidgetItem* item) { acceptAnchorItem (item); });
  connect (anchorList, &QListWidget::itemActivated,
           this, [this] (QListWidgetItem* item) { acceptAnchorItem (item); });
}

int
WikilinkSearchPage::nextId () const {
  return -1;
}

void
WikilinkSearchPage::initializePage () {
  QWizardPage::initializePage ();
  refreshNamespaces ();
  caseInsensitiveCheck->setChecked (
    get_preference (wikilink_search_case_pref, "off") == "on");
  fuzzyCheck->setChecked (
    get_preference (wikilink_search_fuzzy_pref, "off") == "on");
  queryEdit->setFocus ();
}

void
WikilinkSearchPage::showEvent (QShowEvent* event) {
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
WikilinkSearchPage::refreshNamespaces () {
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
WikilinkSearchPage::selectedNamespace () const {
  return namespaceCombo == nullptr ? QString () :
    namespaceCombo->currentText ().trimmed ();
}

QString
WikilinkSearchPage::selectedEnunciation () const {
  if (enunciationCombo == nullptr) return QString ();
  return enunciationCombo->currentData ().toString ().trimmed ();
}

#if ATHENA_ENABLE_PERSON_SUBSYSTEM
QString
WikilinkSearchPage::selectedPerson () const {
  return personCombo == nullptr ? QString () :
    personCombo->currentText ().trimmed ();
}
#endif

bool
WikilinkSearchPage::caseInsensitiveSearch () const {
  return caseInsensitiveCheck != nullptr && caseInsensitiveCheck->isChecked ();
}

bool
WikilinkSearchPage::fuzzySearch () const {
  return fuzzyCheck != nullptr && fuzzyCheck->isChecked ();
}

int
WikilinkSearchPage::searchFile (
  tree body, url u, const tree& query, const VaultSearchOptions& options,
  std::vector<WikilinkSearchResult>& hits) {
  try {
#if ATHENA_ENABLE_PERSON_SUBSYSTEM
    QString person= options.person;
    if (!person.isEmpty () &&
        !athena_tree_contains_person_text (body, from_qstring (person)))
      return 0;
#endif
    int oldMode= set_access_mode (DRD_ACCESS_SOURCE);
    std::vector<VaultContentMatch> matches;
    try {
      QString enunciation= options.enunciation;
      bool caseInsensitive= options.caseInsensitive;
      bool fuzzy= options.fuzzy;
      if (enunciation.isEmpty ()) {
        append_content_matches (matches, body, query, path (), 200,
                                caseInsensitive, fuzzy);
        append_heading_matches (matches, body, query, path (),
                                200 - (int) matches.size (),
                                caseInsensitive, fuzzy);
      }
      else if (enunciation == "__athena_paragraph__")
        collect_aofm_paragraph_matches (matches, body, query, path (), 200,
                                        caseInsensitive, fuzzy);
      else
        collect_enunciation_matches (matches, body, query,
                                     from_qstring (enunciation), path (), 200,
                                     caseInsensitive, fuzzy);
      score_search_match_titles (body, query, path (), matches,
                                 caseInsensitive, fuzzy);
    }
    catch (...) {
      set_access_mode (oldMode);
      throw;
    }
    set_access_mode (oldMode);

    int hitCount= (int) matches.size ();
    if (hitCount <= 0) return 0;

    url root= url_system (from_qstring (options.root));
    QString rel= to_qstring (as_unix_string (delta (root * url (""), u)));
    int occurrence= 1;
    for (const VaultContentMatch& match: matches) {
      WikilinkSearchResult result;
      result.relPath= rel;
      result.file= u;
      result.occurrence= occurrence++;
      result.fileHits= hitCount;
      result.hitStart= match.start;
      result.hitEnd= match.end;
      result.exact= match.exact;
      result.score= match.score;
      result.titleMatchScore= match.titleMatchScore;
      hits.push_back (result);
    }
    return hitCount;
  }
  catch (...) {
    return 0;
  }
}

void
WikilinkSearchPage::startSearch () {
  if (!searchButton->isEnabled ()) return;
  searchButton->setEnabled (false);
  stopButton->setEnabled (true);
  auto finishSearch= [this] () {
    stopButton->setEnabled (false);
    searchButton->setEnabled (true);
  };
  results.clear ();
  currentAnchors.clear ();
  resultList->clear ();
  anchorList->clear ();
  displayTouched= false;
  displayEdit->clear ();
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
      QMessageBox::warning (this, "Insert wikilink",
                            "Unknown namespace: " + ns);
      finishSearch ();
      return;
    }
    namespace_records<athena_namespace_match> members=
      athena_namespace_members (from_qstring (ns), error);
    if (error != "")
      QMessageBox::warning (this, "Insert wikilink",
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
  searchTask= start_vault_search<WikilinkSearchResult> (
    this, std::move (paths), std::move (options), &WikilinkSearchPage::searchFile,
    [this, generation] (const VaultSearchProgress& p) {
      if (generation != searchGeneration) return;
      progress->setRange (0, p.total);
      progress->setValue (p.completed);
      statusLabel->setText (p.inspecting ?
        QString ("Inspecting %1/%2 candidate files; %3 occurrence(s) in %4 file(s).")
          .arg (p.completed).arg (p.total).arg (p.hits).arg (p.matchedFiles) :
        QString ("Prefiltering source %1/%2; %3 candidate file(s).")
          .arg (p.completed).arg (p.total).arg (p.candidates));
    },
    [this, finishSearch, generation] (std::vector<WikilinkSearchResult> collected,
                          const VaultSearchProgress& p, bool stopped) {
      if (generation != searchGeneration) return;
      statusLabel->setText (stopped ?
        QString ("Search stopped after %1/%2 files; %3 occurrence(s) in %4 file(s).")
          .arg (p.completed).arg (p.total).arg (p.hits).arg (p.matchedFiles) :
        QString ("%1 occurrence(s) in %2 file(s); structurally inspected %3 files.")
          .arg (p.hits).arg (p.matchedFiles).arg (p.completed));
      std::stable_sort (
        collected.begin (), collected.end (),
        [] (const WikilinkSearchResult& a, const WikilinkSearchResult& b) {
          if (a.titleMatchScore != b.titleMatchScore)
            return a.titleMatchScore > b.titleMatchScore;
          if (a.exact != b.exact) return a.exact;
          if (a.exact) return false;
          if (a.score != b.score) return a.score > b.score;
          int pathOrder= QString::compare (a.relPath, b.relPath,
                                          Qt::CaseSensitive);
          if (pathOrder != 0) return pathOrder < 0;
          return path_less (a.hitStart, b.hitStart);
        });
      for (const WikilinkSearchResult& hit: collected) addResult (hit);
      if (resultList->count () > 0) {
        resultList->setCurrentRow (0);
        resultList->setFocus ();
      }
      else queryEdit->setFocus ();
      finishSearch ();
    });
}

void
WikilinkSearchPage::addResult (const WikilinkSearchResult& result) {
  results.push_back (result);
  QListWidgetItem* item= new QListWidgetItem (
    QString ("%1 (%2)").arg (result.relPath).arg (result.occurrence));
  item->setData (WikilinkIndexRole, (int) results.size () - 1);
  QString tooltip= QString ("%1\nOccurrence %2 of %3")
      .arg (result.relPath)
      .arg (result.occurrence)
      .arg (result.fileHits);
  if (!result.exact)
    tooltip += QString ("\nFuzzy match: %1%").arg (result.score, 0, 'f', 1);
  item->setToolTip (tooltip);
  resultList->addItem (item);
}

void
WikilinkSearchPage::updatePreview (QListWidgetItem* current) {
  currentAnchors.clear ();
  anchorList->clear ();
  displayTouched= false;
  displayEdit->clear ();
  if (current == nullptr) {
    previewTitle->setText ("Select a search result to preview it.");
    preview.ensureCreated (previewHost);
    preview.setBody (tree (DOCUMENT, ""));
    return;
  }

  int index= current->data (WikilinkIndexRole).toInt ();
  if (index < 0 || index >= (int) results.size ()) return;

  const WikilinkSearchResult& result= results[index];
  previewTitle->setText (
    QString ("%1  (%2 of %3)")
      .arg (result.relPath)
      .arg (result.occurrence)
      .arg (result.fileHits));

  try {
    tree body= import_body_for_preview (result.file);
    int first= 0, last= 0;
    preview.ensureCreated (previewHost);
    preview.setBody (
      build_preview_from_body (body, result.hitStart, &first, &last));

    std::vector<WikilinkAnchorEntry> allAnchors;
    collect_anchors (body, path (), allAnchors);
    std::vector<TransclusionAnchorPair> pairs=
      collect_transclusion_pairs (allAnchors);
    int enclosing= enclosing_anchor_pair_index (pairs, result.hitStart);
    if (enclosing >= 0) {
      int upper= pairs[enclosing].upperIndex;
      if (upper >= 0 && upper < (int) allAnchors.size () &&
          is_wikilink_anchor (allAnchors[upper].anchor))
        currentAnchors.push_back (allAnchors[upper]);
    }
    std::vector<TransclusionAnchorPair> headings=
      collect_heading_anchor_targets (body, path ());
    int heading= heading_anchor_target_index (headings, result.hitStart);
    if (heading >= 0) {
      WikilinkAnchorEntry anchor;
      anchor.anchor= headings[heading].upper;
      anchor.where= headings[heading].upperWhere;
      currentAnchors.push_back (anchor);
    }
    for (const WikilinkAnchorEntry& a: allAnchors) {
      int top= path_top_index (a.where);
      bool duplicate= false;
      for (const WikilinkAnchorEntry& current: currentAnchors)
        if (current.where == a.where) {
          duplicate= true;
          break;
        }
      if (!duplicate && top >= first && top < last &&
          is_wikilink_anchor (a.anchor))
        currentAnchors.push_back (a);
    }
  }
  catch (...) {
    preview.ensureCreated (previewHost);
    preview.setBody (tree (DOCUMENT, "Preview unavailable."));
  }

  if (currentAnchors.empty ()) {
    QListWidgetItem* item= new QListWidgetItem ("No usable { anchor in this preview.");
    item->setFlags (item->flags () & ~Qt::ItemIsEnabled);
    anchorList->addItem (item);
    return;
  }

  for (int i=0; i<(int) currentAnchors.size (); i++) {
    QListWidgetItem* item= new QListWidgetItem (currentAnchors[i].anchor);
    item->setData (WikilinkIndexRole, i);
    anchorList->addItem (item);
  }
  if (anchorList->count () > 0) anchorList->setCurrentRow (0);
  updateDefaultDisplayText ();
}

void
WikilinkSearchPage::updateDefaultDisplayText () {
  if (displayTouched) return;
  QListWidgetItem* item= anchorList->currentItem ();
  if (item == nullptr || !(item->flags () & Qt::ItemIsEnabled)) {
    displayEdit->clear ();
    return;
  }
  int anchorIndex= item->data (WikilinkIndexRole).toInt ();
  if (anchorIndex < 0 || anchorIndex >= (int) currentAnchors.size ()) {
    displayEdit->clear ();
    return;
  }
  QListWidgetItem* resultItem= resultList->currentItem ();
  int resultIndex= resultItem == nullptr ? -1 :
    resultItem->data (WikilinkIndexRole).toInt ();
  QString relPath= resultIndex >= 0 && resultIndex < (int) results.size ()
    ? results[resultIndex].relPath : QString ();
  QString text= default_wikilink_display_text (
    relPath, currentAnchors[anchorIndex].anchor);
  displayEdit->setText (text);
}

bool
WikilinkSearchPage::chooseAnchorItem (QListWidgetItem* item) {
  if (item == nullptr || !(item->flags () & Qt::ItemIsEnabled)) return false;
  QListWidgetItem* resultItem= resultList->currentItem ();
  if (resultItem == nullptr) return false;
  int resultIndex= resultItem->data (WikilinkIndexRole).toInt ();
  int anchorIndex= item->data (WikilinkIndexRole).toInt ();
  if (resultIndex < 0 || resultIndex >= (int) results.size () ||
      anchorIndex < 0 || anchorIndex >= (int) currentAnchors.size ())
    return false;

  const WikilinkSearchResult& result= results[resultIndex];
  QString anchor= currentAnchors[anchorIndex].anchor;
  QString text= displayEdit->text ().trimmed ();
  if (text.isEmpty ())
    text= default_wikilink_display_text (result.relPath, anchor);
  if (text.isEmpty ()) text= anchor;

  QTMVaultWikilinkWizard* w=
    static_cast<QTMVaultWikilinkWizard*> (wizard ());
  w->setResult (result.relPath, anchor, file_display_stem (result.relPath),
                anchor, text);
  return true;
}

void
WikilinkSearchPage::acceptAnchorItem (QListWidgetItem* item) {
  if (!chooseAnchorItem (item)) return;
  QTMVaultWikilinkWizard* w=
    static_cast<QTMVaultWikilinkWizard*> (wizard ());
  QTimer::singleShot (0, w, [w] () { w->accept (); });
}

bool
WikilinkSearchPage::validatePage () {
  QTMVaultWikilinkWizard* w=
    static_cast<QTMVaultWikilinkWizard*> (wizard ());
  if (w->resultAccepted) return true;
  if (chooseAnchorItem (anchorList->currentItem ())) return true;
  QMessageBox::information (this, "Insert wikilink",
                            "Click a usable { anchor in the search preview first.");
  return false;
}

QTMVaultWikilinkWizard::QTMVaultWikilinkWizard (QWidget* parent)
  : QWizard (parent), filesLoaded (false), filesLoadScheduled (false),
    resultAccepted (false) {
  setWindowTitle ("Insert Wikilink");
  resize (1220, 780);
  setOption (QWizard::NoBackButtonOnStartPage, true);

  modePage= new WikilinkModePage (this);
  filePage= new WikilinkFilePage (this);
  anchorPage= new WikilinkAnchorPage (this);
  searchPage= new WikilinkSearchPage (this);
  artifactPage= new QTMVaultArtifactPage (
    QTMVaultArtifactUsage::Wikilink, wikilink_search_case_pref,
    wikilink_search_fuzzy_pref, this);
  artifactPage->setSelectionHandler (
    [this] (const QTMVaultArtifactSelection& selection) {
      QString display= default_wikilink_display_text (
        selection.relative_path, selection.upper_anchor);
      if (display.isEmpty ()) display= selection.display_text;
      setResult (selection.relative_path, selection.upper_anchor,
                 file_display_stem (selection.relative_path),
                 selection.upper_anchor, display);
    });

  setPage (WikilinkModePageId, modePage);
  setPage (WikilinkFilePageId, filePage);
  setPage (WikilinkAnchorPageId, anchorPage);
  setPage (WikilinkSearchPageId, searchPage);
  setPage (WikilinkArtifactPageId, artifactPage);
  setStartId (WikilinkModePageId);
}

void
QTMVaultWikilinkWizard::showEvent (QShowEvent* event) {
  QWizard::showEvent (event);
  scheduleLoadFiles ();
}

void
QTMVaultWikilinkWizard::loadFiles () {
  files= load_vault_link_files ();
  filesLoaded= true;
}

void
QTMVaultWikilinkWizard::scheduleLoadFiles () {
  if (filesLoaded || filesLoadScheduled) return;
  filesLoadScheduled= true;
  QTimer::singleShot (0, this, [this] () {
    if (filesLoaded) return;
    loadFiles ();
    if (filePage != nullptr) filePage->updateList ();
  });
}

void
QTMVaultWikilinkWizard::setResult (const QString& relPath,
                                   const QString& anchor,
                                   const QString& fileHint2,
                                   const QString& anchorHint2,
                                   const QString& displayText2) {
  selectedRelPath= relPath;
  selectedAnchor= anchor;
  fileHint= fileHint2;
  anchorHint= anchorHint2;
  displayText= displayText2;
  resultAccepted= true;
}

bool
QTMVaultWikilinkWizard::selectFileFromPage () {
  QListWidgetItem* item= filePage->fileList->currentItem ();
  if (item == nullptr && filePage->fileList->count () > 0)
    item= filePage->fileList->item (0);
  if (item == nullptr) {
    QMessageBox::information (this, "Insert wikilink",
                              "Select a file first.");
    return false;
  }

  int index= item->data (WikilinkIndexRole).toInt ();
  if (index < 0 || index >= (int) files.size ()) return false;

  selectedRelPath= files[index].relPath;
  selectedFileUrl= files[index].file;
  QString typed= filePage->searchEdit->text ().trimmed ();
  fileHint= typed.isEmpty () ? files[index].stem : typed;
  selectedAnchor.clear ();
  anchorHint.clear ();
  displayText.clear ();
  return true;
}

bool
QTMVaultWikilinkWizard::finishFileFirst () {
  QListWidgetItem* item= anchorPage->anchorList->currentItem ();
  if (item == nullptr && anchorPage->anchorList->count () > 0)
    item= anchorPage->anchorList->item (0);

  QString anchor;
  if (item != nullptr) anchor= item->data (WikilinkPayloadRole).toString ();

  QString typedAnchor= anchorPage->searchEdit->text ().trimmed ();
  QString hint= anchor.isEmpty () ? typedAnchor : anchor;
  QString text= anchorPage->displayEdit->text ().trimmed ();
  if (text.isEmpty ())
    text= default_wikilink_display_text (selectedRelPath, anchor);
  if (text.isEmpty ()) text= anchor;

  setResult (selectedRelPath, anchor, fileHint, hint, text);
  return true;
}

tree
QTMVaultWikilinkWizard::getResult () const {
  if (!resultAccepted) return UNINIT;
  tree res (TUPLE);
  res << tree (from_qstring (selectedRelPath));
  res << tree (from_qstring (selectedAnchor));
  res << tree (from_qstring (fileHint));
  res << tree (from_qstring (anchorHint));
  res << tree (from_qstring (displayText));
  return res;
}

} // namespace

tree
qtm_vault_choose_wikilink (QWidget* parent) {
  QTMVaultWikilinkWizard wizard (parent);
  if (wizard.exec () == QDialog::Accepted) return wizard.getResult ();
  return UNINIT;
}
