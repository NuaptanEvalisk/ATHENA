/******************************************************************************
* MODULE     : QTMViewMenuCommands.cpp
* DESCRIPTION: Native View menubar provider
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "QTMCommandRegistry.hpp"
#include "QTMCommandRegistryInternal.hpp"
#include "QTMReverseHierarchyGraph.hpp"
#include "boot.hpp"
#include "vault.hpp"

#include <QInputDialog>
#include <QJsonObject>

#include <cmath>

using namespace qtm_command_registry_detail;

namespace {

QString
qs (const std::string& value) {
  return QString::fromUtf8 (
    value.data (), static_cast<int> (value.size ()));
}

void
append_item (
  QVector<QTMCommandDynamicItem>& out, const QString& key,
  const QString& label, const QString& group, bool enabled,
  bool checkable= false, bool checked= false) {
  QTMCommandDynamicItem item= enabled_dynamic_item (key, label);
  item.group= group;
  item.state.enabled= enabled;
  item.state.checkable= checkable;
  item.state.checked= checked;
  out.append (std::move (item));
}

bool
submit_view_business (
  const QTMCommandContext& context, const QString& id) {
  QJsonObject action;
  action.insert ("op", "business");
  action.insert ("id", id);
  return submit_inline_editor_action (context, action);
}

bool
preference_on (const char* name) {
  return get_user_preference (string (name), "off") == "on";
}

QString
document_identity (const QTMCommandContext& context) {
  return context.lastDocument.has_buffer_name () ?
    QString::fromUtf8 (
      context.lastDocument.native_url_name.data (),
      static_cast<int> (context.lastDocument.native_url_name.size ())):
    QString ();
}

} // namespace

void
QTMCommandRegistry::registerViewMenuCommands () {
  registerProvider (
    "view-controls", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_editor_command_snapshot editor= proxy->editor_command_state ();
      actor_document_menu_snapshot document= proxy->document_menu_state ();
      actor_viewport_snapshot viewport= proxy->viewport_state ();
      if (!editor.valid () || !document.ready || !viewport.attached) return out;

      append_item (
        out, "full-screen-edit", QObject::tr ("Full screen mode"),
        QObject::tr ("Display"), true, true, viewport.full_screen_edit);
      append_item (
        out, "full-screen", QObject::tr ("Presentation mode"),
        QObject::tr ("Display"), true, true, viewport.full_screen);
      append_item (
        out, "panorama", QObject::tr ("Show panorama"),
        QObject::tr ("Display"), true, true,
        qs (document.page_rendering) == QStringLiteral ("panorama"));
      append_item (
        out, "slideshow", QObject::tr ("Show all slides"),
        QObject::tr ("Display"), true, true,
        qs (document.page_rendering) == QStringLiteral ("slideshow"));
      append_item (
        out, "heading-unfold-all", QObject::tr ("Unfold all"),
        QObject::tr ("Headings"), true);

      append_item (
        out, "fit-screen", QObject::tr ("Fit to screen"),
        QObject::tr ("Viewport"), true);
      append_item (
        out, "fit-width", QObject::tr ("Fit to screen width"),
        QObject::tr ("Viewport"), true);
      const QString rendering= qs (document.page_rendering);
      const bool persistentApplicable=
        rendering == QStringLiteral ("paper") ||
        rendering == QStringLiteral ("papyrus");
      append_item (
        out, "persistent-fit-width", QObject::tr ("Persistent fit width"),
        QObject::tr ("Viewport"), persistentApplicable, true,
        persistentApplicable && preference_on ("persistent fit width"));
      append_item (
        out, "typewriter", QObject::tr ("Typewriter mode"),
        QObject::tr ("Viewport"), true, true,
        preference_on ("typewriter mode"));
      append_item (
        out, "snap-pages", QObject::tr ("Snap to pages"),
        QObject::tr ("Viewport"), true, true,
        preference_on ("snap to pages"));

      string labelsPreference=
        get_user_preference ("vault labels mode", "visible");
      QString labels= QString::fromUtf8 (
        labelsPreference.data (), N(labelsPreference));
      for (const auto& entry:
           std::initializer_list<std::pair<const char*,const char*>> {
             {"visible", "Visible"}, {"small", "Small"}, {"hidden", "Hidden"}})
        append_item (
          out, QStringLiteral ("labels/%1").arg (entry.first),
          QObject::tr (entry.second), QObject::tr ("Labels"), true, true,
          labels == QString::fromLatin1 (entry.first));

      const bool hasDocument= context.lastDocument.has_buffer_name ();
      append_item (
        out, "graph/global", QObject::tr ("Global hierarchy graph"),
        QObject::tr ("Graphs"), vault_active ());
      append_item (
        out, "graph/reverse", QObject::tr ("Reverse hierarchy graph"),
        QObject::tr ("Graphs"), hasDocument);
      append_item (
        out, "graph/direct", QObject::tr ("Direct hierarchy graph"),
        QObject::tr ("Graphs"), hasDocument);
      append_item (
        out, "graph/local-reference", QObject::tr ("Local reference graph"),
        QObject::tr ("Graphs"), hasDocument);
      append_item (
        out, "graph/reference", QObject::tr ("Reference graph"),
        QObject::tr ("Graphs"), hasDocument);

      const double zoom= proxy->handle_get_zoom_factor ();
      append_item (
        out, "zoom/in", QObject::tr ("Zoom in"), QObject::tr ("Zoom"), true);
      append_item (
        out, "zoom/out", QObject::tr ("Zoom out"), QObject::tr ("Zoom"), true);
      static const struct { const char* key; double factor; } zooms[]= {
        {"50%", 0.5},
        {"71%", 0.7071067811865476},
        {"100%", 1.0},
        {"141%", 1.4142135623730951},
        {"200%", 2.0}
      };
      for (const auto& entry: zooms)
        append_item (
          out, QStringLiteral ("zoom/%1").arg (entry.key),
          QString::fromLatin1 (entry.key), QObject::tr ("Zoom"),
          true, true, std::abs (zoom - entry.factor) <= 0.01);
      append_item (
        out, "zoom/other", QObject::tr ("Other..."),
        QObject::tr ("Zoom"), true);
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return false;
      if (key == QStringLiteral ("full-screen-edit"))
        return submit_view_business (context, "view-toggle-full-screen-edit");
      if (key == QStringLiteral ("full-screen"))
        return submit_view_business (context, "view-toggle-full-screen");
      if (key == QStringLiteral ("panorama"))
        return submit_view_business (context, "view-toggle-panorama");
      if (key == QStringLiteral ("slideshow"))
        return submit_view_business (context, "view-toggle-slideshow");
      if (key == QStringLiteral ("heading-unfold-all"))
        return submit_view_business (context, "view-heading-unfold-all");
      if (key == QStringLiteral ("fit-screen"))
        return submit_view_business (context, "view-fit-screen");
      if (key == QStringLiteral ("fit-width"))
        return submit_view_business (context, "view-fit-width");
      if (key == QStringLiteral ("persistent-fit-width"))
        return submit_view_business (
          context, "view-toggle-persistent-fit-width");
      if (key == QStringLiteral ("typewriter"))
        return submit_view_business (context, "view-toggle-typewriter");
      if (key == QStringLiteral ("snap-pages"))
        return submit_view_business (context, "view-toggle-snap-pages");

      if (key.startsWith (QStringLiteral ("labels/"))) {
        QString mode= key.mid (7);
        if (mode != QStringLiteral ("visible") &&
            mode != QStringLiteral ("small") &&
            mode != QStringLiteral ("hidden"))
          return false;
        set_user_preference (
          "vault labels mode", from_qstring (mode));
        return true;
      }
      if (key.startsWith (QStringLiteral ("graph/"))) {
        QString kind= key.mid (6);
        if (kind == QStringLiteral ("global")) {
          global_hierarchy_graph_show ();
          return true;
        }
        QString identity= document_identity (context);
        if (identity.isEmpty ()) return false;
        string nativeIdentity= from_qstring (identity);
        if (kind == QStringLiteral ("reverse"))
          reverse_hierarchy_graph_show_document (nativeIdentity);
        else if (kind == QStringLiteral ("direct"))
          direct_hierarchy_graph_show_document (nativeIdentity);
        else if (kind == QStringLiteral ("local-reference"))
          local_reference_graph_show_document (nativeIdentity);
        else if (kind == QStringLiteral ("reference"))
          reference_graph_show_document (nativeIdentity);
        else
          return false;
        return true;
      }
      if (key == QStringLiteral ("zoom/in")) {
        proxy->handle_zoom_by (true, std::sqrt (std::sqrt (2.0)));
        return true;
      }
      if (key == QStringLiteral ("zoom/out")) {
        proxy->handle_zoom_by (false, std::sqrt (std::sqrt (2.0)));
        return true;
      }
      if (key.startsWith (QStringLiteral ("zoom/"))) {
        QString value= key.mid (5);
        double factor= 0.0;
        if (value == QStringLiteral ("50%")) factor= 0.5;
        else if (value == QStringLiteral ("71%"))
          factor= 0.7071067811865476;
        else if (value == QStringLiteral ("100%")) factor= 1.0;
        else if (value == QStringLiteral ("141%"))
          factor= 1.4142135623730951;
        else if (value == QStringLiteral ("200%")) factor= 2.0;
        else if (value == QStringLiteral ("other")) {
          bool ok= false;
          double percent= QInputDialog::getDouble (
            context.shell.data (), QObject::tr ("Zoom"),
            QObject::tr ("Zoom percentage:"),
            proxy->handle_get_zoom_factor () * 100.0,
            4.0, 2500.0, 1, &ok);
          if (!ok) return true;
          factor= percent / 100.0;
        }
        else
          return false;
        proxy->handle_change_zoom_factor (factor);
        return true;
      }
      return false;
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return state;
      actor_editor_command_snapshot editor= proxy->editor_command_state ();
      if (!editor.valid ()) return state;
      state.available= true;
      state.enabled= true;
      return state;
    });
}
