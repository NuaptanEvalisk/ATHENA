// Copyright (C) 2001-2026 ATHENA contributors. GPL-3.0-or-later.
#include "insertion_commands.hpp"
#include "generic_frontend.hpp"
#include "convert.hpp"
#include "tooltip_commands.hpp"
#include "generic_editor_commands.hpp"
#include "document_style_commands.hpp"
#include "document_commands.hpp"
#include "presentation_commands.hpp"
#include "materials_edit.hpp"
#include "application_tools.hpp"
#include "editor.hpp"
#include "native_interfaces.hpp"
#include "scheme_execution_context.hpp"
#include "ATHENA/Data/compound_edit_batch.hpp"
#include "ATHENA/Data/application_lifecycle.hpp"
#include "buffer_name_catalog.hpp"
#include "boot.hpp"
#include "file.hpp"
#include "new_buffer.hpp"
#include "analyze.hpp"
#include "gui.hpp"
#include "server.hpp"
#include "picture.hpp"
#include "QTMGlobalSearch.hpp"
#include "QTMParagraphPropertiesPane.hpp"
#include "QTMPagePropertiesPane.hpp"
#include "QTMSlidePropertiesPane.hpp"
#include "QTMNativeDialogs.hpp"
#include "qt_utilities.hpp"
#include "vault.hpp"
#include <QApplication>
#include <QFileDialog>
#include <QInputDialog>
#include <QDir>
#include <QTimer>
#include <memory>
#include <algorithm>

namespace {
string text (const std::string& s) {
  return string (s.data (), s.size ());
}
void report (std::string error) {
  if (!error.empty ())
    std_warning << "Native editor command: " << text (error) << LF;
}
void chosen (editor_rep& ed, const std::string& kind, const std::string& file,
             int columns) {
  url target= url_system (text (file));
  if (kind == "include")
    generic_make_include (target);
  else if (kind == "style")
    generic_install_style (target);
  else if (kind == "thumbnails") {
    QDir directory (QString::fromUtf8 (file.data (), file.size ()));
    auto files= directory.entryList (
        {"*.gif", "*.jpg", "*.jpeg", "*.JPG", "*.JPEG", "*.png", "*.PNG"},
        QDir::Files | QDir::Readable, QDir::Name);
    array<url> values;
    for (auto name : files) {
      auto bytes= directory.absoluteFilePath (name).toUtf8 ();
      values << url_system (string (bytes.data (), bytes.size ()));
    }
    generic_make_thumbnails_sub (values, columns);
  } else
    ed.make_image (
        as_string (delta (get_master_buffer (ed.get_name ()), target), URL_UNIX),
        kind == "link-image", "", "", "", "");
}
} // namespace
void generic_insert_dialog (string kind) {
  auto* context= current_scheme_execution_context ();
  const auto actor= context->actor_id, view= context->view_id;
  const std::string operation (kind.data (), N (kind));
  qt_post_to_main_thread ([actor, view, operation] {
    int columns= 4;
    if (operation == "thumbnails") {
      bool accepted= false;
      columns=
          QInputDialog::getInt (QApplication::activeWindow (), "Thumbnails",
                                "Number of pictures per row", 4, 1, 32, 1, &accepted);
      if (!accepted)
        return;
    }
    QString selected=
        operation == "thumbnails"
            ? QFileDialog::getExistingDirectory (QApplication::activeWindow (),
                                                 "Picture directory")
            : QFileDialog::getOpenFileName (
                  QApplication::activeWindow (),
                  operation == "include" ? "Include file"
                  : operation == "style" ? "Install custom style"
                                         : "Load image",
                  {}, operation == "style" ? "ATHENA styles (*.ats *.ts)" : QString ());
    if (selected.isEmpty ())
      return;
    auto file= selected.toUtf8 ().toStdString ();
    athena::avd::submit_source_task (
        actor, view,
        [operation, file, columns] (editor_rep& ed) {
          chosen (ed, operation, file, columns);
        },
        report);
  });
}
void generic_insert_include_dialog () {
  generic_insert_dialog ("include");
}
void generic_insert_link_image_dialog () {
  generic_insert_dialog ("link-image");
}
void generic_insert_inline_image_dialog () {
  generic_insert_dialog ("inline-image");
}
void generic_insert_thumbnails_dialog () {
  generic_insert_dialog ("thumbnails");
}
void generic_insert_figure (string kind) {
  auto ed= get_current_editor ();
  bool selected= kind == "floating-algorithm" ? ed->selection_active_any ()
                                              : ed->selection_active_small ();
  if (selected)
    ed->selection_cut ("wrapbuf");
  else
    ed->selection_cancel ();
  bool floating= starts (kind, "floating-");
  if (floating)
    generic_make_insertion ("float");
  if (kind == "small-figure")
    ed->make_compound (as_tree_label ("small-figure"));
  else if (kind == "floating-algorithm")
    ed->make_compound (as_tree_label ("algorithm"));
  else {
    ed->insert_tree (compound (kind == "floating-table" ? "big-table" : "big-figure",
                               "", tree (DOCUMENT, "")),
                     path (0, 0));
    if (kind == "floating-table")
      ed->make_compound (as_tree_label ("tabular"));
  }
  if (selected)
    ed->selection_paste ("wrapbuf");
}
void generic_make_animation (tree_label label) {
  auto ed= get_current_editor ();
  bool selected= ed->selection_active_any ();
  tree content= selected ? ed->selection_get () : tree ("");
  if (selected) {
    if (!ed->selection_active_small ())
      content= compound ("par-block", content);
    ed->selection_cut ("none");
  }
  tree wrapper (label, content, "1s");
  ed->insert_tree (wrapper, ed->command_tree_end (wrapper, path (0)));
}
namespace {
struct document_update : std::enable_shared_from_this<document_update> {
  athena_actor_id actor;
  athena_view_id view;
  std::string what;
  int remaining;
  void schedule () {
    auto self= shared_from_this ();
    qt_post_to_main_thread ([self] {
      QTimer::singleShot (1, [self] {
        if (self->what == "all") {
          get_server ()->inclusions_gc ();
          tools_picture_gc ();
        }
        athena::avd::submit_source_task (
            self->actor, self->view,
            [self] (editor_rep& ed) {
              struct cursor_keeper {
                editor_rep& ed;
                observer point;
                ~cursor_keeper () {
                  ed.position_delete (point);
                }
              } saved{ed, ed.position_new (ed.the_path ())};
              if (self->what == "all" || self->what == "materials")
                materials_update_current_document ();
              if (self->what == "all") {
                ed.generate_aux ();
                ed.typeset_invalidate_all ();
              } else if (self->what == "buffer")
                ed.typeset_invalidate_all ();
              else if (self->what != "materials")
                ed.generate_aux (text (self->what));
              ed.go_to (ed.position_get (saved.point));
            },
            [self] (std::string error) {
              if (!error.empty ()) {
                report (error);
                return;
              }
              if (--self->remaining > 0)
                self->schedule ();
            });
      });
    });
  }
};
} // namespace
void generic_update_document (string what) {
  const auto* owner= current_scheme_execution_context ();
  auto request= std::make_shared<document_update> ();
  request->actor= owner->actor_id;
  request->view= owner->view_id;
  request->what.assign (what.data (), N (what));
  request->remaining=
      std::clamp (as_int (get_preference ("document update times", "1")), 1, 5);
  request->schedule ();
}
void generic_open_global_search () {
  if (!vault_active ()) {
    qtm_info_dialog ("No active vault. Please load a vault first.", "Global search");
    return;
  }
  athena_dispatch_ui (global_search_show);
}
void generic_search_open_occurrence (url source, path start, path finish) {
  std::vector<int> a, b;
  for (path p= start; !is_nil (p); p= p->next)
    a.push_back (p->item);
  for (path p= finish; !is_nil (p); p= p->next)
    b.push_back (p->item);
  application_browse_document (source, [a, b] (url loaded) {
    string name= as_string (loaded);
    auto owner= published_buffer_source (std::string (name.data (), N (name)));
    athena::avd::submit_source_task (
        owner.first, owner.second,
        [a, b] (editor_rep& ed) {
          tree body= ed.the_buffer ();
          path p= reverse (obtain_ip (body)), q= p;
          for (int i : a)
            p= p * i;
          for (int i : b)
            q= q * i;
          ed.go_to (p);
          if (p != q) {
            ed.selection_set_start ();
            ed.go_to (q);
            ed.selection_set_end ();
            ed.show_selection ();
          }
        },
        report);
  });
}
void generic_search_open_result (url source, path p) {
  generic_search_open_occurrence (source, p, p);
}
void generic_open_paragraph_format () {
  athena_dispatch_ui (paragraph_properties_pane_show);
}
void generic_open_page_format () {
  auto ed= get_current_editor ();
  if (!is_nil (ed->search_upwards ("screens")) ||
      !is_nil (ed->search_upwards ("slideshow")))
    athena_dispatch_ui (slide_properties_pane_show);
  else
    athena_dispatch_ui (page_properties_pane_show);
}
void generic_install_style (url file) {
  auto ed= get_current_editor ();
  if (!document_install_custom_style (file)) {
    ed->set_message ("Could not install the selected style.", "Install custom style");
    return;
  }
  string name= document_custom_style_file_name (file);
  document_set_main_style (name);
  ed->set_message ("Installed and activated style: " * name, "Install custom style");
}
void generic_install_style_dialog () {
  generic_insert_dialog ("style");
}
void generic_make_with_package (tree_label label, string name) {
  if (!document_has_style_package (name) &&
      !get_current_editor ()->defined_at_init (name * "-package"))
    document_add_style_package (name);
  auto ed= get_current_editor ();
  if (ed->selection_active_any ()) insertion_make (label);
  else ed->make_compound (label);
}
array<string> generic_preferred_font_names () {
  string value= get_preference ("preferred fonts");
  array<string> result;
  if (value == "") return result;
  tree parsed= string_to_scheme_tree (value);
  if (!is_tuple (parsed)) return result;
  for (int i=0; i<N(parsed); ++i) {
    if (!is_atomic (parsed[i]) || !is_quoted (parsed[i]->label))
      return array<string> ();
    result << raw_unquote (parsed[i]->label);
  }
  return result;
}
void generic_set_preferred_fonts (array<string> fonts) {
  tree values (TUPLE);
  for (int i=0; i<N(fonts); ++i) values << tree (raw_quote (fonts[i]));
  set_preference ("preferred fonts", scheme_tree_to_string (values));
}
tree generic_render_cardlink (tree body, tree destination) {
  string type= generic_cardlink_native_type (destination);
  return generic_cardlink_native_render (
      body,
      system_icon_for_link (is_atomic (destination) ? destination->label : string (""),
                            type),
      type);
}
void generic_display_balloon (tree body, tree balloon, tree horizontal, tree vertical,
                              tree type) {
  string kind= is_atomic (type) ? type->label : string ("default"),
         ha= is_atomic (horizontal)
                 ? horizontal->label
                 : (kind == "mouse" ? string ("right") : string ("left")),
         va= is_atomic (vertical) ? vertical->label : string ("Bottom");
  tree address (TUPLE);
  for (path p= reverse (obtain_ip (body)); !is_nil (p); p= p->next)
    address << as_string (p->item);
  tree identity (TUPLE, address, tree_to_scheme_tree (body));
  tooltip_show (scheme_tree_to_string (identity), body, balloon, ha, va, kind, 0.833333);
}
void generic_clear_style_cache () {
  get_server ()->style_clear_cache ();
  generic_focus_parameters_cache_clear ();
}
