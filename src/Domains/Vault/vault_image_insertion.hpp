/******************************************************************************
* MODULE     : vault_image_insertion.hpp
* DESCRIPTION: Vault-aware image insertion policy
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef VAULT_IMAGE_INSERTION_HPP
#define VAULT_IMAGE_INSERTION_HPP

#include "url.hpp"

bool vault_image_insertion_prepare_file (url document, url source,
                                         string& document_ref,
                                         string& error);

bool vault_image_insertion_prepare_data (url document, string data,
                                         string extension,
                                         string& document_ref,
                                         string& error);

bool vault_image_insertion_prepare_remote (url document, url source,
                                           string& document_ref,
                                           string& error);

#endif // VAULT_IMAGE_INSERTION_HPP
