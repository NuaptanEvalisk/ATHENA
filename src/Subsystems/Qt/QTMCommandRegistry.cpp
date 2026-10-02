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
#include "QTMPluginManager.hpp"

#include <QApplication>
#include <QSet>

using namespace qtm_command_registry_detail;

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
  std::function<bool(const QString&, const QTMCommandContext&)> execute,
  std::function<QTMCommandState(const QTMCommandContext&)> state) {
  ProviderBehavior provider;
  provider.scope= scope;
  provider.state= std::move (state);
  provider.items= std::move (items);
  provider.execute= std::move (execute);
  providers_.insert (id, std::move (provider));
}

void
QTMCommandRegistry::registerBuiltins () {
  registerApplicationCommands ();
  registerEditorCommands ();
  registerFocusCommands ();
  registerProvider (
    "runtime-plugin-commands", QTMCommandScope::Application,
    [this] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      for (const QString& id: runtimePluginCommandIds_) {
        const QTMCommandDefinition* definition= command (id);
        if (definition == nullptr) continue;
        QTMCommandDynamicItem item;
        item.key= id;
        item.group= runtimePluginGroups_.value (id);
        item.label= definition->label;
        item.help= definition->help;
        item.icon= definition->icon;
        item.state= state (id, context);
        out.append (std::move (item));
      }
      return out;
    },
    [this] (const QString& key, const QTMCommandContext& context) {
      return runtimePluginCommandIds_.contains (key) && execute (key, context);
    },
    [this] (const QTMCommandContext&) {
      QTMCommandState state;
      state.available= !runtimePluginCommandIds_.isEmpty ();
      state.enabled= state.available;
      return state;
    });
  registerProvider (
    "runtime-plugin-menu", QTMCommandScope::Application,
    [this] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      QTMPluginManager* manager=
        qobject_cast<QTMPluginManager*> (pluginManager_.data ());
      if (manager == nullptr) return out;
      const bool busy= manager->busy ();
      for (const QTMPluginInfo& plugin: manager->plugins ()) {
        const QString pluginId= QString::fromStdString (plugin.manifest.id);
        const QString group= QString::fromStdString (plugin.manifest.name);
        const bool active=
          plugin.running || plugin.state == QStringLiteral ("Scheduled");

        QTMCommandDynamicItem toggle= enabled_dynamic_item (
          (active ? QStringLiteral ("stop:") : QStringLiteral ("start:")) +
            pluginId,
          active ? QObject::tr ("Stop") : QObject::tr ("Start"));
        toggle.group= group;
        toggle.icon= active ? QStringLiteral ("media-playback-stop")
                            : QStringLiteral ("media-playback-start");
        toggle.state.enabled=
          !busy && plugin.state != QStringLiteral ("Invalid");
        out.append (std::move (toggle));

        QTMCommandDynamicItem restart= enabled_dynamic_item (
          QStringLiteral ("restart:") + pluginId, QObject::tr ("Restart"));
        restart.group= group;
        restart.icon= QStringLiteral ("view-refresh");
        restart.state.enabled=
          !busy && plugin.state != QStringLiteral ("Invalid");
        out.append (std::move (restart));

        QTMCommandDynamicItem force= enabled_dynamic_item (
          QStringLiteral ("force:") + pluginId, QObject::tr ("Force quit"));
        force.group= group;
        force.icon= QStringLiteral ("process-stop");
        force.state.enabled= plugin.running;
        out.append (std::move (force));

        for (const athena::plugins::plugin_command& command:
             plugin.manifest.commands) {
          const QString runtimeId=
            QStringLiteral ("plugin.command/") + pluginId +
            QStringLiteral ("/") + QString::fromStdString (command.id);
          const QTMCommandDefinition* definition= this->command (runtimeId);
          if (definition == nullptr) continue;
          QTMCommandDynamicItem item;
          item.key= QStringLiteral ("command:") + runtimeId;
          item.group= group;
          item.label= definition->label;
          item.help= definition->help;
          item.icon= definition->icon;
          item.state= state (runtimeId, context);
          out.append (std::move (item));
        }
      }
      return out;
    },
    [this] (const QString& key, const QTMCommandContext& context) {
      if (key.startsWith (QStringLiteral ("command:")))
        return execute (key.mid (8), context);
      QTMPluginManager* manager=
        qobject_cast<QTMPluginManager*> (pluginManager_.data ());
      if (manager == nullptr) return false;
      const int colon= key.indexOf (QChar (':'));
      if (colon <= 0 || colon + 1 >= key.size ()) return false;
      const QString operation= key.left (colon);
      const std::string pluginId= key.mid (colon + 1).toStdString ();
      QTMPluginInfo current;
      bool found= false;
      for (const QTMPluginInfo& plugin: manager->plugins ())
        if (plugin.manifest.id == pluginId) {
          current= plugin;
          found= true;
          break;
        }
      if (!found) return false;
      try {
        if (operation == QStringLiteral ("start")) {
          if (manager->busy () || current.state == QStringLiteral ("Invalid") ||
              current.running || current.state == QStringLiteral ("Scheduled"))
            return false;
          manager->start (pluginId, true);
          return true;
        }
        if (operation == QStringLiteral ("stop")) {
          if (manager->busy () ||
              (!current.running &&
               current.state != QStringLiteral ("Scheduled")))
            return false;
          manager->stop (pluginId);
          return true;
        }
        if (operation == QStringLiteral ("restart")) {
          if (manager->busy () || current.state == QStringLiteral ("Invalid"))
            return false;
          manager->restart (pluginId, true);
          return true;
        }
        if (operation == QStringLiteral ("force")) {
          if (!current.running) return false;
          manager->stop (pluginId, true);
          return true;
        }
      }
      catch (...) {
        return false;
      }
      return false;
    },
    [this] (const QTMCommandContext&) {
      QTMCommandState state;
      state.available= pluginManager_ != nullptr;
      state.enabled= state.available;
      return state;
    });
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

QTMCommandState
QTMCommandRegistry::providerState (
  const QString& providerId, const QTMCommandContext& context) const {
  auto found= providers_.constFind (providerId);
  if (found == providers_.constEnd ()) return {};
  if (found->state) return found->state (context);
  QTMCommandState state;
  if (found->scope == QTMCommandScope::Application) {
    state.available= true;
    state.enabled= true;
    return state;
  }
  if (found->scope == QTMCommandScope::Editor) {
    qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
    if (proxy == nullptr) return state;
    actor_editor_command_snapshot snapshot= proxy->editor_command_state ();
    if (!snapshot.valid ()) return state;
    state.available= true;
    state.enabled= true;
  }
  return state;
}

bool
QTMCommandRegistry::executeProviderItem (
  const QString& providerId, const QString& key,
  const QTMCommandContext& context) const {
  auto found= providers_.constFind (providerId);
  return found != providers_.constEnd () && found->execute &&
         found->execute (key, context);
}

void
QTMCommandRegistry::synchronizePluginCommands (QTMPluginManager* manager) {
  if (!initialized_ && !initialize ()) return;
  pluginManager_= manager;

  QSet<QString> previous;
  for (const QString& id: runtimePluginCommandIds_) {
    previous.insert (id);
    behaviors_.remove (id);
  }
  runtimePluginCommandIds_.clear ();
  runtimePluginGroups_.clear ();
  runtimePluginEnabled_.clear ();

  if (!previous.isEmpty ()) {
    QVector<QTMCommandDefinition> retained;
    retained.reserve (commands_.size ());
    commandIndex_.clear ();
    for (const QTMCommandDefinition& definition: commands_) {
      if (previous.contains (definition.id)) continue;
      commandIndex_.insert (definition.id, retained.size ());
      retained.append (definition);
    }
    commands_= std::move (retained);
  }

  if (manager == nullptr) return;
  for (const QTMPluginInfo& plugin: manager->plugins ()) {
    const QString pluginId= QString::fromStdString (plugin.manifest.id);
    const QString pluginName= QString::fromStdString (plugin.manifest.name);
    for (const athena::plugins::plugin_command& command: plugin.manifest.commands) {
      const QString commandName= QString::fromStdString (command.id);
      const QString id=
        QStringLiteral ("plugin.command/") + pluginId +
        QStringLiteral ("/") + commandName;
      if (commandIndex_.contains (id) || behaviors_.contains (id)) {
        qWarning ("Skipping colliding runtime plugin command: %s",
                  qPrintable (id));
        continue;
      }
      const bool enabled= plugin.running && plugin.state != QStringLiteral ("Stopping");
      runtimePluginCommandIds_.append (id);
      runtimePluginGroups_.insert (id, pluginName);
      runtimePluginEnabled_.insert (id, enabled);

      const std::string nativePluginId= plugin.manifest.id;
      const std::string nativeCommandId= command.id;
      registerBehavior (
        id, QTMCommandScope::Application,
        [this, nativePluginId, nativeCommandId] (const QTMCommandContext&) {
          QTMPluginManager* current=
            qobject_cast<QTMPluginManager*> (pluginManager_.data ());
          if (current == nullptr) return false;
          try {
            (void) current->command (nativePluginId, nativeCommandId);
            return true;
          }
          catch (...) {
            return false;
          }
        },
        [this, id] (const QTMCommandContext&) {
          QTMCommandState state;
          state.available= runtimePluginCommandIds_.contains (id);
          state.enabled= state.available && runtimePluginEnabled_.value (id, false);
          return state;
        });

      QTMCommandDefinition definition;
      definition.id= id;
      definition.label= QString::fromStdString (command.title);
      definition.icon= QStringLiteral ("system-run");
      definition.category= QStringLiteral ("Plugins");
      definition.help= pluginName + QStringLiteral (" — ") + definition.label;
      definition.scope= QTMCommandScope::Application;
      definition.showInPalette= true;
      commandIndex_.insert (id, commands_.size ());
      commands_.append (std::move (definition));
    }
  }
}
