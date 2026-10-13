/******************************************************************************
* MODULE     : pdf_merge_test.cpp
* DESCRIPTION: Native compound PDF link resolution and page-reference regression
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "Subsystems/Pdf/pdf_merge.hpp"
#include "ATHENA/Data/node_reference_export.hpp"
#include "Subsystems/Pdf/PDFWriter/PDFWriter.h"
#include "Subsystems/Pdf/PDFWriter/PDFPage.h"
#include "Subsystems/Pdf/PDFWriter/PageContentContext.h"
#include "Subsystems/Pdf/PDFWriter/DocumentContext.h"
#include "Subsystems/Pdf/PDFWriter/ObjectsContext.h"
#include "Subsystems/Pdf/PDFWriter/DictionaryContext.h"
#include "Subsystems/Pdf/PDFWriter/IndirectObjectsReferenceRegistry.h"
#include "Subsystems/Pdf/PDFWriter/InputFile.h"
#include "Subsystems/Pdf/PDFWriter/PDFParser.h"
#include "Subsystems/Pdf/PDFWriter/PDFObjectCast.h"
#include "Subsystems/Pdf/PDFWriter/PDFArray.h"
#include "Subsystems/Pdf/PDFWriter/PDFDictionary.h"
#include "Subsystems/Pdf/PDFWriter/PDFIndirectObjectReference.h"
#include "Subsystems/Pdf/PDFWriter/PDFLiteralString.h"
#include "Subsystems/Pdf/PDFWriter/PDFName.h"
#include "Subsystems/Pdf/PDFWriter/PDFStreamInput.h"
#include "Subsystems/Pdf/PDFWriter/IByteReader.h"
#include <memory>
#include <stdexcept>

bool headless_mode= true;

namespace {
const std::string first_id= "11111111-1111-4111-8111-111111111111";
const std::string second_id= "22222222-2222-4222-8222-222222222222";
const std::string missing_id= "33333333-3333-4333-8333-333333333333";

void require (bool ok, const char* message) {
  if (!ok) throw std::runtime_error (message);
}
struct link {
  std::string uri;
  int internal_page= -1;
};
void member_pdf (const std::string& filename, int count, const std::vector<link>& links) {
  PDFWriter writer;
  require (writer.StartPDF (filename, ePDFVersion17) == PDFHummus::eSuccess, "start fixture");
  auto& objects= writer.GetObjectsContext ();
  std::vector<ObjectIDType> annotations, pages;
  for (std::size_t i= 0; i < links.size (); ++i)
    annotations.push_back (objects.GetInDirectObjectsRegistry ().AllocateNewObjectID ());
  for (int p= 0; p < count; ++p) {
    PDFPage page;
    page.SetMediaBox (PDFRectangle (0, 0, 200, 200));
    auto* content= writer.StartPageContentContext (&page);
    content->rg (0.2, 0.4, 0.8);
    content->re (10, 20, 80, 15);
    content->f ();
    require (writer.EndPageContentContext (content) == PDFHummus::eSuccess, "finish fixture content");
    if (p == 0) for (auto id: annotations)
      writer.GetDocumentContext ().RegisterAnnotationReferenceForNextPageWrite (id);
    auto written= writer.WritePageAndReturnPageID (&page);
    require (written.first == PDFHummus::eSuccess, "write fixture page");
    pages.push_back (written.second);
  }
  for (std::size_t i= 0; i < links.size (); ++i) {
    objects.StartNewIndirectObject (annotations[i]);
    auto* dictionary= objects.StartDictionary ();
    dictionary->WriteKey ("Type"); dictionary->WriteNameValue ("Annot");
    dictionary->WriteKey ("Subtype"); dictionary->WriteNameValue ("Link");
    dictionary->WriteKey ("Rect"); dictionary->WriteRectangleValue (PDFRectangle (10, 20, 90, 35));
    if (links[i].internal_page >= 0) {
      dictionary->WriteKey ("Dest");
      objects.StartArray ();
      objects.WriteNewIndirectObjectReference (pages.at (links[i].internal_page));
      objects.WriteName ("XYZ");
      objects.WriteDouble (12); objects.WriteDouble (150); objects.WriteNull ();
      objects.EndArray ();
    }
    else {
      dictionary->WriteKey ("A");
      auto* action= objects.StartDictionary ();
      action->WriteKey ("S"); action->WriteNameValue ("URI");
      action->WriteKey ("URI"); action->WriteLiteralStringValue (links[i].uri);
      require (objects.EndDictionary (action) == PDFHummus::eSuccess, "finish fixture action");
    }
    require (objects.EndDictionary (dictionary) == PDFHummus::eSuccess, "finish fixture annotation");
    objects.EndIndirectObject ();
  }
  require (writer.EndPDF () == PDFHummus::eSuccess, "finish fixture PDF");
}
struct parsed_pdf {
  InputFile file;
  PDFParser parser;
  explicit parsed_pdf (const std::string& name) {
    require (file.OpenFile (name) == PDFHummus::eSuccess, "open PDF");
    require (parser.StartPDFParsing (file.GetInputStream ()) == PDFHummus::eSuccess, "parse PDF");
  }
};
std::string contents (PDFParser& parser, unsigned long index) {
  RefCountPtr<PDFDictionary> page (parser.ParsePage (index));
  RefCountPtr<PDFObject> contents (parser.QueryDictionaryObject (page.GetPtr (), "Contents"));
  std::string result;
  auto read= [&] (PDFObject* object) {
    require (object && object->GetType () == PDFObject::ePDFObjectStream, "expected content stream");
    std::unique_ptr<IByteReader> reader (parser.StartReadingFromStream (static_cast<PDFStreamInput*> (object)));
    require (reader != nullptr, "read content stream");
    IOBasicTypes::Byte buffer[1024];
    while (reader->NotEnded ()) {
      auto count= reader->Read (buffer, sizeof (buffer));
      require (count > 0 || !reader->NotEnded (), "stalled content stream");
      result.append (reinterpret_cast<char*> (buffer), count);
    }
  };
  if (contents->GetType () == PDFObject::ePDFObjectArray) {
    auto* array= static_cast<PDFArray*> (contents.GetPtr ());
    for (unsigned long i= 0; i < array->GetLength (); ++i) {
      RefCountPtr<PDFObject> item (parser.QueryArrayObject (array, i));
      read (item.GetPtr ());
    }
  }
  else read (contents.GetPtr ());
  return result;
}
} // namespace

class PdfMergeTest: public QObject {
  Q_OBJECT
private slots:
  void resolvesIncludedNodesAndPreservesExistingLinks () {
    try {
      QTemporaryDir directory;
      QVERIFY (directory.isValid ());
      const auto first= directory.filePath ("first.pdf").toStdString ();
      const auto second= directory.filePath ("second.pdf").toStdString ();
      const auto output= directory.filePath ("merged.pdf").toStdString ();
      member_pdf (first, 2, {{"tmfs://wikilink/" + first_id},
        {"tmfs://wikilink/" + second_id}, {"tmfs://wikilink/" + missing_id},
        {"https://example.org/reference"}, {"", 1}});
      member_pdf (second, 1, {{"tmfs://transclude/" + first_id}, {"", 0}});
      std::vector<athena::pdf::merge_member> members {
        {first, {{"athena-node-" + first_id, 1, 12, 150}}},
        {second, {{"athena-node-" + second_id, 0, 25, 170}}}};
      hashmap<string,tree> destinations;
      for (const auto& member: members) for (const auto& destination: member.destinations)
        destinations (string (destination.label.c_str ()))= tree ("");
      athena::pdf::merge_documents (members, output, [&] (const std::string& uri) {
        string resolved= athena::node_reference::export_node_link (string (uri.c_str ()), destinations);
        return std::string (as_charp (resolved), N (resolved));
      });
      parsed_pdf merged (output), original_first (first), original_second (second);
      QCOMPARE (merged.parser.GetPagesCount (), 3UL);
      QCOMPARE (contents (merged.parser, 0), contents (original_first.parser, 0));
      QCOMPARE (contents (merged.parser, 1), contents (original_first.parser, 1));
      QCOMPARE (contents (merged.parser, 2), contents (original_second.parser, 0));
      for (unsigned long p: {0UL, 2UL}) {
        RefCountPtr<PDFDictionary> page (merged.parser.ParsePage (p));
        PDFObjectCastPtr<PDFArray> annotations (merged.parser.QueryDictionaryObject (page.GetPtr (), "Annots"));
        QVERIFY (!!annotations);
        QCOMPARE (annotations->GetLength (), p == 0 ? 4UL : 2UL);
        const std::vector<unsigned long> targets= p == 0 ?
          std::vector<unsigned long> {1, 2, 0, 1} : std::vector<unsigned long> {1, 2};
        for (unsigned long a= 0; a < annotations->GetLength (); ++a) {
          PDFObjectCastPtr<PDFDictionary> annotation (merged.parser.QueryArrayObject (annotations.GetPtr (), a));
          QVERIFY (!!annotation);
          if (p == 0 && a == 2) {
            PDFObjectCastPtr<PDFDictionary> action (merged.parser.QueryDictionaryObject (annotation.GetPtr (), "A"));
            QVERIFY (!!action);
            PDFObjectCastPtr<PDFName> kind (merged.parser.QueryDictionaryObject (action.GetPtr (), "S"));
            QVERIFY (!!kind);
            QCOMPARE (kind->GetValue (), std::string ("URI"));
            PDFObjectCastPtr<PDFLiteralString> uri (merged.parser.QueryDictionaryObject (action.GetPtr (), "URI"));
            QVERIFY (!!uri);
            QCOMPARE (uri->GetValue (), std::string ("https://example.org/reference"));
          }
          else {
            QVERIFY (!annotation->Exists ("A"));
            PDFObjectCastPtr<PDFArray> destination (merged.parser.QueryDictionaryObject (annotation.GetPtr (), "Dest"));
            QVERIFY (!!destination);
            PDFObjectCastPtr<PDFIndirectObjectReference> target (destination->QueryObject (0));
            QVERIFY (!!target);
            QCOMPARE (target->mObjectID, merged.parser.GetPageObjectID (targets[a]));
          }
        }
      }
    }
    catch (const std::exception& error) { QFAIL (error.what ()); }
  }
};

QTEST_GUILESS_MAIN (PdfMergeTest)
#include "pdf_merge_test.moc"
