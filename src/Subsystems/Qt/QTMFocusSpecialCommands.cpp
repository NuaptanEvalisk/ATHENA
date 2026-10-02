/******************************************************************************
* MODULE     : QTMFocusSpecialCommands.cpp
* DESCRIPTION: Native specialized structured-focus providers
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
#include <QSet>

using namespace qtm_command_registry_detail;

namespace {

QString
qstring (const std::string& value) {
  return QString::fromUtf8 (
    value.data (), static_cast<int> (value.size ()));
}

bool
writable_focus (
  const QTMCommandContext& context,
  actor_focus_toolbar_snapshot* focus= nullptr) {
  qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
  if (proxy == nullptr) return false;
  actor_focus_toolbar_snapshot current= proxy->focus_toolbar_state ();
  actor_editor_command_snapshot editor= proxy->editor_command_state ();
  if (!current.valid () || !editor.valid () || editor.read_only ()) return false;
  if (focus != nullptr) *focus= std::move (current);
  return true;
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

QTMCommandState
state_if (const QTMCommandContext& context, bool available) {
  QTMCommandState state;
  if (!available) return state;
  qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
  if (proxy == nullptr) return state;
  actor_editor_command_snapshot editor= proxy->editor_command_state ();
  if (!editor.valid ()) return state;
  state.available= true;
  state.enabled= !editor.read_only ();
  return state;
}

} // namespace

void
QTMCommandRegistry::registerFocusSpecialCommands () {
  registerProvider (
    "editor-focus-special-actions", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editor= proxy->editor_command_state ();
      if (!focus.valid () || !editor.valid ()) return out;
      const bool enabled= !editor.read_only ();
      auto append=
        [&] (const QString& key, const QString& label, const QString& icon,
             bool checkable= false, bool checked= false) {
          QTMCommandDynamicItem item= enabled_dynamic_item (key, label);
          item.icon= icon;
          item.state.enabled= enabled;
          item.state.checkable= checkable;
          item.state.checked= checked;
          out.append (std::move (item));
        };
      if (focus.poster_block_context) {
        append (
          QStringLiteral ("poster-block-toggle-titled"),
          QObject::tr ("Toggle titled block"),
          QStringLiteral ("tm_small_textual"), true,
          focus.poster_block_titled);
        append (
          QStringLiteral ("poster-block-toggle-wide"),
          QObject::tr ("Make block wide"),
          QStringLiteral ("tm_wide_float"), true, focus.poster_block_wide);
      }
      if (focus.sqrt_context)
        append (
          QStringLiteral ("sqrt-toggle"), QObject::tr ("Multiple root"),
          QStringLiteral ("tm_root_index"), true, focus.sqrt_multiple);
      if (focus.dueto_available)
        append (
          QStringLiteral ("dueto-add"), QObject::tr ("Due to"),
          QStringLiteral ("tm_add"));
      if (focus.has (ACTOR_FOCUS_TOOLBAR_TABLE_CONTEXT))
        append (
          QStringLiteral ("table-toggle-parwidth"),
          QObject::tr ("Extend table to full paragraph width"),
          QStringLiteral ("tm_table_parwidth"), true, focus.table_parwidth);
      if (focus.automatic_section_context)
        append (
          QStringLiteral ("automatic-section-rename"),
          QObject::tr ("Rename section"),
          QStringLiteral ("tm_small_textual"));
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      if (key == QStringLiteral ("automatic-section-rename")) {
        actor_focus_toolbar_snapshot focus;
        if (!writable_focus (context, &focus) ||
            !focus.automatic_section_context)
          return false;
        bool ok= false;
        QString value= QInputDialog::getText (
          context.shell.data (), QObject::tr ("Rename section"),
          QObject::tr ("New name:"), QLineEdit::Normal, QString (), &ok);
        if (!ok) return true;
        QJsonObject action;
        action.insert ("op", "focus-automatic-section-rename");
        action.insert ("value", value);
        return submit_inline_editor_action (
          context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
      }
      static const QSet<QString> allowed {
        QStringLiteral ("poster-block-toggle-titled"),
        QStringLiteral ("poster-block-toggle-wide"),
        QStringLiteral ("sqrt-toggle"),
        QStringLiteral ("dueto-add"),
        QStringLiteral ("table-toggle-parwidth")
      };
      return allowed.contains (key) ?
        submit_focus_action (context, key) : false;
    },
    [] (const QTMCommandContext& context) {
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return QTMCommandState {};
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      const bool available=
        focus.valid () &&
        (focus.poster_block_context || focus.sqrt_context ||
         focus.dueto_available ||
         focus.has (ACTOR_FOCUS_TOOLBAR_TABLE_CONTEXT) ||
         focus.automatic_section_context);
      return state_if (context, available);
    });

  registerProvider (
    "editor-focus-hidden-fields", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editor= proxy->editor_command_state ();
      if (!focus.valid () || !editor.valid ()) return out;
      for (const auto& field: focus.hidden_fields) {
        QString name= qstring (field.name);
        if (name.isEmpty ()) name= qstring (field.type);
        if (name.isEmpty ()) name= QObject::tr ("Value");
        QString label= name + QStringLiteral (": ") + qstring (field.value);
        QTMCommandDynamicItem item= enabled_dynamic_item (
          QString::number (field.index), label);
        item.state.enabled= !editor.read_only ();
        out.append (std::move (item));
      }
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      bool indexOk= false;
      int index= key.toInt (&indexOk);
      if (!indexOk) return false;
      actor_focus_toolbar_snapshot focus;
      if (!writable_focus (context, &focus)) return false;
      const actor_focus_hidden_field_snapshot* field= nullptr;
      for (const auto& candidate: focus.hidden_fields)
        if (candidate.index == index) {
          field= &candidate;
          break;
        }
      if (field == nullptr) return false;
      QString current= qstring (field->value);
      QString value;
      if (qstring (field->type) == QStringLiteral ("color")) {
        QColor initial (current);
        QColor selected= QColorDialog::getColor (
          initial.isValid () ? initial : Qt::white,
          context.shell.data (), QObject::tr ("Choose color"));
        if (!selected.isValid ()) return true;
        value= selected.name ();
      }
      else {
        QString name= qstring (field->name);
        if (name.isEmpty ()) name= QObject::tr ("Value");
        bool ok= false;
        value= QInputDialog::getText (
          context.shell.data (), name, QObject::tr ("Value:"),
          QLineEdit::Normal, current, &ok);
        if (!ok) return true;
      }
      QJsonObject action;
      action.insert ("op", "focus-hidden-field");
      action.insert ("index", index);
      action.insert ("value", value);
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return QTMCommandState {};
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      return state_if (
        context, focus.valid () && !focus.hidden_fields.empty ());
    });

  registerProvider (
    "editor-focus-table-mode", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editor= proxy->editor_command_state ();
      if (!focus.valid () || !editor.valid () ||
          !focus.has (ACTOR_FOCUS_TOOLBAR_TABLE_CONTEXT))
        return out;
      const QString current= qstring (focus.table_cell_mode);
      const struct { const char* key; const char* label; } modes[]= {
        {"cell", "Cells"}, {"row", "Rows"}, {"column", "Columns"},
        {"table", "Entire table"}
      };
      for (const auto& mode: modes) {
        QTMCommandDynamicItem item= enabled_dynamic_item (
          QString::fromLatin1 (mode.key), QObject::tr (mode.label));
        item.state.enabled= !editor.read_only ();
        item.state.checkable= true;
        item.state.checked= current == QString::fromLatin1 (mode.key);
        out.append (std::move (item));
      }
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      static const QSet<QString> modes {
        QStringLiteral ("cell"), QStringLiteral ("row"),
        QStringLiteral ("column"), QStringLiteral ("table")
      };
      if (!modes.contains (key)) return false;
      QJsonObject action;
      action.insert ("op", "focus-set-cell-mode");
      action.insert ("mode", key);
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return QTMCommandState {};
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      return state_if (
        context, focus.valid () &&
                 focus.has (ACTOR_FOCUS_TOOLBAR_TABLE_CONTEXT));
    });

  registerProvider (
    "editor-focus-effect-pen", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editor= proxy->editor_command_state ();
      if (!focus.valid () || !editor.valid () || !focus.pen_effect_context)
        return out;
      const QString current= qstring (focus.pen_effect);
      const struct { const char* key; const char* label; } pens[]= {
        {"gaussian", "Gaussian"}, {"oval", "Oval"},
        {"rectangular", "Rectangular"}, {"motion", "Motion"}
      };
      for (const auto& pen: pens) {
        QTMCommandDynamicItem item= enabled_dynamic_item (
          QString::fromLatin1 (pen.key), QObject::tr (pen.label));
        item.state.enabled= !editor.read_only ();
        item.state.checkable= true;
        item.state.checked= current == QString::fromLatin1 (pen.key);
        out.append (std::move (item));
      }
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      static const QSet<QString> pens {
        QStringLiteral ("gaussian"), QStringLiteral ("oval"),
        QStringLiteral ("rectangular"), QStringLiteral ("motion")
      };
      if (!pens.contains (key)) return false;
      QJsonObject action;
      action.insert ("op", "focus-set-effect-pen");
      action.insert ("pen", key);
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return QTMCommandState {};
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      return state_if (
        context, focus.valid () && focus.pen_effect_context);
    });

  registerProvider (
    "editor-focus-overlays", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editor= proxy->editor_command_state ();
      if (!focus.valid () || !editor.valid () ||
          (!focus.overlays_context && !focus.overlay_context) ||
          focus.overlay_count <= 0)
        return out;
      for (int i=1; i<=focus.overlay_count; ++i) {
        QTMCommandDynamicItem item= enabled_dynamic_item (
          QStringLiteral ("switch:%1").arg (i),
          QObject::tr ("Overlay %1").arg (i));
        item.group= QObject::tr ("Current overlay");
        item.state.enabled= !editor.read_only ();
        item.state.checkable= true;
        item.state.checked= focus.overlay_current == i;
        out.append (std::move (item));
      }
      if (focus.overlay_context)
        for (int i=1; i<=focus.overlay_count; ++i) {
          bool visible=
            i <= static_cast<int> (focus.overlay_visible.size ()) ?
              focus.overlay_visible[static_cast<std::size_t> (i - 1)] : true;
          QString label= QObject::tr ("Overlay %1").arg (i);
          if (!visible) label += QObject::tr (" (hidden)");
          QTMCommandDynamicItem item= enabled_dynamic_item (
            QStringLiteral ("reference:%1").arg (i), label);
          item.group= QObject::tr ("Filter reference");
          item.state.enabled= !editor.read_only ();
          item.state.checkable= true;
          item.state.checked= focus.overlay_reference == i;
          out.append (std::move (item));
        }
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      QStringList parts= key.split (':');
      if (parts.size () != 2) return false;
      bool ok= false;
      int index= parts[1].toInt (&ok);
      if (!ok || index < 1) return false;
      QJsonObject action;
      if (parts[0] == QStringLiteral ("switch"))
        action.insert ("op", "focus-overlay-switch");
      else if (parts[0] == QStringLiteral ("reference"))
        action.insert ("op", "focus-overlay-reference");
      else return false;
      action.insert ("index", index);
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return QTMCommandState {};
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      return state_if (
        context, focus.valid () &&
                 (focus.overlays_context || focus.overlay_context) &&
                 focus.overlay_count > 0);
    });

  registerProvider (
    "editor-focus-embedded-image", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editor= proxy->editor_command_state ();
      if (!focus.valid () || !editor.valid ()) return out;
      const bool enabled= !editor.read_only ();
      auto append= [&] (const QString& key, const QString& label) {
        QTMCommandDynamicItem item= enabled_dynamic_item (key, label);
        item.state.enabled= enabled;
        out.append (std::move (item));
      };
      if (focus.embedded_image_context) {
        append (QStringLiteral ("save-as"), QObject::tr ("Save image as..."));
        append (QStringLiteral ("link-as"), QObject::tr ("Link image as..."));
        append (
          QStringLiteral ("link-copies-as"),
          QObject::tr ("Link image and copies as..."));
        append (
          QStringLiteral ("save-all"),
          QObject::tr ("Save all embedded images"));
        append (
          QStringLiteral ("link-all"),
          QObject::tr ("Link all embedded images"));
      }
      else if (focus.linked_image_context) {
        append (QStringLiteral ("embed-this"), QObject::tr ("Embed this image"));
        append (
          QStringLiteral ("embed-all"),
          QObject::tr ("Embed all linked images"));
      }
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      actor_focus_toolbar_snapshot focus;
      if (!writable_focus (context, &focus)) return false;
      static const QSet<QString> pathActions {
        QStringLiteral ("save-as"), QStringLiteral ("link-as"),
        QStringLiteral ("link-copies-as")
      };
      static const QSet<QString> allActions {
        QStringLiteral ("save-as"), QStringLiteral ("link-as"),
        QStringLiteral ("link-copies-as"), QStringLiteral ("save-all"),
        QStringLiteral ("link-all"), QStringLiteral ("embed-this"),
        QStringLiteral ("embed-all")
      };
      if (!allActions.contains (key)) return false;
      QJsonObject action;
      action.insert ("op", "focus-embedded-image");
      action.insert ("kind", key);
      if (pathActions.contains (key)) {
        QString proposal= qstring (focus.embedded_image_proposal);
        QString path= QFileDialog::getSaveFileName (
          context.shell.data (), QObject::tr ("Save embedded image"),
          proposal);
        if (path.isEmpty ()) return true;
        action.insert ("path", path);
      }
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return QTMCommandState {};
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      return state_if (
        context, focus.valid () &&
                 (focus.embedded_image_context || focus.linked_image_context));
    });

  registerProvider (
    "editor-focus-sections", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      if (!focus.valid () || !focus.section_navigation_available) return out;
      for (std::size_t i=0; i<focus.section_names.size (); ++i)
        out.append (enabled_dynamic_item (
          QString::number (static_cast<qulonglong> (i)),
          qstring (focus.section_names[i])));
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      bool ok= false;
      int index= key.toInt (&ok);
      if (!ok || index < 0) return false;
      QJsonObject action;
      action.insert ("op", "focus-section-switch");
      action.insert ("index", index);
      return submit_inline_editor_action (context, action);
    },
    [] (const QTMCommandContext& context) {
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return QTMCommandState {};
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      QTMCommandState state;
      if (!focus.valid () || !focus.section_navigation_available) return state;
      state.available= true;
      state.enabled= true;
      return state;
    });

  registerProvider (
    "editor-focus-text-data", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editor= proxy->editor_command_state ();
      if (!focus.valid () || !editor.valid ()) return out;
      const bool title=
        focus.has (ACTOR_FOCUS_TOOLBAR_DOC_TITLE_CONTEXT) ||
        focus.has (ACTOR_FOCUS_TOOLBAR_DOC_AUTHOR_CONTEXT);
      const bool author= focus.has (ACTOR_FOCUS_TOOLBAR_DOC_AUTHOR_CONTEXT);
      const bool abstract= focus.has (ACTOR_FOCUS_TOOLBAR_ABSTRACT_CONTEXT);
      if (!title && !author && !abstract) return out;
      const bool enabled= !editor.read_only ();
      auto append=
        [&] (const QString& key, const QString& label, const QString& group,
             bool checkable= false, bool checked= false) {
          QTMCommandDynamicItem item= enabled_dynamic_item (key, label);
          item.group= group;
          item.state.enabled= enabled;
          item.state.checkable= checkable;
          item.state.checked= checked;
          out.append (std::move (item));
        };
      if (title) {
        if (focus.title_hidden_available)
          append (
            QStringLiteral ("title-hidden-toggle"), QObject::tr ("Show hidden"),
            QObject::tr ("Title"), true, focus.title_hidden_checked);
        const struct { const char* key; const char* label; } fields[]= {
          {"doc-subtitle", "Subtitle"}, {"doc-author", "Author"},
          {"doc-date", "Date"}, {"today", "Today"},
          {"doc-misc", "Miscellaneous"}, {"doc-note", "Note"}
        };
        for (const auto& field: fields)
          append (
            QStringLiteral ("title:%1").arg (field.key),
            QObject::tr (field.label), QObject::tr ("Add title information"));
        append (
          QStringLiteral ("title:doc-running-title"),
          QObject::tr ("Running title"), QObject::tr ("Hidden title information"));
        append (
          QStringLiteral ("title:doc-running-author"),
          QObject::tr ("Running author"), QObject::tr ("Hidden title information"));
        const struct { const char* key; const char* label; } clustering[]= {
          {"none", "No clustering"},
          {"affiliation", "Cluster by affiliation"},
          {"all", "Maximal clustering"}
        };
        for (const auto& mode: clustering)
          append (
            QStringLiteral ("cluster:%1").arg (mode.key),
            QObject::tr (mode.label), QObject::tr ("Title presentation"),
            true, qstring (focus.title_clustering) == mode.key);
      }
      if (author) {
        const struct { const char* key; const char* label; } fields[]= {
          {"author-affiliation", "Affiliation"}, {"author-email", "Email"},
          {"author-homepage", "Homepage"}, {"author-misc", "Miscellaneous"},
          {"author-note", "Note"}
        };
        for (const auto& field: fields)
          append (
            QStringLiteral ("author:%1").arg (field.key),
            QObject::tr (field.label), QObject::tr ("Add author information"));
      }
      if (abstract) {
        const struct { const char* key; const char* label; } fields[]= {
          {"abstract-arxiv", "Arxiv category"},
          {"abstract-acm", "A.C.M. computing class"},
          {"abstract-msc", "A.M.S. subject class"},
          {"abstract-pacs", "Physics and astronomy class"},
          {"abstract-keywords", "Keywords"}
        };
        for (const auto& field: fields)
          append (
            QStringLiteral ("abstract:%1").arg (field.key),
            QObject::tr (field.label), QObject::tr ("Add abstract information"));
      }
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      QJsonObject action;
      action.insert ("op", "focus-text-data");
      if (key == QStringLiteral ("title-hidden-toggle")) {
        action.insert ("kind", "title-hidden-toggle");
      }
      else {
        QStringList parts= key.split (':');
        if (parts.size () != 2) return false;
        if (parts[0] == QStringLiteral ("title"))
          action.insert ("kind", "title-element");
        else if (parts[0] == QStringLiteral ("author"))
          action.insert ("kind", "author-element");
        else if (parts[0] == QStringLiteral ("abstract"))
          action.insert ("kind", "abstract-element");
        else if (parts[0] == QStringLiteral ("cluster"))
          action.insert ("kind", "title-clustering");
        else
          return false;
        action.insert ("value", parts[1]);
      }
      return submit_inline_editor_action (
        context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
    },
    [] (const QTMCommandContext& context) {
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return QTMCommandState {};
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      const bool available=
        focus.valid () &&
        (focus.has (ACTOR_FOCUS_TOOLBAR_DOC_TITLE_CONTEXT) ||
         focus.has (ACTOR_FOCUS_TOOLBAR_DOC_AUTHOR_CONTEXT) ||
         focus.has (ACTOR_FOCUS_TOOLBAR_ABSTRACT_CONTEXT));
      return state_if (context, available);
    });

  registerProvider (
    "editor-focus-document-extras", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      actor_editor_command_snapshot editor= proxy->editor_command_state ();
      if (!focus.valid () || !editor.valid () ||
          !focus.has (ACTOR_FOCUS_TOOLBAR_BUFFER))
        return out;
      const bool enabled= !editor.read_only ();
      auto append= [&] (const QString& key, const QString& label) {
        QTMCommandDynamicItem item= enabled_dynamic_item (key, label);
        item.state.enabled= enabled;
        out.append (std::move (item));
      };
      if (focus.tmdoc_insert_title_available)
        append (QStringLiteral ("tmdoc-insert-title"), QObject::tr ("Title"));
      else if (focus.poster_insert_title_available)
        append (QStringLiteral ("poster-insert-title"), QObject::tr ("Title"));
      else if (focus.document_insert_title_available)
        append (QStringLiteral ("document-insert-title"), QObject::tr ("Title"));
      if (focus.document_insert_abstract_available)
        append (
          QStringLiteral ("document-insert-abstract"),
          QObject::tr ("Abstract"));
      if (focus.tmdoc_insert_copyright_available)
        append (
          QStringLiteral ("tmdoc-insert-copyright"),
          QObject::tr ("Copyright"));
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      static const QSet<QString> allowed {
        QStringLiteral ("document-insert-title"),
        QStringLiteral ("document-insert-abstract"),
        QStringLiteral ("poster-insert-title"),
        QStringLiteral ("tmdoc-insert-title"),
        QStringLiteral ("tmdoc-insert-copyright")
      };
      return allowed.contains (key) ?
        submit_focus_action (context, key) : false;
    },
    [] (const QTMCommandContext& context) {
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return QTMCommandState {};
      actor_focus_toolbar_snapshot focus= proxy->focus_toolbar_state ();
      const bool available=
        focus.valid () && focus.has (ACTOR_FOCUS_TOOLBAR_BUFFER) &&
        (focus.document_insert_title_available ||
         focus.document_insert_abstract_available ||
         focus.poster_insert_title_available ||
         focus.tmdoc_insert_title_available ||
         focus.tmdoc_insert_copyright_available);
      return state_if (context, available);
    });
}
