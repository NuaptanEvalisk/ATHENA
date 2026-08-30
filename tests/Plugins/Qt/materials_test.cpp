/******************************************************************************
* MODULE     : materials_test.cpp
* DESCRIPTION: Tests for the vault-native Materials store
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/materials.hpp"
#include "ATHENA/Data/materials_document.hpp"
#include "ATHENA/Data/materials_engine.hpp"
#include "ATHENA/Data/materials_recognition.hpp"
#include "ATHENA/Data/materials_schema.hpp"
#include "ATHENA/Data/materials_zotero.hpp"
#include "ATHENA/Data/vault.hpp"
#include "convert.hpp"

#include <QFile>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <filesystem>

namespace fs= std::filesystem;

namespace {

MaterialRecord
sample_material (const std::string& title, const std::string& family,
                 const std::string& date) {
  MaterialRecord material;
  material.item_type= "book";
  material.fields= {{"title", title, "", 0}, {"date", date, "", 0}};
  material.creators= {{"author", "Loring W.", family, "", "", 0}};
  return material;
}

bool
write_bytes (const fs::path& path, const QByteArray& bytes) {
  QFile file (QString::fromUtf8 (path.u8string ().c_str ()));
  return file.open (QIODevice::WriteOnly) && file.write (bytes) == bytes.size ();
}

bool
tree_contains_compound (tree value, const char* label) {
  if (is_compound (value, label)) return true;
  for (int i=0; i<N(value); ++i)
    if (tree_contains_compound (value[i], label)) return true;
  return false;
}

bool
find_compound (tree value, const char* label, tree& result) {
  if (is_compound (value, label)) {
    result= value;
    return true;
  }
  for (int i=0; i<N(value); ++i)
    if (find_compound (value[i], label, result)) return true;
  return false;
}

} // namespace

class MaterialsTest: public QObject {
  Q_OBJECT

private slots:
  void vaultfileDefaultsAndRoundTrip ();
  void rejectsPathsOutsideVault ();
  void createsSearchesAndUpdatesMaterials ();
  void canonicalizesAndDeduplicatesFiles ();
  void deduplicatesMaterialImportsWithDifferentFilenames ();
  void preservesAliasesAndPrimaryAttachmentOnMerge ();
  void recognizesMetadataAndIdentifiersWithoutUsingFilename ();
  void recognizesIsbnThroughOpenLibrary ();
  void recognizesJournalMastheadWithoutIdentifiers ();
  void cancelsBlockingRecognition ();
  void loadsPinnedZoteroSchema ();
  void listsBundledCslStyles ();
  void importsBibtexAndRendersCsl ();
  void parsesZoteroLibraryAndTracksSource ();
  void rendersMaterialInfoWithGenericMacros ();
};

void
MaterialsTest::vaultfileDefaultsAndRoundTrip () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root= fs::u8path (temporary.path ().toStdString ());

  AthenaVaultfileInfo info;
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());

  AthenaVaultfileInfo loaded;
  QVERIFY2 (athena_vaultfile_read (root, loaded, error), error.c_str ());
  QCOMPARE (loaded.materials_db_path, std::string ("materials.sqlite"));
  QCOMPARE (loaded.materials_directory, std::string ("materials"));

  loaded.materials_db_path= "indexes/library.sqlite";
  loaded.materials_directory= "Library Materials";
  QVERIFY2 (athena_vaultfile_write (root, loaded, error), error.c_str ());
  AthenaVaultfileInfo reread;
  QVERIFY2 (athena_vaultfile_read (root, reread, error), error.c_str ());
  QCOMPARE (reread.materials_db_path, std::string ("indexes/library.sqlite"));
  QCOMPARE (reread.materials_directory, std::string ("Library Materials"));

  std::vector<std::string> fields= athena_vaultfile_to_fields (reread);
  QCOMPARE (fields.size (), (size_t) 16);
  QCOMPARE (fields[15], std::string ("artifact-title-filter.lst"));
  QCOMPARE (fields[13], std::string ("indexes/library.sqlite"));
  QCOMPARE (fields[14], std::string ("Library Materials"));
}

void
MaterialsTest::rejectsPathsOutsideVault () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root= fs::u8path (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  info.materials_directory= "../outside";
  MaterialsStore store;
  std::string error;
  QVERIFY (!store.open (root, info, error));
  QVERIFY (QString::fromStdString (error).contains ("escape"));
}

void
MaterialsTest::createsSearchesAndUpdatesMaterials () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root= fs::u8path (temporary.path ().toStdString ());
  MaterialsStore store;
  std::string error;
  QVERIFY2 (store.open (root, AthenaVaultfileInfo {}, error), error.c_str ());

  MaterialRecord material= sample_material (
    "Introduction to Manifolds", "Tu", "2011");
  material.identifiers= {{"DOI", "https://doi.org/10.1000/Example", ""}};
  material.tags= {"geometry", "manifolds"};
  material.provenance= {{"title", "manual", "", material.field ("title"),
                         1.0}};
  QVERIFY2 (store.create (material, error), error.c_str ());
  QVERIFY (!material.uuid.empty ());
  QCOMPARE (material.revision, (std::int64_t) 1);

  std::optional<MaterialRecord> loaded= store.get (material.uuid, error);
  QVERIFY2 (loaded.has_value (), error.c_str ());
  QCOMPARE (loaded->field ("title"), std::string ("Introduction to Manifolds"));
  QCOMPARE (loaded->identifiers[0].normalized_value,
            std::string ("10.1000/example"));

  std::vector<MaterialSearchHit> hits= store.search ("manifold", 20, error);
  QVERIFY2 (error.empty (), error.c_str ());
  QCOMPARE (hits.size (), (size_t) 1);
  QCOMPARE (hits[0].uuid, material.uuid);

  auto title= std::find_if (
    loaded->fields.begin (), loaded->fields.end (),
    [] (const MaterialField& field) { return field.name == "title"; });
  QVERIFY (title != loaded->fields.end ());
  title->value= "An Introduction to Manifolds";
  QVERIFY2 (store.update (*loaded, loaded->revision, error), error.c_str ());
  QCOMPARE (loaded->revision, (std::int64_t) 2);
  hits= store.search ("Introduction", 20, error);
  QCOMPARE (hits.size (), (size_t) 1);

  std::optional<std::string> by_doi= store.material_for_identifier (
    "doi", "DOI: 10.1000/EXAMPLE", error);
  QVERIFY2 (by_doi.has_value (), error.c_str ());
  QCOMPARE (*by_doi, material.uuid);
}

void
MaterialsTest::canonicalizesAndDeduplicatesFiles () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root= fs::u8path (temporary.path ().toStdString ());
  MaterialsStore store;
  std::string error;
  QVERIFY2 (store.open (root, AthenaVaultfileInfo {}, error), error.c_str ());

  MaterialRecord first= sample_material (
    "Introduction to Manifolds", "Tu", "2011-01-01");
  QVERIFY2 (store.create (first, error), error.c_str ());
  fs::path source= root / "paper2.PDF";
  QVERIFY (write_bytes (source, "%PDF-1.7\nATHENA material test\n"));

  MaterialImportResult imported;
  QVERIFY2 (store.import_file (first.uuid, source, "document", true,
                               imported, error), error.c_str ());
  QVERIFY (!imported.duplicate);
  QCOMPARE (imported.attachment.canonical_name,
            std::string ("Tu - 2011 - Introduction to Manifolds.pdf"));
  QVERIFY (fs::exists (root / imported.attachment.stored_path));
  QVERIFY (imported.attachment.primary);

  MaterialRecord second= sample_material ("Duplicate", "Someone", "2020");
  QVERIFY2 (store.create (second, error), error.c_str ());
  MaterialImportResult duplicate;
  QVERIFY2 (store.import_file (second.uuid, source, "document", true,
                               duplicate, error), error.c_str ());
  QVERIFY (duplicate.duplicate);
  QCOMPARE (duplicate.existing_material_uuid, first.uuid);
  QCOMPARE (store.attachments (second.uuid, error).size (), (size_t) 0);
}

void
MaterialsTest::deduplicatesMaterialImportsWithDifferentFilenames () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root= fs::u8path (temporary.path ().toStdString ());
  MaterialsStore store;
  std::string error;
  QVERIFY2 (store.open (root, AthenaVaultfileInfo {}, error), error.c_str ());

  fs::path first_source= root / "original-name.pdf";
  fs::path second_source= root / "renamed-copy.pdf";
  const QByteArray contents= "%PDF-1.7\nidentical material\n";
  QVERIFY (write_bytes (first_source, contents));
  QVERIFY (write_bytes (second_source, contents));

  MaterialRecord first= sample_material ("Canonical", "Author", "2026");
  MaterialImportResult first_import;
  QVERIFY2 (store.import_material_file (
              first, first_source, "document", true, first_import, error),
            error.c_str ());
  QVERIFY (!first_import.duplicate);

  MaterialRecord second= sample_material ("Duplicate", "Someone", "2025");
  MaterialImportResult second_import;
  QVERIFY2 (store.import_material_file (
              second, second_source, "document", true, second_import, error),
            error.c_str ());
  QVERIFY (second_import.duplicate);
  QCOMPARE (second.uuid, first.uuid);
  QCOMPARE (second.field ("title"), std::string ("Canonical"));
  QCOMPARE (second_import.existing_material_uuid, first.uuid);

  std::vector<MaterialSearchHit> materials= store.list (100, 0, error);
  QVERIFY2 (error.empty (), error.c_str ());
  QCOMPARE (materials.size (), (size_t) 1);
  QCOMPARE (store.attachments (first.uuid, error).size (), (size_t) 1);
  size_t managed_files= 0;
  for (const fs::directory_entry& entry:
       fs::directory_iterator (store.materials_directory ()))
    if (entry.is_regular_file ()) managed_files++;
  QCOMPARE (managed_files, (size_t) 1);
}

void
MaterialsTest::preservesAliasesAndPrimaryAttachmentOnMerge () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root= fs::u8path (temporary.path ().toStdString ());
  MaterialsStore store;
  std::string error;
  QVERIFY2 (store.open (root, AthenaVaultfileInfo {}, error), error.c_str ());

  MaterialRecord canonical= sample_material ("Canonical", "Author", "2025");
  MaterialRecord duplicate= sample_material ("Duplicate", "Author", "2025");
  QVERIFY2 (store.create (canonical, error), error.c_str ());
  QVERIFY2 (store.create (duplicate, error), error.c_str ());

  fs::path first_file= root / "first.pdf";
  fs::path second_file= root / "second.pdf";
  QVERIFY (write_bytes (first_file, "first"));
  QVERIFY (write_bytes (second_file, "second"));
  MaterialImportResult first_import;
  MaterialImportResult second_import;
  QVERIFY2 (store.import_file (canonical.uuid, first_file, "document", true,
                               first_import, error), error.c_str ());
  QVERIFY2 (store.import_file (duplicate.uuid, second_file, "supplement", true,
                               second_import, error), error.c_str ());

  QVERIFY2 (store.merge (canonical.uuid, duplicate.uuid, error), error.c_str ());
  QCOMPARE (store.resolve_uuid (duplicate.uuid, error), canonical.uuid);
  std::optional<MaterialRecord> through_alias= store.get (duplicate.uuid, error);
  QVERIFY2 (through_alias.has_value (), error.c_str ());
  QCOMPARE (through_alias->uuid, canonical.uuid);

  std::vector<MaterialAttachment> attachments= store.attachments (
    canonical.uuid, error);
  QCOMPARE (attachments.size (), (size_t) 2);
  QCOMPARE (std::count_if (attachments.begin (), attachments.end (),
                           [] (const MaterialAttachment& attachment) {
                             return attachment.primary;
                           }), 1);
}

void
MaterialsTest::recognizesMetadataAndIdentifiersWithoutUsingFilename () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root= fs::u8path (temporary.path ().toStdString ());
  fs::path source= root / "paper2.pdf";
  QVERIFY (write_bytes (source, "%PDF test"));

  fs::path metadata= root / "metadata-extractor";
  fs::path text= root / "text-extractor";
  QVERIFY (write_bytes (
    metadata,
    "#!/bin/sh\n"
    "printf '%s' '[{\"Title\":\"Rings With Minimal Condition for Left "
    "Ideals\",\"Author\":\"Charles Hopkins\",\"CreateDate\":\"1939\","
    "\"DOI\":\"10.1000/ATHENA.Test\"}]'\n"));
  QVERIFY (write_bytes (
    text,
    "#!/bin/sh\n"
    "printf '%s' 'Rings With Minimal Condition. ISBN 978-0-306-40615-7'\n"));
  fs::permissions (metadata,
                   fs::perms::owner_read | fs::perms::owner_write |
                   fs::perms::owner_exec);
  fs::permissions (text,
                   fs::perms::owner_read | fs::perms::owner_write |
                   fs::perms::owner_exec);

  MaterialRecognitionOptions options;
  options.metadata_extractor= metadata.string ();
  options.pdf_text_extractor= text.string ();
  std::vector<std::string> stages;
  options.progress= [&] (const std::string& stage) {
    stages.push_back (stage);
  };
  MaterialRecognitionResult recognized;
  std::string error;
  QVERIFY2 (athena_material_recognize_file (
              source, options, recognized, error), error.c_str ());
  QCOMPARE (recognized.material.field ("title"),
            std::string ("Rings With Minimal Condition for Left Ideals"));
  QCOMPARE (recognized.material.review_state, std::string ("needs_review"));
  QVERIFY (std::any_of (
    recognized.identifiers.begin (), recognized.identifiers.end (),
    [] (const MaterialIdentifier& identifier) {
      return identifier.scheme == "doi" &&
             identifier.normalized_value == "10.1000/athena.test";
    }));
  QVERIFY (std::any_of (
    recognized.identifiers.begin (), recognized.identifiers.end (),
    [] (const MaterialIdentifier& identifier) {
      return identifier.scheme == "isbn" &&
             identifier.normalized_value == "9780306406157";
    }));
  QVERIFY (recognized.material.field ("title") != "paper2");
  QVERIFY (std::find (stages.begin (), stages.end (),
                      "Reading embedded metadata with exiftool") !=
           stages.end ());
  QVERIFY (std::find (stages.begin (), stages.end (),
                      "Preparing recognized metadata for review") !=
           stages.end ());
}

void
MaterialsTest::recognizesIsbnThroughOpenLibrary () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root= fs::u8path (temporary.path ().toStdString ());
  fs::path source= root / "book.pdf";
  QVERIFY (write_bytes (source, "%PDF test"));

  fs::path metadata= root / "metadata-extractor";
  fs::path text= root / "text-extractor";
  QVERIFY (write_bytes (
    metadata, "#!/bin/sh\nprintf '%s' '[{\"PageCount\":12}]'\n"));
  QVERIFY (write_bytes (
    text,
    "#!/bin/sh\n"
    "case \" $* \" in\n"
    "  *\" -f 12 -l 12 \"*) printf '232\\nNotes\\n' ;;\n"
    "  *) printf 'Chapter XVI\\n\\nThe Universal Enveloping Algebra\\n"
    "\\n221\\nISBN 978-1-4613-8114-3\\n' ;;\n"
    "esac\n"));
  fs::permissions (metadata,
                   fs::perms::owner_read | fs::perms::owner_write |
                   fs::perms::owner_exec);
  fs::permissions (text,
                   fs::perms::owner_read | fs::perms::owner_write |
                   fs::perms::owner_exec);

  QTcpServer server;
  QVERIFY (server.listen (QHostAddress::LocalHost));
  connect (&server, &QTcpServer::newConnection, &server, [&] {
    QTcpSocket* socket= server.nextPendingConnection ();
    QVERIFY (socket != nullptr);
    QByteArray body= R"({"ISBN:9781461381143":{"title":"Basic Theory of Algebraic Groups and Lie Algebras","authors":[{"name":"Gerhard P. Hochschild"}],"pagination":"267","publishers":[{"name":"Springer London, Limited"}],"publish_date":"2012","url":"https://openlibrary.org/books/OL37150396M"}})";
    QByteArray response= "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                         "Content-Length: " + QByteArray::number (body.size ()) +
                         "\r\nConnection: close\r\n\r\n" + body;
    socket->write (response);
    socket->disconnectFromHost ();
  });

  MaterialRecognitionOptions options;
  options.metadata_extractor= metadata.string ();
  options.pdf_text_extractor= text.string ();
  options.providers.open_library= true;
  options.providers.open_library_endpoint=
    "http://127.0.0.1:" + std::to_string (server.serverPort ()) + "/books";
  std::vector<std::string> stages;
  options.progress= [&] (const std::string& stage) {
    stages.push_back (stage);
  };
  MaterialRecognitionResult recognized;
  std::string error;
  QVERIFY2 (athena_material_recognize_file (
              source, options, recognized, error), error.c_str ());
  QCOMPARE (recognized.material.field ("title"),
            std::string ("The Universal Enveloping Algebra"));
  QCOMPARE (recognized.material.item_type, std::string ("bookSection"));
  QCOMPARE (recognized.material.field ("bookTitle"),
            std::string ("Basic Theory of Algebraic Groups and Lie Algebras"));
  QCOMPARE (recognized.material.field ("pages"), std::string ("221-232"));
  QCOMPARE (recognized.material.field ("publisher"),
            std::string ("Springer London, Limited"));
  QCOMPARE (recognized.material.field ("date"), std::string ("2012"));
  QCOMPARE (recognized.material.creators.size (), (size_t) 1);
  QCOMPARE (recognized.material.creators[0].literal,
            std::string ("Gerhard P. Hochschild"));
  QCOMPARE (recognized.confidence, 0.96);
  QVERIFY (recognized.external_metadata_used);
  QVERIFY (std::any_of (
    stages.begin (), stages.end (), [] (const std::string& stage) {
      return stage.rfind ("Waiting for Open Library metadata for ISBN", 0) == 0;
    }));
}

void
MaterialsTest::recognizesJournalMastheadWithoutIdentifiers () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root= fs::u8path (temporary.path ().toStdString ());
  fs::path source= root / "scan.pdf";
  fs::path metadata= root / "metadata-extractor";
  fs::path text= root / "text-extractor";
  QVERIFY (write_bytes (source, "%PDF test"));
  QVERIFY (write_bytes (
    metadata, "#!/bin/sh\nprintf '%s' '[{\"PageCount\":3}]'\n"));
  QVERIFY (write_bytes (
    text,
    "#!/bin/sh\n"
    "printf 'Annales Mathematicae Silesianae 8. Katowice 1995, 43-45\\n"
    "Prace Naukowe Uniwersytetu Slaskiego nr 1523\\n\\nA\\n\\nNOTE\\n"
    "\\nON T H E FRECHET\\n\\nTHEOREM\\n\\nANDRZEJ NOWAK\\n\\n"
    "A b s t r a c t . We give conditions under which every measurable "
    "function is a limit.\\n'\n"));
  fs::permissions (metadata,
                   fs::perms::owner_read | fs::perms::owner_write |
                   fs::perms::owner_exec);
  fs::permissions (text,
                   fs::perms::owner_read | fs::perms::owner_write |
                   fs::perms::owner_exec);

  MaterialRecognitionOptions options;
  options.metadata_extractor= metadata.string ();
  options.pdf_text_extractor= text.string ();
  MaterialRecognitionResult recognized;
  std::string error;
  QVERIFY2 (athena_material_recognize_file (
              source, options, recognized, error), error.c_str ());
  QCOMPARE (recognized.material.item_type, std::string ("journalArticle"));
  QCOMPARE (recognized.material.field ("title"),
            std::string ("A NOTE ON THE FRECHET THEOREM"));
  QCOMPARE (recognized.material.field ("publicationTitle"),
            std::string ("Annales Mathematicae Silesianae"));
  QCOMPARE (recognized.material.field ("volume"), std::string ("8"));
  QCOMPARE (recognized.material.field ("date"), std::string ("1995"));
  QCOMPARE (recognized.material.field ("pages"), std::string ("43-45"));
  QCOMPARE (recognized.material.creators.size (), (size_t) 1);
  QCOMPARE (recognized.material.creators[0].literal,
            std::string ("Andrzej Nowak"));
  QCOMPARE (recognized.confidence, 0.82);
}

void
MaterialsTest::cancelsBlockingRecognition () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root= fs::u8path (temporary.path ().toStdString ());
  fs::path source= root / "book.pdf";
  fs::path extractor= root / "blocking-extractor";
  QVERIFY (write_bytes (source, "%PDF test"));
  QVERIFY (write_bytes (extractor, "#!/bin/sh\nexec sleep 10\n"));
  fs::permissions (extractor,
                   fs::perms::owner_read | fs::perms::owner_write |
                   fs::perms::owner_exec);

  MaterialRecognitionOptions options;
  options.metadata_extractor= extractor.string ();
  int cancellation_checks= 0;
  options.cancelled= [&] { return ++cancellation_checks >= 3; };
  MaterialRecognitionResult recognized;
  std::string error;
  QElapsedTimer elapsed;
  elapsed.start ();
  QVERIFY (!athena_material_recognize_file (
    source, options, recognized, error));
  QCOMPARE (error, std::string ("Material recognition cancelled"));
  QVERIFY (elapsed.elapsed () < 3000);
}

void
MaterialsTest::loadsPinnedZoteroSchema () {
  MaterialSchema schema;
  std::string error;
  QVERIFY2 (schema.load_bundled (error), error.c_str ());
  QCOMPARE (schema.version (), 45);
  QVERIFY (schema.item_types ().size () >= 35);
  const MaterialSchemaItemType* book= schema.item_type ("book");
  const MaterialSchemaItemType* article= schema.item_type ("journalArticle");
  QVERIFY (book != nullptr);
  QVERIFY (article != nullptr);
  QCOMPARE (book->label, std::string ("Book"));
  QVERIFY (std::any_of (
    book->fields.begin (), book->fields.end (),
    [] (const MaterialSchemaField& field) { return field.name == "ISBN"; }));
  QVERIFY (std::any_of (
    book->creator_types.begin (), book->creator_types.end (),
    [] (const MaterialSchemaCreatorType& creator) {
      return creator.name == "author" && creator.primary;
    }));
  for (const char* field:
       {"publicationTitle", "date", "volume", "issue", "pages"})
    QVERIFY (std::any_of (
      article->fields.begin (), article->fields.end (),
      [&] (const MaterialSchemaField& schema_field) {
        return schema_field.name == field;
      }));
}

void
MaterialsTest::listsBundledCslStyles () {
  std::vector<MaterialCslStyle> styles;
  std::string error;
  QVERIFY2 (athena_materials_list_csl_styles (styles, error), error.c_str ());
  QVERIFY (styles.size () > 20);
  auto has_style= [&] (const std::string& name) {
    return std::any_of (styles.begin (), styles.end (),
                        [&] (const MaterialCslStyle& style) {
                          return style.name == name && !style.title.empty ();
                        });
  };
  QVERIFY (has_style ("apa"));
  QVERIFY (has_style ("ieee"));
  QVERIFY (has_style ("springer-mathphys"));
  QVERIFY (!has_style ("ams"));
}

void
MaterialsTest::importsBibtexAndRendersCsl () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path bib= fs::u8path (temporary.path ().toStdString ()) / "library.bib";
  QVERIFY (write_bytes (
    bib,
    "@book{tu2011,\n"
    " author={Tu, Loring W.},\n"
    " title={An Introduction to Manifolds},\n"
    " year={2011}, publisher={Springer}, isbn={9781441973993}\n"
    "}\n"
    "@article{paper2024, title={A Study}, author={Doe, Jane},\n"
    " journaltitle={Journal of Tests}, year={2024}, volume={12},\n"
    " number={3}, pages={10-20}, doi={10.1/test}}\n"
    "@incollection{chapter2023, title={A Chapter}, author={Doe, Jane},\n"
    " booktitle={The Book}, editor={Smith, John}, publisher={Press},\n"
    " address={Berlin}, year={2023}, pages={5-9}}\n"));
  std::vector<MaterialRecord> records;
  std::string error;
  QVERIFY2 (athena_materials_import_bibtex (bib, records, error), error.c_str ());
  QCOMPARE (records.size (), (size_t) 3);
  auto record_named= [&] (const std::string& title) -> MaterialRecord* {
    auto found= std::find_if (
      records.begin (), records.end (), [&] (const MaterialRecord& record) {
        return record.field ("title") == title;
      });
    return found == records.end () ? nullptr : &*found;
  };
  MaterialRecord* book_ptr= record_named ("An Introduction to Manifolds");
  MaterialRecord* article_ptr= record_named ("A Study");
  MaterialRecord* chapter_ptr= record_named ("A Chapter");
  QVERIFY (book_ptr != nullptr);
  QVERIFY (article_ptr != nullptr);
  QVERIFY (chapter_ptr != nullptr);
  MaterialRecord& book= *book_ptr;
  QCOMPARE (book.item_type, std::string ("book"));
  QCOMPARE (book.field ("title"),
            std::string ("An Introduction to Manifolds"));
  QCOMPARE (book.creators[0].family, std::string ("Tu"));
  QVERIFY (!book.identifiers.empty ());
  MaterialRecord& article= *article_ptr;
  QCOMPARE (article.item_type, std::string ("journalArticle"));
  QCOMPARE (article.field ("publicationTitle"),
            std::string ("Journal of Tests"));
  QCOMPARE (article.field ("volume"), std::string ("12"));
  QCOMPARE (article.field ("issue"), std::string ("3"));
  MaterialRecord& chapter= *chapter_ptr;
  QCOMPARE (chapter.item_type, std::string ("bookSection"));
  QCOMPARE (chapter.field ("bookTitle"), std::string ("The Book"));
  QCOMPARE (chapter.field ("publisher"), std::string ("Press"));
  book.uuid= "11111111-1111-4111-8111-111111111111";
  book.creators= {{"author", "", "", "Loring W. Tu", "", 0}};

  MaterialCitationCluster cluster;
  cluster.items.push_back ({book.uuid, "page", "42", false});
  MaterialRenderedDocument rendered;
  QVERIFY2 (athena_materials_render (
              {book}, {cluster}, {}, "apa", rendered, error), error.c_str ());
  QCOMPARE (rendered.citation_html.size (), (size_t) 1);
  QVERIFY (QString::fromStdString (rendered.citation_html[0]).contains ("42"));
  QCOMPARE (rendered.bibliography.size (), (size_t) 1);
  QCOMPARE (rendered.bibliography[0].uuid, book.uuid);
  QVERIFY (QString::fromStdString (rendered.bibliography[0].html).contains (
    "Introduction to Manifolds"));
  QVERIFY (QString::fromStdString (rendered.bibliography[0].html).contains ("<i>"));
  QVERIFY (!QString::fromStdString (rendered.bibliography[0].html).contains ("{"));
  QVERIFY (!QString::fromStdString (rendered.bibliography[0].html).contains ("}"));

  MaterialRenderedDocument default_rendered;
  QVERIFY2 (athena_materials_render (
              {book}, {cluster}, {}, "", default_rendered, error),
            error.c_str ());
  QCOMPARE (default_rendered.citation_html.size (), (size_t) 1);
  QCOMPARE (default_rendered.bibliography.size (), (size_t) 1);
  QCOMPARE (default_rendered.bibliography[0].uuid, book.uuid);
}

void
MaterialsTest::parsesZoteroLibraryAndTracksSource () {
  const std::string response= R"json([
    {
      "key":"PAPER001", "version":12,
      "library":{"type":"user","id":1,"name":"My Library"},
      "data":{
        "key":"PAPER001", "version":12, "itemType":"journalArticle",
        "title":"A Structured Zotero Import", "date":"2026",
        "publicationTitle":"Journal of Materials", "volume":"7",
        "issue":"2", "pages":"10-21", "DOI":"10.1000/Zotero.Test",
        "creators":[
          {"creatorType":"author","firstName":"Ada","lastName":"Lovelace"},
          {"creatorType":"editor","name":"The Editorial Board"}
        ],
        "tags":[{"tag":"geometry"},{"tag":"imported"}],
        "collections":["COLL0001"], "relations":{"dc:relation":"x"}
      }
    },
    {
      "key":"FILE0001",
      "data":{
        "key":"FILE0001", "itemType":"attachment",
        "parentItem":"PAPER001", "linkMode":"imported_file",
        "title":"Full Text PDF", "filename":"paper.pdf",
        "contentType":"application/pdf"
      }
    },
    {
      "key":"NOTE0001",
      "data":{"key":"NOTE0001","itemType":"note","note":"private note"}
    },
    {
      "key":"FILE0002",
      "data":{
        "key":"FILE0002", "itemType":"attachment", "parentItem":"",
        "linkMode":"linked_file", "title":"Standalone source",
        "filename":"standalone.pdf", "contentType":"application/pdf"
      }
    }
  ])json";

  std::vector<ZoteroMaterialImport> imports;
  ZoteroParseSummary summary;
  std::string error;
  QVERIFY2 (athena_materials_parse_zotero_items (
              response, "server-A", "/users/0", imports, summary, error),
            error.c_str ());
  QCOMPARE (imports.size (), (size_t) 2);
  QCOMPARE (summary.bibliographic_items, 1);
  QCOMPARE (summary.child_attachments, 1);
  QCOMPARE (summary.standalone_attachments, 1);
  QCOMPARE (summary.ignored_notes, 1);

  const ZoteroMaterialImport& article= imports[0];
  QCOMPARE (article.item_key, std::string ("PAPER001"));
  QCOMPARE (article.source_reference, std::string ("zotero:user:1:PAPER001"));
  QCOMPARE (article.material.item_type, std::string ("journalArticle"));
  QCOMPARE (article.material.field ("title"),
            std::string ("A Structured Zotero Import"));
  QCOMPARE (article.material.field ("publicationTitle"),
            std::string ("Journal of Materials"));
  QCOMPARE (article.material.creators.size (), (size_t) 2);
  QCOMPARE (article.material.creators[0].given, std::string ("Ada"));
  QCOMPARE (article.material.creators[0].family, std::string ("Lovelace"));
  QCOMPARE (article.material.creators[1].literal,
            std::string ("The Editorial Board"));
  QCOMPARE (article.material.tags.size (), (size_t) 2);
  QCOMPARE (article.material.identifiers.size (), (size_t) 1);
  QCOMPARE (article.material.identifiers[0].scheme, std::string ("doi"));
  QCOMPARE (article.attachments.size (), (size_t) 1);
  QCOMPARE (article.attachments[0].item_key, std::string ("FILE0001"));
  QVERIFY (QString::fromStdString (article.material.extra_json).contains (
    "COLL0001"));

  std::vector<ZoteroMaterialImport> after_restart;
  ZoteroParseSummary restarted_summary;
  QVERIFY2 (athena_materials_parse_zotero_items (
              response, "server-B", "/users/0", after_restart,
              restarted_summary, error), error.c_str ());
  QCOMPARE (after_restart[0].source_reference, article.source_reference);

  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  MaterialsStore store;
  QVERIFY2 (store.open (fs::u8path (temporary.path ().toStdString ()),
                        AthenaVaultfileInfo {}, error), error.c_str ());
  MaterialRecord stored= article.material;
  QVERIFY2 (store.create (stored, error), error.c_str ());
  std::optional<std::string> by_source= store.material_for_source (
    "zotero", article.source_reference, error);
  QVERIFY2 (by_source.has_value (), error.c_str ());
  QCOMPARE (*by_source, stored.uuid);
}

void
MaterialsTest::rendersMaterialInfoWithGenericMacros () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root= fs::u8path (temporary.path ().toStdString ());
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, AthenaVaultfileInfo {}, error),
            error.c_str ());
  string load_error= vault_load (
    url_system (string (root.string ().c_str ())), "Materials page test",
    "maps.sqlite");
  QVERIFY2 (load_error == "", as_charp (load_error));

  MaterialsStore* store= vault_get_materials_store ();
  QVERIFY (store != nullptr);
  MaterialRecord material= sample_material (
    "A Note on the Fr\xC3\xA9" "chet Theorem", "Nowak", "1995");
  material.item_type= "journalArticle";
  QVERIFY2 (store->create (material, error), error.c_str ());
  fs::path source= root / fs::u8path ("fr\xC3\xA9" "chet.pdf");
  QVERIFY (write_bytes (source, "%PDF-1.7\n"));
  MaterialImportResult imported;
  QVERIFY2 (store->import_file (material.uuid, source, "document", true,
                                imported, error), error.c_str ());

  tree page= athena_material_info_page (material.uuid);
  QVERIFY (tree_contains_compound (page, "section*"));
  QVERIFY (tree_contains_compound (page, "subsection*"));
  QVERIFY (tree_contains_compound (page, "paragraph*"));
  QVERIFY (tree_contains_compound (page, "hlink"));
  QVERIFY (!tree_contains_compound (page, "title"));
  QVERIFY (!tree_contains_compound (page, "description-aligned-item"));
  tree link;
  QVERIFY (find_compound (page, "hlink", link));
  QVERIFY (N(link) == 2 && is_atomic (link[1]));
  string target_utf8= cork_to_utf8 (link[1]->label);
  fs::path target= fs::u8path (std::string (as_charp (target_utf8),
                                           N(target_utf8)));
  QCOMPARE (target.u8string (),
            (root / fs::u8path (imported.attachment.stored_path)).u8string ());
  QVERIFY (fs::exists (target));

  vault_close ();
}

QTEST_MAIN (MaterialsTest)
#include "materials_test.moc"
