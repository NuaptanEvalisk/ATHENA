/******************************************************************************
* MODULE     : workflows.hpp
* DESCRIPTION: nIKIS course workflows using only AUDMAP resource operations
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include <athena/audmap/client.hpp>
#include <functional>
#include <QStringList>

namespace nikis {
using athena::audmap::value;
using athena::audmap::client;
struct command_error: std::runtime_error { using std::runtime_error::runtime_error; };
// Called on the request worker; implementations marshal dialogs onto Qt's thread.
using course_picker= std::function<int(const QStringList&, const QString&)>;
value run (client&, const value& settings, const std::string& command, course_picker);
void serve (client&, const value& settings, course_picker,
            std::function<void(const QString&)> report_error);
}
