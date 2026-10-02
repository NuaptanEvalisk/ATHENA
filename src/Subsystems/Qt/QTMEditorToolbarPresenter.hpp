/******************************************************************************
* MODULE     : QTMEditorToolbarPresenter.hpp
* DESCRIPTION: View-owned native editor toolbar presentation
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMEDITORTOOLBARPRESENTER_HPP
#define QTMEDITORTOOLBARPRESENTER_HPP

#include "QTMCommandRegistry.hpp"

#include <QPointer>
#include <QString>
#include <memory>
#include <vector>

class QAction;
class QMenu;
class QTimer;
class QTMWidget;
class QToolBar;

class QTMEditorToolbarPresenter {
public:
  QTMEditorToolbarPresenter (QTMWidget* canvas, QToolBar* toolbar,
                             QString definitionId,
                             QToolBar* visibilityReference= nullptr);
  ~QTMEditorToolbarPresenter ();

  QTMEditorToolbarPresenter (const QTMEditorToolbarPresenter&)= delete;
  QTMEditorToolbarPresenter& operator = (
    const QTMEditorToolbarPresenter&)= delete;

  bool activate ();
  void deactivate ();
  bool active () const noexcept { return active_; }
  void refresh ();

private:
  struct node {
    QPointer<QAction> action;
    QString commandId;
    QString providerId;
    QTMCommandMenuItem::Kind kind= QTMCommandMenuItem::Kind::Command;
    std::uint32_t requiredFlags= 0;
    std::uint32_t forbiddenFlags= 0;
    std::uint32_t anyFlags= 0;
    std::uint32_t focusRequiredFlags= 0;
    std::uint32_t focusForbiddenFlags= 0;
    std::uint32_t focusAnyFlags= 0;
    bool whenMainToolbarHidden= false;
    QPointer<QMenu> providerMenu;
    std::vector<QPointer<QAction>> dynamicActions;
    std::vector<QPointer<QMenu>> dynamicMenus;
    QString dynamicSignature;
    std::vector<std::unique_ptr<node>> children;
  };

  QPointer<QTMWidget> canvas_;
  QPointer<QToolBar> toolbar_;
  QPointer<QToolBar> visibilityReference_;
  QString definitionId_;
  std::vector<std::unique_ptr<node>> roots_;
  QPointer<QTimer> refreshTimer_;
  bool active_= false;

  QTMCommandContext context () const;
  QAction* makeCommandAction (const QString& commandId, QObject* parent);
  std::unique_ptr<node> buildItem (
    const QTMCommandMenuItem& item, QMenu* menuParent);
  void refreshProviders (node& item, const QTMCommandContext& context);
  void repopulateProvider (node& item, const QTMCommandContext& context);
  bool presentationConditionSatisfied (
    const node& item, const QTMCommandContext& context) const;
  bool refreshNode (node& item, const QTMCommandContext& context);
  void addToolbarAction (QAction* action);
  void clearToolbar ();
};

#endif // QTMEDITORTOOLBARPRESENTER_HPP
