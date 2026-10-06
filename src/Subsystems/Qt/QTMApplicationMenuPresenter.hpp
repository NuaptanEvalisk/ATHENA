/******************************************************************************
* MODULE     : QTMApplicationMenuPresenter.hpp
* DESCRIPTION: Registry-backed application-shell menubar presentation
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMAPPLICATIONMENUPRESENTER_HPP
#define QTMAPPLICATIONMENUPRESENTER_HPP

#include "QTMCommandRegistry.hpp"

#include <QMetaObject>
#include <QPointer>
#include <QVector>

class QAction;
class QMenu;
class QTimer;
class QTMMainTabWindow;

class QTMApplicationMenuPresenter {
public:
  explicit QTMApplicationMenuPresenter (QTMMainTabWindow* shell);
  ~QTMApplicationMenuPresenter ();

  QTMApplicationMenuPresenter (const QTMApplicationMenuPresenter&)= delete;
  QTMApplicationMenuPresenter& operator = (
    const QTMApplicationMenuPresenter&)= delete;

  bool activate ();
  void deactivate ();
  bool active () const noexcept { return active_; }
  void prepareNativeMenus ();

private:
  struct menu_entry {
    QPointer<QAction> action;
    QString command_id;
    QString provider_id;
    QTMCommandMenuItem::Kind kind= QTMCommandMenuItem::Kind::Command;
    int submenu_index= -1;
    QVector<QPointer<QAction>> dynamic_actions;
    QVector<QPointer<QMenu>> dynamic_menus;
    QString dynamic_signature;
  };

  struct menu_state {
    QPointer<QMenu> menu;
    QVector<menu_entry> entries;
    QString root_id;
  };

  QPointer<QTMMainTabWindow> shell_;
  QVector<menu_state> menus_;
  QTMCommandContext presented_context_;
  QPointer<QWidget> last_input_widget_;
  QPointer<QTimer> root_refresh_timer_;
  QMetaObject::Connection focus_connection_;
  QMetaObject::Connection refresh_connection_;
  bool active_= false;

  void remember_input_widget (QWidget* widget);
  void capture_presented_context ();
  void bind_gui_refresh_if_available ();
  int build_menu (QMenu* menu, const QVector<QTMCommandMenuItem>& items,
                   bool root_menu);
  void repopulate_provider (menu_entry& entry, QMenu* menu);
  bool refresh_provider (menu_entry& entry, QMenu* menu);
  bool refresh_menu (int index);
  void refresh_root_visibility ();
  bool execute (const QString& command_id);
};

#endif // QTMAPPLICATIONMENUPRESENTER_HPP
