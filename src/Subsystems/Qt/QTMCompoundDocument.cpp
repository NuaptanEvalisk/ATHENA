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
#include "athena_platform.hpp"
#include "QTMWidget.hpp"
#include "QTMMainTabWindow.hpp"
#include "QTMEditorToolbarPresenter.hpp"
#include "QTMToolbarController.hpp"
#include <QToolBar>
#include "QTMStyle.hpp"
#include "QTMInertialScroll.hpp"
#include <QLayout>
#include "ATHENA/Data/compound_document_edit.hpp"
#include "ATHENA/Data/compound_edit_batch.hpp"
#include "tree_cursor.hpp"
#include "scheme.hpp"
#include "qt_gui.hpp"
#include "qt_actor_widget.hpp"
#include "node_metadata.hpp"
#include "namespaces.hpp"
#include "namespace_ontology.hpp"
#include "new_buffer.hpp"
#include "new_view.hpp"
#include "new_window.hpp"
#include "tm_window.hpp"
#include "buffer_actor.hpp"
#include "buffer_state.hpp"
#include "QTMDocumentHistory.hpp"
#include "QTMVaultBackupDispatcher.hpp"
#include "new_style.hpp"
#include "Edit/Editor/edit_typeset.hpp"
#include "Edit/Editor/edit_main.hpp"
#include "qt_utilities.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "Data/Convert/Xml/clipboard_xml.hpp"
#include "convert.hpp"
#include "ATHENA/Data/node_reference_export.hpp"
#include "Subsystems/Pdf/PDFWriter/PDFWriter.h"
#include <QAbstractButton>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QProgressDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QDateTime>
#include <QInputMethodEvent>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QDir>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QKeyEvent>
#include <QMouseEvent>
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

class CompoundInputCommand: public command_rep {
  std::function<void()> callback_;
public:
  explicit CompoundInputCommand (std::function<void()> callback): callback_ (std::move (callback)) {}
  void apply () override { callback_ (); }
};

url source_url (const QString& filename) {
  const auto bytes= filename.toUtf8 ();
  return url_system (string (bytes.constData (), int (bytes.size ())));
}

QByteArray counter_bytes (const tree& state) {
  // Evaluated environments and counters may reuse source subtrees. They are
  // values, not new source objects; keep properties, not persistent identities.
  return QByteArray::fromStdString (athena::document::write_xml_v2 (
    athena::node::content_projection (state), athena::document::xml_kind::fragment));
}

tree counter_tree (const QByteArray& bytes) {
  return athena::document::read_xml_v2 (
    std::string_view (bytes.constData (), std::size_t (bytes.size ())),
    athena::document::xml_kind::fragment);
}

QByteArray file_revision (const QString& filename) {
  QFile file (filename);
  if (!file.exists ()) return {};
  if (!file.open (QIODevice::ReadOnly)) throw std::runtime_error (file.errorString ().toStdString ());
  QCryptographicHash digest (QCryptographicHash::Sha256);
  if (!digest.addData (&file)) throw std::runtime_error ("Could not fingerprint export destination");
  return digest.result ();
}

struct CompoundPdfJob {
  QTemporaryDir directory;
  QString destination;
  QByteArray original, counters;
  std::vector<std::uint64_t> epochs;
  QPointer<QProgressDialog> progress;
  std::atomic<bool> cancelled {false};
  bool preview= false;
  int next_page= -1;
};

void merge_compound_pdf (const std::shared_ptr<CompoundPdfJob>& job, std::size_t count) {
  const QString merged= job->directory.filePath ("compound.pdf");
  PDFWriter writer;
  if (writer.StartPDF (merged.toStdString (), ePDFVersion17) != PDFHummus::eSuccess)
    throw std::runtime_error ("Could not create compound PDF");
  for (std::size_t i= 0; i < count; ++i) {
    if (job->cancelled) throw std::runtime_error ("PDF export cancelled");
    if (writer.AppendPDFPagesFromPDF (job->directory.filePath (QString::number (i) + ".pdf").toStdString (),
                                     PDFPageRange ()).first != PDFHummus::eSuccess)
      throw std::runtime_error ("Could not append source PDF pages");
  }
  if (writer.EndPDF () != PDFHummus::eSuccess) throw std::runtime_error ("Could not finish compound PDF");
  QFile input (merged);
  QSaveFile output (job->destination);
  output.setDirectWriteFallback (false);
  if (!input.open (QIODevice::ReadOnly) || !output.open (QIODevice::WriteOnly))
    throw std::runtime_error ("Could not publish compound PDF");
  while (!input.atEnd ()) {
    const auto bytes= input.read (4 * 1024 * 1024);
    if (input.error () != QFileDevice::NoError || output.write (bytes) != bytes.size ())
      throw std::runtime_error ("Could not write compound PDF");
  }
  if (job->cancelled || file_revision (job->destination) != job->original)
    throw std::runtime_error ("Export cancelled or destination changed externally");
  if (!output.commit ()) throw std::runtime_error (output.errorString ().toStdString ());
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
    athena_view_id runtime_id= ATHENA_NO_VIEW;
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
  QTimer membership_timer_;
  vault_context_handle vault_= vault_capture_context ();
  std::uint64_t membership_revision_= 0;
  QString sorter_filename_;
  qint64 sorter_stamp_= 0;
  bool membership_pending_= false;
  std::size_t counter_next_= 0;
  std::size_t counter_target_= 0;
  std::uint64_t counter_job_= 0;
  std::uint64_t style_generation_= style_cache_generation ();
  bool counter_pending_= false;
  bool counter_failed_= false;
  bool cache_dirty_= false;
  bool cache_write_failed_= false;
  bool arranging_= false;
  QTMPerformanceMonitor performance_ {viewport ()};
  QString performance_preference_;
  bool forwarding_input_= false;
  std::size_t active_member_= std::numeric_limits<std::size_t>::max ();
  std::vector<athena::avd::source_range> selection_;
  std::shared_ptr<std::atomic<std::uint64_t>> selection_serial_=
    std::make_shared<std::atomic<std::uint64_t>> (0);
  std::size_t selection_pending_= 0;
  bool delete_when_ready_= false;
  bool edit_pending_= false;
  bool io_pending_= false;
  std::vector<std::unique_ptr<QEvent>> deferred_inputs_;
  std::shared_ptr<CompoundPdfJob> pdf_job_;
  QString pending_selection_command_;
  QString pending_input_;
  unsigned queued_selections_= 0;
  struct MouseEndpoint {
    std::size_t member;
    QPoint position;
    std::uint64_t epoch;
  };
  MouseEndpoint drag_anchor_ {};
  std::size_t drag_target_= 0;
  QPoint drag_global_;
  bool mouse_down_= false;
  bool compound_drag_= false;
  bool drag_dirty_= false;
  bool drag_queued_= false;
  QTimer drag_timer_;
  QPointer<QTMWidget> scroll_canvas_;
  QTMInertialScroll inertia_ {this, [this] (int dx, int dy) {
    if (dx != 0) {
      auto* bar= horizontalScrollBar ();
      bar->setValue (bar->value () - dx);
    }
    if (dy != 0) { updateRange (offset () - dy); arrange (true); }
  }};
  double scale_= 1;
  bool typewriter_mode_= false;

  double offset () const { return verticalScrollBar ()->value () * scale_; }

  void queueInput (std::function<void(QTMCompoundViewport&)> action) {
    QPointer<QTMCompoundViewport> self (this);
    the_gui->process_command (command (tm_new<CompoundInputCommand> (
      [self, action= std::move (action)] { if (self) action (*self); })));
  }

  void activateMember (std::size_t i) {
    if (active_member_ < views_.size () && active_member_ != i && views_[active_member_].canvas)
      views_[active_member_].canvas->setPresentationFocus (false);
    if (const auto source= concrete_runtime_view (views_[i].runtime_id)) views_[i].view= abstract_view (source);
    active_member_= i;
    owner_.activateMember (views_[i].canvas);
    set_current_view (views_[i].view);
    if (views_[i].canvas) views_[i].canvas->setPresentationFocus (hasFocus () || viewport ()->hasFocus ());
  }

  void updateRange (double desired) {
    typewriter_mode_= get_preference ("typewriter mode", "off") == "on";
    const double tail= typewriter_mode_ && !members_.empty () ? viewport ()->height () / 2.0 : 0;
    const double maximum= std::max (0.0, layout_.extent () + tail - viewport ()->height ());
    scale_= std::max (1.0, maximum / (std::numeric_limits<int>::max () - 1.0));
    QSignalBlocker blocked (verticalScrollBar ());
    verticalScrollBar ()->setRange (0, int (std::ceil (maximum / scale_)));
    verticalScrollBar ()->setPageStep (std::max (1, int (viewport ()->height () / scale_)));
    verticalScrollBar ()->setSingleStep (std::max (1, int (40 / scale_)));
    verticalScrollBar ()->setValue (int (std::round (std::clamp (desired, 0.0, maximum) / scale_)));
  }

  tm_view ensureView (std::size_t i) {
    auto& item= views_[i];
    if (item.runtime_id != ATHENA_NO_VIEW)
      if (auto view= concrete_runtime_view (item.runtime_id)) {
        item.view= abstract_view (view);
        return view;
      }
    if (!is_none (item.view))
      if (auto view= concrete_view (item.view)) return view;
    const url source= source_url (members_[i].filename);
    if (is_nil (concrete_buffer (source)) && buffer_load (source))
      throw std::runtime_error ("Could not open compound source: " + members_[i].filename.toStdString ());
    item.view= get_new_view (source);
    const auto view= concrete_view (item.view);
    item.runtime_id= view->runtime_id;
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
    // These are hidden input/render adapters. Only the compound viewport paints.
    item.host->hide ();
    item.canvas->setPresentationTarget (viewport ());
    the_gui->process_keyboard_focus (item.canvas->tm_widget (), false, texmacs_time ());
    item.canvas->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    item.canvas->setVerticalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    item.canvas->installEventFilter (this);
    item.canvas->viewport ()->installEventFilter (this);
    item.canvas->surface ()->installEventFilter (this);
    connect (item.canvas, &QTMWidget::presentationChanged,
             viewport (), QOverload<>::of (&QWidget::update));
    connect (item.canvas, &QTMScrollView::originRequested,
      this, [this, canvas= item.canvas] (QPoint position) {
        std::size_t i= 0;
        while (i < views_.size () && views_[i].canvas != canvas) ++i;
        if (i == views_.size ()) return;
        const auto& source= views_[i];
        if (arranging_ || !source.visible || i != active_member_ ||
            !canvas->editorHasFocus () || (compound_drag_ && mouse_down_)) return;
        inertia_.stop ();
        QSignalBlocker horizontal (horizontalScrollBar ());
        horizontalScrollBar ()->setValue (position.x ());
        updateRange (layout_.top (i) + member_heading_height + position.y ());
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

  void clearSelection (bool keep_drag= false) {
    if (!keep_drag) {
      pending_selection_command_.clear ();
      pending_input_.clear ();
      compound_drag_= false;
      drag_dirty_= false;
      drag_timer_.stop ();
      if (QWidget::mouseGrabber () == viewport ()) viewport ()->releaseMouse ();
    }
    ++*selection_serial_;
    selection_pending_= 0;
    delete_when_ready_= false;
    for (const auto& range: selection_) {
      const auto continuation= actor_continuation_registry::instance ().store ([] {
        if (auto* editor= current_scheme_execution_context ()->editor)
          editor->selection_cancel ();
      });
      if (!buffer_actor::try_submit_to (range.actor,
            actor_command_kind::run_native_continuation, range.view,
            ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER, continuation))
        actor_continuation_registry::instance ().discard (continuation);
    }
    selection_.clear ();
  }

  void selectRange (std::size_t first_member, std::size_t last_member,
                    bool from_cursor= false, bool to_cursor= false,
                    std::shared_ptr<std::pair<MouseEndpoint, MouseEndpoint>> points= {}) {
    if (edit_pending_ || members_.empty ()) return;
    const bool deferred_delete= points && delete_when_ready_;
    clearSelection (bool (points));
    delete_when_ready_= deferred_delete;
    const auto serial= selection_serial_->load ();
    const auto token= selection_serial_;
    QPointer<QTMCompoundViewport> self (this);
    const auto count= last_member - first_member + 1;
    auto ranges= std::make_shared<std::vector<athena::avd::source_range>> (count);
    std::vector<athena::avd::edit_participant> participants;
    try {
      for (std::size_t i= 0; i < count; ++i) {
        const auto view= ensureView (first_member + i);
        auto& range= (*ranges)[i];
        range.actor= view->buf->actor->id ();
        range.view= view->runtime_id;
        athena::avd::edit_participant member;
        member.actor= range.actor;
        member.view= range.view;
        member.prepare= [token, serial, points, source= first_member + i] (editor_rep&) {
          if (token->load () != serial) throw std::runtime_error ("Selection was cancelled");
          if (points) {
            const auto epoch= current_scheme_execution_context ()->actor->source_epoch ();
            for (const auto& endpoint: {points->first, points->second})
              if (endpoint.member == source && endpoint.epoch != epoch)
                throw std::runtime_error ("Source changed during compound selection");
          }
        };
        member.apply= [ranges, i, count, from_cursor, to_cursor, points,
                      source= first_member + i] (editor_rep& editor) {
          const tree body= editor.the_buffer ();
          const path root= editor.the_buffer_path ();
          path first= start (body), last= end (body);
          if ((from_cursor && i == 0) || (to_cursor && i + 1 == count)) {
            const path cursor= editor.the_path ();
            if (!(root <= cursor)) throw std::runtime_error ("Cursor is outside the source body");
            const path relative= cursor / root;
            auto& destination= from_cursor ? (*ranges)[i].first : (*ranges)[i].last;
            for (path p= relative; !is_nil (p); p= p->next) destination.push_back (p->item);
            if (from_cursor) first= relative;
            else last= relative;
          }
          if (from_cursor && i + 1 == count) editor.go_to (root * last);
          if (to_cursor && i == 0) editor.go_to (root * first);
          if (points) {
            auto hit= [&] (const MouseEndpoint& endpoint) {
              const auto point= from_qpoint (endpoint.position);
              const path target= editor.document_position_at (point.x1, point.x2);
              if (!(root <= target)) throw std::runtime_error ("Selection is outside the source body");
              return target / root;
            };
            if (source == points->first.member) first= hit (points->first);
            if (source == points->second.member) last= hit (points->second);
            if (path_less (last, first)) std::swap (first, last);
            for (path p= first; !is_nil (p); p= p->next) (*ranges)[i].first.push_back (p->item);
            for (path p= last; !is_nil (p); p= p->next) (*ranges)[i].last.push_back (p->item);
          }
          editor.select (root * first, root * last);
          (*ranges)[i].epoch= current_scheme_execution_context ()->actor->source_epoch ();
        };
        member.rollback= [] (editor_rep& editor) { editor.selection_cancel (); };
        member.commit= [] (editor_rep&) {};
        participants.push_back (std::move (member));
      }
    }
    catch (const std::exception& error) {
      std_warning << "Compound selection: " << string (error.what ()) << LF;
      return;
    }
    catch (const string& error) { std_warning << "Compound selection: " << error << LF; return; }
    selection_= *ranges;
    selection_pending_= 1;
    athena::avd::submit_edit_batch (std::move (participants),
      [self, serial, ranges, first_member, last_member, from_cursor, to_cursor] (std::string error) {
        QMetaObject::invokeMethod (qApp, [self, serial, ranges, first_member, last_member,
                                        from_cursor, to_cursor, error= std::move (error)] {
          if (!self || self->selection_serial_->load () != serial) return;
          if (!error.empty ()) {
            self->clearSelection ();
            std_warning << "Compound selection: " << string (error.c_str ()) << LF;
            return;
          }
          self->selection_= *ranges;
          self->selection_pending_= 0;
          self->updateDrag ();
          if (self->compound_drag_ && !self->mouse_down_ && !self->drag_dirty_ && !self->drag_queued_) {
            self->activateMember (self->drag_target_);
            self->views_[self->drag_target_].canvas->focusEditor (Qt::OtherFocusReason);
          }
          if (from_cursor || to_cursor) {
            const auto target= from_cursor ? last_member : first_member;
            self->updateRange (from_cursor ? self->layout_.top (target + 1) - self->viewport ()->height () :
                                            self->layout_.top (target));
            self->arrange ();
            self->mount (target);
            self->activateMember (target);
            self->views_[target].canvas->focusEditor (Qt::OtherFocusReason);
          }
          if (self->delete_when_ready_) self->eraseSelection ();
          if (!self->pending_selection_command_.isEmpty () && !self->drag_queued_ && !self->drag_dirty_) {
            const auto id= self->pending_selection_command_, input= self->pending_input_;
            self->pending_selection_command_.clear ();
            self->selectionCommand (id, input);
          }
        }, Qt::QueuedConnection);
      });
  }

  void selectAll () {
    if (!members_.empty ()) selectRange (0, members_.size () - 1);
  }

  MouseEndpoint mouseEndpoint (std::size_t member, QPoint global) {
    mount (member);
    const auto& canvas= views_[member].canvas;
    const auto view= concrete_runtime_view (views_[member].runtime_id);
    return {member, canvas->surface ()->mapFromGlobal (global) + canvas->origin (),
            view->buf->actor->source_epoch ()};
  }

  void updateDrag () {
    if (!compound_drag_ || edit_pending_) return;
    QPoint local= viewport ()->mapFromGlobal (drag_global_);
    if (mouse_down_) {
      const int edge= 24;
      const int delta= local.y () < edge ? local.y () - edge :
        local.y () > viewport ()->height () - edge ? local.y () - viewport ()->height () + edge : 0;
      if (delta != 0) {
        updateRange (offset () + std::clamp (delta, -80, 80));
        arrange (true);
        drag_dirty_= true;
      }
    }
    if (!drag_dirty_ || drag_queued_ || selection_pending_ != 0) return;
    local.setY (std::clamp (local.y (), 0, std::max (0, viewport ()->height () - 1)));
    const auto member= layout_.member_at (offset () + local.y ());
    if (member >= members_.size ()) return;
    drag_target_= member;
    auto endpoint= mouseEndpoint (member, viewport ()->mapToGlobal (local));
    auto points= std::make_shared<std::pair<MouseEndpoint, MouseEndpoint>> (drag_anchor_, endpoint);
    if (points->first.member > points->second.member) std::swap (points->first, points->second);
    drag_dirty_= false;
    drag_queued_= true;
    ++queued_selections_;
    queueInput ([points] (QTMCompoundViewport& self) {
      self.drag_queued_= false;
      --self.queued_selections_;
      if (self.compound_drag_)
        self.selectRange (points->first.member, points->second.member, false, false, points);
    });
  }

  void eraseSelection (std::string replacement= {}) {
    if (selection_.empty () || edit_pending_) return;
    if (selection_pending_ != 0 || drag_dirty_ || drag_queued_) { delete_when_ready_= true; return; }
    edit_pending_= true;
    delete_when_ready_= false;
    QPointer<QTMCompoundViewport> self (this);
    const auto first_view= selection_.front ().view;
    athena::avd::replace_ranges (selection_, std::move (replacement), [self, first_view] (std::string error) {
      QMetaObject::invokeMethod (qApp, [self, first_view, error= std::move (error)] {
        if (!error.empty ())
          std_warning << "Compound deletion: " << string (error.c_str ()) << LF;
        if (!self) return;
        self->edit_pending_= false;
        if (error.empty ()) {
          self->clearSelection ();
          for (std::size_t i= 0; i < self->views_.size (); ++i)
            if (self->views_[i].runtime_id == first_view) {
              self->updateRange (self->layout_.top (i));
              self->arrange (); self->mount (i); self->activateMember (i);
              self->views_[i].canvas->focusEditor (Qt::OtherFocusReason);
              self->submit (i, [] { current_scheme_execution_context ()->editor->go_to_here (); });
              break;
            }
        }
      }, Qt::QueuedConnection);
    });
  }

  void selectionCommand (QString id, QString input= {}) {
    if (edit_pending_) return;
    if (selection_pending_ || drag_dirty_ || drag_queued_) {
      pending_selection_command_= id; pending_input_= input;
      return;
    }
    if (id == "editor.paste" || id == "avd.input") {
      tree value; string bytes;
      if (id == "avd.input") value= tuple ("texmacs",
        input == "\r" || input == "\n" ? tree (DOCUMENT, "", "") : tree (from_qstring_utf8 (input)), "text", "english");
      else if (!::get_selection ("primary", value, bytes, "default")) return;
      eraseSelection (athena::document::write_clipboard_xml (value));
      return;
    }
    if (id == "editor.delete") { eraseSelection (); return; }
    if (id != "editor.copy" && id != "editor.cut") return;
    edit_pending_= true;
    QPointer<QTMCompoundViewport> self (this);
    const auto serial= selection_serial_->load ();
    athena::avd::copy_ranges (selection_, [self, id, serial] (std::string error, std::vector<std::string> pieces) {
      QMetaObject::invokeMethod (qApp, [self, id, serial, error= std::move (error), pieces= std::move (pieces)] {
        if (!self) return;
        self->edit_pending_= false;
        if (!error.empty ()) { std_warning << "Compound clipboard: " << string (error.c_str ()) << LF; return; }
        if (self->selection_serial_->load () != serial) return;
        tree body (DOCUMENT);
        for (const auto& bytes: pieces) {
          const tree envelope= athena::document::read_clipboard_xml (bytes);
          tree content= envelope[1];
          if (envelope[2] == "math") content= compound ("math", content);
          if (is_document (content)) for (int i= 0; i < N(content); ++i) body << content[i];
          else body << content;
        }
        const tree envelope= tuple ("texmacs", body, "text", "english");
        const auto bytes= athena::document::write_clipboard_xml (envelope);
        if (!::set_selection ("primary", envelope, string (bytes.data (), int (bytes.size ())),
                              tree_to_verbatim (body, false, "utf-8"), "", "default")) return;
        if (id == "editor.cut") self->eraseSelection ();
      }, Qt::QueuedConnection);
    });
  }

  void reportFailure (const QString& operation, const QString& error) {
    std_error << "Compound document " << from_qstring_utf8 (operation) << ": "
              << from_qstring_utf8 (error) << LF;
    QMessageBox::warning (&owner_, operation, error);
  }

  void inspectModified (std::function<void(std::vector<std::size_t>)> completion) {
    auto dirty= std::make_shared<std::vector<unsigned char>> (members_.size (), 0);
    std::vector<athena::avd::edit_participant> participants;
    for (std::size_t i= 0; i < members_.size (); ++i) {
      if (is_nil (concrete_buffer (source_url (members_[i].filename)))) continue;
      const auto view= ensureView (i);
      athena::avd::edit_participant member;
      member.actor= view->buf->actor->id (); member.view= view->runtime_id;
      member.prepare= [dirty, i] (editor_rep& editor) {
        (*dirty)[i]= editor.need_save () || current_scheme_execution_context ()->actor->current_state ()->source_modified;
      };
      member.apply= member.rollback= member.commit= [] (editor_rep&) {};
      participants.push_back (std::move (member));
    }
    if (participants.empty ()) { completion ({}); return; }
    io_pending_= true;
    QPointer<QTMCompoundViewport> self (this);
    athena::avd::submit_edit_batch (std::move (participants),
      [self, dirty, completion= std::move (completion)] (std::string error) {
        QMetaObject::invokeMethod (qApp, [self, dirty, completion, error= std::move (error)] {
          if (!self) return;
          self->io_pending_= false;
          if (!error.empty ()) { self->reportFailure (tr ("Inspect Compound Documents"), QString::fromStdString (error)); return; }
          std::vector<std::size_t> result;
          for (std::size_t i= 0; i < dirty->size (); ++i) if ((*dirty)[i]) result.push_back (i);
          completion (std::move (result));
        }, Qt::QueuedConnection);
      });
  }

  void saveSources (std::shared_ptr<std::vector<std::size_t>> indices,
                    std::size_t next, bool close_after) {
    if (next == indices->size ()) {
      io_pending_= false;
      saveCounterCache ();
      if (close_after) requestClose ();
      return;
    }
    const auto i= (*indices)[next];
    const auto view= ensureView (i);
    qtm_document_history_manual_request (view->buf);
    io_pending_= true;
    QPointer<QTMCompoundViewport> self (this);
    const auto filename= members_[i].filename;
    athena::avd::submit_source_task (view->buf->actor->id (), view->runtime_id,
      [] (editor_rep& editor) {
        auto* state= current_scheme_execution_context ()->actor->current_state ();
        if (!state->source_modified && !editor.need_save ()) return;
        if (state->read_only) throw std::runtime_error ("Source is read-only");
        if (buffer_save (state->name)) throw std::runtime_error ("Could not save source document; original file retained");
      }, [self, indices, next, close_after, filename] (std::string error) {
        QMetaObject::invokeMethod (qApp, [self, indices, next, close_after, filename, error= std::move (error)] {
          if (!self) return;
          if (!error.empty ()) {
            self->io_pending_= false;
            self->reportFailure (tr ("Save Compound Documents"), filename + ": " + QString::fromStdString (error));
            return;
          }
          qtm_vault_backup_dispatch_realtime (filename);
          self->saveSources (indices, next + 1, close_after);
        }, Qt::QueuedConnection);
      });
  }

  void saveAll (bool close_after= false) {
    if (edit_pending_ || io_pending_) return;
    inspectModified ([this, close_after] (std::vector<std::size_t> dirty) {
      saveSources (std::make_shared<std::vector<std::size_t>> (std::move (dirty)), 0, close_after);
    });
  }

  void finishClose () {
    auto* shell= QTMMainTabWindow::topTabWindow ();
    shell->removeWidget (&owner_);
    owner_.deleteLater ();
  }

  void finishPdf (const std::shared_ptr<CompoundPdfJob>& job, const QString& error) {
    if (job->progress) job->progress->deleteLater ();
    io_pending_= false;
    pdf_job_.reset ();
    invalidateCounters (0);
    if (!error.isEmpty () && !job->cancelled) reportFailure (tr ("Export Compound PDF"), error);
    else if (error.isEmpty () && job->preview)
      QDesktopServices::openUrl (QUrl::fromLocalFile (job->destination));
  }

  void exportMember (const std::shared_ptr<CompoundPdfJob>& job, std::size_t index) {
    if (job->cancelled) { finishPdf (job, tr ("Cancelled")); return; }
    if (index == members_.size ()) {
      for (std::size_t i= 0; i < members_.size (); ++i) {
        const auto source= concrete_runtime_view (views_[i].runtime_id);
        if (!source || source->buf->actor->source_epoch () != job->epochs[i]) {
          finishPdf (job, tr ("A source changed during export; please export again.")); return;
        }
      }
      QPointer<QTMCompoundViewport> self (this);
      QThreadPool::globalInstance ()->start ([self, job, count= members_.size ()] {
        QString error;
        try { merge_compound_pdf (job, count); }
        catch (const std::exception& e) { error= QString::fromUtf8 (e.what ()); }
        QMetaObject::invokeMethod (qApp, [self, job, error] { if (self) self->finishPdf (job, error); }, Qt::QueuedConnection);
      });
      return;
    }
    job->progress->setValue (int (index));
    job->progress->setLabelText (members_[index].relative_filename);
    tm_view view;
    try { view= ensureView (index); }
    catch (const std::exception& error) { finishPdf (job, QString::fromUtf8 (error.what ())); return; }
    catch (const string& error) { finishPdf (job, to_qstring (error)); return; }
    const auto incoming= job->counters;
    QPointer<QTMCompoundViewport> self (this);
    const auto finish= [self, job, index] (std::string error) {
      QMetaObject::invokeMethod (qApp, [self, job, index, error= std::move (error)] {
        if (!self) return;
        if (!error.empty ()) self->finishPdf (job, QString::fromStdString (error));
        else self->exportMember (job, index + 1);
      }, Qt::QueuedConnection);
    };
    athena::avd::submit_source_task (view->buf->actor->id (), view->runtime_id,
      [job, index, incoming, finish] (editor_rep& editor) {
        auto& typesetter= dynamic_cast<edit_typeset_rep&> (editor);
        typesetter.set_compound_counters (incoming.isEmpty () ? tree (COLLECTION) : counter_tree (incoming));
        athena_node_reference_with_native_export ([job, index, incoming] (editor_rep& source) {
          if (job->cancelled) throw std::runtime_error ("PDF export cancelled");
          auto& typesetter= dynamic_cast<edit_typeset_rep&> (source);
          job->counters= counter_bytes (typesetter.evaluate_compound_counters (
            incoming.isEmpty () ? tree (COLLECTION) : counter_tree (incoming)));
          job->next_page= dynamic_cast<edit_main_rep&> (source).print_doc_numbered (
            source_url (job->directory.filePath (QString::number (index) + ".pdf")), job->next_page);
          job->epochs[index]= current_scheme_execution_context ()->actor->source_epoch ();
        }, finish);
      }, [finish] (std::string error) { if (!error.empty ()) finish (std::move (error)); });
  }

  void exportPdf (bool preview) {
    if (io_pending_ || edit_pending_ || members_.empty ()) return;
    const auto target= QFileDialog::getSaveFileName (&owner_, tr ("Export Compound PDF"),
      QFileInfo (filename_).absolutePath () + "/" + QFileInfo (filename_).completeBaseName () + ".pdf",
      tr ("PDF documents (*.pdf)"));
    if (target.isEmpty ()) return;
    auto job= std::make_shared<CompoundPdfJob> ();
    if (!job->directory.isValid ()) { reportFailure (tr ("Export Compound PDF"), tr ("Could not create temporary directory")); return; }
    job->destination= target;
    try { job->original= file_revision (target); }
    catch (const std::exception& e) { reportFailure (tr ("Export Compound PDF"), QString::fromUtf8 (e.what ())); return; }
    job->preview= preview;
    job->epochs.resize (members_.size ());
    job->progress= new QProgressDialog (tr ("Preparing compound PDF"), tr ("Cancel"), 0, int (members_.size ()), &owner_);
    job->progress->setAutoClose (false);
    connect (job->progress, &QProgressDialog::canceled, this, [weak= std::weak_ptr<CompoundPdfJob> (job)] {
      if (auto job= weak.lock ()) job->cancelled= true;
    });
    job->progress->show ();
    io_pending_= true;
    pdf_job_= job;
    ++counter_job_;
    exportMember (job, 0);
  }

  void saveAs () {
    if (io_pending_ || edit_pending_) return;
    QString target= QFileDialog::getSaveFileName (&owner_, tr ("Save Compound Document As"), filename_, tr ("Compound documents (*.avd)"));
    if (target.isEmpty ()) return;
    if (!target.endsWith (".avd", Qt::CaseInsensitive)) target += ".avd";
    auto value= descriptor_.value;
    const auto vault= QDir (QFileInfo (filename_).absolutePath ()).absoluteFilePath (value.vault_directory);
    value.vault_directory= QDir (QFileInfo (target).absolutePath ()).relativeFilePath (vault);
    value.checkpoints= counters_.persistent_checkpoints ();
    try {
      descriptor_.revision= athena::avd::save_descriptor (target, value, file_revision (target));
      descriptor_.value= std::move (value);
      filename_= target;
      cache_dirty_= cache_write_failed_= false;
      owner_.setWindowTitle (QFileInfo (target).fileName ());
      (void) call ("buffer-notify-recent", object (source_url (target)));
      saveAll ();
    }
    catch (const std::exception& e) { reportFailure (tr ("Save Compound Document As"), QString::fromUtf8 (e.what ())); }
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
      const auto view= concrete_runtime_view (views_[i].runtime_id);
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
    if (io_pending_) return;
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
          // current_source also contains evaluated references/auxiliary data,
          // which may repeat source nodes after export. Fingerprint values,
          // not their source identities, just as for the environment below.
          result->content= QCryptographicHash::hash (
            counter_bytes (context->actor->current_source (context->view_id)), QCryptographicHash::Sha256);
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
          const auto view= concrete_runtime_view (guard->views_[i].runtime_id);
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
    item.canvas->setPresentationActive (false);
    const auto view= concrete_runtime_view (item.runtime_id);
    if (view) view->buf->actor->submit (actor_command_kind::suspend_view, view->runtime_id);
    item.visible= false;
  }

  void arrange (bool user_scroll= false) {
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
        const bool resized= item.canvas->surface ()->size () != viewport ()->size ();
        const bool resumed= !item.visible;
        // Stable render dimensions across boundaries: clipping is compositor-owned.
        item.host->setGeometry (viewport ()->rect ());
        item.host->layout ()->activate ();
        item.canvas->setGeometry (QRect (QPoint (), viewport ()->size ()));
        item.canvas->viewport ()->setGeometry (item.canvas->rect ());
        item.canvas->surface ()->setGeometry (item.canvas->viewport ()->rect ());
        if (!item.visible) {
          const auto view= concrete_runtime_view (item.runtime_id);
          view->buf->actor->submit (actor_command_kind::resume_view, view->runtime_id);
          item.visible= true;
          item.canvas->setPresentationActive (true);
          // Resuming a render participant must not steal the composite's focus.
          the_gui->process_keyboard_focus (item.canvas->tm_widget (),
            item.canvas->editorHasFocus (), texmacs_time ());
        }
        if (active_member_ == std::numeric_limits<std::size_t>::max ())
          activateMember (i);
        auto origin= item.canvas->origin ();
        origin.setX (horizontalScrollBar ()->value ());
        // Every adapter sees the same complete compound viewport in its own
        // document coordinates, including the area above its source's start.
        origin.setY (int (std::round (-source_top)));
        const bool moved= origin != item.canvas->origin ();
        item.canvas->setExternalOrigin (origin);
        if (moved || resumed || resized) {
          if (user_scroll) item.canvas->publishUserScroll ();
          else if (auto* proxy= dynamic_cast<qt_actor_widget_rep*> (item.canvas->tm_widget ()))
            proxy->refresh_viewport ();
        }
        if (resized) {
          const auto size= from_qsize (viewport ()->size ());
          the_gui->process_resize (item.canvas->tm_widget (), size.x1, size.x2);
        }
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
    const auto preference= to_qstring (get_preference ("rendering performance monitor", "off"));
    if (preference != performance_preference_) {
      performance_preference_= preference;
      performance_.refresh ();
    }
    if (!edit_pending_ && !io_pending_ && !selection_pending_ && !queued_selections_ && !drag_queued_ &&
        !mouse_down_ && active_member_ < views_.size () && views_[active_member_].canvas) {
      auto pending= std::move (deferred_inputs_);
      deferred_inputs_.clear ();
      for (auto& event: pending) QCoreApplication::postEvent (views_[active_member_].canvas, event.release ());
    }
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
    bool changed= typewriter_mode_ != (get_preference ("typewriter mode", "off") == "on");
    int width= viewport ()->width ();
    for (std::size_t i= 0; i < views_.size (); ++i) {
      const auto& item= views_[i];
      if (!item.canvas || !item.visible || item.canvas->extents ().height () <= 0) continue;
      width= std::max (width, item.canvas->extents ().width ());
      const double height= item.canvas->extents ().height () + member_heading_height;
      if (height != layout_.height (i)) {
        layout_.set_height (i, height);
        changed= true;
      }
    }
    {
      QSignalBlocker horizontal (horizontalScrollBar ());
      const int old_x= horizontalScrollBar ()->value ();
      horizontalScrollBar ()->setRange (0, width - viewport ()->width ());
      horizontalScrollBar ()->setPageStep (viewport ()->width ());
      changed= changed || old_x != horizontalScrollBar ()->value ();
    }
    if (changed) {
      updateRange (at_end ? layout_.extent () :
        anchor < layout_.size () ? layout_.top (anchor) + within : 0);
      arrange ();
    }
  }

  void releaseSource (SourceView& item) {
    if (item.canvas) {
      item.canvas->setPresentationFocus (false);
      item.canvas->setPresentationActive (false);
      item.canvas->setPresentationTarget (nullptr);
      item.canvas->removeEventFilter (this);
      item.canvas->viewport ()->removeEventFilter (this);
      item.canvas->surface ()->removeEventFilter (this);
      disconnect (item.canvas, nullptr, this, nullptr);
    }
    if (item.host) { item.host->hide (); item.host->setParent (nullptr); }
    if (!is_none (item.window)) delete_window (item.window);
    if (const auto view= concrete_runtime_view (item.runtime_id)) {
      const auto view_id= item.runtime_id;
      athena::avd::submit_source_task (view->buf->actor->id (), view->runtime_id,
        [] (editor_rep& editor) { dynamic_cast<edit_typeset_rep&> (editor).clear_compound_counters (); },
        [view_id] (std::string error) {
          QMetaObject::invokeMethod (qApp, [view_id, error= std::move (error)] {
            if (!error.empty ()) std_warning << "Compound source detach: " << string (error.c_str ()) << LF;
            else if (const auto source= concrete_runtime_view (view_id)) source->compound_member= false;
          }, Qt::QueuedConnection);
        });
    }
  }

  void refreshMembers () {
    const auto revision= athena_namespace_ontology_revision ();
    const auto sorter_stamp= sorter_filename_.isEmpty () ? 0 : QFileInfo (sorter_filename_).lastModified ().toMSecsSinceEpoch ();
    if (membership_pending_ || (revision == membership_revision_ && sorter_stamp == sorter_stamp_) || io_pending_ || edit_pending_ ||
        mouse_down_ || selection_pending_ || !selection_.empty () || !vault_context_is_current (vault_)) return;
    membership_pending_= true;
    QPointer<QTMCompoundViewport> self (this);
    const auto filename= filename_;
    const auto descriptor= descriptor_.value;
    const auto vault= vault_;
    QThreadPool::globalInstance ()->start ([self, filename, descriptor, vault, revision] {
      std::vector<athena::avd::member> members;
      QString error, sorter;
      try {
        members= athena::avd::resolve_members (filename, descriptor, vault);
        std::shared_ptr<const athena_namespace_definition> definition;
        string diagnostic;
        if (athena_namespace_get_by_uuid (vault, from_qstring_utf8 (descriptor.namespace_uuid), definition, diagnostic) == namespace_query_status::ok &&
            !definition->sorter_trivial && definition->sorter_path != "")
          sorter= QDir (QString::fromStdString (vault->root.string ())).absoluteFilePath (to_qstring (definition->sorter_path));
      }
      catch (const std::exception& e) { error= QString::fromUtf8 (e.what ()); }
      QMetaObject::invokeMethod (qApp, [self, members= std::move (members), revision, error, sorter] () mutable {
        if (!self) return;
        self->membership_pending_= false;
        if (self->io_pending_ || self->edit_pending_ || self->mouse_down_ || !self->selection_.empty ()) return;
        self->membership_revision_= revision;
        if (!sorter.isEmpty ()) self->sorter_filename_= sorter;
        self->sorter_stamp_= QFileInfo (self->sorter_filename_).lastModified ().toMSecsSinceEpoch ();
        if (!error.isEmpty ()) {
          std_warning << "Compound namespace refresh: " << from_qstring_utf8 (error) << LF; return;
        }
        bool same= members.size () == self->members_.size ();
        for (std::size_t i= 0; same && i < members.size (); ++i)
          same= members[i].filename == self->members_[i].filename;
        if (same) return;
        const auto anchor= self->layout_.member_at (self->offset ());
        QString anchor_name= anchor < self->members_.size () ? self->members_[anchor].filename : QString ();
        if (anchor < self->views_.size ())
          if (const auto source= concrete_runtime_view (self->views_[anchor].runtime_id))
            anchor_name= to_qstring (as_string (view_to_buffer (abstract_view (source)), URL_SYSTEM));
        const double within= anchor < self->views_.size () ? self->offset () - self->layout_.top (anchor) : 0;
        const auto active= self->active_member_ < self->views_.size () ? self->views_[self->active_member_].runtime_id : ATHENA_NO_VIEW;
        self->owner_.activateMember (nullptr);
        self->clearSelection ();
        std::vector<SourceView> views (members.size ());
        athena::avd::layout_index layout (members.size (), 1200);
        double desired= 0;
        self->active_member_= std::numeric_limits<std::size_t>::max ();
        for (std::size_t i= 0; i < members.size (); ++i) {
          for (std::size_t old= 0; old < self->members_.size (); ++old) {
            QString actual= self->members_[old].filename;
            if (const auto source= concrete_runtime_view (self->views_[old].runtime_id))
              actual= to_qstring (as_string (view_to_buffer (abstract_view (source)), URL_SYSTEM));
            if (actual != members[i].filename) continue;
            views[i]= self->views_[old];
            if (const auto source= concrete_runtime_view (views[i].runtime_id)) views[i].view= abstract_view (source);
            self->views_[old]= SourceView {};
            layout.set_height (i, self->layout_.height (old));
            if (active != ATHENA_NO_VIEW && views[i].runtime_id == active) self->active_member_= i;
            break;
          }
          if (members[i].filename == anchor_name) desired= layout.top (i) + within;
        }
        for (auto& removed: self->views_) self->releaseSource (removed);
        self->members_= std::move (members);
        self->views_= std::move (views);
        self->layout_= std::move (layout);
        self->counters_= athena::avd::counter_cache (self->members_, QByteArrayLiteral ("ATHENA counter executor 2"));
        self->invalidateCounters (0);
        self->updateRange (desired);
        self->arrange ();
        if (self->active_member_ < self->views_.size ()) self->activateMember (self->active_member_);
      }, Qt::QueuedConnection);
    });
  }

protected:
  bool eventFilter (QObject* watched, QEvent* event) override {
    if (watched == verticalScrollBar () || watched == horizontalScrollBar ())
      return QAbstractScrollArea::eventFilter (watched, event);
    if ((watched == this || watched == viewport ()) && !forwarding_input_) {
      if (event->type () == QEvent::FocusIn || event->type () == QEvent::FocusOut) {
        if (active_member_ < views_.size () && views_[active_member_].canvas)
          views_[active_member_].canvas->setPresentationFocus (event->type () == QEvent::FocusIn);
      }
      const bool mouse= event->type () == QEvent::MouseButtonPress ||
        event->type () == QEvent::MouseButtonRelease || event->type () == QEvent::MouseButtonDblClick ||
        event->type () == QEvent::MouseMove;
      const bool wheel= event->type () == QEvent::Wheel;
      const bool keyboard= event->type () == QEvent::KeyPress || event->type () == QEvent::KeyRelease ||
        event->type () == QEvent::ShortcutOverride || event->type () == QEvent::InputMethod;
      std::size_t member= active_member_;
      if (mouse || wheel) {
        const QPoint global= mouse ? static_cast<QMouseEvent*> (event)->globalPosition ().toPoint () :
                                    static_cast<QWheelEvent*> (event)->globalPosition ().toPoint ();
        const QPoint local= viewport ()->mapFromGlobal (global);
        const bool continuing_drag= mouse_down_ &&
          (event->type () == QEvent::MouseMove || event->type () == QEvent::MouseButtonRelease);
        // Ignored scrollbar mouse events propagate to the scroll area, but
        // belong to its chrome, not to the source editor beneath that height.
        if (!viewport ()->rect ().contains (local) && !continuing_drag)
          return QAbstractScrollArea::eventFilter (watched, event);
        member= layout_.member_at (offset () + local.y ());
        if (mouse_down_ && drag_anchor_.member < views_.size ()) member= drag_anchor_.member;
      }
      if (member < views_.size () && views_[member].canvas && (mouse || wheel || keyboard)) {
        auto* canvas= views_[member].canvas.data ();
        if (event->type () == QEvent::MouseButtonPress) {
          activateMember (member);
          viewport ()->setFocus (Qt::MouseFocusReason);
        }
        forwarding_input_= true;
        if (mouse) {
          auto* input= static_cast<QMouseEvent*> (event);
          const QPointF local= canvas->surface ()->mapFromGlobal (input->globalPosition ().toPoint ());
          QMouseEvent forwarded (input->type (), local, input->globalPosition (),
            input->button (), input->buttons (), input->modifiers (), input->pointingDevice ());
          QCoreApplication::sendEvent (canvas->surface (), &forwarded);
        }
        else if (wheel) {
          auto* input= static_cast<QWheelEvent*> (event);
          QWheelEvent forwarded (canvas->surface ()->mapFromGlobal (input->globalPosition ().toPoint ()),
            input->globalPosition (), input->pixelDelta (), input->angleDelta (), input->buttons (),
            input->modifiers (), input->phase (), input->inverted (), input->source (), input->pointingDevice ());
          QCoreApplication::sendEvent (canvas->surface (), &forwarded);
        }
        else {
          if (event->type () == QEvent::KeyPress || event->type () == QEvent::InputMethod)
            performance_.recordEditingInput ();
          QCoreApplication::sendEvent (canvas, event);
        }
        forwarding_input_= false;
        viewport ()->setCursor (canvas->surface ()->cursor ());
        event->accept ();
        return true;
      }
      if (event->type () == QEvent::InputMethodQuery && member < views_.size () && views_[member].canvas) {
        auto* query= static_cast<QInputMethodQueryEvent*> (event);
        auto* canvas= views_[member].canvas.data ();
        for (unsigned flag= 1; flag <= unsigned (Qt::ImInputItemClipRectangle); flag <<= 1) {
          auto type= Qt::InputMethodQuery (flag);
          if (!(query->queries () & type)) continue;
          QVariant value= canvas->editorInputMethodQuery (type);
          if (type == Qt::ImCursorRectangle || type == Qt::ImAnchorRectangle || type == Qt::ImInputItemClipRectangle)
            value= value.toRectF ().translated (canvas->mapTo (viewport (), QPoint ()));
          query->setValue (type, value);
        }
        return true;
      }
    }
    if ((edit_pending_ || io_pending_ || selection_pending_ || queued_selections_) &&
        (event->type () == QEvent::KeyPress || event->type () == QEvent::InputMethod)) {
      deferred_inputs_.emplace_back (event->clone ());
      event->accept (); return true;
    }
    if ((edit_pending_ || io_pending_) && (event->type () == QEvent::KeyPress ||
        event->type () == QEvent::MouseButtonPress || event->type () == QEvent::InputMethod)) {
      event->accept ();
      return true;
    }
    if (event->type () == QEvent::InputMethod && !selection_.empty ()) {
      const auto* input= static_cast<QInputMethodEvent*> (event);
      if (!input->commitString ().isEmpty ())
        queueInput ([text= input->commitString ()] (QTMCompoundViewport& self) { self.selectionCommand ("avd.input", text); });
      event->accept (); return true;
    }
    if (event->type () == QEvent::MouseButtonPress || event->type () == QEvent::MouseMove ||
        event->type () == QEvent::MouseButtonRelease) {
      auto* mouse= static_cast<QMouseEvent*> (event);
      if (event->type () == QEvent::MouseButtonPress && mouse->button () == Qt::LeftButton) {
        drag_timer_.stop ();
        compound_drag_= false;
        drag_dirty_= false;
        mouse_down_= false;
        for (std::size_t i= 0; i < views_.size (); ++i) {
          const auto& canvas= views_[i].canvas;
          if (canvas && (watched == canvas || watched == canvas->viewport () || watched == canvas->surface ())) {
            if (mouse->modifiers () == Qt::NoModifier) {
              drag_global_= mouse->globalPosition ().toPoint ();
              drag_anchor_= mouseEndpoint (i, drag_global_);
              mouse_down_= true;
            }
            break;
          }
        }
      }
      else if (mouse_down_ && (event->type () == QEvent::MouseMove ||
                              mouse->button () == Qt::LeftButton)) {
        drag_global_= mouse->globalPosition ().toPoint ();
        auto& origin= views_[drag_anchor_.member];
        if (origin.canvas && origin.canvas->ownsNativePointerGesture ()) {
          mouse_down_= false;
        }
        else {
          const QPoint local= viewport ()->mapFromGlobal (drag_global_);
          const auto target= layout_.member_at (offset () + std::clamp (local.y (), 0,
                                                       std::max (0, viewport ()->height () - 1)));
          const bool outside= local.y () < 0 || local.y () >= viewport ()->height ();
          if (!compound_drag_ && target < members_.size () &&
              (target != drag_anchor_.member || outside)) {
            // Finish the member's native gesture before taking over its mouse grab.
            const auto point= from_qpoint (drag_anchor_.position);
            the_gui->process_mouse (origin.canvas->tm_widget (), "release-left",
                                    point.x1, point.x2, 0, texmacs_time ());
            compound_drag_= true;
            viewport ()->grabMouse ();
            drag_timer_.start ();
          }
          if (compound_drag_) {
            drag_dirty_= true;
            if (event->type () == QEvent::MouseButtonRelease) {
              mouse_down_= false;
              viewport ()->releaseMouse ();
            }
            updateDrag ();
            event->accept ();
            return true;
          }
        }
        if (event->type () == QEvent::MouseButtonRelease) mouse_down_= false;
      }
    }
    if (event->type () == QEvent::ShortcutOverride || event->type () == QEvent::KeyPress) {
      auto* key= static_cast<QKeyEvent*> (event);
      if (key->modifiers () == Qt::ControlModifier && (key->key () == Qt::Key_S || key->key () == Qt::Key_W || key->key () == Qt::Key_P)) {
        if (event->type () == QEvent::KeyPress) {
          if (key->key () == Qt::Key_S) queueInput ([] (QTMCompoundViewport& self) { self.saveAll (); });
          else if (key->key () == Qt::Key_W) queueInput ([] (QTMCompoundViewport& self) { self.requestClose (); });
          else queueInput ([] (QTMCompoundViewport& self) { self.exportPdf (true); });
        }
        event->accept (); return true;
      }
      const bool select_boundary= key->modifiers () == (Qt::ControlModifier | Qt::ShiftModifier) &&
        (key->key () == Qt::Key_Home || key->key () == Qt::Key_End);
      if (select_boundary && active_member_ < members_.size ()) {
        if (event->type () == QEvent::KeyPress) {
          const bool end= key->key () == Qt::Key_End;
          const auto member= active_member_;
          ++queued_selections_;
          queueInput ([member, end] (QTMCompoundViewport& self) {
            --self.queued_selections_;
            self.selectRange (end ? member : 0,
                              end ? self.members_.size () - 1 : member, end, !end);
          });
        }
        event->accept ();
        return true;
      }
      const bool select_all= key->modifiers () == Qt::ControlModifier && key->key () == Qt::Key_A;
      const bool has_selection= !selection_.empty () || queued_selections_ != 0;
      QString selection_command;
      if (has_selection && key->modifiers () == Qt::ControlModifier) {
        if (key->key () == Qt::Key_C) selection_command= "editor.copy";
        if (key->key () == Qt::Key_X) selection_command= "editor.cut";
        if (key->key () == Qt::Key_V) selection_command= "editor.paste";
      }
      const bool text_input= has_selection && !key->text ().isEmpty () &&
        !(key->modifiers () & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
        key->key () != Qt::Key_Backspace && key->key () != Qt::Key_Delete && key->key () != Qt::Key_Escape;
      if (text_input) selection_command= "avd.input";
      if (!selection_command.isEmpty ()) {
        if (event->type () == QEvent::KeyPress)
          queueInput ([selection_command, text= key->text ()] (QTMCompoundViewport& self) {
            self.selectionCommand (selection_command, text);
          });
        event->accept (); return true;
      }
      const bool erase= has_selection && key->modifiers () == Qt::NoModifier &&
        (key->key () == Qt::Key_Delete || key->key () == Qt::Key_Backspace);
      const bool cancel= has_selection && key->key () == Qt::Key_Escape;
      if (select_all || erase || cancel) {
        if (event->type () == QEvent::KeyPress) {
          if (select_all) {
            ++queued_selections_;
            queueInput ([] (QTMCompoundViewport& self) {
              --self.queued_selections_;
              self.selectAll ();
            });
          }
          else if (erase) queueInput ([] (QTMCompoundViewport& self) { self.eraseSelection (); });
          else queueInput ([] (QTMCompoundViewport& self) { self.clearSelection (); });
        }
        event->accept ();
        return true;
      }
      if (event->type () == QEvent::KeyPress && has_selection &&
          key->key () != Qt::Key_Control && key->key () != Qt::Key_Shift &&
          key->key () != Qt::Key_Alt && key->key () != Qt::Key_Meta)
        queueInput ([] (QTMCompoundViewport& self) { self.clearSelection (); });
      if (key->modifiers () == Qt::ControlModifier &&
          (key->key () == Qt::Key_Home || key->key () == Qt::Key_End)) {
        if (event->type () == QEvent::KeyPress && !members_.empty ()) {
          const bool at_end= key->key () == Qt::Key_End;
          queueInput ([at_end] (QTMCompoundViewport& self) {
            self.goToMember (at_end ? self.members_.size () - 1 : 0, at_end);
          });
        }
        event->accept ();
        return true;
      }
    }
    if (event->type () == QEvent::Wheel) {
      auto* wheel= static_cast<QWheelEvent*> (event);
      if (!(wheel->modifiers () & Qt::ControlModifier)) {
        scroll_canvas_= nullptr;
        for (const auto& source: views_)
          if (source.canvas && (watched == source.canvas || watched == source.canvas->viewport () ||
                                watched == source.canvas->surface ())) {
            auto* widget= source.canvas->tm_widget ();
            if (widget->handle_overlay_wheel_capture () || widget->handle_wheel_capture ()) {
              inertia_.stop ();
              return false;
            }
            scroll_canvas_= source.canvas;
            break;
          }
        if (!inertia_.wheel (wheel)) {
          const QPoint delta= wheel->pixelDelta ().isNull () ? wheel->angleDelta () : wheel->pixelDelta ();
          if (scroll_canvas_ && std::abs (delta.x ()) > std::abs (delta.y ()))
            QCoreApplication::sendEvent (horizontalScrollBar (), wheel);
          else QAbstractScrollArea::wheelEvent (wheel);
        }
        event->accept ();
        return true;
      }
    }
    if (event->type () == QEvent::FocusIn || event->type () == QEvent::MouseButtonPress) {
      if (event->type () == QEvent::MouseButtonPress && !selection_.empty ()) clearSelection ();
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

  void scrollContentsBy (int, int) override { arrange (true); }
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
  void paintEvent (QPaintEvent* event) override {
    QPainter painter (viewport ());
    painter.fillRect (viewport ()->rect (), palette ().brush (QPalette::Base));
    if (members_.empty ()) {
      painter.setPen (palette ().color (QPalette::Text));
      painter.drawText (viewport ()->rect (), Qt::AlignCenter,
                        tr ("This namespace has no documents."));
      return;
    }
    const double start= offset ();
    for (std::size_t i= layout_.member_at (start); i < views_.size () && layout_.top (i) < start + viewport ()->height (); ++i) {
      const auto& source= views_[i];
      if (!source.visible || !source.canvas) continue;
      const int top= int (std::max (0.0, layout_.top (i) + member_heading_height - start));
      // The final source renderer also owns the trailing background, including
      // typewriter padding; it already applies the document color preferences.
      const int bottom= i + 1 == views_.size () ? viewport ()->height () :
        int (std::min (double (viewport ()->height ()), layout_.top (i + 1) - start));
      const QPoint position= source.canvas->surface ()->mapTo (viewport (), QPoint ());
      painter.save ();
      painter.setClipRect (QRect (0, top, viewport ()->width (), bottom - top));
      painter.translate (position);
      source.canvas->paintContent (painter, event->region ().translated (-position));
      painter.restore ();
    }
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
    performance_.finishPaint (event, painter);
  }

public:
  bool commandsEnabled (const QString& id) const {
    // These operations require a single persistent document, not an AVD view.
    if (id == "editor.export-pdf-embedded" || id == "editor.export-postscript" ||
        id == "editor.print-page-selection" || id == "editor.print-page-selection-to-file" ||
        id == "editor.revert") return false;
    return !edit_pending_ && !io_pending_;
  }
  QTMWidget* canvasAtGlobalPosition (const QPoint& position, bool activate) {
    const QPoint local= viewport ()->mapFromGlobal (position);
    if (!viewport ()->rect ().contains (local)) return nullptr;
    const auto member= layout_.member_at (offset () + local.y ());
    if (member >= views_.size () || !views_[member].canvas) return nullptr;
    if (offset () + local.y () < layout_.top (member) + member_heading_height) return nullptr;
    auto* canvas= views_[member].canvas.data ();
    if (!canvas->surface ()->rect ().contains (canvas->surface ()->mapFromGlobal (position)))
      return nullptr;
    if (activate) activateMember (member);
    return canvas;
  }
  bool ownsCommand (const QString& id) const {
    if (id == "editor.export-pdf-embedded" || id == "editor.export-postscript" ||
        id == "editor.print-page-selection" || id == "editor.print-page-selection-to-file" ||
        id == "editor.revert") return true;
    if (id == "editor.select-all" || id == "editor.save" || id == "editor.save-as" || id == "editor.close-document" || id == "editor.close-window" ||
        id == "editor.export-pdf" || id == "editor.print-to-file" || id == "editor.print" || id == "editor.preview") return true;
    return (!selection_.empty () || queued_selections_ != 0) &&
      (id == "editor.copy" || id == "editor.cut" || id == "editor.paste" || id == "editor.clear-selection");
  }
  void requestClose () {
    if (io_pending_ || edit_pending_) return;
    inspectModified ([this] (std::vector<std::size_t> dirty) {
      if (dirty.empty ()) { finishClose (); return; }
      QMessageBox question (QMessageBox::Question, tr ("Close Compound Document"),
        tr ("Save changes in %1 source documents? Unsaved sources can remain open as separate tabs.").arg (dirty.size ()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, &owner_);
      question.button (QMessageBox::Discard)->setText (tr ("Keep sources open"));
      const int answer= question.exec ();
      if (answer == QMessageBox::Save)
        saveSources (std::make_shared<std::vector<std::size_t>> (std::move (dirty)), 0, true);
      else if (answer == QMessageBox::Discard) {
        for (auto i: dirty) switch_to_buffer (source_url (members_[i].filename));
        finishClose ();
      }
    });
  }
  bool invokeCommand (const QString& id) {
    if (id == "editor.save-as") { saveAs (); return true; }
    if (id == "editor.clear-selection" && !selection_.empty ()) {
      queueInput ([] (QTMCompoundViewport& self) { self.selectionCommand ("editor.delete"); }); return true;
    }
    if (id == "editor.export-pdf" || id == "editor.print-to-file" || id == "editor.print" || id == "editor.preview") {
      exportPdf (id == "editor.print" || id == "editor.preview"); return true;
    }
    if (id == "editor.save") { saveAll (); return true; }
    if (id == "editor.close-document" || id == "editor.close-window") { requestClose (); return true; }
    if (id == "editor.select-all") { queueInput ([] (QTMCompoundViewport& self) { self.selectAll (); }); return true; }
    if (selection_.empty () && queued_selections_ == 0) return false;
    if (id != "editor.copy" && id != "editor.cut" && id != "editor.paste" && id != "editor.delete") return false;
    queueInput ([id] (QTMCompoundViewport& self) { self.selectionCommand (id); });
    return true;
  }
  QTMCompoundViewport (QTMCompoundDocument& owner, QString filename,
                       athena::avd::descriptor_file descriptor,
                       std::vector<athena::avd::member> members):
    QAbstractScrollArea (&owner), owner_ (owner), filename_ (std::move (filename)),
    descriptor_ (std::move (descriptor)), members_ (std::move (members)),
    layout_ (members_.size (), 1200),
    counters_ (members_, QByteArrayLiteral ("ATHENA counter executor 2"), descriptor_.value.checkpoints),
    views_ (members_.size ()) {
    setVerticalScrollBar (new MemberScrollBar (layout_));
    viewport ()->installEventFilter (this);
    installEventFilter (this);
    setFocusPolicy (Qt::StrongFocus);
    setAttribute (Qt::WA_InputMethodEnabled);
    viewport ()->setFocusPolicy (Qt::StrongFocus);
    viewport ()->setAttribute (Qt::WA_InputMethodEnabled);
    viewport ()->setMouseTracking (true);
    viewport ()->setAcceptDrops (true);
    setHorizontalScrollBarPolicy (Qt::ScrollBarAsNeeded);
    refresh_.setInterval (100);
    connect (&refresh_, &QTimer::timeout, this, [this] { refreshGeometry (); });
    refresh_.start ();
    membership_timer_.setInterval (1000);
    connect (&membership_timer_, &QTimer::timeout, this, [this] { refreshMembers (); });
    membership_timer_.start ();
    drag_timer_.setInterval (40);
    connect (&drag_timer_, &QTimer::timeout, this, [this] {
      updateDrag ();
      if (!mouse_down_ && !drag_dirty_ && !drag_queued_ && selection_pending_ == 0)
        drag_timer_.stop ();
    });
    save_cache_.setSingleShot (true);
    save_cache_.setInterval (2000);
    connect (&save_cache_, &QTimer::timeout, this, [this] { saveCounterCache (); });
  }

  ~QTMCompoundViewport () override {
    if (pdf_job_) pdf_job_->cancelled= true;
    clearSelection ();
    refresh_.stop ();
    save_cache_.stop ();
    saveCounterCache ();
    owner_.activateMember (nullptr);
    for (auto& item: views_) releaseSource (item);
  }

  const QString& filename () const { return filename_; }
  void goToMember (std::size_t i, bool at_end) {
    if (i >= members_.size ()) throw std::out_of_range ("Compound member index");
    updateRange (at_end ? layout_.top (i + 1) - viewport ()->height () : layout_.top (i));
    arrange ();
    mount (i);
    activateMember (i);
    views_[i].canvas->focusEditor (Qt::OtherFocusReason);
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
    auto* bar= new QToolBar (definitions[i], this);
    bar->setObjectName (names[i]);
    bar->setIconSize (QSize (32, 32));
    bar->setFixedHeight (ATHENA_PLATFORM_IPADOS ? 48 : 32);
    bar->setContentsMargins (0, 0, 0, 0);
    bar->layout ()->setContentsMargins (0, 0, 0, 0);
    bar->layout ()->setSpacing (0);
#if ATHENA_PLATFORM_IPADOS
    bar->setProperty ("athenaTouchUi", true);
#endif
    bar->setToolButtonStyle (get_preference ("text toolbar", "off") == "on" ?
                            Qt::ToolButtonTextOnly : Qt::ToolButtonIconOnly);
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

QTMWidget* QTMCompoundDocument::canvasAtGlobalPosition (const QPoint& position, bool activate) {
  return viewport_->canvasAtGlobalPosition (position, activate);
}

void QTMCompoundDocument::refreshToolbars () {
  const bool enabled= editorCanvas () != nullptr && get_preference ("header") == "on";
  toolbar_controller_->setRequestedVisibility (
    enabled && get_preference ("main icon bar") == "on",
    enabled && get_preference ("mode dependent icons") == "on",
    enabled && get_preference ("focus dependent icons") == "on",
    enabled && get_preference ("user provided icons") == "on");
}

bool QTMCompoundDocument::invokeCommand (const QString& id) { return viewport_->invokeCommand (id); }
void QTMCompoundDocument::requestClose () { viewport_->requestClose (); }
bool QTMCompoundDocument::ownsCommand (const QString& id) const { return viewport_->ownsCommand (id); }
bool QTMCompoundDocument::commandsEnabled (const QString& id) const { return viewport_->commandsEnabled (id); }

void compound_document_open (url filename) {
  const string native= as_string (filename, URL_SYSTEM);
  const QString path= QString::fromUtf8 (native.c_str (), N (native));
  QMetaObject::invokeMethod (qApp, [path] {
    for (auto* widget: QApplication::allWidgets ())
      if (auto* pane= dynamic_cast<QTMCompoundDocument*> (widget))
        if (QFileInfo (pane->filename ()).canonicalFilePath () == QFileInfo (path).canonicalFilePath ()) {
          QTMMainTabWindow::topTabWindow ()->showWidget (pane, true);
          (void) call ("buffer-notify-recent", object (source_url (path)));
          return;
        }
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
        for (auto* widget: QApplication::allWidgets ())
          if (auto* pane= dynamic_cast<QTMCompoundDocument*> (widget))
            if (QFileInfo (pane->filename ()).canonicalFilePath () == QFileInfo (path).canonicalFilePath ()) {
              QTMMainTabWindow::topTabWindow ()->showWidget (pane, true);
              (void) call ("buffer-notify-recent", object (source_url (path)));
              return;
            }
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
        QObject::connect (pane, &QTMWindow::closed, pane, [pane] { pane->requestClose (); });
        shell->showWidget (pane, true);
        (void) call ("buffer-notify-recent", object (source_url (path)));
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
