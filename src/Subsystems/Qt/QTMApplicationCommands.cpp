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
#include "QTMQuickSwitcher.hpp"
#include "QTMWebsitesManager.hpp"
#include "file.hpp"
#include "new_buffer.hpp"
#include "new_window.hpp"
#include "scheme.hpp"

#include <QFileDialog>
#include <QFileInfo>

using namespace qtm_command_registry_detail;

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
    "help.about", QTMCommandScope::Application,
    [] (const QTMCommandContext&) {
      help_about_qt ();
      return true;
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
