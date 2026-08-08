/******************************************************************************
* MODULE     : latex_import_test.cpp
* DESCRIPTION: regression tests for LaTeX declaration and theorem import
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

class TestLatexImport: public QObject {
  Q_OBJECT

private slots:
  void importsDeclarationsAndTheoremAliases();
};

void TestLatexImport::importsDeclarationsAndTheoremAliases() {
  QTemporaryDir temp;
  QVERIFY (temp.isValid ());
  QVERIFY (QDir ().mkpath (temp.filePath ("home/fonts")));
  QVERIFY (QDir ().mkpath (temp.filePath ("home/system")));

  QFile input (temp.filePath ("declarations.tex"));
  QVERIFY (input.open (QIODevice::WriteOnly | QIODevice::Text));
  QByteArray preserved=
    "<hide-preamble><assign|theorem-name|<macro|Theorem>>"
    "</hide-preamble>";
  QByteArray encoded= preserved.toBase64 ();
  QByteArray source=
    "\\documentclass{article}\n"
    "\\long\\def\\INLINE_COMMENT#1{}\n"
    "\\newcommand*\\LyXZeroWidthSpace{\\hspace{0pt}}\n"
    "\\newtheorem*{customremark}{\\protect\\remarkname}\n"
    "\\newtheorem{customtheorem}{\\protect\\theoremname}\n"
    "\\newtheorem{observation}{Observation}\n"
    "\\providecommand{\\remarkname}{Remark}\n"
    "\\providecommand{\\theoremname}{Theorem}\n"
    "\\begin{document}\n"
    "\\title{Canonical centered title}\\maketitle\n"
    "\\INLINE_COMMENT{ATHENA-DATA cmd=\"object\" val=(\"";
  source += QByteArray::number (encoded.size ());
  source += "\", \"" + encoded + "\")}\n";
  source +=
    "\\global\\long\\def\\d{\\,\\mathrm{d}}\n"
    "Before\\LyXZeroWidthSpace after.\n"
    "Difficulty: {\\FiveStar{}}{\\FiveStar{}}.\n"
    "\\begin{customremark}Standardized.\\end{customremark}\n"
    "\\begin{customtheorem}Numbered and bold.\\end{customtheorem}\n"
    "\\begin{observation}Preserved.\\end{observation}\n"
    "\\end{document}\n";
  input.write (source);
  input.close ();

  QString executable=
    QDir (QCoreApplication::applicationDirPath ())
      .absoluteFilePath ("../src/ATHENA.bin");
  QVERIFY2 (QFile::exists (executable), qPrintable (executable));

  QString outputPath= temp.filePath ("declarations.ath");
  QProcess process;
  QProcessEnvironment env= QProcessEnvironment::systemEnvironment ();
  env.insert ("ATHENA_HOME_PATH", temp.filePath ("home"));
  env.insert ("QT_QPA_PLATFORM", "offscreen");
  env.insert ("TM_REEXEC", "1");
  QString libraryPath=
    QDir (QCoreApplication::applicationDirPath ())
      .absoluteFilePath ("../x64/lib");
  QString athenaPath= env.value ("ATHENA_PATH");
  if (!athenaPath.isEmpty ())
    libraryPath += ":" + QDir (athenaPath).absoluteFilePath ("lib");
  QString inheritedLibraryPath= env.value ("LD_LIBRARY_PATH");
  if (!inheritedLibraryPath.isEmpty ())
    libraryPath += ":" + inheritedLibraryPath;
  env.insert ("LD_LIBRARY_PATH", libraryPath);
  process.setProcessEnvironment (env);
  process.setProgram (executable);
  process.setArguments ({"-C", input.fileName (), outputPath});
  QString stdoutPath= temp.filePath ("stdout.log");
  QString stderrPath= temp.filePath ("stderr.log");
  process.setStandardOutputFile (stdoutPath);
  process.setStandardErrorFile (stderrPath);
  process.start ();
  QVERIFY2 (process.waitForFinished (40000), qPrintable (process.errorString ()));

  QFile stdoutFile (stdoutPath);
  QFile stderrFile (stderrPath);
  QVERIFY (stdoutFile.open (QIODevice::ReadOnly));
  QVERIFY (stderrFile.open (QIODevice::ReadOnly));
  QByteArray processOutput= stdoutFile.readAll () + stderrFile.readAll ();
  QCOMPARE (process.exitStatus (), QProcess::NormalExit);
  QVERIFY2 (process.exitCode () == 0, processOutput.constData ());

  QFile output (outputPath);
  QVERIFY2 (output.open (QIODevice::ReadOnly), processOutput.constData ());
  QByteArray athena= output.readAll ();

  QVERIFY (!athena.contains ("<global>"));
  QVERIFY (!athena.contains ("<long>"));
  QVERIFY (!athena.contains ("<assign|*|"));
  QVERIFY (!athena.contains ("LyXZeroWidthSpace"));
  QVERIFY (athena.contains ("Before"));
  QVERIFY (athena.contains ("after."));
  QVERIFY (athena.contains (
    "<doc-data|<doc-title|Canonical centered title>>"));
  QVERIFY (!athena.contains ("FiveStar"));
  QCOMPARE (athena.count ("\\<bigstar\\>"), 2);
  QVERIFY (athena.contains ("<assign|d|<macro|"));
  QVERIFY (athena.contains ("<\\render-remark>"));
  QVERIFY (!athena.contains ("customremark"));
  QVERIFY (athena.contains ("<\\theorem>"));
  QVERIFY (!athena.contains ("customtheorem"));
  QVERIFY (!athena.contains ("<assign|theorem-name|"));
  QVERIFY (!athena.contains ("<assign|theoremname|"));
  QVERIFY (!athena.contains ("<assign|remarkname|"));
  QVERIFY (athena.contains ("<new-theorem|observation|Observation>"));
  QVERIFY (athena.contains ("<\\observation>"));
}

QTEST_MAIN (TestLatexImport)
#include "latex_import_test.moc"
