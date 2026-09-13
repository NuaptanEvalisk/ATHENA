/******************************************************************************
* MODULE     : QTMAudmap.hpp
* DESCRIPTION: Desktop AUDMAP service startup and shutdown interface
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include <memory>
#include <QString>

class QTMAudmap {
  struct impl;
  std::unique_ptr<impl> implementation;
public:
  QTMAudmap ();
  ~QTMAudmap ();
  QString discoveryFile () const;
};

void qt_audmap_start ();
void qt_audmap_stop ();
QString qt_audmap_discovery_file ();
void audmap_repl_show ();
