/******************************************************************************
* MODULE     : websites_shell_test.cpp
* DESCRIPTION: Tests for versioned static website shell assets
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/websites_internal.hpp"

#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace athena_websites;

class TestWebsiteShell: public QObject {
  Q_OBJECT

private slots:
  void writesOneVersionedAssetGeneration ();
  void externalWebLinksOpenOutsideDocumentFrame ();
  void persistsRedirectionConfiguration ();
  void writesCloudflareRedirections ();
  void rejectsRedirectionOutsideExportRange ();
  void removesDisabledRedirectionsFile ();
};

static QString
readText (const QString& path) {
  QFile file (path);
  if (!file.open (QIODevice::ReadOnly)) return QString ();
  return QString::fromUtf8 (file.readAll ());
}

void
TestWebsiteShell::writesOneVersionedAssetGeneration () {
  QTemporaryDir temp;
  QVERIFY (temp.isValid ());

  athena_website_entry website;
  website.id= "test-site";
  website.name= "Test site";

  GenerationContext context;
  context.destination= fs::path (temp.path ().toStdString ());
  std::string error;
  QVERIFY2 (write_site_shell (website, context, error), error.c_str ());

  QString index= readText (temp.filePath ("index.html"));
  QVERIFY (!index.isEmpty ());
  QVERIFY (!index.contains ("Skip to desktop"));
  QVERIFY (!index.contains ("{{ASSET_VERSION}}"));

  QRegularExpression reference (
    "(?:href|src)=\\\"((?:css|js)/[^\\\"]+\\.([0-9a-f]{16})\\.(?:css|js))\\\"");
  QRegularExpressionMatchIterator matches= reference.globalMatch (index);
  QString version;
  int count= 0;
  while (matches.hasNext ()) {
    QRegularExpressionMatch match= matches.next ();
    QString path= match.captured (1);
    QString currentVersion= match.captured (2);
    if (version.isEmpty ()) version= currentVersion;
    QCOMPARE (currentVersion, version);
    QVERIFY2 (QFile::exists (temp.filePath (path)),
              qPrintable ("Missing generated asset: " + path));
    count++;
  }
  QCOMPARE (count, 9);
  QCOMPARE (version.size (), 16);
}

void
TestWebsiteShell::externalWebLinksOpenOutsideDocumentFrame () {
  std::string bridge;
  QVERIFY (website_template_text ("document-bridge.js", bridge));
  QString script= QString::fromStdString (bridge);

  QVERIFY (script.contains ("document.querySelectorAll('a[href]')"));
  QVERIFY (script.contains ("/^(?:https?:)?\\/\\//i"));
  QVERIFY (script.contains ("link.setAttribute('target','_blank')"));
  QVERIFY (script.contains ("['noopener','noreferrer']"));
  QVERIFY (script.contains (
    "document.addEventListener('DOMContentLoaded',initializeDocumentBridge)"));
}

void
TestWebsiteShell::persistsRedirectionConfiguration () {
  athena_website_entry website;
  website.id= "redirect-site";
  website.name= "Redirect site";
  website.generate_redirections= true;
  website.redirections.push_back ({"/manual", "Notes/Manual.ath"});
  website.redirections.push_back ({"/start", "Notes/Start.ath"});

  athena_website_entry restored= website_from_json (website_to_json (website));
  QVERIFY (restored.generate_redirections);
  QCOMPARE (restored.redirections.size (), (size_t) 2);
  QCOMPARE (restored.redirections[0].shortcut, std::string ("/manual"));
  QCOMPARE (restored.redirections[0].document,
            std::string ("Notes/Manual.ath"));
  QCOMPARE (restored.redirections[1].shortcut, std::string ("/start"));
}

void
TestWebsiteShell::writesCloudflareRedirections () {
  QTemporaryDir temp;
  QVERIFY (temp.isValid ());

  athena_website_entry website;
  website.id= "redirect-site";
  website.name= "Redirect site";
  website.generate_redirections= true;
  website.redirections.push_back (
    {"/quick-start", "Notes/Quick Start.ath"});
  website.redirections.push_back ({"/manual", "Manual.ath"});

  GenerationContext context;
  context.destination= fs::path (temp.path ().toStdString ());
  context.selected_files= {"Notes/Quick Start.ath", "Manual.ath"};
  context.html_paths["Notes/Quick Start.ath"]= "Notes/Quick Start.html";
  context.html_paths["Manual.ath"]= "Manual.html";
  std::string error;
  QVERIFY2 (write_site_shell (website, context, error), error.c_str ());

  QCOMPARE (readText (temp.filePath ("_redirects")),
            QString ("/quick-start /Notes/Quick%20Start.html 302\n"
                     "/manual /Manual.html 302\n"));
}

void
TestWebsiteShell::rejectsRedirectionOutsideExportRange () {
  QTemporaryDir temp;
  QVERIFY (temp.isValid ());

  athena_website_entry website;
  website.id= "redirect-site";
  website.name= "Redirect site";
  website.generate_redirections= true;
  website.redirections.push_back ({"/private", "Private.ath"});

  GenerationContext context;
  context.destination= fs::path (temp.path ().toStdString ());
  context.selected_files= {"Public.ath"};
  context.html_paths["Public.ath"]= "Public.html";
  std::string error;
  QVERIFY (!write_site_shell (website, context, error));
  QVERIFY (QString::fromStdString (error).contains ("outside the exported range"));
}

void
TestWebsiteShell::removesDisabledRedirectionsFile () {
  QTemporaryDir temp;
  QVERIFY (temp.isValid ());
  QFile stale (temp.filePath ("_redirects"));
  QVERIFY (stale.open (QIODevice::WriteOnly));
  stale.write ("/old /Old.html 302\n");
  stale.close ();

  athena_website_entry website;
  website.id= "redirect-site";
  website.name= "Redirect site";
  website.generate_redirections= false;
  GenerationContext context;
  context.destination= fs::path (temp.path ().toStdString ());
  std::string error;
  QVERIFY2 (write_site_shell (website, context, error), error.c_str ());
  QVERIFY (!QFile::exists (temp.filePath ("_redirects")));
}

QTEST_APPLESS_MAIN (TestWebsiteShell)
#include "websites_shell_test.moc"
