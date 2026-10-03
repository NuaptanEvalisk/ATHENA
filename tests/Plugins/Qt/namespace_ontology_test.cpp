/******************************************************************************
* MODULE     : namespace_ontology_test.cpp
* DESCRIPTION: Tests for the incremental namespace ontology cache
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "namespace_ontology.hpp"
#include "namespace_sorter_migration.hpp"
#include "namespaces.hpp"
#include "namespaces_private.hpp"
#include "drd_std.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include "vault.hpp"
#include "vaultfile_json.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <fstream>
#include <future>

bool headless_mode= true;
bool is_headless () { return true; }

namespace fs= std::filesystem;

class NamespaceOntologyTest: public QObject {
  Q_OBJECT

private slots:
  void initTestCase ();
  void incrementallyMaintainsMembers ();
  void sortersRetainGenerations ();
  void sorterContractsFailAtomically ();
  void structuralSorterCompositions ();
  void migratesSortersWithExplicitMapping ();
};

namespace {

void
write_document (const fs::path& path) {
  fs::create_directories (path.parent_path ());
  std::ofstream output (path, std::ios::binary | std::ios::trunc);
  const tree source (DOCUMENT,
    compound ("style", tree (TUPLE, "generic")),
    compound ("body", tree (DOCUMENT, "test")));
  output << athena::document::write_xml_v2 (source);
}

int
query_count (const fs::path& database, const char* sql) {
  sqlite3* db= nullptr;
  if (sqlite3_open_v2 (database.c_str (), &db, SQLITE_OPEN_READONLY,
                       nullptr) != SQLITE_OK)
    return -1;
  sqlite3_stmt* statement= nullptr;
  int result= -1;
  if (sqlite3_prepare_v2 (db, sql, -1, &statement, nullptr) == SQLITE_OK &&
      sqlite3_step (statement) == SQLITE_ROW)
    result= sqlite3_column_int (statement, 0);
  if (statement != nullptr) sqlite3_finalize (statement);
  sqlite3_close (db);
  return result;
}

bool
execute_sql (const fs::path& database, const char* sql) {
  sqlite3* db= nullptr;
  if (sqlite3_open_v2 (database.c_str (), &db, SQLITE_OPEN_READWRITE,
                       nullptr) != SQLITE_OK)
    return false;
  bool ok= sqlite3_exec (db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
  sqlite3_close (db);
  return ok;
}

std::string
query_text (const fs::path& database, const char* sql) {
  sqlite3* db= nullptr;
  if (sqlite3_open_v2 (database.c_str (), &db, SQLITE_OPEN_READONLY,
                       nullptr) != SQLITE_OK)
    return {};
  sqlite3_stmt* statement= nullptr;
  std::string result;
  if (sqlite3_prepare_v2 (db, sql, -1, &statement, nullptr) == SQLITE_OK &&
      sqlite3_step (statement) == SQLITE_ROW) {
    const unsigned char* text= sqlite3_column_text (statement, 0);
    if (text) result= reinterpret_cast<const char*> (text);
  }
  if (statement != nullptr) sqlite3_finalize (statement);
  sqlite3_close (db);
  return result;
}

} // namespace

void
NamespaceOntologyTest::initTestCase () {
  init_std_drd ();
}

void
NamespaceOntologyTest::incrementallyMaintainsMembers () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root= fs::u8path (temporary.path ().toStdString ());
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, AthenaVaultfileInfo {}, error),
            error.c_str ());
  write_document (root / "Notes" / "Note Alpha.ath");
  write_document (root / "Unmatched.ath");

  string load_error= vault_load (
    url_system (string (root.string ().c_str ())), "Ontology test",
    "map.sqlite", "ns.sqlite");
  QVERIFY2 (load_error == "", as_charp (load_error));

  athena_namespace_definition notes;
  notes.name= "Notes";
  notes.kind= "concrete";
  notes.templ= "Note %s";
  notes.sorter_trivial= true;
  string tm_error;
  athena_namespace_definition universe;
  universe.name= "Universe";
  universe.kind= "abstract";
  universe.templ= "";
  universe.sorter_trivial= true;
  QVERIFY2 (athena_namespace_save (universe, tm_error), as_charp (tm_error));
  notes.parents.push_back ("Universe");
  QVERIFY2 (athena_namespace_save (notes, tm_error), as_charp (tm_error));
  athena_namespace_definition special;
  special.name= "Special Notes";
  special.kind= "concrete";
  special.templ= "Special %s";
  special.sorter_trivial= true;
  special.parents= {"Universe", "Notes"};
  QVERIFY2 (athena_namespace_save (special, tm_error), as_charp (tm_error));
  QVERIFY2 (athena_namespace_ontology_refresh (true, tm_error),
            as_charp (tm_error));

  namespace_records<string> visible;
  namespace_records<string> folded;
  QVERIFY2 (athena_namespace_ontology_children (
              "Universe", false, visible, folded, tm_error),
            as_charp (tm_error));
  QCOMPARE ((int) visible.size (), 2);
  QVERIFY (visible[0] == "Notes" || visible[1] == "Notes");
  QVERIFY (visible[0] == "Special Notes" || visible[1] == "Special Notes");
  QVERIFY2 (athena_namespace_ontology_children (
              "Universe", true, visible, folded, tm_error),
            as_charp (tm_error));
  QCOMPARE ((int) visible.size (), 1);
  QCOMPARE (visible[0], string ("Notes"));
  QCOMPARE ((int) folded.size (), 1);
  QCOMPARE (folded[0], string ("Special Notes"));

  namespace_records<athena_namespace_match> members=
    athena_namespace_members ("Notes", tm_error);
  QCOMPARE (members.size (), (size_t) 1);
  QCOMPARE (members[0].stem, string ("Note Alpha"));
  // A reader on another owner keeps the same published payload, not a copy.
  auto retained_members= members;
  auto retained_definitions= athena_namespaces_list ();
  auto retained_relations= athena_namespace_relations_list ();
  auto retained_children= visible;
  auto reader= std::async (std::launch::async, [&] {
    namespace_records<athena_namespace_match> other_members;
    namespace_records<athena_namespace_definition> other_definitions;
    namespace_records<athena_namespace_relation> other_relations;
    namespace_records<string> other_visible, other_folded;
    std::shared_ptr<const athena_namespace_definition> definition;
    string error;
    if (!athena_namespace_ontology_members ("Notes", other_members, error,
                                           &definition) ||
        !athena_namespace_ontology_namespaces (other_definitions) ||
        !athena_namespace_ontology_relations (other_relations) ||
        !athena_namespace_ontology_children ("Universe", true, other_visible,
                                             other_folded, error))
      return false;
    if (other_members.size () != retained_members.size () ||
        &other_members[0] != &retained_members[0] ||
        &other_definitions[0] != &retained_definitions[0] ||
        &other_visible[0] != &retained_children[0] ||
        other_relations.size () != retained_relations.size ())
      return false;
    for (size_t i=0; i<other_relations.size (); ++i)
      if (&other_relations[i] != &retained_relations[i]) return false;
    string borrowed= other_members[0].captures[0];
    if (borrowed.data () != retained_members[0].captures[0].data ())
      return false;
    for (size_t i=0; i<other_definitions.size (); ++i)
      if (other_definitions[i].name == "Notes")
        return definition.get () == &other_definitions[i];
    return false;
  });
  QVERIFY (reader.get ());
  fs::path cache= root / ".athena" / "namespace-ontology.sqlite";
  QCOMPARE (query_count (cache,
                         "SELECT count(*) FROM namespace_cache_files;"), 2);
  QCOMPARE (query_count (cache,
                         "SELECT count(*) FROM namespace_cache_matches;"), 1);
  QVERIFY (execute_sql (
    cache,
    "CREATE TABLE test_match_deletions(count INTEGER NOT NULL);"
    "INSERT INTO test_match_deletions VALUES(0);"
    "CREATE TRIGGER test_match_delete AFTER DELETE ON namespace_cache_matches "
    "BEGIN UPDATE test_match_deletions SET count=count+1; END;"
    "CREATE TABLE test_hierarchy_deletions(count INTEGER NOT NULL);"
    "INSERT INTO test_hierarchy_deletions VALUES(0);"
    "CREATE TRIGGER test_hierarchy_delete AFTER DELETE "
    "ON namespace_cache_children "
    "BEGIN UPDATE test_hierarchy_deletions SET count=count+1; END;"));

  std::error_code timestamp_error;
  fs::file_time_type unchanged_cache_time=
    fs::last_write_time (cache, timestamp_error);
  QVERIFY (!timestamp_error);
  QVERIFY2 (athena_namespace_ontology_refresh (false, tm_error),
            as_charp (tm_error));
  QVERIFY (fs::last_write_time (cache, timestamp_error) ==
           unchanged_cache_time);
  QVERIFY (!timestamp_error);

  write_document (root / "Notes" / "Note Beta.ath");
  QVERIFY2 (athena_namespace_ontology_refresh (false, tm_error),
            as_charp (tm_error));
  members= athena_namespace_members ("Notes", tm_error);
  QCOMPARE (members.size (), (size_t) 2);
  QCOMPARE (query_count (cache, "SELECT count FROM test_match_deletions;"), 0);

  QVERIFY (fs::remove (root / "Notes" / "Note Alpha.ath"));
  QVERIFY2 (athena_namespace_ontology_refresh (false, tm_error),
            as_charp (tm_error));
  members= athena_namespace_members ("Notes", tm_error);
  QCOMPARE (members.size (), (size_t) 1);
  QCOMPARE (members[0].stem, string ("Note Beta"));
  QCOMPARE (query_count (cache, "SELECT count FROM test_match_deletions;"), 1);

  write_document (root / "Notes" / "Note Gamma.ath");
  bool background_updated= false;
  for (int waited=0; waited<5000 && !background_updated; waited+=100) {
    QTest::qWait (100);
    members= athena_namespace_members ("Notes", tm_error);
    background_updated= members.size () == 2;
  }
  QVERIFY2 (background_updated,
            "The background ontology worker did not notice a new file");

  int deletions_before_restart=
    query_count (cache, "SELECT count FROM test_match_deletions;");
  int hierarchy_deletions_before_restart=
    query_count (cache, "SELECT count FROM test_hierarchy_deletions;");

  QCOMPARE (retained_members[0].stem, string ("Note Alpha"));
  QCOMPARE (retained_members[0].captures[0], string ("Alpha"));
  vault_close ();
  load_error= vault_load (
    url_system (string (root.string ().c_str ())), "Ontology test",
    "map.sqlite", "ns.sqlite");
  QVERIFY2 (load_error == "", as_charp (load_error));
  members= athena_namespace_members ("Notes", tm_error);
  QCOMPARE (members.size (), (size_t) 2);
  QCOMPARE (members[0].stem, string ("Note Beta"));
  QCOMPARE (members[1].stem, string ("Note Gamma"));
  QCOMPARE (query_count (cache, "SELECT count FROM test_match_deletions;"),
            deletions_before_restart);
  QCOMPARE (query_count (cache,
                         "SELECT count FROM test_hierarchy_deletions;"),
            hierarchy_deletions_before_restart);

  special.parents.clear ();
  special.parents.push_back ("Universe");
  QVERIFY2 (athena_namespace_save (special, tm_error), as_charp (tm_error));
  QVERIFY2 (athena_namespace_ontology_refresh (false, tm_error),
            as_charp (tm_error));
  QVERIFY2 (athena_namespace_ontology_children (
              "Universe", true, visible, folded, tm_error),
            as_charp (tm_error));
  QCOMPARE ((int) visible.size (), 2);
  QCOMPARE ((int) folded.size (), 0);
  QVERIFY (query_count (cache,
                        "SELECT count FROM test_hierarchy_deletions;") >
           hierarchy_deletions_before_restart);
  vault_close ();
}

void
NamespaceOntologyTest::sortersRetainGenerations () {
  using namespace athena_namespaces;
  QTemporaryDir temporary ("/home/felix/tmp/athena-luau-runtime-XXXXXX");
  QVERIFY (temporary.isValid ());
  fs::path source= fs::u8path (temporary.path ().toStdString ()) / "sorter.luau";
  string path (source.c_str ());
  path.ensure_transferable ();
  auto write_sorter= [&] (bool descending) {
    std::ofstream output (source);
    if (!descending)
      output <<
        "return {\n"
        "  version = 1,\n"
        "  key = function(fields)\n"
        "    return { fields[1].integer }\n"
        "  end,\n"
        "}\n";
    else
      output <<
        "return {\n"
        "  version = 1,\n"
        "  compare = function(a, b)\n"
        "    local c = athena.int64_compare(a[1].integer, b[1].integer)\n"
        "    if c < 0 then return 1 end\n"
        "    if c > 0 then return -1 end\n"
        "    return 0\n"
        "  end,\n"
        "}\n";
  };
  auto records= [] {
    std::vector<athena_namespace_match> values;
    for (const char* value: {"10", "2", "7"}) {
      athena_namespace_match match;
      match.stem= value;
      match.captures= {string (value)};
      match.capture_types= {string ("int")};
      match.ambiguous= false;
      values.push_back (std::move (match));
    }
    return namespace_records<athena_namespace_match> (std::move (values));
  };
  string error;
  write_sorter (false);
  auto first= load_sorter (path, error);
  QVERIFY2 (first != nullptr, error.c_str ());
  QVERIFY (first == load_sorter (path, error));
  std::weak_ptr<const compiled_sorter> old_generation= first;
  auto timestamp= fs::last_write_time (source);
  write_sorter (true);
  fs::last_write_time (source, timestamp + std::chrono::seconds (1));
  auto second= load_sorter (path, error);
  QVERIFY2 (second != nullptr, error.c_str ());
  QVERIFY (second != first);
  auto ascending= records ();
  const auto* original= &ascending[0];
  QVERIFY2 (sort_namespace_members (first, ascending, error), as_charp (error));
  QCOMPARE (ascending[0].stem, string ("2"));
  QVERIFY (&ascending[2] == original);
  auto descending= ascending;
  QVERIFY2 (sort_namespace_members (second, descending, error), as_charp (error));
  QCOMPARE (descending[0].stem, string ("10"));
  QCOMPARE (ascending[0].stem, string ("2"));
  first.reset ();
  QVERIFY (old_generation.expired ());

  // Independent owner threads must not share mutable Luau module state.
  {
    std::ofstream output (source);
    output <<
      "local calls = 0\n"
      "return {\n"
      "  version = 1,\n"
      "  compare = function(a, b)\n"
      "    calls += 1\n"
      "    if calls > 9 then error('state leaked across owners') end\n"
      "    return athena.int64_compare(a[1].integer, b[1].integer)\n"
      "  end,\n"
      "}\n";
  }
  fs::last_write_time (source, timestamp + std::chrono::seconds (2));
  struct ThreadResult {
    bool sorted;
    std::weak_ptr<const compiled_sorter> generation;
  };
  std::vector<std::future<ThreadResult>> jobs;
  for (int i=0; i<4; ++i)
    jobs.push_back (std::async (std::launch::async, [&] {
      string thread_error;
       auto sorter= load_sorter (path, thread_error);
       if (!sorter) return ThreadResult {false, {}};
       auto values= records ();
       bool ok= sort_namespace_members (sorter, values, thread_error);
       return ThreadResult {ok && values[0].stem == "2" &&
                            values[2].stem == "10", sorter};
    }));
  for (auto& job: jobs) {
    ThreadResult result= job.get ();
    QVERIFY (result.sorted);
    QVERIFY (result.generation.expired ());
  }
  {
    std::ofstream output (source);
    output << "return { version = 1, key = function(";
  }
  fs::last_write_time (source, timestamp + std::chrono::seconds (3));
  QVERIFY (!load_sorter (path, error));
  QVERIFY (error != "");
  auto retained= records ();
  QVERIFY2 (sort_namespace_members (second, retained, error), as_charp (error));
  QCOMPARE (retained[0].stem, string ("10"));
}

void
NamespaceOntologyTest::sorterContractsFailAtomically () {
  using namespace athena_namespaces;
  QTemporaryDir temporary ("/home/felix/tmp/athena-luau-contract-XXXXXX");
  QVERIFY (temporary.isValid ());
  fs::path root= fs::u8path (temporary.path ().toStdString ());
  fs::path source= root / "sorter.luau";
  string path (source.c_str ());
  path.ensure_transferable ();
  auto write_source= [&] (const char* text) {
    std::ofstream output (source, std::ios::binary | std::ios::trunc);
    output << text;
    output.close ();
    QVERIFY (output.good ());
  };
  auto records= [] (std::initializer_list<const char*> values,
                    const char* type) {
    std::vector<athena_namespace_match> out;
    for (const char* value: values) {
      athena_namespace_match match;
      match.stem= value;
      match.captures= {string (value)};
      match.capture_types= {string (type)};
      match.ambiguous= false;
      out.push_back (std::move (match));
    }
    return namespace_records<athena_namespace_match> (std::move (out));
  };

  string error;
  write_source (
    "return { version = 1, key = function(fields) "
    "return { fields[1].integer } end }\n");
  auto exact= load_sorter (path, error);
  QVERIFY2 (exact != nullptr, as_charp (error));
  auto large= records ({"9007199254740993", "9007199254740992"}, "int");
  QVERIFY2 (sort_namespace_members (exact, large, error), as_charp (error));
  QCOMPARE (large[0].stem, string ("9007199254740992"));
  QCOMPARE (large[1].stem, string ("9007199254740993"));

  write_source (
    "return { version = 1, key = function(fields) "
    "if fields[1].text == 'alpha' then return { 1 } end "
    "return { '1' } end }\n");
  auto heterogeneous= load_sorter (path, error);
  QVERIFY2 (heterogeneous != nullptr, as_charp (error));
  auto mixed= records ({"beta", "alpha"}, "string");
  const string mixed_first= mixed[0].stem;
  QVERIFY (!sort_namespace_members (heterogeneous, mixed, error));
  QVERIFY (error != "");
  QCOMPARE (mixed[0].stem, mixed_first);

  write_source (
    "return { version = 1, key = function(fields) return { 0/0 } end }\n");
  auto nan= load_sorter (path, error);
  QVERIFY2 (nan != nullptr, as_charp (error));
  auto nan_values= records ({"b", "a"}, "string");
  QVERIFY (!sort_namespace_members (nan, nan_values, error));
  QCOMPARE (nan_values[0].stem, string ("b"));

  write_source (
    "local blocked = os.execute\n"
    "return { version = 1, key = function(fields) return { fields[1].text } end }\n");
  QVERIFY (!load_sorter (path, error));
  QVERIFY (error != "");

  write_source (
    "return { version = 1, key = function(fields) "
    "while true do end return { fields[1].text } end }\n");
  auto looping= load_sorter (path, error);
  QVERIFY2 (looping != nullptr, as_charp (error));
  auto timed= records ({"b", "a"}, "string");
  QVERIFY (!sort_namespace_members (looping, timed, error));
  QVERIFY (error != "");
  QCOMPARE (timed[0].stem, string ("b"));

  write_source (
    "return { version = 1, compare = function(a, b) "
    "local x, y = a[1].text, b[1].text "
    "if x == y then return 0 end "
    "if (x == 'a' and y == 'b') or (x == 'b' and y == 'c') or "
    "(x == 'c' and y == 'a') then return -1 end "
    "return 1 end }\n");
  auto cyclic= load_sorter (path, error);
  QVERIFY2 (cyclic != nullptr, as_charp (error));
  auto invalid_compare= records ({"a", "b", "c"}, "string");
  QVERIFY (!sort_namespace_members (cyclic, invalid_compare, error));
  QVERIFY (error != "");
  QCOMPARE (invalid_compare[0].stem, string ("a"));

  write_source (
    "return { version = 1, compare = function(a, b) "
    "return athena.byte_compare(string.sub(a[1].text, 1, 1), "
    "string.sub(b[1].text, 1, 1)) end }\n");
  auto equivalence= load_sorter (path, error);
  QVERIFY2 (equivalence != nullptr, as_charp (error));
  auto equivalent= records ({"a2", "a1", "b1"}, "string");
  QVERIFY2 (sort_namespace_members (equivalence, equivalent, error),
            as_charp (error));
  QCOMPARE (equivalent[0].stem, string ("a2"));
  QCOMPARE (equivalent[1].stem, string ("a1"));

  write_source (
    "return { version = 1, key = function(fields) "
    "if fields[1].text == 'a' then return { 'same' } end "
    "return { 'same', 1 } end }\n");
  auto shorter= load_sorter (path, error);
  QVERIFY2 (shorter != nullptr, as_charp (error));
  auto key_lengths= records ({"b", "a"}, "string");
  QVERIFY2 (sort_namespace_members (shorter, key_lengths, error),
            as_charp (error));
  QCOMPARE (key_lengths[0].stem, string ("a"));
}

void
NamespaceOntologyTest::structuralSorterCompositions () {
  using namespace athena_namespaces;
  QTemporaryDir temporary ("/home/felix/tmp/athena-luau-composition-XXXXXX");
  QVERIFY (temporary.isValid ());
  fs::path root= fs::u8path (temporary.path ().toStdString ());
  std::string vaultfile_error;
  QVERIFY2 (athena_vaultfile_write (root, AthenaVaultfileInfo {},
                                    vaultfile_error),
            vaultfile_error.c_str ());
  {
    std::ofstream first (root / "first.luau");
    first <<
      "return { version = 1, key = function(fields) "
      "return { fields[1].text } end }\n";
    std::ofstream second (root / "second.luau");
    second <<
      "return { version = 1, compare = function(a, b) "
      "return -athena.byte_compare(a[1].text, b[1].text) end }\n";
  }
  string load_error= vault_load (
    url_system (string (root.string ().c_str ())), "Composition test",
    "map.sqlite", "ns.sqlite");
  QVERIFY2 (load_error == "", as_charp (load_error));
  auto close_vault= qScopeGuard ([] { vault_close (); });
  vault_context_handle context= vault_capture_context ();
  QVERIFY (context != nullptr);

  athena_namespace_definition first;
  first.name= "First";
  first.kind= "semi-concrete";
  first.templ= "%s";
  first.sorter_path= "first.luau";
  athena_namespace_definition second;
  second.name= "Second";
  second.kind= "semi-concrete";
  second.templ= "%s";
  second.sorter_path= "second.luau";
  string error;
  QVERIFY2 (athena_namespace_save (context, first, error), as_charp (error));
  QVERIFY2 (athena_namespace_save (context, second, error), as_charp (error));
  std::shared_ptr<const athena_namespace_definition> saved_first, saved_second;
  QCOMPARE (athena_namespace_get (context, "First", saved_first, error),
            namespace_query_status::ok);
  QCOMPARE (athena_namespace_get (context, "Second", saved_second, error),
            namespace_query_status::ok);
  QVERIFY (saved_first && saved_second);
  QVERIFY (saved_first->uuid != "" && saved_second->uuid != "");

  auto members= [] {
    std::vector<athena_namespace_match> values;
    for (const char* value: {"b", "a"}) {
      athena_namespace_match match;
      match.stem= value;
      match.captures= {string (value)};
      match.capture_types= {string ("string")};
      match.ambiguous= false;
      values.push_back (std::move (match));
    }
    return namespace_records<athena_namespace_match> (std::move (values));
  };

  string first_path;
  QVERIFY2 (athena_namespace_generate_product_sorter (
              context, *saved_first, *saved_second, "%s",
              "lexicographic-first", first_path, error),
            as_charp (error));
  fs::path descriptor= root / std::string (first_path.data (),
                                           (size_t) N(first_path));
  std::ifstream descriptor_input (descriptor);
  std::ostringstream descriptor_text;
  descriptor_text << descriptor_input.rdbuf ();
  QVERIFY (descriptor_text.str ().find (
    "\"athena-namespace-sorter-composition\"") != std::string::npos);
  QVERIFY (descriptor_text.str ().find ("function(") == std::string::npos);

  athena_namespace_definition product;
  product.uuid= "test-product";
  product.name= "Product";
  product.kind= "semi-concrete";
  product.templ= "%s";
  product.sorter_path= first_path;
  auto first_priority= members ();
  QVERIFY2 (sort_namespace_members (
              context, product, first_priority, error), as_charp (error));
  QCOMPARE (first_priority[0].stem, string ("a"));

  string second_path;
  QVERIFY2 (athena_namespace_generate_product_sorter (
              context, *saved_first, *saved_second, "%s",
              "lexicographic-second", second_path, error),
            as_charp (error));
  product.sorter_path= second_path;
  auto second_priority= members ();
  QVERIFY2 (sort_namespace_members (
              context, product, second_priority, error), as_charp (error));
  QCOMPARE (second_priority[0].stem, string ("b"));

  string union_path;
  QVERIFY2 (athena_namespace_generate_product_sorter (
              context, *saved_first, *saved_second, "%s",
              "constraint-union", union_path, error),
            as_charp (error));
  product.sorter_path= union_path;
  auto contradictory= members ();
  QVERIFY (!sort_namespace_members (context, product, contradictory, error));
  QVERIFY (error != "");
  QCOMPARE (contradictory[0].stem, string ("b"));

  std::string long_text (800, 'x');
  std::vector<athena_namespace_match> long_values;
  athena_namespace_match long_match;
  long_match.stem= string (long_text.c_str ());
  long_match.captures= {string (long_text.c_str ())};
  long_match.capture_types= {string ("string")};
  long_match.ambiguous= false;
  long_values.push_back (std::move (long_match));
  namespace_records<athena_namespace_match> long_members (
    std::move (long_values));
  product.sorter_path= first_path;
  QVERIFY2 (sort_namespace_members (context, product, long_members, error),
            as_charp (error));
  QCOMPARE (N(long_members[0].captures[0]), (int) long_text.size ());

  {
    std::ofstream first_source (
      root / "first.luau", std::ios::binary | std::ios::trunc);
    first_source <<
      "return { version = 1, compare = function(a, b) "
      "return -athena.byte_compare(a[1].text, b[1].text) end }\n";
  }
  auto invalidated= members ();
  QVERIFY2 (sort_namespace_members (
              context, product, invalidated, error), as_charp (error));
  QCOMPARE (invalidated[0].stem, string ("b"));

  athena_namespace_definition recursive= *saved_first;
  recursive.sorter_path= first_path;
  QVERIFY2 (athena_namespace_save (context, recursive, error), as_charp (error));
  auto dependency_cycle= members ();
  QVERIFY (!sort_namespace_members (
    context, product, dependency_cycle, error));
  QVERIFY (error != "");
}

void
NamespaceOntologyTest::migratesSortersWithExplicitMapping () {
  QTemporaryDir temporary ("/home/felix/tmp/athena-luau-migration-XXXXXX");
  QVERIFY (temporary.isValid ());
  fs::path root= fs::u8path (temporary.path ().toStdString ());
  fs::create_directories (root / "dependencies");
  std::string vaultfile_error;
  QVERIFY2 (athena_vaultfile_write (
              root, AthenaVaultfileInfo {}, vaultfile_error),
            vaultfile_error.c_str ());
  {
    std::ofstream old_sorter (root / "dependencies" / "old.c");
    old_sorter << "/* historical sorter: intentionally never executed */\n";
    std::ofstream new_sorter (root / "dependencies" / "new.luau");
    new_sorter <<
      "return { version = 1, key = function(fields) "
      "return { fields[1].text } end }\n";
    std::ofstream bad_sorter (root / "dependencies" / "bad.luau");
    bad_sorter << "return { version = 1, key = function(\n";
  }
  fs::path database= root / "ns.sqlite";
  sqlite3* raw_db= nullptr;
  QCOMPARE (sqlite3_open_v2 (
              database.c_str (), &raw_db,
              SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr), SQLITE_OK);
  QVERIFY (raw_db != nullptr);
  const char* schema=
    "CREATE TABLE namespaces("
    "name TEXT PRIMARY KEY,kind TEXT NOT NULL,template TEXT NOT NULL,"
    "sorter_path TEXT NOT NULL,style_path TEXT NOT NULL DEFAULT '',"
    "sorter_trivial INTEGER NOT NULL DEFAULT 0,"
    "initial_content_path TEXT NOT NULL DEFAULT '',"
    "homepage_path TEXT NOT NULL DEFAULT '',uuid TEXT NOT NULL);"
    "INSERT INTO namespaces(name,kind,template,sorter_path,uuid) VALUES("
    "'Legacy','semi-concrete','%s','dependencies/old.c','legacy-uuid');";
  QCOMPARE (sqlite3_exec (raw_db, schema, nullptr, nullptr, nullptr), SQLITE_OK);
  sqlite3_close (raw_db);

  QCOMPARE (query_text (
    database, "SELECT sorter_path FROM namespaces WHERE name='Legacy';"),
    std::string ("dependencies/old.c"));

  const std::string valid_map=
    R"([{"from":"dependencies/old.c","to":"dependencies/new.luau"}])";
  const std::string unrelated_map=
    R"([{"from":"dependencies/other.c","to":"dependencies/new.luau"}])";
  const std::string invalid_map=
    R"([{"from":"dependencies/old.c","to":"dependencies/bad.luau"}])";
  const std::string object_map=
    R"({"from":"dependencies/old.c","to":"dependencies/new.luau"})";

  QCOMPARE (athena_upgrade_vault_sorters_cli (
              root, object_map, std::nullopt, true), 2);

  QCOMPARE (athena_upgrade_vault_sorters_cli (
              root, unrelated_map, std::nullopt, true), 3);
  QCOMPARE (query_text (
    database, "SELECT sorter_path FROM namespaces WHERE name='Legacy';"),
    std::string ("dependencies/old.c"));

  QCOMPARE (athena_upgrade_vault_sorters_cli (
              root, invalid_map, std::nullopt, true), 2);
  QCOMPARE (query_text (
    database, "SELECT sorter_path FROM namespaces WHERE name='Legacy';"),
    std::string ("dependencies/old.c"));

  fs::path migration_dir= root / ".athena" / "migrations";
  std::error_code ec;
  fs::remove_all (migration_dir, ec);
  QVERIFY (!ec);
  QCOMPARE (athena_upgrade_vault_sorters_cli (
              root, valid_map, std::nullopt, true), 0);
  QCOMPARE (query_text (
    database, "SELECT sorter_path FROM namespaces WHERE name='Legacy';"),
    std::string ("dependencies/old.c"));
  QVERIFY (!fs::exists (migration_dir));
  QVERIFY (!fs::exists (
    root / ".athena" / "namespace-sorter-migration.json"));

  QCOMPARE (athena_upgrade_vault_sorters_cli (
              root, valid_map, std::nullopt, false), 0);
  QCOMPARE (query_text (
    database, "SELECT sorter_path FROM namespaces WHERE name='Legacy';"),
    std::string ("dependencies/new.luau"));
  QVERIFY (!fs::exists (root / "dependencies" / "old.c"));
  QVERIFY (!fs::exists (
    root / ".athena" / "namespace-sorter-migration.json"));
  QVERIFY (fs::is_directory (migration_dir));
  size_t backups= 0;
  for (const fs::directory_entry& entry:
       fs::directory_iterator (migration_dir))
    if (entry.path ().extension () == ".bak") ++backups;
  QCOMPARE (backups, (size_t) 1);

  // Idempotent cleanup also removes a legacy source which reappears after the
  // database already points at the explicitly mapped Luau replacement.
  {
    std::ofstream recreated (root / "dependencies" / "old.c");
    recreated << "/* recreated stale legacy sorter */\n";
  }
  QVERIFY (fs::exists (root / "dependencies" / "old.c"));
  QCOMPARE (athena_upgrade_vault_sorters_cli (
              root, valid_map, std::nullopt, false), 0);
  QVERIFY (!fs::exists (root / "dependencies" / "old.c"));

  fs::path map_file= root / "sorter-map.json";
  {
    std::ofstream output (map_file);
    output << valid_map;
  }
  QCOMPARE (athena_upgrade_vault_sorters_cli (
              root, std::nullopt, map_file, false), 0);
  QCOMPARE (query_text (
    database, "SELECT sorter_path FROM namespaces WHERE name='Legacy';"),
    std::string ("dependencies/new.luau"));
  QCOMPARE (athena_upgrade_vault_sorters_cli (
              root, valid_map, map_file, true), 2);
}

QTEST_MAIN (NamespaceOntologyTest)
#include "namespace_ontology_test.moc"
