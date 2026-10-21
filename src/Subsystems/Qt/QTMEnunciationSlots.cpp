/******************************************************************************
* MODULE     : QTMEnunciationSlots.cpp
* DESCRIPTION: Inline native rich-text editors backed by structured properties
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* GNU general public license version 3 or later; see LICENSE.
******************************************************************************/
#include "QTMEnunciationSlots.hpp"
#include "QTMWidget.hpp"
#include "qt_actor_widget.hpp"
#include "qt_utilities.hpp"
#include "ATHENA/Data/enunciation_slots.hpp"
#include "ATHENA/Data/enunciation_model.hpp"
#include "ATHENA/Data/document_node_model.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include "buffer_actor.hpp"
#include "buffer_state.hpp"
#include "scheme_execution_context.hpp"
#include "editor.hpp"
#include "tm_window.hpp"
#include "new_view.hpp"
#include "convert.hpp"
#include <QApplication>
#include <QLabel>
#include <QPointer>
#include <QScopeGuard>
#include <QTimer>
#include <QVBoxLayout>
#include <nlohmann/json.hpp>
#include <mutex>
#include <unordered_map>

namespace {
namespace en= athena::enunciation;
namespace node= athena::node;
namespace model= athena::document_node;
namespace xml= athena::document;
using json= nlohmann::json;
string native (const std::string& value) { return string (value.data (), value.size ()); }

tree inline_value (tree value) {
  while ((is_func (value, DOCUMENT, 1) || is_func (value, CONCAT, 1))) value= value[0];
  if (is_func (value, CONCAT, 0) || is_func (value, DOCUMENT, 0)) return tree ("");
  return node::content_projection (value);
}

std::pair<tree, tree> split_title (const tree& value, path cursor) {
  if (is_nil (cursor)) throw std::runtime_error ("No title cursor");
  const int index= cursor->item;
  if (is_atomic (value)) {
    if (!is_nil (cursor->next) || index < 0 || index > N(value->label))
      throw std::runtime_error ("Invalid title cursor");
    return {tree (value->label (0, index)), tree (value->label (index, N(value->label)))};
  }
  if (is_nil (cursor->next)) {
    if (index == 0) return {tree (""), value};
    if (index == right_index (value)) return {value, tree ("")};
    throw std::runtime_error ("Invalid title boundary");
  }
  if (index < 0 || index >= N(value)) throw std::runtime_error ("Invalid title cursor");
  const bool sequence= is_document (value) || is_concat (value);
  const bool wrapper= (is_func (value, WITH) && index == N(value)-1) ||
    (N(value) == 1 && (is_compound (value, "math") || is_compound (value, "text") ||
      is_compound (value, "strong") || is_compound (value, "em") ||
      is_compound (value, "emphasize") || is_compound (value, "verbatim") ||
      is_func (value, MATH_VARIANT)));
  if (!sequence && !wrapper)
    throw std::runtime_error ("Split between complete mathematical expressions");
  const auto parts= split_title (value[index], cursor->next);
  // Selection extraction intentionally drops its enclosing formatting. A
  // title split must retain those wrappers independently on both sides.
  if (wrapper) {
    tree left= copy (value), right= copy (value);
    left[index]= parts.first; right[index]= parts.second;
    return {is_empty (parts.first) ? tree ("") : left,
            is_empty (parts.second) ? tree ("") : right};
  }
  tree left (L(value)), right (L(value));
  for (int i=0; i<index; ++i) left << value[i];
  if (!is_empty (parts.first)) left << parts.first;
  if (!is_empty (parts.second)) right << parts.second;
  for (int i=index+1; i<N(value); ++i) right << value[i];
  return {left, right};
}

struct input_session {
  athena_actor_id owner;
  athena_view_id view;
  std::string node, field;
  std::vector<int> path;
  int index= 0, count= 1;
  // Only the input actor accesses these after registration.
  std::string previous;
  std::uint64_t epoch= 0;
  bool navigating= false;
  bool empty= false;
  std::function<void (bool)> show_placeholder;
};
std::mutex sessions_mutex;
std::unordered_map<athena_actor_id, std::shared_ptr<input_session>> sessions;

std::shared_ptr<input_session> input_for (editor_rep& editor) {
  const auto* context= current_scheme_execution_context ();
  if (!context || !context->actor) return {};
  const string name= as_string (context->actor->current_buffer_url ());
  if (!starts (name, "tmfs://aux/enunciation-slot-")) return {};
  std::lock_guard<std::mutex> lock (sessions_mutex);
  const auto found= sessions.find (context->actor_id);
  return found == sessions.end () ? nullptr : found->second;
}

void submit_value (const std::shared_ptr<input_session>& session, tree value,
                   const std::string& action= {}, tree right= tree ("")) {
  value= inline_value (value);
  const auto desired= xml::write_xml_v3 (value, xml::xml_kind::fragment);
  if (desired == session->previous && action.empty ()) return;
  const auto expected= session->previous;
  const auto extra= xml::write_xml_v3 (inline_value (right), xml::xml_kind::fragment);
  session->previous= desired;
  const auto actor= session->owner;
  const auto view= session->view;
  // Capture only detached bytes and scalar addressing across actor boundaries.
  const auto id= session->node, field= session->field;
  const auto where= session->path;
  const auto index= session->index;
  auto continuation= actor_continuation_registry::instance ().store (
    [expected, desired, extra, action, id, field, where, index] {
      const auto* context= current_scheme_execution_context ();
      if (!context || !context->editor || !context->actor) return;
      editor_rep& editor= *context->editor;
      try {
        if (context->actor->current_state ()->read_only)
          throw std::runtime_error ("Document is read-only");
        tree scope= editor.the_buffer ();
        path location;
        for (int step: where) location= location * step;
        if (!has_subtree (scope, location)) throw std::runtime_error ("Title node has moved");
        const tree& source= subtree (scope, location);
        if (node::id (source) != id || !en::editable_provable (source))
          throw std::runtime_error ("Title node has changed");
        auto values= en::title_fields (source, field);
        if (values.empty ()) values.emplace_back ("");
        if (index < 0 || index >= int (values.size ()) ||
            !node::content_equal (inline_value (values[index]),
              xml::read_xml_v3 (expected, xml::xml_kind::fragment)))
          throw std::runtime_error ("This title field was changed by another editor");
        values[index]= xml::read_xml_v3 (desired, xml::xml_kind::fragment);
        int next= index;
        if (action == "split") {
          values.insert (values.begin ()+index+1,
            xml::read_xml_v3 (extra, xml::xml_kind::fragment));
          next= index+1;
        }
        else if (action == "remove" && values.size () > 1) {
          values.erase (values.begin ()+index);
          next= std::max (0, index-1);
        }
        node::metadata metadata= *node::get (source);
        if (field == "year") {
          if (!is_atomic (values[0]))
            throw std::runtime_error ("Year must be plain text");
          const string year= values[0]->label;
          metadata.properties[field]= node::property (std::string (year.data (), N(year)));
        }
        else {
          node::property::list list;
          for (const auto& item: values) list.emplace_back (node::rich_text {item});
          metadata.properties[field]= node::property (std::move (list));
        }
        // Field input has no source IDs. Validate the title without auditing or
        // copying the unchanged enunciation body on each character.
        tree header (en::label (), tree (DOCUMENT, ""));
        node::set (header, metadata);
        const auto errors= model::validate_node_properties (header);
        if (!errors.empty ()) throw std::runtime_error (errors.front ().detail);
        if (!node::equal_metadata (source, header)) {
          editor.before_menu_action ();
          try { ::apply (scope, mod_set_metadata (location, header)); }
          catch (...) { editor.cancel_menu_action (); throw; }
          editor.after_menu_action ();
        }
        std::string next_field= field;
        if (action == "next" || action == "previous") {
          next+= action == "next" ? 1 : -1;
          if (next >= int (values.size ())) {
            next_field= field == "name" ? "year" : field == "year" ? "attribution" : "";
            next= 0;
          }
          else if (next < 0) {
            next_field= field == "attribution" ? "year" : field == "year" ? "name" : "";
            next= std::max (0, int (en::title_fields (source, next_field).size ())-1);
          }
        }
        if (action == "close") next_field.clear ();
        if (!action.empty () && (editor.the_buffer_path () * location) <= editor.the_path ())
          editor.open_enunciation_slot (native (next_field), next);
      }
      catch (const std::exception& error) {
        editor.set_message ("Title field", tree (string (error.what ())), true);
        editor.open_enunciation_slot ("", 0);
      }
    });
  if (!buffer_actor::submit_to (actor, actor_command_kind::run_native_continuation,
        view, ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER, continuation))
    actor_continuation_registry::instance ().discard (continuation);
}

class SlotInput: public QWidget {
  QPointer<QTMWidget> owner;
  widget input;
  athena_actor_id input_actor= ATHENA_NO_ACTOR;
  athena_view_id input_view= ATHENA_NO_VIEW;
  QRect document_rect;
  bool closing= false;
  QLabel* placeholder= nullptr;
  std::string node_id, field;
  int field_index= 0;
public:
  SlotInput (QTMWidget* canvas, athena_actor_id actor, athena_view_id view,
             const json& request): QWidget (canvas->presentationSurface ()), owner (canvas) {
    auto cleanup= qScopeGuard ([this] {
      { std::lock_guard<std::mutex> lock (sessions_mutex); sessions.erase (input_actor); }
      if (!is_nil (input)) send_destroy (input);
    });
    auto session= std::make_shared<input_session> ();
    session->owner= actor; session->view= view;
    session->node= request.at ("node"); session->field= request.at ("field");
    session->index= request.at ("index"); session->path= request.at ("path").get<std::vector<int>> ();
    node_id= session->node; field= session->field; field_index= session->index;
    const auto colors= en::title_slot_style (session->field);
    tree header= xml::read_xml_v3 (request.at ("header").get<std::string> (), xml::xml_kind::fragment);
    auto values= en::title_fields (header, session->field);
    if (values.empty ()) values.emplace_back ("");
    session->count= values.size ();
    tree value= inline_value (values.at (session->index));
    session->previous= xml::write_xml_v3 (value, xml::xml_kind::fragment);
    document_rect= QRect (request.at ("x"), request.at ("y"),
                          request.at ("width"), request.at ("height"));
    auto* layout= new QVBoxLayout (this);
    layout->setContentsMargins (1, 1, 1, 1);
    setAutoFillBackground (true);
    QPalette palette= this->palette ();
    palette.setColor (QPalette::Window, QColor (colors.foreground));
    setPalette (palette);
    tree doc (WITH, "font", native (request.at ("font").get<std::string> ()));
    doc << "font-base-size" << native (request.at ("font-size").get<std::string> ());
    doc << "bg-color" << colors.background << "color" << "#222222"
        << "page-screen-left" << "4px" << "page-screen-right" << "4px"
        << tree (DOCUMENT, value);
    tree style= xml::read_xml_v3 (request.at ("style").get<std::string> (), xml::xml_kind::fragment);
    url name ("tmfs://aux/enunciation-slot-" * native (node::new_id ()));
    if (tm_view source= concrete_runtime_view (view))
      set_current_view (abstract_view (source));
    input= texmacs_input_widget (doc, compound ("style", style), name, xml::xml_format_version::v3);
    QWidget* content= concrete (input)->as_qwidget (this);
    layout->addWidget (content);
    QTMWidget* native_input= qobject_cast<QTMWidget*> (content);
    if (!native_input) native_input= content->findChild<QTMWidget*> ();
    if (!native_input) throw std::runtime_error ("No native title editor");
    auto* proxy= dynamic_cast<qt_actor_widget_rep*> (native_input->tm_widget ());
    if (!proxy) throw std::runtime_error ("No title BufferActor");
    placeholder= new QLabel (colors.label, this);
    placeholder->setObjectName ("enunciation-slot-placeholder");
    placeholder->setAttribute (Qt::WA_TransparentForMouseEvents);
    placeholder->setAlignment (Qt::AlignLeft | Qt::AlignVCenter);
    auto hint_palette= placeholder->palette ();
    hint_palette.setColor (QPalette::WindowText, QColor (colors.foreground));
    placeholder->setPalette (hint_palette);
    auto hint_font= placeholder->font ();
    hint_font.setItalic (true);
    placeholder->setFont (hint_font);
    session->empty= value == "";
    placeholder->setVisible (session->empty);
    session->show_placeholder= [hint= QPointer<QLabel> (placeholder)] (bool empty) {
      qt_post_to_main_thread ([hint, empty] { if (hint) hint->setVisible (empty); });
    };
    input_actor= proxy->actor_id (); input_view= proxy->view_id ();
    {
      std::lock_guard<std::mutex> lock (sessions_mutex);
      sessions[input_actor]= session;
    }
    setFocusProxy (native_input);
    setObjectName ("enunciation-inline-slot");
    setAccessibleName (colors.label);
    auto* timer= new QTimer (this);
    connect (timer, &QTimer::timeout, this, [this] { place (); });
    timer->start (16);
    setEnabled (false);
    place (); show (); raise ();
    const double zoom= request.at ("zoom");
    QTimer::singleShot (0, this, [this, native_input, zoom] {
      // Bootstrap once after the overlay has its final geometry. Both layout
      // and painting remain asynchronous on the embedded editor's own actor.
      native_input->surface ()->setMinimumSize (native_input->viewport ()->size ());
      if (native_input->viewport ()->layout ()) native_input->viewport ()->layout ()->activate ();
      native_input->tm_widget ()->handle_notify_resize (
        native_input->viewport ()->width () * PIXEL,
        native_input->viewport ()->height () * PIXEL);
      auto start= actor_continuation_registry::instance ().store (
        [zoom, guard= QPointer<QTMWidget> (native_input), container= QPointer<SlotInput> (this)] {
          const auto* context= current_scheme_execution_context ();
          if (!context || !context->editor) return;
          auto* editor= context->editor;
          editor->handle_set_zoom_factor (zoom);
          editor->go_to_end (editor->the_buffer_path ());
          editor->notify_change (THE_ENVIRONMENT | THE_EXTENTS);
          context->actor->invoke (actor_command_kind::apply_changes, context->view_id);
          qt_post_to_main_thread ([guard, container] {
            if (!guard || !container || container->closing) return;
            guard->tm_widget ()->drain_external_effects ();
            container->setEnabled (true);
            guard->focusEditor ();
            guard->refreshEmbeddedBackingStore ();
          });
        });
      if (!buffer_actor::submit_to (input_actor, actor_command_kind::run_native_continuation,
            input_view, ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER, start))
        actor_continuation_registry::instance ().discard (start);
    });
    cleanup.dismiss ();
  }
  ~SlotInput () override {
    { std::lock_guard<std::mutex> lock (sessions_mutex); sessions.erase (input_actor); }
    if (!is_nil (input)) send_destroy (input);
  }
  void update_bounds (const json& request) {
    if (closing || request.at ("node") != node_id || request.at ("field") != field ||
        request.at ("index") != field_index) return;
    document_rect= QRect (request.at ("x"), request.at ("y"),
                          request.at ("width"), request.at ("height"));
    place ();
  }
  void finish (std::function<void ()> after= {}) {
    if (closing && !after) return;
    closing= true;
    auto flush= actor_continuation_registry::instance ().store ([] {
      const auto* context= current_scheme_execution_context ();
      if (context && context->editor) en::title_input_changed (*context->editor);
    });
    if (!buffer_actor::submit_to (input_actor, actor_command_kind::run_native_continuation,
          input_view, ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER, flush))
      actor_continuation_registry::instance ().discard (flush);
    hide ();
    // Destruction follows the queued native input commands and final flush.
    auto dispose= actor_continuation_registry::instance ().store ([guard= QPointer<SlotInput> (this), after] {
      qt_post_to_main_thread ([guard, after] {
        if (guard) {
          if (after) QObject::connect (guard, &QObject::destroyed, qApp, [after] { after (); });
          guard->deleteLater ();
        }
        else if (after) after ();
      });
    });
    if (!buffer_actor::submit_to (input_actor, actor_command_kind::run_native_continuation,
          input_view, ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER, dispose)) {
      actor_continuation_registry::instance ().discard (dispose);
      if (after) QObject::connect (this, &QObject::destroyed, qApp, [after] { after (); });
      deleteLater ();
    }
  }
private:
  void place () {
    if (!owner || !owner->presentationVisible ()) { finish (); return; }
    QRect geometry= document_rect;
    geometry.moveTopLeft (owner->contentsToViewport (document_rect.topLeft ()));
    setGeometry (geometry);
    if (placeholder) placeholder->setGeometry (rect ().adjusted (6, 2, -3, -2));
  }
};
std::unordered_map<athena_view_id, QPointer<SlotInput>> editors;
}

void qtm_enunciation_slot (QTMWidget* canvas, athena_actor_id actor,
                           athena_view_id view, const std::string& request) {
  QPointer<SlotInput> previous= editors[view];
  if (request.empty () || !canvas || !canvas->presentationVisible ()) {
    editors.erase (view);
    if (previous) previous->finish ();
    return;
  }
  try {
    const auto parsed= json::parse (request);
    if (parsed.value ("geometry", false)) {
      if (previous) previous->update_bounds (parsed);
      return;
    }
    if (previous) {
      const std::string field= parsed.at ("field"), id= parsed.at ("node");
      const int index= parsed.at ("index");
      const uint64_t generation= parsed.at ("generation");
      // Keep the closing input registered until it is destroyed, so a second
      // switch also waits for its flush and focus cleanup before reopening.
      previous->finish ([actor, view, field, id, index, generation] {
        auto reopen= actor_continuation_registry::instance ().store ([field, id, index, generation] {
          const auto* context= current_scheme_execution_context ();
          if (!context || !context->editor) return;
          auto& editor= *context->editor;
          tree body= editor.the_buffer ();
          for (path p= path_up (editor.the_path ()); editor.the_buffer_path () <= p; p= path_up (p)) {
            const tree value= subtree (body, p / editor.the_buffer_path ());
            if (node::id (value) == id) {
              editor.open_enunciation_slot (native (field), index, generation);
              break;
            }
            if (p == editor.the_buffer_path ()) break;
          }
        });
        if (!buffer_actor::submit_to (actor, actor_command_kind::run_native_continuation,
              view, ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER, reopen))
          actor_continuation_registry::instance ().discard (reopen);
      });
    }
    else editors[view]= new SlotInput (canvas, actor, view, parsed);
  }
  catch (const std::exception& error) { qWarning ("Title editor: %s", error.what ()); }
}

namespace athena::enunciation {
void title_input_changed (editor_rep& editor) {
  auto session= input_for (editor);
  if (!session || session->navigating) return;
  const auto* context= current_scheme_execution_context ();
  const auto epoch= context->actor->source_epoch ();
  if (session->epoch == epoch) return;
  session->epoch= epoch;
  const bool empty= inline_value (editor.the_buffer ()) == "";
  if (empty != session->empty) {
    session->empty= empty;
    session->show_placeholder (empty);
  }
  try { submit_value (session, editor.the_buffer ()); }
  catch (const std::exception& error) {
    editor.set_message ("Title field", tree (string (error.what ())), true);
  }
}

bool title_input_key (editor_rep& editor, const string& key) {
  auto session= input_for (editor);
  if (!session) return false;
  if (session->navigating) return true;
  const bool split= key == "C-,";
  const bool remove= (key == "backspace" || key == "S-backspace") &&
    session->count > 1 && inline_value (editor.the_buffer ()) == "";
  const bool next= key == "tab", previous= key == "S-tab";
  const bool close= key == "return" && editor.get_env_string ("mode") != "math";
  if (!split && !remove && !next && !previous && !close) return false;
  if (split && session->field == "year") return true;
  try {
    tree left= editor.the_buffer (), right ("");
    if (split) {
      const auto parts= split_title (left, editor.the_path () / editor.the_buffer_path ());
      left= parts.first; right= parts.second;
    }
    // Flush the unsplit value first so the owner compares against the same
    // source version even if navigation overtakes its next paint tick.
    title_input_changed (editor);
    submit_value (session, left, split ? "split" : remove ? "remove" :
                  next ? "next" : previous ? "previous" : "close", right);
    session->navigating= true;
  }
  catch (const std::exception& error) {
    editor.set_message ("Title field", tree (string (error.what ())), true);
  }
  return true;
}
}
