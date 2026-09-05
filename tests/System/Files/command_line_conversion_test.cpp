/******************************************************************************
* MODULE     : command_line_conversion_test.cpp
* DESCRIPTION: command-line conversion completion and buffer identity
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
#include <QStandardPaths>
#include <QTemporaryDir>

class TestCommandLineConversion: public QObject {
  Q_OBJECT
private slots:
  void convertsBeforeContinuing();
};

static QByteArray contents (const QString& path) {
  QFile file (path);
  if (!file.open (QIODevice::ReadOnly)) return {};
  return file.readAll ();
}

void TestCommandLineConversion::convertsBeforeContinuing() {
  const QString pdftotext= QStandardPaths::findExecutable ("pdftotext");
  if (pdftotext.isEmpty ()) QSKIP ("pdftotext is required");
  QTemporaryDir temp;
  QVERIFY (temp.isValid ());
  QVERIFY (QDir ().mkpath (temp.filePath ("home/fonts")));
  QVERIFY (QDir ().mkpath (temp.filePath ("home/system")));
  QVERIFY (QDir ().mkpath (temp.filePath ("inputs")));
  for (const QString& name: {QString ("FIRSTINPUT"), QString ("SECONDINPUT")}) {
    QFile file (temp.filePath ("inputs/" + name + ".tm"));
    QVERIFY (file.open (QIODevice::WriteOnly));
    const QByteArray source= "<TeXmacs|2.1.4>\n\n<style|generic>\n\n"
      "<\\body>\n  " + name.toUtf8 () + "\n</body>\n";
    QCOMPARE (file.write (source), source.size ());
  }

  const QString executable= QDir (QCoreApplication::applicationDirPath ())
    .absoluteFilePath ("../src/ATHENA.bin");
  QProcess process;
  auto env= QProcessEnvironment::systemEnvironment ();
  env.insert ("ATHENA_HOME_PATH", temp.filePath ("home"));
  env.insert ("QT_QPA_PLATFORM", "offscreen");
  env.insert ("PWD", temp.path ());
  process.setProcessEnvironment (env);
  process.setWorkingDirectory (temp.path ());
  process.setProcessChannelMode (QProcess::MergedChannels);
  process.start (executable, {"-C", "inputs/FIRSTINPUT.tm", "first.pdf",
                            "-C", "inputs/SECONDINPUT.tm", "second.txt",
                            "-x", "(export-buffer \"after.txt\")"});
  QVERIFY2 (process.waitForFinished (45000), qPrintable (process.errorString ()));
  const QByteArray log= process.readAll ();
  QCOMPARE (process.exitStatus (), QProcess::NormalExit);
  QVERIFY2 (process.exitCode () == 0, log.constData ());
  QVERIFY2 (contents (temp.filePath ("second.txt")).contains ("SECONDINPUT"),
            (log + "\nsecond.txt: " + contents (temp.filePath ("second.txt"))).constData ());
  QVERIFY2 (contents (temp.filePath ("inputs/after.txt")).contains ("SECONDINPUT"),
            log.constData ());

  QProcess inspect;
  inspect.start (pdftotext, {temp.filePath ("first.pdf"), "-"});
  QVERIFY (inspect.waitForFinished (5000));
  QCOMPARE (inspect.exitCode (), 0);
  const QByteArray text= inspect.readAllStandardOutput ();
  QVERIFY2 (text.contains ("FIRSTINPUT"), text.constData ());
  QVERIFY (!text.contains ("SECONDINPUT"));

  process.start (executable, {"-C", "missing.tm", "missing.pdf"});
  QVERIFY (process.waitForFinished (20000));
  QCOMPARE (process.exitStatus (), QProcess::NormalExit);
  QCOMPARE (process.exitCode (), 1);
  QVERIFY (!QFile::exists (temp.filePath ("missing.pdf")));
}

QTEST_MAIN (TestCommandLineConversion)
#include "command_line_conversion_test.moc"
