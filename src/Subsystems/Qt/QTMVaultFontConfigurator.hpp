/******************************************************************************
* MODULE     : QTMVaultFontConfigurator.hpp
* DESCRIPTION: Transactional Vault-wide font configuration
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#ifndef QTMVAULTFONTCONFIGURATOR_HPP
#define QTMVAULTFONTCONFIGURATOR_HPP

#include "string.hpp"
#include "tree.hpp"

tree athena_document_with_font_profile (tree document, string profile);
void qtm_configure_font_for_vault ();

#endif // QTMVAULTFONTCONFIGURATOR_HPP
