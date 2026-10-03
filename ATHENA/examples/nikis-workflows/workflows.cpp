/******************************************************************************
* MODULE     : workflows.cpp
* DESCRIPTION: Course discovery, Roman sequences and source-local section editing
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "workflows.hpp"
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <thread>

namespace nikis {
namespace {
namespace fs= std::filesystem;
using athena::audmap::id;
using athena::audmap::pending_request;
QString qs (const std::string& s) { return QString::fromUtf8 (s.data (), int (s.size ())); }
std::string ss (const QString& s) { return s.toUtf8 ().toStdString (); }
value atom (const std::string& text) { return {{"text", text}}; }
value node (const char* tag, value children) { return {{"tag", tag}, {"children", std::move (children)}}; }
value await (pending_request request) {
  if (request.result.wait_for (std::chrono::minutes (5)) != std::future_status::ready)
    throw std::runtime_error ("AUDMAP request timed out; its outcome is unknown");
  const auto result= request.result.get ();
  if (result.status != "OK") throw command_error (result.status + ": " + result.data.dump ());
  return result.data;
}
value operate (client& c, id ticket, id handle, const std::string& command,
               const value& parameters= value::object ()) {
  const auto request= c.operate (ticket, handle, command, parameters);
  try {
    auto result= await (request);
    await (c.release (ticket, request.operation));
    return result;
  }
  catch (const command_error&) {
    await (c.release (ticket, request.operation));
    throw;
  }
}
struct selection {
  client& connection;
  id ticket;
  std::vector<id> handles;
  selection (client& c, const std::string& expression): connection (c) {
    const auto request= c.resolve (expression, true);
    ticket= request.ticket;
    try {
      const auto result= await (request);
      if (!result.at (1).empty ()) throw command_error ("Incomplete resolution: " + result.at (1).dump ());
      handles= result.at (0).get<std::vector<id>> ();
    }
    catch (...) { c.finish (ticket); throw; }
  }
  ~selection () { try { connection.finish (ticket); } catch (...) {} }
  selection (const selection&)= delete;
  id one () const {
    if (handles.size () != 1)
      throw command_error ("Expected one resource, found " + std::to_string (handles.size ()));
    return handles.front ();
  }
  value call (const std::string& command, const value& parameters= value::object ()) {
    return operate (connection, ticket, one (), command, parameters);
  }
};
std::string quoted (const std::string& s) { return value (s).dump (); }
std::string vault_selector (const value& config) { return "@/vaults/" + quoted (config.at ("vault")); }
fs::path vault_relative (fs::path path, const fs::path& root) {
  if (path.is_absolute ()) path= path.lexically_relative (root);
  if (path.empty ()) throw command_error ("Resource has no vault-relative path");
  for (const auto& part: path)
    if (part == "..") throw command_error ("Resource is outside the selected vault");
  return path.lexically_normal ();
}
std::string file_selector (const std::string& vault, const fs::path& path) {
  std::string result= vault + "/filesystem";
  for (const auto& part: path) if (part != ".") result+= "/" + quoted (part.string ());
  return result;
}
std::string roman (int number) {
  if (number < 1 || number > 3999) throw command_error ("Roman sequence is outside I through MMMCMXCIX");
  static const std::pair<int,const char*> digits[]= {
    {1000,"M"}, {900,"CM"}, {500,"D"}, {400,"CD"}, {100,"C"}, {90,"XC"},
    {50,"L"}, {40,"XL"}, {10,"X"}, {9,"IX"}, {5,"V"}, {4,"IV"}, {1,"I"}};
  std::string result;
  for (const auto& digit: digits) while (number >= digit.first) {
    result+= digit.second; number-= digit.first;
  }
  return result;
}
int roman_number (const QString& text) {
  static const std::map<QChar,int> digits {{'I',1},{'V',5},{'X',10},{'L',50},{'C',100},{'D',500},{'M',1000}};
  int result= 0, last= 0;
  for (auto i= text.crbegin (); i != text.crend (); ++i) {
    const auto found= digits.find (*i);
    if (found == digits.end ()) return 0;
    result+= found->second < last ? -found->second : found->second;
    last= found->second;
  }
  return result >= 1 && result <= 3999 && roman (result) == ss (text) ? result : 0;
}
QString year_in (const std::string& text) {
  static const QRegularExpression pattern ("(?<![0-9])([0-9]{4}-[0-9]{4})(?![0-9])");
  return pattern.match (qs (text)).captured (1);
}
struct series_pattern {
  QString pattern;
  QRegularExpression regex;
  explicit series_pattern (const std::string& input): pattern (qs (input)) {
    if (pattern.count ("%R") != 1 || pattern.count ("%s") > 1)
      throw command_error ("Expected one Roman sequence in namespace template: " + input);
    QString expression;
    for (int i= 0; i < pattern.size (); ++i) {
      if (pattern[i] == '%' && i + 1 < pattern.size ()) {
        const auto type= pattern[++i];
        if (type == 'R') expression+= "(?<roman>[IVXLCDM]+)";
        else if (type == 's') expression+= "(?<year>[0-9]{4}-[0-9]{4})";
        else throw command_error ("Unsupported course namespace placeholder");
      }
      else expression+= QRegularExpression::escape (QString (pattern[i]));
    }
    regex= QRegularExpression ("^" + expression + "$");
  }
  std::string filename (int number, const QString& year) const {
    QString result= pattern;
    if (result.contains ("%s") && year.isEmpty ()) throw command_error ("Cannot determine the course academic year");
    result.replace ("%s", year).replace ("%R", qs (roman (number)));
    return ss (result) + ".ath";
  }
};
QString plain (const value& tree) {
  if (tree.contains ("text")) return qs (tree.at ("text").get<std::string> ());
  QString result;
  if (tree.contains ("children")) for (const auto& child: tree.at ("children")) result+= plain (child);
  return result;
}
struct section_numbers { int maximum= 0, current= 0, subsection= 0; };
void headings (const value& tree, section_numbers& numbers, std::vector<int>& path,
               const std::vector<int>* cursor= nullptr) {
  if (cursor && !(path < *cursor)) return;
  const auto tag= tree.value ("tag", "");
  const bool section= tag == "section" || tag == "section*";
  const bool subsection= tag == "subsection" || tag == "subsection*";
  if (section || subsection) {
    static const QRegularExpression sec (QStringLiteral ("^\\s*\u00a7\\s*([0-9]+)(?![0-9.])"));
    static const QRegularExpression sub (QStringLiteral ("^\\s*\u00a7\\s*([0-9]+)\\.([0-9]+)(?![0-9.])"));
    const auto match= (section ? sec : sub).match (plain (tree));
    if (section) {
      numbers.current= match.hasMatch () ? match.captured (1).toInt () : 0;
      numbers.maximum= std::max (numbers.maximum, numbers.current);
      numbers.subsection= 0;
    }
    else if (match.hasMatch () && match.captured (1).toInt () == numbers.current)
      numbers.subsection= std::max (numbers.subsection, match.captured (2).toInt ());
    return;
  }
  if (tag == "macro" || tag == "xmacro" || tag == "assign" || tag == "doc-data" ||
      tag == "transclude" || tag == "inactive" || tag == "raw-data") return;
  if (tree.contains ("children")) {
    const auto& children= tree.at ("children");
    for (std::size_t i= 0; i < children.size (); ++i) {
      path.push_back (int (i)); headings (children[i], numbers, path, cursor); path.pop_back ();
    }
  }
}
value field (const value& document, const std::string& name) {
  for (const auto& child: document.at ("children"))
    if (child.value ("tag", "") == name && child.at ("children").size () == 1)
      return child.at ("children").at (0);
  throw command_error ("Document is missing " + name);
}
std::string section_title (int number, int sub= -1) {
  if (number <= 0 || number >= 1000000 || sub >= 1000000)
    throw command_error ("Section number is outside the supported range");
  return ss (QStringLiteral ("\u00a7")) + std::to_string (number) +
    (sub < 0 ? "" : "." + std::to_string (sub));
}
value new_document (const value& settings, const std::string& filename, int section) {
  value paragraphs= value::array ({node ("doc-data", value::array ({
    node ("doc-title", value::array ({atom (fs::path (filename).stem ().string ())}))}))});
  if (section) paragraphs.push_back (node ("section", value::array ({atom (section_title (section))})));
  paragraphs.push_back (atom (""));
  return node ("document", value::array ({
    node ("style", value::array ({node ("tuple", value::array ({atom (settings.at ("style"))}))})),
    node ("body", value::array ({node ("document", std::move (paragraphs))})),
    node ("initial", value::array ({node ("collection", value::array ({
      node ("associate", value::array ({atom ("font"), atom (settings.at ("font"))})),
      node ("associate", value::array ({atom ("page-medium"), atom (settings.at ("page_medium"))}))
    }))}))}));
}
value new_heading (client& c, const value& settings, bool subsection) {
  selection buffer (c, "@/buffers/@");
  const auto context= buffer.call ("context");
  const auto properties= buffer.call ("get");
  selection vault (c, vault_selector (settings));
  const fs::path root= vault.call ("get").at ("root").get<std::string> ();
  const auto name= properties.at ("url").get<std::string> ();
  const auto url= QUrl (qs (name));
  const std::string filename= url.isLocalFile () ? ss (url.toLocalFile ()) : name;
  if (!fs::path (filename).is_absolute ()) throw command_error ("Select a source document in nIKIS first");
  vault_relative (filename, root);
  if (context.at ("selection").get<bool> ()) throw command_error ("Clear the selection before inserting a heading");
  const auto cursor= context.at ("cursor").get<std::vector<int>> ();
  section_numbers numbers;
  std::vector<int> path;
  headings (context.at ("body"), numbers, path, &cursor);
  if (subsection && !numbers.current) throw command_error ("No preceding numbered section (for example, section 3) was found");
  const auto text= subsection ? section_title (numbers.current, numbers.subsection + 1)
                             : section_title (numbers.maximum + 1);
  if (context.at ("paragraph_start").is_null ())
    throw command_error ("Place the cursor in a document paragraph first");
  value paragraphs= value::array ();
  // Native multiline insertion joins its first paragraph to the text before
  // the cursor. Keep that text separate from the new block heading.
  if (context.at ("paragraph_start") != context.at ("cursor")) paragraphs.push_back (atom (""));
  paragraphs.push_back (node (subsection ? "subsection" : "section", value::array ({atom (text)})));
  paragraphs.push_back (atom (""));
  return buffer.call ("insert_at_cursor", {{"epoch", context.at ("epoch")},
    {"cursor", context.at ("cursor")}, {"view", context.at ("view")},
    {"tree", node ("document", std::move (paragraphs))}});
}

value new_course_document (client& c, const value& settings, bool assignment, course_picker picker) {
  const auto vault_path= vault_selector (settings);
  selection vault (c, vault_path);
  const fs::path root= vault.call ("get").at ("root").get<std::string> ();
  selection namespaces (c, vault_path + "/namespaces/?($type = \"namespace\")");
  std::map<std::string, value> records;
  std::map<std::string, id> handles;
  // Pipeline independent reads on the SDK's existing I/O thread; do not turn
  // every namespace into a serial socket round trip or a new native thread.
  for (std::size_t first= 0; first < namespaces.handles.size (); first+= 32) {
    std::vector<pending_request> reads, releases;
    const auto last= std::min (first + 32, namespaces.handles.size ());
    for (auto i= first; i < last; ++i)
      reads.push_back (c.operate (namespaces.ticket, namespaces.handles[i], "get"));
    for (std::size_t i= 0; i < reads.size (); ++i) {
      auto ns= await (reads[i]);
      releases.push_back (c.release (namespaces.ticket, reads[i].operation));
      const auto name= ns.at ("name").get<std::string> ();
      handles[name]= namespaces.handles[first + i]; records.emplace (name, std::move (ns));
    }
    for (auto& release: releases) await (release);
  }
  const auto prefix= settings.at ("course_prefix").get<std::string> ();
  const auto suffix= settings.at (assignment ? "assignment_suffix" : "lecture_suffix").get<std::string> ();
  std::vector<std::string> courses;
  QStringList labels;
  std::map<std::string, std::set<QString>> semester_years;
  for (const auto& [name, ns]: records) {
    if (name.compare (0, prefix.size (), prefix) != 0) continue;
    const auto year= year_in (ns.value ("homepage_path", ""));
    if (!year.isEmpty ()) for (const auto& parent: ns.at ("parents"))
      semester_years[parent.get<std::string> ()].insert (year);
    if (!records.count (name + suffix)) continue;
    courses.push_back (name);
    labels << qs (name.substr (prefix.size ())) + (year.isEmpty () ? "" : "  (" + year + ")");
  }
  if (courses.empty ()) throw command_error ("No courses have the requested child namespace");
  const int choice= picker (labels, assignment ? "New assignment for course" : "New note for course");
  if (choice < 0) return {{"cancelled", true}};
  const auto& course= courses.at (choice);
  const auto& ns= records.at (course);
  const auto& series= records.at (course + suffix);
  const auto members= operate (c, namespaces.ticket, handles.at (course + suffix), "members");
  series_pattern pattern (series.at ("template"));
  fs::path directory;
  const auto homepage= ns.value ("homepage_path", "");
  QString year= year_in (homepage);
  if (!homepage.empty ()) directory= vault_relative (homepage, root).parent_path ();
  if (year.isEmpty ()) {
    std::set<QString> candidates;
    for (const auto& parent: ns.at ("parents")) {
      const auto& years= semester_years[parent.get<std::string> ()];
      // Courses is a cross-year umbrella, not an academic-year witness.
      if (years.size () == 1) candidates.insert (*years.begin ());
    }
    if (candidates.size () == 1) year= *candidates.begin ();
  }
  if (directory.empty ()) {
    std::set<fs::path> candidates;
    for (const auto& member: members) candidates.insert (vault_relative (member.at ("path"), root).parent_path ());
    if (candidates.empty ()) {
      const auto all= operate (c, namespaces.ticket, handles.at (course), "members");
      for (const auto& member: all) candidates.insert (vault_relative (member.at ("path"), root).parent_path ());
    }
    if (candidates.size () != 1) throw command_error ("Course directory is missing or ambiguous: " + course);
    directory= *candidates.begin ();
  }
  int maximum= 0;
  fs::path previous;
  std::set<int> seen;
  for (const auto& member: members) {
    const fs::path path= vault_relative (member.at ("path"), root);
    if (path.parent_path () != directory) continue;
    const auto match= pattern.regex.match (qs (path.stem ().string ()));
    if (!match.hasMatch ()) continue;
    if (pattern.pattern.contains ("%s")) {
      if (year.isEmpty ()) throw command_error ("Cannot determine the course academic year: " + course);
      if (match.captured ("year") != year) continue;
    }
    const int number= roman_number (match.captured ("roman"));
    if (!number) continue;
    if (member.value ("ambiguous", false) || !seen.insert (number).second)
      throw command_error ("Ambiguous course sequence: " + path.string ());
    if (number > maximum) { maximum= number; previous= path; }
  }
  int section= assignment ? 0 : 1;
  if (!assignment && !previous.empty ()) {
    selection document (c, file_selector (vault_path, previous) + "/online");
    const auto source= document.call ("get").at ("tree");
    section_numbers numbers;
    std::vector<int> path;
    headings (field (source, "body"), numbers, path);
    section= numbers.maximum + 1;
  }
  const auto filename= pattern.filename (maximum + 1, year);
  // Revalidate the original vault before resolving the mutation target.
  vault.call ("get");
  selection parent (c, file_selector (vault_path, directory));
  const auto created= parent.call ("create_document", {{"name", filename},
    {"document", new_document (settings, filename, section)}});
  try {
    selection file (c, file_selector (vault_path, directory / filename));
    file.call ("open");
    value buffers;
    for (int attempt= 0; attempt < 50; ++attempt) {
      buffers= file.call ("buffers");
      if (!buffers.empty ()) break;
      std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
    if (buffers.empty ()) throw command_error ("The new document has not opened yet");
    selection buffer (c, "@/buffers/[" + std::to_string (buffers.at (0).get<id> ()) + "]");
    const auto context= buffer.call ("context");
    const auto& body= context.at ("body");
    buffer.call ("set_cursor", {{"epoch", context.at ("epoch")}, {"view", context.at ("view")},
      {"cursor", context.at ("cursor")}, {"path", value::array ({body.at ("children").size () - 1, 0})}});
  }
  catch (const command_error& error) {
    throw command_error ("Created " + (directory / filename).string () +
      ". Opening/positioning did not complete: " + error.what () + ". Do not create it again.");
  }
  return created;
}
}

value run (client& c, const value& settings, const std::string& command, course_picker picker) {
  if (command == "new-note") return new_course_document (c, settings, false, picker);
  if (command == "new-assignment") return new_course_document (c, settings, true, picker);
  if (command == "new-section") return new_heading (c, settings, false);
  if (command == "new-subsection") return new_heading (c, settings, true);
  throw command_error ("Unknown command: " + command);
}

void serve (client& c, const value& settings, course_picker picker,
            std::function<void(const QString&)> report_error) {
  const char* subscription= std::getenv ("ATHENA_SUBSCRIPTION_GUID");
  if (!subscription || !*subscription) throw std::runtime_error ("Start this plugin from ATHENA's Plugins menu");
  // Rotate subscriptions/connections to bound retained protocol history.
  for (int round= 0; round < 32; ++round) {
    selection mailbox (c, std::string ("@/subscription/") + subscription);
    for (int poll= 0; poll < 512; ++poll) {
      const auto batch= mailbox.call ("get", {{"limit", 1}});
      for (const auto& message: batch.at ("commands")) {
        std::string status= "OK";
        value result;
        try { result= run (c, settings, message.at ("command"), picker); }
        catch (const command_error& error) {
          status= "ERROR"; result= error.what ();
          std::cerr << "nIKIS Workflows: " << error.what () << std::endl;
          report_error (QString::fromUtf8 (error.what ()));
        }
        mailbox.call ("reply", {{"id", message.at ("id")}, {"status", status}, {"result", result}});
        std::cout << message.at ("command").get<std::string> () << ": " << status << ": " << result.dump () << std::endl;
      }
      std::this_thread::sleep_for (std::chrono::milliseconds (200));
    }
  }
}
}
