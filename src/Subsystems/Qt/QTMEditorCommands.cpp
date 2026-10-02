/******************************************************************************
* MODULE     : QTMCommandRegistry.cpp
* DESCRIPTION: Native application command registry and work-context routing
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/


#include "QTMCommandRegistry.hpp"
#include "QTMCommandRegistryInternal.hpp"

#include "QTMDocumentSearchBar.hpp"
#include "QTMNativeDialogs.hpp"
#include "boot.hpp"
#include "document_persistence.hpp"
#include "new_buffer.hpp"
#include "sys_utils.hpp"

#include <QColor>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QSet>

using namespace qtm_command_registry_detail;

namespace qtm_command_registry_detail {
QVector<QTMCommandDynamicItem>
editor_color_items (const QTMCommandContext& context,
                    std::uint32_t allowedModes) {
  QVector<QTMCommandDynamicItem> out;
  qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
  if (proxy == nullptr) return out;
  actor_editor_command_snapshot snapshot= proxy->editor_command_state ();
  if (!snapshot.valid () || (snapshot.flags & allowedModes) == 0) return out;

  const bool enabled= !snapshot.read_only ();
  QSet<QString> seen;
  auto addColor= [&] (const QString& color) {
    QColor qcolor (color);
    if (!qcolor.isValid ()) return;
    QString canonical= qcolor.name ();
    if (seen.contains (canonical)) return;
    seen.insert (canonical);
    QTMCommandDynamicItem item=
      enabled_dynamic_item (canonical, canonical, canonical);
    item.icon= canonical;
    item.state.enabled= enabled;
    out.append (std::move (item));
  };
  static const char* standard[]= {
    "#000000", "#434343", "#666666", "#999999",
    "#b7b7b7", "#cccccc", "#d9d9d9", "#efefef",
    "#f3f3f3", "#ffffff", "#980000", "#ff0000",
    "#ff9900", "#ffff00", "#00ff00", "#00ffff",
    "#4a86e8", "#0000ff", "#9900ff", "#ff00ff",
    "#e6b8af", "#f4cccc", "#fce5cd", "#fff2cc",
    "#d9ead3", "#d0e0e3", "#c9daf8", "#cfe2f3",
    "#d9d2e9", "#ead1dc", "#85200c", "#a61c00",
    "#bf9000", "#38761d"
  };
  for (const char* color: standard) addColor (QString::fromLatin1 (color));
  try {
    for (const QString& color:
         scheme_string_vector (call ("color-picker-recent-colors")))
      addColor (color);
    for (const QString& color:
         scheme_string_vector (call ("color-picker-saved-colors")))
      addColor (color);
  }
  catch (...) {}
  QTMCommandDynamicItem other= enabled_dynamic_item (
    QStringLiteral ("__other__"), QObject::tr ("Other color..."));
  other.state.enabled= enabled;
  out.append (std::move (other));
  return out;
}

bool
execute_editor_color (const QString& key,
                      const QTMCommandContext& context) {
  qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
  if (proxy == nullptr || key.isEmpty ()) return false;
  QString color= key;
  if (key == QStringLiteral ("__other__")) {
    array<string> recent, saved;
    try {
      recent= scheme_string_array (call ("color-picker-recent-colors"));
      saved= scheme_string_array (call ("color-picker-saved-colors"));
    }
    catch (...) {}
    array<string> selected= qtm_color_dialog ("Choose color", recent, saved);
    if (N(selected) == 0) return true;
    color= to_qstring (selected[0]);
    if (N(selected) > 1) {
      try {
        (void) call (
          "color-picker-set-saved-colors",
          object (array_string_list (selected, 1)));
      }
      catch (...) {}
    }
  }
  QColor qcolor (color);
  if (!qcolor.isValid ()) return false;
  color= qcolor.name ();
  try {
    (void) call (
      "color-picker-remember-color", object (from_qstring (color)));
  }
  catch (...) {}
  QJsonObject action;
  action.insert ("op", "make-with");
  action.insert ("var", "color");
  action.insert ("value", color);
  return submit_inline_editor_action (
    context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
}

QTMCommandState
editor_provider_state (
  const QTMCommandContext& context, std::uint32_t allowedModes= 0,
  bool writable= false, bool requireSelection= false) {
  QTMCommandState state;
  qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
  if (proxy == nullptr) return state;
  actor_editor_command_snapshot snapshot= proxy->editor_command_state ();
  if (!snapshot.valid ()) return state;
  if (allowedModes != 0 && (snapshot.flags & allowedModes) == 0) return state;
  if (requireSelection &&
      !snapshot.selection_active () &&
      !snapshot.graphics_selection_active ())
    return state;
  state.available= true;
  state.enabled= !writable || !snapshot.read_only ();
  return state;
}

native_editor_command_id
editor_command_id (const QString& id) {
  if (id == "editor.undo") return native_editor_command_id::undo;
  if (id == "editor.redo") return native_editor_command_id::redo;
  if (id == "editor.copy") return native_editor_command_id::copy;
  if (id == "editor.cut") return native_editor_command_id::cut;
  if (id == "editor.paste") return native_editor_command_id::paste;
  if (id == "editor.node-properties")
    return native_editor_command_id::node_properties;
  if (id == "editor.save") return native_editor_command_id::save;
  if (id == "editor.revert") return native_editor_command_id::revert;
  if (id == "editor.update-all") return native_editor_command_id::update_all;
  if (id == "editor.close-document")
    return native_editor_command_id::close_document;
  if (id == "editor.save-as") return native_editor_command_id::save_as;
  if (id == "editor.preview") return native_editor_command_id::preview;
  if (id == "editor.print") return native_editor_command_id::print;
  if (id == "editor.close-window")
    return native_editor_command_id::close_window;
  if (id == "editor.history-back")
    return native_editor_command_id::history_back;
  if (id == "editor.history-forward")
    return native_editor_command_id::history_forward;
  if (id == "editor.presentation-first")
    return native_editor_command_id::presentation_first;
  if (id == "editor.presentation-previous-screen")
    return native_editor_command_id::presentation_previous_screen;
  if (id == "editor.presentation-previous")
    return native_editor_command_id::presentation_previous;
  if (id == "editor.presentation-next")
    return native_editor_command_id::presentation_next;
  if (id == "editor.presentation-next-screen")
    return native_editor_command_id::presentation_next_screen;
  if (id == "editor.presentation-last")
    return native_editor_command_id::presentation_last;
  if (id == "editor.print-to-file")
    return native_editor_command_id::print_to_file;
  if (id == "editor.print-page-selection")
    return native_editor_command_id::print_page_selection;
  if (id == "editor.print-page-selection-to-file")
    return native_editor_command_id::print_page_selection_to_file;
  if (id == "editor.export-pdf")
    return native_editor_command_id::export_pdf;
  if (id == "editor.export-postscript")
    return native_editor_command_id::export_postscript;
  if (id == "editor.focus.first-similar")
    return native_editor_command_id::focus_traverse_first;
  if (id == "editor.focus.previous-similar")
    return native_editor_command_id::focus_traverse_previous;
  if (id == "editor.focus.next-similar")
    return native_editor_command_id::focus_traverse_next;
  if (id == "editor.focus.last-similar")
    return native_editor_command_id::focus_traverse_last;
  if (id == "editor.focus.insert-left")
    return native_editor_command_id::focus_insert_left;
  if (id == "editor.focus.insert-right")
    return native_editor_command_id::focus_insert_right;
  if (id == "editor.focus.insert-up")
    return native_editor_command_id::focus_insert_up;
  if (id == "editor.focus.insert-down")
    return native_editor_command_id::focus_insert_down;
  if (id == "editor.focus.remove-left")
    return native_editor_command_id::focus_remove_left;
  if (id == "editor.focus.remove-right")
    return native_editor_command_id::focus_remove_right;
  if (id == "editor.focus.remove-up")
    return native_editor_command_id::focus_remove_up;
  if (id == "editor.focus.remove-down")
    return native_editor_command_id::focus_remove_down;
  if (id == "editor.focus.exit-left")
    return native_editor_command_id::focus_exit_left;
  if (id == "editor.focus.exit-right")
    return native_editor_command_id::focus_exit_right;
  if (id == "editor.focus.remove-tag")
    return native_editor_command_id::focus_remove_tag;
  if (id == "editor.focus.help")
    return native_editor_command_id::focus_help;
  return native_editor_command_id::none;
}

QTMCommandState
native_editor_command_state (const QString& id,
                             const QTMCommandContext& context) {
  QTMCommandState result;
  qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
  if (proxy == nullptr) return result;
  actor_editor_command_snapshot snapshot= proxy->editor_command_state ();
  if (!snapshot.valid ()) return result;

  native_editor_command_id command= editor_command_id (id);
  if (command == native_editor_command_id::none) return result;
  result.available= true;
  const bool localEdit=
    (command == native_editor_command_id::undo ||
     command == native_editor_command_id::redo ||
     command == native_editor_command_id::copy ||
     command == native_editor_command_id::cut ||
     command == native_editor_command_id::paste) &&
    local_text_input_owns_edit_command (context);
  if (localEdit) return result;

  const bool hasSelection=
    snapshot.selection_active () || snapshot.graphics_selection_active ();
  actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
  const bool genericFocus=
    focus.valid () &&
    !focus.has (ACTOR_FOCUS_TOOLBAR_BUFFER) &&
    !focus.has (ACTOR_FOCUS_TOOLBAR_CODE_CONTEXT) &&
    !focus.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT);
  switch (command) {
  case native_editor_command_id::undo:
    result.enabled= !snapshot.read_only () && snapshot.undo_count != 0;
    break;
  case native_editor_command_id::redo:
    result.enabled= !snapshot.read_only () && snapshot.redo_count != 0;
    break;
  case native_editor_command_id::copy:
    result.enabled= hasSelection;
    break;
  case native_editor_command_id::cut:
    result.enabled= !snapshot.read_only () && hasSelection;
    break;
  case native_editor_command_id::paste:
    result.enabled= !snapshot.read_only ();
    break;
  case native_editor_command_id::node_properties:
    result.enabled= snapshot.focus_node_available ();
    break;
  case native_editor_command_id::save:
  case native_editor_command_id::update_all:
    result.enabled= !snapshot.read_only ();
    break;
  case native_editor_command_id::revert:
  case native_editor_command_id::close_document:
  case native_editor_command_id::save_as:
  case native_editor_command_id::preview:
  case native_editor_command_id::close_window:
  case native_editor_command_id::history_back:
  case native_editor_command_id::history_forward:
    result.enabled= true;
    break;
  case native_editor_command_id::presentation_first:
  case native_editor_command_id::presentation_previous:
  case native_editor_command_id::presentation_next:
  case native_editor_command_id::presentation_last:
    result.available= snapshot.presentation_mode ();
    result.enabled= result.available;
    break;
  case native_editor_command_id::presentation_previous_screen:
  case native_editor_command_id::presentation_next_screen:
    result.available= snapshot.presentation_mode () && snapshot.screens_mode ();
    result.enabled= result.available;
    break;
  case native_editor_command_id::print:
    result.available= has_printing_cmd ();
    result.enabled= result.available;
    break;
  case native_editor_command_id::print_to_file: {
    bool useDialog=
      get_user_preference ("gui:print dialogue", "on") == "on";
    result.available= !useDialog || !has_printing_cmd ();
    result.enabled= result.available;
    break;
  }
  case native_editor_command_id::print_page_selection: {
    bool useDialog=
      get_user_preference ("gui:print dialogue", "on") == "on";
    result.available= !useDialog && !has_printing_cmd ();
    result.enabled= result.available;
    break;
  }
  case native_editor_command_id::print_page_selection_to_file:
    result.available=
      get_user_preference ("gui:print dialogue", "on") != "on";
    result.enabled= result.available;
    break;
  case native_editor_command_id::export_pdf:
  case native_editor_command_id::export_postscript:
    result.enabled= true;
    break;
  case native_editor_command_id::focus_traverse_first:
  case native_editor_command_id::focus_traverse_previous:
  case native_editor_command_id::focus_traverse_next:
  case native_editor_command_id::focus_traverse_last:
    result.available=
      genericFocus && focus.has (ACTOR_FOCUS_TOOLBAR_CAN_MOVE);
    result.enabled= result.available;
    break;
  case native_editor_command_id::focus_insert_left:
  case native_editor_command_id::focus_insert_right:
    result.available=
      genericFocus &&
      focus.has (ACTOR_FOCUS_TOOLBAR_CAN_INSERT_REMOVE) &&
      (focus.has (ACTOR_FOCUS_TOOLBAR_VERTICAL) ||
       (focus.has (ACTOR_FOCUS_TOOLBAR_HORIZONTAL) &&
        focus.has (ACTOR_FOCUS_TOOLBAR_CAN_INSERT)));
    result.enabled= result.available && !snapshot.read_only ();
    break;
  case native_editor_command_id::focus_remove_left:
  case native_editor_command_id::focus_remove_right:
    result.available=
      genericFocus &&
      focus.has (ACTOR_FOCUS_TOOLBAR_CAN_INSERT_REMOVE) &&
      (focus.has (ACTOR_FOCUS_TOOLBAR_VERTICAL) ||
       (focus.has (ACTOR_FOCUS_TOOLBAR_HORIZONTAL) &&
        focus.has (ACTOR_FOCUS_TOOLBAR_CAN_REMOVE)));
    result.enabled= result.available && !snapshot.read_only ();
    break;
  case native_editor_command_id::focus_insert_up:
  case native_editor_command_id::focus_insert_down:
  case native_editor_command_id::focus_remove_up:
  case native_editor_command_id::focus_remove_down:
    result.available=
      genericFocus &&
      focus.has (ACTOR_FOCUS_TOOLBAR_CAN_INSERT_REMOVE) &&
      focus.has (ACTOR_FOCUS_TOOLBAR_VERTICAL);
    result.enabled= result.available && !snapshot.read_only ();
    break;
  case native_editor_command_id::focus_exit_left:
  case native_editor_command_id::focus_exit_right:
  case native_editor_command_id::focus_remove_tag:
    result.available=
      genericFocus && focus.has (ACTOR_FOCUS_TOOLBAR_CURSOR_INSIDE);
    result.enabled= result.available && !snapshot.read_only ();
    break;
  case native_editor_command_id::focus_help:
    result.available=
      genericFocus ||
      (focus.valid () &&
       (focus.has (ACTOR_FOCUS_TOOLBAR_BUFFER) ||
        focus.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT)));
    result.enabled= result.available;
    break;
  default:
    break;
  }
  return result;
}

bool
invoke_native_editor_command (const QString& id,
                               const QTMCommandContext& context) {
  qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
  if (proxy == nullptr) return false;
  native_editor_command_id command= editor_command_id (id);
  return command != native_editor_command_id::none &&
         proxy->submit_editor_command (command);
}

QTMCommandState
native_editor_view_command_state (const QTMCommandContext& context,
                                  bool writable) {
  QTMCommandState result;
  qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
  if (proxy == nullptr) return result;
  actor_editor_command_snapshot snapshot= proxy->editor_command_state ();
  if (!snapshot.valid ()) return result;
  result.available= true;
  result.enabled= !writable || !snapshot.read_only ();
  return result;
}

QTMCommandState
math_mode_command_state (const QTMCommandContext& context, bool writable) {
  QTMCommandState result;
  qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
  if (proxy == nullptr) return result;
  actor_editor_command_snapshot snapshot= proxy->editor_command_state ();
  if (!snapshot.valid () || !snapshot.math_mode ()) return result;
  result.available= true;
  result.enabled= !writable || !snapshot.read_only ();
  return result;
}

} // namespace qtm_command_registry_detail

void
QTMCommandRegistry::registerEditorCommands () {
  registerProvider (
    "file-export-formats", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      if (is_none (frozen_document_url (context))) return out;
      try {
        QVector<QString> data= scheme_string_vector (
          call ("native-export-format-provider-data"));
        for (int i= 0; i + 2 < data.size (); i += 3) {
          QTMCommandDynamicItem item= enabled_dynamic_item (
            data[i], QObject::tr ("Export as %1").arg (data[i + 1]));
          item.help= data[i + 2];
          out.append (std::move (item));
        }
      }
      catch (...) {}
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      url source= frozen_document_url (context);
      if (key.isEmpty () || is_none (source)) return false;
      QString suffix= provider_format_suffix (key);
      QString path= QFileDialog::getSaveFileName (
        context.shell.data (), QObject::tr ("Export document"), QString (),
        provider_file_filter (key, suffix));
      if (path.isEmpty ()) return true;
      if (!suffix.isEmpty () && QFileInfo (path).suffix ().isEmpty ())
        path += QStringLiteral (".") + suffix;
      bool failed= buffer_export (
        source, url_system (from_qstring (path)), from_qstring (key));
      if (failed)
        QMessageBox::warning (
          context.shell.data (), QObject::tr ("Export"),
          QObject::tr ("Could not export the document to %1.").arg (path));
      return !failed;
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state;
      if (is_none (frozen_document_url (context))) return state;
      state.available= true;
      state.enabled= true;
      return state;
    });
  registerProvider (
    "selection-image-formats", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_editor_command_snapshot snapshot= proxy->editor_command_state ();
      if (!snapshot.valid () ||
          (!snapshot.selection_active () &&
           !snapshot.graphics_selection_active ()))
        return out;
      try {
        QVector<QString> data= scheme_string_vector (
          call ("native-selection-image-format-provider-data"));
        for (int i= 0; i + 2 < data.size (); i += 3) {
          QTMCommandDynamicItem item= enabled_dynamic_item (
            data[i], data[i + 1], data[i + 2]);
          out.append (std::move (item));
        }
      }
      catch (...) {}
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      return proxy != nullptr && !key.isEmpty () &&
             proxy->submit_editor_command (
               native_editor_command_id::export_selection_image,
               from_qstring (key));
    },
    [] (const QTMCommandContext& context) {
      return editor_provider_state (context, 0, false, true);
    });
  registerProvider (
    "realtime-save-toggle", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      url source= frozen_document_url (context);
      if (is_none (source) ||
          athena_current_document_save_mode () !=
            athena_document_save_mode::realtime ||
          !athena_realtime_save_eligible (source))
        return out;
      bool paused= athena_realtime_save_paused (source);
      out.append (enabled_dynamic_item (
        QStringLiteral ("toggle"),
        paused ? QObject::tr ("Resume realtime save")
               : QObject::tr ("Pause realtime save")));
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      if (key != QStringLiteral ("toggle")) return false;
      url source= frozen_document_url (context);
      if (is_none (source) || !athena_realtime_save_eligible (source))
        return false;
      return athena_set_realtime_save_paused (
        source, !athena_realtime_save_paused (source));
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state;
      url source= frozen_document_url (context);
      if (is_none (source) ||
          athena_current_document_save_mode () !=
            athena_document_save_mode::realtime ||
          !athena_realtime_save_eligible (source))
        return state;
      state.available= true;
      state.enabled= true;
      return state;
    });
  registerProvider (
    "editor-text-colors", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      return editor_color_items (
        context,
        ACTOR_EDITOR_COMMAND_STATE_TEXT_MODE |
        ACTOR_EDITOR_COMMAND_STATE_SOURCE_MODE);
    },
    [] (const QString& key, const QTMCommandContext& context) {
      return execute_editor_color (key, context);
    },
    [] (const QTMCommandContext& context) {
      return editor_provider_state (
        context,
        ACTOR_EDITOR_COMMAND_STATE_TEXT_MODE |
        ACTOR_EDITOR_COMMAND_STATE_SOURCE_MODE,
        true);
    });
  registerProvider (
    "editor-prog-colors", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      return editor_color_items (
        context, ACTOR_EDITOR_COMMAND_STATE_PROG_MODE);
    },
    [] (const QString& key, const QTMCommandContext& context) {
      return execute_editor_color (key, context);
    },
    [] (const QTMCommandContext& context) {
      return editor_provider_state (
        context, ACTOR_EDITOR_COMMAND_STATE_PROG_MODE, true);
    });
  registerProvider (
    "editor-math-colors", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      return editor_color_items (
        context, ACTOR_EDITOR_COMMAND_STATE_MATH_MODE);
    },
    [] (const QString& key, const QTMCommandContext& context) {
      return execute_editor_color (key, context);
    },
    [] (const QTMCommandContext& context) {
      return editor_provider_state (
        context, ACTOR_EDITOR_COMMAND_STATE_MATH_MODE, true);
    });
  registerProvider (
    "editor-prominent-spacing", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_editor_command_snapshot snapshot= proxy->editor_command_state ();
      const std::uint32_t required=
        ACTOR_EDITOR_COMMAND_STATE_TEXT_MODE |
        ACTOR_EDITOR_COMMAND_STATE_STD_MARKUP;
      if (!snapshot.valid () ||
          (snapshot.flags & required) != required ||
          !proxy->prominent_spacing_available ())
        return out;
      const bool enabled= !snapshot.read_only ();
      const struct { const char* key; const char* label; } values[]= {
        {"compact", "Compact"},
        {"compressed", "Compressed"},
        {"amplified", "Amplified"}
      };
      for (const auto& value: values) {
        QTMCommandDynamicItem item= enabled_dynamic_item (
          QString::fromLatin1 (value.key), QObject::tr (value.label));
        item.state.enabled= enabled;
        out.append (std::move (item));
      }
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      static const QSet<QString> allowed {
        QStringLiteral ("compact"),
        QStringLiteral ("compressed"),
        QStringLiteral ("amplified")
      };
      if (!allowed.contains (key)) return false;
      QJsonObject action;
      action.insert ("op", "make");
      action.insert ("tag", key);
      return submit_inline_editor_action (
        context, action,
        ACTOR_EDITOR_COMMAND_STATE_TEXT_MODE |
        ACTOR_EDITOR_COMMAND_STATE_STD_MARKUP,
        ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return state;
      actor_editor_command_snapshot snapshot= proxy->editor_command_state ();
      const std::uint32_t required=
        ACTOR_EDITOR_COMMAND_STATE_TEXT_MODE |
        ACTOR_EDITOR_COMMAND_STATE_STD_MARKUP;
      if (!snapshot.valid () ||
          (snapshot.flags & required) != required ||
          !proxy->prominent_spacing_available ())
        return state;
      state.available= true;
      state.enabled= !snapshot.read_only ();
      return state;
    });
  registerProvider (
    "editor-semantic-annotations", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_editor_command_snapshot snapshot= proxy->editor_command_state ();
      if (!snapshot.valid () || !snapshot.math_mode () ||
          get_user_preference ("semantic editing", "off") != "on")
        return out;
      const bool enabled= !snapshot.read_only ();
      const struct {
        const char* key;
        const char* label;
      } values[]= {
        {"math-ordinary", "Ordinary symbol"},
        {"math-ignore", "Ignore"},
        {"math-separator", "Separator"},
        {"math-quantifier", "Quantifier"},
        {"math-imply", "Logical implication"},
        {"math-or", "Logical or"},
        {"math-and", "Logical and"},
        {"math-not", "Logical not"},
        {"math-relation", "Relation"},
        {"math-union", "Set union"},
        {"math-intersection", "Set intersection"},
        {"math-exclude", "Set difference"},
        {"math-plus", "Addition"},
        {"math-minus", "Subtraction"},
        {"math-times", "Multiplication"},
        {"math-over", "Division"},
        {"math-prefix", "Prefix"},
        {"math-postfix", "Postfix"},
        {"math-open", "Open"},
        {"math-close", "Close"},
        {"syntax", "Other"}
      };
      for (const auto& value: values) {
        QTMCommandDynamicItem item= enabled_dynamic_item (
          QString::fromLatin1 (value.key), QObject::tr (value.label));
        item.state.enabled= enabled;
        out.append (std::move (item));
      }
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      static const QSet<QString> allowed {
        "math-ordinary", "math-ignore", "math-separator", "math-quantifier",
        "math-imply", "math-or", "math-and", "math-not", "math-relation",
        "math-union", "math-intersection", "math-exclude", "math-plus",
        "math-minus", "math-times", "math-over", "math-prefix",
        "math-postfix", "math-open", "math-close", "syntax"
      };
      if (!allowed.contains (key) ||
          get_user_preference ("semantic editing", "off") != "on")
        return false;
      QJsonObject action;
      action.insert ("op", "make");
      action.insert ("tag", key);
      return submit_inline_editor_action (
        context, action, ACTOR_EDITOR_COMMAND_STATE_MATH_MODE,
        ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state=
        editor_provider_state (
          context, ACTOR_EDITOR_COMMAND_STATE_MATH_MODE, true);
      if (!state.available) return state;
      if (get_user_preference ("semantic editing", "off") != "on") {
        state.available= false;
        state.enabled= false;
      }
      return state;
    });
  registerProvider (
    "editor-personal-macros", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_editor_command_snapshot commandState= proxy->editor_command_state ();
      const std::uint32_t insertModes=
        ACTOR_EDITOR_COMMAND_STATE_TEXT_MODE |
        ACTOR_EDITOR_COMMAND_STATE_MATH_MODE |
        ACTOR_EDITOR_COMMAND_STATE_PROG_MODE;
      if (!commandState.valid () || (commandState.flags & insertModes) == 0)
        return out;
      actor_dynamic_menu_snapshot snapshot= proxy->personal_macro_items ();
      if (!snapshot.ready) {
        (void) proxy->request_personal_macro_items ();
        QTMCommandDynamicItem loading= enabled_dynamic_item (
          QStringLiteral ("__loading__"),
          QObject::tr ("Loading personal macros..."));
        loading.state.enabled= false;
        out.append (std::move (loading));
        return out;
      }
      for (const actor_dynamic_menu_item_snapshot& source: snapshot.items) {
        QString group= QString::fromUtf8 (
          source.group.data (), static_cast<int> (source.group.size ()));
        QString label= QString::fromUtf8 (
          source.label.data (), static_cast<int> (source.label.size ()));
        QString key= QString::fromUtf8 (
          source.key.data (), static_cast<int> (source.key.size ()));
        if (key.isEmpty ()) continue;
        QTMCommandDynamicItem item= enabled_dynamic_item (
          key, label, QObject::tr ("Insert personal macro %1").arg (label));
        item.group= group;
        out.append (std::move (item));
      }
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      if (key.isEmpty () || key == QStringLiteral ("__loading__"))
        return false;
      QJsonObject action;
      action.insert ("op", "make");
      action.insert ("tag", key);
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      return editor_provider_state (
        context,
        ACTOR_EDITOR_COMMAND_STATE_TEXT_MODE |
        ACTOR_EDITOR_COMMAND_STATE_MATH_MODE |
        ACTOR_EDITOR_COMMAND_STATE_PROG_MODE,
        true);
    });
  registerBehavior (
    "editor.search", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QTMWidget* canvas= editor_canvas_for_context (context);
      if (canvas == nullptr) return false;
      QTMDocumentSearchBar::showForCanvas (canvas, false);
      return true;
    },
    [] (const QTMCommandContext& context) {
      return native_editor_view_command_state (context, false);
    });
  registerBehavior (
    "editor.replace", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QTMWidget* canvas= editor_canvas_for_context (context);
      if (canvas == nullptr) return false;
      QTMDocumentSearchBar::showForCanvas (canvas, true);
      return true;
    },
    [] (const QTMCommandContext& context) {
      return native_editor_view_command_state (context, true);
    });
  registerBehavior (
    "editor.math-correct-all", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      return proxy != nullptr &&
             proxy->submit_editor_command (
               native_editor_command_id::math_correct_all);
    },
    [] (const QTMCommandContext& context) {
      return math_mode_command_state (context, true);
    });

  auto registerMathPreference=
    [this] (const QString& id, string preference) {
      registerBehavior (
        id, QTMCommandScope::Editor,
        [preference] (const QTMCommandContext& context) {
          QTMCommandState state= math_mode_command_state (context, false);
          if (!state.available || !state.enabled) return false;
          string current= get_user_preference (preference, "on");
          set_user_preference (
            preference, current == "on" ? string ("off") : string ("on"));
          return true;
        },
        [preference] (const QTMCommandContext& context) {
          QTMCommandState state= math_mode_command_state (context, false);
          if (!state.available) return state;
          state.checkable= true;
          state.checked= get_user_preference (preference, "on") == "on";
          return state;
        });
    };
  registerMathPreference (
    "editor.math-correct-remove-superfluous",
    "manual remove superfluous invisible");
  registerMathPreference (
    "editor.math-correct-insert-missing",
    "manual insert missing invisible");
  registerMathPreference (
    "editor.math-correct-homoglyph",
    "manual homoglyph correct");

  auto registerMathToggle=
    [this] (const QString& id, string preference, string defaultValue,
            string onValue, string offValue,
            std::function<bool(const QTMCommandContext&)> available= {}) {
      registerBehavior (
        id, QTMCommandScope::Editor,
        [preference, defaultValue, onValue, offValue, available]
        (const QTMCommandContext& context) {
          QTMCommandState state= math_mode_command_state (context, false);
          if (!state.available || !state.enabled) return false;
          if (available && !available (context)) return false;
          string current= get_user_preference (preference, defaultValue);
          set_user_preference (
            preference, current == offValue ? onValue : offValue);
          return true;
        },
        [preference, defaultValue, offValue, available]
        (const QTMCommandContext& context) {
          QTMCommandState state= math_mode_command_state (context, false);
          if (!state.available) return state;
          if (available && !available (context)) {
            state.available= false;
            state.enabled= false;
            return state;
          }
          state.checkable= true;
          state.checked=
            get_user_preference (preference, defaultValue) != offValue;
          return state;
        });
    };
  registerMathToggle (
    "editor.math-preferences.match-brackets",
    "automatic brackets", "mathematics", "mathematics", "off");
  registerMathToggle (
    "editor.math-preferences.large-brackets",
    "use large brackets", "on", "on", "off");
  registerMathToggle (
    "editor.math-preferences.full-context",
    "show full context", "on", "on", "off");
  registerMathToggle (
    "editor.math-preferences.table-cells",
    "show table cells", "on", "on", "off",
    [] (const QTMCommandContext& context) {
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      return proxy != nullptr && proxy->inside_table ();
    });
  registerMathToggle (
    "editor.math-preferences.focus",
    "show focus", "on", "on", "off");
  registerMathToggle (
    "editor.math-preferences.semantic-focus-only",
    "show only semantic focus", "on", "on", "off",
    [] (const QTMCommandContext&) {
      return get_user_preference ("semantic editing", "off") != "off";
    });
  registerMathToggle (
    "editor.math-preferences.semantic-editing",
    "semantic editing", "off", "on", "off");
  registerMathToggle (
    "editor.math-preferences.semantic-selections",
    "semantic selections", "on", "on", "off",
    [] (const QTMCommandContext&) {
      return get_user_preference ("semantic editing", "off") == "on";
    });
  registerMathToggle (
    "editor.math-preferences.semantic-correctness",
    "semantic correctness", "off", "on", "off");

  const QString editorCommands[]= {
    "editor.undo",
    "editor.redo",
    "editor.copy",
    "editor.cut",
    "editor.paste",
    "editor.node-properties",
    "editor.save",
    "editor.revert",
    "editor.update-all",
    "editor.close-document",
    "editor.save-as",
    "editor.preview",
    "editor.print",
    "editor.close-window",
    "editor.history-back",
    "editor.history-forward",
    "editor.presentation-first",
    "editor.presentation-previous-screen",
    "editor.presentation-previous",
    "editor.presentation-next",
    "editor.presentation-next-screen",
    "editor.presentation-last",
    "editor.print-to-file",
    "editor.print-page-selection",
    "editor.print-page-selection-to-file",
    "editor.export-pdf",
    "editor.export-postscript"
  };
  for (const QString& id: editorCommands)
    registerBehavior (
      id, QTMCommandScope::Editor,
      [id] (const QTMCommandContext& context) {
        return invoke_native_editor_command (id, context);
      },
      [id] (const QTMCommandContext& context) {
        return native_editor_command_state (id, context);
      });
}
