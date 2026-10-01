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

#include <QApplication>

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
