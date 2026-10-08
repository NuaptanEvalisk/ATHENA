/******************************************************************************
* MODULE     : QTMCommandPalette.cpp
* DESCRIPTION: Qt command palette backed by the native command registry
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMCommandPalette.hpp"
#include "QTMApplication.hpp"
#include "QTMCommandRegistry.hpp"
#include "QTMMainTabWindow.hpp"

#ifdef USE_KF6
#include <KCommandBar>
#endif

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QHash>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QString>
#include <QVariant>
#include <QVBoxLayout>
#include <QVector>

namespace {

struct PaletteGroup {
  QString name;
  QList<QAction*> actions;
};

QAction*
make_palette_action (QObject* parent, const QTMCommandDefinition& definition,
                     const QTMCommandContext& context) {
  QTMCommandRegistry& registry= QTMCommandRegistry::instance ();
  QTMCommandState state= registry.state (definition.id, context);
  QIcon icon= tmapp ()->icon_manager ().getPresentationIcon (definition.icon);
  QAction* action= new QAction (icon, definition.label, parent);
  action->setEnabled (state.available && state.enabled);
  action->setCheckable (state.checkable);
  action->setChecked (state.checkable && state.checked);
  action->setShortcut (definition.shortcut);
  QString path= definition.category + " -> " + definition.label;
  action->setToolTip (path);
  action->setStatusTip (definition.help.isEmpty () ? path: definition.help);
  action->setWhatsThis (definition.help);

  QString id= definition.id;
  QObject::connect (action, &QAction::triggered, action,
                    [id, context] () {
    (void) QTMCommandRegistry::instance ().execute (id, context);
  });
  return action;
}

QVector<PaletteGroup>
build_palette_groups (QObject* parent, const QTMCommandContext& context) {
  QVector<PaletteGroup> groups;
  QHash<QString, int> groupIndex;
  const QVector<QTMCommandDefinition>& commands=
    QTMCommandRegistry::instance ().commands ();
  for (const QTMCommandDefinition& definition: commands) {
    if (!definition.showInPalette) continue;
    int index= groupIndex.value (definition.category, -1);
    if (index < 0) {
      index= groups.size ();
      PaletteGroup group;
      group.name= definition.category;
      groups.append (std::move (group));
      groupIndex.insert (definition.category, index);
    }
    groups[index].actions.append (
      make_palette_action (parent, definition, context));
  }
  return groups;
}

#ifndef USE_KF6
QAction*
action_for_item (QListWidgetItem* item) {
  if (item == nullptr) return nullptr;
  quintptr ptr= item->data (Qt::UserRole).value<quintptr> ();
  return reinterpret_cast<QAction*> (ptr);
}

void
select_first_visible (QListWidget* list) {
  if (list == nullptr) return;
  for (int i= 0; i < list->count (); ++i) {
    QListWidgetItem* item= list->item (i);
    if (item != nullptr && !item->isHidden () &&
        (item->flags () & Qt::ItemIsEnabled)) {
      list->setCurrentItem (item);
      return;
    }
  }
  list->setCurrentItem (nullptr);
}

void
show_qt_command_palette (QWidget* host,
                         const QTMCommandContext& context) {
  QDialog* palette= new QDialog (host);
  palette->setAttribute (Qt::WA_DeleteOnClose);
  palette->setWindowTitle (QObject::tr ("Command palette"));

  QVBoxLayout* layout= new QVBoxLayout (palette);
  QLineEdit* filter= new QLineEdit (palette);
  QListWidget* list= new QListWidget (palette);
  filter->setPlaceholderText (QObject::tr ("Search commands"));
  list->setSelectionMode (QAbstractItemView::SingleSelection);
  layout->addWidget (filter);
  layout->addWidget (list);

  QVector<PaletteGroup> groups= build_palette_groups (palette, context);
  for (const PaletteGroup& group: groups)
    for (QAction* action: group.actions) {
      QListWidgetItem* item=
        new QListWidgetItem (action->icon (), action->text (), list);
      QString tooltip= group.name + " -> " + action->text ();
      if (!action->statusTip ().isEmpty ())
        tooltip += "\n" + action->statusTip ();
      item->setToolTip (tooltip);
      item->setData (Qt::UserRole,
                     QVariant::fromValue<quintptr> (
                       reinterpret_cast<quintptr> (action)));
      if (!action->isEnabled ())
        item->setFlags (item->flags () & ~Qt::ItemIsEnabled);
    }

  QObject::connect (filter, &QLineEdit::textChanged, list,
                    [list] (const QString& text) {
    for (int i= 0; i < list->count (); ++i) {
      QListWidgetItem* item= list->item (i);
      if (item == nullptr) continue;
      bool matched= text.isEmpty () ||
        item->text ().contains (text, Qt::CaseInsensitive) ||
        item->toolTip ().contains (text, Qt::CaseInsensitive);
      item->setHidden (!matched);
    }
    select_first_visible (list);
  });

  auto trigger_current= [palette, list] () {
    QAction* action= action_for_item (list->currentItem ());
    if (action != nullptr && action->isEnabled ()) {
      action->trigger ();
      palette->close ();
    }
  };
  QObject::connect (list, &QListWidget::itemActivated, palette,
                    [palette] (QListWidgetItem* item) {
    QAction* action= action_for_item (item);
    if (action != nullptr && action->isEnabled ()) {
      action->trigger ();
      palette->close ();
    }
  });
  QObject::connect (filter, &QLineEdit::returnPressed,
                    palette, trigger_current);

  select_first_visible (list);
  palette->resize (640, 480);
  palette->show ();
  filter->setFocus ();
}
#endif

} // namespace

void
command_palette_show () {
  QTMCommandRegistry& registry= QTMCommandRegistry::instance ();
  if (!registry.initialize ()) return;

  QTMMainTabWindow* shell= QTMMainTabWindow::topTabWindow ();
  if (shell == nullptr) return;
  QTMCommandContext context=
    registry.captureContext (shell, QApplication::focusWidget ());
  QWidget* host= QApplication::activeWindow ();
  if (host == nullptr) host= shell;

#ifdef USE_KF6
  KCommandBar* palette= new KCommandBar (host);
  palette->setAttribute (Qt::WA_DeleteOnClose);
  QVector<PaletteGroup> source= build_palette_groups (palette, context);
  QVector<KCommandBar::ActionGroup> groups;
  for (const PaletteGroup& sourceGroup: source) {
    KCommandBar::ActionGroup group;
    group.name= sourceGroup.name;
    group.actions= sourceGroup.actions;
    groups.append (std::move (group));
  }
  palette->setActions (groups);
  palette->show ();
#else
  show_qt_command_palette (host, context);
#endif
}
