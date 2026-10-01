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

#include "QTMAbout.hpp"
#include "QTMCommandPalette.hpp"
#include "QTMArtifactsPane.hpp"
#include "QTMErrorMessagesPane.hpp"
#include "QTMNamespaceExplorer.hpp"
#include "QTMPreferencesDialog.hpp"
#include "QTMWidget.hpp"
#include "QTMATHENADiff.hpp"
#include "QTMAudmap.hpp"
#include "QTMCustomStylesManager.hpp"
#include "QTMDocumentHistoryPane.hpp"
#include "QTMDocumentSearchBar.hpp"
#include "QTMGlobalSearch.hpp"
#include "QTMMaterialsManager.hpp"
#include "QTMNeighborhoodsPane.hpp"
#include "QTMNamespaceExport.hpp"
#include "QTMNamespaceManager.hpp"
#include "QTMNativeDialogs.hpp"
#include "QTMOutlinePane.hpp"
#include "QTMQuickSwitcher.hpp"
#include "QTMWebsitesManager.hpp"
#include "QTMGoogleTasksPane.hpp"
#include "qt_actor_widget.hpp"
#include "native_editor_actions.hpp"
#include "document_persistence.hpp"
#include "boot.hpp"
#include "file.hpp"
#include "new_buffer.hpp"
#include "new_window.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"
#include "sys_utils.hpp"
#include "tm_ostream.hpp"

#include <QApplication>
#include <QAbstractSpinBox>
#include <QColor>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QMessageBox>
#include <QSet>
#include <QTextEdit>

namespace {

QTMCommandState
enabled_application_command () {
  QTMCommandState state;
  state.available= true;
  state.enabled= true;
  return state;
}

QTMCommandDynamicItem
enabled_dynamic_item (QString key, QString label, QString help= QString ()) {
  QTMCommandDynamicItem item;
  item.key= std::move (key);
  item.label= std::move (label);
  item.help= std::move (help);
  item.state.available= true;
  item.state.enabled= true;
  return item;
}

QVector<QString>
scheme_string_vector (object value) {
  QVector<QString> result;
  list<string> values= as_list_string (value);
  for (list<string> it= values; !is_nil (it); it= it->next)
    result.append (to_qstring (it->item));
  return result;
}

array<string>
scheme_string_array (object value) {
  array<string> result;
  list<string> values= as_list_string (value);
  for (list<string> it= values; !is_nil (it); it= it->next)
    result << it->item;
  return result;
}

list<string>
array_string_list (const array<string>& values, int start= 0) {
  list<string> result;
  for (int i=N(values)-1; i>=start; --i)
    result= list<string> (values[i], result);
  return result;
}

QString
provider_format_suffix (const QString& format) {
  try {
    object value= call ("format-default-suffix", object (from_qstring (format)));
    if (is_string (value)) return to_qstring (as_string (value));
  }
  catch (...) {}
  return QString ();
}

QString
provider_file_filter (const QString& format, const QString& suffix) {
  if (suffix.isEmpty ()) return QObject::tr ("All files (*)");
  QString display= format;
  if (!display.isEmpty ()) display[0]= display[0].toUpper ();
  return QObject::tr ("%1 files (*.%2);;All files (*)").arg (display, suffix);
}

bool
editor_capability_bit (const QString& name, std::uint32_t& bit) {
  static const QHash<QString, std::uint32_t> bits {
    {"read-only", ACTOR_EDITOR_COMMAND_STATE_READ_ONLY},
    {"selection", ACTOR_EDITOR_COMMAND_STATE_SELECTION},
    {"non-small-selection", ACTOR_EDITOR_COMMAND_STATE_NON_SMALL_SELECTION},
    {"math-mode", ACTOR_EDITOR_COMMAND_STATE_MATH_MODE},
    {"presentation-mode", ACTOR_EDITOR_COMMAND_STATE_PRESENTATION_MODE},
    {"screens-mode", ACTOR_EDITOR_COMMAND_STATE_SCREENS_MODE},
    {"text-mode", ACTOR_EDITOR_COMMAND_STATE_TEXT_MODE},
    {"prog-mode", ACTOR_EDITOR_COMMAND_STATE_PROG_MODE},
    {"source-mode", ACTOR_EDITOR_COMMAND_STATE_SOURCE_MODE},
    {"graphics-mode", ACTOR_EDITOR_COMMAND_STATE_GRAPHICS_MODE},
    {"poster-style", ACTOR_EDITOR_COMMAND_STATE_POSTER_STYLE},
    {"manual-style", ACTOR_EDITOR_COMMAND_STATE_MANUAL_STYLE},
    {"header-letter", ACTOR_EDITOR_COMMAND_STATE_HEADER_LETTER},
    {"book-style", ACTOR_EDITOR_COMMAND_STATE_BOOK_STYLE},
    {"section-base", ACTOR_EDITOR_COMMAND_STATE_SECTION_BASE},
    {"env-theorem", ACTOR_EDITOR_COMMAND_STATE_ENV_THEOREM},
    {"std-markup", ACTOR_EDITOR_COMMAND_STATE_STD_MARKUP},
    {"std-list", ACTOR_EDITOR_COMMAND_STATE_STD_LIST},
    {"env-float", ACTOR_EDITOR_COMMAND_STATE_ENV_FLOAT},
    {"std-fold", ACTOR_EDITOR_COMMAND_STATE_STD_FOLD},
    {"std-dtd", ACTOR_EDITOR_COMMAND_STATE_STD_DTD}
  };
  auto found= bits.constFind (name);
  if (found == bits.constEnd ()) return false;
  bit= found.value ();
  return true;
}

bool
parse_editor_capability_mask (
  const QJsonValue& value, std::uint32_t& mask, QString& error) {
  mask= 0;
  if (value.isUndefined ()) return true;
  if (!value.isArray ()) {
    error= "editor capability list must be an array";
    return false;
  }
  for (const QJsonValue& item: value.toArray ()) {
    if (!item.isString ()) {
      error= "editor capability name must be a string";
      return false;
    }
    std::uint32_t bit= 0;
    QString name= item.toString ().trimmed ();
    if (!editor_capability_bit (name, bit)) {
      error= QString ("unknown editor capability: %1").arg (name);
      return false;
    }
    mask |= bit;
  }
  return true;
}

QTMWidget*
editor_canvas_for_context (const QTMCommandContext& context) {
  QWidget* pane= context.workPane.data ();
  if (pane == nullptr) return nullptr;
  if (QTMWidget* canvas= qobject_cast<QTMWidget*> (pane)) return canvas;
  return pane->findChild<QTMWidget*> ();
}

qt_actor_widget_rep*
editor_proxy_for_context (const QTMCommandContext& context) {
  QTMWidget* canvas= editor_canvas_for_context (context);
  if (canvas == nullptr) return nullptr;
  return dynamic_cast<qt_actor_widget_rep*> (canvas->tm_widget ());
}

bool
submit_inline_editor_action (
  const QTMCommandContext& context, const QJsonObject& action,
  std::uint32_t required= 0, std::uint32_t forbidden= 0,
  std::uint32_t any= 0) {
  QString validation;
  if (!native_editor_action_validate (action, &validation)) return false;
  qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
  if (proxy == nullptr) return false;
  QString encoded= QString::fromUtf8 (
    QJsonDocument (action).toJson (QJsonDocument::Compact));
  return proxy->submit_editor_action (encoded, required, forbidden, any);
}

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

bool
local_text_input_owns_edit_command (const QTMCommandContext& context) {
  QWidget* input= context.inputWidget.data ();
  QWidget* pane= context.workPane.data ();
  if (input == nullptr || pane == nullptr ||
      (input != pane && !pane->isAncestorOf (input)))
    return false;
  if (qobject_cast<QLineEdit*> (input) != nullptr ||
      qobject_cast<QTextEdit*> (input) != nullptr ||
      qobject_cast<QPlainTextEdit*> (input) != nullptr ||
      qobject_cast<QAbstractSpinBox*> (input) != nullptr)
    return true;
  if (QComboBox* combo= qobject_cast<QComboBox*> (input))
    return combo->isEditable ();
  return false;
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

bool
open_document_from_shell (const QTMCommandContext& context,
                          bool newWindow= false) {
  QWidget* parent= context.shell.data ();
  QString path= QFileDialog::getOpenFileName (
    parent, QObject::tr ("Open document"), QString (),
    QObject::tr ("ATHENA documents (*.ath *.tm);;All files (*)"));
  if (path.isEmpty ()) return true;

  try {
    (void) call (newWindow ? "load-buffer-in-new-window" : "load-buffer",
                 object (url_system (from_qstring (path))));
    return true;
  }
  catch (...) {
    std_warning << "native command "
                << (newWindow ? "application.open-new-window"
                              : "application.open")
                << " could not load "
                << from_qstring (path) << LF;
    return false;
  }
}

url
frozen_document_url (const QTMCommandContext& context) {
  if (!context.lastDocument.has_buffer_name ()) return url_none ();
  const std::string& name= context.lastDocument.native_url_name;
  return url (string (name.data (), static_cast<int> (name.size ())));
}

} // namespace

QTMCommandRegistry&
QTMCommandRegistry::instance () {
  static QTMCommandRegistry registry;
  return registry;
}

void
QTMCommandRegistry::registerBehavior (
  const QString& id, QTMCommandScope scope,
  std::function<bool(const QTMCommandContext&)> execute,
  std::function<QTMCommandState(const QTMCommandContext&)> state) {
  Behavior behavior;
  behavior.scope= scope;
  behavior.execute= std::move (execute);
  behavior.state= std::move (state);
  behaviors_.insert (id, std::move (behavior));
}

void
QTMCommandRegistry::registerProvider (
  const QString& id, QTMCommandScope scope,
  std::function<QVector<QTMCommandDynamicItem>(
    const QTMCommandContext&)> items,
  std::function<bool(const QString&, const QTMCommandContext&)> execute) {
  ProviderBehavior provider;
  provider.scope= scope;
  provider.items= std::move (items);
  provider.execute= std::move (execute);
  providers_.insert (id, std::move (provider));
}

void
QTMCommandRegistry::registerBuiltins () {
  registerBehavior (
    "application.new-document", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      new_document_buffer ();
      return true;
    });
  registerBehavior (
    "application.open", QTMCommandScope::Application,
    [] (const QTMCommandContext& context) {
      return open_document_from_shell (context);
    });
  registerBehavior (
    "application.new-tab", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      open_document_window (false);
      return true;
    });
  registerBehavior (
    "application.open-new-window", QTMCommandScope::Application,
    [] (const QTMCommandContext& context) {
      return open_document_from_shell (context, true);
    });
  registerBehavior (
    "application.page-setup", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      qtm_page_setup_dialog_show ();
      return true;
    });
  registerBehavior (
    "application.preferences", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      qtm_preferences_dialog_show ();
      return true;
    });
  registerBehavior (
    "application.command-palette", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      command_palette_show ();
      return true;
    });
  registerBehavior (
    "application.quit", QTMCommandScope::Application,
    [] (const QTMCommandContext& context) {
      QTMMainTabWindow* shell= context.shell.data ();
      if (shell == nullptr) shell= QTMMainTabWindow::topTabWindow ();
      if (shell == nullptr) return false;
      shell->close ();
      return true;
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state= enabled_application_command ();
      state.enabled= context.shell != nullptr ||
                     QTMMainTabWindow::topTabWindow () != nullptr;
      return state;
    });
  registerProvider (
    "recent-files", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      QVector<QTMCommandDynamicItem> out;
      try {
        QVector<QString> data= scheme_string_vector (
          call ("native-recent-file-provider-data", object (25)));
        for (int i= 0; i + 2 < data.size (); i += 3) {
          QString label= data[i + 1].trimmed ();
          if (label.isEmpty ()) label= QFileInfo (data[i + 2]).fileName ();
          if (label.isEmpty ()) label= data[i];
          out.append (enabled_dynamic_item (
            data[i], label, data[i + 2]));
        }
      }
      catch (...) {}
      return out;
    },
    [] (const QString& key, const QTMCommandContext&) {
      if (key.isEmpty ()) return false;
      try {
        (void) call ("load-buffer", object (url (from_qstring (key))));
        return true;
      }
      catch (...) {
        return false;
      }
    });
  registerProvider (
    "file-import-formats", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      QVector<QTMCommandDynamicItem> out;
      try {
        QVector<QString> data= scheme_string_vector (
          call ("native-import-format-provider-data"));
        for (int i= 0; i + 2 < data.size (); i += 3) {
          QTMCommandDynamicItem item= enabled_dynamic_item (
            data[i], QObject::tr ("Import %1").arg (data[i + 1]));
          item.help= data[i + 2];
          out.append (std::move (item));
        }
      }
      catch (...) {}
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      if (key.isEmpty ()) return false;
      QString suffix= provider_format_suffix (key);
      QString path= QFileDialog::getOpenFileName (
        context.shell.data (), QObject::tr ("Import file"), QString (),
        provider_file_filter (key, suffix));
      if (path.isEmpty ()) return true;
      try {
        (void) call ("import-buffer",
                     object (url_system (from_qstring (path))),
                     object (from_qstring (key)));
        return true;
      }
      catch (...) {
        return false;
      }
    });
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
    });
  registerProvider (
    "editor-prog-colors", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      return editor_color_items (
        context, ACTOR_EDITOR_COMMAND_STATE_PROG_MODE);
    },
    [] (const QString& key, const QTMCommandContext& context) {
      return execute_editor_color (key, context);
    });
  registerProvider (
    "editor-math-colors", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      return editor_color_items (
        context, ACTOR_EDITOR_COMMAND_STATE_MATH_MODE);
    },
    [] (const QString& key, const QTMCommandContext& context) {
      return execute_editor_color (key, context);
    });
  registerBehavior (
    "workspace.namespace-explorer", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      namespace_explorer_show ();
      return true;
    });
  registerBehavior (
    "view.error-messages", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      error_messages_show ();
      return true;
    });
  registerBehavior (
    "view.artifacts", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      artifacts_pane_show ();
      return true;
    });
  registerBehavior (
    "view.outline", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      outline_pane_show ();
      return true;
    });
  registerBehavior (
    "view.neighborhoods", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      neighborhoods_pane_show ();
      return true;
    });
  registerBehavior (
    "view.document-history", QTMCommandScope::Workspace,
    [] (const QTMCommandContext& context) {
      document_history_pane_show_frozen (frozen_document_url (context));
      return true;
    });
  registerBehavior (
    "workspace.global-search", QTMCommandScope::Workspace,
    [] (const QTMCommandContext& context) {
      global_search_show_with_zoom (context.lastDocument.zoom_factor);
      return true;
    });
  registerBehavior (
    "workspace.artifacts-build-vault", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      artifacts_build_entire_vault ();
      return true;
    });
  registerBehavior (
    "file.compare-files", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      athena_diff_show ();
      return true;
    });
  registerBehavior (
    "file.export-namespace", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      namespace_export_show ();
      return true;
    });
  registerBehavior (
    "application.quick-switcher", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      open_vault_quick_switcher ();
      return true;
    });
  registerBehavior (
    "workspace.namespace-manager", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      namespace_manager_show ();
      return true;
    });
  registerBehavior (
    "workspace.websites-manager", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      websites_manager_show ();
      return true;
    });
  registerBehavior (
    "workspace.materials-manager", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      materials_manager_show ();
      return true;
    });
  registerBehavior (
    "workspace.custom-styles-manager", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      custom_styles_manager_show ();
      return true;
    });
  registerBehavior (
    "workspace.audmap-repl", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      audmap_repl_show ();
      return true;
    });
  registerBehavior (
    "workspace.google-tasks", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      google_tasks_show ();
      return true;
    });
  registerBehavior (
    "help.about", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      help_about_qt ();
      return true;
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

  const QString paneCommands[]= {
    "namespace.open",
    "namespace.copy",
    "namespace.paste",
    "namespace.rename",
    "namespace.delete",
    "namespace.refresh"
  };
  for (const QString& id: paneCommands)
    registerBehavior (id, QTMCommandScope::Pane, {});

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

bool
QTMCommandRegistry::failPresentation (const QString& message) {
  std_warning << "native command presentation error: "
              << from_qstring (message) << LF;
  commands_.clear ();
  menus_.clear ();
  toolbars_.clear ();
  commandIndex_.clear ();
  return false;
}

bool
QTMCommandRegistry::loadPresentation () {
  string text;
  const char* resource= "$ATHENA_PATH/misc/ui/application-shell.json";
  if (load_string (url (resource), text, false))
    return failPresentation (
      QString ("cannot read %1").arg (QString::fromLatin1 (resource)));

  c_string bytes (text);
  QJsonParseError parse;
  QJsonDocument document= QJsonDocument::fromJson (
    QByteArray (bytes, N(text)), &parse);
  if (parse.error != QJsonParseError::NoError || !document.isObject ())
    return failPresentation (
      QString ("invalid JSON: %1").arg (parse.errorString ()));

  QJsonObject root= document.object ();
  if (root.value ("version").toInt (-1) != 3)
    return failPresentation ("unsupported or missing version");
  if (!root.value ("commands").isArray ())
    return failPresentation ("commands must be an array");
  if (!root.value ("menus").isArray ())
    return failPresentation ("menus must be an array");
  if (!root.value ("toolbars").isArray ())
    return failPresentation ("toolbars must be an array");

  QJsonArray commandValues= root.value ("commands").toArray ();
  QJsonArray menuValues= root.value ("menus").toArray ();
  QJsonArray toolbarValues= root.value ("toolbars").toArray ();
  {
    string extensionText;
    const char* extensionResource=
      "$ATHENA_PATH/misc/ui/editor-mode-toolbar.json";
    if (load_string (url (extensionResource), extensionText, false))
      return failPresentation (
        QString ("cannot read %1")
          .arg (QString::fromLatin1 (extensionResource)));
    c_string extensionBytes (extensionText);
    QJsonParseError extensionParse;
    QJsonDocument extensionDocument= QJsonDocument::fromJson (
      QByteArray (extensionBytes, N(extensionText)), &extensionParse);
    if (extensionParse.error != QJsonParseError::NoError ||
        !extensionDocument.isObject ())
      return failPresentation (
        QString ("invalid editor mode JSON: %1")
          .arg (extensionParse.errorString ()));
    QJsonObject extensionRoot= extensionDocument.object ();
    if (extensionRoot.value ("version").toInt (-1) != 1 ||
        !extensionRoot.value ("commands").isArray () ||
        !extensionRoot.value ("toolbars").isArray ())
      return failPresentation ("invalid editor mode toolbar schema");
    for (const QJsonValue& value:
         extensionRoot.value ("commands").toArray ())
      commandValues.append (value);
    for (const QJsonValue& value:
         extensionRoot.value ("toolbars").toArray ())
      toolbarValues.append (value);
  }

  QSet<QString> commandIds;
  QHash<QString, QString> shortcuts;
  for (const QJsonValue& value: commandValues) {
    if (!value.isObject ())
      return failPresentation ("every commands entry must be an object");
    QJsonObject object= value.toObject ();
    QString id= object.value ("id").toString ().trimmed ();
    QString label= object.value ("label").toString ().trimmed ();
    QString category= object.value ("category").toString ().trimmed ();
    if (id.isEmpty () || label.isEmpty () || category.isEmpty ())
      return failPresentation (
        "every command requires non-empty id, label, and category");
    if (commandIds.contains (id))
      return failPresentation (QString ("duplicate command id: %1").arg (id));
    if (!behaviors_.contains (id)) {
      if (!object.value ("editor_action").isObject ())
        return failPresentation (
          QString ("presentation references unknown command id: %1").arg (id));
      QJsonObject action= object.value ("editor_action").toObject ();
      QString actionError;
      if (!native_editor_action_validate (action, &actionError))
        return failPresentation (
          QString ("invalid editor_action for %1: %2").arg (id, actionError));
      std::uint32_t required= 0;
      std::uint32_t forbidden= 0;
      std::uint32_t any= 0;
      QString maskError;
      if (!parse_editor_capability_mask (
            object.value ("requires"), required, maskError) ||
          !parse_editor_capability_mask (
            object.value ("forbids"), forbidden, maskError) ||
          !parse_editor_capability_mask (
            object.value ("requires_any"), any, maskError))
        return failPresentation (
          QString ("invalid capability mask for %1: %2").arg (id, maskError));
      if ((required & forbidden) != 0)
        return failPresentation (
          QString ("command %1 requires and forbids the same capability")
            .arg (id));
      QString encoded= QString::fromUtf8 (
        QJsonDocument (action).toJson (QJsonDocument::Compact));
      registerBehavior (
        id, QTMCommandScope::Editor,
        [encoded, required, forbidden, any] (const QTMCommandContext& context) {
          qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
          return proxy != nullptr &&
                 proxy->submit_editor_action (
                   encoded, required, forbidden, any);
        },
        [required, forbidden, any] (const QTMCommandContext& context) {
          QTMCommandState state;
          if (local_text_input_owns_edit_command (context)) return state;
          qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
          if (proxy == nullptr) return state;
          actor_editor_command_snapshot snapshot= proxy->editor_command_state ();
          if (!snapshot.valid ()) return state;
          state.available=
            (snapshot.flags & required) == required &&
            (snapshot.flags & forbidden) == 0 &&
            (any == 0 || (snapshot.flags & any) != 0);
          state.enabled= state.available;
          return state;
        });
    }

    QString shortcutText= object.value ("shortcut").toString ().trimmed ();
    QKeySequence shortcut;
    if (!shortcutText.isEmpty ()) {
      shortcut= QKeySequence::fromString (shortcutText,
                                          QKeySequence::PortableText);
      if (shortcut.isEmpty ())
        return failPresentation (
          QString ("invalid shortcut '%1' for %2").arg (shortcutText, id));
      QString canonical= shortcut.toString (QKeySequence::PortableText);
      if (shortcuts.contains (canonical))
        return failPresentation (
          QString ("shortcut conflict %1 between %2 and %3")
            .arg (canonical, shortcuts.value (canonical), id));
      shortcuts.insert (canonical, id);
    }

    QTMCommandDefinition definition;
    definition.id= id;
    definition.label= label;
    definition.icon= object.value ("icon").toString ().trimmed ();
    definition.category= category;
    definition.help= object.value ("help").toString ().trimmed ();
    definition.shortcut= shortcut;
    definition.scope= behaviors_.value (id).scope;
    definition.showInPalette= object.value ("palette").toBool (true);
    commandIndex_.insert (id, commands_.size ());
    commands_.append (std::move (definition));
    commandIds.insert (id);
  }

  for (auto it= behaviors_.constBegin (); it != behaviors_.constEnd (); ++it)
    if (!commandIds.contains (it.key ()))
      return failPresentation (
        QString ("native command has no presentation entry: %1").arg (it.key ()));

  QSet<QString> menuIds;
  std::function<bool(
    const QString&, const QJsonArray&, QVector<QTMCommandMenuItem>&)>
  parseMenuItems;
  parseMenuItems=
    [&] (const QString& ownerId, const QJsonArray& values,
         QVector<QTMCommandMenuItem>& out) -> bool {
    for (const QJsonValue& itemValue: values) {
      if (!itemValue.isObject ())
        return failPresentation (
          QString ("menu %1 contains a non-object item").arg (ownerId));
      QJsonObject itemObject= itemValue.toObject ();
      bool separator= itemObject.value ("separator").toBool (false);
      QString commandId=
        itemObject.value ("command").toString ().trimmed ();
      QString submenuId=
        itemObject.value ("submenu").toString ().trimmed ();
      QString providerId=
        itemObject.value ("provider").toString ().trimmed ();
      int kinds= (separator ? 1 : 0) + (!commandId.isEmpty () ? 1 : 0) +
                  (!submenuId.isEmpty () ? 1 : 0) +
                  (!providerId.isEmpty () ? 1 : 0);
      if (kinds != 1)
        return failPresentation (
          QString ("menu %1 item requires one item kind").arg (ownerId));

      QTMCommandMenuItem item;
      if (separator)
        item.kind= QTMCommandMenuItem::Kind::Separator;
      else if (!commandId.isEmpty ()) {
        if (!commandIndex_.contains (commandId))
          return failPresentation (
            QString ("menu %1 references unknown command: %2")
              .arg (ownerId, commandId));
        item.kind= QTMCommandMenuItem::Kind::Command;
        item.commandId= commandId;
      }
      else if (!providerId.isEmpty ()) {
        if (!providers_.contains (providerId))
          return failPresentation (
            QString ("menu %1 references unknown provider: %2")
              .arg (ownerId, providerId));
        item.kind= QTMCommandMenuItem::Kind::Provider;
        item.providerId= providerId;
      }
      else {
        QString label= itemObject.value ("label").toString ().trimmed ();
        if (label.isEmpty () || !itemObject.value ("items").isArray ())
          return failPresentation (
            QString ("submenu %1 requires label and items").arg (submenuId));
        if (menuIds.contains (submenuId))
          return failPresentation (
            QString ("duplicate menu/submenu id: %1").arg (submenuId));
        menuIds.insert (submenuId);
        item.kind= QTMCommandMenuItem::Kind::Submenu;
        item.submenuId= submenuId;
        item.label= label;
        item.icon= itemObject.value ("icon").toString ().trimmed ();
        if (!parseMenuItems (
              submenuId, itemObject.value ("items").toArray (), item.items))
          return false;
      }
      out.append (std::move (item));
    }
    return true;
  };
  for (const QJsonValue& value: menuValues) {
    if (!value.isObject ())
      return failPresentation ("every menus entry must be an object");
    QJsonObject object= value.toObject ();
    QString id= object.value ("id").toString ().trimmed ();
    QString label= object.value ("label").toString ().trimmed ();
    if (id.isEmpty () || label.isEmpty () || menuIds.contains (id))
      return failPresentation (
        QString ("invalid or duplicate menu id: %1").arg (id));
    if (!object.value ("items").isArray ())
      return failPresentation (
        QString ("menu %1 has no items array").arg (id));

    QTMCommandMenuDefinition menu;
    menu.id= id;
    menu.label= label;
    menuIds.insert (id);
    if (!parseMenuItems (
          id, object.value ("items").toArray (), menu.items))
      return false;
    menus_.append (std::move (menu));
  }

  for (const QJsonValue& value: toolbarValues) {
    if (!value.isObject ())
      return failPresentation ("every toolbars entry must be an object");
    QJsonObject object= value.toObject ();
    QString id= object.value ("id").toString ().trimmed ();
    if (id.isEmpty () || menuIds.contains (id))
      return failPresentation (
        QString ("invalid or duplicate toolbar id: %1").arg (id));
    if (!object.value ("items").isArray ())
      return failPresentation (
        QString ("toolbar %1 has no items array").arg (id));
    QTMCommandToolbarDefinition toolbar;
    toolbar.id= id;
    menuIds.insert (id);
    if (!parseMenuItems (
          id, object.value ("items").toArray (), toolbar.items))
      return false;
    toolbars_.append (std::move (toolbar));
  }

  return true;
}

bool
QTMCommandRegistry::initialize () {
  if (initialized_) return true;
  behaviors_.clear ();
  providers_.clear ();
  commandIndex_.clear ();
  commands_.clear ();
  menus_.clear ();
  toolbars_.clear ();
  registerBuiltins ();
  if (!loadPresentation ()) return false;
  initialized_= true;
  return true;
}

const QTMCommandDefinition*
QTMCommandRegistry::command (const QString& id) const {
  auto it= commandIndex_.constFind (id);
  if (it == commandIndex_.constEnd ()) return nullptr;
  int index= it.value ();
  if (index < 0 || index >= commands_.size ()) return nullptr;
  return &commands_[index];
}

const QTMCommandToolbarDefinition*
QTMCommandRegistry::toolbar (const QString& id) const {
  for (const QTMCommandToolbarDefinition& definition: toolbars_)
    if (definition.id == id) return &definition;
  return nullptr;
}

const QTMCommandDefinition*
QTMCommandRegistry::commandForShortcut (const QKeySequence& shortcut) const {
  if (shortcut.isEmpty ()) return nullptr;
  for (const QTMCommandDefinition& definition: commands_)
    if (!definition.shortcut.isEmpty () &&
        definition.shortcut.matches (shortcut) == QKeySequence::ExactMatch)
      return &definition;
  return nullptr;
}

QTMCommandContext
QTMCommandRegistry::captureContext (QTMMainTabWindow* shell,
                                    QWidget* inputWidget) const {
  QTMCommandContext context;
  context.shell= shell;
  if (shell != nullptr) {
    context.workPane= shell->activeWorkPaneWidget ();
    context.lastDocument= qtm_last_active_document_identity (shell);
  }
  context.inputWidget= inputWidget != nullptr ? inputWidget:
                       QApplication::focusWidget ();
  return context;
}

QTMCommandState
QTMCommandRegistry::state (const QString& id,
                           const QTMCommandContext& context) const {
  auto it= behaviors_.constFind (id);
  if (it == behaviors_.constEnd ()) return {};
  const Behavior& behavior= it.value ();

  if (behavior.scope == QTMCommandScope::Application ||
      behavior.scope == QTMCommandScope::Workspace) {
    if (behavior.state) return behavior.state (context);
    return enabled_application_command ();
  }

  if (behavior.scope == QTMCommandScope::Pane) {
    QWidget* pane= context.workPane.data ();
    QTMCommandProvider* provider= dynamic_cast<QTMCommandProvider*> (pane);
    if (provider == nullptr || !provider->qtmSupportsCommand (id)) return {};
    QTMCommandState result= provider->qtmCommandState (id);
    result.available= true;
    return result;
  }

  if (behavior.scope == QTMCommandScope::Editor) {
    if (behavior.state) return behavior.state (context);
    return {};
  }

  return {};
}

bool
QTMCommandRegistry::execute (const QString& id,
                             const QTMCommandContext& context) const {
  auto it= behaviors_.constFind (id);
  if (it == behaviors_.constEnd ()) return false;
  QTMCommandState current= state (id, context);
  if (!current.available || !current.enabled) return false;

  const Behavior& behavior= it.value ();
  if (behavior.scope == QTMCommandScope::Application ||
      behavior.scope == QTMCommandScope::Workspace)
    return behavior.execute ? behavior.execute (context): false;

  if (behavior.scope == QTMCommandScope::Pane) {
    QWidget* pane= context.workPane.data ();
    QTMCommandProvider* provider= dynamic_cast<QTMCommandProvider*> (pane);
    return provider != nullptr && provider->qtmSupportsCommand (id) &&
           provider->qtmInvokeCommand (id);
  }

  if (behavior.scope == QTMCommandScope::Editor)
    return behavior.execute ? behavior.execute (context): false;

  return false;
}

QVector<QTMCommandDynamicItem>
QTMCommandRegistry::providerItems (
  const QString& providerId, const QTMCommandContext& context) const {
  auto found= providers_.constFind (providerId);
  if (found == providers_.constEnd () || !found->items) return {};
  return found->items (context);
}

bool
QTMCommandRegistry::executeProviderItem (
  const QString& providerId, const QString& key,
  const QTMCommandContext& context) const {
  auto found= providers_.constFind (providerId);
  return found != providers_.constEnd () && found->execute &&
         found->execute (key, context);
}
