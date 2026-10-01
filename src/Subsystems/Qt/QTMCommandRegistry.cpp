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
#include "QTMOutlinePane.hpp"
#include "QTMQuickSwitcher.hpp"
#include "QTMWebsitesManager.hpp"
#include "QTMGoogleTasksPane.hpp"
#include "qt_actor_widget.hpp"
#include "file.hpp"
#include "new_window.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"
#include "tm_ostream.hpp"

#include <QApplication>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLineEdit>
#include <QPlainTextEdit>
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

bool
open_document_from_shell (const QTMCommandContext& context) {
  QWidget* parent= context.shell.data ();
  QString path= QFileDialog::getOpenFileName (
    parent, QObject::tr ("Open document"), QString (),
    QObject::tr ("ATHENA documents (*.ath *.tm);;All files (*)"));
  if (path.isEmpty ()) return true;

  try {
    (void) call ("load-buffer", object (url_system (from_qstring (path))));
    return true;
  }
  catch (...) {
    std_warning << "native command application.open could not load "
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
    "editor.close-document"
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
  if (root.value ("version").toInt (-1) != 2)
    return failPresentation ("unsupported or missing version");
  if (!root.value ("commands").isArray ())
    return failPresentation ("commands must be an array");
  if (!root.value ("menus").isArray ())
    return failPresentation ("menus must be an array");

  QSet<QString> commandIds;
  QHash<QString, QString> shortcuts;
  for (const QJsonValue& value: root.value ("commands").toArray ()) {
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
    if (!behaviors_.contains (id))
      return failPresentation (
        QString ("presentation references unknown command id: %1").arg (id));

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
      int kinds= (separator ? 1 : 0) + (!commandId.isEmpty () ? 1 : 0) +
                 (!submenuId.isEmpty () ? 1 : 0);
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
        if (!parseMenuItems (
              submenuId, itemObject.value ("items").toArray (), item.items))
          return false;
      }
      out.append (std::move (item));
    }
    return true;
  };
  for (const QJsonValue& value: root.value ("menus").toArray ()) {
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

  return true;
}

bool
QTMCommandRegistry::initialize () {
  if (initialized_) return true;
  behaviors_.clear ();
  commandIndex_.clear ();
  commands_.clear ();
  menus_.clear ();
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
