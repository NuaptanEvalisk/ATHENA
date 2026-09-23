
/******************************************************************************
* MODULE     : qt_utilities_test.cpp
* COPYRIGHT  : (C) 2019  Darcy Shen
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include "Qt/qt_utilities.hpp"
#include "Xml/clipboard_xml.hpp"
#include "convert.hpp"
#include "drd_std.hpp"
#include <stdexcept>


class TestQtUtilities: public QObject {
  Q_OBJECT

private slots:
  void initTestCase () { init_std_drd (); }
  void text_roundtrip ();
  void invalid_text ();
  void text_lists ();
  void clipboard_fragment ();
  void plain_text ();
  void test_qt_supports();
  void test_png_to_pdf();
};

void TestQtUtilities::text_roundtrip () {
  string source (u8"\u00e9 \u4e2d\U0001f600 e\u0301 <alpha>");
  source << string ("\0tail", 5);
  QString qt= to_qstring (source);
  QVERIFY (from_qstring (qt) == source);
  QVERIFY (from_qstring_utf8 (qt) == source);
  QCOMPARE (qt, QString::fromUtf8 (source.data (), N(source)));
  QCOMPARE (to_qstring ("<alpha>"), QStringLiteral ("<alpha>"));
}

void TestQtUtilities::invalid_text () {
  QVERIFY_EXCEPTION_THROWN (to_qstring ("\xe9"), std::invalid_argument);
  QVERIFY_EXCEPTION_THROWN (utf8_to_qstring ("\xc0\x80"), std::invalid_argument);
  QVERIFY_EXCEPTION_THROWN (from_qstring (QString (QChar (0xd800))),
                            std::invalid_argument);
  QVERIFY_EXCEPTION_THROWN (from_qstring_utf8 (QString (QChar (0xdc00))),
                            std::invalid_argument);
}

void TestQtUtilities::text_lists () {
  QStringList source {QString (), QStringLiteral ("<alpha>"),
                      QString::fromUtf8 (u8"\u4e2d\U0001f600")};
  array<string> native= from_qstringlist (source);
  QCOMPARE (N(native), source.size ());
  QCOMPARE (to_qstringlist (native), source);
}

void TestQtUtilities::clipboard_fragment () {
  using namespace athena::document;
  tree content (DOCUMENT, tree (u8"\u00e9\u4e2d e\u0301 <alpha>"),
                tree (RAW_DATA, tree (string ("\0\xff\xc0\x80", 4))));
  tree source= tuple ("texmacs", content, "math", "english");
  std::string bytes= write_clipboard_xml (source);
  tree restored= read_clipboard_xml (bytes);
  QVERIFY (restored == source);
  QVERIFY_EXCEPTION_THROWN (read_clipboard_xml (
    write_xml (content, xml_kind::fragment)), codec_exception);
  QVERIFY_EXCEPTION_THROWN (write_clipboard_xml (
    tuple ("texmacs", "x", tree (DOCUMENT, "invalid mode"), "en")), codec_exception);
  QVERIFY_EXCEPTION_THROWN (read_clipboard_xml ("<!DOCTYPE x><x/>"), codec_exception);
}

void TestQtUtilities::plain_text () {
  string source (u8"\u00e9\u4e2d\U0001f600 e\u0301 <alpha>");
  source << string ("\0tail", 5);
  QVERIFY (verbatim_to_tree (source, false, "auto") == tree (source));
  QVERIFY (tree_to_verbatim (tree (source), false, "utf-8") == source);
  QVERIFY (verbatim_to_tree (source, false, "SourceCode") == tree (source));
  QVERIFY (verbatim_to_tree ("\xe9", false, "ISO-8859-1") == tree (u8"\u00e9"));
  QVERIFY (verbatim_to_tree (string ("\x2d\x4e\x0a\0", 4), true, "UTF-16LE") ==
           tree (u8"\u4e2d "));
  QVERIFY (tree_to_verbatim (tree (u8"\u00e9"), false, "ISO-8859-1") == "\xe9");
  QVERIFY_EXCEPTION_THROWN (verbatim_to_tree ("\xe9", false, "auto"), std::invalid_argument);
  QVERIFY_EXCEPTION_THROWN (tree_to_verbatim (tree (u8"\u4e2d"), false, "ISO-8859-1"),
                            std::invalid_argument);
  QVERIFY (verbatim_to_tree (u8"e\u0301\b!", false, "utf-8") == "!");
  QVERIFY (verbatim_to_tree (u8"\U0001f469\u200d\U0001f4bb\b!", false, "utf-8") == "!");
  QVERIFY (verbatim_to_tree (u8"\u4e2d\t!", false, "utf-8") == tree (u8"\u4e2d       !"));
}

void TestQtUtilities::test_qt_supports () {
#ifdef QTTEXMACS
  QVERIFY (qt_supports (url ("x.svg")));
  QVERIFY (qt_supports (url ("x.png")));
#endif
}

void TestQtUtilities::test_png_to_pdf () {
#ifdef QTTEXMACS
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());

  QString png_path= temporary.filePath ("source.png");
  QString pdf_path= temporary.filePath ("result.pdf");
  QImage source (QSize (32, 16), QImage::Format_ARGB32);
  source.fill (QColor (17, 91, 203, 180));
  QVERIFY (source.save (png_path, "PNG"));

  qt_image_to_pdf (url_system (from_qstring (png_path)),
                   url_system (from_qstring (pdf_path)), 144, 72, 96);

  QFile pdf (pdf_path);
  QVERIFY (pdf.open (QIODevice::ReadOnly));
  QCOMPARE (pdf.read (5), QByteArray ("%PDF-"));
#endif
}

QTEST_MAIN(TestQtUtilities)
#include "qt_utilities_test.moc"
