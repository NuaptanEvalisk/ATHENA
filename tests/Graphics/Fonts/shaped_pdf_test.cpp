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
#include "Boxes/construct.hpp"
#include "unicode_text.hpp"
#include "shaped_line.hpp"
#include "font_selection.hpp"
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
#include <QXmlStreamReader>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <filesystem>
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

static void check_text_geometry (const QString& pdf, int minimum_words= 10) {
  QXmlStreamReader xml (execute ("pdftotext", {"-bbox", "-enc", "UTF-8", pdf, "-"}));
  int words= 0;
  while (!xml.atEnd ()) {
    if (xml.readNext () != QXmlStreamReader::StartElement || xml.name () != "word")
      continue;
    const auto a= xml.attributes ();
    bool valid= true;
    auto coordinate= [&] (const char* name) {
      bool ok= false;
      const double result= a.value (QLatin1String (name)).toDouble (&ok);
      valid= valid && ok && std::isfinite (result);
      return result;
    };
    const double x1= coordinate ("xMin"), x2= coordinate ("xMax");
    const double y1= coordinate ("yMin"), y2= coordinate ("yMax");
    require (valid && x1 >= 40 && x2 > x1 && x2 < 550 && y2 > y1 &&
             y1 >= 45 && y2 < 420,
             "Extracted text geometry disagrees with rendered text placement");
    ++words;
  }
  require (!xml.hasError () && words >= minimum_words, "Missing PDF text geometry");
}

static void check_collection_export (const QString& pdf) {
  font_domain owner;
  font_domain_binding binding (owner);
  const auto fixture= (std::filesystem::path (__FILE__).parent_path () /
                       "fixtures/two-faces.ttc").string ();
  QTemporaryDir copies;
  require (copies.isValid (), "No isolated collection directory");
  const auto filename= copies.filePath (QString::fromUtf8 ("font-\xe5\xad\x97:collection.ttc"));
  require (QFile::copy (QString::fromUtf8 (fixture.c_str ()), filename),
           "Could not copy Unicode-path font fixture");
  const auto file= filename.toUtf8 ().toStdString ();
  const url output= url_system (string (pdf.toUtf8 ().constData ()));
  renderer ren= pdf_hummus_renderer (output, 600);
  require (ren->is_started (), "Could not create collection PDF");
  try {
    ren->set_pencil (pencil (black));
    const std::string text= "A \xce\xb1";
    for (long index: {0L, 1L}) {
      const auto run= athena::text::shape_freetype_utf8 (
        athena::text::font_file_source {file, index}, 12, 600, 600, text, 0, text.size ());
      run.draw_fixed (ren, text, 400 * PIXEL, -(600 + index * 300) * PIXEL);
    }
    const auto variable= (std::filesystem::path (__FILE__).parent_path () /
                           "fixtures/named-instance.ttf").string ();
    const auto heavy= athena::text::shape_freetype_utf8 (
      athena::text::font_file_source {variable, 0x10000}, 12, 600, 600, text, 0, text.size ());
    heavy.draw_fixed (ren, text, 400 * PIXEL, -1200 * PIXEL);
    const auto compressed= athena::text::shape_freetype_utf8 (
      athena::text::font_file_source {file, 1}, 12, 600, 300, text, 0, text.size ());
    compressed.draw_fixed (ren, text, 400 * PIXEL, -1500 * PIXEL);
    const auto medium= athena::text::shape_freetype_utf8 (
      athena::text::font_file_source {variable, 0, {650 * 65536}}, 12, 600, 600, text, 0, text.size ());
    medium.draw_fixed (ren, text, 400 * PIXEL, -1800 * PIXEL);
    athena::text::font_catalog catalog (false, {file});
    athena::text::font_request request {"ATHENA Collection Fixture One,ATHENA Collection Fixture Two"};
    request.horizontal_dpi= request.vertical_dpi= 600;
    const std::string mixed= "A \xce\xb1\xce\xb2 A";
    athena::text::font_paragraph paragraph (mixed, request, catalog);
    const auto line= paragraph.line (0, mixed.size ());
    require (!line.missing_glyphs, "PDF fallback retained a missing glyph");
    line.draw_fixed (ren, paragraph.analysis ().source (), 400 * PIXEL, -2100 * PIXEL);
  }
  catch (...) { tm_delete (ren); throw; }
  tm_delete (ren);
  if (const char *directory= std::getenv ("ATHENA_SHAPED_PDF_TEST_OUTPUT"))
    require (QFile::copy (pdf, QString::fromUtf8 (directory) + "/collection.pdf"),
             "Could not retain collection PDF");
  execute ("qpdf", {"--check", pdf});
  const auto fonts= execute ("pdffonts", {pdf});
  require (fonts.contains ("ATHENAFixtureOne") && fonts.contains ("ATHENAFixtureTwo") &&
           fonts.contains ("Type 3"), "PDF lost native collection or variant bitmap fonts");
  const auto text= execute ("pdftotext", {"-raw", "-nopgbrk", "-enc", "UTF-8", pdf, "-"});
  auto lines= text.split ('\n');
  if (!lines.isEmpty () && lines.back ().isEmpty ()) lines.removeLast ();
  if (lines != QList<QByteArray> {"A \xce\xb1", "A \xce\xb1", "A \xce\xb1", "A \xce\xb1",
                                 "A \xce\xb1", "A \xce\xb1\xce\xb2 A"}) {
    std::cerr << "Collection PDF text: " << text.constData () << '\n';
    throw std::runtime_error ("Collection PDF lost Unicode mappings");
  }
  check_text_geometry (pdf, 6);
}

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
    athena::text::unicode_paragraph paragraph (source);
    const auto shaped= athena::text::shape_line (paragraph, 0, source.size (),
      [&] (std::string_view source, const athena::text::shaping_item& item,
            const athena::text::shaping_options& options) {
      auto run= fn->shape_utf8 (source, item.run.begin, item.run.end, options);
      require (!run.missing_glyphs, "PDF fixture font lacks a required glyph");
      if (bitmap)
        run.glyph_source = tm_new<bitmap_only_glyphs> (run.glyph_source);
      return run;
    });
    shaped.draw_fixed (pdf, source, 400 * PIXEL, -(600 + row * 300) * PIXEL);
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
    box excerpt = utf8_text_box (::path (0), string (context.data (), context.size ()),
                                 7, 14, text, pencil (black));
    const SI old_x= pdf->ox, old_y= pdf->oy;
    pdf->move_origin (400 * PIXEL, -2700 * PIXEL);
    excerpt->display (pdf);
    pdf->set_origin (old_x, old_y);
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
    check_collection_export (output.filePath ("collection.pdf"));
    for (const bool postscript : {false, true}) {
      const QString stem = postscript ? "shaped-ps" : "shaped";
      const QString pdf = output.filePath (stem + ".pdf");
      if (postscript) {
        const QString ps = output.filePath (stem + ".ps");
        render_document (ps, true);
        if (const char *directory = std::getenv ("ATHENA_SHAPED_PDF_TEST_OUTPUT"))
          require (QFile::copy (ps, QString::fromUtf8 (directory) + "/" + stem + ".ps"),
                   "Could not retain PostScript source");
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
      check_text_geometry (pdf);
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
