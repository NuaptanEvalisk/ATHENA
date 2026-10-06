/******************************************************************************
* MODULE     : QTMApplicationMenuPresenter.cpp
* DESCRIPTION: Registry-backed application-shell menubar presentation
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMApplicationMenuPresenter.hpp"

#include "QTMMainTabWindow.hpp"
#include "QTMCommandRegistryInternal.hpp"
#include "QTMGuiHelper.hpp"
#include "boot.hpp"
#include "qt_gui.hpp"
#include "athena_platform.hpp"
#if ATHENA_PLATFORM_IPADOS
#include "Subsystems/iOS/athena_ios.hpp"
#endif

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>

namespace {

bool
belongs_to (QWidget* child, QWidget* ancestor) {
  return child != nullptr && ancestor != nullptr &&
         (child == ancestor || ancestor->isAncestorOf (child));
}

QString
menu_action_text (const QTMCommandDefinition& command) {
  QString text= command.label;
  if (!command.shortcut.isEmpty ())
    text += "\t" + command.shortcut.toString (QKeySequence::NativeText);
  return text;
}

} // namespace

QTMApplicationMenuPresenter::QTMApplicationMenuPresenter (
  QTMMainTabWindow* shell): shell_ (shell) {}

QTMApplicationMenuPresenter::~QTMApplicationMenuPresenter () {
  deactivate ();
}

void
QTMApplicationMenuPresenter::remember_input_widget (QWidget* widget) {
  if (!active_ || shell_ == nullptr || widget == nullptr) return;
  QWidget* work= shell_->activeWorkPaneWidget ();
  if (belongs_to (widget, work)) last_input_widget_= widget;
}

void
QTMApplicationMenuPresenter::capture_presented_context () {
  presented_context_= {};
  if (shell_ == nullptr) return;
  QTMCommandRegistry& registry= QTMCommandRegistry::instance ();
  presented_context_= registry.captureContext (
    shell_, last_input_widget_ != nullptr ? last_input_widget_.data ():
                                           QApplication::focusWidget ());
}

bool
QTMApplicationMenuPresenter::refresh_menu (int index) {
  if (index < 0 || index >= menus_.size ()) return false;
  menu_state& menu= menus_[index];
  if (menu.menu == nullptr) return false;
  QTMCommandRegistry& registry= QTMCommandRegistry::instance ();

  QVector<bool> command_visible (menu.entries.size (), false);
  for (int i= 0; i < menu.entries.size (); ++i) {
    menu_entry& entry= menu.entries[i];
    if (entry.action == nullptr) continue;
    if (entry.kind == QTMCommandMenuItem::Kind::Command) {
      QTMCommandState state=
        registry.state (entry.command_id, presented_context_);
      entry.action->setVisible (state.available);
      entry.action->setEnabled (state.available && state.enabled);
      entry.action->setCheckable (state.checkable);
      if (state.checkable) entry.action->setChecked (state.checked);
      command_visible[i]= state.available;
    }
    else if (entry.kind == QTMCommandMenuItem::Kind::Submenu) {
      bool available= refresh_menu (entry.submenu_index);
      entry.action->setVisible (available);
      entry.action->setEnabled (available);
      command_visible[i]= available;
    }
    else if (entry.kind == QTMCommandMenuItem::Kind::Provider)
      command_visible[i]= refresh_provider (entry, menu.menu);
  }

  bool visible_before= false;
  for (int i= 0; i < menu.entries.size (); ++i) {
    menu_entry& entry= menu.entries[i];
    if (entry.action == nullptr) continue;
    if (entry.kind != QTMCommandMenuItem::Kind::Separator) {
      if (command_visible[i]) visible_before= true;
      continue;
    }
    bool visible_after= false;
    for (int j= i + 1; j < menu.entries.size (); ++j) {
      if (menu.entries[j].kind == QTMCommandMenuItem::Kind::Separator) continue;
      if (command_visible[j]) {
        visible_after= true;
        break;
      }
    }
    entry.action->setVisible (visible_before && visible_after);
    if (entry.action->isVisible ()) visible_before= false;
  }
  for (bool visible: command_visible)
    if (visible) return true;
  return false;
}

void
QTMApplicationMenuPresenter::repopulate_provider (
  menu_entry& entry, QMenu* menu) {
  if (menu == nullptr || entry.action == nullptr || entry.provider_id.isEmpty ())
    return;
  QTMCommandRegistry& registry= QTMCommandRegistry::instance ();
  QVector<QTMCommandDynamicItem> values=
    registry.providerItems (entry.provider_id, presented_context_);

  QString signature;
  for (const QTMCommandDynamicItem& value: values) {
    signature += value.key;
    signature += QChar (0x1f);
    signature += value.group;
    signature += QChar (0x1f);
    signature += value.label;
    signature += QChar (0x1f);
    signature += value.help;
    signature += QChar (0x1f);
    signature += value.icon;
    signature += QChar (value.state.available ? '1' : '0');
    signature += QChar (value.state.enabled ? '1' : '0');
    signature += QChar (value.state.checkable ? '1' : '0');
    signature += QChar (value.state.checked ? '1' : '0');
    signature += QChar (0x1e);
  }
  if (signature == entry.dynamic_signature) return;
  entry.dynamic_signature= signature;

  for (const QPointer<QAction>& action: entry.dynamic_actions) {
    if (action == nullptr) continue;
    menu->removeAction (action);
    action->deleteLater ();
  }
  entry.dynamic_actions.clear ();
  for (const QPointer<QMenu>& dynamicMenu: entry.dynamic_menus)
    if (dynamicMenu != nullptr) dynamicMenu->deleteLater ();
  entry.dynamic_menus.clear ();

  QHash<QString, QMenu*> groups;
  for (const QTMCommandDynamicItem& value: values) {
    if (!value.state.available) continue;
    QMenu* target= menu;
    if (!value.group.isEmpty ()) {
      QMenu* group= groups.value (value.group, nullptr);
      if (group == nullptr) {
        group= new QMenu (value.group, menu);
        menu->insertAction (entry.action, group->menuAction ());
        groups.insert (value.group, group);
        entry.dynamic_menus.append (group);
      }
      target= group;
    }
    QIcon icon= value.icon.isEmpty () ? QIcon () : QIcon::fromTheme (value.icon);
    QAction* action= new QAction (icon, value.label, target);
    action->setToolTip (value.help);
    action->setStatusTip (value.help);
    action->setWhatsThis (value.help);
    action->setEnabled (value.state.enabled);
    action->setCheckable (value.state.checkable);
    if (value.state.checkable) action->setChecked (value.state.checked);
    const QString providerId= entry.provider_id;
    const QString key= value.key;
    QObject::connect (action, &QAction::triggered, action,
                      [this, providerId, key] {
      bool executed=
        QTMCommandRegistry::instance ().executeProviderItem (
          providerId, key, presented_context_);
      if (executed && root_refresh_timer_ != nullptr)
        root_refresh_timer_->start ();
    });
    if (target == menu) menu->insertAction (entry.action, action);
    else target->addAction (action);
    entry.dynamic_actions.append (action);
  }
}

bool
QTMApplicationMenuPresenter::refresh_provider (
  menu_entry& entry, QMenu* menu) {
  QTMCommandRegistry& registry= QTMCommandRegistry::instance ();
  QTMCommandState state=
    registry.providerState (entry.provider_id, presented_context_);
  if (!state.available) {
    for (const QPointer<QAction>& action: entry.dynamic_actions)
      if (action != nullptr) action->setVisible (false);
    for (const QPointer<QMenu>& dynamicMenu: entry.dynamic_menus)
      if (dynamicMenu != nullptr)
        dynamicMenu->menuAction ()->setVisible (false);
    entry.action->setVisible (false);
    return false;
  }
  repopulate_provider (entry, menu);
  bool any= false;
  for (const QPointer<QAction>& action: entry.dynamic_actions)
    if (action != nullptr) {
      action->setVisible (true);
      any= true;
    }
  for (const QPointer<QMenu>& dynamicMenu: entry.dynamic_menus)
    if (dynamicMenu != nullptr)
      dynamicMenu->menuAction ()->setVisible (true);
  entry.action->setVisible (false);
  entry.action->setEnabled (false);
  return any;
}

void
QTMApplicationMenuPresenter::bind_gui_refresh_if_available () {
  if (refresh_connection_ || shell_ == nullptr || the_gui == nullptr ||
      the_gui->gui_helper == nullptr)
    return;
  refresh_connection_= QObject::connect (
    the_gui->gui_helper, &QTMGuiHelper::refresh, shell_,
    [this] {
      if (root_refresh_timer_ != nullptr) root_refresh_timer_->start ();
    });
}

int
QTMApplicationMenuPresenter::build_menu (
  QMenu* menu, const QVector<QTMCommandMenuItem>& items, bool root_menu) {
  if (menu == nullptr) return -1;

  menu_state state;
  state.menu= menu;
  menus_.append (std::move (state));
  const int index= menus_.size () - 1;
  QTMCommandRegistry& registry= QTMCommandRegistry::instance ();

  for (const QTMCommandMenuItem& item: items) {
    menu_entry entry;
    entry.kind= item.kind;
    entry.command_id= item.commandId;
    entry.provider_id= item.providerId;
    if (item.kind == QTMCommandMenuItem::Kind::Separator)
      entry.action= menu->addSeparator ();
    else if (item.kind == QTMCommandMenuItem::Kind::Submenu) {
      QMenu* submenu= menu->addMenu (item.label);
      entry.action= submenu == nullptr ? nullptr : submenu->menuAction ();
      entry.submenu_index= build_menu (submenu, item.items, false);
    }
    else if (item.kind == QTMCommandMenuItem::Kind::Provider) {
      QAction* anchor= new QAction (menu);
      anchor->setVisible (false);
      anchor->setEnabled (false);
      menu->addAction (anchor);
      entry.action= anchor;
    }
    else {
      const QTMCommandDefinition* command= registry.command (item.commandId);
      if (command == nullptr) continue;
      QIcon icon= command->icon.isEmpty () ? QIcon ():
                  QIcon::fromTheme (command->icon);
      QAction* action=
        new QAction (icon, menu_action_text (*command), menu);
      action->setStatusTip (command->help);
      action->setWhatsThis (command->help);
      action->setProperty ("athena.shortcut", command->shortcut);
      const QString command_id= command->id;
      QObject::connect (action, &QAction::triggered, menu,
                        [this, command_id] {
        (void) execute (command_id);
      });
      menu->addAction (action);
      entry.action= action;
    }
    menus_[index].entries.append (std::move (entry));
  }

  QObject::connect (
    menu, &QMenu::aboutToShow, menu,
    [this, index, root_menu] {
      if (index < 0 || index >= menus_.size ()) return;
      if (root_menu) {
        bind_gui_refresh_if_available ();
        if (the_gui != nullptr && the_gui->gui_helper != nullptr)
          the_gui->gui_helper->aboutToShowMainMenu ();
        capture_presented_context ();
      }
      (void) refresh_menu (index);
    });
  if (root_menu)
    QObject::connect (
      menu, &QMenu::aboutToHide, menu, [this] {
        bind_gui_refresh_if_available ();
        if (the_gui != nullptr && the_gui->gui_helper != nullptr)
          the_gui->gui_helper->aboutToHideMainMenu ();
      });
  return index;
}

bool
QTMApplicationMenuPresenter::execute (const QString& command_id) {
  bool executed= QTMCommandRegistry::instance ().execute (
    command_id, presented_context_);
  if (executed && root_refresh_timer_ != nullptr)
    root_refresh_timer_->start ();
  return executed;
}

void
QTMApplicationMenuPresenter::refresh_root_visibility () {
  bind_gui_refresh_if_available ();
  capture_presented_context ();
  actor_editor_command_snapshot snapshot;
  actor_document_menu_snapshot document_snapshot;
  if (qt_actor_widget_rep* proxy=
        qtm_command_registry_detail::editor_proxy_for_context (
          presented_context_)) {
    snapshot= proxy->editor_command_state ();
    document_snapshot= proxy->document_menu_state ();
  }

  for (menu_state& menu: menus_) {
    if (menu.menu == nullptr || menu.root_id.isEmpty ()) continue;
    bool visible= true;
    const bool graphicsOnly=
      snapshot.valid () &&
      snapshot.has (ACTOR_EDITOR_COMMAND_STATE_GRAPHICS_MODE) &&
      !document_snapshot.commutative_diagram;
    if (menu.root_id == QStringLiteral ("manual"))
      visible= !graphicsOnly && snapshot.valid () &&
               snapshot.has (ACTOR_EDITOR_COMMAND_STATE_MANUAL_STYLE);
    else if (menu.root_id == QStringLiteral ("source"))
      visible= !graphicsOnly && snapshot.valid () &&
        (snapshot.has (ACTOR_EDITOR_COMMAND_STATE_SOURCE_MODE) ||
         get_user_preference ("source tool", "off") == "on");
    else if (menu.root_id == QStringLiteral ("dynamic"))
      visible= !graphicsOnly && snapshot.valid () &&
               snapshot.presentation_mode ();
    else if (menu.root_id == QStringLiteral ("format"))
      visible= !graphicsOnly;
    else if (menu.root_id == QStringLiteral ("automate"))
      visible= document_snapshot.ready && document_snapshot.automate_style;
    menu.menu->menuAction ()->setVisible (visible);
  }
#if ATHENA_PLATFORM_IPADOS
  athena_ios_menus_changed (shell_);
#endif
}

void QTMApplicationMenuPresenter::prepareNativeMenus () {
  capture_presented_context ();
  for (int i= 0; i < menus_.size (); ++i)
    if (!menus_[i].root_id.isEmpty ()) (void) refresh_menu (i);
}

bool
QTMApplicationMenuPresenter::activate () {
  if (active_) return true;
  if (shell_ == nullptr) return false;

  QTMCommandRegistry& registry= QTMCommandRegistry::instance ();
  if (!registry.initialize ()) return false;

  QMenuBar* bar= shell_->menuBar ();
  if (bar == nullptr) return false;

  active_= true;
  menus_.clear ();
  bar->clear ();
  bar->setNativeMenuBar (false);

  for (const QTMCommandMenuDefinition& definition: registry.menus ()) {
    QMenu* menu= bar->addMenu (definition.label);
    if (menu != nullptr) {
      menu->setProperty ("athena.menuId", definition.id);
      int index= build_menu (menu, definition.items, true);
      if (index >= 0 && index < menus_.size ())
        menus_[index].root_id= definition.id;
    }
  }

  root_refresh_timer_= new QTimer (shell_);
  root_refresh_timer_->setSingleShot (true);
  root_refresh_timer_->setInterval (0);
  QObject::connect (
    root_refresh_timer_, &QTimer::timeout, shell_,
    [this] { refresh_root_visibility (); });
  focus_connection_= QObject::connect (
    qApp, &QApplication::focusChanged, shell_,
    [this] (QWidget*, QWidget* now) {
      remember_input_widget (now);
      root_refresh_timer_->start ();
    });
  bind_gui_refresh_if_available ();
  remember_input_widget (QApplication::focusWidget ());
  refresh_root_visibility ();
#if ATHENA_PLATFORM_IPADOS
  bar->hide ();
#endif
  return true;
}

void
QTMApplicationMenuPresenter::deactivate () {
  if (!active_) return;
  active_= false;
  if (focus_connection_)
    QObject::disconnect (focus_connection_);
  focus_connection_= {};
  if (refresh_connection_)
    QObject::disconnect (refresh_connection_);
  refresh_connection_= {};
  if (root_refresh_timer_ != nullptr) {
    root_refresh_timer_->stop ();
    root_refresh_timer_->deleteLater ();
    root_refresh_timer_= nullptr;
  }
  menus_.clear ();
  presented_context_= {};
  last_input_widget_= nullptr;
  if (shell_ != nullptr && shell_->menuBar () != nullptr)
    shell_->menuBar ()->clear ();
}
