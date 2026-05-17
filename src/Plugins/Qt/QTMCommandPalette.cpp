/******************************************************************************
* MODULE     : QTMCommandPalette.cpp
* DESCRIPTION: Qt command palette backed by the live menubar
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMCommandPalette.hpp"
#include "QTMMainTabWindow.hpp"
#include "QTMMenuHelper.hpp"

#include <KCommandBar>

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QList>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QToolBar>
#include <QToolButton>
#include <QVector>
#include <QWidgetAction>

static QString
clean_action_text (const QString& raw) {
  QString text= raw;
  int tab= text.indexOf ('\t');
  if (tab >= 0) text= text.left (tab);

  QString out;
  out.reserve (text.size ());
  for (int i=0; i < text.size (); ++i) {
    if (text[i] == '&') {
      if (i + 1 < text.size () && text[i + 1] == '&') {
        out.append ('&');
        ++i;
      }
      continue;
    }
    out.append (text[i]);
  }
  return out.trimmed ();
}

static void
force_lazy_menu_tree (QMenu* menu, QSet<QMenu*>& seen) {
  if (menu == nullptr || seen.contains (menu)) return;
  seen.insert (menu);

  if (QTMLazyMenu* lazy= qobject_cast<QTMLazyMenu*> (menu))
    lazy->force ();

  QList<QAction*> actions= menu->actions ();
  for (QAction* action : actions)
    if (action != nullptr && action->menu () != nullptr)
      force_lazy_menu_tree (action->menu (), seen);
}

static QAction*
make_palette_action (KCommandBar* palette, QAction* original,
                     const QString& label, const QString& fullPath) {
  QAction* copy= new QAction (original->icon (), label, palette);
  copy->setEnabled (original->isEnabled ());
  copy->setCheckable (original->isCheckable ());
  copy->setChecked (original->isChecked ());
  copy->setShortcut (original->shortcut ());
  copy->setToolTip (fullPath);
  copy->setStatusTip (original->statusTip ().isEmpty () ?
                      fullPath : original->statusTip ());
  copy->setWhatsThis (original->whatsThis ());

  QPointer<QAction> target= original;
  QObject::connect (copy, &QAction::triggered, copy, [target] () {
    if (target != nullptr) target->trigger ();
  });
  return copy;
}

static void
collect_menu_actions (QMenu* menu, KCommandBar* palette,
                      const QString& groupName, const QStringList& parents,
                      QList<QAction*>& out) {
  if (menu == nullptr) return;

  QList<QAction*> actions= menu->actions ();
  for (QAction* action : actions) {
    if (action == nullptr || action->isSeparator ()) continue;

    QString name= clean_action_text (action->text ());
    if (name.isEmpty () || name == "native menubar trick") continue;

    QMenu* submenu= action->menu ();
    if (submenu != nullptr) {
      QStringList nextParents= parents;
      nextParents << name;
      collect_menu_actions (submenu, palette, groupName, nextParents, out);
      continue;
    }

    if (qobject_cast<QWidgetAction*> (action) != nullptr) continue;

    QStringList commandPath= parents;
    commandPath << name;
    QString label= commandPath.join (" -> ");
    QString fullPath= groupName;
    if (!label.isEmpty ()) fullPath += " -> " + label;
    out.append (make_palette_action (palette, action, label, fullPath));
  }
}

static void
append_action_once (QList<QAction*>& result, QSet<QMenu*>& menus,
                    QAction* action) {
  if (action == nullptr || action->menu () == nullptr) return;
  if (menus.contains (action->menu ())) return;
  menus.insert (action->menu ());
  result.append (action);
}

static QString
menu_group_name (QAction* action) {
  if (action == nullptr) return QString ();

  QString name= clean_action_text (action->text ());
  if (name.isEmpty () && action->menu () != nullptr)
    name= clean_action_text (action->menu ()->title ());
  if (name.isEmpty ()) {
    for (QWidget* widget : action->associatedWidgets ()) {
      if (QToolButton* button= qobject_cast<QToolButton*> (widget)) {
        name= clean_action_text (button->text ());
        if (!name.isEmpty ()) break;
      }
    }
  }
  return name;
}

static QList<QAction*>
top_level_menu_actions (QMainWindow* win) {
  QList<QAction*> result;
  QSet<QMenu*> menus;
  if (win == nullptr) return result;

  if (win->menuBar () != nullptr) {
    for (QAction* action : win->menuBar ()->actions ())
      append_action_once (result, menus, action);
  }

  for (QToolBar* toolbar : win->findChildren<QToolBar*> ("menuToolBar")) {
    for (QAction* action : toolbar->actions ())
      append_action_once (result, menus, action);
    for (QToolButton* button : toolbar->findChildren<QToolButton*> ())
      append_action_once (result, menus, button->defaultAction ());
  }

  return result;
}

static void
append_window_once (QList<QMainWindow*>& windows, QSet<QMainWindow*>& seen,
                    QMainWindow* win) {
  if (win == nullptr || seen.contains (win)) return;
  seen.insert (win);
  windows.append (win);
}

static void
append_parent_main_windows (QList<QMainWindow*>& windows,
                            QSet<QMainWindow*>& seen, QWidget* widget) {
  while (widget != nullptr) {
    append_window_once (windows, seen, qobject_cast<QMainWindow*> (widget));
    widget= widget->parentWidget ();
  }
}

static QList<QMainWindow*>
candidate_menu_windows (QTMMainTabWindow* top) {
  QList<QMainWindow*> windows;
  QSet<QMainWindow*> seen;

  append_parent_main_windows (windows, seen, QApplication::focusWidget ());
  append_parent_main_windows (windows, seen, QApplication::activeWindow ());
  append_window_once (windows, seen, top);
  if (top != nullptr) {
    for (QMainWindow* win : top->findChildren<QMainWindow*> ())
      append_window_once (windows, seen, win);
  }
  for (QWidget* widget : QApplication::allWidgets ())
    append_window_once (windows, seen, qobject_cast<QMainWindow*> (widget));

  return windows;
}

void
command_palette_show () {
  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr) return;

  QList<QAction*> topActions;
  for (QMainWindow* candidate : candidate_menu_windows (win)) {
    topActions= top_level_menu_actions (candidate);
    if (!topActions.isEmpty ()) break;
  }

  QSet<QMenu*> seen;
  for (QAction* action : topActions)
    if (action != nullptr && action->menu () != nullptr)
      force_lazy_menu_tree (action->menu (), seen);

  KCommandBar palette (win);
  QVector<KCommandBar::ActionGroup> groups;
  for (QAction* action : topActions) {
    if (action == nullptr || action->menu () == nullptr) continue;

    QString groupName= menu_group_name (action);
    if (groupName.isEmpty ()) continue;

    QList<QAction*> entries;
    collect_menu_actions (action->menu (), &palette, groupName,
                          QStringList (), entries);
    if (!entries.isEmpty ()) {
      KCommandBar::ActionGroup group;
      group.name= groupName;
      group.actions= entries;
      groups.append (group);
    }
  }

  palette.setActions (groups);
  palette.exec ();
}
