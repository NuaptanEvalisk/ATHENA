/******************************************************************************
* MODULE     : finite_part_integral_test.cpp
* DESCRIPTION: regression test for finite-part integrals in math fonts
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

class TestFinitePartIntegral: public QObject {
  Q_OBJECT

private slots:
  void fallsBackToStixForPagellaDisplayMath();
};

void TestFinitePartIntegral::fallsBackToStixForPagellaDisplayMath() {
  QTemporaryDir temp;
  QVERIFY (temp.isValid ());
  QVERIFY (QDir ().mkpath (temp.filePath ("home/fonts")));
  QVERIFY (QDir ().mkpath (temp.filePath ("home/system")));

  QFile input (temp.filePath ("finite-part-integral.tm"));
  QVERIFY (input.open (QIODevice::WriteOnly | QIODevice::Text));
  input.write (
    "<TeXmacs|2.1.4>\n"
    "\n"
    "<style|generic>\n"
    "\n"
    "<\\body>\n"
    "  Inline: <math|<big|fint>f(x)\\<mathd\\>x>\n"
    "\n"
    "  <\\equation*>\n"
    "    <big|fint><rsub|0><rsup|1>f(x)\\<mathd\\>x\n"
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
  QString outputFile= temp.filePath ("finite-part-integral.pdf");
  process.setArguments ({"-log-file", logFile, "-C", input.fileName (),
                         outputFile});
  process.start ();
  QVERIFY2 (process.waitForFinished (20000), qPrintable (process.errorString ()));

  QByteArray processOutput= process.readAllStandardOutput () +
                            process.readAllStandardError ();
  QCOMPARE (process.exitStatus (), QProcess::NormalExit);
  QCOMPARE (process.exitCode (), 0);
  QVERIFY2 (QFile::exists (outputFile), processOutput.constData ());

  QFile log (logFile);
  QVERIFY2 (log.open (QIODevice::ReadOnly), processOutput.constData ());
  QByteArray output= log.readAll ();
  QByteArray resolution;
  for (QByteArray line: output.split ('\n'))
    if (line.contains ("FONT-RESOLUTION source=<big-fint"))
      resolution += line + '\n';

  QVERIFY2 (resolution.contains ("source=<big-fint-1>"),
            resolution.constData ());
  QVERIFY2 (resolution.contains ("source=<big-fint-2>") &&
            resolution.contains ("subfont=rubberstix["),
            resolution.constData ());
  QVERIFY2 (!resolution.contains ("spec=tuple (error)"),
            resolution.constData ());
}

QTEST_MAIN (TestFinitePartIntegral)
#include "finite_part_integral_test.moc"
