/******************************************************************************
* MODULE     : pagella_blackboard_test.cpp
* DESCRIPTION: regression test for Pagella blackboard glyph resolution
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

class TestPagellaBlackboard: public QObject {
  Q_OBJECT

private slots:
  void resolvesBoldBlackboardCapitalsFromMathFace();
};

void TestPagellaBlackboard::resolvesBoldBlackboardCapitalsFromMathFace() {
  QTemporaryDir temp;
  QVERIFY (temp.isValid ());
  QVERIFY (QDir ().mkpath (temp.filePath ("home/system")));

  QFile input (temp.filePath ("pagella-blackboard.tm"));
  QVERIFY (input.open (QIODevice::WriteOnly | QIODevice::Text));
  input.write (
    "<TeXmacs|2.1.4>\n"
    "\n"
    "<style|generic>\n"
    "\n"
    "<\\body>\n"
    "  <\\equation*>\n"
    "    <with|font-series|bold|math-font-series|bold|"
    "\\<bbb-N\\>\\quad\\<bbb-Q\\>\\quad\\<bbb-R\\>>\n"
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
    "-C", input.fileName (), temp.filePath ("pagella-blackboard.pdf")
  });
  process.start ();
  QVERIFY2 (process.waitForFinished (20000), qPrintable (process.errorString ()));

  QByteArray processOutput= process.readAllStandardOutput () +
                            process.readAllStandardError ();
  QCOMPARE (process.exitStatus (), QProcess::NormalExit);
  QCOMPARE (process.exitCode (), 0);
  QVERIFY2 (QFile::exists (temp.filePath ("pagella-blackboard.pdf")),
            processOutput.constData ());

  QFile log (logFile);
  QVERIFY2 (log.open (QIODevice::ReadOnly), processOutput.constData ());
  QByteArray output= log.readAll ();
  QByteArray resolution;
  for (QByteArray line: output.split ('\n'))
    if (line.contains ("FONT-RESOLUTION") && line.contains ("<bbb-"))
      resolution += line + '\n';

  for (QByteArray symbol: {QByteArray ("<bbb-N>"), QByteArray ("<bbb-Q>"),
                           QByteArray ("<bbb-R>")}) {
    QVERIFY2 (resolution.contains ("source=" + symbol +
                                   " family=TeX Gyre Pagella series=bold "
                                   "spec=tuple (poor-bold)"),
              resolution.constData ());
  }
  QVERIFY2 (!resolution.contains ("spec=tuple (error)"),
            resolution.constData ());
}

QTEST_MAIN (TestPagellaBlackboard)
#include "pagella_blackboard_test.moc"
