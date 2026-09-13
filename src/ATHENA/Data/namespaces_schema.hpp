/******************************************************************************
* MODULE     : namespaces_schema.hpp
* DESCRIPTION: Shared namespace database opening and transactional migration
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#ifndef ATHENA_NAMESPACES_SCHEMA_HPP
#define ATHENA_NAMESPACES_SCHEMA_HPP

#include <filesystem>
#include <string>

struct sqlite3;

// On failure db is null. The caller owns the returned connection.
bool athena_namespace_database_open (const std::filesystem::path& path,
                                     bool create, sqlite3*& db,
                                     std::string& error);
bool athena_namespace_schema_ensure (sqlite3* db, std::string& error);
std::string athena_namespace_new_uuid ();

#endif
