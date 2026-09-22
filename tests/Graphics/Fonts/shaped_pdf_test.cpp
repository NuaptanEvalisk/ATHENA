/******************************************************************************
 * MODULE     : shaped_pdf_test.cpp
 * DESCRIPTION: UTF-8 shaped glyph PDF rendering and exact text extraction
 * COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
 *******************************************************************************
 * This software falls under the GNU general public license version 3 or later.
 * It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
 * in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
 ******************************************************************************/
#include "Pdf/pdf_hummus_renderer.hpp"
#include "boot.hpp"
#include "data_cache.hpp"
#include "drd_std.hpp"
#include "font.hpp"
#include "pdf_text_string.hpp"
#include "printer.hpp"
#include "scheme.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

bool headless_mode = true;
bool is_headless () { return true; }

static void require (bool value, const char *message) {
  if (!value)
    throw std::runtime_error (message);
}

static QByteArray execute (const QString &program,
                           const QStringList &arguments) {
  require (!QStandardPaths::findExecutable (program).isEmpty (),
           "PDF regression requires Poppler and qpdf executables");
  QProcess process;
  process.start (program, arguments);
  require (process.waitForStarted (5000), "Could not start PDF verification");
  require (process.waitForFinished (10000), "PDF verification timed out");
  const QByteArray errors = process.readAllStandardError ();
  if (process.exitStatus () != QProcess::NormalExit || process.exitCode () != 0)
    throw std::runtime_error (errors.toStdString ());
  if (!errors.isEmpty ())
    throw std::runtime_error (program.toStdString () + ": " +
                              errors.toStdString ());
  return process.readAllStandardOutput ();
}

static void check_bitmap_mappings (const QString& pdf) {
  const auto document= QJsonDocument::fromJson (execute ("qpdf", {
    "--json", "--json-key=qpdf", "--json-stream-data=inline", pdf}));
  require (document.isObject (), "Could not inspect PDF font dictionaries");
  const auto sections= document.object ().value ("qpdf").toArray ();
  require (sections.size () == 2, "Missing qpdf object table");
  const auto objects= sections[1].toObject ();
  QByteArray mappings;
  for (auto item= objects.begin (); item != objects.end (); ++item) {
    const auto dictionary= item.value ().toObject ().value ("value").toObject ();
    if (dictionary.value ("/Subtype").toString () != "/Type3") continue;
    const auto reference= dictionary.value ("/ToUnicode").toString ();
    const auto stream= objects.value ("obj:" + reference).toObject ()
      .value ("stream").toObject ();
    mappings+= QByteArray::fromBase64 (stream.value ("data").toString ().toLatin1 ());
  }
  // ActualText must not hide bogus glyph-id-as-Unicode font mappings.
  require (mappings.contains ("<03b1>") && mappings.contains ("<00650301>") &&
           mappings.contains ("<d835dc00>"),
           "Bitmap CMaps must retain Unicode clusters, not physical glyph ids");
}

struct bitmap_only_glyphs final : font_glyphs_rep {
  font_glyphs original;
  explicit bitmap_only_glyphs (font_glyphs glyphs)
      : font_glyphs_rep ("utf8-test-bitmap-" * glyphs->res_name),
        original (glyphs) {}
  glyph &get (int code) override { return original->get (code); }
};

static void render_document (const QString &path, bool postscript) {
  font_domain owner;
  font_domain_binding binding (owner);
  const string fonts = string (std::getenv ("ATHENA_PATH")) *
                       string ("/fonts/truetype/texgyre/");
  cache_set ("font_cache.scm", "ttf:texgyrepagella-regular",
             fonts * "texgyrepagella-regular.otf");
  cache_set ("font_cache.scm", "ttf:texgyrepagella-math",
             fonts * "texgyrepagella-math.otf");
  cache_set ("font_cache.scm", "ttf:LinLibertine_R",
             string (std::getenv ("ATHENA_PATH")) *
                 string ("/fonts/truetype/libertine/LinLibertine_R.otf"));
  font text = unicode_font ("texgyrepagella-regular", 12, 600);
  font math = unicode_font ("texgyrepagella-math", 12, 600);
  font embedded = unicode_font ("LinLibertine_R", 12, 600);
  const url output = url_system (string (path.toUtf8 ().constData ()));
  renderer pdf = postscript ? static_cast<renderer> (tm_new<printer_rep> (
                                  output, 600, 1, "a4", false, 21.0, 29.7))
                            : pdf_hummus_renderer (output, 600);
  require (pdf->is_started (), "Could not create PDF");
  pdf->set_pencil (pencil (black));
  auto line = [&] (font fn, const std::string &source, int row,
                   bool bitmap = false) {
    auto run = fn->shape_utf8 (source, 0, source.size ());
    require (!run.missing_glyphs, "PDF fixture font lacks a required glyph");
    if (bitmap)
      run.glyph_source = tm_new<bitmap_only_glyphs> (run.glyph_source);
    run.draw_fixed (pdf, source, 400 * PIXEL, -(600 + row * 300) * PIXEL);
  };
  try {
    line (text, "literal <alpha> | Unicode \xce\xb1", 0);
    line (embedded, "ffi \xef\xac\x83", 1);
    // Same physical glyph in distinct spans must retain each original spelling.
    line (embedded, "\xc3\xa9", 2);
    line (embedded, "e\xcc\x81", 3);
    line (text, "escaping (parentheses) \\ < > %", 4);
    line (math, "\xf0\x9d\x90\x80", 5);
    line (text, "bitmap \xce\xb1 e\xcc\x81", 6, true);
    // The source argument includes joining context; only the item is exported.
    const std::string context = "prefix excerpt suffix";
    auto excerpt = text->shape_utf8 (context, 7, 14);
    excerpt.draw_fixed (pdf, context, 400 * PIXEL, -2700 * PIXEL);
    // Preserve the independent legacy drawing entry point during migration.
    text->draw_fixed (pdf, "legacy", 400 * PIXEL, -3000 * PIXEL);
    line (math, "\xf0\x9d\x90\x80", 9, true);
  } catch (...) {
    tm_delete (pdf);
    throw;
  }
  tm_delete (pdf);
}

static int status = 1;
static std::unique_ptr<QTemporaryDir> profile;

static void run_tests (int, char **) {
  try {
    init_std_drd ();
    eval ("(define (standard-paper-size value) value)");
    set_user_preference ("texmacs->pdf:version", "1.4");
    require (athena::text::pdf_text_string ("e\xcc\x81") == "<FEFF00650301>",
             "PDF text strings must preserve decomposed spelling");
    require (athena::text::pdf_text_string ("\xf0\x9d\x90\x80") ==
                 "<FEFFd835dc00>",
             "PDF text strings must encode surrogate pairs");
    require (athena::text::pdf_text_string (std::string_view ("\0", 1)) ==
                 "<FEFF0000>",
             "PDF text strings must preserve embedded NUL");
    bool rejected = false;
    try {
      (void)athena::text::pdf_text_string ("\xff");
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    require (rejected, "PDF text strings must reject invalid UTF-8");
    QTemporaryDir output;
    require (output.isValid (), "No temporary PDF directory");
    for (const bool postscript : {false, true}) {
      const QString stem = postscript ? "shaped-ps" : "shaped";
      const QString pdf = output.filePath (stem + ".pdf");
      if (postscript) {
        const QString ps = output.filePath (stem + ".ps");
        render_document (ps, true);
        execute ("gs",
                 {"-q", "-dSAFER", "-dBATCH", "-dNOPAUSE", "-sDEVICE=pdfwrite",
                  "-dCompatibilityLevel=1.5", "-sOutputFile=" + pdf, ps});
      } else
        render_document (pdf, false);
      if (const char *directory = std::getenv ("ATHENA_SHAPED_PDF_TEST_OUTPUT"))
        require (QFile::copy (pdf, QString::fromUtf8 (directory) + "/" + stem +
                                     ".pdf"),
                 "Could not retain test PDF");
      const auto check = execute ("qpdf", {"--check", pdf});
      require (check.contains ("No syntax or stream encoding errors"),
               "PDF structural check did not complete");
      const auto information = execute ("pdfinfo", {pdf});
      bool pdf15 = false;
      for (const auto &field : information.split ('\n'))
        if (field.startsWith ("PDF version:"))
          pdf15 = field.mid (12).trimmed () == "1.5";
      require (
          pdf15,
          "ActualText spans require an effective PDF version of at least 1.5");
      const auto fonts = execute ("pdffonts", {pdf});
      if (!postscript) check_bitmap_mappings (pdf);
      require (postscript || (fonts.contains ("Type 3") &&
                              fonts.contains ("TeXGyrePagellaMath") &&
                              fonts.contains ("LinLibertine") &&
                              (fonts.contains ("CID Type 0C") ||
                               fonts.contains ("Type 1C"))),
               "PDF fixture must cover bitmap and native embedded font paths");
      const auto extracted = execute (
          "pdftotext", {"-raw", "-nopgbrk", "-enc", "UTF-8", pdf, "-"});
      const QList<QByteArray> expected{"literal <alpha> | Unicode \xce\xb1",
                                       "ffi \xef\xac\x83",
                                       "\xc3\xa9",
                                       "e\xcc\x81",
                                       "escaping (parentheses) \\ < > %",
                                       "\xf0\x9d\x90\x80",
                                       "bitmap \xce\xb1 e\xcc\x81",
                                       "excerpt",
                                       "legacy"};
      const auto lines = extracted.split ('\n');
      require (lines.count (QByteArray ("\xf0\x9d\x90\x80")) == 2,
               "Native and bitmap fonts must both preserve non-BMP text");
      for (const auto &value : expected) {
        if (postscript && value == "legacy")
          continue;
        if (!lines.contains (value)) {
          std::cerr << "Expected PDF line: " << value.constData ()
                    << "\nActual PDF text:\n"
                    << extracted.constData () << '\n';
          throw std::runtime_error (
              "PDF did not preserve original Unicode text");
        }
      }
      require (!extracted.contains ("prefix") && !extracted.contains ("suffix"),
               "PDF exported context outside the shaped range");
      execute ("pdftoppm", {"-png", "-singlefile", "-r", "96", pdf,
                            output.filePath (stem)});
      QImage image (output.filePath (stem + ".png"));
      require (!image.isNull (), "No rendered PDF image");
      // Every expected text row needs real ink, including the
      // supplementary-plane glyph and the Type 3 fallback; extraction alone
      // could accept blank pages.
      for (int row = 0; row < 10; ++row) {
        const int baseline = (600 + row * 300) * 96 / 600;
        int ink = 0;
        for (int y = baseline - 22; y <= baseline + 5; ++y)
          for (int x = 50; x < 650; ++x)
            ink += qGray (image.pixel (x, y)) < 220;
        require (ink > 15, "An extracted PDF text row is visually blank");
      }
      if (const char *directory =
              std::getenv ("ATHENA_SHAPED_PDF_TEST_OUTPUT")) {
        require (
            image.save (QString::fromUtf8 (directory) + "/" + stem + ".png"),
            "Could not retain PDF image");
      }
    }
    std::cout
        << "Shaped PDF/PostScript text, glyphs and bitmap fallback passed\n";
    status = 0;
  } catch (const std::exception &error) {
    std::cerr << error.what () << '\n';
  }
  profile.reset ();
  std::exit (status);
}

int main (int argc, char **argv) {
  QCoreApplication app (argc, argv);
  profile = std::make_unique<QTemporaryDir> ();
  if (!profile->isValid () || std::getenv ("ATHENA_PATH") == nullptr)
    return 1;
  qputenv ("ATHENA_HOME_PATH", profile->path ().toUtf8 ());
  start_scheme (argc, argv, run_tests);
  return status;
}
