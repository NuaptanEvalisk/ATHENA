// Actor-owned animation frames, presented in an ADS pane. GPL-3.0-or-later.
#include "UI/Qt/Document/QTMSlideMotionProperties.hpp"
#include "UI/Qt/Document/QTMDocumentIdentity.hpp"
#include "UI/Qt/Shell/QTMMainTabWindow.hpp"
#include "UI/Qt/Toolkit/qt_utilities.hpp"
#include "UI/Qt/Knowledge/QTMNodePropertiesDialog.hpp"
#include "Editor/Commands/presentation_commands.hpp"
#include "Editor/editor.hpp"
#include "App/Commands/native_interfaces.hpp"
#include "App/Runtime/buffer_actor.hpp"
#include "App/Runtime/buffer_state.hpp"
#include "Document/Model/document_node_model.hpp"
#include <DockWidget.h>
#include <QApplication>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {
namespace motion= athena::slides::motion;
namespace node= athena::node;
struct snapshot {
  athena_actor_id actor= ATHENA_NO_ACTOR;
  athena_view_id view= ATHENA_NO_VIEW;
  std::string page_id, header;
  std::vector<motion::event_frame> frames;
  bool writable= false;
  int index= 0;
};

class AnimationsPane final: public QWidget {
  QTableWidget* table;
  QLabel* status;
  QWidget* toolbar;
  snapshot current;
  bool pending= false, populating= false;
public:
  AnimationsPane () {
    auto* layout= new QVBoxLayout (this);
    toolbar= new QWidget (this);
    auto* controls= new QHBoxLayout (toolbar);
    controls->setContentsMargins (0, 0, 0, 0);
    const auto button= [&] (const char* icon, const char* text, auto action) {
      auto* b= new QToolButton (toolbar);
      b->setIcon (QIcon::fromTheme (icon)); b->setToolTip (tr (text));
      controls->addWidget (b);
      connect (b, &QToolButton::clicked, this, action);
    };
    button ("list-add", "New event frame", [this] {
      auto frames= current.frames;
      frames.push_back ({node::new_id (), "Frame " + std::to_string (frames.size ()+1), "manual"});
      change (std::move (frames));
    });
    button ("list-remove", "Delete event frame", [this] {
      const int row= table->currentRow ();
      if (row < 0) return;
      auto frames= current.frames;
      frames.erase (frames.begin ()+row);
      change (std::move (frames));
    });
    for (auto [icon, delta]: {std::pair {"go-up", -1}, {"go-down", 1}})
      button (icon, delta < 0 ? "Move frame up" : "Move frame down", [this, delta] {
        int row= table->currentRow (), other= row+delta;
        if (row < 0 || other < 0 || other >= int (current.frames.size ())) return;
        auto frames= current.frames;
        std::swap (frames[row], frames[other]);
        change (std::move (frames));
      });
    controls->addStretch ();
    layout->addWidget (toolbar);
    table= new QTableWidget (this);
    table->setColumnCount (2);
    table->setHorizontalHeaderLabels ({tr ("Event frame"), tr ("Trigger")});
    table->horizontalHeader ()->setSectionResizeMode (0, QHeaderView::Stretch);
    table->horizontalHeader ()->setSectionResizeMode (1, QHeaderView::ResizeToContents);
    table->setSelectionBehavior (QAbstractItemView::SelectRows);
    table->setSelectionMode (QAbstractItemView::SingleSelection);
    layout->addWidget (table, 1);
    status= new QLabel (this);
    status->setWordWrap (true);
    layout->addWidget (status);
    connect (table, &QTableWidget::itemChanged, this, [this] (QTableWidgetItem* item) {
      if (populating || pending) return;
      auto frames= current.frames;
      frames[item->row ()].name= item->text ().toStdString ();
      change (std::move (frames));
    });
    auto* timer= new QTimer (this);
    connect (timer, &QTimer::timeout, this, [this] { refresh (); });
    timer->start (400);
    refresh ();
  }
private:
  void showSnapshot (snapshot next, const QString& error= {}) {
    pending= false;
    const bool different= next.header != current.header || next.actor != current.actor || next.view != current.view;
    current= std::move (next);
    toolbar->setEnabled (current.writable);
    table->setEnabled (current.writable);
    status->setText (!error.isEmpty () ? error : current.page_id.empty () ? tr ("No slide page selected") : tr ("Slide %1").arg (current.index+1));
    if (!different) return;
    populating= true;
    const int selected= table->currentRow ();
    table->setRowCount (int (current.frames.size ()));
    for (int i=0; i<int (current.frames.size ()); ++i) {
      table->setItem (i, 0, new QTableWidgetItem (QString::fromStdString (current.frames[i].name)));
      auto* combo= new QComboBox (table);
      combo->addItem (tr ("Manual next"), "manual");
      combo->addItem (tr ("After previous"), "after");
      combo->addItem (tr ("With previous"), "with");
      combo->setCurrentIndex (combo->findData (QString::fromStdString (current.frames[i].trigger)));
      connect (combo, &QComboBox::currentIndexChanged, this, [this, i, combo] {
        if (populating || pending) return;
        auto frames= current.frames;
        frames[i].trigger= combo->currentData ().toString ().toStdString ();
        change (std::move (frames));
      });
      table->setCellWidget (i, 1, combo);
    }
    if (selected >= 0 && table->rowCount ()) table->selectRow (std::min (selected, table->rowCount ()-1));
    populating= false;
  }
  void refresh () {
    if (!isVisible () || pending ||
        (qobject_cast<QLineEdit*> (QApplication::focusWidget ()) && table->isAncestorOf (QApplication::focusWidget ()))) return;
    const auto identity= qtm_last_active_document_identity (QTMMainTabWindow::topTabWindow ());
    if (!identity.has_view ()) { showSnapshot ({}); return; }
    request (identity.actor, identity.view, {});
  }
  void change (std::vector<motion::event_frame> frames) {
    if (pending || !current.writable || current.page_id.empty ()) return;
    auto expected= current;
    request (current.actor, current.view,
      [expected, frames= std::move (frames)] (editor_rep* ed) {
        auto pages= athena::slides::source_pages (ed->the_buffer ()).pages;
        auto found= std::find_if (pages.begin (), pages.end (), [&] (const tree& t) { return node::id (t) == expected.page_id; });
        if (found == pages.end ()) throw std::runtime_error ("Slide page was deleted");
        auto old= decode_node_property_snapshot (expected.header).header;
        if (athena::document_node::property_header (*found) != old)
          throw std::runtime_error ("Slide properties changed; retry the edit");
        tree replacement= copy (*found);
        for (const auto& f: expected.frames)
          if (std::none_of (frames.begin (), frames.end (), [&] (const auto& v) { return v.id == f.id; }))
            motion::remove_frame (replacement, f.id);
        auto meta= *node::get (replacement);
        motion::write_frames (meta, frames);
        node::set (replacement, meta);
        motion::validate_page (replacement);
        ed->before_menu_action ();
        try { tree_set_diff (*found, replacement); }
        catch (...) { ed->cancel_menu_action (); throw; }
        ed->after_menu_action ();
      });
  }
  void request (athena_actor_id actor, athena_view_id view,
                 std::function<void (editor_rep*)> edit) {
    pending= true;
    if (edit) {
      toolbar->setEnabled (false);
      table->setEnabled (false);
    }
    QPointer<AnimationsPane> guard (this);
    auto continuation= actor_continuation_registry::instance ().store ([guard, actor, view, edit] {
      snapshot next;
      QString error;
      try {
        const auto* context= current_execution_context ();
        if (!context || !context->editor || !context->actor) throw std::runtime_error ("Editor closed");
        auto* ed= context->editor;
        const bool writable= !context->actor->current_state ()->read_only;
        if (edit) {
          if (!writable) throw std::runtime_error ("Document is read-only");
          edit (ed);
        }
        tree page (UNINIT);
        const path root= ed->the_buffer_path ();
        for (path p= path_up (ed->the_path ()); root <= p; p= path_up (p)) {
          tree candidate= ed->the_subtree (p);
          if (is_func (candidate, SLIDE_PAGE)) { page= candidate; break; }
          if (p == root) break;
        }
        if (page != tree (UNINIT)) {
          next.actor= actor; next.view= view;
          next.page_id= node::id (page);
          next.header= encode_node_property_snapshot (page, context->actor->current_state ()->storage_version);
          next.frames= motion::read_frames (page);
          next.writable= writable;
          next.index= presentation_page_index ();
        }
      }
      catch (const std::exception& failure) { error= QString::fromUtf8 (failure.what ()); }
      catch (const string& failure) { error= to_qstring (failure); }
      qt_post_to_main_thread ([guard, next= std::move (next), error] {
        if (guard) guard->showSnapshot (next, error);
      });
    });
    if (!buffer_actor::submit_to (actor, actor_command_kind::run_native_continuation,
          view, ATHENA_NO_BLOB, ATHENA_NO_BLOB, EXECUTION_CAPABILITY_BUFFER, continuation)) {
      actor_continuation_registry::instance ().discard (continuation);
      showSnapshot ({}, tr ("Editor closed"));
    }
  }
};
QPointer<AnimationsPane> pane;
QPointer<ads::CDockWidget> dock;
}

void animations_pane_show () {
  if (qt_defer_to_main_thread (animations_pane_show)) return;
  auto* shell= QTMMainTabWindow::topTabWindow ();
  if (!shell || !shell->dockManager ()) return;
  if (!dock) {
    dock= new ads::CDockWidget ("Animations");
    dock->setObjectName ("athena-animations-pane");
    dock->setIcon (slide_motion_icon ("cmd/lc_animationeffects.svg"));
    pane= new AnimationsPane;
    dock->setWidget (pane);
    dock->setFeature (ads::CDockWidget::DockWidgetDeleteOnClose, false);
    dock->resize (380, 600);
  }
  shell->showAdsDockWidget (dock, ads::RightDockWidgetArea);
}
