/******************************************************************************
* MODULE     : QTMFocusDocumentCommands.cpp
* DESCRIPTION: Native document/screens structured-focus providers
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMCommandRegistry.hpp"
#include "QTMCommandRegistryInternal.hpp"

#include <QColor>
#include <QColorDialog>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>

using namespace qtm_command_registry_detail;

namespace {

QString
qstring (const std::string& value) {
  return QString::fromUtf8 (
    value.data (), static_cast<int> (value.size ()));
}

QTMCommandState
document_focus_state (const QTMCommandContext& context, bool writable= true) {
  QTMCommandState state;
  qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
  if (proxy == nullptr) return state;
  actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
  actor_editor_command_snapshot editorState= proxy->editor_command_state ();
  if (!focus.valid () || !editorState.valid () ||
      (!focus.has (ACTOR_FOCUS_TOOLBAR_BUFFER) &&
       !focus.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT)))
    return state;
  state.available= true;
  state.enabled= !writable || !editorState.read_only ();
  return state;
}

bool
submit_focus_action (
  const QTMCommandContext& context, const QString& id) {
  QJsonObject action;
  action.insert ("op", "focus-action");
  action.insert ("id", id);
  return submit_inline_editor_action (
    context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
}

} // namespace

void
QTMCommandRegistry::registerFocusDocumentCommands () {
  registerProvider (
    "editor-focus-document-styles", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editorState= proxy->editor_command_state ();
      if (!document_focus_state (context).available || !editorState.valid ())
        return out;
      const bool enabled= !editorState.read_only ();
      for (std::size_t i=0; i<focus.document_styles.size (); ++i) {
        const auto& choice= focus.document_styles[i];
        QTMCommandDynamicItem item= enabled_dynamic_item (
          QString::number (static_cast<qulonglong> (i)),
          qstring (choice.label));
        item.state.enabled= enabled;
        item.state.checkable= true;
        item.state.checked= choice.checked;
        out.append (std::move (item));
      }
      QTMCommandDynamicItem other= enabled_dynamic_item (
        QStringLiteral ("other"), QObject::tr ("Other style..."));
      other.state.enabled= enabled;
      out.append (std::move (other));
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      if (key == QStringLiteral ("other")) {
        qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
        if (proxy == nullptr) return false;
        actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
        bool ok= false;
        QString name= QInputDialog::getText (
          context.shell.data (), QObject::tr ("Document style"),
          QObject::tr ("Style name:"), QLineEdit::Normal, QString (), &ok)
                         .trimmed ();
        if (!ok || name.isEmpty ()) return true;
        for (const auto& style: focus.document_styles)
          if (qstring (style.value) == name) {
            QJsonObject action;
            action.insert ("op", "set-main-style");
            action.insert ("style", name);
            return submit_inline_editor_action (
              context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
          }
        QMessageBox::warning (
          context.shell.data (), QObject::tr ("Document style"),
          QObject::tr ("No installed document style named '%1' was found.")
            .arg (name));
        return true;
      }
      bool ok= false;
      int index= key.toInt (&ok);
      if (!ok) return false;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return false;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      if (index < 0 ||
          index >= static_cast<int> (focus.document_styles.size ()))
        return false;
      QJsonObject action;
      action.insert ("op", "set-main-style");
      action.insert (
        "style",
        qstring (
          focus.document_styles[static_cast<std::size_t> (index)].value));
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state= document_focus_state (context);
      if (!state.available) return state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr ||
          proxy->focus_toolbar_state ().document_styles.empty ()) {
        state.available= false;
        state.enabled= false;
      }
      return state;
    });

  registerProvider (
    "editor-focus-document-packages", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editorState= proxy->editor_command_state ();
      if (!document_focus_state (context).available || !editorState.valid ())
        return out;
      const bool enabled= !editorState.read_only ();
      for (std::size_t i=0; i<focus.current_packages.size (); ++i) {
        const auto& package= focus.current_packages[i];
        const QString group= qstring (package.label);
        QTMCommandDynamicItem edit= enabled_dynamic_item (
          QStringLiteral ("edit/%1").arg (i), QObject::tr ("Edit package"));
        edit.group= group;
        edit.state.enabled= enabled;
        out.append (std::move (edit));
        QTMCommandDynamicItem remove= enabled_dynamic_item (
          QStringLiteral ("remove/%1").arg (i), QObject::tr ("Remove package"));
        remove.group= group;
        remove.state.enabled= enabled;
        out.append (std::move (remove));
      }
      for (std::size_t i=0; i<focus.document_packages.size (); ++i) {
        const auto& package= focus.document_packages[i];
        QTMCommandDynamicItem item= enabled_dynamic_item (
          QStringLiteral ("add/%1").arg (i), qstring (package.label));
        item.group= QObject::tr ("Add package");
        item.state.enabled= enabled && !package.checked;
        item.state.checkable= true;
        item.state.checked= package.checked;
        out.append (std::move (item));
      }
      QTMCommandDynamicItem other= enabled_dynamic_item (
        QStringLiteral ("other"), QObject::tr ("Other package..."));
      other.group= QObject::tr ("Add package");
      other.state.enabled= enabled;
      out.append (std::move (other));
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      if (key == QStringLiteral ("other")) {
        qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
        if (proxy == nullptr) return false;
        actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
        bool ok= false;
        QString name= QInputDialog::getText (
          context.shell.data (), QObject::tr ("Add style package"),
          QObject::tr ("Package name:"), QLineEdit::Normal, QString (), &ok)
                         .trimmed ();
        if (!ok || name.isEmpty ()) return true;
        for (const auto& package: focus.document_packages)
          if (qstring (package.value) == name) {
            QJsonObject action;
            action.insert ("op", "focus-document-package");
            action.insert ("action", "add");
            action.insert ("name", name);
            return submit_inline_editor_action (
              context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
          }
        QMessageBox::warning (
          context.shell.data (), QObject::tr ("Add style package"),
          QObject::tr ("No installed style package named '%1' was found.")
            .arg (name));
        return true;
      }
      QStringList parts= key.split ('/');
      if (parts.size () != 2) return false;
      bool ok= false;
      int index= parts[1].toInt (&ok);
      if (!ok) return false;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return false;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      QString actionKind;
      QString name;
      if (parts[0] == QStringLiteral ("add")) {
        if (index < 0 ||
            index >= static_cast<int> (focus.document_packages.size ()))
          return false;
        actionKind= QStringLiteral ("add");
        name= qstring (
          focus.document_packages[static_cast<std::size_t> (index)].value);
      }
      else if (parts[0] == QStringLiteral ("edit") ||
               parts[0] == QStringLiteral ("remove")) {
        if (index < 0 ||
            index >= static_cast<int> (focus.current_packages.size ()))
          return false;
        actionKind= parts[0];
        name= qstring (
          focus.current_packages[static_cast<std::size_t> (index)].value);
      }
      else return false;
      QJsonObject action;
      action.insert ("op", "focus-document-package");
      action.insert ("action", actionKind);
      action.insert ("name", name);
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state= document_focus_state (context);
      if (!state.available) return state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return QTMCommandState {};
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      if (focus.document_packages.empty () && focus.current_packages.empty ()) {
        state.available= false;
        state.enabled= false;
      }
      return state;
    });

  registerProvider (
    "editor-focus-document-themes", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editorState= proxy->editor_command_state ();
      if (!document_focus_state (context).available || !editorState.valid ())
        return out;
      const bool enabled= !editorState.read_only ();
      if (qstring (focus.document_theme_kind) == QStringLiteral ("basic")) {
        bool plain= true;
        for (const auto& choice: focus.document_themes)
          if (choice.checked) {
            plain= false;
            break;
          }
        QTMCommandDynamicItem item= enabled_dynamic_item (
          QStringLiteral ("plain"), QObject::tr ("Plain"));
        item.state.enabled= enabled;
        item.state.checkable= true;
        item.state.checked= plain;
        out.append (std::move (item));
      }
      for (std::size_t i=0; i<focus.document_themes.size (); ++i) {
        const auto& choice= focus.document_themes[i];
        QTMCommandDynamicItem item= enabled_dynamic_item (
          QStringLiteral ("theme/%1").arg (i), qstring (choice.label));
        item.state.enabled= enabled;
        item.state.checkable= true;
        item.state.checked= choice.checked;
        out.append (std::move (item));
      }
      for (std::size_t i=0; i<focus.document_title_themes.size (); ++i) {
        const auto& choice= focus.document_title_themes[i];
        QTMCommandDynamicItem item= enabled_dynamic_item (
          QStringLiteral ("title/%1").arg (i), qstring (choice.label));
        item.group= QObject::tr ("Title style");
        item.state.enabled= enabled;
        item.state.checkable= true;
        item.state.checked= choice.checked;
        out.append (std::move (item));
      }
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      if (key == QStringLiteral ("plain")) {
        QJsonObject action;
        action.insert ("op", "focus-document-default-theme");
        return submit_inline_editor_action (
          context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
      }
      QStringList parts= key.split ('/');
      if (parts.size () != 2) return false;
      bool ok= false;
      int index= parts[1].toInt (&ok);
      if (!ok) return false;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return false;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      const std::vector<actor_focus_choice_snapshot>* choices= nullptr;
      if (parts[0] == QStringLiteral ("theme"))
        choices= &focus.document_themes;
      else if (parts[0] == QStringLiteral ("title"))
        choices= &focus.document_title_themes;
      if (choices == nullptr || index < 0 ||
          index >= static_cast<int> (choices->size ()))
        return false;
      QJsonObject action;
      action.insert ("op", "focus-document-package");
      const QString name=
        qstring ((*choices)[static_cast<std::size_t> (index)].value);
      action.insert (
        "action",
        name == QStringLiteral ("alt-colors") ||
        name == QStringLiteral ("framed-theorems") ?
          QStringLiteral ("toggle"): QStringLiteral ("add"));
      action.insert (
        "name", name);
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state= document_focus_state (context);
      if (!state.available) return state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return QTMCommandState {};
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      if (focus.document_themes.empty () &&
          focus.document_title_themes.empty ()) {
        state.available= false;
        state.enabled= false;
      }
      return state;
    });

  registerProvider (
    "editor-focus-slides", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      if (!focus.valid () ||
          !focus.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT))
        return out;
      for (std::size_t i=0; i<focus.slide_names.size (); ++i)
        out.append (enabled_dynamic_item (
          QString::number (static_cast<qulonglong> (i)),
          qstring (focus.slide_names[i])));
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      bool ok= false;
      int index= key.toInt (&ok);
      if (!ok || index < 0) return false;
      QJsonObject action;
      action.insert ("op", "focus-slide-switch");
      action.insert ("index", index);
      return submit_inline_editor_action (context, action);
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return state;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      if (!focus.valid () ||
          !focus.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT) ||
          focus.slide_names.empty ())
        return state;
      state.available= true;
      state.enabled= true;
      return state;
    });

  registerBehavior (
    "editor.focus.document-edit-style", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QJsonObject action;
      action.insert ("op", "focus-document-edit-style");
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      return document_focus_state (context);
    });
  registerBehavior (
    "editor.focus.document-install-style", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QString path= QFileDialog::getOpenFileName (
        context.shell.data (), QObject::tr ("Install custom style"),
        QString (),
        QObject::tr ("ATHENA styles (*.ats *.ts);;All files (*)"));
      if (path.isEmpty ()) return true;
      QJsonObject action;
      action.insert ("op", "focus-document-install-style");
      action.insert ("path", path);
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      return document_focus_state (context);
    });
  registerBehavior (
    "editor.focus.document-background", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return false;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      QColor initial (qstring (focus.background_color));
      QColor selected= QColorDialog::getColor (
        initial.isValid () ? initial : Qt::white,
        context.shell.data (), QObject::tr ("Document background"));
      if (!selected.isValid ()) return true;
      QJsonObject action;
      action.insert ("op", "init-env");
      action.insert ("var", "bg-color");
      action.insert ("value", selected.name ());
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state= document_focus_state (context);
      if (!state.available) return state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr ||
          !proxy->focus_toolbar_state ().background_available) {
        state.available= false;
        state.enabled= false;
      }
      return state;
    });
  registerProvider (
    "editor-focus-document-background", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      QTMCommandState state= document_focus_state (context);
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (!state.available || proxy == nullptr ||
          !proxy->focus_toolbar_state ().background_available)
        return out;
      const bool enabled= state.enabled;
      auto append= [&] (const QString& key, const QString& label,
                         const QString& icon= QString ()) {
        QTMCommandDynamicItem item= enabled_dynamic_item (key, label);
        item.icon= icon;
        item.state.enabled= enabled;
        out.append (std::move (item));
      };
      append (QStringLiteral ("default"), QObject::tr ("Default"));
      append (QStringLiteral ("color"), QObject::tr ("Color..."),
              QStringLiteral ("tm_color"));
      append (QStringLiteral ("pattern"), QObject::tr ("Pattern..."));
      append (QStringLiteral ("gradient"), QObject::tr ("Gradient..."));
      append (QStringLiteral ("picture"), QObject::tr ("Picture..."),
              QStringLiteral ("tm_camera"));
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      if (key == QStringLiteral ("color"))
        return QTMCommandRegistry::instance ().execute (
          QStringLiteral ("editor.focus.document-background"), context);
      QJsonObject action;
      if (key == QStringLiteral ("default")) {
        action.insert ("op", "init-default");
        action.insert ("var", "bg-color");
      }
      else {
        action.insert ("op", "business");
        if (key == QStringLiteral ("pattern"))
          action.insert ("id", "document-background-pattern");
        else if (key == QStringLiteral ("gradient"))
          action.insert ("id", "document-background-gradient");
        else if (key == QStringLiteral ("picture"))
          action.insert ("id", "document-background-picture");
        else
          return false;
      }
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state= document_focus_state (context);
      if (!state.available) return state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr ||
          !proxy->focus_toolbar_state ().background_available) {
        state.available= false;
        state.enabled= false;
      }
      return state;
    });
  registerBehavior (
    "editor.focus.slide-title", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      return submit_focus_action (context, QStringLiteral ("slide-insert-title"));
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return state;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editorState= proxy->editor_command_state ();
      if (!focus.valid () || !editorState.valid () ||
          !focus.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT) ||
          !focus.slide_propose_title)
        return state;
      state.available= true;
      state.enabled= !editorState.read_only ();
      return state;
    });
  registerBehavior (
    "editor.focus.slide-draw", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      return submit_focus_action (
        context, QStringLiteral ("slide-insert-graphics"));
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return state;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editorState= proxy->editor_command_state ();
      if (!focus.valid () || !editorState.valid () ||
          !focus.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT) ||
          !focus.slide_propose_graphics)
        return state;
      state.available= true;
      state.enabled= !editorState.read_only ();
      return state;
    });
}
