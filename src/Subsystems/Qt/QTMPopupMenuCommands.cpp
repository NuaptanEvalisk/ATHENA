/******************************************************************************
* MODULE     : QTMPopupMenuCommands.cpp
* DESCRIPTION: Native editor context-menu providers
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "QTMCommandRegistry.hpp"
#include "QTMCommandRegistryInternal.hpp"
#include "vault.hpp"
#include "ATHENA/Data/artifact_radioactive_links.hpp"
#include "ATHENA/Data/artifact_title_filter.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "tm_ostream.hpp"

#include <QJsonObject>
#include <QMessageBox>

using namespace qtm_command_registry_detail;

namespace {

QString
qs (const std::string& value) {
  return QString::fromUtf8 (
    value.data (), static_cast<int> (value.size ()));
}

QTMCommandDynamicItem
popup_item (
  QString key, QString label, bool enabled= true,
  bool checkable= false, bool checked= false) {
  QTMCommandDynamicItem item= enabled_dynamic_item (
    std::move (key), std::move (label));
  item.state.enabled= enabled;
  item.state.checkable= checkable;
  item.state.checked= checked;
  return item;
}

bool
submit_business (
  const QTMCommandContext& context, const QString& id) {
  QJsonObject action;
  action.insert ("op", "business");
  action.insert ("id", id);
  return submit_inline_editor_action (context, action);
}

} // namespace

void
QTMCommandRegistry::registerPopupMenuCommands () {
  registerProvider (
    "popup-reject-artifact", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      auto* proxy= editor_proxy_for_context (context);
      if (!proxy) return out;
      auto popup= proxy->popup_menu_state ();
      auto vault= vault_capture_context ();
      std::string fragment, display;
      if (vault && vault->incarnation == popup.vault_incarnation &&
          athena_artifact_radioactive_name_for_key (
            popup.artifact_name_key, fragment, display))
        out.append (popup_item (
          qs (popup.artifact_name_key), QObject::tr ("Reject artifact name")));
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      auto* proxy= editor_proxy_for_context (context);
      if (!proxy) return false;
      auto popup= proxy->popup_menu_state ();
      auto vault= vault_capture_context ();
      if (!vault || vault->incarnation != popup.vault_incarnation ||
          key != qs (popup.artifact_name_key)) return false;
      std::string fragment, display, error;
      if (!athena_artifact_radioactive_name_for_key (
            popup.artifact_name_key, fragment, display)) return false;
      try {
        auto name= athena::document::read_xml (
          fragment, athena::document::xml_kind::fragment);
        if (athena_artifact_title_filter_reject (vault->root, name, error))
          return true;
      }
      catch (const std::exception& e) { error= e.what (); }
      std_error << "Could not reject artifact name: " << string (error.c_str ()) << LF;
      QMessageBox::warning (nullptr, QObject::tr ("Artifactization"), qs (error));
      return false;
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state;
      auto* proxy= editor_proxy_for_context (context);
      auto vault= vault_capture_context ();
      if (!proxy || !vault) return state;
      auto popup= proxy->popup_menu_state ();
      state.available= !popup.artifact_name_key.empty () &&
        popup.vault_incarnation == vault->incarnation;
      state.enabled= state.available;
      return state;
    });

  registerProvider (
    "popup-presentation", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_viewport_snapshot viewport= proxy->viewport_state ();
      actor_document_menu_snapshot document= proxy->document_menu_state ();
      if (!viewport.attached || !viewport.full_screen || !document.ready)
        return out;
      out.append (popup_item (
        "full-screen", QObject::tr ("Presentation mode"),
        true, true, viewport.full_screen));
      out.append (popup_item (
        "panorama", QObject::tr ("Show panorama"), true, true,
        qs (document.page_rendering) == QStringLiteral ("panorama")));
      out.append (popup_item (
        "slideshow", QObject::tr ("Show all slides"), true, true,
        qs (document.page_rendering) == QStringLiteral ("slideshow")));
      out.append (popup_item (
        "remote-control", QObject::tr ("Remote control")));
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      if (key == QStringLiteral ("full-screen"))
        return submit_business (context, "view-toggle-full-screen");
      if (key == QStringLiteral ("panorama"))
        return submit_business (context, "view-toggle-panorama");
      if (key == QStringLiteral ("slideshow"))
        return submit_business (context, "view-toggle-slideshow");
      if (key == QStringLiteral ("remote-control"))
        return submit_business (context, "view-toggle-remote-control");
      return false;
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return state;
      actor_viewport_snapshot viewport= proxy->viewport_state ();
      state.available= viewport.attached && viewport.full_screen;
      state.enabled= state.available;
      return state;
    });

  registerProvider (
    "popup-artifact", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_editor_command_snapshot editor= proxy->editor_command_state ();
      if (!editor.valid () || !editor.has (ACTOR_EDITOR_COMMAND_STATE_SELECTION) ||
          !vault_active ())
        return out;
      out.append (popup_item (
        "resolve", QObject::tr ("Resolve as artifact name")));
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      return key == QStringLiteral ("resolve") &&
             submit_business (context, "popup-resolve-artifact");
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return state;
      actor_editor_command_snapshot editor= proxy->editor_command_state ();
      state.available=
        editor.valid () &&
        editor.has (ACTOR_EDITOR_COMMAND_STATE_SELECTION) &&
        vault_active ();
      state.enabled= state.available;
      return state;
    });

  registerProvider (
    "popup-formula-ast", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_editor_command_snapshot editor= proxy->editor_command_state ();
      if (!editor.valid () || !editor.has (ACTOR_EDITOR_COMMAND_STATE_MATH_MODE))
        return out;
      out.append (popup_item ("inspect", QObject::tr ("Inspect AST")));
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      return key == QStringLiteral ("inspect") &&
             submit_business (context, "popup-formula-ast");
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return state;
      actor_editor_command_snapshot editor= proxy->editor_command_state ();
      state.available=
        editor.valid () &&
        editor.has (ACTOR_EDITOR_COMMAND_STATE_MATH_MODE);
      state.enabled= state.available;
      return state;
    });

  registerProvider (
    "popup-spell", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_popup_menu_snapshot popup= proxy->popup_menu_state ();
      if (!popup.ready || popup.spell_word.empty ()) return out;
      for (std::size_t i= 0; i < popup.spell_suggestions.size (); ++i)
        out.append (popup_item (
          QStringLiteral ("replace/%1").arg (i),
          qs (popup.spell_suggestions[i])));
      out.append (popup_item (
        "add",
        QObject::tr ("Add '%1' to dictionary").arg (qs (popup.spell_word))));
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return false;
      actor_popup_menu_snapshot popup= proxy->popup_menu_state ();
      if (!popup.ready || popup.spell_word.empty ()) return false;
      if (key == QStringLiteral ("add"))
        return submit_business (context, "popup-spell-add");
      if (!key.startsWith (QStringLiteral ("replace/"))) return false;
      bool ok= false;
      int index= key.mid (8).toInt (&ok);
      if (!ok || index < 0 ||
          index >= static_cast<int> (popup.spell_suggestions.size ()))
        return false;
      QJsonObject action;
      action.insert ("op", "popup-spell-replace");
      action.insert (
        "replacement",
        qs (popup.spell_suggestions[static_cast<std::size_t> (index)]));
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return state;
      actor_popup_menu_snapshot popup= proxy->popup_menu_state ();
      state.available= popup.ready && !popup.spell_word.empty ();
      state.enabled= state.available;
      return state;
    });
}
