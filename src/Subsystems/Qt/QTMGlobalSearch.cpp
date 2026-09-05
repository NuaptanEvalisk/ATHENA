/******************************************************************************
* MODULE     : QTMGlobalSearch.cpp
* DESCRIPTION: Qt global search pane for ATHENA vault files
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMGlobalSearch.hpp"
#include "QTMMainTabWindow.hpp"
#include "actor_ui_bridge.hpp"
#if ATHENA_ENABLE_PERSON_SUBSYSTEM
#include "QTMPersonsExplorer.hpp"
#include "ATHENA/Data/person_names.hpp"
#endif
#include "QTMVaultSearch.hpp"
#include "QTMWidget.hpp"
#include "convert.hpp"
#include "converter.hpp"
#include "drd_mode.hpp"
#include "editor.hpp"
#include "message.hpp"
#include "namespaces.hpp"
#include "qt_gui.hpp"
#include "new_buffer.hpp"
#include "new_view.hpp"
#include "qt_utilities.hpp"
#include "qt_widget.hpp"
#include "renderer.hpp"
#include "scheme.hpp"
#include "link.hpp"
#include "tm_buffer.hpp"
#include "tm_ostream.hpp"
#include "tm_window.hpp"
#include "tree_search.hpp"
#include "vault.hpp"

#include <DockAreaWidget.h>
#include <DockSplitter.h>
#include <DockWidget.h>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QCompleter>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QList>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QPointer>
#include <QSize>
#include <QSizeGrip>
#include <QSizePolicy>
#include <QSplitter>
#include <QStringListModel>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <set>

static QTMGlobalSearch* global_search_widget= nullptr;
static ads::CDockWidget* global_search_dock= nullptr;
static widget global_search_content_widget;
static widget global_search_pane_window;

static constexpr const char* global_search_case_insensitive_pref=
  "vault global search case insensitive search";
static constexpr const char* global_search_fuzzy_pref=
  "vault global search fuzzy search";

class qt_adopted_qwidget_rep : public qt_widget_rep {
  QPointer<QWidget> adopted;

public:
  qt_adopted_qwidget_rep (QWidget* w)
    : qt_widget_rep (qt_widget_rep::division_widget), adopted (w) {}

  QWidget* as_qwidget (QWidget* parent_widget) override {
    if (adopted == nullptr) {
      qwid= new QWidget (parent_widget);
      return qwid;
    }

    adopted->setParent (parent_widget);
    qwid= adopted;
    return adopted;
  }
};

struct enunciation_filter_entry {
  const char* label;
  const char* tag;
};

static const enunciation_filter_entry enunciation_filter_entries[]= {
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
qstring_from_tm (string s) {
  return to_qstring (s);
}

static tree
apply_vault_preferred_font_to_preview (tree body) {
  string font= get_preference ("vault preferred font", "");
  if (font == "") return body;
  return tree (WITH, "font", font, body);
}

static tree
import_body_for_global_preview (url file) {
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
  return rebase_preview_images (import_body_for_global_preview (file),
                                head (file));
}

static void
set_global_search_area_height (ads::CDockWidget* dock) {
  if (dock == nullptr || dock->isInFloatingContainer ()) return;
  ads::CDockAreaWidget* area= dock->dockAreaWidget ();
  if (area == nullptr || dock->dockContainer () == nullptr) return;
  ads::CDockSplitter* splitter= area->parentSplitter ();
  if (splitter == nullptr) return;
  QList<int> sizes= splitter->sizes ();
  if (sizes.size () < 2) return;
  int total= 0;
  for (int size : sizes) total += size;
  if (total <= 0) return;
  int target= qBound (440, total / 2, 720);
  int areaIndex= splitter->indexOf (area);
  if (areaIndex < 0 || areaIndex >= sizes.size ()) return;
  int delta= target - sizes[areaIndex];
  if (delta == 0) return;
  sizes[areaIndex]= target;
  int other= areaIndex == 0 ? 1 : 0;
  sizes[other]= qMax (180, sizes[other] - delta);
  splitter->setSizes (sizes);
}

static void
update_global_search_floating_state (ads::CDockWidget* dock, bool floating) {
  if (global_search_widget != nullptr)
    global_search_widget->setFloatingResizeGripVisible (floating);
  if (!floating || dock == nullptr) return;

  QWidget* window= dock->window ();
  if (window == nullptr) return;
  window->setMinimumSize (760, 420);
  window->resize (window->size ().expandedTo (QSize (960, 560)));
}

static QWidget*
global_search_dock_widget () {
  if (global_search_widget == nullptr) return nullptr;

  if (is_nil (global_search_content_widget))
    global_search_content_widget=
      tm_new<qt_adopted_qwidget_rep> (global_search_widget);

  if (is_nil (global_search_pane_window))
    global_search_pane_window=
      plain_window_widget (global_search_content_widget,
                           "athena-global-search", command ());

  qt_widget pane= concrete (global_search_pane_window);
  QWidget* paneWidget= pane->qwid;
  if (paneWidget != nullptr) {
    paneWidget->setWindowTitle ("Global search");
    paneWidget->setProperty ("athena-document-widget", false);
    paneWidget->setFocusPolicy (Qt::StrongFocus);
    paneWidget->setFocusProxy (global_search_widget);
  }
  return paneWidget;
}

QTMGlobalSearch::QTMGlobalSearch (QWidget* parent)
  : QWidget (parent),
    queryUrl (url ("tmfs://aux/global-search")),
    previewBody (tree (DOCUMENT, "")),
    scanIndex (0),
    matchedFiles (0),
    previewZoomFactor (1.0),
    previewWidth (0),
    previewZoom (0.0),
    previewRecreating (false),
    queryTexmacsWidget (nullptr),
    previewHostWidget (nullptr),
    previewQtWidget (nullptr),
    previewTexmacsWidget (nullptr) {
  setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
  setMinimumSize (760, 420);

  prompt= new QLabel ("Search the current vault", this);
  status= new QLabel (this);
  floatingSizeGrip= new QSizeGrip (this);

  QWidget* query= createQueryWidget ();
  query->setMinimumHeight (88);

  searchButton= new QPushButton ("Search", this);
  cancelButton= new QPushButton ("Cancel", this);
  cancelButton->setEnabled (false);

  progress= new QProgressBar (this);
  progress->setRange (0, 1);
  progress->setValue (0);

  resultList= new QListWidget (this);
  resultList->setAlternatingRowColors (true);
  resultList->setMinimumWidth (480);
  resultList->setMinimumHeight (320);
  resultList->installEventFilter (this);

  previewTitle= new QLabel ("Select a result to preview it.", this);
  QWidget* preview= createPreviewWidget ();
  preview->setMinimumHeight (220);

  namespaceEdit= new QLineEdit (this);
  namespaceEdit->setPlaceholderText ("All namespaces");
  namespaceEdit->setClearButtonEnabled (true);
  namespaceEdit->setMinimumWidth (360);
  namespaceModel= new QStringListModel (this);
  QCompleter* namespaceCompleter= new QCompleter (namespaceModel, this);
  namespaceCompleter->setCaseSensitivity (Qt::CaseInsensitive);
  namespaceCompleter->setFilterMode (Qt::MatchContains);
  namespaceEdit->setCompleter (namespaceCompleter);

  enunciationCombo= new QComboBox (this);
  enunciationCombo->addItem ("Not required", "");
  for (const enunciation_filter_entry& entry: enunciation_filter_entries)
    enunciationCombo->addItem (entry.label, entry.tag);
  enunciationCombo->setMinimumWidth (220);

#if ATHENA_ENABLE_PERSON_SUBSYSTEM
  personCombo= new QComboBox (this);
  personCombo->setEditable (true);
  personCombo->setInsertPolicy (QComboBox::NoInsert);
  personCombo->setMinimumWidth (220);
  personCombo->lineEdit ()->setPlaceholderText ("Any person");
#endif

  caseInsensitiveCheck= new QCheckBox ("Case-insensitive", this);
  fuzzyCheck= new QCheckBox ("Fuzzy", this);
  refreshSearchOptions ();

  QWidget* leftPane= new QWidget (this);
  leftPane->setMinimumWidth (500);
  QVBoxLayout* leftLayout= new QVBoxLayout (leftPane);
  leftLayout->setContentsMargins (0, 0, 0, 0);
  leftLayout->addWidget (resultList, 1);

  QWidget* rightPane= new QWidget (this);
  QVBoxLayout* rightLayout= new QVBoxLayout (rightPane);
  rightLayout->setContentsMargins (0, 0, 0, 0);
  rightLayout->addWidget (previewTitle);
  rightLayout->addWidget (preview, 1);

  splitter= new QSplitter (Qt::Horizontal, this);
  splitter->addWidget (leftPane);
  splitter->addWidget (rightPane);
  splitter->setStretchFactor (0, 0);
  splitter->setStretchFactor (1, 1);
  splitter->setSizes (QList<int> () << 520 << 820);

  scanTimer= new QTimer (this);
  scanTimer->setInterval (0);

  QHBoxLayout* buttons= new QHBoxLayout ();
  buttons->addWidget (new QLabel ("Namespace:", this));
  buttons->addWidget (namespaceEdit);
  buttons->addSpacing (12);
  buttons->addWidget (new QLabel ("Enunciation:", this));
  buttons->addWidget (enunciationCombo);
  buttons->addSpacing (12);
#if ATHENA_ENABLE_PERSON_SUBSYSTEM
  buttons->addWidget (new QLabel ("Person:", this));
  buttons->addWidget (personCombo);
  buttons->addSpacing (12);
#endif
  buttons->addWidget (caseInsensitiveCheck);
  buttons->addWidget (fuzzyCheck);
  buttons->addSpacing (12);
  buttons->addWidget (searchButton);
  buttons->addWidget (cancelButton);
  buttons->addStretch ();

  floatingSizeGrip->hide ();
  QHBoxLayout* gripRow= new QHBoxLayout ();
  gripRow->setContentsMargins (0, 0, 0, 0);
  gripRow->addStretch ();
  gripRow->addWidget (floatingSizeGrip, 0, Qt::AlignRight | Qt::AlignBottom);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->setContentsMargins (8, 8, 8, 8);
  layout->addWidget (prompt);
  layout->addWidget (query);
  layout->addLayout (buttons);
  layout->addWidget (progress);
  layout->addWidget (status);
  layout->addWidget (splitter, 1);
  layout->addLayout (gripRow);

  connect (searchButton, &QPushButton::clicked,
           this, [this] () { startSearch (); });
  connect (cancelButton, &QPushButton::clicked,
           this, [this] () { cancelSearch (); });
  connect (caseInsensitiveCheck, &QCheckBox::toggled,
           this, [] (bool checked) {
             set_preference (global_search_case_insensitive_pref,
                             checked ? "on" : "off");
           });
  connect (fuzzyCheck, &QCheckBox::toggled,
           this, [] (bool checked) {
             set_preference (global_search_fuzzy_pref,
                             checked ? "on" : "off");
           });
  connect (scanTimer, &QTimer::timeout,
           this, [this] () { scanChunk (); });
  connect (resultList, &QListWidget::itemDoubleClicked,
           this, [this] (QListWidgetItem* item) { openResult (item); });
  connect (resultList, &QListWidget::itemActivated,
           this, [this] (QListWidgetItem* item) { openResult (item); });
  connect (resultList, &QListWidget::currentItemChanged,
           this, [this] (QListWidgetItem* current, QListWidgetItem*) {
             updatePreview (current);
           });

  refreshNamespaces ();
  setIdleStatus ();
}

QTMGlobalSearch::~QTMGlobalSearch () {
  scanTimer->stop ();
  if (!is_nil (queryWidget)) send_destroy (queryWidget);
  destroyPreviewWidget ();
}

QSize
QTMGlobalSearch::sizeHint () const {
  return QSize (1360, 720);
}

void
QTMGlobalSearch::setPreviewZoomFactor (double zoom) {
  if (zoom >= 0.04 && zoom <= 25.0)
    previewZoomFactor= zoom;
  applyPreviewZoom ();
}

void
QTMGlobalSearch::setFloatingResizeGripVisible (bool visible) {
  floatingSizeGrip->setVisible (visible);
}

void
QTMGlobalSearch::refreshSearchOptions () {
  if (caseInsensitiveCheck != nullptr)
    caseInsensitiveCheck->setChecked (
      get_preference (global_search_case_insensitive_pref, "off") == "on");
  if (fuzzyCheck != nullptr)
    fuzzyCheck->setChecked (
      get_preference (global_search_fuzzy_pref, "off") == "on");
}

bool
QTMGlobalSearch::eventFilter (QObject* watched, QEvent* event) {
  if (event != nullptr && event->type () == QEvent::Resize &&
      (watched == previewHostWidget || isPreviewWatchedObject (watched))) {
    QTimer::singleShot (0, this, [this] () { refreshPreviewLayoutNow (); });
  }

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

  if (watched == resultList && event->type () == QEvent::KeyPress) {
    QKeyEvent* key= static_cast<QKeyEvent*> (event);
    if (key->key () == Qt::Key_Return || key->key () == Qt::Key_Enter) {
      openCurrentResult ();
      return true;
    }
  }
  return QWidget::eventFilter (watched, event);
}

QWidget*
QTMGlobalSearch::createQueryWidget () {
  tree doc (DOCUMENT, "");
  tree style= compound ("style", tuple ("generic"));
  queryWidget= texmacs_input_widget (doc, style, queryUrl);
  QWidget* qwid= concrete (queryWidget)->as_qwidget (this);
  queryTexmacsWidget= qobject_cast<QTMWidget*> (qwid);
  if (queryTexmacsWidget == nullptr)
    queryTexmacsWidget= qwid->findChild<QTMWidget*> ();
  if (queryTexmacsWidget != nullptr) {
    queryTexmacsWidget->setObjectName (
      "global-search-query-texmacs-widget");
    queryTexmacsWidget->setFocusPolicy (Qt::StrongFocus);
    setFocusProxy (queryTexmacsWidget);
  }
  else setFocusProxy (qwid);
  qwid->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Fixed);
  return qwid;
}

QWidget*
QTMGlobalSearch::createPreviewWidget () {
  previewHostWidget= new QWidget (this);
  previewHostWidget->setSizePolicy (QSizePolicy::Expanding,
                                    QSizePolicy::Expanding);
  QVBoxLayout* layout= new QVBoxLayout (previewHostWidget);
  layout->setContentsMargins (0, 0, 0, 0);
  layout->setSpacing (0);
  previewHostWidget->installEventFilter (this);
  recreatePreviewWidget ();
  return previewHostWidget;
}

void
QTMGlobalSearch::destroyPreviewWidget () {
  if (previewQtWidget != nullptr) {
    if (previewQtWidget->parentWidget () != nullptr &&
        previewQtWidget->parentWidget ()->layout () != nullptr)
      previewQtWidget->parentWidget ()->layout ()->removeWidget (
        previewQtWidget);
    previewQtWidget->hide ();
    previewQtWidget->deleteLater ();
    previewQtWidget= nullptr;
    previewTexmacsWidget= nullptr;
  }
  if (!is_nil (previewWidget)) {
    try { send_destroy (previewWidget); }
    catch (...) {}
    previewWidget= widget ();
  }
  previewWidth= 0;
  previewZoom= 0.0;
}

void
QTMGlobalSearch::recreatePreviewWidget () {
  if (previewHostWidget == nullptr || previewRecreating) return;
  struct flag_guard {
    bool& flag;
    flag_guard (bool& flag2) : flag (flag2) { flag= true; }
    ~flag_guard () { flag= false; }
  } guard (previewRecreating);

  destroyPreviewWidget ();

  tree style= compound ("style", tuple ("generic"));
  previewWidth= currentPreviewWidth ();
  previewZoom= currentPreviewZoom ();
  try {
    previewWidget= texmacs_output_widget (
      apply_vault_preferred_font_to_preview (previewBody), style, previewWidth,
      previewZoom);
    QWidget* qwid= concrete (previewWidget)->as_qwidget (previewHostWidget);
    previewQtWidget= qwid;
    previewTexmacsWidget= qobject_cast<QTMWidget*> (qwid);
    if (previewTexmacsWidget == nullptr)
      previewTexmacsWidget= qwid->findChild<QTMWidget*> ();

    installPreviewEventFilter (qwid);
    qwid->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
    if (previewHostWidget->layout () != nullptr)
      previewHostWidget->layout ()->addWidget (qwid);
    qwid->show ();
  }
  catch (string msg) {
    std_error << "global search preview: failed to typeset preview: "
              << msg << LF;
    showFallbackPreview ();
  }
  catch (...) {
    std_error << "global search preview: failed to typeset preview" << LF;
    showFallbackPreview ();
  }

  refreshPreviewLayout ();
}

void
QTMGlobalSearch::showFallbackPreview () {
  if (previewHostWidget == nullptr) return;
  QLabel* label= new QLabel ("Preview unavailable.", previewHostWidget);
  label->setAlignment (Qt::AlignCenter);
  label->setWordWrap (true);
  label->setFocusPolicy (Qt::NoFocus);
  label->setContextMenuPolicy (Qt::NoContextMenu);
  label->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
  if (previewHostWidget->layout () != nullptr)
    previewHostWidget->layout ()->addWidget (label);
  previewQtWidget= label;
  previewTexmacsWidget= nullptr;
  previewWidget= widget ();
  previewWidth= currentPreviewWidth ();
  previewZoom= currentPreviewZoom ();
  installPreviewEventFilter (label);
  label->show ();
}

void
QTMGlobalSearch::applyPreviewZoom () {
  refreshPreviewLayout ();
}

void
QTMGlobalSearch::focusQueryEditor () {
  if (queryTexmacsWidget == nullptr) return;
  if (window () != nullptr) {
    window ()->raise ();
    window ()->activateWindow ();
  }
  queryTexmacsWidget->setFocus (Qt::OtherFocusReason);
}

void
QTMGlobalSearch::installPreviewEventFilter (QWidget* root) {
  if (root == nullptr) return;

  root->installEventFilter (this);
  root->setContextMenuPolicy (Qt::NoContextMenu);
  root->setFocusPolicy (Qt::NoFocus);
  QList<QWidget*> children= root->findChildren<QWidget*> ();
  for (QWidget* child : children) {
    if (child == nullptr) continue;
    child->installEventFilter (this);
    child->setContextMenuPolicy (Qt::NoContextMenu);
    child->setFocusPolicy (Qt::NoFocus);
  }
}

bool
QTMGlobalSearch::isPreviewWatchedObject (QObject* watched) const {
  for (QObject* obj= watched; obj != nullptr; obj= obj->parent ())
    if (obj == previewQtWidget) return true;
  return false;
}

void
QTMGlobalSearch::refreshPreviewLayoutNow () {
  if (previewQtWidget == nullptr || previewHostWidget == nullptr) return;

  SI width= currentPreviewWidth ();
  SI delta= width > previewWidth ? width - previewWidth :
    previewWidth - width;
  double zoom= currentPreviewZoom ();
  double zoomDelta= zoom > previewZoom ? zoom - previewZoom :
    previewZoom - zoom;
  if ((width > 0 && (previewWidth <= 0 || delta > 8 * PIXEL)) ||
      zoomDelta > 0.001) {
    recreatePreviewWidget ();
    return;
  }

  installPreviewEventFilter (previewQtWidget);
  previewQtWidget->show ();
  previewQtWidget->updateGeometry ();
  previewQtWidget->update ();

  QTMWidget* tmWidget= previewTexmacsWidget;
  if (tmWidget != nullptr) {
    tmWidget->setFocusPolicy (Qt::NoFocus);
    tmWidget->show ();
    tmWidget->updateGeometry ();
    tmWidget->update ();
    if (tmWidget->viewport () != nullptr) {
      tmWidget->viewport ()->setFocusPolicy (Qt::NoFocus);
      tmWidget->viewport ()->show ();
      if (tmWidget->viewport ()->layout () != nullptr)
        tmWidget->viewport ()->layout ()->activate ();
    }
    if (tmWidget->surface () != nullptr) {
      tmWidget->surface ()->setFocusPolicy (Qt::NoFocus);
      tmWidget->surface ()->show ();
      tmWidget->surface ()->updateGeometry ();
      tmWidget->surface ()->update ();
    }
  }

  if (the_gui != nullptr) the_gui->force_update ();
  if (tmWidget != nullptr) tmWidget->refreshEmbeddedBackingStore ();
}

void
QTMGlobalSearch::refreshPreviewLayout () {
  refreshPreviewLayoutNow ();

  QTimer::singleShot (0, this, [this] () {
    refreshPreviewLayoutNow ();
  });
}

SI
QTMGlobalSearch::currentPreviewWidth () const {
  if (previewHostWidget == nullptr) return 0;
  int w= previewHostWidget->contentsRect ().width ();
  if (w <= 0) w= previewHostWidget->width ();
  if (w <= 0) return 0;
  return from_qsize (QSize (w, 1)).x1;
}

double
QTMGlobalSearch::currentPreviewZoom () const {
  return get_retina_zoom () * previewZoomFactor;
}

tree
QTMGlobalSearch::normalizeQuery (tree t) const {
  while (is_func (t, DOCUMENT, 1) ||
         is_func (t, INACTIVE, 1) ||
         is_func (t, VAR_INACTIVE, 1))
    t= t[0];
  if (is_func (t, WITH) && N(t) > 0)
    t= t[N(t) - 1];
  return t;
}

tree
QTMGlobalSearch::currentQuery () const {
  if (!contains (queryUrl, get_all_buffers ())) return tree ("");
  return normalizeQuery (get_buffer_body (queryUrl));
}

QString
QTMGlobalSearch::relativePath (url u) const {
  url rel= delta (vault_get_root () * url (""), u);
  return qstring_from_tm (as_unix_string (rel));
}

void
QTMGlobalSearch::refreshNamespaces () {
  QString current= namespaceEdit == nullptr ? QString () :
    namespaceEdit->text ().trimmed ();

  QStringList names;
  for (const athena_namespace_definition& ns: athena_namespaces_list ())
    names << to_qstring (ns.name);
  names.removeDuplicates ();
  names.sort (Qt::CaseInsensitive);
  namespaceModel->setStringList (names);

  if (!current.isEmpty () && !names.contains (current, Qt::CaseSensitive))
    namespaceEdit->setText (current);

#if ATHENA_ENABLE_PERSON_SUBSYSTEM
  if (personCombo != nullptr) {
    QString person= personCombo->currentText ().trimmed ();
    personCombo->clear ();
    personCombo->addItem ("", "");
    personCombo->addItems (qtm_vault_person_names ());
    personCombo->setCurrentText (person);
  }
#endif
}

QString
QTMGlobalSearch::selectedNamespace () const {
  return namespaceEdit == nullptr ? QString () : namespaceEdit->text ().trimmed ();
}

QString
QTMGlobalSearch::selectedEnunciation () const {
  if (enunciationCombo == nullptr) return QString ();
  return enunciationCombo->currentData ().toString ().trimmed ();
}

#if ATHENA_ENABLE_PERSON_SUBSYSTEM
QString
QTMGlobalSearch::selectedPerson () const {
  return personCombo == nullptr ? QString () :
    personCombo->currentText ().trimmed ();
}
#endif

void
QTMGlobalSearch::setIdleStatus () {
  status->setText ("Enter a query and search the current vault.");
  progress->setRange (0, 1);
  progress->setValue (0);
}

void
QTMGlobalSearch::setRunningStatus () {
  QString ns= selectedNamespace ();
  QString enunciation= selectedEnunciation ();
  QString scope= ns.isEmpty () ? QString ("current vault") :
    QString ("namespace %1").arg (ns);
  QString kind= enunciation.isEmpty () ? QString () :
    QString (" inside <%1>").arg (enunciation);
#if ATHENA_ENABLE_PERSON_SUBSYSTEM
  QString person= selectedPerson ();
  QString personScope= person.isEmpty () ? QString () :
    QString (" mentioning %1").arg (person);
#else
  QString personScope;
#endif
  status->setText (QString ("Searching %1 file(s) in %2%3%4...")
                   .arg ((int) scanFiles.size ())
                   .arg (scope)
                   .arg (kind)
                   .arg (personScope));
  progress->setRange (0, (int) scanFiles.size ());
  progress->setValue (0);
}

void
QTMGlobalSearch::startSearch () {
  debug_qt << "Global search: Search button clicked" << LF;
  if (!vault_active ()) {
    QMessageBox::warning (this, "Global search",
                          "No active vault. Please load a vault first.");
    return;
  }

  if (scanTimer->isActive ()) scanTimer->stop ();
  resultList->clear ();
  clearPreview ();
  results.clear ();
  scanFiles.clear ();
  scanIndex= 0;
  matchedFiles= 0;

  queryTree= currentQuery ();
  if (is_empty (queryTree)) {
    status->setText ("Enter a non-empty query.");
    progress->setRange (0, 1);
    progress->setValue (0);
    return;
  }

  refreshNamespaces ();
  QString ns= selectedNamespace ();
  if (ns.isEmpty ()) {
    array<url> files= vault_get_all_files ();
    for (int i=0; i<N(files); i++)
      if (suffix (files[i]) == "ath")
        scanFiles.push_back (files[i]);
  }
  else {
    string error;
    athena_namespace_definition def;
    if (!athena_namespace_get (from_qstring (ns), def)) {
      QMessageBox::warning (this, "Global search",
                            "Unknown namespace: " + ns);
      setIdleStatus ();
      return;
    }
    std::vector<athena_namespace_match> members=
      athena_namespace_members (from_qstring (ns), error);
    if (error != "") {
      QMessageBox::warning (this, "Global search",
                            "Namespace warning: " + to_qstring (error));
    }
    std::set<std::string> seen;
    for (const athena_namespace_match& m: members) {
      if (suffix (m.file) != "ath") continue;
      std::string key= to_qstring (concretize (m.file)).toStdString ();
      if (!seen.insert (key).second) continue;
      scanFiles.push_back (m.file);
    }
  }

  std::sort (scanFiles.begin (), scanFiles.end (),
             [this] (const url& a, const url& b) {
               return relativePath (a) < relativePath (b);
             });

  debug_qt << "Global search: deferred scan starting, "
           << (int) scanFiles.size () << " files";
  if (!ns.isEmpty ())
    debug_qt << " in namespace " << from_qstring (ns);
  QString enunciation= selectedEnunciation ();
  if (!enunciation.isEmpty ())
    debug_qt << ", enunciation " << from_qstring (enunciation);
  debug_qt << LF;
  setRunningStatus ();
  searchButton->setEnabled (false);
  cancelButton->setEnabled (true);
  scanTimer->start ();
}

void
QTMGlobalSearch::cancelSearch () {
  if (!scanTimer->isActive ()) return;
  scanTimer->stop ();
  status->setText (QString ("Search cancelled after %1/%2 files; %3 occurrence(s) in %4 file(s).")
                   .arg (scanIndex)
                   .arg ((int) scanFiles.size ())
                   .arg ((int) results.size ())
                   .arg (matchedFiles));
  searchButton->setEnabled (true);
  cancelButton->setEnabled (false);
}

int
QTMGlobalSearch::searchFile (url u, std::vector<Result>& hits) const {
  try {
    tree t= import_tree (u, "texmacs");
    tree body= extract (t, "body");
    if (is_empty (body)) body= t;
#if ATHENA_ENABLE_PERSON_SUBSYSTEM
    QString person= selectedPerson ();
    if (!person.isEmpty () &&
        !athena_tree_contains_person_text (body, from_qstring (person)))
      return 0;
#endif

    int oldMode= set_access_mode (DRD_ACCESS_SOURCE);
    std::vector<VaultContentMatch> matches;
    try {
      QString enunciation= selectedEnunciation ();
      bool caseInsensitive= caseInsensitiveCheck != nullptr &&
                            caseInsensitiveCheck->isChecked ();
      bool fuzzy= fuzzyCheck != nullptr && fuzzyCheck->isChecked ();
      if (enunciation.isEmpty ())
        append_content_matches (matches, body, queryTree, path (), 200,
                                caseInsensitive, fuzzy);
      else
        collect_enunciation_matches (matches, body, queryTree,
                                     from_qstring (enunciation), path (), 200,
                                     caseInsensitive, fuzzy);
    }
    catch (...) {
      set_access_mode (oldMode);
      throw;
    }
    set_access_mode (oldMode);

    std::stable_sort (
      matches.begin (), matches.end (),
      [] (const VaultContentMatch& a, const VaultContentMatch& b) {
        if (a.exact != b.exact) return a.exact;
        if (!a.exact && a.score != b.score) return a.score > b.score;
        return path_less (a.start, b.start);
      });

    int hitCount= (int) matches.size ();
    if (hitCount <= 0) return 0;

    QString rel= relativePath (u);
    int occurrence= 1;
    for (const VaultContentMatch& match: matches) {
      Result result;
      result.relPath= rel;
      result.file= u;
      result.occurrence= occurrence++;
      result.fileHits= hitCount;
      result.hitStart= match.start;
      result.hitEnd= match.end;
      result.exact= match.exact;
      result.score= match.score;
      hits.push_back (result);
    }
    return hitCount;
  }
  catch (...) {
    debug_qt << "Global search: skipped "
             << from_qstring (to_qstring (concretize (u))) << LF;
    return 0;
  }
}

tree
QTMGlobalSearch::buildPreviewFromBody (tree body, path hitStart) const {
  if (is_empty (body)) return tree (DOCUMENT, "");
  if (!is_func (body, DOCUMENT)) {
    tree preview (DOCUMENT);
    preview << compound ("marked", copy (body));
    return preview;
  }

  if (N(body) == 0) return tree (DOCUMENT, "");

  int top= 0;
  if (!is_nil (hitStart)) top= hitStart->item;
  if (top < 0 || top >= N(body)) top= 0;

  int first= std::max (0, top - 2);
  int last = std::min (N(body), top + 3);
  tree preview (DOCUMENT);
  for (int i= first; i<last; i++) {
    tree block= copy (body[i]);
    if (i == top) block= compound ("marked", block);
    preview << block;
  }
  return preview;
}

tree
QTMGlobalSearch::buildPreview (const Result& result) const {
  try {
    tree body= import_body_for_preview (result.file);
    return buildPreviewFromBody (body, result.hitStart);
  }
  catch (...) {
    return tree (DOCUMENT, "Preview unavailable.");
  }
}

void
QTMGlobalSearch::addResult (const Result& result) {
  results.push_back (result);
  QListWidgetItem* item= new QListWidgetItem (
    QString ("%1 (%2)")
      .arg (result.relPath)
      .arg (result.occurrence));
  QString tooltip=
    QString ("%1\nOccurrence %2 of %3\nHit path: %4")
      .arg (result.relPath)
      .arg (result.occurrence)
      .arg (result.fileHits)
      .arg (qstring_from_tm (as_string (result.hitStart)));
  if (!result.exact)
    tooltip += QString ("\nFuzzy match: %1%").arg (result.score, 0, 'f', 1);
  item->setToolTip (tooltip);
  item->setData (Qt::UserRole, (int) results.size () - 1);
  resultList->addItem (item);
  if (resultList->count () == 1) resultList->setCurrentRow (0);
}

void
QTMGlobalSearch::updatePreview (QListWidgetItem* current) {
  if (current == nullptr) {
    clearPreview ();
    return;
  }

  int index= current->data (Qt::UserRole).toInt ();
  if (index < 0 || index >= (int) results.size ()) {
    clearPreview ();
    return;
  }

  const Result& result= results[index];
  previewTitle->setText (
    QString ("%1  (%2 of %3)")
      .arg (result.relPath)
      .arg (result.occurrence)
      .arg (result.fileHits));
  previewBody= buildPreview (result);
  recreatePreviewWidget ();
  refreshPreviewLayout ();
}

void
QTMGlobalSearch::clearPreview () {
  if (previewTitle != nullptr)
    previewTitle->setText ("Select a result to preview it.");
  previewBody= tree (DOCUMENT, "");
  recreatePreviewWidget ();
  refreshPreviewLayout ();
}

void
QTMGlobalSearch::scanChunk () {
  const int chunkSize= 8;
  int end= std::min (scanIndex + chunkSize, (int) scanFiles.size ());
  for (; scanIndex < end; scanIndex++) {
    std::vector<Result> hits;
    if (searchFile (scanFiles[scanIndex], hits) > 0) {
      matchedFiles++;
      for (const Result& result : hits)
        addResult (result);
    }
  }

  progress->setValue (scanIndex);
  status->setText (
    QString ("Searching %1/%2 files; %3 occurrence(s) in %4 file(s).")
      .arg (scanIndex)
      .arg ((int) scanFiles.size ())
      .arg ((int) results.size ())
      .arg (matchedFiles));

  if (scanIndex >= (int) scanFiles.size ())
    finishSearch ();
}

void
QTMGlobalSearch::finishSearch () {
  scanTimer->stop ();
  searchButton->setEnabled (true);
  cancelButton->setEnabled (false);
  status->setText (
    QString ("%1 occurrence(s) in %2 file(s), out of %3 scanned files.")
      .arg ((int) results.size ())
      .arg (matchedFiles)
      .arg ((int) scanFiles.size ()));
  debug_qt << "Global search: deferred scan finished with "
           << (int) results.size () << " occurrences in "
           << matchedFiles << " files" << LF;
}

void
QTMGlobalSearch::openResult (QListWidgetItem* item) {
  if (item == nullptr) return;
  int index= item->data (Qt::UserRole).toInt ();
  if (index < 0 || index >= (int) results.size ()) return;

  const Result result= results[index];
  array<object> cmd;
  cmd << symbol_object ("global-search-open-occurrence");
  cmd << object (result.file);
  cmd << list_object (symbol_object ("quote"), object (result.hitStart));
  cmd << list_object (symbol_object ("quote"), object (result.hitEnd));
  exec_delayed (scheme_cmd (as_list_object (cmd)));
}

void
QTMGlobalSearch::openCurrentResult () {
  QListWidgetItem* item= resultList->currentItem ();
  if (item == nullptr && resultList->count () > 0) item= resultList->item (0);
  openResult (item);
}

void
global_search_show () {
  if (qt_defer_to_main_thread (global_search_show)) return;

  tm_view sourceView= concrete_view (get_current_view_safe ());
  actor_ui_endpoint* sourceEndpoint= sourceView == nullptr ? nullptr :
    find_actor_ui_endpoint (sourceView->runtime_id);
  double previewZoom= sourceEndpoint == nullptr ? 1.0 :
    sourceEndpoint->zoom_factor ();

  if (!vault_active ()) {
    QMessageBox::warning (QApplication::activeWindow (), "Global search",
                          "No active vault. Please load a vault first.");
    return;
  }

  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr || win->dockManager () == nullptr) {
    QMessageBox::warning (QApplication::activeWindow (), "Global search",
                          "No active ATHENA window.");
    return;
  }

  if (global_search_widget == nullptr) {
    global_search_widget= new QTMGlobalSearch ();
    global_search_widget->resize (1360, 720);
    QObject::connect (global_search_widget, &QObject::destroyed, [] () {
      global_search_widget= nullptr;
      global_search_dock= nullptr;
      global_search_content_widget= widget ();
      global_search_pane_window= widget ();
    });
  }
  global_search_widget->refreshNamespaces ();
  global_search_widget->refreshSearchOptions ();
  global_search_widget->setPreviewZoomFactor (previewZoom);

  QWidget* paneWidget= global_search_dock_widget ();
  if (paneWidget == nullptr) return;

  QString title= "Global search";
  bool freshDock= global_search_dock == nullptr;
  if (freshDock) {
    global_search_dock= new ads::CDockWidget (title);
    global_search_dock->setObjectName ("athena-global-search");
    global_search_dock->resize (1360, 720);
    global_search_dock->setWidget (
      paneWidget, ads::CDockWidget::ForceNoScrollArea);
    global_search_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QTMGlobalSearch* pane= global_search_widget;
    ads::CDockWidget* dock= global_search_dock;
    QObject::connect (dock, &ads::CDockWidget::topLevelChanged,
                      pane, [dock] (bool topLevel) {
                        update_global_search_floating_state (dock, topLevel);
                        if (topLevel)
                          QTimer::singleShot (0, dock, [dock] () {
                            update_global_search_floating_state (dock, true);
                          });
                      });
    QObject::connect (global_search_dock, &QObject::destroyed, [] () {
      global_search_dock= nullptr;
    });
  }

  if (freshDock && global_search_dock->dockAreaWidget () == nullptr &&
      global_search_dock->dockContainer () == nullptr) {
    win->dockManager ()->addDockWidgetFloating (global_search_dock);
    global_search_dock->toggleView (true);
    global_search_dock->show ();
    global_search_dock->raise ();
  }
  else win->showAdsDockWidget (global_search_dock, ads::BottomDockWidgetArea);

  global_search_dock->setWindowTitle (title);
  update_global_search_floating_state (
    global_search_dock, global_search_dock->isInFloatingContainer ());
  set_global_search_area_height (global_search_dock);
  QTimer::singleShot (0, win, [] () {
    set_global_search_area_height (global_search_dock);
  });
  QTimer::singleShot (0, global_search_widget, [] () {
    if (global_search_widget != nullptr)
      global_search_widget->focusQueryEditor ();
  });
}
