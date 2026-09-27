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
#include "node_metadata.hpp"
#include <cmath>
#include <future>
#include <limits>

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
        "<athena-document version='3' text-model='utf-8'><node tag='document'/></athena-document>",
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
  void legacyVersionMetadata () {
    tree version= compound ("TeXmacs", "2.1.4");
    tree body= compound ("body", tree (DOCUMENT, version, "text"));
    tree original (DOCUMENT, version, compound ("style", "generic"), body,
                   compound ("custom-metadata", "keep"), version);
    std::vector<int> mapping;
    tree upgraded= strip_legacy_document_version (original, &mapping);
    QVERIFY (mapping == std::vector<int> ({-1, 0, 1, 2, -1}));
    QCOMPARE (N (original), 5);
    QCOMPARE (N (upgraded), 3);
    QVERIFY (upgraded[1] == body);
    QVERIFY (read_xml (write_xml (upgraded)) == upgraded);
    QVERIFY_THROWS_EXCEPTION (codec_exception, write_xml (original));
    QVERIFY_THROWS_EXCEPTION (codec_exception, strip_legacy_document_version (tree ("text")));
    // Generic fragments must not erase same-named macros or data nodes.
    QVERIFY (read_xml (write_xml (original, xml_kind::fragment), xml_kind::fragment) == original);
    QVERIFY_THROWS_EXCEPTION (codec_exception, read_xml (
      "<athena-document version='1' text-model='utf-8'><node tag='document'>"
      "<node tag='TeXmacs'><text>2.1.4</text></node></node></athena-document>"));
    athena::node::metadata meta;
    meta.id= "00000000-0000-4000-8000-000000000001";
    athena::node::set (original, meta);
    upgraded= strip_legacy_document_version (original);
    QVERIFY (athena::node::equal_metadata (original, upgraded));
    QVERIFY_THROWS_EXCEPTION (codec_exception, write_xml (upgraded));
    QVERIFY (read_xml_v2 (write_xml_v2 (upgraded)) == upgraded);
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
  void v1RemainsDefault () {
    tree source (DOCUMENT, tree ("plain"));
    auto xml= write_xml (source);
    QVERIFY (xml.find ("version=\"1\" text-model=\"utf-8\"") != std::string::npos);
    QVERIFY (xml.find ("<text>plain</text>") != std::string::npos);
    QCOMPARE (write_xml (read_xml (xml)), xml);
    auto v2= write_xml_v2 (source);
    QVERIFY (v2.find ("version=\"2\" text-model=\"utf-8\"") != std::string::npos);
    QVERIFY (v2.find ("<text><value>plain</value></text>") != std::string::npos);
    QVERIFY (v2.find ("<properties") == std::string::npos);
    QVERIFY (read_xml_v2 (v2) == source);
    QCOMPARE (write_xml (read_xml_v2 (v2)), xml);

    athena::node::metadata meta;
    meta.id= athena::node::new_id ();
    athena::node::set (source, meta);
    QVERIFY_EXCEPTION_THROWN (write_xml (source), codec_exception);
    auto annotated= write_xml_v2 (source);
    QVERIFY (annotated.find ("<properties") == std::string::npos);
    QVERIFY (athena::node::equal_metadata (read_xml_v2 (annotated), source));
    athena::node::clear (source);
    athena::node::set (source[0], meta);
    QVERIFY_EXCEPTION_THROWN (write_xml (source), codec_exception);
    athena::node::clear (source[0]);
    meta.id.clear ();
    meta.properties.emplace ("key", athena::node::property (std::string ("value")));
    athena::node::set (source[0], meta);
    QVERIFY_EXCEPTION_THROWN (write_xml (source), codec_exception);
    tree raw (RAW_DATA, tree (string ("\0\xff", 2)));
    athena::node::set (raw[0], meta);
    QVERIFY_EXCEPTION_THROWN (write_xml (raw, xml_kind::fragment), codec_exception);
    for (const std::string content: std::vector<std::string> {
      "<text><value>v2</value></text>",
      "<node tag='x'><properties/></node>",
      "<text id='" + athena::node::new_id () + "'>text</text>"
    }) QVERIFY_EXCEPTION_THROWN (read_xml (
      "<athena-tree version='1' text-model='utf-8'>" + content + "</athena-tree>",
      xml_kind::fragment), codec_exception);
  }
  void v2ReaderRequiresExplicitOptIn () {
    for (auto kind: {xml_kind::document, xml_kind::fragment}) {
      tree source= kind == xml_kind::document ? tree (DOCUMENT, tree ("text")) : tree ("text");
      const auto v1= write_xml (source, kind);
      QVERIFY (read_xml (v1, kind) == source);
      QVERIFY (read_xml_v2 (v1, kind) == source);
      for (bool annotated: {false, true}) {
        if (annotated) {
          athena::node::metadata meta;
          meta.id= athena::node::new_id ();
          meta.properties.emplace ("note", athena::node::property (std::string ("keep")));
          athena::node::set (source, meta);
        }
        const auto v2= write_xml_v2 (source, kind);
        try {
          (void) read_xml (v2, kind);
          QFAIL ("Default XML reader accepted v2 before activation");
        }
        catch (const codec_exception& error) {
          QVERIFY (error.code == codec_error::unsupported_version);
        }
        tree decoded= read_xml_v2 (v2, kind);
        QVERIFY (decoded == source);
        QVERIFY (athena::node::equal_metadata (decoded, source));
      }
    }
  }
  void v2MetadataRoundtrip () {
    using athena::node::property;
    athena::node::metadata text_meta;
    text_meta.id= athena::node::new_id ();
    text_meta.properties.emplace ("note", property (std::string ("a\0\r\x01", 4)));
    tree text (u8"\u4e2d\U0001f469\u200d\U0001f4bb");
    athena::node::set (text, text_meta);
    tree raw (RAW_DATA, tree (string ("\0\xff\xc0\x80", 4)));
    athena::node::metadata bytes_meta;
    bytes_meta.id= athena::node::new_id ();
    bytes_meta.properties.emplace ("format", property (std::string ("opaque")));
    athena::node::set (raw[0], bytes_meta);
    tree rich (make_tree_label ("strong"), tree ("rich"));
    athena::node::metadata rich_meta;
    rich_meta.id= athena::node::new_id ();
    rich_meta.properties.emplace ("nested", property (true));
    athena::node::set (rich, rich_meta);
    athena::node::metadata meta;
    meta.id= athena::node::new_id ();
    meta.properties= {
      {"string", property (std::string ("42"))},
      {"boolean", property (true)},
      {"false", property (false)},
      {"integer", property (std::int64_t (42))},
      {"minimum", property (std::numeric_limits<std::int64_t>::min ())},
      {"maximum", property (std::numeric_limits<std::int64_t>::max ())},
      {"real", property (42.0)},
      {"tiny", property (std::numeric_limits<double>::denorm_min ())},
      {"large", property (std::numeric_limits<double>::max ())},
      {"negative-zero", property (-0.0)},
      {"reference", property (athena::node::reference {text_meta.id})},
      {"rich", property (athena::node::rich_text {rich})},
      {"list", property (property::list {
        property (std::string ("")), property (std::int64_t (-7)),
        property (property::dictionary {{"key", property (false)}})})},
      {"dictionary", property (property::dictionary {
        {"list", property (property::list {})},
        {"map", property (property::dictionary {})}})}
    };
    meta.properties.emplace (std::string ("key\0\r\t\n", 7), property (std::string ("escaped key")));
    tree source (DOCUMENT, text, raw, tree (""), tree (string ("\0\r", 2)));
    athena::node::set (source, meta);
    auto xml= write_xml_v2 (source);
    tree decoded= read_xml_v2 (xml);
    QVERIFY (decoded == source);
    QVERIFY (athena::node::equal_metadata (decoded, source));
    QVERIFY (athena::node::equal_metadata (decoded[0], source[0]));
    QVERIFY (athena::node::equal_metadata (decoded[1][0], source[1][0]));
    const auto* decoded_meta= athena::node::get (decoded);
    QVERIFY (decoded_meta != nullptr);
    QVERIFY (std::holds_alternative<std::string> (decoded_meta->properties.at ("string").data));
    QVERIFY (std::holds_alternative<std::int64_t> (decoded_meta->properties.at ("integer").data));
    QVERIFY (std::holds_alternative<double> (decoded_meta->properties.at ("real").data));
    QVERIFY (std::signbit (std::get<double> (decoded_meta->properties.at ("negative-zero").data)));
    const auto& decoded_rich= std::get<athena::node::rich_text> (
      decoded_meta->properties.at ("rich").data).content;
    QVERIFY (athena::node::equal_metadata (decoded_rich, rich));
    QCOMPARE (write_xml_v2 (decoded), xml);
    QCOMPARE (write_xml_v2 (copy (source)), xml);
    QVERIFY (xml.find ("<value encoding=\"base64\">AP/AgA==</value>") != std::string::npos);
    QVERIFY (xml.find ("type=\"string\">42</property>") != std::string::npos);
    QVERIFY (xml.find ("type=\"int64\">42</property>") != std::string::npos);
    QVERIFY (xml.find ("type=\"double\">42</property>") != std::string::npos);
    QVERIFY (xml.find ("name-encoding=\"base64-utf8\"") != std::string::npos);
    QCOMPARE (write_xml_v2 (read_xml_v2 (write_xml_v2 (text, xml_kind::fragment),
      xml_kind::fragment), xml_kind::fragment), write_xml_v2 (text, xml_kind::fragment));
  }
  void v2InvalidMetadata () {
    const std::string prefix= "<athena-tree version='2' text-model='utf-8'>";
    const std::string suffix= "</athena-tree>";
    for (const std::string content: {
      "<text/>", "<text>old</text>", "<text encoding='base64-utf8'><value/></text>",
      "<text><value/><value/></text>",
      "<text><value/><properties/></text>",
      "<text><properties/><properties/><value/></text>",
      "<text><value encoding=''/></text>",
      "<text><value encoding='base64-utf8'>/w==</value></text>",
      "<text><value encoding='base64-utf8'>AB==</value></text>",
      "<text><value><node tag='x'/></value></text>",
      "<text id=''><value/></text>",
      "<text id='bad id'><value/></text>",
      "<node tag='x'><text><value/></text><properties/></node>",
      "<node tag='x'><properties ignored='1'/></node>",
      "<bytes><value encoding='base64'>AA==</value></bytes>",
      "<node tag='raw-data'><bytes><value/></bytes></node>",
      "<node tag='raw-data'><text><value/></text></node>",
      "<node tag='raw-data'><bytes><value encoding='base64'>@@</value></bytes></node>",
      "<node tag='x'><properties><property name='k' type='string'/><property name='k' type='string'/></properties></node>",
      "<node tag='x'><properties><property name='k' type='string'/><property name='aw==' name-encoding='base64-utf8' type='string'/></properties></node>",
      "<node tag='x' xmlns='urn:unexpected'/>",
      "<node tag='x' tag-encoding=''/>",
      "<node tag='x' tag='y'/>"
    }) QVERIFY_EXCEPTION_THROWN (read_xml_v2 (prefix + content + suffix, xml_kind::fragment), codec_exception);
    for (const std::string entry: {
      "<property name='k'/>", "<property type='string'/>",
      "<property name='' type='string'/>",
      "<property name='k' type='unknown'/>",
      "<property name='k' type='boolean'>1</property>",
      "<property name='k' type='int64'>9223372036854775808</property>",
      "<property name='k' type='int64'>-9223372036854775809</property>",
      "<property name='k' type='int64'>1.0</property>",
      "<property name='k' type='int64'>01</property>",
      "<property name='k' type='int64'> 1</property>",
      "<property name='k' type='int64'/>",
      "<property name='k' type='double'>NaN</property>",
      "<property name='k' type='double'>inf</property>",
      "<property name='k' type='double'>1e9999</property>",
      "<property name='k' type='double'>1tail</property>",
      "<property name='k' type='double' encoding='base64-utf8'>MQ==</property>",
      "<property name='k' type='string' encoding='base64-utf8'>/w==</property>",
      "<property name='k' type='string' encoding='base64-utf8'>@@</property>",
      "<property name='k' name-encoding='base64' type='string'/>",
      "<property name='/w==' name-encoding='base64-utf8' type='string'/>",
      "<property name='k' type='reference'/>",
      "<property name='k' type='list'><property name='x' type='string'/></property>",
      "<property name='k' type='list'><item type='string' name='x'/></property>",
      "<property name='k' type='dictionary'><item type='string'/></property>",
      "<property name='k' type='dictionary'><property name='x' type='string'/><property name='x' type='string'/></property>",
      "<property name='k' type='rich_tree'/>",
      "<property name='k' type='rich_tree'><text><value/></text><text><value/></text></property>",
      "<property name='k' type='string' xmlns='urn:unexpected'/>",
      "<property name='k' type='string' ignored='1'/>",
      "<property name='k' type='string'>&unknown;</property>"
    }) QVERIFY_EXCEPTION_THROWN (read_xml_v2 (prefix + "<text><properties>" + entry +
      "</properties><value/></text>" + suffix, xml_kind::fragment), codec_exception);
    QVERIFY_EXCEPTION_THROWN (read_xml_v2 (
      "<!DOCTYPE athena-tree [<!ENTITY x 'value'>]>" + prefix +
      "<text><properties><property name='k' type='string'>&x;</property></properties><value/></text>" +
      suffix, xml_kind::fragment), codec_exception);
    QVERIFY_EXCEPTION_THROWN (read_xml_v2 (
      "<!DOCTYPE athena-tree SYSTEM 'file:///etc/passwd'>" + prefix +
      "<text><value/></text>" + suffix, xml_kind::fragment), codec_exception);
    for (const std::string version: {"0", "02", "2.0", "v2", "3"})
      QVERIFY_EXCEPTION_THROWN (read_xml_v2 ("<athena-tree version='" + version +
        "' text-model='utf-8'><text><value/></text></athena-tree>", xml_kind::fragment), codec_exception);
  }
  void v2DuplicateIdentities () {
    using athena::node::property;
    athena::node::metadata meta;
    meta.id= athena::node::new_id ();
    tree child ("child");
    athena::node::set (child, meta);
    tree source (DOCUMENT, child, copy (child));
    QVERIFY_EXCEPTION_THROWN (write_xml_v2 (source), codec_exception);
    source= tree (DOCUMENT, child);
    athena::node::set (source, meta);
    QVERIFY_EXCEPTION_THROWN (write_xml_v2 (source), codec_exception);
    meta.id.clear ();
    meta.properties.emplace ("rich", property (athena::node::rich_text {child}));
    athena::node::set (source, meta);
    QVERIFY_EXCEPTION_THROWN (write_xml_v2 (source), codec_exception);
    const auto id= athena::node::id (child);
    const auto atom= "<text id='" + id + "'><value/></text>";
    for (const std::string content: {
      atom + atom,
      "<properties><property name='rich' type='rich_tree'>" + atom +
        "</property></properties>" + atom
    }) QVERIFY_EXCEPTION_THROWN (read_xml_v2 (
      "<athena-document version='2' text-model='utf-8'><node tag='document'>" +
      content + "</node></athena-document>"), codec_exception);
  }
  void v2MetadataLimits () {
    using athena::node::property;
    tree source ("");
    athena::node::metadata meta;
    meta.properties.emplace ("p", property (property::list {
      property (std::int64_t (7)), property (property::dictionary {
        {"k", property (athena::node::rich_text {tree ("x")})}})}));
    athena::node::set (source, meta);
    codec_limits exact;
    exact.nodes= 6;
    exact.depth= 4;
    exact.text_bytes= 4;
    auto xml= write_xml_v2 (source, xml_kind::fragment, exact);
    QVERIFY (read_xml_v2 (xml, xml_kind::fragment, exact) == source);
    for (int field= 0; field < 3; ++field) {
      auto limit= exact;
      if (field == 0) --limit.nodes;
      if (field == 1) --limit.depth;
      if (field == 2) --limit.text_bytes;
      QVERIFY_EXCEPTION_THROWN (write_xml_v2 (source, xml_kind::fragment, limit), codec_exception);
      QVERIFY_EXCEPTION_THROWN (read_xml_v2 (xml, xml_kind::fragment, limit), codec_exception);
    }
    auto limit= exact;
    limit.output_bytes= xml.size () - 1;
    QVERIFY_EXCEPTION_THROWN (write_xml_v2 (source, xml_kind::fragment, limit), codec_exception);
    limit= exact;
    limit.input_bytes= xml.size () - 1;
    QVERIFY_EXCEPTION_THROWN (read_xml_v2 (xml, xml_kind::fragment, limit), codec_exception);

    // Identity and reference bytes use the same aggregate text budget.
    meta.properties.clear ();
    meta.id= athena::node::new_id ();
    meta.properties.emplace ("r", property (athena::node::reference {meta.id}));
    athena::node::set (source, meta);
    limit= {};
    limit.text_bytes= 2 * meta.id.size () + 1;
    xml= write_xml_v2 (source, xml_kind::fragment, limit);
    QVERIFY (read_xml_v2 (xml, xml_kind::fragment, limit) == source);
    --limit.text_bytes;
    QVERIFY_EXCEPTION_THROWN (write_xml_v2 (source, xml_kind::fragment, limit), codec_exception);
    QVERIFY_EXCEPTION_THROWN (read_xml_v2 (xml, xml_kind::fragment, limit), codec_exception);
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
