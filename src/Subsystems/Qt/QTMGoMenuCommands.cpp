/******************************************************************************
* MODULE     : QTMGoMenuCommands.cpp
* DESCRIPTION: Native Go menubar provider
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "QTMCommandRegistry.hpp"
#include "QTMCommandRegistryInternal.hpp"
#include "new_buffer.hpp"
#include "new_view.hpp"
#include "new_window.hpp"
#include "vault.hpp"
#include "vault_frontend.hpp"

#include <QRandomGenerator>
#include <QSet>

#include <algorithm>

using namespace qtm_command_registry_detail;

namespace {

QString
url_key (url value) {
  return to_qstring (as_string (value));
}

bool
buffer_in_go_menu (url value) {
  if (!is_rooted_tmfs (value)) return true;
  string identity= as_string (value);
  return starts (identity, "tmfs://part/") ||
         starts (identity, "tmfs://help/") ||
         starts (identity, "tmfs://apidoc/");
}

QString
buffer_label (url value) {
  string title= get_title_buffer (value);
  QString label= to_qstring (title).trimmed ();
  if (label.isEmpty ()) label= to_qstring (as_string (tail (value)));
  if (buffer_menu_modified (value)) label += QStringLiteral (" *");
  return label;
}

QVector<url>
sorted_menu_buffers (int limit) {
  array<url> raw= get_all_buffers ();
  QVector<url> values;
  values.reserve (N(raw));
  for (int i=0; i<N(raw); ++i)
    if (buffer_in_go_menu (raw[i])) values.append (raw[i]);
  std::stable_sort (
    values.begin (), values.end (),
    [] (url left, url right) {
      return last_visited (left) > last_visited (right);
    });
  if (limit >= 0 && values.size () > limit) values.resize (limit);
  return values;
}

QSet<QString>
loaded_buffer_keys () {
  QSet<QString> out;
  array<url> values= get_all_buffers ();
  for (int i=0; i<N(values); ++i) out.insert (url_key (values[i]));
  return out;
}

QSet<QString>
window_buffer_keys () {
  QSet<QString> out;
  array<url> windows= windows_list ();
  for (int i=0; i<N(windows); ++i) {
    url buffer= window_to_buffer (windows[i]);
    if (!is_none (buffer)) out.insert (url_key (buffer));
  }
  return out;
}

bool
switch_to_buffer_like_legacy (url target) {
  if (is_none (target)) return false;
  array<url> windows= buffer_to_windows (target);
  if (N(windows) > 0) switch_to_window (windows[0]);
  else switch_to_buffer (target);
  try { (void) call ("schedule-persistent-fit-width"); }
  catch (...) {}
  return true;
}

void
append_item (
  QVector<QTMCommandDynamicItem>& out, QString key, QString label,
  QString group= QString (), bool enabled= true, bool checkable= false,
  bool checked= false, QString help= QString ()) {
  QTMCommandDynamicItem item=
    enabled_dynamic_item (std::move (key), std::move (label), std::move (help));
  item.group= std::move (group);
  item.state.enabled= enabled;
  item.state.checkable= checkable;
  item.state.checked= checked;
  out.append (std::move (item));
}

bool
submit_save_position (const QTMCommandContext& context) {
  QJsonObject action;
  action.insert ("op", "business");
  action.insert ("id", "go-save-position");
  return submit_inline_editor_action (context, action);
}

} // namespace

void
QTMCommandRegistry::registerGoMenuCommands () {
  registerProvider (
    "go-navigation", QTMCommandScope::Application,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      append_item (
        out, "welcome/system", QObject::tr ("Welcome (System)"));
      append_item (
        out, "welcome/vault", QObject::tr ("Welcome (Vault)"),
        QString (), vault_active ());
      append_item (
        out, "random", QObject::tr ("Random document"),
        QString (), vault_active ());

      QTMCommandRegistry& registry= QTMCommandRegistry::instance ();
      QTMCommandState back=
        registry.state (QStringLiteral ("editor.history-back"), context);
      if (back.available)
        append_item (
          out, "history/back", QObject::tr ("Back"),
          QObject::tr ("History"), back.enabled);
      QTMCommandState forward=
        registry.state (QStringLiteral ("editor.history-forward"), context);
      if (forward.available)
        append_item (
          out, "history/forward", QObject::tr ("Forward"),
          QObject::tr ("History"), forward.enabled);
      if (editor_proxy_for_context (context) != nullptr)
        append_item (
          out, "history/save-position", QObject::tr ("Save position"),
          QObject::tr ("History"));

      const QString current=
        context.lastDocument.has_buffer_name () ?
          QString::fromUtf8 (
            context.lastDocument.native_url_name.data (),
            static_cast<int> (
              context.lastDocument.native_url_name.size ())):
          QString ();
      QSet<QString> windowBuffers= window_buffer_keys ();

      QVector<url> menuBuffers= sorted_menu_buffers (15);
      for (url buffer: menuBuffers) {
        const QString key= url_key (buffer);
        // Legacy buffer-go-menu omits buffers active in another window while
        // retaining the current buffer.
        if (windowBuffers.contains (key) && key != current) continue;
        append_item (
          out, QStringLiteral ("buffer/") + key, buffer_label (buffer),
          QObject::tr ("Buffers"), true, true, key == current, key);
      }

      array<url> windows= windows_list ();
      for (int i=0; i<N(windows); ++i) {
        url buffer= window_to_buffer (windows[i]);
        if (is_none (buffer)) continue;
        QString bufferKey= url_key (buffer);
        append_item (
          out, QStringLiteral ("window/") + url_key (windows[i]),
          buffer_label (buffer), QObject::tr ("Windows"), true, true,
          bufferKey == current, bufferKey);
      }

      QVector<url> hidden= sorted_menu_buffers (25);
      for (url buffer: hidden) {
        const QString key= url_key (buffer);
        if (windowBuffers.contains (key)) continue;
        append_item (
          out, QStringLiteral ("hidden/") + key, buffer_label (buffer),
          QObject::tr ("Hidden"), true, true, key == current, key);
      }

      QSet<QString> loaded= loaded_buffer_keys ();
      try {
        QVector<QString> data= scheme_string_vector (
          call ("native-recent-file-provider-data", object (15)));
        for (int i=0; i + 2 < data.size (); i += 3) {
          const QString key= data[i];
          if (loaded.contains (key)) continue;
          QString label= data[i + 1].trimmed ();
          if (label.isEmpty ()) label= key;
          append_item (
            out, QStringLiteral ("recent/") + key, label,
            QObject::tr ("Recent Files"), true, false, false, data[i + 2]);
        }
      }
      catch (...) {}
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      if (key == QStringLiteral ("welcome/system")) {
        go_to_system_welcome_page_native ();
        return true;
      }
      if (key == QStringLiteral ("welcome/vault")) {
        if (!vault_active ()) return false;
        go_to_welcome_page_native ();
        return true;
      }
      if (key == QStringLiteral ("random")) {
        if (!vault_active ()) return false;
        array<url> all= vault_get_all_files ();
        QVector<url> documents;
        for (int i=0; i<N(all); ++i)
          if (ends (as_string (tail (all[i])), ".ath"))
            documents.append (all[i]);
        if (documents.isEmpty ()) return false;
        int index= QRandomGenerator::global ()->bounded (documents.size ());
        return switch_to_buffer_like_legacy (documents[index]);
      }
      if (key == QStringLiteral ("history/back"))
        return QTMCommandRegistry::instance ().execute (
          QStringLiteral ("editor.history-back"), context);
      if (key == QStringLiteral ("history/forward"))
        return QTMCommandRegistry::instance ().execute (
          QStringLiteral ("editor.history-forward"), context);
      if (key == QStringLiteral ("history/save-position"))
        return submit_save_position (context);

      if (key.startsWith (QStringLiteral ("window/"))) {
        url target (from_qstring (key.mid (7)));
        if (is_none (target)) return false;
        switch_to_window (target);
        try { (void) call ("schedule-persistent-fit-width"); }
        catch (...) {}
        return true;
      }
      for (const QString& prefix:
           {QStringLiteral ("buffer/"), QStringLiteral ("hidden/"),
            QStringLiteral ("recent/")}) {
        if (!key.startsWith (prefix)) continue;
        url target (from_qstring (key.mid (prefix.size ())));
        return switch_to_buffer_like_legacy (target);
      }
      return false;
    },
    [] (const QTMCommandContext&) {
      QTMCommandState state;
      state.available= true;
      state.enabled= true;
      return state;
    });
}
