/******************************************************************************
* MODULE     : QTMAnchorConfirmation.hpp
* DESCRIPTION: Asynchronous confirmation of document anchor changes
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#ifndef QTM_ANCHOR_CONFIRMATION_HPP
#define QTM_ANCHOR_CONFIRMATION_HPP

#include <QString>
#include <functional>

// The dialog and completion run on the GUI thread. Bind editor callbacks to
// their originating actor before calling this function.
void qt_anchor_enunciations_confirm (
  QString wraps, QString dead, QString headings, QString notes,
  std::function<void (bool)> completion);

#endif
