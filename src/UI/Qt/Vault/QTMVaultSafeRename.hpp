/******************************************************************************
* MODULE     : QTMVaultSafeRename.hpp
* DESCRIPTION: Qt confirmation frontend for safe Vault renaming
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************/

#ifndef QTMVAULTSAFERENAME_HPP
#define QTMVAULTSAFERENAME_HPP

#include <QString>

class QWidget;

bool qtm_safe_rename_vault_item (QWidget* parent, const QString& source,
                                 const QString& target);

#endif // QTMVAULTSAFERENAME_HPP
