/******************************************************************************
* MODULE     : rag_storage.hpp
* DESCRIPTION: Shared Continuous RAG SQLite schema and reset policy
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef RAG_STORAGE_HPP
#define RAG_STORAGE_HPP

#include <filesystem>
#include <string>

struct sqlite3;

namespace athena::rag::storage {

inline constexpr const char* schema_version= "3";

// RAG is derived data. A pre-v3 or incompatible draft-v3 database is
// deliberately discarded instead of migrated; documents and vault databases
// are never modified by this helper.
bool prepare_database_path (const std::filesystem::path& path,
                            std::string& error);

// Establishes the one shared schema used by local, delegated, and realtime
// writers. Existing non-v3 schemas are rejected; callers must run the path
// preparation policy before opening writable storage.
bool ensure_schema (sqlite3* db, std::string& error);

} // namespace athena::rag::storage

#endif // RAG_STORAGE_HPP
