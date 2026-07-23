/******************************************************************************
* MODULE     : bold_big_operator_test.cpp
* DESCRIPTION: regression test for bold display operators
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
#include <QTemporaryDir>

class TestBoldBigOperator: public QObject {
  Q_OBJECT

private slots:
  void preservesLargeOperatorInSyntheticBold();
};

void TestBoldBigOperator::preservesLargeOperatorInSyntheticBold() {
  QTemporaryDir temp;
  QVERIFY (temp.isValid ());
  QVERIFY (QDir ().mkpath (temp.filePath ("home/fonts")));
  QVERIFY (QDir ().mkpath (temp.filePath ("home/system")));

  QFile input (temp.filePath ("bold-big-operator.tm"));
  QVERIFY (input.open (QIODevice::WriteOnly | QIODevice::Text));
  input.write (
    "<TeXmacs|2.1.4>\n"
    "\n"
    "<style|generic>\n"
    "\n"
    "<\\body>\n"
    "  <\\equation*>\n"
    "    <big|sum><rsub|k=1><rsup|n>k\n"
    "  </equation*>\n"
    "  <\\equation*>\n"
    "    <with|font-series|bold|math-font-series|bold|"
    "<big|sum><rsub|k=1><rsup|n>k>\n"
    "  </equation*>\n"
    "</body>\n"
    "\n"
    "<\\initial>\n"
    "  <\\collection>\n"
    "    <associate|font|pagella>\n"
    "    <associate|math-font|math-pagella>\n"
    "  </collection>\n"
    "</initial>\n");
  input.close ();

  QString executable=
    QDir (QCoreApplication::applicationDirPath ())
      .absoluteFilePath ("../src/ATHENA.bin");
  QVERIFY2 (QFile::exists (executable), qPrintable (executable));

  QProcess process;
  QProcessEnvironment env= QProcessEnvironment::systemEnvironment ();
  env.insert ("ATHENA_HOME_PATH", temp.filePath ("home"));
  env.insert ("ATHENA_FONT_RESOLUTION_DEBUG", "1");
  env.insert ("QT_QPA_PLATFORM", "offscreen");
  process.setProcessEnvironment (env);
  process.setProgram (executable);
  QString logFile= temp.filePath ("font-resolution.log");
  process.setArguments ({
    "-log-file", logFile,
    "-C", input.fileName (), temp.filePath ("bold-big-operator.pdf")
  });
  process.start ();
  QVERIFY2 (process.waitForFinished (20000), qPrintable (process.errorString ()));

  QByteArray processOutput= process.readAllStandardOutput () +
                            process.readAllStandardError ();
  QCOMPARE (process.exitStatus (), QProcess::NormalExit);
  QCOMPARE (process.exitCode (), 0);
  QVERIFY2 (QFile::exists (temp.filePath ("bold-big-operator.pdf")),
            processOutput.constData ());

  QFile log (logFile);
  QVERIFY2 (log.open (QIODevice::ReadOnly), processOutput.constData ());
  QByteArray output= log.readAll ();
  QByteArray resolution;
  for (QByteArray line: output.split ('\n'))
    if (line.contains ("FONT-RESOLUTION"))
      resolution += line + '\n';
  QVERIFY2 (
    output.contains ("series=medium spec=tuple (main)"),
    resolution.constData ());
  QVERIFY2 (
    output.contains ("series=bold spec=tuple (synthetic-bold-rubber)"),
    resolution.constData ());
}

QTEST_MAIN (TestBoldBigOperator)
#include "bold_big_operator_test.moc"
