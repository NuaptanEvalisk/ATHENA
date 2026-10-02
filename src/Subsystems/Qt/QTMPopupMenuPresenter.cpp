/******************************************************************************
* MODULE     : QTMPopupMenuPresenter.cpp
* DESCRIPTION: Registry-backed transient editor popup menus
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "QTMPopupMenuPresenter.hpp"

#include "QTMApplication.hpp"
#include "QTMCommandRegistryInternal.hpp"
#include "boot.hpp"

#include <QAction>
#include <QColor>
#include <QIcon>
#include <QMenu>
#include <QPixmap>

using namespace qtm_command_registry_detail;

namespace {

QIcon
popup_icon (const QString& value) {
  if (value.isEmpty ()) return QIcon ();
  if (value.startsWith ('#')) {
    QColor color (value);
    if (color.isValid ()) {
      QPixmap pixmap (16, 16);
      pixmap.fill (color);
      return QIcon (pixmap);
    }
  }
  QIcon themed= QIcon::fromTheme (value);
  if (!themed.isNull ()) return themed;
  if (tmapp () != nullptr)
    return tmapp ()->icon_manager ().getIcon (url (from_qstring (value)));
  return QIcon ();
}

QString
command_text (const QTMCommandDefinition& command) {
  QString text= command.label;
  if (!command.shortcut.isEmpty ())
    text += QStringLiteral ("\t") +
            command.shortcut.toString (QKeySequence::NativeText);
  return text;
}

bool
item_condition (
  const QTMCommandMenuItem& item, const QTMCommandContext& context) {
  if (item.requiredFlags == 0 && item.forbiddenFlags == 0 &&
      item.anyFlags == 0 && item.focusRequiredFlags == 0 &&
      item.focusForbiddenFlags == 0 && item.focusAnyFlags == 0)
    return true;

  qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
  if (proxy == nullptr) return false;
  actor_editor_command_snapshot editor= proxy->editor_command_state ();
  if (!editor.valid ()) return false;
  if ((editor.flags & item.requiredFlags) != item.requiredFlags ||
      (editor.flags & item.forbiddenFlags) != 0 ||
      (item.anyFlags != 0 && (editor.flags & item.anyFlags) == 0))
    return false;
  if (item.focusRequiredFlags == 0 && item.focusForbiddenFlags == 0 &&
      item.focusAnyFlags == 0)
    return true;
  actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
  return focus.valid () &&
         (focus.flags & item.focusRequiredFlags) == item.focusRequiredFlags &&
         (focus.flags & item.focusForbiddenFlags) == 0 &&
         (item.focusAnyFlags == 0 ||
          (focus.flags & item.focusAnyFlags) != 0);
}

bool
menu_reference_visible (
  const QString& id, const QTMCommandContext& context) {
  qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
  if (proxy == nullptr) return id == QStringLiteral ("help") ||
                               id == QStringLiteral ("workspace") ||
                               id == QStringLiteral ("go");
  actor_editor_command_snapshot editor= proxy->editor_command_state ();
  actor_document_menu_snapshot document= proxy->document_menu_state ();
  if (!editor.valid ()) return false;

  const bool graphicsOnly=
    editor.has (ACTOR_EDITOR_COMMAND_STATE_GRAPHICS_MODE) &&
    !document.commutative_diagram;
  if (id == QStringLiteral ("manual"))
    return !graphicsOnly &&
           editor.has (ACTOR_EDITOR_COMMAND_STATE_MANUAL_STYLE);
  if (id == QStringLiteral ("source"))
    return !graphicsOnly &&
      (editor.has (ACTOR_EDITOR_COMMAND_STATE_SOURCE_MODE) ||
       get_user_preference ("source tool", "off") == "on");
  if (id == QStringLiteral ("dynamic"))
    return !graphicsOnly && editor.presentation_mode ();
  if (id == QStringLiteral ("format"))
    return !graphicsOnly;
  if (id == QStringLiteral ("automate"))
    return document.ready && document.automate_style;
  return true;
}

void
normalize_separators (QMenu* menu) {
  if (menu == nullptr) return;
  QList<QAction*> actions= menu->actions ();
  bool seenContent= false;
  QAction* pendingSeparator= nullptr;
  for (QAction* action: actions) {
    if (action == nullptr || !action->isVisible ()) continue;
    if (action->isSeparator ()) {
      action->setVisible (false);
      if (seenContent) pendingSeparator= action;
      continue;
    }
    if (pendingSeparator != nullptr) {
      pendingSeparator->setVisible (true);
      pendingSeparator= nullptr;
    }
    seenContent= true;
  }
}

bool
menu_has_content (QMenu* menu) {
  if (menu == nullptr) return false;
  for (QAction* action: menu->actions ())
    if (action != nullptr && action->isVisible () && !action->isSeparator ())
      return true;
  return false;
}

void
add_provider (
  QMenu* menu, const QString& providerId, const QTMCommandContext& context) {
  if (menu == nullptr) return;
  QTMCommandRegistry& registry= QTMCommandRegistry::instance ();
  QTMCommandState providerState= registry.providerState (providerId, context);
  if (!providerState.available) return;
  QVector<QTMCommandDynamicItem> values=
    registry.providerItems (providerId, context);
  QHash<QString, QMenu*> groups;
  for (const QTMCommandDynamicItem& value: values) {
    if (!value.state.available) continue;
    QMenu* target= menu;
    if (!value.group.isEmpty ()) {
      QMenu* group= groups.value (value.group, nullptr);
      if (group == nullptr) {
        group= menu->addMenu (value.group);
        groups.insert (value.group, group);
      }
      target= group;
    }
    QAction* action= target->addAction (
      popup_icon (value.icon), value.label);
    action->setToolTip (value.help);
    action->setStatusTip (value.help);
    action->setWhatsThis (value.help);
    action->setEnabled (value.state.enabled);
    action->setCheckable (value.state.checkable);
    if (value.state.checkable) action->setChecked (value.state.checked);
    const QString key= value.key;
    QObject::connect (
      action, &QAction::triggered, action,
      [providerId, key, context] {
        (void) QTMCommandRegistry::instance ().executeProviderItem (
          providerId, key, context);
      });
  }
}

bool
add_menu_items (
  QMenu* menu, const QVector<QTMCommandMenuItem>& items,
  const QTMCommandContext& context) {
  if (menu == nullptr) return false;
  QTMCommandRegistry& registry= QTMCommandRegistry::instance ();
  for (const QTMCommandMenuItem& item: items) {
    if (!item_condition (item, context)) continue;
    if (item.kind == QTMCommandMenuItem::Kind::Separator) {
      menu->addSeparator ();
      continue;
    }
    if (item.kind == QTMCommandMenuItem::Kind::Command) {
      const QTMCommandDefinition* command= registry.command (item.commandId);
      if (command == nullptr) continue;
      QTMCommandState state= registry.state (item.commandId, context);
      if (!state.available) continue;
      QAction* action= menu->addAction (
        popup_icon (command->icon), command_text (*command));
      action->setToolTip (command->help);
      action->setStatusTip (command->help);
      action->setWhatsThis (command->help);
      action->setEnabled (state.enabled);
      action->setCheckable (state.checkable);
      if (state.checkable) action->setChecked (state.checked);
      const QString commandId= item.commandId;
      QObject::connect (
        action, &QAction::triggered, action,
        [commandId, context] {
          (void) QTMCommandRegistry::instance ().execute (
            commandId, context);
        });
      continue;
    }
    if (item.kind == QTMCommandMenuItem::Kind::Provider) {
      add_provider (menu, item.providerId, context);
      continue;
    }
    QMenu* submenu= new QMenu (item.label, menu);
    submenu->setIcon (popup_icon (item.icon));
    (void) add_menu_items (submenu, item.items, context);
    normalize_separators (submenu);
    if (menu_has_content (submenu)) menu->addMenu (submenu);
    else delete submenu;
  }
  normalize_separators (menu);
  return menu_has_content (menu);
}

} // namespace

QMenu*
qtm_create_popup_menu (
  const QString& popupId, const QTMCommandContext& context, QWidget* parent) {
  QTMCommandRegistry& registry= QTMCommandRegistry::instance ();
  if (!registry.initialize ()) return nullptr;
  const QTMCommandPopupDefinition* definition= registry.popup (popupId);
  if (definition == nullptr) return nullptr;

  QMenu* root= new QMenu (parent);
  for (const QTMCommandPopupItem& item: definition->items) {
    if (item.kind == QTMCommandPopupItem::Kind::Separator) {
      root->addSeparator ();
      continue;
    }
    if (item.kind == QTMCommandPopupItem::Kind::Provider) {
      add_provider (root, item.providerId, context);
      continue;
    }
    if (item.kind == QTMCommandPopupItem::Kind::Command) {
      const QTMCommandDefinition* command= registry.command (item.commandId);
      if (command == nullptr) continue;
      QTMCommandState state= registry.state (item.commandId, context);
      if (!state.available) continue;
      QAction* action= root->addAction (
        popup_icon (command->icon), command_text (*command));
      action->setEnabled (state.enabled);
      action->setCheckable (state.checkable);
      if (state.checkable) action->setChecked (state.checked);
      const QString commandId= item.commandId;
      QObject::connect (
        action, &QAction::triggered, action,
        [commandId, context] {
          (void) QTMCommandRegistry::instance ().execute (
            commandId, context);
        });
      continue;
    }

    if (!menu_reference_visible (item.menuId, context)) continue;
    const QTMCommandMenuDefinition* referenced= registry.menu (item.menuId);
    if (referenced == nullptr) continue;
    if (item.flattenMenu) {
      (void) add_menu_items (root, referenced->items, context);
      continue;
    }
    QMenu* submenu= new QMenu (referenced->label, root);
    (void) add_menu_items (submenu, referenced->items, context);
    normalize_separators (submenu);
    if (menu_has_content (submenu)) root->addMenu (submenu);
    else delete submenu;
  }
  normalize_separators (root);
  if (!menu_has_content (root)) {
    delete root;
    return nullptr;
  }
  return root;
}
