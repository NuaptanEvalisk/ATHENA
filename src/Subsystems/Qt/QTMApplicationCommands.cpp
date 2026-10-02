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

#include "QTMAbout.hpp"
#include "QTMArtifactsPane.hpp"
#include "QTMATHENADiff.hpp"
#include "QTMAudmap.hpp"
#include "QTMCommandPalette.hpp"
#include "QTMCustomStylesManager.hpp"
#include "QTMDocumentHistoryPane.hpp"
#include "QTMErrorMessagesPane.hpp"
#include "QTMGlobalSearch.hpp"
#include "QTMGoogleTasksPane.hpp"
#include "QTMMaterialsManager.hpp"
#include "QTMNamespaceExplorer.hpp"
#include "QTMNamespaceExport.hpp"
#include "QTMNamespaceManager.hpp"
#include "QTMNativeDialogs.hpp"
#include "QTMNeighborhoodsPane.hpp"
#include "QTMOutlinePane.hpp"
#include "QTMPreferencesDialog.hpp"
#include "QTMPluginUi.hpp"
#include "QTMQuickSwitcher.hpp"
#include "QTMVaultExplorer.hpp"
#include "QTMVaultFontConfigurator.hpp"
#include "QTMVaultMaintenanceDialog.hpp"
#include "QTMWebsitesManager.hpp"
#include "file.hpp"
#include "new_buffer.hpp"
#include "new_window.hpp"
#include "scheme.hpp"
#include "server.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>

using namespace qtm_command_registry_detail;

namespace {

enum class help_action_kind {
  article,
  buffer,
  book,
  license,
  search_documentation,
  search_source,
  search_recent,
  shortcuts
};

struct help_action {
  const char* key;
  const char* label;
  const char* group;
  help_action_kind kind;
  const char* target;
};

const help_action help_actions[]= {
  {"welcome", "Welcome", "Getting started", help_action_kind::article,
   "about/welcome/new-welcome"},
  {"start", "Getting started", "Getting started", help_action_kind::article,
   "about/welcome/start"},
  {"config-browse", "Browse", "Configuration", help_action_kind::buffer,
   "main/config/man-configuration"},
  {"config-preferences", "Preferences", "Configuration",
   help_action_kind::article, "main/config/man-preferences"},
  {"config-keyboard", "Keyboard configuration", "Configuration",
   help_action_kind::article, "main/config/man-config-keyboard"},
  {"config-russian", "Users of Cyrillic languages", "Configuration",
   help_action_kind::article, "main/config/man-russian"},
  {"config-oriental", "Users of oriental languages", "Configuration",
   help_action_kind::article, "main/config/man-oriental"},
  {"manual-browse", "Browse", "Manual", help_action_kind::buffer,
   "main/man-manual"},
  {"manual-preferences", "Preferences", "Manual", help_action_kind::article,
   "main/config/man-preferences"},
  {"manual-getting-started", "Getting started", "Manual",
   help_action_kind::article, "main/start/man-getting-started"},
  {"manual-workflows", "ATHENA knowledge workflows", "Manual",
   help_action_kind::article, "main/start/man-athena-workflows"},
  {"manual-text", "Typing simple texts", "Manual", help_action_kind::article,
   "main/text/man-text"},
  {"manual-math", "Mathematical formulas", "Manual", help_action_kind::article,
   "main/math/man-math"},
  {"manual-table", "Tabular material", "Manual", help_action_kind::article,
   "main/table/man-table"},
  {"manual-links", "Automatic content generation", "Manual",
   help_action_kind::article, "main/links/man-links"},
  {"manual-namespaces", "Namespaces in ATHENA", "Manual",
   help_action_kind::article, "main/links/man-namespaces"},
  {"manual-graphics", "Creating technical pictures", "Manual",
   help_action_kind::article, "main/graphics/man-graphics"},
  {"manual-layout", "Advanced layout features", "Manual",
   help_action_kind::article, "main/layout/man-layout"},
  {"manual-editing", "Editing tools", "Manual", help_action_kind::article,
   "main/editing/man-editing-tools"},
  {"manual-beamer", "Laptop presentations", "Manual",
   help_action_kind::article, "main/beamer/man-beamer"},
  {"manual-interface", "ATHENA as an interface", "Manual",
   help_action_kind::article, "main/interface/man-itf"},
  {"manual-style", "Writing your own style files", "Manual",
   help_action_kind::article, "devel/style/style"},
  {"manual-scheme", "Customizing ATHENA", "Manual", help_action_kind::article,
   "main/scheme/man-scheme"},
  {"reference-browse", "Browse", "Reference guide", help_action_kind::buffer,
   "main/man-reference"},
  {"reference-format", "The ATHENA format", "Reference guide",
   help_action_kind::article, "devel/format/basics/basics"},
  {"reference-env", "Standard environment variables", "Reference guide",
   help_action_kind::article, "devel/format/environment/environment"},
  {"reference-primitives", "ATHENA primitives", "Reference guide",
   help_action_kind::article, "devel/format/regular/regular"},
  {"reference-stylesheet", "Stylesheet language", "Reference guide",
   help_action_kind::article, "devel/format/stylesheet/stylesheet"},
  {"reference-styles", "Standard ATHENA styles", "Reference guide",
   help_action_kind::article, "main/styles/styles"},
  {"reference-convert", "Compatibility with other formats", "Reference guide",
   help_action_kind::article, "main/convert/man-convert"},
  {"apropos-browse", "Browse", "Apropos", help_action_kind::buffer,
   "about/about"},
  {"apropos-summary", "Summary", "Apropos", help_action_kind::article,
   "about/about-summary"},
  {"apropos-license", "License", "Apropos", help_action_kind::license, ""},
  {"apropos-philosophy", "Philosophy", "Apropos", help_action_kind::article,
   "about/philosophy/philosophy"},
  {"apropos-authors", "The ATHENA authors", "Apropos",
   help_action_kind::article, "about/authors/authors"},
  {"apropos-first", "Original welcome message", "Apropos",
   help_action_kind::article, "about/welcome/first"},
  {"search-doc", "Documentation", "Search",
   help_action_kind::search_documentation, ""},
  {"search-src", "Source code", "Search", help_action_kind::search_source, ""},
  {"search-recent", "Recent documents", "Search",
   help_action_kind::search_recent, ""},
  {"full-user-manual", "User manual", "Full manuals", help_action_kind::book,
   "main/man-user-manual"},
  {"shortcuts", "Shortcuts listing", "Tools", help_action_kind::shortcuts, ""}
};

const help_action*
find_help_action (const QString& key) {
  for (const help_action& action: help_actions)
    if (key == QString::fromLatin1 (action.key)) return &action;
  return nullptr;
}

bool
execute_help_action (const help_action& action, const QTMCommandContext& context) {
  try {
    switch (action.kind) {
    case help_action_kind::article:
      (void) call ("load-help-article", object (string (action.target)));
      return true;
    case help_action_kind::buffer:
      (void) call ("load-help-buffer", object (string (action.target)));
      return true;
    case help_action_kind::book:
      (void) call ("load-help-book", object (string (action.target)));
      return true;
    case help_action_kind::license:
      (void) call ("load-document", object (string ("$ATHENA_PATH/LICENSE")));
      return true;
    case help_action_kind::search_documentation:
    case help_action_kind::search_recent: {
      bool ok= false;
      QString query= QInputDialog::getText (
        context.shell.data (), QObject::tr ("Search help"),
        QObject::tr ("Search:"), QLineEdit::Normal, QString (), &ok).trimmed ();
      if (!ok || query.isEmpty ()) return true;
      (void) call (
        action.kind == help_action_kind::search_documentation ?
          "docgrep-in-doc" : "docgrep-in-recent",
        object (from_qstring (query)));
      return true;
    }
    case help_action_kind::search_source: {
      bool ok= false;
      QString query= QInputDialog::getText (
        context.shell.data (), QObject::tr ("Search source code"),
        QObject::tr ("Search:"), QLineEdit::Normal, QString (), &ok).trimmed ();
      if (!ok || query.isEmpty ()) return true;
      const QStringList locations {
        QStringLiteral ("Scheme"), QStringLiteral ("Styles"),
        QStringLiteral ("C++"), QStringLiteral ("All code")
      };
      QString where= QInputDialog::getItem (
        context.shell.data (), QObject::tr ("Search source code"),
        QObject::tr ("In:"), locations, 3, false, &ok);
      if (!ok) return true;
      (void) call (
        "docgrep-in-src", object (from_qstring (query)),
        object (from_qstring (where)));
      return true;
    }
    case help_action_kind::shortcuts:
      (void) call ("list-all-shortcuts");
      return true;
    }
  }
  catch (...) {}
  return false;
}

} // namespace

void
QTMCommandRegistry::registerApplicationCommands () {
  registerBehavior (
    "application.new-document", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      new_document_buffer ();
      return true;
    });
  registerBehavior (
    "application.open", QTMCommandScope::Application,
    [] (const QTMCommandContext& context) {
      return open_document_from_shell (context);
    });
  registerBehavior (
    "application.new-tab", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      open_document_window (false);
      return true;
    });
  registerBehavior (
    "application.open-new-window", QTMCommandScope::Application,
    [] (const QTMCommandContext& context) {
      return open_document_from_shell (context, true);
    });
  registerBehavior (
    "application.page-setup", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      qtm_page_setup_dialog_show ();
      return true;
    });
  registerBehavior (
    "application.preferences", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      qtm_preferences_dialog_show ();
      return true;
    });
  registerBehavior (
    "application.command-palette", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      command_palette_show ();
      return true;
    });
  registerBehavior (
    "application.quit", QTMCommandScope::Application,
    [] (const QTMCommandContext& context) {
      QTMMainTabWindow* shell= context.shell.data ();
      if (shell == nullptr) shell= QTMMainTabWindow::topTabWindow ();
      if (shell == nullptr) return false;
      shell->close ();
      return true;
    },
    [] (const QTMCommandContext& context) {
      QTMCommandState state= enabled_application_command ();
      state.enabled= context.shell != nullptr ||
                     QTMMainTabWindow::topTabWindow () != nullptr;
      return state;
    });
  registerProvider (
    "recent-files", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      QVector<QTMCommandDynamicItem> out;
      try {
        QVector<QString> data= scheme_string_vector (
          call ("native-recent-file-provider-data", object (25)));
        for (int i= 0; i + 2 < data.size (); i += 3) {
          QString label= data[i + 1].trimmed ();
          if (label.isEmpty ()) label= QFileInfo (data[i + 2]).fileName ();
          if (label.isEmpty ()) label= data[i];
          out.append (enabled_dynamic_item (
            data[i], label, data[i + 2]));
        }
      }
      catch (...) {}
      return out;
    },
    [] (const QString& key, const QTMCommandContext&) {
      if (key.isEmpty ()) return false;
      try {
        (void) call ("load-buffer", object (url (from_qstring (key))));
        return true;
      }
      catch (...) {
        return false;
      }
    });
  registerProvider (
    "file-import-formats", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      QVector<QTMCommandDynamicItem> out;
      try {
        QVector<QString> data= scheme_string_vector (
          call ("native-import-format-provider-data"));
        for (int i= 0; i + 2 < data.size (); i += 3) {
          QTMCommandDynamicItem item= enabled_dynamic_item (
            data[i], QObject::tr ("Import %1").arg (data[i + 1]));
          item.help= data[i + 2];
          out.append (std::move (item));
        }
      }
      catch (...) {}
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      if (key.isEmpty ()) return false;
      QString suffix= provider_format_suffix (key);
      QString path= QFileDialog::getOpenFileName (
        context.shell.data (), QObject::tr ("Import file"), QString (),
        provider_file_filter (key, suffix));
      if (path.isEmpty ()) return true;
      try {
        (void) call ("import-buffer",
                     object (url_system (from_qstring (path))),
                     object (from_qstring (key)));
        return true;
      }
      catch (...) {
        return false;
      }
    });
  registerBehavior (
    "workspace.namespace-explorer", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      namespace_explorer_show ();
      return true;
    });
  registerBehavior (
    "view.error-messages", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      error_messages_show ();
      return true;
    });
  registerBehavior (
    "view.artifacts", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      artifacts_pane_show ();
      return true;
    });
  registerBehavior (
    "view.outline", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      outline_pane_show ();
      return true;
    });
  registerBehavior (
    "view.neighborhoods", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      neighborhoods_pane_show ();
      return true;
    });
  registerBehavior (
    "view.document-history", QTMCommandScope::Workspace,
    [] (const QTMCommandContext& context) {
      document_history_pane_show_frozen (frozen_document_url (context));
      return true;
    });
  registerBehavior (
    "workspace.global-search", QTMCommandScope::Workspace,
    [] (const QTMCommandContext& context) {
      global_search_show_with_zoom (context.lastDocument.zoom_factor);
      return true;
    });
  registerBehavior (
    "workspace.artifacts-build-vault", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      artifacts_build_entire_vault ();
      return true;
    });
  registerBehavior (
    "file.compare-files", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      athena_diff_show ();
      return true;
    });
  registerBehavior (
    "file.export-namespace", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      namespace_export_show ();
      return true;
    });
  registerBehavior (
    "application.quick-switcher", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      open_vault_quick_switcher ();
      return true;
    });
  registerBehavior (
    "workspace.namespace-manager", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      namespace_manager_show ();
      return true;
    });
  registerBehavior (
    "workspace.websites-manager", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      websites_manager_show ();
      return true;
    });
  registerBehavior (
    "workspace.materials-manager", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      materials_manager_show ();
      return true;
    });
  registerBehavior (
    "workspace.custom-styles-manager", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      custom_styles_manager_show ();
      return true;
    });
  registerBehavior (
    "workspace.audmap-repl", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      audmap_repl_show ();
      return true;
    });
  registerBehavior (
    "workspace.google-tasks", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      google_tasks_show ();
      return true;
    });
  registerBehavior (
    "workspace.new-floating-window", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      open_document_window (true);
      return true;
    });
  registerBehavior (
    "workspace.vault-explorer", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      vault_show_explorer ();
      return true;
    });
  registerBehavior (
    "workspace.configure-vault-font", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      qtm_configure_font_for_vault ();
      return true;
    });
  registerBehavior (
    "workspace.global-transformation", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      try {
        (void) call ("run-global-transformation");
        return true;
      }
      catch (...) {
        return false;
      }
    });
  registerBehavior (
    "workspace.vault-maintenance", QTMCommandScope::Workspace,
    [] (const QTMCommandContext&) {
      qtm_vault_maintenance_start ();
      return true;
    });
  registerBehavior (
    "workspace.refresh-styles", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      get_server ()->style_clear_cache ();
      return true;
    });
  registerBehavior (
    "workspace.clean-cache", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      try {
        (void) call ("clean-athena-cache");
        return true;
      }
      catch (...) {
        return false;
      }
    });
  registerBehavior (
    "help.about", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      help_about_qt ();
      return true;
    });
  registerBehavior (
    "plugins.manage", QTMCommandScope::Application,
    [] (const QTMCommandContext& context) {
      qtm_manage_plugins (context.shell.data ());
      return true;
    });
  registerProvider (
    "help-resources", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      QVector<QTMCommandDynamicItem> out;
      for (const help_action& action: help_actions) {
        QTMCommandDynamicItem item= enabled_dynamic_item (
          QString::fromLatin1 (action.key), QObject::tr (action.label));
        item.group= QObject::tr (action.group);
        out.append (std::move (item));
      }
      return out;
    },
    [] (const QString& key, const QTMCommandContext& context) {
      const help_action* action= find_help_action (key);
      return action != nullptr && execute_help_action (*action, context);
    });
  const QString paneCommands[]= {
    "namespace.open",
    "namespace.copy",
    "namespace.paste",
    "namespace.rename",
    "namespace.delete",
    "namespace.refresh"
  };
  for (const QString& id: paneCommands)
    registerBehavior (id, QTMCommandScope::Pane, {});
}
