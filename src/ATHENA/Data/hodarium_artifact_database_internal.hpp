/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "vaultfile_json.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <string>
#include <vector>

// Shared native owner primitives: extractor and headless adapter use one schema.
namespace athena::artifact_database {

struct SqliteDb {
  sqlite3* db= nullptr;
  ~SqliteDb () { if (db) sqlite3_close (db); }
};

struct Statement {
  sqlite3_stmt* st= nullptr;
  ~Statement () { if (st) sqlite3_finalize (st); }
};

bool exec_sql (sqlite3* db, const std::string& sql, std::string& error);
bool prepare (sqlite3* db, const char* sql, Statement& out, std::string& error);
bool bind_text (sqlite3_stmt* st, int index, const std::string& value);
std::string column_text (sqlite3_stmt* st, int column);
bool safe_relative_database (const std::string& value);
bool open_databases (const std::filesystem::path& root, SqliteDb& holder,
                     AthenaVaultfileInfo& info, std::string& error,
                     bool read_only= false);
std::string offsets_text (const std::vector<int>& offsets);
std::vector<int> parse_offsets (const std::string& text);

} // namespace athena::artifact_database
