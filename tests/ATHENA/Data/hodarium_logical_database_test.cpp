/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include "ATHENA/Data/hodarium_logical_database.hpp"
#include "ATHENA/Data/hodarium_logical_sync.hpp"
#include "ATHENA/Data/hodarium_materials_database.hpp"
#include "ATHENA/Data/namespaces_schema.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include <sqlite3.h>

bool headless_mode= true;
bool is_headless () { return true; }
namespace h= athena::hodarium;
namespace {
const std::string parent_id= "11111111-1111-4111-8111-111111111111";
const std::string child_id= "22222222-2222-4222-8222-222222222222";
std::string encoded (const QJsonArray& value) {
  return QJsonDocument (value).toJson (QJsonDocument::Compact).toStdString ();
}
h::logical_database_object definition (const std::string& id, const char* name,
                                        const std::vector<std::string>& parents= {}) {
  QJsonArray refs;
  for (const auto& parent: parents) refs.append (QString::fromStdString (parent));
  return {"namespace/" + id, "athena-namespace-v1", encoded (QJsonArray {
    1, QString::fromStdString (id), name, "concrete", "Notes %R", true,
    "", "", "", "Notes/First.ath", refs, QJsonArray {}})};
}
std::filesystem::path setup (const QTemporaryDir& dir) {
  const std::filesystem::path root (dir.path ().toStdString ());
  AthenaVaultfileInfo info;
  std::string error;
  if (!athena_vaultfile_write (root, info, error)) throw std::runtime_error (error);
  sqlite3* db= nullptr;
  if (!athena_namespace_database_open (root / info.namespace_db_path, true, db, error))
    throw std::runtime_error (error);
  sqlite3_close (db);
  return root;
}
}
class TestHodariumLogicalDatabase: public QObject {
  Q_OBJECT
private slots:
  void materialAuthoritativeRecords () {
    QTemporaryDir dir;
    auto root= setup (dir);
    MaterialRecord source;
    source.uuid= parent_id;
    source.fields.push_back ({"title","A book","",0});
    source.provenance.push_back ({"title","import","/private/source.pdf","A book",1.0});
    auto object= h::encode_material_record (source);
    QVERIFY (object.payload.find ("/private/")==std::string::npos);
    h::validate_logical_object (object);
    QCOMPARE (h::apply_material_object (root,{object.key,{},object.payload}),h::logical_apply_result::applied);
    source.fields[0].value= "Revised title";
    auto revised= h::encode_material_record (source);
    QCOMPARE (h::apply_material_object (root,{object.key,object.payload,revised.payload},[] {return false;}),
      h::logical_apply_result::stale);
    QCOMPARE (h::export_material_objects (root).front ().payload,object.payload);
    QCOMPARE (h::apply_material_object (root,{object.key,object.payload,revised.payload}),h::logical_apply_result::applied);
    QCOMPARE (h::apply_material_object (root,{object.key,object.payload,{}}),h::logical_apply_result::stale);
    auto alias= h::encode_material_alias (child_id,parent_id);
    QCOMPARE (h::apply_material_object (root,{alias.key,{},alias.payload}),h::logical_apply_result::applied);
    QCOMPARE (h::apply_material_object (root,{object.key,revised.payload,{}}),h::logical_apply_result::dependency_missing);
    QCOMPARE (h::apply_material_object (root,{alias.key,alias.payload,{}}),h::logical_apply_result::applied);
    QCOMPARE (h::apply_material_object (root,{object.key,revised.payload,{}}),h::logical_apply_result::applied);
    QVERIFY (h::export_material_objects (root).empty ());
  }

  void namespaceIdentityAndAtomicity () {
    QTemporaryDir dir;
    auto root= setup (dir), db= root / "ns.sqlite";
    auto parent= definition (parent_id, "Course"), child= definition (child_id, "Lecture notes", {parent_id});
    QCOMPARE (h::apply_namespace_objects (db, root, {{parent.key, {}, parent.payload},
      {child.key, {}, child.payload}}), h::logical_apply_result::applied);
    auto snapshot= h::export_namespace_objects (db, root);
    QCOMPARE (snapshot.size (), size_t (2));
    QCOMPARE (snapshot[0].payload, parent.payload);
    QCOMPARE (snapshot[1].payload, child.payload);
    auto renamed= definition (parent_id, "Renamed course");
    QCOMPARE (h::apply_namespace_objects (db, root, {{parent.key, parent.payload, renamed.payload}},
      [] { return false; }), h::logical_apply_result::stale);
    QCOMPARE (h::export_namespace_objects (db, root)[0].payload, parent.payload);
    QCOMPARE (h::apply_namespace_objects (db, root, {{parent.key, parent.payload, renamed.payload}}),
      h::logical_apply_result::applied);
    QCOMPARE (h::export_namespace_objects (db, root)[1].payload, child.payload);
    QCOMPARE (h::apply_namespace_objects (db, root, {{parent.key, parent.payload, renamed.payload}}),
      h::logical_apply_result::stale);
    QCOMPARE (h::apply_namespace_objects (db, root, {{parent.key, renamed.payload, {}}}),
      h::logical_apply_result::dependency_missing);
    auto detached= definition (child_id, "Lecture notes");
    QCOMPARE (h::apply_namespace_objects (db, root, {{parent.key, renamed.payload, {}},
      {child.key, child.payload, detached.payload}}), h::logical_apply_result::applied);
    QCOMPARE (h::export_namespace_objects (db, root).size (), size_t (1));
    auto bad= QJsonDocument::fromJson (QByteArray::fromStdString (detached.payload)).array ();
    bad[4]= "Unsupported %X";
    QVERIFY_EXCEPTION_THROWN (h::validate_logical_object (
      {detached.key, detached.format, encoded (bad)}), std::runtime_error);
  }

  void cloneBaselineHistoryAndRecovery () {
    QTemporaryDir da, db;
    auto a= setup (da), b= setup (db);
    auto original= definition (parent_id, "Original course");
    for (const auto& root: {a,b})
      QCOMPARE (h::apply_namespace_objects (root / "ns.sqlite", root,
        {{original.key, {}, original.payload}}), h::logical_apply_result::applied);
    h::revision_store ja (a / "journal.sqlite"), jb (b / "journal.sqlite");
    auto first= h::capture_logical_databases (a, ja, "vault", "member-A");
    auto second= h::capture_logical_databases (b, jb, "vault", "member-B");
    QVERIFY (first.complete && second.complete);
    QCOMPARE (ja.applied ("vault", original.key), jb.applied ("vault", original.key));
    QVERIFY (!std::filesystem::exists (a / "artifact-title-filter.lst"));
    QCOMPARE (h::capture_logical_databases (a, ja, "vault", "member-A").captured, size_t (0));
    auto changed= definition (parent_id, "Updated course");
    QCOMPARE (h::apply_namespace_objects (a / "ns.sqlite", a,
      {{original.key, original.payload, changed.payload}}), h::logical_apply_result::applied);
    QCOMPARE (h::capture_logical_databases (a, ja, "vault", "member-A").captured, size_t (1));
    auto next= ja.get (*ja.applied ("vault", original.key));
    QVERIFY (next.has_value ()); QVERIFY (jb.receive (*next));
    athena::history::document_history_store history;
    std::string error;
    QVERIFY2 (history.open (b, error), error.c_str ());
    QVERIFY (h::apply_logical_revision (b, jb, history, next->id));
    QCOMPARE (h::export_namespace_objects (b / "ns.sqlite", b)[0].payload, changed.payload);
    QVERIFY (h::apply_logical_revision (b, jb, history, next->id));
    std::vector<athena::history::version_entry> versions;
    QVERIFY2 (history.list (next->relative_path, versions, error), error.c_str ());
    QVERIFY (!versions.empty ());
    std::string protected_bytes;
    QVERIFY2 (history.reconstruct (versions.front ().id, protected_bytes, error), error.c_str ());
    QCOMPARE (protected_bytes, original.payload);
  }
};
QTEST_APPLESS_MAIN (TestHodariumLogicalDatabase)
#include "hodarium_logical_database_test.moc"
