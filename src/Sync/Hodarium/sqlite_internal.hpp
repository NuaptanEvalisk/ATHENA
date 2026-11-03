/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include <sqlite3.h>
#include <stdexcept>
#include <string>

namespace athena::hodarium::detail {
inline void check (sqlite3* db, int result) {
  if (result != SQLITE_OK) throw std::runtime_error (sqlite3_errmsg (db));
}
inline void sql (sqlite3* db, const char* query) {
  check (db, sqlite3_exec (db, query, nullptr, nullptr, nullptr));
}
struct statement {
  sqlite3* db;
  sqlite3_stmt* value= nullptr;
  statement (sqlite3* connection, const char* query): db (connection) {
    check (db, sqlite3_prepare_v2 (db, query, -1, &value, nullptr));
  }
  statement (const statement&)= delete;
  statement& operator= (const statement&)= delete;
  ~statement () { sqlite3_finalize (value); }
  void text (int at, const std::string& text) {
    check (db, sqlite3_bind_text64 (value, at, text.data (),
      text.size (), SQLITE_TRANSIENT, SQLITE_UTF8));
  }
  void blob (int at, const std::string& bytes) {
    check (db, sqlite3_bind_blob64 (value, at, bytes.data (),
      bytes.size (), SQLITE_TRANSIENT));
  }
  bool row () {
    int result= sqlite3_step (value);
    if (result == SQLITE_ROW) return true;
    if (result == SQLITE_DONE) return false;
    throw std::runtime_error (sqlite3_errmsg (db));
  }
  std::string bytes (int at) const {
    const auto* data= static_cast<const char*> (sqlite3_column_blob (value, at));
    return data == nullptr ? std::string () :
      std::string (data, sqlite3_column_bytes (value, at));
  }
};
struct transaction {
  sqlite3* db;
  bool committed= false;
  explicit transaction (sqlite3* connection): db (connection) {
    sql (db, "BEGIN IMMEDIATE");
  }
  transaction (const transaction&)= delete;
  transaction& operator= (const transaction&)= delete;
  ~transaction () {
    if (!committed) sqlite3_exec (db, "ROLLBACK", nullptr, nullptr, nullptr);
  }
  void commit () { sql (db, "COMMIT"); committed= true; }
};
} // namespace athena::hodarium::detail
