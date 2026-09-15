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

#include <filesystem>

#include "ATHENA/Data/vault_map_sqlite.hpp"

class TestCommandLineConversion: public QObject {
  Q_OBJECT
private slots:
  void convertsBeforeContinuing();
  void ignoresPersonalInitFiles();
  void websiteGenerationSkipsEditorModeLazyInitialization();
  void vaultMaintenanceUsesHeadlessDocumentContext();
};

static QByteArray contents (const QString& path) {
  QFile file (path);
  if (!file.open (QIODevice::ReadOnly)) return {};
  return file.readAll ();
}

static QString scheme_quote (QString value) {
  value.replace ('\\', "\\\\");
  value.replace ('"', "\\\"");
  return '"' + value + '"';
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
    const QByteArray body= name.toUtf8 () +
      (name == "FIRSTINPUT" ? QByteArray (" <ATHENA>") : QByteArray ());
    const QByteArray source= "<TeXmacs|2.1.4>\n\n<style|generic>\n\n"
      "<\\body>\n  " + body + "\n</body>\n\n"
      "<\\initial>\n  <\\collection>\n"
      "    <associate|page-medium|automatic>\n"
      "  </collection>\n</initial>\n";
    QCOMPARE (file.write (source), source.size ());
  }

  const QString executable= QDir (QCoreApplication::applicationDirPath ())
    .absoluteFilePath ("../src/ATHENA.bin");
  const QString resources= QDir (QCoreApplication::applicationDirPath ())
    .absoluteFilePath ("../../ATHENA");
  QProcess process;
  auto env= QProcessEnvironment::systemEnvironment ();
  env.insert ("ATHENA_HOME_PATH", temp.filePath ("home"));
  env.insert ("ATHENA_PATH", resources);
  env.insert ("QT_QPA_PLATFORM", "offscreen");
  env.insert ("PWD", temp.path ());
  process.setProcessEnvironment (env);
  process.setWorkingDirectory (temp.path ());
  process.setProcessChannelMode (QProcess::MergedChannels);
  process.start (executable, {"-C", "inputs/FIRSTINPUT.tm", "first.pdf",
                             "-C", "inputs/FIRSTINPUT.tm", "first.tex",
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
  const QByteArray latex= contents (temp.filePath ("first.tex"));
  QVERIFY2 (latex.contains ("FIRSTINPUT"),
            (log + "\nfirst.tex: " + latex).constData ());
  QVERIFY2 (latex.contains ("\\newcommand{\\ATHENA}"),
            (log + "\nfirst.tex: " + latex).constData ());
  QVERIFY2 (latex.contains ("\\usepackage{graphicx}"),
            (log + "\nfirst.tex: " + latex).constData ());
  QVERIFY2 (!log.contains ("GUI buffer registry accessed from a BufferActor"),
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

void TestCommandLineConversion::ignoresPersonalInitFiles() {
  QTemporaryDir temp;
  QVERIFY (temp.isValid ());
  QVERIFY (QDir ().mkpath (temp.filePath ("home/progs")));
  QVERIFY (QDir ().mkpath (temp.filePath ("home/fonts")));
  QVERIFY (QDir ().mkpath (temp.filePath ("home/system")));

  const QString startup_poison= temp.filePath ("startup-poison");
  const QString buffer_poison= temp.filePath ("buffer-poison");
  const QString result= temp.filePath ("result.txt");
  const QString document= temp.filePath ("document.ath");
  auto write= [] (const QString& path, const QByteArray& bytes) {
    QFile file (path);
    return file.open (QIODevice::WriteOnly) && file.write (bytes) == bytes.size ();
  };

  QVERIFY (write (temp.filePath ("home/progs/my-init-texmacs.scm"),
    QString ("(string-save \"executed\" (system->url %1))\n")
      .arg (scheme_quote (startup_poison)).toUtf8 ()));
  QVERIFY (write (temp.filePath ("home/progs/my-init-buffer.scm"),
    QString ("(string-save \"executed\" (system->url %1))\n")
      .arg (scheme_quote (buffer_poison)).toUtf8 ()));
  QVERIFY (write (document,
    "<TeXmacs|2.1.4>\n\n<style|generic>\n\n<\\body>\n"
    "Personal init files must be ignored.\n</body>\n"));

  const QString probe= QString (R"SCM(
(delayed (:pause 1500)
  (exec-global
    (lambda ()
      (string-save
        (if (buffer-exists? (system->url %1)) "loaded" "missing")
        (system->url %2))
      (quit-TeXmacs))))
)SCM")
    .arg (scheme_quote (document), scheme_quote (result));

  const QDir binaries (QCoreApplication::applicationDirPath ());
  const QString executable= binaries.absoluteFilePath ("../src/ATHENA.bin");
  QProcess process;
  auto env= QProcessEnvironment::systemEnvironment ();
  env.insert ("ATHENA_HOME_PATH", temp.filePath ("home"));
  env.insert ("ATHENA_PATH", binaries.absoluteFilePath ("../../ATHENA"));
  env.insert ("QT_QPA_PLATFORM", "offscreen");
  env.insert ("PWD", temp.path ());
  process.setProcessEnvironment (env);
  process.setWorkingDirectory (temp.path ());
  process.setProcessChannelMode (QProcess::MergedChannels);
  process.start (executable,
    {"--no-splash-screen", "-X", "-x", probe, document});
  QVERIFY2 (process.waitForFinished (20000), qPrintable (process.errorString ()));
  const QByteArray log= process.readAll ();
  QCOMPARE (process.exitStatus (), QProcess::NormalExit);
  QVERIFY2 (process.exitCode () == 0, log.constData ());
  QCOMPARE (contents (result), QByteArray ("loaded"));
  QVERIFY2 (!QFile::exists (startup_poison), log.constData ());
  QVERIFY2 (!QFile::exists (buffer_poison), log.constData ());
}

void TestCommandLineConversion::websiteGenerationSkipsEditorModeLazyInitialization() {
  QTemporaryDir temp;
  QVERIFY (temp.isValid ());
  QVERIFY (QDir ().mkpath (temp.filePath ("home/fonts")));
  QVERIFY (QDir ().mkpath (temp.filePath ("home/system")));

  const QString executable= QDir (QCoreApplication::applicationDirPath ())
    .absoluteFilePath ("../src/ATHENA.bin");
  const QString resources= QDir (QCoreApplication::applicationDirPath ())
    .absoluteFilePath ("../../ATHENA");
  QProcess process;
  auto env= QProcessEnvironment::systemEnvironment ();
  env.insert ("ATHENA_HOME_PATH", temp.filePath ("home"));
  env.insert ("ATHENA_PATH", resources);
  env.insert ("QT_QPA_PLATFORM", "offscreen");
  env.insert ("PWD", temp.path ());
  process.setProcessEnvironment (env);
  process.setWorkingDirectory (temp.path ());
  process.setProcessChannelMode (QProcess::MergedChannels);
  process.start (executable,
                 {"--generate-website", temp.filePath ("missing-vault"),
                  "missing-site"});
  QVERIFY2 (process.waitForFinished (20000), qPrintable (process.errorString ()));
  const QByteArray log= process.readAll ();
  QCOMPARE (process.exitStatus (), QProcess::NormalExit);
  QCOMPARE (process.exitCode (), 1);
  QVERIFY2 (log.contains ("website generation failed"), log.constData ());
  QVERIFY2 (!log.contains ("editor state is owned by its BufferActor"),
            log.constData ());
}

void TestCommandLineConversion::vaultMaintenanceUsesHeadlessDocumentContext() {
  QTemporaryDir temp;
  QVERIFY (temp.isValid ());
  QVERIFY (QDir ().mkpath (temp.filePath ("home/fonts")));
  QVERIFY (QDir ().mkpath (temp.filePath ("home/system")));
  QVERIFY (QDir ().mkpath (temp.filePath ("vault")));
  auto write= [&] (const QString& path, const QByteArray& bytes) {
    QFile file (temp.filePath (path));
    return file.open (QIODevice::WriteOnly) && file.write (bytes) == bytes.size ();
  };
  QVERIFY (write ("home/system/preferences.json",
    "{\"format\":\"athena-preferences\",\"version\":1,\"preferences\":{"
    "\"texmacs->pdf:data-art cover\":\"on\"}}"));
  QVERIFY (write ("vault/Vaultfile.json", "{\"name\":\"Maintenance test\"}"));
  QVERIFY (write ("vault/websites.json",
    "{\"version\":1,\"websites\":[{"
    "\"id\":\"maintenance-site\","
    "\"name\":\"Maintenance site\","
    "\"selector\":{\"kind\":\"path\",\"path\":\"test.ath\"},"
    "\"destination\":\"generated-site\","
    "\"generatePdfs\":true,"
    "\"regenerate\":\"maintenance\","
    "\"entrypoint\":{\"kind\":\"file\",\"path\":\"test.ath\"},"
    "\"postCommand\":{\"enabled\":false}}]}"));
  QVERIFY (write ("vault/test.ath",
    "<TeXmacs|2.1.4>\n\n<style|generic>\n\n<\\body>\n"
    "<section|Maintenance test>\n\n"
    "<\\theorem>\nDataArt theorem sentinel<label|data-art-label>\n</theorem>\n\n"
    "DataArt reference sentinel: <reference|data-art-label>.\n\n"
    "<\\proof>\nDataArt proof sentinel.\n</proof>\n"
    "<transclude|data-art-transclusion|child.ath|trans-b|trans-e>\n"
    "<\\table-of-contents|toc>\n  \n</table-of-contents>\n</body>\n"));
  QVERIFY (write ("vault/child.ath",
    "<TeXmacs|2.1.4>\n\n<style|generic>\n\n<\\body>\n"
    "<label|trans-b>\nDataArt transclusion sentinel.\n<label|trans-e>\n"
    "</body>\n"));
  {
    AthenaVaultMapSqlite map;
    std::string error;
    QVERIFY2 (map.open (
      std::filesystem::path (temp.filePath ("vault/map.sqlite").toStdString ()),
      true, error), error.c_str ());
    QVERIFY2 (map.set_node (
      AthenaVaultMapNode {"data-art-transclusion", "child.ath",
                          "trans-b", "trans-e"}, error), error.c_str ());
    map.close ();
  }
  const QDir binaries (QCoreApplication::applicationDirPath ());
  QProcess process;
  auto env= QProcessEnvironment::systemEnvironment ();
  env.insert ("ATHENA_HOME_PATH", temp.filePath ("home"));
  env.insert ("ATHENA_PATH", binaries.absoluteFilePath ("../../ATHENA"));
  env.insert ("QT_QPA_PLATFORM", "offscreen");
  env.insert ("PWD", temp.path ());
  env.insert ("ATHENA_VAULT_MAINTENANCE_TAKE_PREFS", "off");
  // Cover the real startup and anchor transforms without models or services.
  env.insert ("ATHENA_VAULT_MAINTENANCE_SKIP_PASSES",
    "full-backup,maintain-materials,normalize-assets,scan-missing-images,"
    "normalize-person-names,build-artifacts,update-tocs,continuous-rag,"
    "collect-orphans,purge-retained-data,dispatch-backups");
  env.remove ("ATHENA_VAULT_MAINTENANCE_ENABLE_PASSES");
  process.setProcessEnvironment (env);
  process.setWorkingDirectory (temp.path ());
  process.setProcessChannelMode (QProcess::MergedChannels);
  const QString executable= binaries.absoluteFilePath ("../src/ATHENA.bin");
  for (bool check_only: {true, false}) {
    QStringList args {"--vault-maintenance", temp.filePath ("vault")};
    if (check_only) args << "--check-only";
    process.start (executable, args);
    QVERIFY2 (process.waitForFinished (45000), qPrintable (process.errorString ()));
    const QByteArray log= process.readAll ();
    QVERIFY2 (process.exitStatus () == QProcess::NormalExit &&
              process.exitCode () == 0, log.constData ());
    QVERIFY2 (log.contains ("health check: all 2 .ath file(s) are legible"),
              log.constData ());
    if (!check_only)
      QVERIFY2 (log.contains ("pass success: anchor-structures"), log.constData ());
    if (!check_only) {
      QVERIFY2 (log.contains ("pass success: generate-websites"), log.constData ());
      const QByteArray pdf= contents (
        temp.filePath ("vault/generated-site/pdf/test.pdf"));
      QVERIFY2 (pdf.startsWith ("%PDF-"), log.constData ());
      const QString pdf_path= temp.filePath ("vault/generated-site/pdf/test.pdf");
      const QString pdftotext= QStandardPaths::findExecutable ("pdftotext");
      if (!pdftotext.isEmpty ()) {
        QProcess inspect;
        inspect.start (pdftotext, {pdf_path, "-"});
        QVERIFY2 (inspect.waitForFinished (15000),
                  qPrintable (inspect.errorString ()));
        const QByteArray text= inspect.readAllStandardOutput ();
        QVERIFY2 (text.contains ("DataArt theorem sentinel"), text.constData ());
        QVERIFY2 (text.contains ("DataArt reference sentinel: 1."), text.constData ());
        QVERIFY2 (text.contains ("DataArt proof sentinel."), text.constData ());
        QVERIFY2 (text.contains ("DataArt transclusion sentinel."), text.constData ());
      }
      const QString pdfimages= QStandardPaths::findExecutable ("pdfimages");
      if (!pdfimages.isEmpty ()) {
        QProcess inspect;
        inspect.start (pdfimages, {"-list", pdf_path});
        QVERIFY2 (inspect.waitForFinished (15000),
                  qPrintable (inspect.errorString ()));
        const QByteArray images= inspect.readAllStandardOutput ();
        QVERIFY2 (images.contains ("image") && images.contains ("rgb"),
                  images.constData ());
      }
    }
    QVERIFY2 (!log.contains ("editor state is owned by its BufferActor"), log.constData ());
    QVERIFY2 (!log.contains ("GUI buffer registry accessed from a BufferActor"),
              log.constData ());
    QVERIFY2 (!log.contains ("Unbound variable"), log.constData ());
  }
  process.start (executable, {"--vault-maintenance-toc-worker",
    temp.filePath ("vault/test.ath"), temp.filePath ("toc-result")});
  QVERIFY2 (process.waitForFinished (20000), qPrintable (process.errorString ()));
  const QByteArray log= process.readAll ();
  QVERIFY2 (process.exitStatus () == QProcess::NormalExit &&
            process.exitCode () == 0, log.constData ());
  QVERIFY2 (contents (temp.filePath ("toc-result")) == "ok", log.constData ());
  const QByteArray saved= contents (temp.filePath ("vault/test.ath"));
  const auto start= saved.indexOf ("<\\table-of-contents|toc>");
  const auto end= saved.indexOf ("</table-of-contents>", start);
  QVERIFY2 (start >= 0 && end > start, saved.constData ());
  const QByteArray toc= saved.mid (start, end - start).simplified ();
  QVERIFY2 (toc.contains ("Maintenance test") && toc.contains ("<pageref|"),
            toc.constData ());
}

QTEST_MAIN (TestCommandLineConversion)
#include "command_line_conversion_test.moc"
