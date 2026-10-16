/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include <QtTest/QtTest>
#include <QCryptographicHash>
#include "Xml/document_file_codec.hpp"
#include "drd_std.hpp"
#include "node_metadata.hpp"

using namespace athena::document;

namespace {
std::string digest (const std::string& bytes) {
  return "sha256:" + QCryptographicHash::hash (
    QByteArrayView (bytes.data (), bytes.size ()), QCryptographicHash::Sha256).toHex ().toStdString ();
}
}

class TestNativeXmlDispatch: public QObject {
  Q_OBJECT
private slots:
  void initTestCase () { init_std_drd (); }

  void preserveExplicitVersionWithoutMetadata () {
    for (const auto version: {xml_format_version::v1, xml_format_version::v2,
                              xml_format_version::v3})
      for (const auto kind: {xml_kind::document, xml_kind::fragment}) {
        const tree source= kind == xml_kind::document ?
          tree (DOCUMENT, tree ("plain")) : tree ("plain");
        const auto bytes= encode_native_xml_bytes (source, version, kind);
        const auto decoded= decode_native_xml_bytes (bytes, kind);
        QVERIFY (decoded.version == version);
        QVERIFY (decoded.document == source);
        QVERIFY (!athena::node::contains_metadata (decoded.document));
        QCOMPARE (encode_native_xml_bytes (decoded.document, decoded.version, kind), bytes);
        if (kind == xml_kind::document) {
          const auto general= decode_document_bytes (bytes);
          QVERIFY (!general.legacy ());
          QVERIFY (general.document == source);
          QVERIFY ((general.format == document_source_format::xml_v3) ==
                   (version == xml_format_version::v3));
        }
      }
  }

  void dispatchIsStrictAndDoesNotImportOrFallback () {
    for (const std::string bytes: {
      "<TeXmacs|2.1><body|legacy>", "(document \"legacy\")",
      "<a v='99' k='d'><n t='document'/></a>",
      "<a v='3' k='d'><node tag='document'/></a>",
      "<a v='3' k='d'><n t='document'><n t='around'>"
      "<t><v>(</v></t><t><v>x</v></t><t><v>)</v></t></n></n></a>"
    }) QVERIFY_EXCEPTION_THROWN (decode_native_xml_bytes (bytes), codec_exception);
    for (const auto version: {xml_format_version::v1, xml_format_version::v2,
                              xml_format_version::v3}) {
      const auto fragment= encode_native_xml_bytes (tree ("x"), version, xml_kind::fragment);
      QVERIFY_EXCEPTION_THROWN (decode_native_xml_bytes (fragment), codec_exception);
      const auto document= encode_native_xml_bytes (tree (DOCUMENT, tree ("x")), version);
      QVERIFY_EXCEPTION_THROWN (decode_native_xml_bytes (document, xml_kind::fragment), codec_exception);
      codec_limits limits;
      limits.nodes= 1;
      QVERIFY_EXCEPTION_THROWN (decode_native_xml_bytes (document, xml_kind::document, limits),
                                codec_exception);
      QVERIFY_EXCEPTION_THROWN (encode_native_xml_bytes (
        tree (DOCUMENT, tree ("x")), version, xml_kind::document, limits), codec_exception);
    }
    QVERIFY_EXCEPTION_THROWN (encode_native_xml_bytes (
      tree (DOCUMENT), static_cast<xml_format_version> (99)), codec_exception);
  }

  void fingerprintsKeepLegacyDigestsAndRequireExplicitV3 () {
    tree plain (DOCUMENT, tree ("plain"));
    QCOMPARE (semantic_document_fingerprint (plain, xml_format_version::v2),
              digest (write_xml (plain)));
    QCOMPARE (semantic_document_fingerprint (plain, xml_format_version::v2),
              semantic_document_fingerprint (plain));
    QCOMPARE (semantic_document_fingerprint (plain, xml_format_version::v3),
              digest (write_xml_v3 (plain)));
    QVERIFY (semantic_document_fingerprint (plain, xml_format_version::v3) !=
             semantic_document_fingerprint (plain, xml_format_version::v2));
    athena::node::set (plain[0], {athena::node::new_id (), {}});
    QCOMPARE (semantic_document_fingerprint (plain, xml_format_version::v2),
              digest (write_xml_v2 (plain)));
    QCOMPARE (semantic_document_fingerprint (plain, xml_format_version::v3),
              digest (write_xml_v3 (plain)));
    QVERIFY_EXCEPTION_THROWN (semantic_document_fingerprint (
      plain, static_cast<xml_format_version> (99)), codec_exception);
  }
};

QTEST_GUILESS_MAIN (TestNativeXmlDispatch)
#include "native_xml_dispatch_test.moc"
