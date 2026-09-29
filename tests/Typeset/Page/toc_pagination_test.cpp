/******************************************************************************
* MODULE     : toc_pagination_test.cpp
* DESCRIPTION: regression test for printed table-of-contents pagination
* COPYRIGHT  : (C) 2026  Nuaptan
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "ATHENA/Data/document_node_model.hpp"
#include "drd_std.hpp"
#include "node_metadata.hpp"
#include "Xml/athena_document_xml.hpp"
#include "drd_std.hpp"

class TestTocPagination: public QObject {
  Q_OBJECT

private slots:
  void paginatesLongBookToc();
};

void TestTocPagination::paginatesLongBookToc() {
  init_std_drd ();
  QString pdfinfo= QStandardPaths::findExecutable ("pdfinfo");
  if (pdfinfo.isEmpty ())
    QSKIP ("pdfinfo is required for the PDF pagination regression test");

  QTemporaryDir temp;
  QVERIFY (temp.isValid ());
  QVERIFY (QDir ().mkpath (temp.filePath ("home/fonts")));
  QVERIFY (QDir ().mkpath (temp.filePath ("home/system")));

  tree entries (DOCUMENT);
  for (int i= 1; i <= 180; ++i) {
    const QByteArray number= QByteArray::number (i);
    const QByteArray title= "TOCENTRY" + number.rightJustified (3, '0');
    entries << compound ("toc-1",
      string (title.constData (), title.size ()),
      string (number.constData (), number.size ()));
  }
  tree body (DOCUMENT, compound ("table-of-contents", "toc", entries));
  init_std_drd ();
  auto identified= athena::document_node::assign_detached_source_ids (
    body, standard_drd_for_thread (), athena::document_node::standard_source_role,
    [] (const athena::document_node::identity_request&) {
      return athena::node::new_id ();
    });
  QByteArray identityDiagnostic= "Could not assign current document identities";
  if (!identified.diagnostics.empty ())
    identityDiagnostic += ": " + QByteArray::fromStdString (
      identified.diagnostics.front ().detail);
  QVERIFY2 (identified.ok (), identityDiagnostic.constData ());
  tree document (DOCUMENT,
    compound ("style", tuple ("book")),
    compound ("body", *identified.body),
    compound ("initial", tree (COLLECTION,
      compound ("associate", "page-medium", "paper"),
      compound ("associate", "font", "TeX Gyre Pagella"),
      compound ("associate", "math-font", "math-pagella"))));
  const std::string source= athena::document::write_xml_v2 (document);

  QFile input (temp.filePath ("long-toc.ath"));
  QVERIFY (input.open (QIODevice::WriteOnly | QIODevice::Text));
  QCOMPARE (input.write (source.data (), static_cast<qint64> (source.size ())),
            static_cast<qint64> (source.size ()));
  input.close ();

  QString executable=
    QDir (QCoreApplication::applicationDirPath ())
      .absoluteFilePath ("../src/ATHENA.bin");
  QVERIFY2 (QFile::exists (executable), qPrintable (executable));

  QProcess process;
  QProcessEnvironment env= QProcessEnvironment::systemEnvironment ();
  env.insert ("ATHENA_HOME_PATH", temp.filePath ("home"));
  env.insert ("QT_QPA_PLATFORM", "offscreen");
  process.setProcessEnvironment (env);
  process.setProgram (executable);
  QString outputFile= temp.filePath ("long-toc.pdf");
  process.setArguments ({"-C", input.fileName (), outputFile});
  process.start ();
  QVERIFY2 (process.waitForFinished (20000), qPrintable (process.errorString ()));

  QByteArray processOutput= process.readAllStandardOutput () +
                            process.readAllStandardError ();
  QCOMPARE (process.exitStatus (), QProcess::NormalExit);
  QVERIFY2 (process.exitCode () == 0, processOutput.constData ());
  QVERIFY2 (QFile::exists (outputFile), processOutput.constData ());

  QProcess inspect;
  inspect.start (pdfinfo, {outputFile});
  QVERIFY2 (inspect.waitForFinished (5000), qPrintable (inspect.errorString ()));
  QByteArray metadata= inspect.readAllStandardOutput () +
                       inspect.readAllStandardError ();
  QCOMPARE (inspect.exitCode (), 0);
  QRegularExpression matchPages (QStringLiteral ("(?m)^Pages:\\s+(\\d+)$"));
  QRegularExpressionMatch match=
    matchPages.match (QString::fromUtf8 (metadata));
  QVERIFY2 (match.hasMatch (), metadata.constData ());
  QVERIFY2 (match.captured (1).toInt () >= 3, metadata.constData ());
}

QTEST_MAIN (TestTocPagination)
#include "toc_pagination_test.moc"
