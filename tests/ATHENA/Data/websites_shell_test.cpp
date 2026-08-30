/******************************************************************************
* MODULE     : websites_shell_test.cpp
* DESCRIPTION: Tests for content-first static website pages
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/websites_internal.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace athena_websites;

class TestWebsiteShell: public QObject {
  Q_OBJECT

private slots:
  void writesContentFirstEntryAndManifest ();
  void externalWebLinksOpenInNewTabs ();
  void persistsRedirectionConfiguration ();
  void persistsPdfGenerationConfiguration ();
  void persistsAndCopiesCustomFavicon ();
  void exposesDocumentPdfDownloads ();
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

static bool
writePage (const QString& path) {
  QFileInfo info (path);
  QDir ().mkpath (info.absolutePath ());
  QFile file (path);
  if (!file.open (QIODevice::WriteOnly)) return false;
  return file.write (
    "<!doctype html><html><head><title>Old</title></head>"
    "<body><h1>Heading</h1><p>Content</p></body></html>") > 0;
}

void
TestWebsiteShell::writesContentFirstEntryAndManifest () {
  QTemporaryDir temp;
  QVERIFY (temp.isValid ());

  athena_website_entry website;
  website.id= "test-site";
  website.name= "Test site";
  website.entrypoint_value= "Notes/Home.ath";

  GenerationContext context;
  context.destination= fs::path (temp.path ().toStdString ());
  context.selected_files= {"Notes/Home.ath"};
  context.html_paths["Notes/Home.ath"]= "Notes/Home.html";
  context.titles["Notes/Home.ath"]= "Home";
  QVERIFY (writePage (temp.filePath ("Notes/Home.html")));

  std::string error;
  QVERIFY2 (write_site_shell (website, context, error), error.c_str ());

  QString index= readText (temp.filePath ("index.html"));
  QVERIFY (!index.isEmpty ());
  QVERIFY (index.contains ("location.replace"));
  QVERIFY (index.contains ("Notes/Home.html"));
  QVERIFY (!index.contains ("iframe"));
  QVERIFY (!index.contains ("taskbar"));
  QVERIFY (QFile::exists (temp.filePath ("site-manifest.json")));
  QVERIFY (QFile::exists (temp.filePath ("site-data.js")));
  QVERIFY (QFile::exists (temp.filePath ("icons/vault.svg")));
  QVERIFY (QFile::exists (temp.filePath ("icons/search.svg")));

  QString page= readText (temp.filePath ("Notes/Home.html"));
  QVERIFY (page.contains ("window.ATHENA_SITE_ROOT=\"../\""));
  QVERIFY (page.contains ("athena-site-toolbar"));
  QVERIFY (page.contains ("site-manifest.json"));
  QVERIFY (page.contains ("src=\"../site-data.js?v="));
}

void
TestWebsiteShell::externalWebLinksOpenInNewTabs () {
  std::string bridge;
  QVERIFY (website_template_text ("document-bridge.js", bridge));
  QString script= QString::fromStdString (bridge);

  QVERIFY (script.contains ("document.querySelectorAll('a[href]')"));
  QVERIFY (script.contains ("/^(?:https?:)?\\/\\//i"));
  QVERIFY (script.contains ("link.target='_blank'"));
  QVERIFY (script.contains ("['noopener','noreferrer']"));
  QVERIFY (script.contains (
    "document.addEventListener('DOMContentLoaded',start)"));
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
TestWebsiteShell::persistsPdfGenerationConfiguration () {
  athena_website_entry website;
  website.id= "pdf-site";
  website.name= "PDF site";
  website.generate_pdfs= true;

  athena_website_entry restored= website_from_json (website_to_json (website));
  QVERIFY (restored.generate_pdfs);

  QJsonObject withoutPdf= website_to_json (website);
  withoutPdf.remove ("generatePdfs");
  QVERIFY (!website_from_json (withoutPdf).generate_pdfs);
}

void
TestWebsiteShell::persistsAndCopiesCustomFavicon () {
  QTemporaryDir temp;
  QVERIFY (temp.isValid ());
  QFile source (temp.filePath ("custom.png"));
  QVERIFY (source.open (QIODevice::WriteOnly));
  QCOMPARE (source.write ("custom favicon"), (qint64) 14);
  source.close ();

  athena_website_entry website;
  website.id= "favicon-site";
  website.name= "Favicon site";
  website.favicon= "custom.png";
  athena_website_entry restored= website_from_json (website_to_json (website));
  QCOMPARE (restored.favicon, std::string ("custom.png"));

  GenerationContext context;
  context.root= fs::path (temp.path ().toStdString ());
  context.destination= context.root / "site";
  std::string error;
  QVERIFY2 (write_site_shell (restored, context, error), error.c_str ());
  QCOMPARE (readText (temp.filePath ("site/icons/favicon.png")),
            QString ("custom favicon"));
}

void
TestWebsiteShell::exposesDocumentPdfDownloads () {
  QTemporaryDir temp;
  QVERIFY (temp.isValid ());

  athena_website_entry website;
  website.id= "pdf-site";
  website.name= "PDF site";
  website.generate_pdfs= true;
  GenerationContext context;
  context.destination= fs::path (temp.path ().toStdString ());
  context.selected_files= {"Notes/Example.ath"};
  context.html_paths["Notes/Example.ath"]= "Notes/Example.html";
  context.pdf_paths["Notes/Example.ath"]= "pdf/Notes/Example.pdf";
  context.titles["Notes/Example.ath"]= "Example";
  QVERIFY (writePage (temp.filePath ("Notes/Example.html")));

  std::string error;
  QVERIFY2 (write_site_shell (website, context, error), error.c_str ());
  QVERIFY (!readText (temp.filePath ("index.html")).contains ("doc-pdf"));
  QVERIFY (readText (temp.filePath ("site-manifest.json")).contains (
    "\"pdf\":\"pdf/Notes/Example.pdf\""));
  QString page= readText (temp.filePath ("Notes/Example.html"));
  QVERIFY (page.contains (
    "window.ATHENA_DOCUMENT_PDF=\"../pdf/Notes/Example.pdf\""));
  QVERIFY (page.contains ("athena-site-tool-pdf"));
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
  QVERIFY (writePage (temp.filePath ("Notes/Quick Start.html")));
  QVERIFY (writePage (temp.filePath ("Manual.html")));
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
  QVERIFY (writePage (temp.filePath ("Public.html")));
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
