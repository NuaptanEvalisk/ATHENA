/******************************************************************************
* MODULE     : QTMCommandRegistry.hpp
* DESCRIPTION: Native application command registry and work-context routing
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMCOMMANDREGISTRY_HPP
#define QTMCOMMANDREGISTRY_HPP

#include "QTMDocumentIdentity.hpp"
#include "QTMMainTabWindow.hpp"

#include <QHash>
#include <QKeySequence>
#include <QPointer>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

enum class QTMCommandScope {
  Application,
  Workspace,
  Pane,
  Editor
};

struct QTMCommandState {
  bool available= false;
  bool enabled= false;
  bool checkable= false;
  bool checked= false;
};

struct QTMCommandContext {
  QPointer<QTMMainTabWindow> shell;
  QPointer<QWidget> workPane;
  QPointer<QWidget> inputWidget;
  QTMDocumentIdentity lastDocument;
};

struct QTMCommandDefinition {
  QString id;
  QString label;
  QString icon;
  QString category;
  QString help;
  QKeySequence shortcut;
  QTMCommandScope scope= QTMCommandScope::Application;
  bool showInPalette= true;
};

struct QTMCommandMenuItem {
  enum class Kind {
    Command,
    Separator,
    Submenu,
    Provider
  };

  Kind kind= Kind::Command;
  QString commandId;
  QString submenuId;
  QString providerId;
  QString label;
  QString icon;
  QVector<QTMCommandMenuItem> items;
};

struct QTMCommandDynamicItem {
  QString key;
  QString group;
  QString label;
  QString help;
  QString icon;
  QTMCommandState state;
};

struct QTMCommandMenuDefinition {
  QString id;
  QString label;
  QVector<QTMCommandMenuItem> items;
};

struct QTMCommandToolbarDefinition {
  QString id;
  QVector<QTMCommandMenuItem> items;
};

class QTMCommandProvider {
public:
  virtual ~QTMCommandProvider ()= default;
  virtual bool qtmSupportsCommand (const QString& commandId) const= 0;
  virtual QTMCommandState qtmCommandState (const QString& commandId) const= 0;
  virtual bool qtmInvokeCommand (const QString& commandId)= 0;
};

class QTMCommandRegistry {
public:
  static QTMCommandRegistry& instance ();

  bool initialize ();
  bool initialized () const { return initialized_; }

  const QVector<QTMCommandDefinition>& commands () const { return commands_; }
  const QVector<QTMCommandMenuDefinition>& menus () const { return menus_; }
  const QVector<QTMCommandToolbarDefinition>& toolbars () const {
    return toolbars_;
  }
  const QTMCommandDefinition* command (const QString& id) const;
  const QTMCommandToolbarDefinition* toolbar (const QString& id) const;
  const QTMCommandDefinition* commandForShortcut (
    const QKeySequence& shortcut) const;

  QTMCommandContext captureContext (QTMMainTabWindow* shell,
                                    QWidget* inputWidget= nullptr) const;
  QTMCommandState state (const QString& id,
                         const QTMCommandContext& context) const;
  bool execute (const QString& id, const QTMCommandContext& context) const;
  QVector<QTMCommandDynamicItem> providerItems (
    const QString& providerId, const QTMCommandContext& context) const;
  QTMCommandState providerState (
    const QString& providerId, const QTMCommandContext& context) const;
  bool executeProviderItem (const QString& providerId, const QString& key,
                            const QTMCommandContext& context) const;

private:
  struct Behavior {
    QTMCommandScope scope= QTMCommandScope::Application;
    std::function<QTMCommandState(const QTMCommandContext&)> state;
    std::function<bool(const QTMCommandContext&)> execute;
  };
  struct ProviderBehavior {
    QTMCommandScope scope= QTMCommandScope::Application;
    std::function<QTMCommandState(const QTMCommandContext&)> state;
    std::function<QVector<QTMCommandDynamicItem>(
      const QTMCommandContext&)> items;
    std::function<bool(const QString&, const QTMCommandContext&)> execute;
  };

  QTMCommandRegistry ()= default;
  void registerBuiltins ();
  void registerBehavior (
    const QString& id, QTMCommandScope scope,
    std::function<bool(const QTMCommandContext&)> execute,
    std::function<QTMCommandState(const QTMCommandContext&)> state= {});
  void registerProvider (
    const QString& id, QTMCommandScope scope,
    std::function<QVector<QTMCommandDynamicItem>(
      const QTMCommandContext&)> items,
    std::function<bool(const QString&, const QTMCommandContext&)> execute,
    std::function<QTMCommandState(const QTMCommandContext&)> state= {});
  bool loadPresentation ();
  bool failPresentation (const QString& message);

  bool initialized_= false;
  QHash<QString, Behavior> behaviors_;
  QHash<QString, ProviderBehavior> providers_;
  QHash<QString, int> commandIndex_;
  QVector<QTMCommandDefinition> commands_;
  QVector<QTMCommandMenuDefinition> menus_;
  QVector<QTMCommandToolbarDefinition> toolbars_;
};

#endif // QTMCOMMANDREGISTRY_HPP
