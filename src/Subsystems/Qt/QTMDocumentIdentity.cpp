/******************************************************************************
* MODULE     : QTMDocumentIdentity.cpp
* DESCRIPTION: Explicit Qt-side identity for a document view
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMDocumentIdentity.hpp"

#include "QTMMainTabWindow.hpp"
#include "QTMWidget.hpp"
#include "QTMWindow.hpp"
#include "actor_ui_bridge.hpp"
#include "buffer_name_catalog.hpp"
#include "qt_actor_widget.hpp"

QTMDocumentIdentity
qtm_document_identity (QWidget* document) {
  QTMDocumentIdentity result;
  if (document == nullptr) return result;

  QTMWidget* canvas= qobject_cast<QTMWidget*> (document);
  if (canvas == nullptr)
    if (QTMWindow* window= qobject_cast<QTMWindow*> (document))
      canvas= window->editorCanvas ();
  if (canvas == nullptr) return result;
  auto* proxy= dynamic_cast<qt_actor_widget_rep*> (canvas->tm_widget ());
  if (proxy == nullptr) return result;

  result.widget= document;
  result.actor= proxy->actor_id ();
  result.view= proxy->view_id ();
  if (actor_ui_endpoint* endpoint= find_actor_ui_endpoint (result.view))
    result.zoom_factor= endpoint->zoom_factor ();
  for (const auto& entry: published_buffer_metadata ())
    if (entry.second.actor_id == result.actor) {
      result.native_url_name= entry.first;
      break;
    }
  return result;
}

QTMDocumentIdentity
qtm_last_active_document_identity (QTMMainTabWindow* shell) {
  return shell == nullptr ? QTMDocumentIdentity {} :
         qtm_document_identity (shell->lastActiveDocumentWidget ());
}
