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
};

struct QTMCommandDefinition {
  QString id;
  QString label;
  QString icon;
  QString category;
  QString help;
  QKeySequence shortcut;
  QTMCommandScope scope= QTMCommandScope::Application;
};

struct QTMCommandMenuItem {
  enum class Kind {
    Command,
    Separator,
    Submenu
  };

  Kind kind= Kind::Command;
  QString commandId;
  QString submenuId;
  QString label;
  QVector<QTMCommandMenuItem> items;
};

struct QTMCommandMenuDefinition {
  QString id;
  QString label;
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
  const QTMCommandDefinition* command (const QString& id) const;
  const QTMCommandDefinition* commandForShortcut (
    const QKeySequence& shortcut) const;

  QTMCommandContext captureContext (QTMMainTabWindow* shell,
                                    QWidget* inputWidget= nullptr) const;
  QTMCommandState state (const QString& id,
                         const QTMCommandContext& context) const;
  bool execute (const QString& id, const QTMCommandContext& context) const;

private:
  struct Behavior {
    QTMCommandScope scope= QTMCommandScope::Application;
    std::function<QTMCommandState(const QTMCommandContext&)> state;
    std::function<bool(const QTMCommandContext&)> execute;
  };

  QTMCommandRegistry ()= default;
  void registerBuiltins ();
  void registerBehavior (
    const QString& id, QTMCommandScope scope,
    std::function<bool(const QTMCommandContext&)> execute,
    std::function<QTMCommandState(const QTMCommandContext&)> state= {});
  bool loadPresentation ();
  bool failPresentation (const QString& message);

  bool initialized_= false;
  QHash<QString, Behavior> behaviors_;
  QHash<QString, int> commandIndex_;
  QVector<QTMCommandDefinition> commands_;
  QVector<QTMCommandMenuDefinition> menus_;
};

#endif // QTMCOMMANDREGISTRY_HPP
