/******************************************************************************
* MODULE     : QTMEditorToolbarPresenter.cpp
* DESCRIPTION: View-owned native editor toolbar presentation
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMEditorToolbarPresenter.hpp"

#include "QTMApplication.hpp"
#include "QTMDocumentIdentity.hpp"
#include "QTMMainTabWindow.hpp"
#include "QTMToolbar.hpp"
#include "QTMWidget.hpp"
#include "qt_utilities.hpp"
#include "qt_actor_widget.hpp"

#include <QAction>
#include <QColor>
#include <QIcon>
#include <QMenu>
#include <QPixmap>
#include <QTimer>
#include <QToolBar>

namespace {

QIcon
presentation_icon (const QString& value) {
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

} // namespace

QTMEditorToolbarPresenter::QTMEditorToolbarPresenter (
  QTMWidget* canvas, QToolBar* toolbar, QString definitionId,
  QToolBar* visibilityReference):
  canvas_ (canvas), toolbar_ (toolbar),
  visibilityReference_ (visibilityReference),
  definitionId_ (std::move (definitionId)) {}

QTMEditorToolbarPresenter::~QTMEditorToolbarPresenter () {
  deactivate ();
}

QTMCommandContext
QTMEditorToolbarPresenter::context () const {
  QTMCommandContext result;
  result.shell= QTMMainTabWindow::topTabWindow ();
  result.workPane= canvas_;
  result.lastDocument= qtm_document_identity (canvas_);
  return result;
}

QAction*
QTMEditorToolbarPresenter::makeCommandAction (
  const QString& commandId, QObject* parent) {
  const QTMCommandDefinition* command=
    QTMCommandRegistry::instance ().command (commandId);
  if (command == nullptr) return nullptr;
  QIcon icon= presentation_icon (command->icon);
  QAction* action= new QAction (icon, command->label, parent);
  action->setToolTip (command->help);
  action->setStatusTip (command->help);
  action->setWhatsThis (command->help);
  QObject::connect (action, &QAction::triggered, action,
                    [this, commandId] {
    QTMCommandContext target= context ();
    (void) QTMCommandRegistry::instance ().execute (commandId, target);
  });
  return action;
}

std::unique_ptr<QTMEditorToolbarPresenter::node>
QTMEditorToolbarPresenter::buildItem (
  const QTMCommandMenuItem& item, QMenu* menuParent) {
  auto result= std::make_unique<node> ();
  result->kind= item.kind;
  result->commandId= item.commandId;
  result->providerId= item.providerId;
  result->requiredFlags= item.requiredFlags;
  result->forbiddenFlags= item.forbiddenFlags;
  result->anyFlags= item.anyFlags;
  result->whenMainToolbarHidden= item.whenMainToolbarHidden;

  if (item.kind == QTMCommandMenuItem::Kind::Separator) {
    QAction* action= new QAction (menuParent != nullptr ?
                                  static_cast<QObject*> (menuParent):
                                  static_cast<QObject*> (toolbar_.data ()));
    action->setSeparator (true);
    result->action= action;
    return result;
  }

  if (item.kind == QTMCommandMenuItem::Kind::Command) {
    result->action= makeCommandAction (
      item.commandId, menuParent != nullptr ?
                      static_cast<QObject*> (menuParent):
                      static_cast<QObject*> (toolbar_.data ()));
    return result;
  }

  if (item.kind == QTMCommandMenuItem::Kind::Provider) {
    if (menuParent == nullptr || item.providerId.isEmpty ()) return {};
    QAction* anchor= new QAction (menuParent);
    anchor->setVisible (false);
    anchor->setEnabled (false);
    result->action= anchor;
    result->providerMenu= menuParent;
    return result;
  }

  QIcon icon= presentation_icon (item.icon);
  QAction* action= new QAction (icon, item.label, toolbar_);
  QMenu* menu= new QMenu (item.label, toolbar_);
  action->setMenu (menu);
  result->action= action;
  for (const QTMCommandMenuItem& child: item.items) {
    std::unique_ptr<node> built= buildItem (child, menu);
    if (!built || built->action == nullptr) continue;
    menu->addAction (built->action);
    result->children.push_back (std::move (built));
  }
  node* submenuNode= result.get ();
  QObject::connect (
    menu, &QMenu::aboutToShow, menu, [this, submenuNode, menu] {
      QTMCommandContext target= context ();
      refreshProviders (*submenuNode, target);
      (void) refreshNode (*submenuNode, target);
      QPointer<QMenu> menuRef= submenuNode->action == nullptr ?
        nullptr : submenuNode->action->menu ();
      for (int delay: {120, 350})
        QTimer::singleShot (delay, menu, [this, submenuNode, menuRef] {
          if (menuRef == nullptr || !menuRef->isVisible ()) return;
          QTMCommandContext refreshed= context ();
          refreshProviders (*submenuNode, refreshed);
          (void) refreshNode (*submenuNode, refreshed);
        });
    });
  return result;
}

void
QTMEditorToolbarPresenter::repopulateProvider (
  node& item, const QTMCommandContext& target) {
  if (item.kind != QTMCommandMenuItem::Kind::Provider ||
      item.providerMenu == nullptr || item.action == nullptr)
    return;

  QMenu* menu= item.providerMenu.data ();
  for (const QPointer<QAction>& action: item.dynamicActions)
    if (action != nullptr) action->deleteLater ();
  item.dynamicActions.clear ();
  for (const QPointer<QMenu>& dynamicMenu: item.dynamicMenus)
    if (dynamicMenu != nullptr) dynamicMenu->deleteLater ();
  item.dynamicMenus.clear ();

  QVector<QTMCommandDynamicItem> values=
    QTMCommandRegistry::instance ().providerItems (item.providerId, target);
  QHash<QString, QMenu*> groups;
  for (const QTMCommandDynamicItem& value: values) {
    if (!value.state.available) continue;
    QMenu* targetMenu= menu;
    if (!value.group.isEmpty ()) {
      QMenu* groupMenu= groups.value (value.group, nullptr);
      if (groupMenu == nullptr) {
        groupMenu= new QMenu (value.group, menu);
        menu->insertAction (item.action, groupMenu->menuAction ());
        groups.insert (value.group, groupMenu);
        item.dynamicMenus.push_back (groupMenu);
      }
      targetMenu= groupMenu;
    }
    QIcon icon= presentation_icon (value.icon);
    QAction* action= new QAction (icon, value.label, targetMenu);
    action->setToolTip (value.help);
    action->setStatusTip (value.help);
    action->setWhatsThis (value.help);
    action->setEnabled (value.state.enabled);
    action->setCheckable (value.state.checkable);
    if (value.state.checkable) action->setChecked (value.state.checked);
    const QString providerId= item.providerId;
    const QString key= value.key;
    QObject::connect (action, &QAction::triggered, action,
                      [this, providerId, key] {
      QTMCommandContext target= context ();
      (void) QTMCommandRegistry::instance ().executeProviderItem (
        providerId, key, target);
      refresh ();
    });
    if (targetMenu == menu) menu->insertAction (item.action, action);
    else targetMenu->addAction (action);
    item.dynamicActions.push_back (action);
  }
}

void
QTMEditorToolbarPresenter::refreshProviders (
  node& item, const QTMCommandContext& target) {
  if (item.kind == QTMCommandMenuItem::Kind::Provider) {
    repopulateProvider (item, target);
    return;
  }
  for (const std::unique_ptr<node>& child: item.children)
    if (child) refreshProviders (*child, target);
}

bool
QTMEditorToolbarPresenter::presentationConditionSatisfied (
  const node& item, const QTMCommandContext&) const {
  if (item.whenMainToolbarHidden &&
      (visibilityReference_ == nullptr || visibilityReference_->isVisible ()))
    return false;

  if (item.requiredFlags == 0 &&
      item.forbiddenFlags == 0 &&
      item.anyFlags == 0)
    return true;
  if (canvas_ == nullptr || canvas_->tm_widget () == nullptr) return false;
  qt_actor_widget_rep* proxy=
    dynamic_cast<qt_actor_widget_rep*> (canvas_->tm_widget ());
  if (proxy == nullptr) return false;
  actor_editor_command_snapshot snapshot= proxy->editor_command_state ();
  if (!snapshot.valid ()) return false;
  return (snapshot.flags & item.requiredFlags) == item.requiredFlags &&
         (snapshot.flags & item.forbiddenFlags) == 0 &&
         (item.anyFlags == 0 || (snapshot.flags & item.anyFlags) != 0);
}

bool
QTMEditorToolbarPresenter::refreshNode (
  node& item, const QTMCommandContext& target) {
  if (item.action == nullptr) return false;
  if (!presentationConditionSatisfied (item, target)) {
    item.action->setVisible (false);
    item.action->setEnabled (false);
    return false;
  }
  if (item.kind == QTMCommandMenuItem::Kind::Separator) return false;

  if (item.kind == QTMCommandMenuItem::Kind::Command) {
    QTMCommandState state=
      QTMCommandRegistry::instance ().state (item.commandId, target);
    item.action->setVisible (state.available);
    item.action->setEnabled (state.available && state.enabled);
    item.action->setCheckable (state.checkable);
    if (state.checkable) item.action->setChecked (state.checked);
    return state.available;
  }

  if (item.kind == QTMCommandMenuItem::Kind::Provider) {
    QTMCommandState state=
      QTMCommandRegistry::instance ().providerState (item.providerId, target);
    item.action->setVisible (state.available);
    item.action->setEnabled (state.available && state.enabled);
    return state.available;
  }

  bool any= false;
  std::vector<bool> visible (item.children.size (), false);
  for (std::size_t i= 0; i < item.children.size (); ++i) {
    const std::unique_ptr<node>& child= item.children[i];
    if (child && child->kind != QTMCommandMenuItem::Kind::Separator) {
      visible[i]= refreshNode (*child, target);
      if (visible[i]) any= true;
    }
  }
  bool before= false;
  for (std::size_t i= 0; i < item.children.size (); ++i) {
    node* child= item.children[i].get ();
    if (child == nullptr || child->action == nullptr) continue;
    if (child->kind != QTMCommandMenuItem::Kind::Separator) {
      if (visible[i]) before= true;
      continue;
    }
    bool after= false;
    for (std::size_t j= i + 1; j < item.children.size (); ++j) {
      if (item.children[j] != nullptr &&
          item.children[j]->kind != QTMCommandMenuItem::Kind::Separator &&
          visible[j]) {
        after= true;
        break;
      }
    }
    child->action->setVisible (before && after);
    if (child->action->isVisible ()) before= false;
  }
  item.action->setVisible (any);
  item.action->setEnabled (any);
  return any;
}

void
QTMEditorToolbarPresenter::addToolbarAction (QAction* action) {
  if (toolbar_ == nullptr || action == nullptr) return;
  if (QTMToolbar* custom= qobject_cast<QTMToolbar*> (toolbar_.data ()))
    custom->addAction (action);
  else
    toolbar_->addAction (action);
}

void
QTMEditorToolbarPresenter::clearToolbar () {
  if (toolbar_ == nullptr) return;
  if (QTMToolbar* custom= qobject_cast<QTMToolbar*> (toolbar_.data ()))
    custom->clear ();
  else
    toolbar_->clear ();
}

bool
QTMEditorToolbarPresenter::activate () {
  if (active_) return true;
  if (canvas_ == nullptr || toolbar_ == nullptr) return false;
  QTMCommandRegistry& registry= QTMCommandRegistry::instance ();
  if (!registry.initialize ()) return false;
  const QTMCommandToolbarDefinition* definition=
    registry.toolbar (definitionId_);
  if (definition == nullptr) return false;

  clearToolbar ();
  roots_.clear ();
  for (const QTMCommandMenuItem& item: definition->items) {
    std::unique_ptr<node> built= buildItem (item, nullptr);
    if (!built || built->action == nullptr) continue;
    addToolbarAction (built->action);
    roots_.push_back (std::move (built));
  }

  refreshTimer_= new QTimer (toolbar_);
  refreshTimer_->setInterval (150);
  QObject::connect (refreshTimer_, &QTimer::timeout, toolbar_,
                    [this] { refresh (); });
  refreshTimer_->start ();
  active_= true;
  refresh ();
  return true;
}

void
QTMEditorToolbarPresenter::deactivate () {
  if (!active_) return;
  active_= false;
  if (refreshTimer_ != nullptr) {
    refreshTimer_->stop ();
    refreshTimer_->deleteLater ();
  }
  refreshTimer_= nullptr;
  roots_.clear ();
  clearToolbar ();
}

void
QTMEditorToolbarPresenter::refresh () {
  if (!active_ || canvas_ == nullptr) return;
  QTMCommandContext target= context ();
  std::vector<bool> visible (roots_.size (), false);
  for (std::size_t i= 0; i < roots_.size (); ++i)
    if (roots_[i] != nullptr &&
        roots_[i]->kind != QTMCommandMenuItem::Kind::Separator)
      visible[i]= refreshNode (*roots_[i], target);
  bool before= false;
  for (std::size_t i= 0; i < roots_.size (); ++i) {
    node* root= roots_[i].get ();
    if (root == nullptr || root->action == nullptr) continue;
    if (root->kind != QTMCommandMenuItem::Kind::Separator) {
      if (visible[i]) before= true;
      continue;
    }
    bool after= false;
    for (std::size_t j= i + 1; j < roots_.size (); ++j) {
      if (roots_[j] != nullptr &&
          roots_[j]->kind != QTMCommandMenuItem::Kind::Separator &&
          visible[j]) {
        after= true;
        break;
      }
    }
    root->action->setVisible (before && after);
    if (root->action->isVisible ()) before= false;
  }
}
