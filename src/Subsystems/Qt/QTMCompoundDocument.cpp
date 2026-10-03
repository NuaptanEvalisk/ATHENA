/******************************************************************************
* MODULE     : QTMCompoundDocument.cpp
* DESCRIPTION: Lazy source canvases, continuous scrolling and member markers
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "QTMCompoundDocument.hpp"
#include "QTMWidget.hpp"
#include "QTMMainTabWindow.hpp"
#include "QTMEditorToolbarPresenter.hpp"
#include "QTMToolbarController.hpp"
#include "QTMToolbar.hpp"
#include "QTMStyle.hpp"
#include "scheme.hpp"
#include "namespaces.hpp"
#include "new_buffer.hpp"
#include "new_view.hpp"
#include "new_window.hpp"
#include "tm_window.hpp"
#include "buffer_actor.hpp"
#include "new_style.hpp"
#include "Edit/Editor/edit_typeset.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include <QAbstractScrollArea>
#include <QApplication>
#include <QDir>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QThreadPool>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

namespace {
constexpr int member_heading_height= 28;

url source_url (const QString& filename) {
  const auto bytes= filename.toUtf8 ();
  return url_system (string (bytes.constData (), int (bytes.size ())));
}

QByteArray counter_bytes (const tree& state) {
  return QByteArray::fromStdString (athena::document::write_xml_v2 (
    state, athena::document::xml_kind::fragment));
}

tree counter_tree (const QByteArray& bytes) {
  return athena::document::read_xml_v2 (
    std::string_view (bytes.constData (), std::size_t (bytes.size ())),
    athena::document::xml_kind::fragment);
}

class MemberScrollBar: public QScrollBar {
  const athena::avd::layout_index& layout_;
public:
  explicit MemberScrollBar (const athena::avd::layout_index& layout):
    QScrollBar (Qt::Vertical), layout_ (layout) {}
protected:
  void paintEvent (QPaintEvent* event) override {
    QScrollBar::paintEvent (event);
    if (layout_.extent () <= 0) return;
    QPainter painter (this);
    painter.setPen (palette ().color (QPalette::Mid));
    const int margin= style ()->pixelMetric (QStyle::PM_ScrollBarExtent, nullptr, this);
    const int span= std::max (0, height () - 2 * margin);
    for (std::size_t i= 1; i < layout_.size (); ++i) {
      const int y= margin + int (span * layout_.top (i) / layout_.extent ());
      painter.drawLine (0, y, std::min (5, width ()), y);
    }
  }
};
}

class QTMCompoundViewport: public QAbstractScrollArea {
  struct SourceView {
    url view= url_none ();
    url window= url_none ();
    QPointer<QTMWidget> canvas;
    QPointer<QWidget> host;
    bool visible= false;
    std::uint64_t source_epoch= 0;
  };
  QTMCompoundDocument& owner_;
  QString filename_;
  athena::avd::descriptor_file descriptor_;
  std::vector<athena::avd::member> members_;
  athena::avd::layout_index layout_;
  athena::avd::counter_cache counters_;
  std::vector<SourceView> views_;
  QTimer refresh_;
  QTimer save_cache_;
  std::size_t counter_next_= 0;
  std::size_t counter_target_= 0;
  std::uint64_t counter_job_= 0;
  std::uint64_t style_generation_= style_cache_generation ();
  bool counter_pending_= false;
  bool counter_failed_= false;
  bool cache_dirty_= false;
  bool cache_write_failed_= false;
  bool arranging_= false;
  std::size_t active_member_= std::numeric_limits<std::size_t>::max ();
  double scale_= 1;

  double offset () const { return verticalScrollBar ()->value () * scale_; }

  void activateMember (std::size_t i) {
    active_member_= i;
    owner_.activateMember (views_[i].canvas);
    set_current_view (views_[i].view);
  }

  void updateRange (double desired) {
    const double maximum= std::max (0.0, layout_.extent () - viewport ()->height ());
    scale_= std::max (1.0, maximum / (std::numeric_limits<int>::max () - 1.0));
    QSignalBlocker blocked (verticalScrollBar ());
    verticalScrollBar ()->setRange (0, int (std::ceil (maximum / scale_)));
    verticalScrollBar ()->setPageStep (std::max (1, int (viewport ()->height () / scale_)));
    verticalScrollBar ()->setSingleStep (std::max (1, int (40 / scale_)));
    verticalScrollBar ()->setValue (int (std::round (std::clamp (desired, 0.0, maximum) / scale_)));
  }

  tm_view ensureView (std::size_t i) {
    auto& item= views_[i];
    if (!is_none (item.view))
      if (auto view= concrete_view (item.view)) return view;
    const url source= source_url (members_[i].filename);
    if (is_nil (concrete_buffer (source)) && buffer_load (source))
      throw std::runtime_error ("Could not open compound source: " + members_[i].filename.toStdString ());
    item.view= get_new_view (source);
    const auto view= concrete_view (item.view);
    view->compound_member= true;
    return view;
  }

  void mount (std::size_t i) {
    auto& item= views_[i];
    if (item.canvas) return;
    ensureView (i);
    item.window= new_window (false);
    const auto window= concrete_window (item.window);
    window->set_header_flag (false);
    window->set_footer_flag (false);
    item.host= concrete (window->wid)->qwid;
    item.host->setParent (viewport ());
    attach_view (item.window, item.view);
    const auto view= concrete_view (item.view);
    item.canvas= qobject_cast<QTMWidget*> (concrete (view->canvas)->qwid.data ());
    if (!item.canvas) throw std::runtime_error ("Compound source has no editor canvas");
    // Keep the native window/canvas ownership and coordinate hierarchy intact.
    // The whole member container is clipped by the compound viewport.
    item.canvas->setHorizontalScrollBarPolicy (Qt::ScrollBarAsNeeded);
    item.canvas->setVerticalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    item.canvas->installEventFilter (this);
    item.canvas->viewport ()->installEventFilter (this);
    item.canvas->surface ()->installEventFilter (this);
    connect (item.canvas, &QTMScrollView::originRequested,
      this, [this, i] (QPoint position) {
        const auto& source= views_[i];
        if (arranging_ || !source.visible) return;
        updateRange (layout_.top (i) + member_heading_height + position.y () -
                     source.host->y ());
        arrange ();
      });
  }

  bool submit (std::size_t i, std::function<void()> work) {
    const auto view= ensureView (i);
    const auto continuation= actor_continuation_registry::instance ().store (std::move (work));
    if (buffer_actor::try_submit_to (view->buf->actor->id (),
        actor_command_kind::run_native_continuation, view->runtime_id,
        ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER, continuation)) return true;
    actor_continuation_registry::instance ().discard (continuation);
    return false;
  }

  void invalidateCounters (std::size_t first) {
    ++counter_job_;
    counter_pending_= false;
    counter_failed_= false;
    counter_next_= std::min (counter_next_, first);
    counters_.invalidate_from (first);
    for (std::size_t i= first; i < members_.size (); ++i) {
      views_[i].source_epoch= 0;
    }
  }

  void checkCounterChanges () {
    const auto styles= style_cache_generation ();
    if (style_generation_ != styles) {
      style_generation_= styles;
      invalidateCounters (0);
      return;
    }
    for (std::size_t i= 0; i < views_.size (); ++i) {
      if (views_[i].source_epoch == 0) continue;
      const auto view= concrete_view (views_[i].view);
      if (!view || view->buf->actor->source_epoch () != views_[i].source_epoch) {
        invalidateCounters (i);
        return;
      }
    }
  }

  void counterError (const QString& error) {
    counter_pending_= false;
    counter_failed_= true;
    const auto bytes= error.toUtf8 ();
    std_error << "Compound counters: " << string (bytes.constData (), int (bytes.size ())) << LF;
  }

  void finishCounters (std::size_t i) {
    counter_next_= i + 1;
    counter_pending_= false;
    cache_dirty_= true;
    if (!save_cache_.isActive () && !cache_write_failed_) save_cache_.start ();
    QTimer::singleShot (0, this, [this] { scheduleCounters (); });
  }

  void computeCounters (std::size_t i, QByteArray incoming, std::uint64_t job,
                         std::uint64_t epoch) {
    const auto expected= counters_.prefix_after (i);
    const auto cached= counters_.before (i + 1);
    struct Result { QByteArray output; QString error; bool stale= false; };
    auto result= std::make_shared<Result> ();
    const QPointer<QTMCompoundViewport> guard (this);
    const auto work= [guard, result, i, incoming, cached, job, epoch, expected] {
      try {
        const auto* context= current_scheme_execution_context ();
        auto* editor= dynamic_cast<edit_typeset_rep*> (context->editor);
        result->stale= context->actor->source_epoch () != epoch;
        if (!result->stale) {
          const tree state= counter_tree (incoming);
          editor->set_compound_counters (state);
          result->output= cached ? cached->counters_xml :
            counter_bytes (editor->evaluate_compound_counters (state));
          result->stale= context->actor->source_epoch () != epoch;
        }
      }
      catch (const std::exception& e) { result->error= QString::fromUtf8 (e.what ()); }
      catch (const string& e) { result->error= QString::fromUtf8 (e.c_str (), N (e)); }
      QMetaObject::invokeMethod (qApp, [guard, result, i, job, expected] {
        if (!guard) return;
        guard->checkCounterChanges ();
        if (job != guard->counter_job_) return;
        if (!result->error.isEmpty ()) { guard->counterError (result->error); return; }
        if (result->stale || !guard->counters_.publish (i, expected, result->output)) {
          guard->invalidateCounters (i);
          return;
        }
        guard->finishCounters (i);
      }, Qt::QueuedConnection);
    };
    if (!submit (i, work)) counter_pending_= false;
  }

  void scheduleCounters () {
    if (counter_pending_ || counter_failed_ || counter_next_ >= members_.size () ||
        counter_next_ > counter_target_) return;
    const std::size_t i= counter_next_;
    const auto preceding= i == 0 ? std::optional<athena::avd::counter_checkpoint> () : counters_.before (i);
    if (i != 0 && !preceding) return;
    const QByteArray incoming= preceding ? preceding->counters_xml : counter_bytes (tree (COLLECTION));
    struct Probe { QByteArray content, environment; QString error; std::uint64_t epoch= 0; };
    auto result= std::make_shared<Probe> ();
    const auto job= ++counter_job_;
    const QPointer<QTMCompoundViewport> guard (this);
    counter_pending_= true;
    try {
      if (!submit (i, [guard, result, i, incoming, job] {
        try {
          const auto* context= current_scheme_execution_context ();
          auto* editor= dynamic_cast<edit_typeset_rep*> (context->editor);
          result->epoch= context->actor->source_epoch ();
          result->content= QByteArray::fromStdString (athena::document::semantic_document_fingerprint (
            context->actor->current_source (context->view_id)));
          result->environment= QCryptographicHash::hash (
            counter_bytes (editor->compound_counter_environment ()), QCryptographicHash::Sha256);
        }
        catch (const std::exception& e) { result->error= QString::fromUtf8 (e.what ()); }
        catch (const string& e) { result->error= QString::fromUtf8 (e.c_str (), N (e)); }
        QMetaObject::invokeMethod (qApp, [guard, result, i, incoming, job] {
          if (!guard) return;
          guard->checkCounterChanges ();
          if (job != guard->counter_job_) return;
          if (!result->error.isEmpty ()) { guard->counterError (result->error); return; }
          const auto view= concrete_view (guard->views_[i].view);
          if (!view || view->buf->actor->source_epoch () != result->epoch) {
            guard->invalidateCounters (i);
            return;
          }
          guard->counters_.set_revision (i, result->content, result->environment);
          guard->views_[i].source_epoch= result->epoch;
          guard->computeCounters (i, incoming, job, result->epoch);
        }, Qt::QueuedConnection);
      })) counter_pending_= false;
    }
    catch (const std::exception& e) { counterError (QString::fromUtf8 (e.what ())); }
  }

  void saveCounterCache () {
    if (!cache_dirty_ || cache_write_failed_) return;
    try {
      descriptor_.value.checkpoints= counters_.persistent_checkpoints ();
      descriptor_.revision= athena::avd::save_descriptor (
        filename_, descriptor_.value, descriptor_.revision);
      cache_dirty_= false;
    }
    catch (const std::exception& error) {
      cache_write_failed_= true;
      std_warning << "Could not save compound counter cache: " << string (error.what ()) << LF;
    }
  }

  void suspend (SourceView& item) {
    if (!item.canvas || !item.visible) return;
    item.host->hide ();
    const auto view= concrete_view (item.view);
    if (view) view->buf->actor->submit (actor_command_kind::suspend_view, view->runtime_id);
    item.visible= false;
  }

  void arrange () {
    if (arranging_ || !isVisible ()) return;
    arranging_= true;
    try {
      const double start= offset (), end= start + viewport ()->height ();
      const auto first= layout_.member_at (start);
      counter_target_= first;
      for (std::size_t i= 0; i < views_.size (); ++i)
        if (i < first || layout_.top (i) >= end) suspend (views_[i]);
      for (std::size_t i= first; i < members_.size () && layout_.top (i) < end; ++i) {
        const double source_top= layout_.top (i) + member_heading_height - start;
        const int top= int (std::max (0.0, source_top));
        const int bottom= int (std::min (double (viewport ()->height ()),
                                        layout_.top (i + 1) - start));
        if (bottom <= top) continue;
        counter_target_= i;
        mount (i);
        auto& item= views_[i];
        item.host->setGeometry (0, top, viewport ()->width (), bottom - top);
        if (!item.visible) {
          const auto view= concrete_view (item.view);
          view->buf->actor->submit (actor_command_kind::resume_view, view->runtime_id);
          item.visible= true;
          item.host->show ();
        }
        if (active_member_ == std::numeric_limits<std::size_t>::max ())
          activateMember (i);
        auto origin= item.canvas->origin ();
        origin.setY (int (std::max (0.0, -source_top)));
        item.canvas->setOrigin (origin);
      }
      viewport ()->update ();
      scheduleCounters ();
    }
    catch (const std::exception& error) {
      refresh_.stop ();
      std_error << "Compound document: " << string (error.what ()) << LF;
    }
    arranging_= false;
  }

  void refreshGeometry () {
    if (arranging_) return;
    owner_.refreshToolbars ();
    if (active_member_ < views_.size ()) {
      if (auto* host= qobject_cast<QTMWindow*> (views_[active_member_].host.data ())) {
        owner_.editorStatus= host->editorStatus;
        owner_.editorStatus.visible= get_preference ("status bar", "on") == "on";
      }
    }
    checkCounterChanges ();
    scheduleCounters ();
    const double old_offset= offset ();
    const auto anchor= layout_.member_at (old_offset);
    const double within= anchor < layout_.size () ? old_offset - layout_.top (anchor) : 0;
    const bool at_end= verticalScrollBar ()->maximum () > 0 &&
      verticalScrollBar ()->value () == verticalScrollBar ()->maximum ();
    bool changed= false;
    for (std::size_t i= 0; i < views_.size (); ++i) {
      const auto& item= views_[i];
      if (!item.canvas || !item.visible || item.canvas->extents ().height () <= 0) continue;
      const double height= item.canvas->extents ().height () + member_heading_height;
      if (height != layout_.height (i)) {
        layout_.set_height (i, height);
        changed= true;
      }
    }
    if (changed) {
      updateRange (at_end ? layout_.extent () :
        anchor < layout_.size () ? layout_.top (anchor) + within : 0);
      arrange ();
    }
  }

protected:
  bool eventFilter (QObject* watched, QEvent* event) override {
    if (event->type () == QEvent::ShortcutOverride || event->type () == QEvent::KeyPress) {
      auto* key= static_cast<QKeyEvent*> (event);
      if (key->modifiers () == Qt::ControlModifier &&
          (key->key () == Qt::Key_Home || key->key () == Qt::Key_End)) {
        if (event->type () == QEvent::KeyPress && !members_.empty ()) {
          const bool at_end= key->key () == Qt::Key_End;
          goToMember (at_end ? members_.size () - 1 : 0, at_end);
        }
        event->accept ();
        return true;
      }
    }
    if (event->type () == QEvent::Wheel) {
      auto* wheel= static_cast<QWheelEvent*> (event);
      if (!(wheel->modifiers () & Qt::ControlModifier)) {
        const double delta= wheel->pixelDelta ().isNull () ?
          wheel->angleDelta ().y () / 120.0 * 120.0 : wheel->pixelDelta ().y ();
        updateRange (offset () - delta);
        arrange ();
        event->accept ();
        return true;
      }
    }
    if (event->type () == QEvent::FocusIn || event->type () == QEvent::MouseButtonPress) {
      for (std::size_t i= 0; i < views_.size (); ++i) {
        auto& item= views_[i];
        if (item.canvas && (watched == item.canvas || watched == item.canvas->viewport () ||
                            watched == item.canvas->surface ())) {
          activateMember (i);
          break;
        }
      }
    }
    return QAbstractScrollArea::eventFilter (watched, event);
  }

  void scrollContentsBy (int, int) override { arrange (); }
  void resizeEvent (QResizeEvent* event) override {
    QAbstractScrollArea::resizeEvent (event);
    updateRange (offset ());
    arrange ();
  }
  void showEvent (QShowEvent* event) override {
    QAbstractScrollArea::showEvent (event);
    updateRange (offset ());
    arrange ();
  }
  void paintEvent (QPaintEvent*) override {
    QPainter painter (viewport ());
    painter.fillRect (viewport ()->rect (), palette ().brush (QPalette::Base));
    if (members_.empty ()) {
      painter.setPen (palette ().color (QPalette::Text));
      painter.drawText (viewport ()->rect (), Qt::AlignCenter,
                        tr ("This namespace has no documents."));
      return;
    }
    const double start= offset ();
    for (auto i= layout_.member_at (start); i < members_.size (); ++i) {
      const int y= int (layout_.top (i) - start);
      if (y >= viewport ()->height ()) break;
      painter.setPen (QPen (palette ().color (QPalette::Mid), 1, Qt::DashLine));
      painter.drawLine (0, y, viewport ()->width (), y);
      painter.setPen (palette ().color (QPalette::Text));
      painter.drawText (QRect (8, y, std::max (0, viewport ()->width () - 16), member_heading_height),
        Qt::AlignVCenter | Qt::AlignLeft,
        fontMetrics ().elidedText (members_[i].relative_filename, Qt::ElideMiddle,
                                  std::max (0, viewport ()->width () - 16)));
    }
  }

public:
  QTMCompoundViewport (QTMCompoundDocument& owner, QString filename,
                       athena::avd::descriptor_file descriptor,
                       std::vector<athena::avd::member> members):
    QAbstractScrollArea (&owner), owner_ (owner), filename_ (std::move (filename)),
    descriptor_ (std::move (descriptor)), members_ (std::move (members)),
    layout_ (members_.size (), 1200),
    counters_ (members_, QByteArrayLiteral ("ATHENA counter executor 1"), descriptor_.value.checkpoints),
    views_ (members_.size ()) {
    setVerticalScrollBar (new MemberScrollBar (layout_));
    setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    refresh_.setInterval (100);
    connect (&refresh_, &QTimer::timeout, this, [this] { refreshGeometry (); });
    refresh_.start ();
    save_cache_.setSingleShot (true);
    save_cache_.setInterval (2000);
    connect (&save_cache_, &QTimer::timeout, this, [this] { saveCounterCache (); });
  }

  ~QTMCompoundViewport () override {
    refresh_.stop ();
    save_cache_.stop ();
    saveCounterCache ();
    owner_.activateMember (nullptr);
    for (auto& item: views_) {
      if (item.canvas) {
        item.canvas->removeEventFilter (this);
        item.canvas->viewport ()->removeEventFilter (this);
        item.canvas->surface ()->removeEventFilter (this);
      }
      if (item.host) {
        item.host->hide ();
        item.host->setParent (nullptr);
      }
      if (!is_none (item.window)) delete_window (item.window);
      const auto view= concrete_view (item.view);
      if (view) {
        const url view_url= item.view;
        const auto continuation= actor_continuation_registry::instance ().store (
          [view_url] {
            const auto* context= current_scheme_execution_context ();
            auto* editor= dynamic_cast<edit_typeset_rep*> (context->editor);
            editor->clear_compound_counters ();
            QMetaObject::invokeMethod (qApp, [view_url] {
              if (const auto source= concrete_view (view_url))
                source->compound_member= false;
            }, Qt::QueuedConnection);
          });
        view->buf->actor->submit (actor_command_kind::run_native_continuation,
          view->runtime_id, ATHENA_NO_BLOB, ATHENA_NO_BLOB,
          SCHEME_CAPABILITY_BUFFER, continuation);
      }
      // Keep the passive source view and its undo history. Closing an AVD is
      // not permission to discard unsaved source documents.
    }
  }

  const QString& filename () const { return filename_; }
  void goToMember (std::size_t i, bool at_end) {
    if (i >= members_.size ()) throw std::out_of_range ("Compound member index");
    updateRange (at_end ? layout_.top (i + 1) - viewport ()->height () : layout_.top (i));
    arrange ();
    mount (i);
    activateMember (i);
    views_[i].canvas->setFocus (Qt::OtherFocusReason);
    if (!submit (i, [at_end] {
          auto* editor= current_scheme_execution_context ()->editor;
          if (at_end) editor->go_to_end (editor->the_buffer_path ());
          else editor->go_to_start (editor->the_buffer_path ());
        }))
      std_warning << "Compound document: navigation could not be queued" << LF;
  }
};

QTMCompoundDocument::QTMCompoundDocument (
  QString filename, athena::avd::descriptor_file descriptor,
  std::vector<athena::avd::member> members): QTMWindow (nullptr) {
  setWindowTitle (QFileInfo (filename).fileName ());
  const std::array<QString, 4> definitions {
    QStringLiteral ("editor-main"), QStringLiteral ("editor-mode"),
    QStringLiteral ("editor-focus"), QStringLiteral ("editor-user")};
  const std::array<QString, 4> names {
    QStringLiteral ("mainToolBar"), QStringLiteral ("modeToolBar"),
    QStringLiteral ("focusToolBar"), QStringLiteral ("userToolBar")};
  for (std::size_t i= 0; i < toolbars_.size (); ++i) {
    auto* bar= new QTMToolbar (definitions[i], QSize (32, 32), this);
    bar->setObjectName (names[i]);
    bar->setFixedHeight (32);
    bar->setToolButtonStyle (get_preference ("text toolbar", "off") == "on" ?
                            Qt::ToolButtonTextOnly : Qt::ToolButtonIconOnly);
    if (tm_style_sheet == "" && !tmapp ()->useNewToolbar ())
      bar->setStyle (qtmstyle ());
    bar->setMovable (false);
    bar->setFocusPolicy (Qt::NoFocus);
    toolbars_[i]= bar;
    addToolBar (bar);
    addToolBarBreak ();
    toolbar_presenters_[i]= std::make_unique<QTMEditorToolbarPresenter> (
      nullptr, bar, definitions[i], i == 1 ? toolbars_[0] : nullptr);
  }
  toolbar_controller_= std::make_unique<QTMToolbarController> (
    this, toolbars_[0], toolbars_[1], toolbars_[2], toolbars_[3]);
  viewport_= std::make_unique<QTMCompoundViewport> (
    *this, std::move (filename), std::move (descriptor), std::move (members));
  setCentralWidget (viewport_.get ());
}

QTMCompoundDocument::~QTMCompoundDocument () { takeCentralWidget (); }
const QString& QTMCompoundDocument::filename () const { return viewport_->filename (); }
void QTMCompoundDocument::goToMember (std::size_t i, bool at_end) {
  viewport_->goToMember (i, at_end);
}

void QTMCompoundDocument::activateMember (QTMWidget* canvas) {
  if (editorCanvas () == canvas) return;
  setEditorCanvas (canvas);
  for (auto& presenter: toolbar_presenters_) presenter->setCanvas (canvas);
  refreshToolbars ();
}

void QTMCompoundDocument::refreshToolbars () {
  const bool enabled= editorCanvas () != nullptr && get_preference ("header") == "on";
  toolbar_controller_->setRequestedVisibility (
    enabled && get_preference ("main icon bar") == "on",
    enabled && get_preference ("mode dependent icons") == "on",
    enabled && get_preference ("focus dependent icons") == "on",
    enabled && get_preference ("user provided icons") == "on");
}

void compound_document_open (url filename) {
  const string native= as_string (filename, URL_SYSTEM);
  const QString path= QString::fromUtf8 (native.c_str (), N (native));
  QMetaObject::invokeMethod (qApp, [path] {
    auto context= vault_capture_context ();
    QThreadPool::globalInstance ()->start ([path, context] {
      struct Loaded {
        athena::avd::descriptor_file descriptor;
        std::vector<athena::avd::member> members;
        QString error;
      };
      auto result= std::make_shared<Loaded> ();
      try {
        result->descriptor= athena::avd::read_descriptor (path);
        result->members= athena::avd::resolve_members (path, result->descriptor.value, context);
      }
      catch (const std::exception& error) { result->error= QString::fromUtf8 (error.what ()); }
      QMetaObject::invokeMethod (qApp, [path, context, result] {
        if (result->error.isEmpty () && !vault_context_is_current (context))
          result->error= QStringLiteral ("The selected vault was closed while opening the compound document");
        if (!result->error.isEmpty ()) {
          const auto bytes= result->error.toUtf8 ();
          std_error << "Compound document: " << string (bytes.constData (), int (bytes.size ())) << LF;
          QMessageBox::warning (QApplication::activeWindow (), QObject::tr ("Open Compound Document"),
                                result->error);
          return;
        }
        auto* pane= new QTMCompoundDocument (path, std::move (result->descriptor), std::move (result->members));
        auto* shell= QTMMainTabWindow::topTabWindow ();
        QObject::connect (pane, &QTMWindow::closed, pane, [pane, shell] {
          shell->removeWidget (pane);
          pane->deleteLater ();
        });
        shell->showWidget (pane, true);
      }, Qt::QueuedConnection);
    });
  }, Qt::QueuedConnection);
}

void compound_document_create () {
  const auto context= vault_capture_context ();
  namespace_records<athena_namespace_definition> namespaces;
  string error;
  if (!context || athena_namespaces_list (context, namespaces, error) != namespace_query_status::ok) {
    QMessageBox::warning (QApplication::activeWindow (), QObject::tr ("New Compound Document"),
                          QObject::tr ("Open a vault before creating a compound document."));
    return;
  }
  QStringList names;
  for (const auto& ns: namespaces) names << QString::fromUtf8 (ns.name.c_str (), N (ns.name));
  if (names.isEmpty ()) {
    QMessageBox::information (QApplication::activeWindow (), QObject::tr ("New Compound Document"),
                              QObject::tr ("This vault has no namespaces."));
    return;
  }
  bool accepted= false;
  const auto name= QInputDialog::getItem (QApplication::activeWindow (),
    QObject::tr ("New Compound Document"), QObject::tr ("Namespace"), names, 0, false, &accepted);
  if (!accepted) return;
  const auto filename= QFileDialog::getSaveFileName (QApplication::activeWindow (),
    QObject::tr ("New Compound Document"), QString::fromStdString (context->root.string ()),
    QObject::tr ("ATHENA compound documents (*.avd)"));
  if (filename.isEmpty ()) return;
  const QString target= filename.endsWith (QStringLiteral (".avd"), Qt::CaseInsensitive) ?
    filename : filename + QStringLiteral (".avd");
  try {
    if (!vault_context_is_current (context)) throw std::runtime_error ("The selected vault was closed");
    const auto& ns= namespaces[std::size_t (names.indexOf (name))];
    athena::avd::descriptor descriptor;
    descriptor.namespace_uuid= QString::fromUtf8 (ns.uuid.c_str (), N (ns.uuid));
    descriptor.vault_directory= QDir (QFileInfo (target).absolutePath ())
      .relativeFilePath (QString::fromStdString (context->root.string ()));
    // Creating a view never overwrites a descriptor selected accidentally.
    (void) athena::avd::save_descriptor (target, descriptor, {});
    compound_document_open (source_url (target));
  }
  catch (const std::exception& problem) {
    QMessageBox::warning (QApplication::activeWindow (), QObject::tr ("New Compound Document"),
                          QString::fromUtf8 (problem.what ()));
  }
}
