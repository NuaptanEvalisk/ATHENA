/******************************************************************************
* MODULE     : QTMDocumentMenuCommands.cpp
* DESCRIPTION: Native Document menubar provider
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
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
#include <QSet>

using namespace qtm_command_registry_detail;

namespace {

QString
qs (const std::string& value) {
  return QString::fromUtf8 (
    value.data (), static_cast<int> (value.size ()));
}

QTMCommandState
document_menu_state (const QTMCommandContext& context) {
  QTMCommandState state;
  qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
  if (proxy == nullptr) return state;
  actor_editor_command_snapshot editor= proxy->editor_command_state ();
  actor_document_menu_snapshot document= proxy->document_menu_state ();
  if (!editor.valid () || !document.ready) return state;
  state.available= true;
  state.enabled= true;
  return state;
}

bool
submit_business (
  const QTMCommandContext& context, const QString& id,
  bool writable= true) {
  QJsonObject action;
  action.insert ("op", "business");
  action.insert ("id", id);
  return submit_inline_editor_action (
    context, action, 0,
    writable ? ACTOR_EDITOR_COMMAND_STATE_READ_ONLY : 0);
}

bool
submit_document_package (
  const QTMCommandContext& context, const QString& kind,
  const QString& name) {
  QJsonObject action;
  action.insert ("op", "document-package");
  action.insert ("action", kind);
  action.insert ("name", name);
  return submit_inline_editor_action (
    context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
}

void
append_item (
  QVector<QTMCommandDynamicItem>& out, const QString& key,
  const QString& label, const QString& group, bool enabled,
  bool checkable= false, bool checked= false,
  const QString& help= QString ()) {
  QTMCommandDynamicItem item= enabled_dynamic_item (key, label, help);
  item.group= group;
  item.state.enabled= enabled;
  item.state.checkable= checkable;
  item.state.checked= checked;
  out.append (std::move (item));
}

QString
title_case (QString value) {
  value.replace ('-', ' ');
  if (!value.isEmpty ()) value[0]= value[0].toUpper ();
  return value;
}

} // namespace

void
QTMCommandRegistry::registerDocumentMenuCommands () {
  registerProvider (
    "document-menu", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_editor_command_snapshot editor= proxy->editor_command_state ();
      actor_document_menu_snapshot document= proxy->document_menu_state ();
      if (!editor.valid () || !document.ready) return out;
      const bool writable= !editor.read_only ();

      append_item (
        out, "style/none", QObject::tr ("No style"),
        QObject::tr ("Style"), writable, true,
        document.document_style.empty ());
      for (std::size_t i=0; i<document.document_styles.size (); ++i) {
        const auto& choice= document.document_styles[i];
        append_item (
          out, QStringLiteral ("style/%1").arg (i), qs (choice.label),
          QObject::tr ("Style"), writable, true, choice.checked);
      }
      append_item (
        out, "style/edit", QObject::tr ("Edit style"),
        QObject::tr ("Style"), writable);
      append_item (
        out, "style/other", QObject::tr ("Enter style name..."),
        QObject::tr ("Style"), writable);
      append_item (
        out, "style/install", QObject::tr ("Install custom style..."),
        QObject::tr ("Style"), writable);
      for (std::size_t i=0; i<document.current_packages.size (); ++i) {
        const QString package= qs (document.current_packages[i].label);
        append_item (
          out, QStringLiteral ("package/edit/%1").arg (i),
          QObject::tr ("Edit package"), package, writable);
        append_item (
          out, QStringLiteral ("package/remove/%1").arg (i),
          QObject::tr ("Remove package"), package, writable);
      }
      for (std::size_t i=0; i<document.document_packages.size (); ++i) {
        const auto& package= document.document_packages[i];
        append_item (
          out, QStringLiteral ("package/add/%1").arg (i), qs (package.label),
          QObject::tr ("Add package"), writable && !package.checked,
          true, package.checked);
      }
      append_item (
        out, "package/other", QObject::tr ("Add other package..."),
        QObject::tr ("Add package"), writable);

      append_item (
        out, "citation/default", QObject::tr ("Use Preferences default"),
        QObject::tr ("Citation Style"), writable, true,
        document.materials_citation_default);
      try {
        object raw= call ("materials-csl-styles");
        if (is_tree (raw)) {
          tree styles= as_tree (raw);
          for (int i=0; i<N(styles); ++i) {
            tree entry= styles[i];
            if (!is_compound (entry, "tuple", 2) ||
                !is_atomic (entry[0]) || !is_atomic (entry[1]))
              continue;
            QString name= to_qstring (as_string (entry[0]));
            QString title= to_qstring (as_string (entry[1]));
            append_item (
              out, QStringLiteral ("citation/%1").arg (name),
              title + QStringLiteral (" (") + name + QStringLiteral (")"),
              QObject::tr ("Citation Style"), writable, true,
              !document.materials_citation_default &&
                qs (document.materials_citation_style) == name);
          }
        }
      }
      catch (...) {}

      if (qs (document.document_theme_kind) == QStringLiteral ("basic")) {
        bool plain= true;
        for (const auto& choice: document.document_themes)
          if (choice.checked) {
            plain= false;
            break;
          }
        append_item (
          out, "theme/plain", QObject::tr ("Plain"),
          QObject::tr ("Theme"), writable, true, plain);
      }
      for (std::size_t i=0; i<document.document_themes.size (); ++i) {
        const auto& choice= document.document_themes[i];
        append_item (
          out, QStringLiteral ("theme/%1").arg (i), qs (choice.label),
          QObject::tr ("Theme"), writable, true, choice.checked);
      }
      for (std::size_t i=0; i<document.document_title_themes.size (); ++i) {
        const auto& choice= document.document_title_themes[i];
        append_item (
          out, QStringLiteral ("title-theme/%1").arg (i), qs (choice.label),
          QObject::tr ("Title style"), writable, true, choice.checked);
      }

      append_item (
        out, "source", QObject::tr ("Edit source tree"), QString (), writable,
        true, editor.has (ACTOR_EDITOR_COMMAND_STATE_SOURCE_MODE));
      append_item (
        out, "preamble",
        document.preamble_mode ? QObject::tr ("Show main document"):
        document.has_preamble ? QObject::tr ("Show preamble"):
                                QObject::tr ("Create preamble"),
        QString (), writable, true, document.preamble_mode);

      static const struct {
        const char* key;
        const char* label;
      } updates[]= {
        {"all", "All"},
        {"buffer", "Buffer"},
        {"materials", "Referenced Materials"},
        {"table-of-contents", "Table of contents"},
        {"index", "Index"},
        {"glossary", "Glossary"}
      };
      for (const auto& entry: updates)
        append_item (
          out, QStringLiteral ("update/%1").arg (entry.key),
          QObject::tr (entry.label), QObject::tr ("Update"), writable);

      append_item (
        out, "macro/preamble", QObject::tr ("Edit preamble"),
        QObject::tr ("Macros"), writable);
      append_item (
        out, "macro/extract-style", QObject::tr ("Extract style file"),
        QObject::tr ("Macros"), writable);
      append_item (
        out, "macro/extract-package", QObject::tr ("Extract style package"),
        QObject::tr ("Macros"), writable);
      append_item (
        out, "aux/inclusions", QObject::tr ("Inclusions"),
        QObject::tr ("Refresh auxiliary data"), true);
      append_item (
        out, "aux/pictures", QObject::tr ("Pictures"),
        QObject::tr ("Refresh auxiliary data"), true);
      append_item (
        out, "stats/characters", QObject::tr ("Characters"),
        QObject::tr ("Statistics"), true);
      append_item (
        out, "stats/words", QObject::tr ("Words"),
        QObject::tr ("Statistics"), true);
      append_item (
        out, "stats/lines", QObject::tr ("Lines"),
        QObject::tr ("Statistics"), true);
      append_item (
        out, "save-aux", QObject::tr ("Save auxiliary data"),
        QString (), writable, true, document.save_aux);

      append_item (out, "font", QObject::tr ("Font..."), QString (), writable);
      append_item (
        out, "paragraph", QObject::tr ("Paragraph..."), QString (), writable);
      append_item (out, "page", QObject::tr ("Page..."), QString (), writable);
      append_item (
        out, "metadata", QObject::tr ("Metadata..."), QString (), writable);

      static const char* magnifications[]= {
        "0.7", "0.8", "1", "1.2", "1.4", "1.7", "2"
      };
      append_item (
        out, "magnification/default", QObject::tr ("Default"),
        QObject::tr ("Magnification"), writable);
      for (const char* value: magnifications)
        append_item (
          out, QStringLiteral ("magnification/%1").arg (value),
          QString::fromLatin1 (value), QObject::tr ("Magnification"), writable,
          true, qs (document.magnification) == QString::fromLatin1 (value));
      append_item (
        out, "magnification/other", QObject::tr ("Other..."),
        QObject::tr ("Magnification"), writable);

      append_item (
        out, "color/background-default", QObject::tr ("Default"),
        QObject::tr ("Background"), writable);
      append_item (
        out, "color/background", QObject::tr ("Other..."),
        QObject::tr ("Background"), writable);
      append_item (
        out, "color/pattern", QObject::tr ("Pattern..."),
        QObject::tr ("Background"), writable);
      append_item (
        out, "color/gradient", QObject::tr ("Gradient..."),
        QObject::tr ("Background"), writable);
      append_item (
        out, "color/picture", QObject::tr ("Picture..."),
        QObject::tr ("Background"), writable);
      append_item (
        out, "color/foreground-default", QObject::tr ("Default"),
        QObject::tr ("Foreground"), writable);
      append_item (
        out, "color/foreground", QObject::tr ("Other..."),
        QObject::tr ("Foreground"), writable);

      append_item (
        out, "language/default", QObject::tr ("Default"),
        QObject::tr ("Language"), writable);
      static const char* languages[]= {
        "british", "bulgarian", "chinese", "croatian", "czech", "danish",
        "dutch", "english", "esperanto", "finnish", "french", "german",
        "greek", "hungarian", "italian", "japanese", "korean", "polish",
        "portuguese", "romanian", "russian", "slovak", "slovene", "spanish",
        "swedish", "taiwanese", "ukrainian"
      };
      for (const char* raw: languages) {
        QString key= QString::fromLatin1 (raw);
        append_item (
          out, QStringLiteral ("language/%1").arg (key), title_case (key),
          QObject::tr ("Language"), writable, true,
          qs (document.document_language) == key);
      }

      static const struct {
        const char* key;
        const char* value;
        const char* label;
      } flags[]= {
        {"none", "none", "None"},
        {"minimal", "minimal", "Minimal"},
        {"short", "short", "Short"},
        {"detailed", "detailed", "Detailed"},
        {"paper", "paper", "Also on paper"}
      };
      append_item (
        out, "info/default", QObject::tr ("Default"),
        QObject::tr ("Informative flags"), writable);
      for (const auto& flag: flags)
        append_item (
          out, QStringLiteral ("info/%1").arg (flag.key),
          QObject::tr (flag.label), QObject::tr ("Informative flags"), writable,
          true, qs (document.info_flag) == QString::fromLatin1 (flag.value));
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return false;
      actor_document_menu_snapshot document= proxy->document_menu_state ();
      if (!document.ready) return false;

      if (key == QStringLiteral ("style/none"))
        return submit_business (context, "document-no-style");
      if (key.startsWith (QStringLiteral ("style/"))) {
        QString suffix= key.mid (6);
        if (suffix == QStringLiteral ("edit")) {
          QJsonObject action;
          action.insert ("op", "document-edit-style");
          return submit_inline_editor_action (
            context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
        }
        if (suffix == QStringLiteral ("install")) {
          QString path= QFileDialog::getOpenFileName (
            context.shell.data (), QObject::tr ("Install custom style"),
            QString (),
            QObject::tr ("ATHENA styles (*.ats *.ts);;All files (*)"));
          if (path.isEmpty ()) return true;
          QJsonObject action;
          action.insert ("op", "document-install-style");
          action.insert ("path", path);
          return submit_inline_editor_action (
            context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
        }
        if (suffix == QStringLiteral ("other")) {
          bool ok= false;
          QString name= QInputDialog::getText (
            context.shell.data (), QObject::tr ("Document style"),
            QObject::tr ("Style name:"), QLineEdit::Normal, QString (), &ok)
                           .trimmed ();
          if (!ok || name.isEmpty ()) return true;
          QJsonObject action;
          action.insert ("op", "set-main-style");
          action.insert ("style", name);
          return submit_inline_editor_action (
            context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
        }
        bool ok= false;
        int index= suffix.toInt (&ok);
        if (!ok || index < 0 ||
            index >= static_cast<int> (document.document_styles.size ()))
          return false;
        QJsonObject action;
        action.insert ("op", "set-main-style");
        action.insert (
          "style", qs (
            document.document_styles[static_cast<std::size_t> (index)].value));
        return submit_inline_editor_action (
          context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
      }
      if (key.startsWith (QStringLiteral ("package/"))) {
        QStringList parts= key.split ('/');
        if (parts.size () == 2 && parts[1] == QStringLiteral ("other")) {
          bool ok= false;
          QString name= QInputDialog::getText (
            context.shell.data (), QObject::tr ("Add style package"),
            QObject::tr ("Package name:"), QLineEdit::Normal, QString (), &ok)
                           .trimmed ();
          if (!ok || name.isEmpty ()) return true;
          for (const auto& package: document.document_packages)
            if (qs (package.value) == name)
              return submit_document_package (context, "add", name);
          QMessageBox::warning (
            context.shell.data (), QObject::tr ("Add style package"),
            QObject::tr ("No installed style package named '%1' was found.")
              .arg (name));
          return true;
        }
        if (parts.size () != 3) return false;
        bool ok= false;
        int index= parts[2].toInt (&ok);
        if (!ok || index < 0) return false;
        if (parts[1] == QStringLiteral ("add")) {
          if (index >= static_cast<int> (document.document_packages.size ()))
            return false;
          return submit_document_package (
            context, "add",
            qs (document.document_packages[static_cast<std::size_t> (index)].value));
        }
        if (parts[1] == QStringLiteral ("edit") ||
            parts[1] == QStringLiteral ("remove")) {
          if (index >= static_cast<int> (document.current_packages.size ()))
            return false;
          return submit_document_package (
            context, parts[1],
            qs (document.current_packages[static_cast<std::size_t> (index)].value));
        }
        return false;
      }
      if (key.startsWith (QStringLiteral ("citation/"))) {
        QJsonObject action;
        action.insert ("op", "document-citation-style");
        QString style= key.mid (9);
        const bool useDefault= style == QStringLiteral ("default");
        action.insert ("default", useDefault);
        if (!useDefault) action.insert ("style", style);
        return submit_inline_editor_action (
          context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
      }
      if (key == QStringLiteral ("theme/plain")) {
        QJsonObject action;
        action.insert ("op", "document-default-theme");
        return submit_inline_editor_action (
          context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
      }
      if (key.startsWith (QStringLiteral ("theme/")) ||
          key.startsWith (QStringLiteral ("title-theme/"))) {
        const bool title= key.startsWith (QStringLiteral ("title-theme/"));
        QString suffix= key.mid (title ? 12 : 6);
        bool ok= false;
        int index= suffix.toInt (&ok);
        const auto& choices=
          title ? document.document_title_themes : document.document_themes;
        if (!ok || index < 0 || index >= static_cast<int> (choices.size ()))
          return false;
        QString name= qs (choices[static_cast<std::size_t> (index)].value);
        return submit_document_package (
          context,
          name == QStringLiteral ("alt-colors") ||
          name == QStringLiteral ("framed-theorems") ? "toggle" : "add",
          name);
      }
      if (key == QStringLiteral ("source"))
        return submit_business (context, "document-toggle-source-mode");
      if (key == QStringLiteral ("preamble"))
        return submit_business (context, "document-toggle-preamble-mode");
      if (key.startsWith (QStringLiteral ("update/")))
        return submit_business (
          context, QStringLiteral ("document-update-") + key.mid (7));
      if (key == QStringLiteral ("macro/preamble"))
        return submit_business (context, "document-toggle-preamble-mode");
      if (key == QStringLiteral ("macro/extract-style"))
        return submit_business (context, "document-extract-style-file");
      if (key == QStringLiteral ("macro/extract-package"))
        return submit_business (context, "document-extract-style-package");
      if (key == QStringLiteral ("aux/inclusions"))
        return submit_business (context, "document-refresh-inclusions", false);
      if (key == QStringLiteral ("aux/pictures"))
        return submit_business (context, "document-refresh-pictures", false);
      if (key == QStringLiteral ("stats/characters"))
        return submit_business (context, "document-character-count", false);
      if (key == QStringLiteral ("stats/words"))
        return submit_business (context, "document-word-count", false);
      if (key == QStringLiteral ("stats/lines"))
        return submit_business (context, "document-line-count", false);
      if (key == QStringLiteral ("save-aux"))
        return submit_business (context, "document-toggle-save-aux");
      if (key == QStringLiteral ("font"))
        return submit_business (context, "open-document-font-selector");
      if (key == QStringLiteral ("paragraph"))
        return submit_business (context, "open-document-paragraph-format");
      if (key == QStringLiteral ("page"))
        return submit_business (context, "open-document-page-format");
      if (key == QStringLiteral ("metadata"))
        return submit_business (context, "open-document-metadata");

      if (key.startsWith (QStringLiteral ("magnification/"))) {
        QString value= key.mid (14);
        QJsonObject action;
        if (value == QStringLiteral ("default")) {
          action.insert ("op", "init-default");
          action.insert ("var", "magnification");
        }
        else {
          if (value == QStringLiteral ("other")) {
            bool ok= false;
            double current= qs (document.magnification).toDouble (&ok);
            if (!ok || current <= 0.0) current= 1.0;
            double selected= QInputDialog::getDouble (
              context.shell.data (), QObject::tr ("Magnification"),
              QObject::tr ("Magnification:"), current, 0.01, 100.0, 3, &ok);
            if (!ok) return true;
            value= QString::number (selected, 'g', 6);
          }
          action.insert ("op", "init-env");
          action.insert ("var", "magnification");
          action.insert ("value", value);
        }
        return submit_inline_editor_action (
          context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
      }
      if (key.startsWith (QStringLiteral ("color/"))) {
        QString kind= key.mid (6);
        QJsonObject action;
        if (kind == QStringLiteral ("background-default") ||
            kind == QStringLiteral ("foreground-default")) {
          action.insert ("op", "init-default");
          action.insert (
            "var", kind.startsWith (QStringLiteral ("background")) ?
                     QStringLiteral ("bg-color"): QStringLiteral ("color"));
        }
        else if (kind == QStringLiteral ("background") ||
                 kind == QStringLiteral ("foreground")) {
          QString initialText=
            kind == QStringLiteral ("background") ?
              qs (document.background_color): qs (document.foreground_color);
          QColor initial (initialText);
          QColor selected= QColorDialog::getColor (
            initial.isValid () ? initial : Qt::white,
            context.shell.data (),
            kind == QStringLiteral ("background") ?
              QObject::tr ("Document background"):
              QObject::tr ("Document foreground"));
          if (!selected.isValid ()) return true;
          action.insert ("op", "init-env");
          action.insert (
            "var", kind == QStringLiteral ("background") ?
                     QStringLiteral ("bg-color"): QStringLiteral ("color"));
          action.insert ("value", selected.name ());
        }
        else {
          action.insert ("op", "business");
          if (kind == QStringLiteral ("pattern"))
            action.insert ("id", "document-background-pattern");
          else if (kind == QStringLiteral ("gradient"))
            action.insert ("id", "document-background-gradient");
          else if (kind == QStringLiteral ("picture"))
            action.insert ("id", "document-background-picture");
          else
            return false;
        }
        return submit_inline_editor_action (
          context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
      }
      if (key.startsWith (QStringLiteral ("language/"))) {
        QString language= key.mid (9);
        QJsonObject action;
        if (language == QStringLiteral ("default"))
          action.insert ("op", "set-default-document-language");
        else {
          action.insert ("op", "set-document-language");
          action.insert ("language", language);
        }
        return submit_inline_editor_action (
          context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
      }
      if (key.startsWith (QStringLiteral ("info/"))) {
        QString value= key.mid (5);
        QJsonObject action;
        if (value == QStringLiteral ("default")) {
          action.insert ("op", "init-default");
          action.insert ("var", "info-flag");
        }
        else {
          action.insert ("op", "init-env");
          action.insert ("var", "info-flag");
          action.insert ("value", value);
        }
        return submit_inline_editor_action (
          context, action, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY);
      }
      return false;
    },
    [] (const QTMCommandContext& context) {
      return document_menu_state (context);
    });

  registerProvider (
    "automate-menu", QTMCommandScope::Editor,
    [] (const QTMCommandContext& context) {
      QVector<QTMCommandDynamicItem> out;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr) return out;
      actor_document_menu_snapshot document= proxy->document_menu_state ();
      actor_editor_command_snapshot editor= proxy->editor_command_state ();
      if (!document.ready || !document.automate_style || !editor.valid ())
        return out;
      const bool enabled= !editor.read_only ();
      auto add= [&] (const char* key, const char* label, const char* group) {
        append_item (
          out, QString::fromLatin1 (key), QObject::tr (label),
          QObject::tr (group), enabled);
      };
      add ("block-if", "if", "Block");
      add ("block-if-else", "if-else", "Block");
      add ("block-for", "for", "Block");
      add ("block-while", "while", "Block");
      add ("block-assign", "assign", "Block");
      add ("block-intersperse", "intersperse", "Block");
      add ("block-tag", "tag", "Block");
      add ("inline-if", "if", "Inline");
      add ("inline-if-else", "if-else", "Inline");
      add ("inline-for", "for", "Inline");
      add ("inline-while", "while", "Inline");
      add ("inline-assign", "assign", "Inline");
      add ("inline-intersperse", "intersperse", "Inline");
      add ("inline-tag", "tag", "Inline");
      add ("output-string", "String", "Output");
      add ("output-inline", "Inline content", "Output");
      add ("output-block", "Block content", "Output");
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      static const QSet<QString> allowed {
        "block-if", "block-if-else", "block-for", "block-while",
        "block-assign", "block-intersperse", "block-tag",
        "inline-if", "inline-if-else", "inline-for", "inline-while",
        "inline-assign", "inline-intersperse", "inline-tag",
        "output-string", "output-inline", "output-block"
      };
      if (!allowed.contains (key)) return false;
      return submit_business (
        context, QStringLiteral ("automate-") + key);
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state= document_menu_state (context);
      if (!state.available) return state;
      qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
      if (proxy == nullptr || !proxy->document_menu_state ().automate_style) {
        state.available= false;
        state.enabled= false;
      }
      return state;
    });
}
