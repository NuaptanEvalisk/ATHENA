/******************************************************************************
* MODULE     : QTMPopupMenuPresenter.hpp
* DESCRIPTION: Registry-backed transient editor popup menus
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#ifndef QTMPOPUPMENUPRESENTER_HPP
#define QTMPOPUPMENUPRESENTER_HPP

#include "QTMCommandRegistry.hpp"

class QMenu;
class QWidget;

QMenu* qtm_create_popup_menu (
  const QString& popupId, const QTMCommandContext& context, QWidget* parent);

#endif // QTMPOPUPMENUPRESENTER_HPP
