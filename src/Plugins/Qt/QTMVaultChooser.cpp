/******************************************************************************
* MODULE     : QTMVaultChooser.cpp
* DESCRIPTION: Qt vault chooser for Wikilinks
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMVaultChooser.hpp"
#include "QTMWidget.hpp"
#include "convert.hpp"
#include "converter.hpp"
#include "drd_mode.hpp"
#include "edit_interface.hpp"
#include "editor.hpp"
#include "link.hpp"
#include "message.hpp"
#include "namespaces.hpp"
#include "vault.hpp"
#include "new_buffer.hpp"
#include "new_view.hpp"
#include "renderer.hpp"
#include "server.hpp"
#include "tm_buffer.hpp"
#include "tm_window.hpp"
#include "qt_utilities.hpp"
#include "qt_gui.hpp"
#include "qt_simple_widget.hpp"
#include "qt_widget.hpp"
#include "tree_search.hpp"
#include <QKeyEvent>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSizePolicy>
#include <QShowEvent>
#include <QSplitter>
#include <QStringListModel>
#include <QTimer>
#include <QVariant>
#include <QWizard>
#include <QWizardPage>
#include <algorithm>
#include <set>
#include <vector>

QTMVaultChooser::QTMVaultChooser (QWidget* parent, bool transcludeMode2)
  : QDialog (parent), state (SELECT_FILE), transcludeMode (transcludeMode2),
    selectionChangedByArrows (false), resultAccepted (false)
{
  setWindowTitle ("Wikilink Chooser");
  resize (600, 400);

  layout = new QVBoxLayout (this);
  prompt = new QLabel ("Search File:", this);
  layout->addWidget (prompt);

  searchEdit = new QLineEdit (this);
  layout->addWidget (searchEdit);

  resultList = new QListWidget (this);
  layout->addWidget (resultList);

  allFiles = vault_get_all_files ();
  
  // Sort files by mtime initially?
  // User asked for "recently visited first manner, if possible"
  // For now let's just use what vault_get_all_files returns.
  
  updateList ();

  connect (searchEdit, SIGNAL (textChanged (const QString&)), this, SLOT (onTextChanged (const QString&)));
  connect (searchEdit, SIGNAL (returnPressed ()), this, SLOT (onReturnPressed ()));
  connect (resultList, SIGNAL (itemDoubleClicked (QListWidgetItem*)), this, SLOT (onItemDoubleClicked (QListWidgetItem*)));
  
  searchEdit->setFocus ();
}

QTMVaultChooser::~QTMVaultChooser () {}

void
QTMVaultChooser::keyPressEvent (QKeyEvent* event) {
  if (event->key () == Qt::Key_Up || event->key () == Qt::Key_Down) {
    selectionChangedByArrows = true;
    QApplication::sendEvent (resultList, event);
    return;
  }
  QDialog::keyPressEvent (event);
}

bool
QTMVaultChooser::fuzzyMatch (const QString& str, const QString& hint) {
  if (hint.isEmpty()) return true;
  int hintIdx = 0;
  QString lStr = str.toLower();
  QString lHint = hint.toLower();
  for (int i = 0; i < lStr.length() && hintIdx < lHint.length(); ++i) {
    if (lStr[i] == lHint[hintIdx]) {
      hintIdx++;
    }
  }
  return hintIdx == lHint.length();
}

void
QTMVaultChooser::updateList () {
  resultList->clear ();
  QString hint = searchEdit->text ();
  
  if (state == SELECT_FILE) {
    filterFiles (hint);
  } else if (state == SELECT_ANCHOR || state == SELECT_ANCHOR_BEGIN || state == SELECT_ANCHOR_END) {
    filterAnchors (hint);
  }
  
  if (resultList->count () > 0 && state == SELECT_FILE) {
    resultList->setCurrentRow (0);
  } else {
    resultList->setCurrentItem (nullptr);
  }
}

void
QTMVaultChooser::filterFiles (const QString& hint) {
  url root = vault_get_root ();
  for (int i = 0; i < N(allFiles); i++) {
    url rel = delta (root * url (""), allFiles[i]);
    string s = as_unix_string (rel);
    string suf = suffix (rel);
    if (suf == "tm" && N(s) > 3) s = s (0, N(s) - 3);
    else if (suf == "ath" && N(s) > 4) s = s (0, N(s) - 4);
    QString relNoExt = to_qstring (s);
    if (fuzzyMatch (relNoExt, hint)) {
      resultList->addItem (to_qstring (as_unix_string (rel)));
    }
  }
}

void
QTMVaultChooser::filterAnchors (const QString& hint) {
  for (int i = 0; i < N(allAnchors); i++) {
    QString a = to_qstring (allAnchors[i]);
    if (fuzzyMatch (a, hint)) {
      resultList->addItem (a);
    }
  }
}

void
QTMVaultChooser::onTextChanged (const QString& text) {
  updateList ();
}

void
QTMVaultChooser::onReturnPressed () {
  if (state == SELECT_FILE) {
    QListWidgetItem* item = resultList->currentItem ();
    if (item) {
      selectedRelPath = item->text ();
      if (selectionChangedByArrows) {
        QString s = selectedRelPath;
        if (s.endsWith (".tm")) s.chop (3);
        else if (s.endsWith (".ath")) s.chop (4);
        fileHint = s;
      } else {
        fileHint = searchEdit->text ();
      }
    } else return;
    
    url absUrl = vault_get_root () * url_unix (from_qstring (selectedRelPath));
    allAnchors = vault_get_anchors (absUrl);

    if (transcludeMode) {
      state = SELECT_ANCHOR_BEGIN;
      prompt->setText ("Search BEGIN Anchor:");
    } else {
      state = SELECT_ANCHOR;
      prompt->setText ("Search Anchor (optional):");
    }
    selectionChangedByArrows = false;
    searchEdit->clear ();
    updateList ();
    
  } else if (state == SELECT_ANCHOR) {
    QListWidgetItem* item = resultList->currentItem ();
    if (selectionChangedByArrows && item) {
      selectedAnchor = item->text ();
      anchorHint = selectedAnchor;
    } else {
      selectedAnchor = (item ? item->text () : "");
      anchorHint = searchEdit->text ();
    }
    
    state = SELECT_DISPLAY_TEXT;
    selectionChangedByArrows = false;
    prompt->setText ("Enter Display Text:");
    searchEdit->clear ();
    resultList->hide ();
    if (!selectedAnchor.isEmpty()) {
      searchEdit->setText (selectedAnchor);
    } else {
      QString s = selectedRelPath;
      if (s.endsWith (".tm")) s.chop (3);
      else if (s.endsWith (".ath")) s.chop (4);
      searchEdit->setText (s);
    }
    searchEdit->selectAll ();
    
  } else if (state == SELECT_ANCHOR_BEGIN) {
    QListWidgetItem* item = resultList->currentItem ();
    if (selectionChangedByArrows && item) {
      selectedAnchorBegin = item->text ();
    } else {
      selectedAnchorBegin = (item ? item->text () : "");
    }
    
    state = SELECT_ANCHOR_END;
    prompt->setText ("Search END Anchor:");
    searchEdit->clear ();
    updateList ();

  } else if (state == SELECT_ANCHOR_END) {
    QListWidgetItem* item = resultList->currentItem ();
    if (selectionChangedByArrows && item) {
      selectedAnchorEnd = item->text ();
      anchorHint = selectedAnchorEnd; 
    } else {
      selectedAnchorEnd = (item ? item->text () : "");
      anchorHint = searchEdit->text ();
    }
    
    resultAccepted = true;
    accept ();

  } else if (state == SELECT_DISPLAY_TEXT) {
    displayText = searchEdit->text ();
    resultAccepted = true;
    accept ();
  }
}

void
QTMVaultChooser::onItemDoubleClicked (QListWidgetItem* item) {
  onReturnPressed ();
}

tree
QTMVaultChooser::getResult () {
  if (!resultAccepted) return UNINIT;
  tree res (TUPLE);
  res << tree (from_qstring (selectedRelPath));
  if (transcludeMode) {
    res << tree (from_qstring (selectedAnchorBegin));
    res << tree (from_qstring (selectedAnchorEnd));
  } else {
    res << tree (from_qstring (selectedAnchor));
  }
  res << tree (from_qstring (fileHint));
  res << tree (from_qstring (anchorHint)); // anchorHint is the search string
  if (!transcludeMode) {
    res << tree (from_qstring (displayText));
  }
  return res;
}

namespace {

enum WikilinkWizardPageId {
  WikilinkModePageId= 0,
  WikilinkFilePageId= 1,
  WikilinkAnchorPageId= 2,
  WikilinkSearchPageId= 3
};

enum TransclusionWizardPageId {
  TransclusionModePageId= 10,
  TransclusionFilePageId= 11,
  TransclusionKindPageId= 12,
  TransclusionEnunciationPageId= 13,
  TransclusionUpperPageId= 14,
  TransclusionLowerPageId= 15,
  TransclusionSearchPageId= 16
};

enum WikilinkItemRole {
  WikilinkPayloadRole= Qt::UserRole,
  WikilinkIndexRole,
  WikilinkCompletionRole
};

struct WikilinkFileEntry {
  url     file;
  QString relPath;
  QString stem;
  QString searchText;
  int     mtime;
};

struct WikilinkAnchorEntry {
  QString anchor;
  path    where;
};

struct WikilinkSearchResult {
  QString relPath;
  url     file;
  int     occurrence;
  int     fileHits;
  path    hitStart;
  path    hitEnd;
};

struct TransclusionAnchorPair {
  QString upper;
  QString lower;
  path    upperWhere;
  path    lowerWhere;
  int     upperIndex;
  int     lowerIndex;
};

struct TransclusionSearchResult {
  QString relPath;
  url     file;
  QString upper;
  QString lower;
  path    upperWhere;
  path    lowerWhere;
  int     occurrence;
  int     fileHits;
};

struct WikilinkEnunciationFilterEntry {
  const char* label;
  const char* tag;
};

static const WikilinkEnunciationFilterEntry wikilink_enunciation_filters[]= {
  { "Theorem", "theorem" },
  { "Proposition", "proposition" },
  { "Lemma", "lemma" },
  { "Corollary", "corollary" },
  { "Axiom", "axiom" },
  { "Definition", "definition" },
  { "Conjecture", "conjecture" },
  { "Remark", "remark" },
  { "Note", "note" },
  { "Example", "example" },
  { "Warning", "warning" },
  { "Disambiguation", "disambiguation" },
  { "Question", "question" },
  { "Solution", "solution" },
  { "Solution*", "solution*" },
  { "Proof", "proof" },
  { "Alternative proof", "proof-alternative" },
  { "Standard proof", "proof-standard" }
};

static QString
strip_known_extension (QString s) {
  if (s.endsWith (".ath")) s.chop (4);
  else if (s.endsWith (".tm")) s.chop (3);
  return s;
}

static QString
file_display_stem (const QString& relPath) {
  return strip_known_extension (relPath.section ('/', -1));
}

static QString
clean_anchor_display (QString anchor) {
  anchor.replace ("{", "");
  anchor.replace ("}", "");
  return anchor.trimmed ();
}

static bool
is_wikilink_anchor (const QString& anchor) {
  return anchor.contains ("{");
}

static bool
is_upper_anchor (const QString& anchor) {
  return anchor.contains ("{");
}

static bool
is_lower_anchor (const QString& anchor) {
  return anchor.contains ("}");
}

static QString
anchor_pair_key (QString anchor) {
  anchor.replace ("{", "");
  anchor.replace ("}", "");
  return anchor.trimmed ();
}

static QString
anchor_pair_tag (QString anchor) {
  QString key= anchor_pair_key (anchor).toLower ();
  int pos= key.indexOf (":");
  return pos < 0 ? key : key.left (pos);
}

static QString
normalized_enunciation_tag (QString tag) {
  tag= tag.trimmed ().toLower ();
  if (tag == "solution*") return "solution";
  if (tag == "proof-alternative" || tag == "proof-standard") return "proof";
  return tag;
}

static bool
anchor_pair_matches_enunciation (const TransclusionAnchorPair& pair,
                                 const QString& tag) {
  QString normalized= normalized_enunciation_tag (tag);
  if (normalized.isEmpty ()) return false;
  return anchor_pair_tag (pair.upper) == normalized;
}

static int
fuzzy_subsequence_score (const QString& text, const QString& query) {
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

static int
fuzzy_score (const QString& text, const QString& query) {
  QString normalized= text.toLower ();
  if (query.isEmpty ()) return 0;
  if (normalized == query) return 100000;
  if (normalized.startsWith (query)) return 90000 - normalized.length ();
  if (normalized.contains (query)) return 80000 - normalized.length ();
  return fuzzy_subsequence_score (normalized, query);
}

static tree
import_body (url file) {
  tree t= import_tree (file, "texmacs");
  tree body= extract (t, "body");
  return is_empty (body) ? t : body;
}

static bool
preview_absolute_image_path (const string& path) {
  return path == "" || starts (path, "/") || starts (path, "~") ||
    starts (path, "$") || occurs ("://", path);
}

static string
preview_rebase_image_path (const string& path, url sourceDir) {
  if (preview_absolute_image_path (path)) return path;
  url absolute= sourceDir * url_unix (cork_to_utf8 (path));
  return utf8_to_cork (as_system_string (absolute));
}

static tree
rebase_preview_images (tree t, url sourceDir) {
  if (is_atomic (t)) return copy (t);

  tree r (L(t));
  for (int i=0; i<N(t); i++) {
    if (i == 0 && is_func (t, IMAGE) && is_atomic (t[i]))
      r << tree (preview_rebase_image_path (t[i]->label, sourceDir));
    else
      r << rebase_preview_images (t[i], sourceDir);
  }
  return r;
}

static tree
import_body_for_preview (url file) {
  return rebase_preview_images (import_body (file), head (file));
}

static int
path_top_index (path p) {
  if (is_nil (p)) return 0;
  return p->item;
}

static void
collect_anchors (tree t, path base, std::vector<WikilinkAnchorEntry>& out) {
  if (is_atomic (t)) return;
  if (is_func (t, LABEL, 1)) {
    WikilinkAnchorEntry e;
    e.anchor= to_qstring (tree_as_string (t[0]));
    e.where= base;
    out.push_back (e);
  }
  for (int i=0; i<N(t); i++)
    collect_anchors (t[i], base * i, out);
}

static void
append_search_hits (std::vector<range_set>& out, tree t, tree query,
                    path base, int limit) {
  if (limit <= 0) return;
  range_set sels= search (t, query, base, limit);
  if (N(sels) > 0) out.push_back (sels);
}

static void
collect_enunciation_hits (std::vector<range_set>& out, tree t, tree query,
                          const string& tag, path base, int limit) {
  if (limit <= 0 || is_atomic (t)) return;

  if (is_compound (t, tag)) {
    append_search_hits (out, t, query, base, limit);
    return;
  }

  for (int i=0; i<N(t); i++) {
    int found= 0;
    for (const range_set& sels: out) found += N(sels) / 2;
    if (found >= limit) return;
    collect_enunciation_hits (out, t[i], query, tag, base * i,
                              limit - found);
  }
}

static tree
build_preview_from_body (tree body, path focus, int* firstOut,
                         int* lastOut) {
  if (firstOut != nullptr) *firstOut= 0;
  if (lastOut != nullptr) *lastOut= 0;
  if (is_empty (body)) return tree (DOCUMENT, "");

  if (!is_func (body, DOCUMENT)) {
    if (firstOut != nullptr) *firstOut= 0;
    if (lastOut != nullptr) *lastOut= 1;
    return tree (DOCUMENT, compound ("marked", copy (body)));
  }

  if (N(body) == 0) return tree (DOCUMENT, "");

  int top= path_top_index (focus);
  if (top < 0 || top >= N(body)) top= 0;
  int first= std::max (0, top - 2);
  int last = std::min (N(body), top + 3);
  if (firstOut != nullptr) *firstOut= first;
  if (lastOut != nullptr) *lastOut= last;

  tree preview (DOCUMENT);
  for (int i= first; i<last; i++) {
    tree block= copy (body[i]);
    if (i == top) block= compound ("marked", block);
    preview << block;
  }
  return preview;
}

static std::vector<TransclusionAnchorPair>
collect_transclusion_pairs (const std::vector<WikilinkAnchorEntry>& anchors) {
  std::vector<TransclusionAnchorPair> pairs;
  for (int i=0; i<(int) anchors.size (); i++) {
    if (!is_upper_anchor (anchors[i].anchor)) continue;
    QString key= anchor_pair_key (anchors[i].anchor);
    if (key.isEmpty ()) continue;
    for (int j=i+1; j<(int) anchors.size (); j++) {
      if (!is_lower_anchor (anchors[j].anchor)) continue;
      if (anchor_pair_key (anchors[j].anchor) != key) continue;
      if (!path_less (anchors[i].where, anchors[j].where)) continue;
      TransclusionAnchorPair pair;
      pair.upper= anchors[i].anchor;
      pair.lower= anchors[j].anchor;
      pair.upperWhere= anchors[i].where;
      pair.lowerWhere= anchors[j].where;
      pair.upperIndex= i;
      pair.lowerIndex= j;
      pairs.push_back (pair);
      break;
    }
  }
  return pairs;
}

static tree
build_preview_from_anchor_range (tree body, path upper, path lower,
                                 int* firstOut= nullptr,
                                 int* lastOut= nullptr) {
  if (firstOut != nullptr) *firstOut= 0;
  if (lastOut != nullptr) *lastOut= 0;
  if (is_empty (body)) return tree (DOCUMENT, "");

  if (!is_func (body, DOCUMENT)) {
    if (firstOut != nullptr) *firstOut= 0;
    if (lastOut != nullptr) *lastOut= 1;
    return tree (DOCUMENT, compound ("marked", copy (body)));
  }

  if (N(body) == 0) return tree (DOCUMENT, "");
  int first= path_top_index (upper);
  int last = path_top_index (lower);
  if (first < 0 || first >= N(body)) first= 0;
  if (last < first || last >= N(body)) last= first;
  last++;
  if (firstOut != nullptr) *firstOut= first;
  if (lastOut != nullptr) *lastOut= last;

  tree preview (DOCUMENT);
  for (int i= first; i<last; i++) {
    tree block= copy (body[i]);
    if (i == first) block= compound ("marked", block);
    preview << block;
  }
  return preview;
}

class WikilinkPreview : public QObject {
public:
  WikilinkPreview (QObject* parent= nullptr)
    : QObject (parent),
      previewUrl (url (string ("tmfs://aux/wikilink-preview-") *
                       as_string (nextPreviewId++))),
      previewQtWidget (nullptr),
      previewViewportWidget (nullptr),
      previewSurfaceWidget (nullptr),
      previewTexmacsWidget (nullptr) {}

  ~WikilinkPreview () {
    destroyPreview ();
  }

  void destroyPreview () {
    if (!is_nil (previewWidget)) {
      try {
        send_destroy (previewWidget);
      }
      catch (...) {
        // If this is reached during Qt page destruction, the embedded close
        // path may already be unsafe.  Callers that complete the wizard should
        // destroy previews before accepting, while the pages are still intact.
      }
    }
    previewWidget= widget ();
    previewQtWidget= nullptr;
    previewViewportWidget= nullptr;
    previewSurfaceWidget= nullptr;
    previewTexmacsWidget= nullptr;
  }

  QWidget* ensureCreated (QWidget* parent) {
    if (previewQtWidget != nullptr) return previewQtWidget;
    if (parent == nullptr) return nullptr;

    tree doc (DOCUMENT, "");
    tree style= compound ("style", tuple ("generic"));
    previewWidget= texmacs_input_widget (doc, style, previewUrl);
    tm_buffer buf= concrete_buffer (previewUrl);
    if (!is_nil (buf)) buf->buf->read_only= true;

    QWidget* qwid= concrete (previewWidget)->as_qwidget (parent);
    previewQtWidget= qwid;
    locateWidgets ();
    installPreviewEventFilter (previewQtWidget);
    qwid->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
    set_zoom_factor (previewWidget,
                     get_retina_zoom () *
                       get_server ()->get_window_zoom_factor ());
    if (parent->layout () != nullptr)
      parent->layout ()->addWidget (qwid);
    qwid->show ();
    if (previewTexmacsWidget != nullptr) {
      previewTexmacsWidget->show ();
      if (previewTexmacsWidget->viewport () != nullptr)
        previewTexmacsWidget->viewport ()->show ();
      if (previewTexmacsWidget->surface () != nullptr)
        previewTexmacsWidget->surface ()->show ();
    }

    return qwid;
  }

  void setBody (tree body) {
    set_buffer_body (previewUrl, body);
    tm_buffer buf= concrete_buffer (previewUrl);
    if (!is_nil (buf)) buf->buf->read_only= true;
    notifyPreviewChanged ();
    refreshLayout ();
  }

  void refresh () {
    refreshLayout ();
  }

  bool eventFilter (QObject* watched, QEvent* event) override {
    if (event != nullptr && isPreviewWatchedObject (watched)) {
      switch (event->type ()) {
        case QEvent::ContextMenu:
          event->accept ();
          return true;
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        {
          QMouseEvent* mouse= static_cast<QMouseEvent*> (event);
          if (mouse->button () == Qt::RightButton ||
              (mouse->buttons () & Qt::RightButton) != 0) {
            event->accept ();
            return true;
          }
          break;
        }
        default:
          break;
      }
    }
    return QObject::eventFilter (watched, event);
  }

private:
  void notifyPreviewChanged () {
    array<url> views= buffer_to_views (previewUrl);
    for (int i=0; i<N(views); i++) {
      editor ed= view_to_editor (views[i]);
      if (!is_nil (ed))
        ed->notify_change (THE_TREE + THE_EXTENTS + THE_FREEZE);
    }
  }

  void applyPreviewChanges () {
    if (previewTexmacsWidget == nullptr) return;
    edit_interface_rep* ed=
      dynamic_cast<edit_interface_rep*> (previewTexmacsWidget->tm_widget ());
    if (ed != nullptr) ed->apply_changes ();
  }

  void locateWidgets () {
    previewTexmacsWidget= nullptr;
    previewViewportWidget= nullptr;
    previewSurfaceWidget= nullptr;
    if (previewQtWidget == nullptr) return;

    previewTexmacsWidget= qobject_cast<QTMWidget*> (previewQtWidget);
    if (previewTexmacsWidget == nullptr)
      previewTexmacsWidget= previewQtWidget->findChild<QTMWidget*> ();

    if (previewTexmacsWidget != nullptr) {
      previewTexmacsWidget->setContextMenuPolicy (Qt::NoContextMenu);
      if (QAbstractScrollArea* area=
            qobject_cast<QAbstractScrollArea*> (previewTexmacsWidget))
        previewViewportWidget= area->viewport ();
      previewSurfaceWidget= previewTexmacsWidget->surface ();
    }
  }

  void installPreviewEventFilter (QWidget* root) {
    if (root == nullptr) return;
    root->installEventFilter (this);
    root->setContextMenuPolicy (Qt::NoContextMenu);
    QList<QWidget*> children= root->findChildren<QWidget*> ();
    for (QWidget* child : children) {
      if (child == nullptr) continue;
      child->installEventFilter (this);
      child->setContextMenuPolicy (Qt::NoContextMenu);
    }
  }

  bool isPreviewWatchedObject (QObject* watched) const {
    for (QObject* obj= watched; obj != nullptr; obj= obj->parent ())
      if (obj == previewQtWidget) return true;
    return false;
  }

  void refreshLayoutNow () {
    if (previewQtWidget == nullptr) return;

    locateWidgets ();
    installPreviewEventFilter (previewQtWidget);
    previewQtWidget->show ();
    previewQtWidget->updateGeometry ();
    previewQtWidget->update ();

    QTMWidget* tmWidget= previewTexmacsWidget;
    if (tmWidget == nullptr || tmWidget->surface () == nullptr ||
        the_gui == nullptr)
      return;

    tmWidget->updateGeometry ();
    tmWidget->show ();
    tmWidget->update ();
    if (tmWidget->viewport () != nullptr) {
      tmWidget->viewport ()->show ();
      if (tmWidget->viewport ()->size ().isEmpty () &&
          !tmWidget->contentsRect ().size ().isEmpty ())
        tmWidget->viewport ()->setGeometry (tmWidget->contentsRect ());
      if (tmWidget->viewport ()->layout () != nullptr)
        tmWidget->viewport ()->layout ()->activate ();
    }
    if (tmWidget->surface ()->size ().isEmpty ()) {
      QSize fallback= tmWidget->viewport () == nullptr ? QSize () :
        tmWidget->viewport ()->size ();
      if (fallback.isEmpty ()) fallback= tmWidget->contentsRect ().size ();
      if (fallback.isEmpty ()) fallback= tmWidget->size ();
      if (!fallback.isEmpty ()) {
        tmWidget->surface ()->setMinimumSize (fallback);
        tmWidget->surface ()->resize (fallback);
      }
    }
    tmWidget->surface ()->show ();
    tmWidget->surface ()->updateGeometry ();
    tmWidget->surface ()->update ();

    the_gui->process_keyboard_focus (tmWidget->tm_widget (), true,
                                     texmacs_time ());

    coord2 size= from_qsize (tmWidget->surface ()->size ());
    if (size.x1 > 0 && size.x2 > 0)
      the_gui->process_resize (tmWidget->tm_widget (), size.x1, size.x2);
    the_gui->process_queued_events (4);
    applyPreviewChanges ();

    // The wikilink wizard is launched by a synchronous Scheme command.  While
    // its modal event loop is running, TeXmacs may still be inside
    // qt_gui_rep::update(), where force_update() only marks a pending repaint.
    // Repaint this embedded canvas synchronously so the preview is visible
    // before the wizard closes.
    qt_simple_widget_rep* simple= tmWidget->tm_widget ();
    if (simple != nullptr) simple->reset_all ();
    the_gui->force_update ();
    applyPreviewChanges ();
    qt_simple_widget_rep::repaint_all ();
    tmWidget->surface ()->repaint ();
  }

  void refreshLayout () {
    refreshLayoutNow ();
    QTimer::singleShot (0, this, [this] () { refreshLayoutNow (); });
  }

  static int nextPreviewId;

  widget     previewWidget;
  url        previewUrl;
  QWidget*   previewQtWidget;
  QWidget*   previewViewportWidget;
  QWidget*   previewSurfaceWidget;
  QTMWidget* previewTexmacsWidget;
};

int WikilinkPreview::nextPreviewId= 1;

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
  void startSearch ();
  int  searchFile (url u, const tree& query,
                   std::vector<WikilinkSearchResult>& hits) const;
  void addResult (const WikilinkSearchResult& result);
  void updatePreview (QListWidgetItem* current);
  void acceptAnchorItem (QListWidgetItem* item);

  QLineEdit*   queryEdit;
  QLineEdit*   namespaceEdit;
  QStringListModel* namespaceModel;
  QComboBox*   enunciationCombo;
  QPushButton* searchButton;
  QLabel*      statusLabel;
  QProgressBar* progress;
  QListWidget* resultList;
  QListWidget* anchorList;
  QLabel*      previewTitle;
  QWidget*     previewHost;
  WikilinkPreview preview;
  std::vector<WikilinkSearchResult> results;
  std::vector<WikilinkAnchorEntry> currentAnchors;
};

class QTMVaultWikilinkWizard : public QWizard {
public:
  QTMVaultWikilinkWizard (QWidget* parent= nullptr);

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
  bool    resultAccepted;

  WikilinkModePage*   modePage;
  WikilinkFilePage*   filePage;
  WikilinkAnchorPage* anchorPage;
  WikilinkSearchPage* searchPage;

private:
  void loadFiles ();
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
  QString query= searchEdit->text ().trimmed ().toLower ();

  std::vector<std::pair<int,int> > matches;
  for (int i=0; i<(int) w->files.size (); i++) {
    int score= fuzzy_score (w->files[i].searchText, query);
    if (score >= 0) matches.push_back (std::make_pair (-score, i));
  }
  std::sort (matches.begin (), matches.end (),
             [&] (const std::pair<int,int>& a,
                  const std::pair<int,int>& b) {
               if (a.first != b.first) return a.first < b.first;
               return w->files[a.second].relPath < w->files[b.second].relPath;
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

bool
WikilinkAnchorPage::eventFilter (QObject* watched, QEvent* event) {
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
WikilinkAnchorPage::validatePage () {
  QTMVaultWikilinkWizard* w=
    static_cast<QTMVaultWikilinkWizard*> (wizard ());
  return w->finishFileFirst ();
}

WikilinkSearchPage::WikilinkSearchPage (QWidget* parent)
  : QWizardPage (parent) {
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

  searchButton= new QPushButton ("Search", this);
  statusLabel= new QLabel (this);
  progress= new QProgressBar (this);
  progress->setRange (0, 1);
  progress->setValue (0);
  resultList= new QListWidget (this);
  resultList->setAlternatingRowColors (true);
  anchorList= new QListWidget (this);
  anchorList->setAlternatingRowColors (true);
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
  filtersRow->addStretch ();

  QWidget* left= new QWidget (this);
  QVBoxLayout* leftLayout= new QVBoxLayout (left);
  leftLayout->setContentsMargins (0, 0, 0, 0);
  leftLayout->addWidget (new QLabel ("Occurrences:", this));
  leftLayout->addWidget (resultList, 1);
  leftLayout->addWidget (new QLabel ("Usable wikilink anchors in preview:", this));
  leftLayout->addWidget (anchorList, 1);

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

  connect (searchButton, &QPushButton::clicked,
           this, [this] () { startSearch (); });
  connect (queryEdit, &QLineEdit::returnPressed,
           this, [this] () { startSearch (); });
  connect (resultList, &QListWidget::currentItemChanged,
           this, [this] (QListWidgetItem* current, QListWidgetItem*) {
             updatePreview (current);
           });
  connect (anchorList, &QListWidget::itemClicked,
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

int
WikilinkSearchPage::searchFile (url u, const tree& query,
                                std::vector<WikilinkSearchResult>& hits) const {
  try {
    tree body= import_body_for_preview (u);
    int oldMode= set_access_mode (DRD_ACCESS_SOURCE);
    std::vector<range_set> hitRanges;
    try {
      QString enunciation= selectedEnunciation ();
      if (enunciation.isEmpty ())
        append_search_hits (hitRanges, body, query, path (), 200);
      else
        collect_enunciation_hits (hitRanges, body, query,
                                  from_qstring (enunciation), path (), 200);
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
}

void
WikilinkSearchPage::acceptAnchorItem (QListWidgetItem* item) {
  if (item == nullptr || !(item->flags () & Qt::ItemIsEnabled)) return;
  QListWidgetItem* resultItem= resultList->currentItem ();
  if (resultItem == nullptr) return;
  int resultIndex= resultItem->data (WikilinkIndexRole).toInt ();
  int anchorIndex= item->data (WikilinkIndexRole).toInt ();
  if (resultIndex < 0 || resultIndex >= (int) results.size () ||
      anchorIndex < 0 || anchorIndex >= (int) currentAnchors.size ())
    return;

  const WikilinkSearchResult& result= results[resultIndex];
  QString anchor= currentAnchors[anchorIndex].anchor;
  QString text= clean_anchor_display (anchor);
  if (text.isEmpty ()) text= anchor;

  QTMVaultWikilinkWizard* w=
    static_cast<QTMVaultWikilinkWizard*> (wizard ());
  w->setResult (result.relPath, anchor, file_display_stem (result.relPath),
                anchor, text);
  QTimer::singleShot (0, w, [w] () { w->accept (); });
}

bool
WikilinkSearchPage::validatePage () {
  QTMVaultWikilinkWizard* w=
    static_cast<QTMVaultWikilinkWizard*> (wizard ());
  if (w->resultAccepted) return true;
  QMessageBox::information (this, "Insert wikilink",
                            "Click a usable { anchor in the search preview first.");
  return false;
}

QTMVaultWikilinkWizard::QTMVaultWikilinkWizard (QWidget* parent)
  : QWizard (parent), resultAccepted (false) {
  setWindowTitle ("Insert Wikilink");
  resize (1220, 780);
  setOption (QWizard::NoBackButtonOnStartPage, true);

  loadFiles ();

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
QTMVaultWikilinkWizard::loadFiles () {
  files.clear ();
  url root= vault_get_root ();
  array<url> all= vault_get_all_files ();
  for (int i=0; i<N(all); i++) {
    string suf= suffix (all[i]);
    if (suf != "ath" && suf != "tm") continue;
    url rel= delta (root * url (""), all[i]);
    QString relPath= to_qstring (as_unix_string (rel));
    WikilinkFileEntry e;
    e.file= all[i];
    e.relPath= relPath;
    e.stem= file_display_stem (relPath);
    e.searchText= strip_known_extension (relPath).toLower ();
    e.mtime= vault_get_mtime (all[i]);
    files.push_back (e);
  }
  std::sort (files.begin (), files.end (),
             [] (const WikilinkFileEntry& a,
                 const WikilinkFileEntry& b) {
               if (a.mtime != b.mtime) return a.mtime > b.mtime;
               return a.relPath < b.relPath;
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

class QTMVaultTransclusionWizard;

class TransclusionModePage : public QWizardPage {
public:
  TransclusionModePage (QWidget* parent= nullptr);
  int nextId () const override;

  QRadioButton* fileFirstRadio;
  QRadioButton* searchRadio;
};

class TransclusionFilePage : public QWizardPage {
public:
  TransclusionFilePage (QWidget* parent= nullptr);
  void initializePage () override;
  bool validatePage () override;
  bool eventFilter (QObject* watched, QEvent* event) override;

  void updateList ();
  void moveSelection (int delta);
  void completeFromSelection ();

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
  void startSearch ();
  int  searchFile (url u, const tree& query,
                   std::vector<TransclusionSearchResult>& hits) const;
  void addResult (const TransclusionSearchResult& result);
  void updatePreview (QListWidgetItem* current);
  bool acceptCurrentResult ();

  QLineEdit*   queryEdit;
  QLineEdit*   namespaceEdit;
  QStringListModel* namespaceModel;
  QComboBox*   enunciationCombo;
  QPushButton* searchButton;
  QLabel*      statusLabel;
  QProgressBar* progress;
  QListWidget* resultList;
  QLabel*      previewTitle;
  QWidget*     previewHost;
  WikilinkPreview preview;
  std::vector<TransclusionSearchResult> results;
};

class QTMVaultTransclusionWizard : public QWizard {
public:
  QTMVaultTransclusionWizard (QWidget* parent= nullptr);

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
  bool    resultAccepted;

  TransclusionModePage* modePage;
  TransclusionFilePage* filePage;
  TransclusionKindPage* kindPage;
  TransclusionEnunciationPage* enunciationPage;
  TransclusionUpperPage* upperPage;
  TransclusionLowerPage* lowerPage;
  TransclusionSearchPage* searchPage;

private:
  void loadFiles ();
};

TransclusionModePage::TransclusionModePage (QWidget* parent)
  : QWizardPage (parent) {
  setTitle ("Insert transclusion");
  setSubTitle ("Choose how to locate the material to transclude.");

  fileFirstRadio= new QRadioButton ("Locate a file first", this);
  searchRadio= new QRadioButton ("Locate by search", this);
  fileFirstRadio->setChecked (true);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->addWidget (fileFirstRadio);
  layout->addWidget (searchRadio);
  layout->addStretch ();
}

int
TransclusionModePage::nextId () const {
  return searchRadio->isChecked () ? TransclusionSearchPageId :
    TransclusionFilePageId;
}

TransclusionFilePage::TransclusionFilePage (QWidget* parent)
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
TransclusionFilePage::initializePage () {
  QWizardPage::initializePage ();
  updateList ();
  searchEdit->setFocus ();
}

void
TransclusionFilePage::updateList () {
  QTMVaultTransclusionWizard* w=
    static_cast<QTMVaultTransclusionWizard*> (wizard ());
  fileList->clear ();
  QString query= searchEdit->text ().trimmed ().toLower ();

  std::vector<std::pair<int,int> > matches;
  for (int i=0; i<(int) w->files.size (); i++) {
    int score= fuzzy_score (w->files[i].searchText, query);
    if (score >= 0) matches.push_back (std::make_pair (-score, i));
  }
  std::sort (matches.begin (), matches.end (),
             [&] (const std::pair<int,int>& a,
                  const std::pair<int,int>& b) {
               if (a.first != b.first) return a.first < b.first;
               return w->files[a.second].relPath < w->files[b.second].relPath;
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

void
TransclusionFilePage::completeFromSelection () {
  QListWidgetItem* item= fileList->currentItem ();
  if (item == nullptr && fileList->count () > 0) item= fileList->item (0);
  if (item == nullptr) return;
  QString completion= item->data (WikilinkCompletionRole).toString ();
  if (completion.isEmpty ()) return;
  searchEdit->setText (completion);
  searchEdit->setCursorPosition (completion.length ());
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
TransclusionFilePage::validatePage () {
  QTMVaultTransclusionWizard* w=
    static_cast<QTMVaultTransclusionWizard*> (wizard ());
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
  pairList= new QListWidget (this);
  pairList->setAlternatingRowColors (true);
  pairList->setMinimumWidth (500);
  previewTitle= new QLabel ("Select an enunciation to preview it.", this);
  previewHost= new QWidget (this);
  previewHost->setMinimumHeight (360);
  previewHost->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
  QVBoxLayout* previewHostLayout= new QVBoxLayout (previewHost);
  previewHostLayout->setContentsMargins (0, 0, 0, 0);

  QWidget* left= new QWidget (this);
  QVBoxLayout* leftLayout= new QVBoxLayout (left);
  leftLayout->setContentsMargins (0, 0, 0, 0);
  leftLayout->addWidget (new QLabel ("Enunciations:", this));
  leftLayout->addWidget (searchEdit);
  leftLayout->addWidget (pairList, 1);

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
  splitter->setSizes (QList<int> () << 520 << 800);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->addWidget (splitter, 1);

  searchEdit->installEventFilter (this);
  pairList->installEventFilter (this);
  connect (searchEdit, &QLineEdit::textChanged,
           this, [this] (const QString&) { updateList (); });
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
  setSubTitle ("Target file: " + w->selectedRelPath);
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
  QString query= searchEdit->text ().trimmed ().toLower ();
  std::vector<std::pair<int,int> > matches;
  for (int i=0; i<(int) pairs.size (); i++) {
    int score= fuzzy_score (pairs[i].upper, query);
    if (score >= 0) matches.push_back (std::make_pair (-score, i));
  }
  std::sort (matches.begin (), matches.end (),
             [&] (const std::pair<int,int>& a,
                  const std::pair<int,int>& b) {
               if (a.first != b.first) return a.first < b.first;
               return pairs[a.second].upper < pairs[b.second].upper;
             });
  for (auto m: matches) {
    QListWidgetItem* item= new QListWidgetItem (pairs[m.second].upper);
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

  // FIXME: arbitrary-anchor transclusion previews currently trigger a
  // nonfatal TeXmacs segfault popup when the modal wizard is closed.  The
  // preview renders correctly and the transclusion is inserted correctly; the
  // popup does not crash ATHENA and can be dismissed.  Attempts to explicitly
  // destroy or park the embedded texmacs_input_widget either crashed earlier or
  // broke insertion, so we keep the preview-enabled behavior until this widget
  // teardown path is fixed properly.

  searchEdit= new QLineEdit (this);
  searchEdit->setPlaceholderText ("Filter anchors");
  anchorList= new QListWidget (this);
  anchorList->setAlternatingRowColors (true);
  anchorList->setMinimumWidth (500);
  previewTitle= new QLabel ("Select an anchor to preview it.", this);
  previewHost= new QWidget (this);
  previewHost->setMinimumHeight (360);
  previewHost->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
  QVBoxLayout* previewHostLayout= new QVBoxLayout (previewHost);
  previewHostLayout->setContentsMargins (0, 0, 0, 0);

  QWidget* left= new QWidget (this);
  QVBoxLayout* leftLayout= new QVBoxLayout (left);
  leftLayout->setContentsMargins (0, 0, 0, 0);
  leftLayout->addWidget (new QLabel ("Upper bound:", this));
  leftLayout->addWidget (searchEdit);
  leftLayout->addWidget (anchorList, 1);

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
  splitter->setSizes (QList<int> () << 520 << 800);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->addWidget (splitter, 1);

  searchEdit->installEventFilter (this);
  anchorList->installEventFilter (this);
  connect (searchEdit, &QLineEdit::textChanged,
           this, [this] (const QString&) { updateList (); });
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
  setSubTitle ("Target file: " + w->selectedRelPath);
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
  QString query= searchEdit->text ().trimmed ().toLower ();
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
  anchorList= new QListWidget (this);
  anchorList->setAlternatingRowColors (true);
  anchorList->setMinimumWidth (500);
  previewTitle= new QLabel ("Select a lower bound to preview the range.", this);
  previewHost= new QWidget (this);
  previewHost->setMinimumHeight (360);
  previewHost->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
  QVBoxLayout* previewHostLayout= new QVBoxLayout (previewHost);
  previewHostLayout->setContentsMargins (0, 0, 0, 0);

  QWidget* left= new QWidget (this);
  QVBoxLayout* leftLayout= new QVBoxLayout (left);
  leftLayout->setContentsMargins (0, 0, 0, 0);
  leftLayout->addWidget (new QLabel ("Lower bound:", this));
  leftLayout->addWidget (searchEdit);
  leftLayout->addWidget (anchorList, 1);

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
  splitter->setSizes (QList<int> () << 520 << 800);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->addWidget (splitter, 1);

  searchEdit->installEventFilter (this);
  anchorList->installEventFilter (this);
  connect (searchEdit, &QLineEdit::textChanged,
           this, [this] (const QString&) { updateList (); });
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
  setSubTitle ("Upper bound: " + w->selectedUpperAnchor);
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
  QString query= searchEdit->text ().trimmed ().toLower ();
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
  for (const WikilinkEnunciationFilterEntry& entry: wikilink_enunciation_filters)
    enunciationCombo->addItem (entry.label, entry.tag);
  enunciationCombo->setMinimumWidth (190);

  searchButton= new QPushButton ("Search", this);
  statusLabel= new QLabel (this);
  progress= new QProgressBar (this);
  progress->setRange (0, 1);
  progress->setValue (0);
  resultList= new QListWidget (this);
  resultList->setAlternatingRowColors (true);
  previewTitle= new QLabel ("Select a search result to preview it.", this);
  previewHost= new QWidget (this);
  previewHost->setMinimumHeight (420);
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
  filtersRow->addStretch ();

  QWidget* left= new QWidget (this);
  QVBoxLayout* leftLayout= new QVBoxLayout (left);
  leftLayout->setContentsMargins (0, 0, 0, 0);
  leftLayout->addWidget (new QLabel ("Enunciations:", this));
  leftLayout->addWidget (resultList, 1);

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
  splitter->setSizes (QList<int> () << 430 << 830);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->addLayout (searchRow);
  layout->addLayout (filtersRow);
  layout->addWidget (statusLabel);
  layout->addWidget (progress);
  layout->addWidget (splitter, 1);

  connect (searchButton, &QPushButton::clicked,
           this, [this] () { startSearch (); });
  connect (queryEdit, &QLineEdit::returnPressed,
           this, [this] () { startSearch (); });
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
TransclusionSearchPage::selectedNamespace () const {
  return namespaceEdit == nullptr ? QString () : namespaceEdit->text ().trimmed ();
}

QString
TransclusionSearchPage::selectedEnunciation () const {
  return enunciationCombo == nullptr ? QString () :
    enunciationCombo->currentData ().toString ().trimmed ();
}

int
TransclusionSearchPage::searchFile (
  url u, const tree& query, std::vector<TransclusionSearchResult>& hits) const
{
  try {
    tree body= import_body_for_preview (u);
    std::vector<WikilinkAnchorEntry> anchors;
    collect_anchors (body, path (), anchors);
    std::vector<TransclusionAnchorPair> pairs=
      collect_transclusion_pairs (anchors);
    QString tag= selectedEnunciation ();

    int matched= 0;
    int oldMode= set_access_mode (DRD_ACCESS_SOURCE);
    try {
      for (const TransclusionAnchorPair& pair: pairs) {
        if (!anchor_pair_matches_enunciation (pair, tag)) continue;
        tree range= build_preview_from_anchor_range (
          body, pair.upperWhere, pair.lowerWhere);
        range_set sels= search (range, query, path (), 1);
        if (N(sels) <= 0) continue;

        TransclusionSearchResult result;
        result.file= u;
        result.upper= pair.upper;
        result.lower= pair.lower;
        result.upperWhere= pair.upperWhere;
        result.lowerWhere= pair.lowerWhere;
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
    url root= vault_get_root ();
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
  searchButton->setEnabled (false);
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
      QMessageBox::warning (this, "Insert transclusion",
                            "Unknown namespace: " + ns);
      searchButton->setEnabled (true);
      return;
    }
    std::vector<athena_namespace_match> members=
      athena_namespace_members (from_qstring (ns), error);
    if (error != "")
      QMessageBox::warning (this, "Insert transclusion",
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
    std::vector<TransclusionSearchResult> fileHits;
    if (searchFile (file, query, fileHits) > 0) {
      matchedFiles++;
      for (const TransclusionSearchResult& hit: fileHits)
        addResult (hit);
    }
    scanned++;
    progress->setValue (scanned);
    statusLabel->setText (
      QString ("Searching %1/%2 files; %3 enunciation(s) in %4 file(s).")
        .arg (scanned)
        .arg ((int) files.size ())
        .arg ((int) results.size ())
        .arg (matchedFiles));
    if ((scanned % 8) == 0)
      QApplication::processEvents (QEventLoop::ExcludeUserInputEvents);
  }

  statusLabel->setText (
    QString ("%1 enunciation(s) in %2 file(s), out of %3 scanned file(s).")
      .arg ((int) results.size ())
      .arg (matchedFiles)
      .arg ((int) files.size ()));
  if (resultList->count () > 0) resultList->setCurrentRow (0);
  searchButton->setEnabled (true);
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
  item->setToolTip (
    QString ("%1\nOccurrence %2 of %3\n%4 ... %5")
      .arg (result.relPath)
      .arg (result.occurrence)
      .arg (result.fileHits)
      .arg (result.upper)
      .arg (result.lower));
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
  : QWizard (parent), selectedUpperIndex (-1), resultAccepted (false) {
  setWindowTitle ("Insert Transclusion");
  resize (1220, 780);
  setOption (QWizard::NoBackButtonOnStartPage, true);

  loadFiles ();

  modePage= new TransclusionModePage (this);
  filePage= new TransclusionFilePage (this);
  kindPage= new TransclusionKindPage (this);
  enunciationPage= new TransclusionEnunciationPage (this);
  upperPage= new TransclusionUpperPage (this);
  lowerPage= new TransclusionLowerPage (this);
  searchPage= new TransclusionSearchPage (this);

  setPage (TransclusionModePageId, modePage);
  setPage (TransclusionFilePageId, filePage);
  setPage (TransclusionKindPageId, kindPage);
  setPage (TransclusionEnunciationPageId, enunciationPage);
  setPage (TransclusionUpperPageId, upperPage);
  setPage (TransclusionLowerPageId, lowerPage);
  setPage (TransclusionSearchPageId, searchPage);
  setStartId (TransclusionModePageId);
}

void
QTMVaultTransclusionWizard::loadFiles () {
  files.clear ();
  url root= vault_get_root ();
  array<url> all= vault_get_all_files ();
  for (int i=0; i<N(all); i++) {
    string suf= suffix (all[i]);
    if (suf != "ath" && suf != "tm") continue;
    url rel= delta (root * url (""), all[i]);
    QString relPath= to_qstring (as_unix_string (rel));
    WikilinkFileEntry e;
    e.file= all[i];
    e.relPath= relPath;
    e.stem= file_display_stem (relPath);
    e.searchText= strip_known_extension (relPath).toLower ();
    e.mtime= vault_get_mtime (all[i]);
    files.push_back (e);
  }
  std::sort (files.begin (), files.end (),
             [] (const WikilinkFileEntry& a,
                 const WikilinkFileEntry& b) {
               if (a.mtime != b.mtime) return a.mtime > b.mtime;
               return a.relPath < b.relPath;
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
vault_choose_link (bool transcludeMode) {
  if (!vault_active ()) return UNINIT;
  if (!transcludeMode) {
    QTMVaultWikilinkWizard wizard (QApplication::activeWindow ());
    if (wizard.exec () == QDialog::Accepted) return wizard.getResult ();
    return UNINIT;
  }

  QTMVaultTransclusionWizard wizard (QApplication::activeWindow ());
  if (wizard.exec () == QDialog::Accepted) return wizard.getResult ();
  return UNINIT;
}
