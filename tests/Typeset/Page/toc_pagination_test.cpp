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

class TestTocPagination: public QObject {
  Q_OBJECT

private slots:
  void paginatesLongBookToc();
};

void TestTocPagination::paginatesLongBookToc() {
  QString pdfinfo= QStandardPaths::findExecutable ("pdfinfo");
  if (pdfinfo.isEmpty ())
    QSKIP ("pdfinfo is required for the PDF pagination regression test");

  QTemporaryDir temp;
  QVERIFY (temp.isValid ());
  QVERIFY (QDir ().mkpath (temp.filePath ("home/fonts")));
  QVERIFY (QDir ().mkpath (temp.filePath ("home/system")));

  QByteArray source=
    "<TeXmacs|2.1.4>\n\n"
    "<style|book>\n\n"
    "<\\body>\n"
    "  <\\table-of-contents|toc>\n";
  for (int i= 1; i <= 180; ++i) {
    QByteArray number= QByteArray::number (i);
    source += "    <toc-1|TOCENTRY" + number.rightJustified (3, '0') +
              "|" + number + ">\n\n";
  }
  source +=
    "  </table-of-contents>\n"
    "</body>\n\n"
    "<\\initial>\n"
    "  <\\collection>\n"
    "    <associate|page-medium|paper>\n"
    "  </collection>\n"
    "</initial>\n";

  QFile input (temp.filePath ("long-toc.tm"));
  QVERIFY (input.open (QIODevice::WriteOnly | QIODevice::Text));
  QCOMPARE (input.write (source), source.size ());
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
  QCOMPARE (process.exitCode (), 0);
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
