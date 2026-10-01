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
  QTMWidget* canvas, QToolBar* toolbar, QString definitionId):
  canvas_ (canvas), toolbar_ (toolbar), definitionId_ (std::move (definitionId)) {}

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
    menu, &QMenu::aboutToShow, menu, [this, submenuNode] {
      QTMCommandContext target= context ();
      refreshProviders (*submenuNode, target);
      (void) refreshNode (*submenuNode, target);
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
    if (action != nullptr) {
      menu->removeAction (action);
      action->deleteLater ();
    }
  item.dynamicActions.clear ();

  QVector<QTMCommandDynamicItem> values=
    QTMCommandRegistry::instance ().providerItems (item.providerId, target);
  for (const QTMCommandDynamicItem& value: values) {
    if (!value.state.available) continue;
    QIcon icon= presentation_icon (value.icon);
    QAction* action= new QAction (icon, value.label, menu);
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
    menu->insertAction (item.action, action);
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
QTMEditorToolbarPresenter::refreshNode (
  node& item, const QTMCommandContext& target) {
  if (item.action == nullptr) return false;
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

  if (item.kind == QTMCommandMenuItem::Kind::Provider)
    return !item.dynamicActions.empty ();

  bool any= false;
  for (const std::unique_ptr<node>& child: item.children)
    if (child && refreshNode (*child, target)) any= true;
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
  for (const std::unique_ptr<node>& root: roots_)
    if (root) (void) refreshNode (*root, target);
}
