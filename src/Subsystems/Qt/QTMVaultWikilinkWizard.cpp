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
#include "QTMVaultAnchorModel.hpp"
#include "QTMVaultLinkModel.hpp"
#include "QTMVaultPreviewBuilder.hpp"
#include "QTMVaultPreviewWidget.hpp"
#include "QTMVaultSearch.hpp"
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
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QShowEvent>
#include <QSizePolicy>
#include <QSplitter>
#include <QStringListModel>
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

static int
path_top_index (path p) {
  if (is_nil (p)) return 0;
  return p->item;
}

enum WikilinkWizardPageId {
  WikilinkModePageId= 0,
  WikilinkFilePageId= 1,
  WikilinkAnchorPageId= 2,
  WikilinkSearchPageId= 3
};

class QTMVaultWikilinkWizard;

class WikilinkModePage : public QWizardPage {
public:
  WikilinkModePage (QWidget* parent= nullptr);
  int nextId () const override;

  QRadioButton* fileFirstRadio;
  QRadioButton* searchRadio;
};

class WikilinkFilePage : public QWizardPage {
public:
  WikilinkFilePage (QWidget* parent= nullptr);
  void initializePage () override;
  bool validatePage () override;
  bool eventFilter (QObject* watched, QEvent* event) override;

  void updateList ();
  void moveSelection (int delta);
  void completeFromSelection ();

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
  bool caseInsensitiveSearch () const;
  void startSearch ();
  int  searchFile (url u, const tree& query,
                   std::vector<WikilinkSearchResult>& hits) const;
  void addResult (const WikilinkSearchResult& result);
  void updatePreview (QListWidgetItem* current);
  void updateDefaultDisplayText ();
  bool chooseAnchorItem (QListWidgetItem* item);
  void acceptAnchorItem (QListWidgetItem* item);

  QLineEdit*   queryEdit;
  QLineEdit*   namespaceEdit;
  QStringListModel* namespaceModel;
  QComboBox*   enunciationCombo;
  QCheckBox*   caseInsensitiveCheck;
  QPushButton* searchButton;
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

private:
  void loadFiles ();
  void scheduleLoadFiles ();
};

WikilinkModePage::WikilinkModePage (QWidget* parent)
  : QWizardPage (parent) {
  setTitle ("Insert wikilink");
  setSubTitle ("Choose how to locate the target.");

  fileFirstRadio= new QRadioButton ("Locate a file first", this);
  searchRadio= new QRadioButton ("Locate by search", this);
  fileFirstRadio->setChecked (true);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->addWidget (fileFirstRadio);
  layout->addWidget (searchRadio);
  layout->addStretch ();
}

int
WikilinkModePage::nextId () const {
  return searchRadio->isChecked () ? WikilinkSearchPageId :
    WikilinkFilePageId;
}

WikilinkFilePage::WikilinkFilePage (QWidget* parent)
  : QWizardPage (parent) {
  setTitle ("Select a file");
  setSubTitle ("Type to filter vault files, then press Enter or Next.");

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

void
WikilinkFilePage::completeFromSelection () {
  QListWidgetItem* item= fileList->currentItem ();
  if (item == nullptr && fileList->count () > 0) item= fileList->item (0);
  if (item == nullptr) return;
  QString completion= item->data (WikilinkCompletionRole).toString ();
  if (completion.isEmpty ()) return;
  searchEdit->setText (completion);
  searchEdit->setCursorPosition (completion.length ());
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
    if (key->key () == Qt::Key_Return || key->key () == Qt::Key_Enter) {
      wizard ()->next ();
      return true;
    }
    if (key->key () == Qt::Key_Tab) {
      if (key->modifiers () & Qt::ShiftModifier) return true;
      completeFromSelection ();
      return true;
    }
    if (key->key () == Qt::Key_Backtab) return true;
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
  anchorList= new QListWidget (this);
  anchorList->setAlternatingRowColors (true);
  anchorList->setTabKeyNavigation (false);
  anchorList->setMinimumWidth (500);
  displayEdit= new QLineEdit (this);
  previewTitle= new QLabel ("Select an anchor to preview it.", this);
  previewHost= new QWidget (this);
  previewHost->setMinimumHeight (360);
  previewHost->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
  QVBoxLayout* previewHostLayout= new QVBoxLayout (previewHost);
  previewHostLayout->setContentsMargins (0, 0, 0, 0);

  QWidget* left= new QWidget (this);
  QVBoxLayout* leftLayout= new QVBoxLayout (left);
  leftLayout->setContentsMargins (0, 0, 0, 0);
  leftLayout->addWidget (new QLabel ("Anchor:", this));
  leftLayout->addWidget (searchEdit);
  leftLayout->addWidget (anchorList, 1);
  leftLayout->addWidget (new QLabel ("Display text:", this));
  leftLayout->addWidget (displayEdit);

  QWidget* right= new QWidget (this);
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
  setSubTitle ("Target file: " + w->selectedRelPath);

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
  QString query= searchEdit->text ().trimmed ().toLower ();

  QListWidgetItem* whole= new QListWidgetItem ("(whole file)");
  whole->setData (WikilinkIndexRole, -1);
  whole->setData (WikilinkPayloadRole, QString ());
  anchorList->addItem (whole);

  std::vector<std::pair<int,int> > matches;
  for (int i=0; i<(int) anchors.size (); i++) {
    int score= fuzzy_score (anchors[i].anchor, query);
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
  QString text= anchor.isEmpty () ? file_display_stem (w->selectedRelPath) :
    clean_anchor_display (anchor);
  displayEdit->setText (text);
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
  namespaceEdit= new QLineEdit (this);
  namespaceEdit->setPlaceholderText ("All namespaces");
  namespaceEdit->setClearButtonEnabled (true);
  namespaceEdit->setMinimumWidth (300);
  namespaceModel= new QStringListModel (this);
  QCompleter* namespaceCompleter= new QCompleter (namespaceModel, this);
  namespaceCompleter->setCaseSensitivity (Qt::CaseInsensitive);
  namespaceCompleter->setFilterMode (Qt::MatchContains);
  namespaceEdit->setCompleter (namespaceCompleter);

  enunciationCombo= new QComboBox (this);
  enunciationCombo->addItem ("Not required", "");
  for (const WikilinkEnunciationFilterEntry& entry: wikilink_enunciation_filters)
    enunciationCombo->addItem (entry.label, entry.tag);
  enunciationCombo->setMinimumWidth (190);
  caseInsensitiveCheck= new QCheckBox ("Case-insensitive", this);
  caseInsensitiveCheck->setChecked (
    get_preference (wikilink_search_case_pref, "off") == "on");

  searchButton= new QPushButton ("Search", this);
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
  previewHost= new QWidget (this);
  previewHost->setMinimumHeight (360);
  previewHost->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
  QVBoxLayout* previewHostLayout= new QVBoxLayout (previewHost);
  previewHostLayout->setContentsMargins (0, 0, 0, 0);

  QHBoxLayout* searchRow= new QHBoxLayout ();
  searchRow->addWidget (queryEdit, 1);
  searchRow->addWidget (searchButton);

  QHBoxLayout* filtersRow= new QHBoxLayout ();
  filtersRow->addWidget (new QLabel ("Namespace:", this));
  filtersRow->addWidget (namespaceEdit);
  filtersRow->addSpacing (12);
  filtersRow->addWidget (new QLabel ("Enunciation:", this));
  filtersRow->addWidget (enunciationCombo);
  filtersRow->addSpacing (12);
  filtersRow->addWidget (caseInsensitiveCheck);
  filtersRow->addStretch ();

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
  layout->addLayout (filtersRow);
  layout->addWidget (statusLabel);
  layout->addWidget (progress);
  layout->addWidget (splitter, 1);
  setTabOrder (queryEdit, searchButton);
  setTabOrder (searchButton, namespaceEdit);
  setTabOrder (namespaceEdit, enunciationCombo);
  setTabOrder (enunciationCombo, caseInsensitiveCheck);
  setTabOrder (caseInsensitiveCheck, resultList);
  setTabOrder (resultList, anchorList);
  setTabOrder (anchorList, displayEdit);
  setTabOrder (displayEdit, insertButton);

  connect (searchButton, &QPushButton::clicked,
           this, [this] () { startSearch (); });
  connect (queryEdit, &QLineEdit::returnPressed,
           this, [this] () { startSearch (); });
  connect (caseInsensitiveCheck, &QCheckBox::toggled,
           this, [] (bool on) {
             set_preference (wikilink_search_case_pref,
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
  QString current= namespaceEdit == nullptr ? QString () :
    namespaceEdit->text ().trimmed ();

  QStringList names;
  string error;
  athena_namespace_refresh_derived (error);
  for (const athena_namespace_definition& ns: athena_namespaces_list ())
    names << to_qstring (ns.name);
  names.removeDuplicates ();
  names.sort (Qt::CaseInsensitive);
  namespaceModel->setStringList (names);

  if (!current.isEmpty () && !names.contains (current, Qt::CaseSensitive))
    namespaceEdit->setText (current);
}

QString
WikilinkSearchPage::selectedNamespace () const {
  return namespaceEdit == nullptr ? QString () : namespaceEdit->text ().trimmed ();
}

QString
WikilinkSearchPage::selectedEnunciation () const {
  if (enunciationCombo == nullptr) return QString ();
  return enunciationCombo->currentData ().toString ().trimmed ();
}

bool
WikilinkSearchPage::caseInsensitiveSearch () const {
  return caseInsensitiveCheck != nullptr && caseInsensitiveCheck->isChecked ();
}

int
WikilinkSearchPage::searchFile (url u, const tree& query,
                                std::vector<WikilinkSearchResult>& hits) const {
  try {
    tree body= import_body_for_preview (u);
    int oldMode= set_access_mode (DRD_ACCESS_SOURCE);
    std::vector<range_set> hitRanges;
    try {
      QString enunciation= selectedEnunciation ();
      bool caseInsensitive= caseInsensitiveSearch ();
      if (enunciation.isEmpty ())
        append_search_hits (hitRanges, body, query, path (), 200,
                            caseInsensitive);
      else
        collect_enunciation_hits (hitRanges, body, query,
                                  from_qstring (enunciation), path (), 200,
                                  caseInsensitive);
    }
    catch (...) {
      set_access_mode (oldMode);
      throw;
    }
    set_access_mode (oldMode);

    int hitCount= 0;
    for (const range_set& sels: hitRanges) hitCount += N(sels) / 2;
    if (hitCount <= 0) return 0;

    url root= vault_get_root ();
    QString rel= to_qstring (as_unix_string (delta (root * url (""), u)));
    int occurrence= 1;
    for (const range_set& sels: hitRanges) {
      int rangeHits= N(sels) / 2;
      for (int i=0; i<rangeHits; i++) {
        WikilinkSearchResult result;
        result.relPath= rel;
        result.file= u;
        result.occurrence= occurrence++;
        result.fileHits= hitCount;
        result.hitStart= sels[2*i];
        result.hitEnd= sels[2*i + 1];
        hits.push_back (result);
      }
    }
    return hitCount;
  }
  catch (...) {
    return 0;
  }
}

void
WikilinkSearchPage::startSearch () {
  searchButton->setEnabled (false);
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
    searchButton->setEnabled (true);
    return;
  }

  tree query= tree (from_qstring (queryText));
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
    athena_namespace_definition def;
    if (!athena_namespace_get (from_qstring (ns), def)) {
      QMessageBox::warning (this, "Insert wikilink",
                            "Unknown namespace: " + ns);
      searchButton->setEnabled (true);
      return;
    }
    std::vector<athena_namespace_match> members=
      athena_namespace_members (from_qstring (ns), error);
    if (error != "")
      QMessageBox::warning (this, "Insert wikilink",
                            "Namespace warning: " + to_qstring (error));
    std::set<std::string> seen;
    for (const athena_namespace_match& m: members) {
      string suf= suffix (m.file);
      if (suf != "ath" && suf != "tm") continue;
      std::string key= to_qstring (concretize (m.file)).toStdString ();
      if (!seen.insert (key).second) continue;
      files.push_back (m.file);
    }
  }

  std::sort (files.begin (), files.end (),
             [] (const url& a, const url& b) {
               return as_unix_string (a) < as_unix_string (b);
             });

  int matchedFiles= 0;
  progress->setRange (0, (int) files.size ());
  progress->setValue (0);
  int scanned= 0;
  for (const url& file: files) {
    std::vector<WikilinkSearchResult> fileHits;
    if (searchFile (file, query, fileHits) > 0) {
      matchedFiles++;
      for (const WikilinkSearchResult& hit: fileHits)
        addResult (hit);
    }
    scanned++;
    progress->setValue (scanned);
    statusLabel->setText (
      QString ("Searching %1/%2 files; %3 occurrence(s) in %4 file(s).")
        .arg (scanned)
        .arg ((int) files.size ())
        .arg ((int) results.size ())
        .arg (matchedFiles));
    if ((scanned % 8) == 0)
      QApplication::processEvents (QEventLoop::ExcludeUserInputEvents);
  }

  statusLabel->setText (
    QString ("%1 occurrence(s) in %2 file(s), out of %3 scanned file(s). Click a { anchor below to insert.")
      .arg ((int) results.size ())
      .arg (matchedFiles)
      .arg ((int) files.size ()));
  if (resultList->count () > 0) resultList->setCurrentRow (0);
  searchButton->setEnabled (true);
}

void
WikilinkSearchPage::addResult (const WikilinkSearchResult& result) {
  results.push_back (result);
  QListWidgetItem* item= new QListWidgetItem (
    QString ("%1 (%2)").arg (result.relPath).arg (result.occurrence));
  item->setData (WikilinkIndexRole, (int) results.size () - 1);
  item->setToolTip (
    QString ("%1\nOccurrence %2 of %3")
      .arg (result.relPath)
      .arg (result.occurrence)
      .arg (result.fileHits));
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
    for (const WikilinkAnchorEntry& a: allAnchors) {
      int top= path_top_index (a.where);
      if (top >= first && top < last && is_wikilink_anchor (a.anchor))
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
  QString text= clean_anchor_display (currentAnchors[anchorIndex].anchor);
  if (text.isEmpty ()) text= currentAnchors[anchorIndex].anchor;
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
  if (text.isEmpty ()) text= clean_anchor_display (anchor);
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

  setPage (WikilinkModePageId, modePage);
  setPage (WikilinkFilePageId, filePage);
  setPage (WikilinkAnchorPageId, anchorPage);
  setPage (WikilinkSearchPageId, searchPage);
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
  QTimer::singleShot (350, this, [this] () {
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
    text= anchor.isEmpty () ? file_display_stem (selectedRelPath) :
      clean_anchor_display (anchor);
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
