/******************************************************************************
* MODULE     : materials_latex_roundtrip_test.cpp
* DESCRIPTION: lossless LaTeX round trip for Materials document nodes
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
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

namespace {

bool
run_conversion (const QString& executable, const QProcessEnvironment& env,
                const QString& input, const QString& output,
                QByteArray& diagnostics) {
  QProcess process;
  process.setProcessEnvironment (env);
  QString athenaPath= env.value ("ATHENA_PATH");
  if (!athenaPath.isEmpty ())
    process.setWorkingDirectory (QDir (athenaPath).absoluteFilePath (".."));
  process.setProgram (executable);
  process.setArguments ({"-C", input, output});
  process.start ();
  if (!process.waitForFinished (40000)) {
    diagnostics= process.errorString ().toUtf8 ();
    process.kill ();
    process.waitForFinished ();
    return false;
  }
  QByteArray combined=
    process.readAllStandardOutput () + process.readAllStandardError ();
  bool ok= process.exitStatus () == QProcess::NormalExit && process.exitCode () == 0;
  diagnostics= QByteArray ("exit-status=") +
    QByteArray::number ((int) process.exitStatus ()) + " exit-code=" +
    QByteArray::number (process.exitCode ()) + "\n" + combined.right (3000);
  return ok;
}

} // namespace

class MaterialsLatexRoundTripTest: public QObject {
  Q_OBJECT

private slots:
  void preservesCitationAndReferencedMaterials ();
};

void
MaterialsLatexRoundTripTest::preservesCitationAndReferencedMaterials () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  QVERIFY (QDir ().mkpath (temporary.filePath ("home/fonts")));
  QVERIFY (QDir ().mkpath (temporary.filePath ("home/system")));

  const QByteArray uuid= "11111111-1111-4111-8111-111111111111";
  QFile source (temporary.filePath ("materials.ath"));
  QVERIFY (source.open (QIODevice::WriteOnly | QIODevice::Text));
  QByteArray document=
    "<TeXmacs|2.1.4>\n\n"
    "<style|generic>\n\n"
    "<\\body>\n"
    "See <material-citation|<tuple|<material-cite-item|" + uuid +
    "|page|42>>|<hlink|(Tu, 2011, p. 42)|tmfs://material/" + uuid +
    "?locator=page&value=42>>.\n\n"
    "<referenced-materials|apa|<tuple|" + uuid +
    ">|<document|Tu, L. W. (2011). An Introduction to Manifolds.>>\n"
    "</body>\n";
  QCOMPARE (source.write (document), document.size ());
  source.close ();

  QString executable= QDir (QCoreApplication::applicationDirPath ())
    .absoluteFilePath ("../src/ATHENA.bin");
  QVERIFY2 (QFile::exists (executable), qPrintable (executable));
  QProcessEnvironment env= QProcessEnvironment::systemEnvironment ();
  env.insert ("ATHENA_HOME_PATH", temporary.filePath ("home"));
  env.insert ("QT_QPA_PLATFORM", "offscreen");
  env.insert ("TM_REEXEC", "1");
  QString libraryPath= QDir (QCoreApplication::applicationDirPath ())
    .absoluteFilePath ("../x64/lib");
  QString athenaPath= env.value ("ATHENA_PATH");
  if (!athenaPath.isEmpty ())
    libraryPath += ":" + QDir (athenaPath).absoluteFilePath ("lib");
  QString inherited= env.value ("LD_LIBRARY_PATH");
  if (!inherited.isEmpty ()) libraryPath += ":" + inherited;
  env.insert ("LD_LIBRARY_PATH", libraryPath);

  QString latexPath= temporary.filePath ("materials.tex");
  QByteArray diagnostics;
  QVERIFY2 (run_conversion (executable, env, source.fileName (), latexPath,
                            diagnostics), diagnostics.constData ());
  QFile latex (latexPath);
  QVERIFY (latex.open (QIODevice::ReadOnly));
  QByteArray latexBytes= latex.readAll ();
  QVERIFY (latexBytes.contains ("ATHENA-DATA cmd=\"object\""));
  QVERIFY (latexBytes.contains ("(Tu, 2011, p. 42)"));
  QVERIFY (latexBytes.contains ("An Introduction to Manifolds"));

  QString importedPath= temporary.filePath ("materials-roundtrip.ath");
  QVERIFY2 (run_conversion (executable, env, latexPath, importedPath,
                            diagnostics), diagnostics.constData ());
  QFile imported (importedPath);
  QVERIFY (imported.open (QIODevice::ReadOnly));
  QByteArray importedBytes= imported.readAll ();
  QVERIFY (importedBytes.contains (
    "<material-cite-item|" + uuid + "|page|42>"));
  QVERIFY (importedBytes.contains ("<material-citation|"));
  QVERIFY (importedBytes.contains ("referenced-materials|"));
  QVERIFY (importedBytes.contains ("apa"));
  QVERIFY (importedBytes.count (uuid) >= 3);
  QVERIFY (importedBytes.contains ("tmfs://material/" + uuid));
}

QTEST_MAIN (MaterialsLatexRoundTripTest)
#include "materials_latex_roundtrip_test.moc"
