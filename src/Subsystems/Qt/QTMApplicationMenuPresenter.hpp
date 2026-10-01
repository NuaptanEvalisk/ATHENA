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

private:
  struct menu_entry {
    QPointer<QAction> action;
    QString command_id;
    bool separator= false;
  };

  struct menu_state {
    QPointer<QMenu> menu;
    QVector<menu_entry> entries;
  };

  QPointer<QTMMainTabWindow> shell_;
  QVector<menu_state> menus_;
  QTMCommandContext presented_context_;
  QPointer<QWidget> last_input_widget_;
  QMetaObject::Connection focus_connection_;
  bool active_= false;

  void remember_input_widget (QWidget* widget);
  void capture_presented_context ();
  void refresh_menu (menu_state& menu);
  bool execute (const QString& command_id);
};

#endif // QTMAPPLICATIONMENUPRESENTER_HPP
