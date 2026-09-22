/******************************************************************************
* MODULE     : athena_document_xml_test.cpp
* DESCRIPTION: Lossless XML tree round trips and hostile-input rejection
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include <QtTest/QtTest>
#include "Xml/athena_document_xml.hpp"
#include <future>

using namespace athena::document;
bool headless_mode= true;
bool is_headless () { return true; }

class TestDocumentXml: public QObject {
  Q_OBJECT
private slots:
  void initTestCase () {
    make_tree_label (DOCUMENT, "document");
    make_tree_label (RAW_DATA, "raw-data");
  }
  void roundtrip () {
    tree source (DOCUMENT);
    source << tree ("") << tree (" <alpha>&\"\n\t ")
           << tree (u8"\u4e2d\U0001f469\u200d\U0001f4bb")
           << tree (string ("a\0\r\x01" "b", 5))
           << tree (RAW_DATA, tree (string ("\0\xff\xc0\x80", 4)))
           << tree (make_tree_label ("unknown-tag"), 0)
           << tree (make_tree_label (string ("tag\0\r\t\n", 7)), tree ("arg"));
    std::string xml= write_xml (source);
    QVERIFY (read_xml (xml) == source);
    QCOMPARE (write_xml (read_xml (xml)), xml);
    QVERIFY (xml.find ("&lt;alpha&gt;") != std::string::npos);
    QVERIFY (xml.find ("base64-utf8") != std::string::npos);
    QVERIFY (read_xml (write_xml (tree (""), xml_kind::fragment), xml_kind::fragment) == "");
  }
  void invalidInput () {
    for (const std::string input: {
        "", "<athena-document>",
        "<!DOCTYPE athena-document [<!ENTITY x 'a'>]><athena-document/>",
        "<!DOCTYPE athena-document SYSTEM 'file:///etc/passwd'><athena-document/>",
        "<athena-document version='2' text-model='utf-8'><node tag='document'/></athena-document>",
        "<athena-document version='1' text-model='utf-8'><node tag='document'><text><node tag='x'/></text></node></athena-document>",
        "<athena-document version='1' text-model='utf-8'><node tag='document'><bytes encoding='base64'>AA==</bytes></node></athena-document>",
        "<athena-document version='1' text-model='utf-8'><node tag='raw-data'><text>x</text></node></athena-document>",
        "<athena-document version='1' text-model='utf-8'><node tag='document'><text encoding='base64-utf8'>/w==</text></node></athena-document>",
        "<athena-document version='1' text-model='utf-8'><node tag='document'><text encoding='base64-utf8'>@@</text></node></athena-document>",
        "<athena-document version='1' text-model='utf-8' ignored='yes'><node tag='document'/></athena-document>",
        "<athena-document version='1' text-model='utf-8'><node tag='document'/><node tag='document'/></athena-document>",
        "<athena-document version='1' text-model='utf-8'><node tag='document'/></athena-document>extra",
        "<?xml version='1.0' encoding='ISO-8859-1'?><athena-document/>",
        "\xff"}) {
      QVERIFY_EXCEPTION_THROWN (read_xml (input), codec_exception);
    }
    QVERIFY_EXCEPTION_THROWN (write_xml (tree (DOCUMENT, tree ("\xff"))), codec_exception);
    QVERIFY_EXCEPTION_THROWN (write_xml (tree (RAW_DATA), xml_kind::fragment), codec_exception);
  }
  void limitsAndLocations () {
    tree source (DOCUMENT, tree ("abc"));
    auto xml= write_xml (source);
    codec_limits limit;
    limit.nodes= 1;
    QVERIFY_EXCEPTION_THROWN (write_xml (source, xml_kind::document, limit), codec_exception);
    QVERIFY_EXCEPTION_THROWN (read_xml (xml, xml_kind::document, limit), codec_exception);
    limit= {};
    limit.depth= 0;
    QVERIFY_EXCEPTION_THROWN (read_xml (xml, xml_kind::document, limit), codec_exception);
    limit= {};
    limit.output_bytes= 12;
    QVERIFY_EXCEPTION_THROWN (write_xml (source, xml_kind::document, limit), codec_exception);
    limit= {};
    limit.input_bytes= xml.size () - 1;
    QVERIFY_EXCEPTION_THROWN (read_xml (xml, xml_kind::document, limit), codec_exception);
    limit= {};
    limit.text_bytes= 9;
    QVERIFY_EXCEPTION_THROWN (read_xml (xml, xml_kind::document, limit), codec_exception);
    try {
      (void) read_xml ("<athena-document version='1' text-model='utf-8'>\n<bad/>");
      QFAIL ("Malformed document accepted");
    }
    catch (const codec_exception& e) {
      QVERIFY (e.line >= 2);
      QVERIFY (e.column > 0);
    }
  }
  void independentWorkers () {
    std::vector<std::future<bool>> jobs;
    for (int i= 0; i < 4; ++i) jobs.push_back (std::async (std::launch::async, [] {
      tree source (DOCUMENT, tree (u8"\u03b1"));
      for (int j= 0; j < 32; ++j)
        if (read_xml (write_xml (source)) != source) return false;
      return true;
    }));
    for (auto& job: jobs) QVERIFY (job.get ());
  }
};
QTEST_GUILESS_MAIN (TestDocumentXml)
#include "athena_document_xml_test.moc"
