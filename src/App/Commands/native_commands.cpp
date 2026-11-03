
/******************************************************************************
* MODULE     : native_commands.cpp
* DESCRIPTION: Generated native command interfaces
* COPYRIGHT  : (C) 1999-2011  Joris van der Hoeven and Massimiliano Gubinelli
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "App/Commands/native_dispatch.hpp"
#include "App/Commands/native_interfaces.hpp"
#include "App/Commands/native_node_groups.hpp"
#include "native_graphics_grid.hpp"
#include "native_graphics_commands.hpp"
#include "native_math_edit.hpp"
#include "native_text_edit.hpp"
#include "utf8_edit.hpp"

#include "promise.hpp"
#include "tree.hpp"
#include "tree_search.hpp"
#include "modification.hpp"
#include "patch.hpp"
#include "new_document.hpp"

#include "Typesetting/boxes.hpp"
#include "Editor/editor.hpp"
#include "universal.hpp"
#include "convert.hpp"
#include "Document/Convert/Generic/conversion_registry.hpp"
#include "Document/Model/buffer_export.hpp"
#include "Document/Convert/Generic/image_conversion.hpp"
#include "Document/Convert/Generic/conversion_graph.hpp"
#include "Document/Convert/Generic/conversion_executor.hpp"
#include "Document/Convert/Xml/html_import.hpp"
#include "Document/Convert/Xml/html_export.hpp"
#include "file.hpp"
#include "gui.hpp"
#include "gui_text.hpp"
#include "vault.hpp"
#include "namespaces.hpp"
#include "Document/Persistence/document_persistence.hpp"
#include "Domains/Artifacts/artifact_document.hpp"
#include "QTMVaultChooser.hpp"
#include "QTMQuickSwitcher.hpp"
#include "QTMBufferSwitcher.hpp"
#include "QTMVaultExplorer.hpp"
#include "QTMDocumentHistoryPane.hpp"
#include "QTMGlobalSearch.hpp"
#include "QTMATHENADiff.hpp"
#include "QTMOutlinePane.hpp"
#include "QTMErrorMessagesPane.hpp"
#include "QTMAudmap.hpp"
#include "QTMCommandPalette.hpp"
#include "QTMCustomStylesManager.hpp"
#include "QTMNamespaceManager.hpp"
#include "QTMNamespaceExplorer.hpp"
#include "QTMNeighborhoodsPane.hpp"
#include "QTMNamespaceExport.hpp"
#include "QTMCompoundDocument.hpp"
#include "QTMWebsitesManager.hpp"
#include "QTMReverseHierarchyGraph.hpp"
#include "QTMFormulaAstViewer.hpp"
#include "QTMDocumentSearchBar.hpp"
#include "QTMDocumentHistory.hpp"
#include "QTMArtifactsPane.hpp"
#include "QTMMaterialsManager.hpp"
#include "QTMMaterialCitationDialog.hpp"
#include "QTMAbout.hpp"
#include "QTMESCSymbolPicker.hpp"
#include "QTMHandwritingSymbolPane.hpp"
#include "QTMVaultFontConfigurator.hpp"
#include "QTMPreferencesDialog.hpp"
#include "QTMPagePropertiesPane.hpp"
#include "QTMParagraphPropertiesPane.hpp"
#include "QTMMetadataPropertiesPane.hpp"
#include "QTMGoogleTasksPane.hpp"
#include "GoogleCloudTodo.hpp"
#include "boot.hpp"
#include "qt_widget.hpp"
#include "qt_utilities.hpp"
#include "message.hpp"
#include "locale.hpp"
#include "iterator.hpp"
#include "Rendering/Fonts/Freetype/tt_tools.hpp"
#include "Integrations/Sqlite3/sqlite3.hpp"

#include <utility>

#include "App/Runtime/server.hpp"
#include "App/Window/tm_window.hpp"
#include "boot.hpp"
#include "convert.hpp"
#include "file.hpp"
#include "image_files.hpp"
#include "web_files.hpp"
#include "sys_utils.hpp"
#include "analyze.hpp"
#include "wencoding.hpp"
#include "base64.hpp"
#include "tree_traverse.hpp"
#include "tree_analyze.hpp"
#include "tree_correct.hpp"
#include "tree_modify.hpp"
#include "App/Window/tm_frame.hpp"
#include "Typesetting/Concat/concater.hpp"
#include "converter.hpp"
#include "tm_timer.hpp"
#include "Rendering/Fonts/Freetype/tt_file.hpp"
#include "Tex/latex_picture_fallback.hpp"
#include "link.hpp"
#include "dictionary.hpp"
#include "patch.hpp"
#include "packrat.hpp"
#include "new_style.hpp"
#include "persistent.hpp"

#include "Rendering/Pdf/pdf_hummus_extract_attachment.hpp"
#include "Rendering/Pdf/pdf_hummus_make_attachment.hpp"

#include <mutex>

#include <athena-commands/commands_basic.cpp>
#include <athena-commands/commands_editor.cpp>
#include <athena-commands/commands_server.cpp>
#include <athena-commands/commands_native.cpp>

void
initialize_native_commands () {
  static std::once_flag initialized;
  std::call_once (initialized, [] {
    initialize_escape_symbol_picker_data ();
    initialize_commands_basic ();
    initialize_commands_editor ();
    initialize_commands_server ();
    initialize_commands_native ();
  });
}
