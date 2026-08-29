
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


class TestQtUtilities: public QObject {
  Q_OBJECT

private slots:
  void test_qt_supports();
  void test_png_to_pdf();
};

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
