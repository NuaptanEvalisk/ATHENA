/******************************************************************************
* MODULE     : value.hpp
* DESCRIPTION: Portable semantic values shared by AUDM and standalone AUDMAP clients
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include <nlohmann/json.hpp>
namespace athena::interop { using value = nlohmann::json; }
