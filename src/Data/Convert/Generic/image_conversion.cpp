/******************************************************************************
* MODULE     : image_conversion.cpp
* DESCRIPTION: Declarative image routes and native argv-based tool execution
* COPYRIGHT  : (C) 2026 ATHENA authors
* GNU GPL version 3 or later; see LICENSE.
******************************************************************************/
#include "image_conversion.hpp"
#include "conversion_registry.hpp"
#include "conversion_graph.hpp"
#include "scheme.hpp"
#include "file.hpp"
#include "analyze.hpp"
#include "Qt/qt_utilities.hpp"
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <nlohmann/json.hpp>
#include <map>
#include <stdexcept>

namespace {
using json= nlohmann::json;
std::string bytes (string s) { return {s.data (), std::size_t (N(s))}; }
string text (const std::string& s) { return string (s.data (), s.size ()); }
QString qtext (string s) { return QString::fromUtf8 (s.data (), N(s)); }
string native_text (const QString& s) { auto b= s.toUtf8 (); return string (b.data (), b.size ()); }

struct ImageRoute {
  QString program;
  QString output;
  QStringList arguments;
  bool native_pdf= false;
};
using RouteKey= std::pair<std::string, std::string>;

struct ImageRegistry {
  std::map<RouteKey, ImageRoute> routes;
  ImageRegistry () {
    string source;
    if (load_string (url ("$ATHENA_PATH/misc/convert/image-tools.json"), source, false))
      throw std::runtime_error ("Cannot load image conversion registry");
    auto data= json::parse (source.data (), source.data () + N(source));
    if (data.at ("version").get<int> () != 1)
      throw std::runtime_error ("Unsupported image conversion registry version");
    std::map<std::string, QString> tools;
    for (const auto& tool: data.at ("tools").items ()) {
      for (const auto& candidate: tool.value ()) {
#ifdef _WIN32
        if (candidate.value ("platform", "") == "unix") continue;
#endif
        auto path= QStandardPaths::findExecutable (
          QString::fromStdString (candidate.at ("program").get<std::string> ()));
        if (!path.isEmpty ()) { tools.emplace (tool.key (), path); break; }
      }
    }
    for (const auto& entry: data.at ("routes")) {
      ImageRoute route;
      route.output= QString::fromStdString (entry.at ("output").get<std::string> ());
      if (entry.contains ("native")) {
        if (entry.at ("native") != "image-pdf")
          throw std::runtime_error ("Unknown native image converter");
        route.native_pdf= true;
      }
      else {
        auto tool= tools.find (entry.at ("tool").get<std::string> ());
        if (tool == tools.end ()) continue;
        route.program= tool->second;
        for (const auto& arg: entry.at ("args"))
          route.arguments << QString::fromStdString (arg.get<std::string> ());
      }
      // JSON order defines priority when several installed tools serve a route.
      routes.emplace (RouteKey {entry.at ("from").get<std::string> (),
                                entry.at ("to").get<std::string> ()}, std::move (route));
    }
  }
};
const ImageRegistry& registry () { static const ImageRegistry data; return data; }

void publish (const QString& from, const QString& to) {
  QFile input (from);
  if (!input.open (QIODevice::ReadOnly))
    throw std::runtime_error ("Image converter produced no readable output: " + from.toStdString ());
  QSaveFile output (to);
  if (!output.open (QIODevice::WriteOnly))
    throw std::runtime_error ("Cannot write converted image: " + output.errorString ().toStdString ());
  while (!input.atEnd ()) {
    auto block= input.read (1024 * 1024);
    if (input.error () != QFileDevice::NoError || output.write (block) != block.size ())
      throw std::runtime_error ("Cannot copy converted image to destination");
  }
  if (!output.commit ())
    throw std::runtime_error ("Cannot publish converted image: " + output.errorString ().toStdString ());
}

} // namespace

bool conversion_image_file (url source, url destination) {
  string from= conversion_format_from_suffix (suffix (source));
  string to= conversion_format_from_suffix (suffix (destination));
  auto path= conversion_graph_path (bytes (from * "-file"), bytes (to * "-file"));
  if (!path) return false;
  for (const auto& step: *path) if (step.executor != "convert-image") return false;
  struct temporaries {
    std::vector<url> values;
    ~temporaries () { for (const auto& value: values) remove (value); }
  } cleanup;
  url current= source;
  string resolution= get_preference ("texmacs->image:raster-resolution");
  for (std::size_t i=0; i<path->size (); ++i) {
    const auto& step= (*path)[i];
    current= conversion_image_run (text (step.from.substr (0, step.from.size ()-5)),
      text (step.to.substr (0, step.to.size ()-5)), current,
      i+1 == path->size () ? destination : url_none (), resolution);
    if (i+1 != path->size ()) cleanup.values.push_back (current);
  }
  if (path->empty () && source != destination) copy (source, destination);
  return true;
}

tree conversion_image_routes () {
  tree result (TUPLE);
  for (const auto& entry: registry ().routes)
    result << tree (TUPLE, scm_quote (text (entry.first.first)),
                           scm_quote (text (entry.first.second)));
  return result;
}

url conversion_image_run (string from, string to, url source, url destination,
                          string resolution) {
  auto found= registry ().routes.find ({bytes (from), bytes (to)});
  if (found == registry ().routes.end ())
    throw std::runtime_error ("Image conversion unavailable: " + bytes (from) + " -> " + bytes (to));
  const auto& route= found->second;
  bool valid_dpi= false;
  int dpi= qtext (resolution).toInt (&valid_dpi);
  if (!valid_dpi || dpi <= 0)
    throw std::runtime_error ("Image conversion resolution must be a positive integer");
  QTemporaryDir temporary;
  if (!temporary.isValid ())
    throw std::runtime_error ("Cannot create image conversion temporary directory");
  const QString output= temporary.filePath (route.output);
  const QString input= QFileInfo (qtext (concretize (source))).absoluteFilePath ();
  if (route.native_pdf)
    qt_image_to_pdf (source, url_system (native_text (output)), 0, 0, dpi);
  else {
    QStringList args;
    for (auto arg: route.arguments) {
      if (arg == "{source}") arg= input;
      else if (arg == "{output}") arg= output;
      else if (arg == "{prefix}") arg= temporary.filePath ("output");
      else if (arg == "{dpi}") arg= QString::number (dpi);
      args << arg;
    }
    QProcess process;
    process.start (route.program, args);
    if (!process.waitForStarted ())
      throw std::runtime_error ("Cannot start image converter: " + process.errorString ().toStdString ());
    process.waitForFinished (-1);
    if (process.exitStatus () != QProcess::NormalExit || process.exitCode () != 0)
      throw std::runtime_error ("Image converter failed: " + route.program.toStdString () +
                                ": " + process.readAllStandardError ().toStdString ());
  }
  if (is_none (destination))
    destination= url_temp ("." * conversion_format_default_suffix (to));
  publish (output, qtext (concretize (destination)));
  return destination;
}
