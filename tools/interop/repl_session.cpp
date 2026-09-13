/******************************************************************************
* MODULE     : repl_session.cpp
* DESCRIPTION: Selector and operation REPL modes with ticket and handle navigation
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "repl_session.hpp"
#include <charconv>
#include <iomanip>
#include <set>
#include <sstream>

namespace athena::interop {
namespace {
value frame (opcode op, value args) {
  value message = value::array ({static_cast<unsigned> (op)});
  for (auto& arg: args) message.push_back (std::move (arg));
  return message;
}
std::string trim (const std::string& s) {
  const auto first = s.find_first_not_of (" \t\r\n");
  if (first == std::string::npos) return {};
  return s.substr (first, s.find_last_not_of (" \t\r\n") - first + 1);
}
} // namespace

void repl_session::reset () {
  ticket = 0; operation = 0; selected = 0; resolving = false; handles.clear ();
}
void repl_session::close () {
  if (ticket) send (frame (resolving ? opcode::cnl : opcode::fin, {ticket}));
  if (busy ()) progress (resolving ? repl_progress::cancelled : repl_progress::detached);
  reset (); finished = true;
}
std::string repl_session::prompt () const {
  if (busy () || finished) return {};
  if (!ticket) return "audm> ";
  return "handle " + std::to_string (selected) + "> ";
}
void repl_session::show_handles () const {
  std::size_t width = 6;
  for (const auto& h: handles) width = std::max (width, std::to_string (h.first).size ());
  std::ostringstream text;
  text << "Resolution " << ticket << "\n"
       << std::setw (width) << "Handle" << "  " << std::setw (width) << "Parent" << "  Target\n"
       << std::string (width, '-') << "  " << std::string (width, '-') << "  --------\n";
  for (const auto& h: handles)
    text << std::setw (width) << h.first << "  "
         << std::setw (width) << (h.second ? std::to_string (h.second) : "-") << "  "
         << (h.first == selected ? "selected" : "") << '\n';
  text << "Parent '-' means no parent. Operations target the selected handle.\n";
  print (text.str ());
}
void repl_session::line (const std::string& input) {
  const auto text = trim (input);
  if (text.empty () || finished) return;
  if (text == "exit") {
    if (!ticket) { finished = true; return; }
    send (frame (resolving ? opcode::cnl : opcode::fin, {ticket}));
    if (busy ()) progress (resolving ? repl_progress::cancelled : repl_progress::detached);
    reset (); return;
  }
  if (text == "help") {
    if (ticket) print (
      "Operations target handle " + std::to_string (selected) + " in resolution " + std::to_string (ticket) + ".\n"
      "A handle identifies a node in this resolution; it is not a persistent resource ID.\n\n"
      "  handles       List handles, their parents and the selected target.\n"
      "  use HANDLE    Select a target from that list, e.g. use " + std::to_string (selected) + ".\n"
      "  lineage       Show the root-to-target chain of handles.\n"
      "  inspect       Ask the target for its supported operations and parameters.\n"
      "  get           Read the target's properties.\n"
      "  COMMAND JSON  Run an operation returned by inspect. JSON must be an object.\n"
      "                Omit JSON for no parameters: get is equivalent to get {}.\n"
      "  exit          Close this resolution and return to selector input.\n\n"
      "Ctrl+C while waiting leaves the resolution; an admitted operation may still finish.");
    else print (
      "Enter an AUDM selector to resolve resources. Examples:\n"
      "  @                          ATHENA root\n"
      "  @/vaults/@                 Default vault\n"
      "  @/vaults/@/namespaces/@     Default namespace in the default vault\n\n"
      "A successful match opens operation mode for a selected handle.\n"
      "  help    Show this help.\n"
      "  exit    Disconnect and quit.\n"
      "Ctrl+C cancels a pending resolution.");
    return;
  }
  if (busy ()) { print ("Waiting for ATHENA; exit leaves this ticket."); return; }
  try {
    if (!ticket) {
      parse_selection (text);
      const auto id = ++next_ticket;
      send (frame (opcode::req, {id, text, value::array ({0})}));
      ticket = id; resolving = true;
      progress (repl_progress::resolving);
      return;
    }
    if (text == "handles") { show_handles (); return; }
    const auto split = text.find_first_of (" \t");
    const auto command = text.substr (0, split);
    const auto rest = split == std::string::npos ? std::string () : trim (text.substr (split));
    if (command == "use") {
      handle id = 0;
      const auto parsed = std::from_chars (rest.data (), rest.data () + rest.size (), id);
      if (parsed.ec != std::errc () || parsed.ptr != rest.data () + rest.size () || !handles.count (id))
        throw std::invalid_argument ("use requires a handle from this ticket");
      selected = id; return;
    }
    const auto id = ++next_operation;
    if (command == "lineage") {
      if (!rest.empty ()) throw std::invalid_argument ("lineage takes no arguments; use HANDLE to select a target");
      send (frame (opcode::lin, {ticket, id, selected}));
    }
    else {
      auto parameters = rest.empty () ? value::object () : value::parse (rest);
      if (!parameters.is_object ()) throw std::invalid_argument ("Operation parameters must be a JSON object");
      send (frame (opcode::opr, {ticket, id, selected, command, std::move (parameters)}));
    }
    operation = id;
    progress (repl_progress::running);
  }
  catch (const std::exception& e) { print (std::string ("Error: ") + e.what ()); }
}
void repl_session::receive (const value& msg) {
  if (!ticket || msg.size () < 2 || msg[1] != ticket) return;
  const auto op = msg[0].get<unsigned> ();
  if (op == static_cast<unsigned> (opcode::acx) && resolving) {
    progress (repl_progress::done);
    resolving = false;
    std::set<handle> parents;
    for (const auto& n: msg.at (2).at (0)) {
      const auto id = n.at (0).get<handle> (), parent = n.at (1).get<handle> ();
      handles.emplace (id, parent); parents.insert (parent);
    }
    if (!msg[2][1].empty ()) print ("Truncated: " + msg[2][1].dump ());
    if (handles.empty ()) {
      print ("No matches."); send (frame (opcode::fin, {ticket})); reset (); return;
    }
    for (const auto& h: handles) if (!parents.count (h.first)) { selected = h.first; break; }
    show_handles ();
  }
  else if (op == static_cast<unsigned> (opcode::rsp) && msg.at (2) == operation) {
    progress (msg.at (3) == "OK" ? repl_progress::done : repl_progress::failed);
    print (msg.at (3).get<std::string> () + "\n" + msg.at (4).dump (2));
    send (frame (opcode::rel, {ticket, operation})); operation = 0;
  }
  else if (op == static_cast<unsigned> (opcode::err)) {
    if (busy () && (msg.size () == 3 || msg.at (2) == operation)) progress (repl_progress::failed);
    print ("Error: " + msg.dump ());
    if (msg.size () == 3) reset ();
    else if (msg.at (2) == operation) operation = 0;
  }
}
} // namespace athena::interop
