/******************************************************************************
* MODULE     : QTMNamespaceNewFile.hpp
* DESCRIPTION: Native Qt namespace file creation helpers
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTM_NAMESPACE_NEW_FILE_HPP
#define QTM_NAMESPACE_NEW_FILE_HPP

#include "string.hpp"

#include <functional>

string namespace_new_file_wizard ();
void namespace_new_file_wizard_async (std::function<void(string)> completion);
bool namespace_create_file_with_optional_initializer (string system_path,
                                                       string& error);
void namespace_create_file_with_optional_initializer_async (
  string system_path, std::function<void(bool, string)> completion);

#endif // QTM_NAMESPACE_NEW_FILE_HPP
