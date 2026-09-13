/******************************************************************************
* MODULE     : selection.cpp
* DESCRIPTION: AUDM selector parsing, traversal bounds and predicate evaluation
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "selection.hpp"

#include <tao/pegtl.hpp>
#include <tao/pegtl/contrib/json.hpp>
#include <tao/pegtl/contrib/parse_tree.hpp>
#include <charconv>
#include <fnmatch.h>
#include <stdexcept>

namespace athena::interop {
namespace grammar {
namespace p = tao::pegtl;
template<typename R> using token = p::pad<R, p::ascii::space>;
struct identifier : p::seq<p::sor<p::ascii::alpha, p::one<'_'>>,
  p::star<p::sor<p::ascii::alnum, p::one<'_', '-'>>>> {};
struct property : p::seq<p::one<'$'>, identifier> {};
struct literal : p::sor<p::json::string, p::json::number,
  TAO_PEGTL_STRING("true"), TAO_PEGTL_STRING("false"),
  TAO_PEGTL_STRING("null")> {};
struct comparator : p::sor<TAO_PEGTL_STRING("!="),
  TAO_PEGTL_STRING("<="), TAO_PEGTL_STRING(">="), p::one<'=', '<', '>'>,
  TAO_PEGTL_STRING("contains"), TAO_PEGTL_STRING("starts_with"),
  TAO_PEGTL_STRING("ends_with")> {};
struct comparison : p::seq<token<property>, token<comparator>, token<literal>> {};
struct exists : p::seq<TAO_PEGTL_STRING("exists"), token<p::one<'('>>,
  token<property>, p::one<')'>> {};
struct expression;
struct negation;
struct atom : p::sor<exists, comparison,
  p::seq<token<p::one<'('>>, expression, token<p::one<')'>>>> {};
struct negation : p::sor<p::seq<token<p::sor<TAO_PEGTL_STRING("NOT"),
  p::one<'!'>>>, negation>, atom> {};
struct conjunction : p::list<negation, token<p::sor<TAO_PEGTL_STRING("AND"),
  TAO_PEGTL_STRING("&&"), p::one<','>>>> {};
struct expression : p::list<conjunction, token<p::sor<TAO_PEGTL_STRING("OR"),
  TAO_PEGTL_STRING("||")>>> {};
struct limit_name : p::sor<TAO_PEGTL_STRING("$max_depth"),
  TAO_PEGTL_STRING("$max_matches"), TAO_PEGTL_STRING("$max_duration")> {};
struct limit : p::seq<token<limit_name>, token<p::one<'='>>, token<literal>> {};
struct limits : p::seq<token<p::one<'|'>>, p::list<limit, token<p::one<','>>>> {};
struct filter : p::seq<token<p::one<'('>>, expression,
  p::opt<limits>, token<p::one<')'>>> {};
struct recursive : p::seq<TAO_PEGTL_STRING("???"), filter> {};
struct scoped : p::seq<TAO_PEGTL_STRING("??"), filter> {};
struct local : p::seq<p::one<'?'>, filter> {};
struct default_resource : p::one<'@'> {};
struct quoted_name : p::json::string {};
struct bare_name : p::plus<p::not_one<'/', '@', '?', '(', ')', '"', '\n', '\r'>> {};
struct part : p::sor<recursive, scoped, local, default_resource, quoted_name,
  bare_name> {};
struct document : p::must<p::list<token<part>, p::one<'/'>>, p::eof> {};
template<typename R> using nodes = p::parse_tree::selector<R,
  p::parse_tree::store_content::on<recursive, scoped, local, default_resource,
    quoted_name, bare_name, expression, conjunction, negation, comparison,
    exists, property, literal, comparator, limit>>;
} // namespace grammar

namespace {
using node = tao::pegtl::parse_tree::node;

std::string glob_bytes (const std::string& text, bool pattern) {
  static constexpr char hex[] = "0123456789abcdef";
  std::string encoded;
  encoded.reserve (text.size () * 3);
  for (std::size_t i = 0; i < text.size (); ++i) {
    unsigned char c = text[i];
    if (pattern && c == '*') { encoded += '*'; continue; }
    if (pattern && c == '\\' && i + 1 < text.size () &&
        (text[i + 1] == '*' || text[i + 1] == '\\')) c = text[++i];
    encoded += 'x';
    encoded += hex[c >> 4];
    encoded += hex[c & 15];
  }
  return encoded;
}

bool string_matches (const std::string& actual, const std::string& pattern,
                     const std::string& operation) {
  if (pattern.find_first_of ("*\\") == std::string::npos) {
    if (operation == "contains") return actual.find (pattern) != std::string::npos;
    if (operation == "starts_with") return actual.compare (0, pattern.size (), pattern) == 0;
    if (operation == "ends_with") return actual.size () >= pattern.size () &&
      actual.compare (actual.size () - pattern.size (), pattern.size (), pattern) == 0;
    return actual == pattern;
  }
  // ASCII byte tokens preserve UTF-8 and embedded NULs while letting fnmatch
  // implement only our '*' operator, without its '?', bracket or locale rules.
  std::string glob = glob_bytes (pattern, true);
  if (operation == "contains" || operation == "ends_with") glob.insert (0, "*");
  if (operation == "contains" || operation == "starts_with") glob += '*';
  return ::fnmatch (glob.c_str (), glob_bytes (actual, false).c_str (), 0) == 0;
}

predicate read_predicate (const node& n) {
  using namespace grammar;
  predicate out;
  if (n.is_type<comparison> ()) {
    out.type = predicate::kind::comparison;
    out.property = n.children.at (0)->string ().substr (1);
    out.operation = n.children.at (1)->string ();
    out.literal = value::parse (n.children.at (2)->string ());
  }
  else if (n.is_type<exists> ()) {
    out.type = predicate::kind::exists;
    out.property = n.children.at (0)->string ().substr (1);
  }
  else {
    if (n.is_type<negation> () && !n.children.empty () &&
        n.children.front ()->is_type<negation> ())
      out.type = predicate::kind::negation;
    else if (n.children.size () == 1)
      return read_predicate (*n.children.front ());
    else out.type = n.is_type<expression> () ? predicate::kind::disjunction :
                                              predicate::kind::conjunction;
    for (const auto& child: n.children)
      out.operands.push_back (read_predicate (*child));
  }
  return out;
}

std::uint64_t read_bound (const node& n) {
  value v = value::parse (n.string ());
  if (v.is_number_unsigned ()) return v.get<std::uint64_t> ();
  if (v.is_string ()) {
    const auto s = v.get<std::string> ();
    std::uint64_t number = 0;
    auto r = std::from_chars (s.data (), s.data () + s.size (), number);
    if (!s.empty () && r.ec == std::errc () && r.ptr == s.data () + s.size ())
      return number;
  }
  throw std::invalid_argument ("Traversal bounds must be unsigned integers");
}

// Bound the parser's nesting before entering recursive PEG rules. Quotes are
// still validated by PEGTL/JSON, not by this resource-limit scan.
void check_source_size (const std::string& source) {
  if (source.empty () || source.size () > 65536)
    throw std::invalid_argument ("Selection size must be 1..65536 bytes");
  unsigned depth = 0;
  bool quoted = false, escaped = false;
  for (char c: source) {
    if (escaped) { escaped = false; continue; }
    if (quoted && c == '\\') { escaped = true; continue; }
    if (c == '"') { quoted = !quoted; continue; }
    if (quoted) continue;
    if (c == '(' && ++depth > 64)
      throw std::invalid_argument ("Selection nesting exceeds 64");
    if (c == ')' && depth) --depth;
  }
  // Prefix NOT/! also recurses without parentheses.
  if (std::count (source.begin (), source.end (), '!') > 64)
    throw std::invalid_argument ("Too many predicate negations");
  std::size_t start = 0, count = 0;
  while ((start = source.find ("NOT", start)) != std::string::npos) {
    if (++count > 64) throw std::invalid_argument ("Too many predicate negations");
    start += 3;
  }
}
} // namespace

selection parse_selection (const std::string& source) {
  check_source_size (source);
  try {
    tao::pegtl::memory_input input (source, "AUDM selection");
    const auto tree = tao::pegtl::parse_tree::parse<grammar::document,
      grammar::nodes> (input);
    if (!tree) throw std::invalid_argument ("Invalid selection");
    selection result;
    for (const auto& child: tree->children) {
      selector s;
      const auto& n = *child;
      if (n.is_type<grammar::default_resource> ())
        s.type = selector::kind::default_resource;
      else if (n.is_type<grammar::quoted_name> ())
        s.name = value::parse (n.string ()).get<std::string> ();
      else if (n.is_type<grammar::bare_name> ()) {
        s.name = n.string ();
        const auto a = s.name.find_first_not_of (" \t");
        if (a == std::string::npos) throw std::invalid_argument ("Empty name");
        s.name = s.name.substr (a, s.name.find_last_not_of (" \t") - a + 1);
      }
      else {
        s.type = n.is_type<grammar::recursive> () ? selector::kind::recursive :
                 n.is_type<grammar::scoped> () ? selector::kind::scoped :
                                                selector::kind::local;
        s.filter = read_predicate (*n.children.at (0));
        for (std::size_t i = 1; i < n.children.size (); ++i) {
          const auto& l = *n.children[i];
          const auto key = l.string ().substr (0, l.string ().find_first_of (" =\t\r\n"));
          auto* destination = key == "$max_depth" ? &s.limits.max_depth :
            key == "$max_matches" ? &s.limits.max_matches : &s.limits.max_duration_ms;
          if (destination->has_value ()) throw std::invalid_argument ("Duplicate traversal bound");
          *destination = read_bound (*l.children.at (0));
          if (key != "$max_depth" && **destination == 0)
            throw std::invalid_argument ("Match and duration bounds must be positive");
          if (key == "$max_duration" && **destination > 86400000)
            throw std::invalid_argument ("Maximum traversal duration is 86400000 milliseconds");
        }
        if (s.type == selector::kind::recursive && !s.limits.max_depth &&
            !s.limits.max_matches && !s.limits.max_duration_ms)
          throw std::invalid_argument ("Cross-resolver recursion requires a bound");
        if (s.type == selector::kind::local && n.children.size () > 1)
          throw std::invalid_argument ("Local selectors do not accept traversal bounds");
      }
      result.push_back (std::move (s));
      if (result.size () > 256) throw std::invalid_argument ("Too many selectors");
    }
    return result;
  }
  catch (const std::exception& e) { throw std::invalid_argument (e.what ()); }
}

bool predicate::matches (const value& properties) const {
  switch (type) {
  case kind::constant: return constant;
  case kind::exists: return properties.contains (property);
  case kind::negation: return !operands.at (0).matches (properties);
  case kind::conjunction:
    for (const auto& p: operands) if (!p.matches (properties)) return false;
    return true;
  case kind::disjunction:
    for (const auto& p: operands) if (p.matches (properties)) return true;
    return false;
  case kind::comparison: break;
  }
  auto it = properties.find (property);
  if (it == properties.end ()) return false;
  const auto& actual = *it;
  if (actual.type () != literal.type () &&
      !(actual.is_number () && literal.is_number ())) return false;
  if (actual.is_string () && literal.is_string () &&
      (operation == "=" || operation == "!=" || operation == "contains" ||
       operation == "starts_with" || operation == "ends_with")) {
    const bool matched = string_matches (actual.get_ref<const std::string&> (),
      literal.get_ref<const std::string&> (), operation);
    return operation == "!=" ? !matched : matched;
  }
  if (operation == "=") return actual == literal;
  if (operation == "!=") return actual != literal;
  if (operation == "<") return actual < literal;
  if (operation == "<=") return actual <= literal;
  if (operation == ">") return actual > literal;
  if (operation == ">=") return actual >= literal;
  return false;
}
} // namespace athena::interop
