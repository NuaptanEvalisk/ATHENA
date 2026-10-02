/******************************************************************************
* MODULE     : QTMCommandRegistry.cpp
* DESCRIPTION: Native application command registry and work-context routing
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/


#include "QTMCommandRegistry.hpp"
#include "QTMCommandRegistryInternal.hpp"

#include "QTMMetadataPropertiesPane.hpp"
#include "QTMPagePropertiesPane.hpp"
#include "QTMSlidePropertiesPane.hpp"
#include "QTMTablePropertiesPane.hpp"

#include <QInputDialog>
#include <QJsonObject>
#include <QLineEdit>
#include <QSet>

using namespace qtm_command_registry_detail;

namespace {
QTMCommandState
focus_surface_state (
  const QTMCommandContext& context, std::uint32_t anyFocusFlags,
  bool writable= false) {
  QTMCommandState state;
  qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
  if (proxy == nullptr) return state;
  actor_editor_command_snapshot editorState= proxy->editor_command_state ();
  actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
  if (!editorState.valid () || !focus.valid () ||
      (focus.flags & anyFocusFlags) == 0)
    return state;
  state.available= true;
  state.enabled= !writable || !editorState.read_only ();
  return state;
}

} // namespace

void
QTMCommandRegistry::registerFocusCommands () {
  registerFocusParameterCommands ();
  registerFocusDocumentCommands ();
  registerFocusSpecialCommands ();
  registerProvider (
    "editor-focus-variants", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editorState= proxy->editor_command_state ();
      if (!focus.valid () ||
          focus.has (ACTOR_FOCUS_TOOLBAR_BUFFER) ||
          focus.has (ACTOR_FOCUS_TOOLBAR_CODE_CONTEXT) ||
          focus.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT) ||
          !focus.has (ACTOR_FOCUS_TOOLBAR_HAS_VARIANTS) ||
          !editorState.valid ())
        return out;
      for (std::size_t i=0; i<focus.variants.size (); ++i) {
        QString key= QString::fromUtf8 (
          focus.variants[i].data (),
          static_cast<int> (focus.variants[i].size ()));
        QString label= key;
        if (i < focus.variant_names.size ())
          label= QString::fromUtf8 (
            focus.variant_names[i].data (),
            static_cast<int> (focus.variant_names[i].size ()));
        QTMCommandDynamicItem item=
          enabled_dynamic_item (key, label, QObject::tr ("Use %1").arg (label));
        item.state.enabled= !editorState.read_only ();
        out.append (std::move (item));
      }
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      if (key.isEmpty ()) return false;
      QJsonObject action;
      action.insert ("op", "focus-variant");
      action.insert ("tag", key);
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return state;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editorState= proxy->editor_command_state ();
      if (!focus.valid () ||
          focus.has (ACTOR_FOCUS_TOOLBAR_BUFFER) ||
          focus.has (ACTOR_FOCUS_TOOLBAR_CODE_CONTEXT) ||
          focus.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT) ||
          !focus.has (ACTOR_FOCUS_TOOLBAR_HAS_VARIANTS) ||
          !editorState.valid ())
        return state;
      state.available= true;
      state.enabled= !editorState.read_only ();
      return state;
    });
  registerProvider (
    "editor-focus-code-label", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      if (!focus.valid () ||
          !focus.has (ACTOR_FOCUS_TOOLBAR_CODE_CONTEXT))
        return out;
      QString label= QString::fromUtf8 (
        focus.code_language.data (),
        static_cast<int> (focus.code_language.size ()));
      if (label.isEmpty ()) label= QObject::tr ("Code");
      QTMCommandDynamicItem item= enabled_dynamic_item (
        QStringLiteral ("__label__"), label);
      item.state.enabled= false;
      out.append (std::move (item));
      return out;
    },
    [] (const QString&, const QTMCommandContext&) {
      return false;
    },
    [] (const QTMCommandContext& context) {
      return focus_surface_state (
        context, ACTOR_FOCUS_TOOLBAR_CODE_CONTEXT, false);
    });
  registerProvider (
    "editor-focus-tag-label", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      const bool screens=
        focus.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT);
      if (!focus.valid () ||
          focus.has (ACTOR_FOCUS_TOOLBAR_BUFFER) ||
          focus.has (ACTOR_FOCUS_TOOLBAR_CODE_CONTEXT) ||
          (!screens && focus.has (ACTOR_FOCUS_TOOLBAR_HAS_VARIANTS)))
        return out;
      QString label= QString::fromUtf8 (
        focus.tag_name.data (), static_cast<int> (focus.tag_name.size ()));
      if (label.isEmpty ())
        label= QString::fromUtf8 (
          focus.tag_label.data (), static_cast<int> (focus.tag_label.size ()));
      if (label.isEmpty ()) return out;
      QTMCommandDynamicItem item= enabled_dynamic_item (
        QStringLiteral ("__label__"), label);
      item.state.enabled= false;
      out.append (std::move (item));
      return out;
    },
    [] (const QString&, const QTMCommandContext&) {
      return false;
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return state;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      const bool screens=
        focus.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT);
      if (!focus.valid () ||
          focus.has (ACTOR_FOCUS_TOOLBAR_BUFFER) ||
          focus.has (ACTOR_FOCUS_TOOLBAR_CODE_CONTEXT) ||
          (!screens && focus.has (ACTOR_FOCUS_TOOLBAR_HAS_VARIANTS)))
        return state;
      state.available= !focus.tag_name.empty () || !focus.tag_label.empty ();
      state.enabled= false;
      return state;
    });
  registerProvider (
    "editor-focus-document-font-sizes", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editorState= proxy->editor_command_state ();
      const std::uint32_t surfaces=
        ACTOR_FOCUS_TOOLBAR_BUFFER |
        ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT;
      if (!focus.valid () || (focus.flags & surfaces) == 0 ||
          !editorState.valid ())
        return out;
      const bool enabled= !editorState.read_only ();
      QTMCommandDynamicItem def= enabled_dynamic_item (
        QStringLiteral ("__default__"), QObject::tr ("Default"));
      def.state.enabled= enabled;
      out.append (std::move (def));
      static const char* values[]= {"8", "9", "10", "11", "12", "14"};
      for (const char* value: values) {
        QTMCommandDynamicItem item= enabled_dynamic_item (
          QString::fromLatin1 (value), QString::fromLatin1 (value));
        item.state.enabled= enabled;
        out.append (std::move (item));
      }
      QTMCommandDynamicItem other= enabled_dynamic_item (
        QStringLiteral ("__other__"), QObject::tr ("Other..."));
      other.state.enabled= enabled;
      out.append (std::move (other));
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      QJsonObject action;
      if (key == QStringLiteral ("__default__")) {
        action.insert ("op", "init-default");
        action.insert ("var", "font-base-size");
      }
      else {
        QString value= key;
        if (key == QStringLiteral ("__other__")) {
          qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
          if (proxy == nullptr) return false;
          actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
          QString current= QString::fromUtf8 (
            focus.font_base_size.data (),
            static_cast<int> (focus.font_base_size.size ()));
          bool ok= false;
          value= QInputDialog::getText (
            context.shell.data (), QObject::tr ("Font size"),
            QObject::tr ("Base size:"), QLineEdit::Normal, current, &ok)
                    .trimmed ();
          if (!ok || value.isEmpty ()) return true;
        }
        action.insert ("op", "init-env");
        action.insert ("var", "font-base-size");
        action.insert ("value", value);
      }
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      return focus_surface_state (
        context,
        ACTOR_FOCUS_TOOLBAR_BUFFER |
        ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT,
        true);
    });
  registerProvider (
    "editor-focus-document-languages", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editorState= proxy->editor_command_state ();
      const std::uint32_t surfaces=
        ACTOR_FOCUS_TOOLBAR_BUFFER |
        ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT;
      if (!focus.valid () || (focus.flags & surfaces) == 0 ||
          !editorState.valid ())
        return out;
      QString current= QString::fromUtf8 (
        focus.document_language.data (),
        static_cast<int> (focus.document_language.size ()));
      const bool enabled= !editorState.read_only ();
      QTMCommandDynamicItem def= enabled_dynamic_item (
        QStringLiteral ("__default__"), QObject::tr ("Default"));
      def.state.enabled= enabled;
      out.append (std::move (def));
      static const char* languages[]= {
        "british", "bulgarian", "chinese", "croatian", "czech",
        "danish", "dutch", "english", "esperanto", "finnish", "french",
        "german", "greek", "hungarian", "italian", "japanese", "korean",
        "polish", "portuguese", "romanian", "russian", "slovak",
        "slovene", "spanish", "swedish", "taiwanese", "ukrainian"
      };
      for (const char* raw: languages) {
        string language (raw);
        bool supported= true;
        try {
          supported= as_bool (
            call ("supported-language?", object (language)));
        }
        catch (...) {}
        if (!supported) continue;
        QString key= QString::fromLatin1 (raw);
        QString label= key;
        if (!label.isEmpty ()) label[0]= label[0].toUpper ();
        QTMCommandDynamicItem item= enabled_dynamic_item (key, label);
        item.state.enabled= enabled;
        item.state.checkable= true;
        item.state.checked= current == key;
        out.append (std::move (item));
      }
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      QJsonObject action;
      if (key == QStringLiteral ("__default__"))
        action.insert ("op", "set-default-document-language");
      else {
        action.insert ("op", "set-document-language");
        action.insert ("language", key);
      }
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      return focus_surface_state (
        context,
        ACTOR_FOCUS_TOOLBAR_BUFFER |
        ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT,
        true);
    });
  registerProvider (
    "editor-focus-core-toggles", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editorState= proxy->editor_command_state ();
      if (!focus.valid () || !editorState.valid () ||
          focus.has (ACTOR_FOCUS_TOOLBAR_BUFFER) ||
          focus.has (ACTOR_FOCUS_TOOLBAR_CODE_CONTEXT) ||
          focus.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT) ||
          focus.has (ACTOR_FOCUS_TOOLBAR_ALGORITHM_CONTEXT))
        return out;
      const bool enabled= !editorState.read_only ();
      auto appendToggle=
        [&] (const QString& key, const QString& label,
             const QString& icon, bool checked) {
          QTMCommandDynamicItem item= enabled_dynamic_item (key, label);
          item.icon= icon;
          item.state.enabled= enabled;
          item.state.checkable= true;
          item.state.checked= checked;
          out.append (std::move (item));
        };
      if (focus.numbered_available)
        appendToggle (
          QStringLiteral ("numbered-toggle"), QObject::tr ("Toggle numbering"),
          QStringLiteral ("tm_numbered"), focus.numbered_checked);
      if (focus.alternate_available) {
        QString label= QString::fromUtf8 (
          focus.alternate_label.data (),
          static_cast<int> (focus.alternate_label.size ()));
        if (label.isEmpty ()) label= QObject::tr ("Toggle alternate");
        QString icon= QString::fromUtf8 (
          focus.alternate_icon.data (),
          static_cast<int> (focus.alternate_icon.size ()));
        if (icon.isEmpty ()) icon= QStringLiteral ("tm_alternate_both");
        appendToggle (
          QStringLiteral ("alternate-toggle"), label, icon,
          focus.alternate_checked);
      }
      if (focus.hidden_toggle_available)
        appendToggle (
          QStringLiteral ("inactive-toggle"), QObject::tr ("Show hidden"),
          QStringLiteral ("tm_show_hidden"), focus.hidden_checked);
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      static const QSet<QString> allowed {
        QStringLiteral ("numbered-toggle"),
        QStringLiteral ("alternate-toggle"),
        QStringLiteral ("inactive-toggle")
      };
      if (!allowed.contains (key)) return false;
      QJsonObject action;
      action.insert ("op", "focus-action");
      action.insert ("id", key);
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return state;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editorState= proxy->editor_command_state ();
      if (!focus.valid () || !editorState.valid () ||
          focus.has (ACTOR_FOCUS_TOOLBAR_BUFFER) ||
          focus.has (ACTOR_FOCUS_TOOLBAR_CODE_CONTEXT) ||
          focus.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT) ||
          focus.has (ACTOR_FOCUS_TOOLBAR_ALGORITHM_CONTEXT) ||
          (!focus.numbered_available &&
           !focus.alternate_available &&
           !focus.hidden_toggle_available))
        return state;
      state.available= true;
      state.enabled= !editorState.read_only ();
      return state;
    });
  registerProvider (
    "editor-focus-algorithm-toggles", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editorState= proxy->editor_command_state ();
      if (!focus.valid () || !editorState.valid () ||
          !focus.has (ACTOR_FOCUS_TOOLBAR_ALGORITHM_CONTEXT))
        return out;
      const bool enabled= !editorState.read_only ();
      auto appendToggle=
        [&] (const QString& key, const QString& label,
             const QString& icon, bool checked) {
          QTMCommandDynamicItem item= enabled_dynamic_item (key, label);
          item.icon= icon;
          item.state.enabled= enabled;
          item.state.checkable= true;
          item.state.checked= checked;
          out.append (std::move (item));
        };
      if (!focus.algorithm_named)
        appendToggle (
          QStringLiteral ("algorithm-toggle-number"),
          QObject::tr ("Toggle numbering"), QStringLiteral ("tm_numbered"),
          focus.algorithm_numbered);
      appendToggle (
        QStringLiteral ("algorithm-toggle-name"), QObject::tr ("Toggle name"),
        QStringLiteral ("tm_small_textual"), focus.algorithm_named);
      appendToggle (
        QStringLiteral ("algorithm-toggle-specification"),
        QObject::tr ("Toggle specification"),
        QStringLiteral ("tm_specified"), focus.algorithm_specified);
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      static const QSet<QString> allowed {
        QStringLiteral ("algorithm-toggle-number"),
        QStringLiteral ("algorithm-toggle-name"),
        QStringLiteral ("algorithm-toggle-specification")
      };
      if (!allowed.contains (key)) return false;
      QJsonObject action;
      action.insert ("op", "focus-action");
      action.insert ("id", key);
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      return focus_surface_state (
        context, ACTOR_FOCUS_TOOLBAR_ALGORITHM_CONTEXT, true);
    });
  registerProvider (
    "editor-focus-tag-toggles", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editorState= proxy->editor_command_state ();
      if (!focus.valid () || !editorState.valid ()) return out;
      const bool enabled= !editorState.read_only ();
      auto appendToggle=
        [&] (const QString& key, const QString& label,
             const QString& icon, bool checked) {
          QTMCommandDynamicItem item= enabled_dynamic_item (key, label);
          item.icon= icon;
          item.state.enabled= enabled;
          item.state.checkable= true;
          item.state.checked= checked;
          out.append (std::move (item));
        };
      if (focus.has (ACTOR_FOCUS_TOOLBAR_DETACHED_NOTE_CONTEXT))
        appendToggle (
          QStringLiteral ("note-toggle-custom"),
          QObject::tr ("Use custom note symbol"),
          QStringLiteral ("tm_small_textual"), focus.detached_note_custom);
      if (focus.has (ACTOR_FOCUS_TOOLBAR_TITLED_CONTEXT)) {
        if (focus.figure_context)
          appendToggle (
            QStringLiteral ("titled-toggle-name"), QObject::tr ("Toggle name"),
            QStringLiteral ("tm_small_textual"), focus.titled_named);
        else {
          QTMCommandDynamicItem item= enabled_dynamic_item (
            QStringLiteral ("node-properties"), QObject::tr ("Properties"));
          item.icon= QStringLiteral ("tm_small_textual");
          item.state.enabled= enabled;
          out.append (std::move (item));
        }
      }
      if (focus.has (ACTOR_FOCUS_TOOLBAR_FRAME_CONTEXT))
        appendToggle (
          QStringLiteral ("frame-toggle-title"), QObject::tr ("Toggle name"),
          QStringLiteral ("tm_small_textual"), focus.frame_titled);
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      if (key == QStringLiteral ("node-properties"))
        return invoke_native_editor_command (
          QStringLiteral ("editor.node-properties"), context);
      static const QSet<QString> allowed {
        QStringLiteral ("note-toggle-custom"),
        QStringLiteral ("titled-toggle-name"),
        QStringLiteral ("frame-toggle-title")
      };
      if (!allowed.contains (key)) return false;
      QJsonObject action;
      action.insert ("op", "focus-action");
      action.insert ("id", key);
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return state;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editorState= proxy->editor_command_state ();
      const std::uint32_t flags=
        ACTOR_FOCUS_TOOLBAR_DETACHED_NOTE_CONTEXT |
        ACTOR_FOCUS_TOOLBAR_TITLED_CONTEXT |
        ACTOR_FOCUS_TOOLBAR_FRAME_CONTEXT;
      if (!focus.valid () || !editorState.valid () ||
          (focus.flags & flags) == 0)
        return state;
      state.available= true;
      state.enabled= !editorState.read_only ();
      return state;
    });
  registerProvider (
    "editor-focus-float-toggles", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editorState= proxy->editor_command_state ();
      if (!focus.valid () || !editorState.valid ()) return out;
      const bool enabled= !editorState.read_only ();
      auto append=
        [&] (const QString& key, const QString& label,
             const QString& icon, bool checkable= false, bool checked= false) {
          QTMCommandDynamicItem item= enabled_dynamic_item (key, label);
          item.icon= icon;
          item.state.enabled= enabled;
          item.state.checkable= checkable;
          item.state.checked= checked;
          out.append (std::move (item));
        };

      if (focus.multicol_style &&
          (focus.has (ACTOR_FOCUS_TOOLBAR_RICH_FLOAT_CONTEXT) ||
           focus.has (ACTOR_FOCUS_TOOLBAR_FOOTNOTE_CONTEXT)))
        append (
          QStringLiteral ("float-toggle-wide"), QObject::tr ("Make wide"),
          QStringLiteral ("tm_wide_float"), true, focus.float_wide);
      if (focus.multicol_style &&
          focus.has (ACTOR_FOCUS_TOOLBAR_FLOATABLE_CONTEXT))
        append (
          QStringLiteral ("floatable-toggle-wide"), QObject::tr ("Make wide"),
          QStringLiteral ("tm_wide_float"), true, focus.floatable_wide);
      if (focus.has (ACTOR_FOCUS_TOOLBAR_FLOATABLE_CONTEXT))
        append (
          QStringLiteral ("turn-floating"), QObject::tr ("Make floating"),
          QStringLiteral ("tm_position_float"));
      if (focus.float_context_available)
        append (
          QStringLiteral ("turn-non-floating"),
          QObject::tr ("Make non floating"),
          QStringLiteral ("tm_position_float"));
      if (focus.has (ACTOR_FOCUS_TOOLBAR_RICH_FLOAT_CONTEXT) ||
          focus.has (ACTOR_FOCUS_TOOLBAR_FOOTNOTE_CONTEXT))
        append (
          QStringLiteral ("cursor-toggle-anchor"),
          focus.cursor_at_anchor ? QObject::tr ("Go to float or footnote")
                                 : QObject::tr ("Go to anchor"),
          QStringLiteral ("tm_anchor"));
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      static const QSet<QString> allowed {
        QStringLiteral ("float-toggle-wide"),
        QStringLiteral ("floatable-toggle-wide"),
        QStringLiteral ("turn-floating"),
        QStringLiteral ("turn-non-floating"),
        QStringLiteral ("cursor-toggle-anchor")
      };
      if (!allowed.contains (key)) return false;
      QJsonObject action;
      action.insert ("op", "focus-action");
      action.insert ("id", key);
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return state;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editorState= proxy->editor_command_state ();
      const std::uint32_t flags=
        ACTOR_FOCUS_TOOLBAR_RICH_FLOAT_CONTEXT |
        ACTOR_FOCUS_TOOLBAR_FLOATABLE_CONTEXT |
        ACTOR_FOCUS_TOOLBAR_FOOTNOTE_CONTEXT;
      if (!focus.valid () || !editorState.valid () ||
          (focus.flags & flags) == 0)
        return state;
      state.available= true;
      state.enabled= !editorState.read_only ();
      return state;
    });
  registerProvider (
    "editor-focus-float-positioning", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editorState= proxy->editor_command_state ();
      if (!focus.valid () || !editorState.valid ()) return out;
      const bool enabled= !editorState.read_only ();
      auto append=
        [&] (const QString& group, const QString& key, const QString& label,
             bool checked= false) {
          QTMCommandDynamicItem item= enabled_dynamic_item (key, label);
          item.group= group;
          item.state.enabled= enabled;
          item.state.checkable= true;
          item.state.checked= checked;
          out.append (std::move (item));
        };

      if (focus.has (ACTOR_FOCUS_TOOLBAR_MARGINAL_NOTE_CONTEXT)) {
        const QString currentH= QString::fromUtf8 (
          focus.marginal_hpos.data (),
          static_cast<int> (focus.marginal_hpos.size ()));
        const QString currentV= QString::fromUtf8 (
          focus.marginal_valign.data (),
          static_cast<int> (focus.marginal_valign.size ()));
        const struct { const char* key; const char* label; } h[]= {
          {"normal", "Automatic"}, {"left", "Left"}, {"right", "Right"},
          {"even-left", "Left on even pages"},
          {"even-right", "Right on even pages"}
        };
        for (const auto& value: h)
          append (
            QObject::tr ("Horizontal position"),
            QStringLiteral ("marginal-h:") + value.key,
            QObject::tr (value.label), currentH == value.key);
        const struct { const char* key; const char* label; } v[]= {
          {"t", "Top"}, {"c", "Center"}, {"b", "Bottom"}
        };
        for (const auto& value: v)
          append (
            QObject::tr ("Vertical alignment"),
            QStringLiteral ("marginal-v:") + value.key,
            QObject::tr (value.label), currentV == value.key);
      }
      if (focus.has (ACTOR_FOCUS_TOOLBAR_RICH_FLOAT_CONTEXT) ||
          focus.has (ACTOR_FOCUS_TOOLBAR_PHANTOM_FLOAT_CONTEXT)) {
        append (
          QObject::tr ("Allowed positions"), QStringLiteral ("float:t"),
          QObject::tr ("Top"));
        append (
          QObject::tr ("Allowed positions"), QStringLiteral ("float:h"),
          QObject::tr ("Here"));
        append (
          QObject::tr ("Allowed positions"), QStringLiteral ("float:b"),
          QObject::tr ("Bottom"));
        append (
          QObject::tr ("Allowed positions"), QStringLiteral ("float-not:f"),
          QObject::tr ("Other pages"));
      }
      if (focus.has (ACTOR_FOCUS_TOOLBAR_BALLOON_CONTEXT)) {
        const QString currentH= QString::fromUtf8 (
          focus.balloon_halign.data (),
          static_cast<int> (focus.balloon_halign.size ()));
        const QString currentV= QString::fromUtf8 (
          focus.balloon_valign.data (),
          static_cast<int> (focus.balloon_valign.size ()));
        const struct { const char* key; const char* label; } h[]= {
          {"Left", "Outer left"}, {"left", "Inner left"},
          {"center", "Center"}, {"right", "Inner right"},
          {"Right", "Outer right"}
        };
        for (const auto& value: h)
          append (
            QObject::tr ("Horizontal alignment"),
            QStringLiteral ("balloon-h:") + value.key,
            QObject::tr (value.label), currentH == value.key);
        const struct { const char* key; const char* label; } v[]= {
          {"Bottom", "Outer bottom"}, {"bottom", "Inner bottom"},
          {"center", "Center"}, {"top", "Inner top"}, {"Top", "Outer top"}
        };
        for (const auto& value: v)
          append (
            QObject::tr ("Vertical alignment"),
            QStringLiteral ("balloon-v:") + value.key,
            QObject::tr (value.label), currentV == value.key);
      }
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      QString id;
      QString value;
      if (key.startsWith ("marginal-h:")) {
        id= "set-marginal-note-hpos";
        value= key.mid (11);
      }
      else if (key.startsWith ("marginal-v:")) {
        id= "set-marginal-note-valign";
        value= key.mid (11);
      }
      else if (key.startsWith ("balloon-h:")) {
        id= "set-balloon-halign";
        value= key.mid (10);
      }
      else if (key.startsWith ("balloon-v:")) {
        id= "set-balloon-valign";
        value= key.mid (10);
      }
      else if (key.startsWith ("float-not:")) {
        id= "toggle-insertion-positioning-not";
        value= key.mid (10);
      }
      else if (key.startsWith ("float:")) {
        id= "toggle-insertion-positioning";
        value= key.mid (6);
      }
      else return false;
      QJsonObject action;
      action.insert ("op", "focus-action");
      action.insert ("id", id);
      action.insert ("value", value);
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return state;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editorState= proxy->editor_command_state ();
      const std::uint32_t flags=
        ACTOR_FOCUS_TOOLBAR_MARGINAL_NOTE_CONTEXT |
        ACTOR_FOCUS_TOOLBAR_RICH_FLOAT_CONTEXT |
        ACTOR_FOCUS_TOOLBAR_PHANTOM_FLOAT_CONTEXT |
        ACTOR_FOCUS_TOOLBAR_BALLOON_CONTEXT;
      if (!focus.valid () || !editorState.valid () ||
          (focus.flags & flags) == 0)
        return state;
      state.available= true;
      state.enabled= !editorState.read_only ();
      return state;
    });
  registerBehavior (
    "editor.focus.document-font", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QJsonObject action;
      action.insert ("op", "business");
      action.insert ("id", "open-document-font-selector");
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      return focus_surface_state (
        context,
        ACTOR_FOCUS_TOOLBAR_BUFFER |
        ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT,
        true);
    });
  registerBehavior (
    "editor.focus.page-properties", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      url target= frozen_document_url (context);
      if (is_none (target)) return false;
      page_properties_pane_show_for (target);
      return true;
    },
    [] (const QTMCommandContext& context) {
      return focus_surface_state (
        context,
        ACTOR_FOCUS_TOOLBAR_BUFFER |
        ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT,
        true);
    });
  registerBehavior (
    "editor.focus.slide-properties", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      url target= frozen_document_url (context);
      if (is_none (target)) return false;
      slide_properties_pane_show_for (target);
      return true;
    },
    [] (const QTMCommandContext& context) {
      return focus_surface_state (
        context, ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT, true);
    });
  registerBehavior (
    "editor.focus.set-main-style", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return false;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      QString current= QString::fromUtf8 (
        focus.document_style.data (),
        static_cast<int> (focus.document_style.size ()));
      bool ok= false;
      QString style= QInputDialog::getText (
        context.shell.data (), QObject::tr ("Document style"),
        QObject::tr ("Style name:"), QLineEdit::Normal, current, &ok).trimmed ();
      if (!ok || style.isEmpty ()) return true;
      QJsonObject action;
      action.insert ("op", "set-main-style");
      action.insert ("style", style);
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      return focus_surface_state (
        context,
        ACTOR_FOCUS_TOOLBAR_BUFFER |
        ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT,
        true);
    });
  registerBehavior (
    "editor.focus.metadata-properties", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      url target= frozen_document_url (context);
      if (is_none (target)) return false;
      metadata_properties_pane_show_for (target);
      return true;
    },
    [] (const QTMCommandContext& context) {
      return focus_surface_state (
        context,
        ACTOR_FOCUS_TOOLBAR_DOC_TITLE_CONTEXT |
        ACTOR_FOCUS_TOOLBAR_DOC_AUTHOR_CONTEXT |
        ACTOR_FOCUS_TOOLBAR_ABSTRACT_CONTEXT,
        true);
    });
  registerBehavior (
    "editor.focus.table-properties", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      url target= frozen_document_url (context);
      if (is_none (target)) return false;
      table_properties_pane_show_for (target);
      return true;
    },
    [] (const QTMCommandContext& context) {
      return focus_surface_state (
        context, ACTOR_FOCUS_TOOLBAR_TABLE_CONTEXT, true);
    });
  registerBehavior (
    "editor.focus.cell-properties", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      url target= frozen_document_url (context);
      if (is_none (target)) return false;
      cell_properties_pane_show_for (target);
      return true;
    },
    [] (const QTMCommandContext& context) {
      return focus_surface_state (
        context, ACTOR_FOCUS_TOOLBAR_TABLE_CONTEXT, true);
    });
  registerBehavior (
    "editor.focus.edit-label", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return false;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      if (!focus.valid () ||
          !focus.has (ACTOR_FOCUS_TOOLBAR_HAS_LABEL))
        return false;
      QString current= QString::fromUtf8 (
        focus.focus_label_value.data (),
        static_cast<int> (focus.focus_label_value.size ()));
      bool ok= false;
      QString value= QInputDialog::getText (
        context.shell.data (), QObject::tr ("Label"),
        QObject::tr ("Label:"), QLineEdit::Normal, current, &ok);
      if (!ok) return true;
      QJsonObject action;
      action.insert ("op", "focus-set-label");
      action.insert ("value", value);
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return state;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editorState= proxy->editor_command_state ();
      if (!focus.valid () || !editorState.valid () ||
          focus.has (ACTOR_FOCUS_TOOLBAR_BUFFER) ||
          focus.has (ACTOR_FOCUS_TOOLBAR_CODE_CONTEXT) ||
          focus.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT) ||
          !focus.has (ACTOR_FOCUS_TOOLBAR_HAS_LABEL))
        return state;
      state.available= true;
      state.enabled= !editorState.read_only ();
      return state;
    });
  const QString focusCommands[]= {
    "editor.focus.first-similar",
    "editor.focus.previous-similar",
    "editor.focus.next-similar",
    "editor.focus.last-similar",
    "editor.focus.insert-left",
    "editor.focus.insert-right",
    "editor.focus.insert-up",
    "editor.focus.insert-down",
    "editor.focus.remove-left",
    "editor.focus.remove-right",
    "editor.focus.remove-up",
    "editor.focus.remove-down",
    "editor.focus.exit-left",
    "editor.focus.exit-right",
    "editor.focus.remove-tag",
    "editor.focus.help"
  };
  for (const QString& id: focusCommands)
    registerBehavior (
      id, QTMCommandScope::Editor,
      [id] (const QTMCommandContext& context) {
        return invoke_native_editor_command (id, context);
      },
      [id] (const QTMCommandContext& context) {
        return native_editor_command_state (id, context);
      });
}
