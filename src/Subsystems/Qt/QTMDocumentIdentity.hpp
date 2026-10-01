/******************************************************************************
* MODULE     : QTMDocumentIdentity.hpp
* DESCRIPTION: Explicit Qt-side identity for a document view
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMDOCUMENTIDENTITY_HPP
#define QTMDOCUMENTIDENTITY_HPP

#include "actor_transport.hpp"

#include <QPointer>
#include <string>

class QTMMainTabWindow;
class QWidget;

struct QTMDocumentIdentity {
  QPointer<QWidget> widget;
  athena_actor_id actor= ATHENA_NO_ACTOR;
  athena_view_id view= ATHENA_NO_VIEW;
  std::string native_url_name;
  double zoom_factor= 1.0;

  bool has_view () const noexcept {
    return widget != nullptr && actor != ATHENA_NO_ACTOR &&
           view != ATHENA_NO_VIEW;
  }
  bool has_buffer_name () const noexcept {
    return !native_url_name.empty ();
  }
};

QTMDocumentIdentity qtm_document_identity (QWidget* document);
QTMDocumentIdentity qtm_last_active_document_identity (
  QTMMainTabWindow* shell);

#endif // QTMDOCUMENTIDENTITY_HPP
