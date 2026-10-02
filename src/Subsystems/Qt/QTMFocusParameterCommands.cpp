/******************************************************************************
* MODULE     : QTMFocusParameterCommands.cpp
* DESCRIPTION: Native structured-focus parameter and search providers
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
#include <QInputDialog>
#include <QJsonObject>
#include <QLineEdit>

using namespace qtm_command_registry_detail;

namespace {

QString
qstring (const std::string& value) {
  return QString::fromUtf8 (
    value.data (), static_cast<int> (value.size ()));
}

bool
generic_focus_surface (const actor_focus_toolbar_snapshot& focus) {
  return focus.valid () &&
         !focus.has (ACTOR_FOCUS_TOOLBAR_BUFFER) &&
         !focus.has (ACTOR_FOCUS_TOOLBAR_CODE_CONTEXT) &&
         !focus.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT);
}

bool
preferences_focus_surface (const actor_focus_toolbar_snapshot& focus) {
  return generic_focus_surface (focus) ||
         (focus.valid () &&
          focus.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT));
}

const std::vector<actor_focus_parameter_snapshot>*
parameter_vector (
  const actor_focus_toolbar_snapshot& focus, const QString& scope) {
  if (scope == QStringLiteral ("global")) return &focus.global_parameters;
  if (scope == QStringLiteral ("local")) return &focus.local_parameters;
  return nullptr;
}

const actor_focus_parameter_snapshot*
parameter_at (
  const actor_focus_toolbar_snapshot& focus,
  const QString& scope, int index) {
  const auto* parameters= parameter_vector (focus, scope);
  if (parameters == nullptr || index < 0 ||
      index >= static_cast<int> (parameters->size ()))
    return nullptr;
  return &(*parameters)[static_cast<std::size_t> (index)];
}

void
append_parameter_items (
  QVector<QTMCommandDynamicItem>& out,
  const std::vector<actor_focus_parameter_snapshot>& parameters,
  const QString& scope, bool enabled) {
  for (std::size_t i=0; i<parameters.size (); ++i) {
    const auto& parameter= parameters[i];
    QString group= qstring (parameter.label);
    if (group.isEmpty ()) group= qstring (parameter.name);
    QString current= qstring (parameter.current);

    QTMCommandDynamicItem def= enabled_dynamic_item (
      QStringLiteral ("%1/%2/default").arg (scope).arg (i),
      QObject::tr ("Default"));
    def.group= group;
    def.state.enabled= enabled;
    def.state.checkable= true;
    def.state.checked= parameter.is_default;
    out.append (std::move (def));

    if (!parameter.choices.empty ()) {
      for (std::size_t j=0; j<parameter.choices.size (); ++j) {
        const auto& choice= parameter.choices[j];
        QString label= qstring (choice.label);
        if (label.isEmpty ()) label= qstring (choice.value);
        QTMCommandDynamicItem item= enabled_dynamic_item (
          QStringLiteral ("%1/%2/choice/%3").arg (scope).arg (i).arg (j),
          label);
        item.group= group;
        item.state.enabled= enabled;
        item.state.checkable= true;
        item.state.checked= current == qstring (choice.value);
        out.append (std::move (item));
      }
    }
    else if (qstring (parameter.type) == QStringLiteral ("boolean")) {
      const struct {
        const char* value;
        const char* label;
      } values[]= {{"true", "On"}, {"false", "Off"}};
      for (int j=0; j<2; ++j) {
        QTMCommandDynamicItem item= enabled_dynamic_item (
          QStringLiteral ("%1/%2/bool/%3").arg (scope).arg (i).arg (j),
          QObject::tr (values[j].label));
        item.group= group;
        item.state.enabled= enabled;
        item.state.checkable= true;
        item.state.checked= current == QString::fromLatin1 (values[j].value);
        out.append (std::move (item));
      }
    }
    else if (qstring (parameter.name).endsWith (QStringLiteral ("-font"))) {
      static const char* values[]= {
        "roman", "stix", "bonum", "pagella", "schola", "termes"
      };
      for (int j=0; j<6; ++j) {
        QString value= QString::fromLatin1 (values[j]);
        QString label= value;
        if (!label.isEmpty ()) label[0]= label[0].toUpper ();
        QTMCommandDynamicItem item= enabled_dynamic_item (
          QStringLiteral ("%1/%2/font/%3").arg (scope).arg (i).arg (j),
          label);
        item.group= group;
        item.state.enabled= enabled;
        item.state.checkable= true;
        item.state.checked= current == value;
        out.append (std::move (item));
      }
    }
    else if (qstring (parameter.type) == QStringLiteral ("font-size")) {
      const struct {
        const char* value;
        const char* label;
      } values[]= {
        {"0.841", "Small"}, {"1", "Normal"}, {"1.189", "Large"},
        {"1.414", "Very large"}, {"1.682", "Huge"}
      };
      for (int j=0; j<5; ++j) {
        QTMCommandDynamicItem item= enabled_dynamic_item (
          QStringLiteral ("%1/%2/size/%3").arg (scope).arg (i).arg (j),
          QObject::tr (values[j].label));
        item.group= group;
        item.state.enabled= enabled;
        item.state.checkable= true;
        item.state.checked=
          current == QString::fromLatin1 (values[j].value);
        out.append (std::move (item));
      }
    }

    QTMCommandDynamicItem other= enabled_dynamic_item (
      QStringLiteral ("%1/%2/other").arg (scope).arg (i),
      QObject::tr ("Other..."));
    other.group= group;
    other.state.enabled= enabled;
    out.append (std::move (other));
  }
}

bool
execute_parameter_item (
  const QString& key, const QTMCommandContext& context) {
  QStringList parts= key.split ('/');
  if (parts.size () < 3) return false;
  QString scope= parts[0];
  bool indexOk= false;
  int index= parts[1].toInt (&indexOk);
  if (!indexOk) return false;

  qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
  if (proxy == nullptr) return false;
  actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
  const actor_focus_parameter_snapshot* parameter=
    parameter_at (focus, scope, index);
  if (parameter == nullptr) return false;

  QJsonObject action;
  action.insert ("op", "focus-parameter");
  action.insert ("scope", scope);
  action.insert ("name", qstring (parameter->name));

  const QString kind= parts[2];
  if (kind == QStringLiteral ("default")) {
    action.insert ("reset", true);
  }
  else {
    QString value;
    if (kind == QStringLiteral ("choice") && parts.size () == 4) {
      bool choiceOk= false;
      int choiceIndex= parts[3].toInt (&choiceOk);
      if (!choiceOk || choiceIndex < 0 ||
          choiceIndex >= static_cast<int> (parameter->choices.size ()))
        return false;
      value= qstring (
        parameter->choices[static_cast<std::size_t> (choiceIndex)].value);
    }
    else if (kind == QStringLiteral ("bool") && parts.size () == 4) {
      value= parts[3] == QStringLiteral ("0") ?
        QStringLiteral ("true"): QStringLiteral ("false");
    }
    else if (kind == QStringLiteral ("font") && parts.size () == 4) {
      static const char* values[]= {
        "roman", "stix", "bonum", "pagella", "schola", "termes"
      };
      bool valueOk= false;
      int valueIndex= parts[3].toInt (&valueOk);
      if (!valueOk || valueIndex < 0 || valueIndex >= 6) return false;
      value= QString::fromLatin1 (values[valueIndex]);
    }
    else if (kind == QStringLiteral ("size") && parts.size () == 4) {
      static const char* values[]= {
        "0.841", "1", "1.189", "1.414", "1.682"
      };
      bool valueOk= false;
      int valueIndex= parts[3].toInt (&valueOk);
      if (!valueOk || valueIndex < 0 || valueIndex >= 5) return false;
      value= QString::fromLatin1 (values[valueIndex]);
    }
    else if (kind == QStringLiteral ("other")) {
      QString current= qstring (parameter->current);
      if (qstring (parameter->type) == QStringLiteral ("color")) {
        QColor initial (current);
        QColor selected= QColorDialog::getColor (
          initial.isValid () ? initial : Qt::white,
          context.shell.data (), QObject::tr ("Choose color"));
        if (!selected.isValid ()) return true;
        value= selected.name ();
      }
      else {
        bool ok= false;
        value= QInputDialog::getText (
          context.shell.data (), qstring (parameter->label),
          QObject::tr ("Value:"), QLineEdit::Normal, current, &ok).trimmed ();
        if (!ok || value.isEmpty ()) return true;
      }
    }
    else return false;
    action.insert ("value", value);
  }
  return submit_inline_editor_action (
    context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
}

QTMCommandState
parameter_provider_state (
  const QTMCommandContext& context, bool global) {
  QTMCommandState state;
  qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
  if (proxy == nullptr) return state;
  actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
  actor_editor_command_snapshot editorState= proxy->editor_command_state ();
  const bool surface=
    global ? preferences_focus_surface (focus) : generic_focus_surface (focus);
  if (!surface || !editorState.valid ()) return state;
  const auto& parameters=
    global ? focus.global_parameters: focus.local_parameters;
  const bool hasStyleOptions= global && !focus.style_options.empty ();
  if (parameters.empty () && !hasStyleOptions) return state;
  state.available= true;
  state.enabled= !editorState.read_only ();
  return state;
}

} // namespace

void
QTMCommandRegistry::registerFocusParameterCommands () {
  registerProvider (
    "editor-focus-preferences", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editorState= proxy->editor_command_state ();
      if (!preferences_focus_surface (focus) || !editorState.valid ())
        return out;
      const bool enabled= !editorState.read_only ();
      for (std::size_t i=0; i<focus.style_options.size (); ++i) {
        const auto& option= focus.style_options[i];
        QTMCommandDynamicItem item= enabled_dynamic_item (
          QStringLiteral ("style/%1").arg (i), qstring (option.label),
          qstring (option.help));
        item.group= QObject::tr ("Style options");
        item.state.enabled= enabled;
        item.state.checkable= true;
        item.state.checked= option.checked;
        out.append (std::move (item));
      }
      append_parameter_items (
        out, focus.global_parameters, QStringLiteral ("global"), enabled);
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      if (key.startsWith (QStringLiteral ("style/"))) {
        bool ok= false;
        int index= key.mid (6).toInt (&ok);
        if (!ok) return false;
        qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
        if (proxy == nullptr) return false;
        actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
        if (index < 0 ||
            index >= static_cast<int> (focus.style_options.size ()))
          return false;
        QJsonObject action;
        action.insert ("op", "focus-style-option");
        action.insert (
          "name",
          qstring (
            focus.style_options[static_cast<std::size_t> (index)].name));
        return submit_inline_editor_action (
          context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
      }
      return execute_parameter_item (key, context);
    },
    [] (const QTMCommandContext& context) {
      return parameter_provider_state (context, true);
    });

  registerProvider (
    "editor-focus-tag-edit", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      if (!focus.valid () || !focus.tag_extension) return out;
      if (focus.tag_macro_source_available)
        out.append (enabled_dynamic_item (
          QStringLiteral ("edit-source"), QObject::tr ("Edit source")));
      QString command= qstring (focus.tag_shortcut_command);
      if (!command.isEmpty ())
        out.append (enabled_dynamic_item (
          QStringLiteral ("shortcut"),
          focus.tag_shortcut_exists ?
            QObject::tr ("Edit shortcut"): QObject::tr ("Create shortcut")));
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return false;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      if (!focus.valid () || !focus.tag_extension) return false;
      if (key == QStringLiteral ("edit-source")) {
        if (!focus.tag_macro_source_available) return false;
        QJsonObject action;
        action.insert ("op", "focus-action");
        action.insert ("id", "edit-focus-macro-source");
        return submit_inline_editor_action (context, action);
      }
      if (key != QStringLiteral ("shortcut")) return false;
      QString command= qstring (focus.tag_shortcut_command);
      if (command.isEmpty ()) return false;
      try {
        (void) call (
          "open-shortcuts-editor", object (string ("")),
          object (from_qstring (command)));
        return true;
      }
      catch (...) {
        return false;
      }
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return state;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      if (!focus.valid () || !focus.tag_extension) return state;
      state.available= true;
      state.enabled= true;
      return state;
    });

  registerProvider (
    "editor-focus-rendering", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editorState= proxy->editor_command_state ();
      if (!generic_focus_surface (focus) || !editorState.valid ()) return out;
      append_parameter_items (
        out, focus.local_parameters, QStringLiteral ("local"),
        !editorState.read_only ());
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      return execute_parameter_item (key, context);
    },
    [] (const QTMCommandContext& context) {
      return parameter_provider_state (context, false);
    });

  registerBehavior (
    "editor.focus.search", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QJsonObject action;
      action.insert ("op", "focus-search");
      return submit_inline_editor_action (context, action);
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return state;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      if (!generic_focus_surface (focus) ||
          (!focus.has (ACTOR_FOCUS_TOOLBAR_HAS_SEARCH_MENU) &&
           !focus.has (ACTOR_FOCUS_TOOLBAR_CAN_SEARCH)))
        return state;
      state.available= true;
      state.enabled= true;
      return state;
    });
}
