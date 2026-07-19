/******************************************************************************
* MODULE     : QTMVaultLinkFocus.hpp
* DESCRIPTION: TeXmacs focus preservation for vault link dialogs
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMVAULTLINKFOCUS_HPP
#define QTMVAULTLINKFOCUS_HPP

#include "QTMWidget.hpp"
#include "url.hpp"
#include <QPointer>

struct TeXmacsFocusSnapshot {
  url view;
  QPointer<QTMWidget> widget;
  int hScroll;
  int vScroll;
  bool hasScroll;
};

TeXmacsFocusSnapshot capture_texmacs_focus_snapshot ();
void restore_texmacs_focus_snapshot_later (const TeXmacsFocusSnapshot& s);

#endif // QTMVAULTLINKFOCUS_HPP
