/******************************************************************************
* MODULE     : math_v2_to_v3.cpp
* DESCRIPTION: Offline-only mathematical AST conversion for XML v3
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include "math_v2_to_v3.hpp"
#include "math_tree_v3.hpp"
#include "node_metadata.hpp"
#include "tree_analyze.hpp"
#include "named_symbol.hpp"
#include "drd_std.hpp"
#include "ATHENA/Data/document_node_model.hpp"
#include "vars.hpp"
#include <algorithm>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <unordered_set>
#include <unicode/uchar.h>
#include <unicode/utf8.h>

namespace athena::document {
namespace {

using source_path= std::vector<int>;

std::string ascii (const string& value) {
  return {value.data (), std::size_t (N (value))};
}

const std::set<std::string>& cold_declarations () {
  static const auto names= [] {
    std::set<std::string> result;
#define init(label, name, ...) result.insert (ascii (name))
#define init_var(name, ...) result.insert (ascii (name))
#include "Data/Drd/standard_tree_labels.inc"
#undef init_var
#undef init
#define make_tree_label(name) result.insert (ascii (name))
#include "shipped_macro_labels.inc"
#undef make_tree_label
    return result;
  } ();
  return names;
}

void check_cold_tree (const tree& value, const std::string& where);

void check_cold_property (const athena::node::property& value,
                          const std::string& where) {
  if (const auto* rich= std::get_if<athena::node::rich_text> (&value.data))
    check_cold_tree (rich->content, where);
  else if (const auto* list= std::get_if<athena::node::property::list> (&value.data))
    for (std::size_t i=0; i<list->size (); ++i)
      check_cold_property ((*list)[i], where + "/" + std::to_string (i));
  else if (const auto* dict=
           std::get_if<athena::node::property::dictionary> (&value.data))
    for (const auto& [key, child]: *dict)
      check_cold_property (child, where + "/" + key);
}

void check_cold_tree (const tree& value, const std::string& where) {
  if (is_compound (value)) {
    const auto tag= ascii (as_string (L (value)));
    if (!cold_declarations ().count (tag))
      throw std::runtime_error (
        "No cold-load declaration for v3 tag '" + tag + "' at " + where +
        "; resolve the source or its extension declaration before migration");
    for (int i=0; i<N(value); ++i)
      check_cold_tree (value[i], where + "/" + std::to_string (i));
  }
  if (const auto* meta= athena::node::get (value))
    for (const auto& [key, property]: meta->properties)
      check_cold_property (property, where + "/@" + key);
}

bool has_metadata (const tree& value);

string unicode_delimiter (const tree& source) {
  // Legacy delimiter descriptors may be a raw text leaf, or a one-item
  // CONCAT(NAMED_SYMBOL(...)). The latter is *not* an invisible bracket:
  // lvert/rvert are genuine visible endpoints, and discarding them would
  // change the mathematical expression. This decoding belongs solely to
  // the v2 -> v3 migrator, never to the strict v3 Reader.
  if (is_func (source, CONCAT, 1) && !has_metadata (source))
    return unicode_delimiter (source[0]);
  if (is_func (source, NAMED_SYMBOL, 1) && is_atomic (source[0])) {
    const string& name= source[0]->label;
    if (name == "texmacs:nobracket") return "";
    if (name == "texmacs:lvert" || name == "texmacs:rvert") return "|";
    if (name == "texmacs:lVert" || name == "texmacs:rVert") return "‖";
    throw std::runtime_error (
      "Unrecognized historical named delimiter: " + ascii (name));
  }
  if (!is_atomic (source))
    throw std::runtime_error ("Noncanonical v2 delimiter endpoint structure");
  const string glyph= source->label;
  if (glyph == "." || glyph == "<nobracket>" ||
      glyph == "texmacs:nobracket") return "";
  static const std::map<std::string,string> names= {
    {"<langle>", "⟨"}, {"<rangle>", "⟩"},
    {"langle", "⟨"}, {"rangle", "⟩"},
    {"<lfloor>", "⌊"}, {"<rfloor>", "⌋"},
    {"<lceil>", "⌈"}, {"<rceil>", "⌉"},
    {"<llbracket>", "⟦"}, {"<rrbracket>", "⟧"},
    {"<lBrack>", "⟦"}, {"<rBrack>", "⟧"},
    {"<||>", "‖"}, {"<vert>", "|"}, {"<Vert>", "‖"},
    {"<lbrace>", "{"}, {"<rbrace>", "}"},
    {"<lvert>", "|"}, {"<rvert>", "|"},
    {"<lVert>", "‖"}, {"<rVert>", "‖"},
    {"backslash", "\\"}, {"<backslash>", "\\"}
  };
  auto found= names.find (ascii (glyph));
  return found == names.end () ? glyph : found->second;
}

string unicode_accent (const string& source, bool& stretch) {
  static const std::map<std::string,string> identities= {
    {"^", "̂"}, {"<hat>", "̂"},
    {"~", "̃"}, {"<tilde>", "̃"},
    {"<bar>", "̅"}, {"<overline>", "̅"},
    {"<wide-bar>", "̅"},
    {"<vect>", "⃗"}, {"<rightarrow>", "⃗"},
    {"<wide-varrightarrow>", "⃗"},
    {"<wide-varleftarrow>", "⃖"},
    {"<leftarrow>", "⃖"}, {"<leftrightarrow>", "⃡"},
    {"<breve>", "̆"}, {"<check>", "̌"},
    {"<acute>", "́"}, {"<grave>", "̀"},
    {"<dot>", "̇"}, {"<ddot>", "̈"},
    {"<dddot>", "⃛"}, {"<ddddot>", "⃜"},
    {"<abovering>", "̊"},
    // The historical CMMI varbind accent is an over-tie between operands.
    {"<bind>", "͡"},
    {"<overbrace>", "⏞"}, {"<underbrace>", "⏟"},
    {"<wide-overbrace>", "⏞"}, {"<wide-underbrace>", "⏟"},
    {"<poverbrace>", "⏜"}, {"<punderbrace>", "⏝"},
    {"<sqoverbrace>", "⎴"}, {"<squnderbrace>", "⎵"}
  };
  const std::string raw= ascii (source);
  auto found= identities.find (raw);
  if (found != identities.end ()) {
    if (raw.rfind ("<wide-", 0) == 0) stretch= true;
    return found->second;
  }
  if (raw.size () > 7 && raw.compare (0, 6, "<wide-") == 0 && raw.back () == '>') {
    const std::string without_wide= "<" + raw.substr (6);
    auto base= identities.find (without_wide);
    if (base != identities.end ()) { stretch= true; return base->second; }
  }
  return source;
}

bool script_label (tree_label label) {
  return label == RSUB || label == RSUP || label == LSUB ||
         label == LSUP || label == RPRIME || label == LPRIME;
}

int script_slot (tree_label label) {
  switch (label) {
  case LSUB: return 1;
  case LSUP: case LPRIME: return 2;
  case RSUB: return 3;
  case RSUP: case RPRIME: return 4;
  default: return -1;
  }
}

bool has_metadata (const tree& value) {
  const auto* meta= athena::node::get (value);
  return meta && !meta->empty ();
}

tree repair_character_escapes (const tree& source) {
  if (!is_func (source, CONCAT)) return source;
  static const std::regex digit ("'?([0-9])");
  bool damaged= false;
  for (int i=0; i<N(source); ++i)
    if (is_compound (source[i]) && N(source[i]) == 0 &&
        std::regex_match (ascii (as_string (L(source[i]))), digit)) damaged= true;
  if (!damaged) return source;
  std::string encoded;
  for (int i=0; i<N(source); ++i) {
    const tree& child= source[i];
    if (has_metadata (child))
      throw std::runtime_error ("Character escape repair would merge annotated source nodes");
    if (is_atomic (child)) encoded+= ascii (child->label);
    else {
      std::smatch match;
      const std::string tag= ascii (as_string (L(child)));
      if (N(child) != 0 || !std::regex_match (tag, match, digit) ||
          encoded.empty () || encoded.back () != '<')
        throw std::runtime_error ("Ambiguous fragmented character escape");
      encoded+= "#" + match[1].str ();
    }
  }
  static const std::regex escape ("<#([0-9A-Fa-f][0-9A-Fa-f*]*)>");
  std::string decoded;
  std::size_t offset= 0;
  for (std::sregex_iterator it (encoded.begin (), encoded.end (), escape), end;
       it != end; ++it) {
    decoded+= encoded.substr (offset, std::size_t (it->position ()) - offset);
    std::string hex= (*it)[1].str ();
    hex.erase (std::remove (hex.begin (), hex.end (), '*'), hex.end ());
    if (hex.size () > 6) throw std::runtime_error ("Oversized character escape");
    const auto scalar= std::stoul (hex, nullptr, 16);
    if (scalar < 0xa0 || scalar > 0x10ffff || !U_IS_UNICODE_CHAR (scalar))
      throw std::runtime_error ("Invalid Unicode character escape");
    char bytes[4];
    int32_t length= 0;
    UBool error= false;
    U8_APPEND (bytes, length, 4, scalar, error);
    if (error) throw std::runtime_error ("Cannot encode repaired character escape");
    decoded.append (bytes, std::size_t (length));
    offset= std::size_t (it->position () + it->length ());
  }
  decoded+= encoded.substr (offset);
  if (decoded.find ("<#") != std::string::npos)
    throw std::runtime_error ("Incomplete fragmented character escape");
  tree result (CONCAT, tree (string (decoded.data (), int (decoded.size ()))));
  athena::node::copy_metadata (source, result);
  return result;
}

tree legacy_length (const tree& source) {
  if (!is_atomic (source))
    throw std::runtime_error ("Non-text legacy layout length");
  std::string value= ascii (source->label);
  static const std::regex historical_length (
    "[+-]?([0-9]+(\\.\\*?[0-9]*)?|\\.\\*?[0-9]+)\\*?"
    "(p\\*?t|p\\*?c|i\\*?n|c\\*?m|m\\*?m|e\\*?m|e\\*?x|p\\*?x)");
  if (!std::regex_match (value, historical_length))
    throw std::runtime_error ("Ambiguous legacy layout length: " + value);
  value.erase (std::remove (value.begin (), value.end (), '*'), value.end ());
  static const std::regex length (
    "[+-]?([0-9]+(\\.[0-9]*)?|\\.[0-9]+)(pt|pc|in|cm|mm|em|ex|px)");
  if (!std::regex_match (value, length))
    throw std::runtime_error ("Unsupported legacy layout length: " + ascii (source->label));
  tree result (string (value.data (), int (value.size ())));
  athena::node::copy_metadata (source, result);
  return result;
}

bool locally_enters_math_mode (const tree& value) {
  if (!is_func (value, WITH) || N (value) < 3) return false;
  for (int i=0; i+1 < N (value)-1; i+=2) {
    if (!is_atomic (value[i]) || value[i] != "mode") continue;
    const tree& setting= value[i+1];
    if (setting == "math" ||
        (is_func (setting, QUOTE, 1) && setting[0] == "math"))
      return true;
  }
  return false;
}

void inherited_header (const tree& old, tree& converted) {
  athena::node::copy_metadata (old, converted);
}

void source_ids (const tree& node, std::set<std::string>& ids);

void property_source_ids (const athena::node::property& value,
                          std::set<std::string>& ids) {
  if (const auto* rich= std::get_if<athena::node::rich_text> (&value.data))
    source_ids (rich->content, ids);
  else if (const auto* items= std::get_if<athena::node::property::list> (&value.data))
    for (const auto& item: *items) property_source_ids (item, ids);
  else if (const auto* dictionary=
           std::get_if<athena::node::property::dictionary> (&value.data))
    for (const auto& [name, item]: *dictionary) {
      (void) name;
      property_source_ids (item, ids);
    }
}

void source_ids (const tree& node, std::set<std::string>& ids) {
  if (const auto* meta= athena::node::get (node)) {
    if (!meta->id.empty ()) ids.insert (meta->id);
    for (const auto& [name, property]: meta->properties) {
      (void) name;
      property_source_ids (property, ids);
    }
  }
  if (is_compound (node))
    for (int i=0; i<N (node); ++i) source_ids (node[i], ids);
}

tree prune_removed_reference_records (
    const tree& source, const std::unordered_set<std::string>& ids,
    bool references= false) {
  if (!is_compound (source)) return source;
  const bool inside= references || is_compound (source, "references");
  tree output (L (source));
  for (int i=0; i<N (source); ++i) {
    const tree& child= source[i];
    if (inside && is_func (child, ASSOCIATE, 2) && is_atomic (child[0])) {
      const std::string key (child[0]->label.data (),
                             std::size_t (N (child[0]->label)));
      static const std::string prefix= "athena-node-";
      if (key.rfind (prefix, 0) == 0 &&
          ids.count (key.substr (prefix.size ()))) continue;
    }
    output << prune_removed_reference_records (child, ids, inside);
  }
  inherited_header (source, output);
  return output;
}

class converter {
  math_v3_migration report_;
  bool strict_;
  int delimiter_depth_= 0;

  static bool has_rich_value (const athena::node::property& value) {
    if (std::holds_alternative<athena::node::rich_text> (value.data)) return true;
    if (const auto* list= std::get_if<athena::node::property::list> (&value.data)) {
      for (const auto& item: *list) if (has_rich_value (item)) return true;
    }
    if (const auto* dict=
          std::get_if<athena::node::property::dictionary> (&value.data)) {
      for (const auto& [key, item]: *dict) {
        (void) key;
        if (has_rich_value (item)) return true;
      }
    }
    return false;
  }

  void rewrite_property_rich (athena::node::property& value,
                              source_path path) {
    if (auto* rich= std::get_if<athena::node::rich_text> (&value.data)) {
      tree converted= walk (rich->content, false, path);
      rewrite_nested_metadata (converted, path);
      rich->content= std::move (converted);
      return;
    }
    if (auto* list= std::get_if<athena::node::property::list> (&value.data))
      for (auto& item: *list) rewrite_property_rich (item, path);
    if (auto* dict=
         std::get_if<athena::node::property::dictionary> (&value.data))
      for (auto& [key, item]: *dict) {
        (void) key;
        rewrite_property_rich (item, path);
      }
  }

  void issue (const source_path& path, const char* code, std::string detail) {
    if (strict_)
      throw std::runtime_error ("Strict v3 migration: " + std::string (code) +
                                " at " + path_text (path) + ": " + detail);
    report_.warnings.push_back ({path, code, std::move (detail)});
  }

  static std::string path_text (const source_path& path) {
    std::string out= "/";
    for (int part: path) out+= std::to_string (part) + "/";
    return out;
  }

  tree tokenized (const tree& source) {
    // Use the established TeXmacs math-language lexer. This is a lexical
    // presentation tokenization, not a semantic parse of the formula.
    const array<tree> tokens= concat_tokenize (source);
    if (N (tokens) == 1 && is_atomic (tokens[0]) &&
        tokens[0] == source) return copy (source);
    tree result;
    if (N (tokens) == 0) result= tree ("");
    else if (N (tokens) == 1) result= tokens[0];
    else result= tree (CONCAT, tokens);
    // An identified text run remains one identified container, never a UUID
    // moved onto the first arbitrary token.
    inherited_header (source, result);
    report_.math_tokens+= std::size_t (N (tokens));
    return result;
  }

  tree wrap_script (const tree& base, const tree& original_script,
                    const tree& content, int slot, const source_path& path) {
    tree payload= content;
    if (has_metadata (original_script)) {
      // The legacy script wrapper was a separate source object. Retain its
      // identity at a dedicated subtree, not on the base or the new container.
      if (has_metadata (payload)) {
        tree wrapper (CONCAT, payload);
        inherited_header (original_script, wrapper);
        payload= wrapper;
      }
      else inherited_header (original_script, payload);
      issue (path, "SCRIPT_WRAPPER_IDENTITY_REHOMED",
             "Legacy script identity now belongs to its script expression");
    }
    if (is_func (base, MATH_SCRIPTS, 5) && base[slot] == "") {
      tree next (base, 5);
      for (int i=0; i<5; ++i) next[i]=base[i];
      next[slot]= payload;
      inherited_header (base, next);
      ++report_.scripts;
      return next;
    }
    tree slots[5]= {base, tree (""), tree (""), tree (""), tree ("")};
    slots[slot]= payload;
    ++report_.scripts;
    return math_v3::scripts (slots[0], slots[1], slots[2], slots[3], slots[4]);
  }

  void emit (array<tree>& out, const tree& value) {
    if (is_func (value, CONCAT) && !has_metadata (value))
      for (int i=0; i<N (value); ++i) out << value[i];
    else out << value;
  }

  struct logical_math_item {
    tree source;
    source_path original_path;
  };

  struct pending_left_script {
    tree source;
    tree content;
    int slot;
    source_path source_path;
  };

  static bool starts_identifier (const tree& source) {
    if (is_atomic (source)) {
      if (N (source->label) == 0) return false;
      int32_t offset= 0;
      UChar32 codepoint= U_SENTINEL;
      U8_NEXT (source->label.data (), offset, N (source->label), codepoint);
      return codepoint >= 0 && u_isalpha (codepoint);
    }
    // A math-mode text or variant node may serve as a single lexical base.
    if (is_compound (source, "text", 1) ||
        is_compound (source, "math", 1) ||
        is_func (source, MATH_VARIANT, 2))
      return starts_identifier (source[N (source)-1]);
    if (is_func (source, CONCAT) && N (source) > 0)
      return starts_identifier (source[0]);
    return false;
  }

  static bool following_prescript_base (
      const std::vector<logical_math_item>& items, std::size_t first) {
    for (std::size_t j=first; j<items.size (); ++j) {
      // Several prescripts may precede the same following identifier.
      if (script_label (L (items[j].source)) &&
          N (items[j].source) == 1) continue;
      return starts_identifier (items[j].source);
    }
    return false;
  }

  tree attach_left_script (tree base, const pending_left_script& deferred) {
    // A run such as `f=-f` is lexed into several tokens. Its prescript
    // belongs to the first math token, not to the entire equality. Preserve
    // the text run's own UUID on its enclosing CONCAT when it has one.
    if (is_func (base, CONCAT) && N (base) > 0) {
      tree grouped (CONCAT);
      grouped << wrap_script (base[0], deferred.source, deferred.content,
                              deferred.slot, deferred.source_path);
      for (int i=1; i<N (base); ++i) grouped << base[i];
      inherited_header (base, grouped);
      return grouped;
    }
    return wrap_script (base, deferred.source, deferred.content,
                        deferred.slot, deferred.source_path);
  }

  void collect_math_items (const tree& value, source_path where, bool math,
                           std::vector<logical_math_item>& items) {
    // TeXmacs frequently represents a single inline expression as
    //   math(concat("GL", math(concat(rsub("2"), around("(","R",")")))))
    // The nested, unannotated `math` is a mode switch, NOT a script-base
    // boundary. Only unwrap it inside an existing math expression, and only
    // if neither the wrapper nor its concat body owns source identity.
    // Never cross from ordinary paragraph text into a neighboring math node.
    if (math && is_compound (value, "math", 1) &&
        !has_metadata (value)) {
      const tree& body= value[0];
      if (is_func (body, CONCAT) && !has_metadata (body)) {
        where.push_back (0);
        for (int i=0; i<N (body); ++i) {
          source_path child= where;
          child.push_back (i);
          collect_math_items (body[i], std::move (child), math, items);
        }
        return;
      }
      if (is_atomic (body) && !has_metadata (body)) {
        where.push_back (0);
        items.push_back ({body, std::move (where)});
        return;
      }
    }
    items.push_back ({value, std::move (where)});
  }

  tree sequence (const tree& source, bool math, source_path where) {
    const tree normalized= repair_character_escapes (source);
    array<tree> children;
    std::optional<pending_left_script> deferred_lsub, deferred_lsup;
    std::vector<logical_math_item> items;
    items.reserve (std::size_t (N (source)));
    for (int i=0; i<N (normalized); ++i) {
      source_path path= where;
      path.push_back (i);
      collect_math_items (normalized[i], std::move (path), math, items);
    }
    for (std::size_t i= 0; i<items.size (); ++i) {
      const tree& raw= items[i].source;
      const tree_label label= L (raw);
      const source_path& child_path= items[i].original_path;
      if (math && is_compound (raw, "Big", 1) && raw[0] == "<") {
        int depth= 1;
        std::size_t closing= i+1;
        for (; closing<items.size (); ++closing) {
          const tree& candidate= items[closing].source;
          if (is_compound (candidate, "Big", 1) && candidate[0] == "<") ++depth;
          if (is_compound (candidate, "Big", 1) && candidate[0] == ">" && --depth == 0)
            break;
        }
        if (closing == items.size ())
          throw std::runtime_error ("Unpaired historical Big delimiter at " + path_text (child_path));
        const tree& right= items[closing].source;
        if (has_metadata (raw) || has_metadata (right) ||
            has_metadata (raw[0]) || has_metadata (right[0]))
          throw std::runtime_error ("Annotated Big endpoints require explicit identity resolution");
        tree middle (CONCAT);
        for (std::size_t j=i+1; j<closing; ++j) middle << items[j].source;
        tree inner= sequence (middle, true, child_path);
        children << math_v3::delimited ("⟨", "⟩", 'm', inner, "2", "2");
        ++report_.delimiters;
        i= closing;
        continue;
      }
      if (label == LEFT) {
        int closing= -1, depth= 1;
        for (std::size_t j=i+1; j<items.size (); ++j) {
          if (L (items[j].source) == LEFT) ++depth;
          else if (L (items[j].source) == RIGHT && --depth == 0) {
            closing= j;
            break;
          }
        }
        if (closing < 0) {
          issue (child_path, "UNPAIRED_LEFT_DELIMITER",
                 "Legacy open bracket has no matching right bracket; keeping its glyph as text");
          if (!is_func (raw, LEFT, 1) || !is_atomic (raw[0]))
            throw std::runtime_error ("Malformed legacy LEFT at " + path_text (child_path));
          tree glyph (unicode_delimiter (raw[0]));
          inherited_header (raw, glyph);
          emit (children, glyph);
          continue;
        }
        if (!is_func (raw, LEFT, 1) || !is_atomic (raw[0]) ||
            !is_func (items[closing].source, RIGHT, 1) ||
            !is_atomic (items[closing].source[0]))
          throw std::runtime_error ("Malformed legacy bracket pair at " +
                                    path_text (child_path));
        const string left= unicode_delimiter (raw[0]);
        const string right= unicode_delimiter (items[closing].source[0]);
        if (!math_v3::glyph (left, true) || !math_v3::glyph (right, true) ||
            (left == "" && right == ""))
          throw std::runtime_error ("Invalid legacy bracket pair at " +
                                    path_text (child_path));
        tree middle (CONCAT);
        for (std::size_t j=i+1; j<std::size_t (closing); ++j)
          middle << items[j].source;
        struct delimiter_scope {
          int& depth;
          explicit delimiter_scope (int& value): depth (value) { ++depth; }
          ~delimiter_scope () { --depth; }
        } scope (delimiter_depth_);
        tree inner= sequence (middle, true, child_path);
        tree pair= math_v3::delimited (left, right, 'a', inner);
        if (has_metadata (raw) && !has_metadata (items[closing].source))
          inherited_header (raw, pair);
        if (has_metadata (raw) && has_metadata (items[closing].source))
          issue (child_path, "BRACKET_ENDPOINT_IDENTITIES",
                 "A matched delimiter pair had separate annotated endpoints");
        ++report_.delimiters;
        emit (children, pair);
        i= closing;
        continue;
      }
      if (label == RIGHT) {
        issue (child_path, "UNPAIRED_RIGHT_DELIMITER",
               "Legacy closing bracket has no matching left bracket; keeping its glyph as text");
        if (!is_func (raw, RIGHT, 1) || !is_atomic (raw[0]))
          throw std::runtime_error ("Malformed legacy RIGHT at " + path_text (child_path));
        tree glyph (unicode_delimiter (raw[0]));
        inherited_header (raw, glyph);
        emit (children, glyph);
        continue;
      }
      if (math && script_label (label) && N (raw) == 1) {
        const int slot= script_slot (label);
        source_path payload_path= child_path; payload_path.push_back (0);
        tree payload;
        if ((label == RSUP || label == LSUP) &&
            is_func (raw[0], RPRIME, 1) && is_atomic (raw[0][0])) {
          // A prime written inside an explicit superscript denotes the
          // superscript's actual glyph, not a second script on an empty base.
          // Preserve the prime payload UUID and its source node UUID if any.
          const tree& prime= raw[0];
          payload= copy (prime[0]);
          if (payload == "'") {
            tree unicode_prime ("′");
            inherited_header (payload, unicode_prime);
            payload= unicode_prime;
          }
          if (has_metadata (prime)) {
            if (has_metadata (payload)) {
              tree wrapper (CONCAT, payload);
              inherited_header (prime, wrapper);
              payload= wrapper;
            }
            else inherited_header (prime, payload);
          }
        }
        else payload= walk (raw[0], true, payload_path);
        if (label == RPRIME || label == LPRIME) {
          if (is_atomic (payload) && payload == "'") payload= tree ("′");
        }
        const bool leading_prescript=
          N (children) == 0 &&
          (label == RSUB || label == RSUP || label == RPRIME) &&
          following_prescript_base (items, i+1);
        if (slot <= 2 || leading_prescript) {
          const int left_slot= slot <= 2 ? slot : slot-2;
          auto& pending= left_slot == 1 ? deferred_lsub : deferred_lsup;
          if (pending) {
            issue (child_path, "DUPLICATE_LEFT_SCRIPT",
                   "Multiple scripts claim the same left-side slot");
            // Do not overwrite the earlier source object or its identity.
            children << wrap_script (tree (""), pending->source,
                                     pending->content, pending->slot,
                                     pending->source_path);
          }
          pending= pending_left_script {raw, payload, left_slot, child_path};
          continue;
        }
        if (N (children) == 0) {
          // An author may deliberately write just ^{op}, ^{-1}, etc. The
          // canonical five-slot MathScripts permits an explicit empty base.
          // This preserves the source rather than inventing a neighbor.
          children << wrap_script (tree (""), raw, payload, slot, child_path);
        }
        else {
          tree base= children[N (children)-1];
          // If a tokenized or identified source run was kept as a CONCAT,
          // bind a trailing script to its final token, not to the entire run.
          if (is_func (base, CONCAT) && N (base)>0 && !has_metadata (base)) {
            children[N (children)-1]= base;
          }
          if (is_func (base, CONCAT) && N (base)>0 && has_metadata (base)) {
            tree updated (CONCAT);
            for (int j=0; j<N (base)-1; ++j) updated << base[j];
            updated << wrap_script (base[N (base)-1], raw, payload, slot, child_path);
            inherited_header (base, updated);
            children[N (children)-1]= updated;
            continue;
          }
          children[N (children)-1]= wrap_script (base, raw, payload, slot, child_path);
        }
        continue;
      }
      if (math && is_func (raw, VAR_AROUND, 3) &&
          is_atomic (raw[0]) && raw[0] == "|" &&
          N (children) > 0 && starts_identifier (children[N (children)-1]) &&
          is_atomic (children[N (children)-1]) &&
          is_func (raw[1], RSUB, 1) &&
          !has_metadata (raw[0]) && !has_metadata (raw[2])) {
        // The historic evaluation/restriction form
        //   concat(v, around*("|", rsub(Gamma), <nobracket>))
        // represents v|_Gamma. It was built from a separate one-sided
        // bracket whose script was *inside* its body. Its exact v3 form is a
        // right-bar DELIMITED over v, carrying Gamma as its right subscript.
        // Do not generalize to paired |...| with a missing expression.
        const bool invisible_right=
          (is_atomic (raw[2]) &&
           (raw[2] == "." || raw[2] == "")) ||
          (is_func (raw[2], CONCAT, 1) &&
           is_func (raw[2][0], NAMED_SYMBOL, 1) &&
           raw[2][0][0] == "texmacs:nobracket");
        if (invisible_right) {
          tree expression= children[N (children)-1];
          tree delimited= math_v3::delimited ("", "|", 'a', expression);
          inherited_header (raw, delimited);
          source_path script_path= child_path;
          script_path.push_back (1);
          source_path value_path= script_path;
          value_path.push_back (0);
          tree value= walk (raw[1][0], true, value_path);
          children[N (children)-1]= wrap_script (
            delimited, raw[1], value, 3, script_path);
          ++report_.delimiters;
          continue;
        }
      }
      tree next= walk (raw, math, child_path);
      if (math && (deferred_lsub || deferred_lsup)) {
        if (deferred_lsub)
          next= attach_left_script (next, *deferred_lsub);
        if (deferred_lsup)
          next= attach_left_script (next, *deferred_lsup);
        deferred_lsub.reset ();
        deferred_lsup.reset ();
      }
      emit (children, next);
    }
    if (deferred_lsub || deferred_lsup) {
      issue (where, "LEFT_SCRIPT_WITHOUT_BASE",
             "Left script has no succeeding base; empty base inserted");
      tree standalone ("");
      if (deferred_lsub)
        standalone= wrap_script (standalone, deferred_lsub->source,
          deferred_lsub->content, 1, deferred_lsub->source_path);
      if (deferred_lsup)
        standalone= wrap_script (standalone, deferred_lsup->source,
          deferred_lsup->content, 2, deferred_lsup->source_path);
      children << standalone;
    }
    tree result (CONCAT, children);
    inherited_header (source, result);
    return result;
  }

  tree operator_atom (const tree& source, const source_path& where) {
    if (!is_func (source, BIG, 1))
      throw std::logic_error ("Expected one historical big operator");
    tree descriptor= source[0];
    bool upright= false, display_limits= false;
    while (is_func (descriptor, WITH) && N (descriptor) >= 3) {
      for (int i=0; i+1<N (descriptor)-1; i+=2)
        if (is_atomic (descriptor[i]) && is_atomic (descriptor[i+1]) &&
            descriptor[i+1] == "true") {
          if (descriptor[i] == "math-big-upright") upright= true;
          if (descriptor[i] == "math-big-limits") display_limits= true;
        }
      descriptor= descriptor[N (descriptor)-1];
    }
    if (!is_atomic (descriptor))
      throw std::runtime_error ("Non-atomic big operator at " + path_text (where));
    const string old= descriptor->label;
    if (old == "." || old == "bignone") return tree ("");
    static const std::map<std::string,string> names= {
      {"sum", "∑"}, {"prod", "∏"}, {"coprod", "∐"},
      {"int", "∫"}, {"iint", "∬"}, {"iiint", "∭"},
      {"iiiint", "⨌"}, {"oint", "∮"}, {"oiint", "∯"},
      {"oiiint", "∰"}, {"fint", "⨍"},
      // Historical BIG descriptors use bare font keys, not LaTeX command
      // names. Match the large-operator encoding, not the binary cup/cap glyph.
      {"cap", "⋂"}, {"cup", "⋃"}, {"wedge", "⋀"}, {"vee", "⋁"},
      {"oplus", "⨁"}, {"otimes", "⨂"}, {"odot", "⨀"},
      {"sqcap", "⨅"}, {"sqcup", "⨆"}, {"pluscup", "⨄"},
      {"amalg", "∐"}, {"times", "×"},
      {"bigcap", "⋂"}, {"bigcup", "⋃"},
      {"bigwedge", "⋀"}, {"bigvee", "⋁"},
      {"bigoplus", "⨁"}, {"bigotimes", "⨂"}
    };
    auto found= names.find (ascii (old));
    const string glyph= found == names.end () ? old : found->second;
    if (!math_v3::glyph (glyph))
      throw std::runtime_error (
        "Unrecognized historical big operator '" + ascii (old) +
        "' at " + path_text (where));
    tree result= math_v3::big_operator (glyph, upright);
    inherited_header (source, result);
    ++report_.operators;
    if (display_limits)
      result= math_v3::scripts (result, tree (""), tree (""),
                                tree (""), tree (""), 'd');
    return result;
  }

  tree accent (const tree& source, source_path where) {
    if (N (source) != 2)
      throw std::runtime_error ("Invalid historical accent at " + path_text (where));
    tree glyph= source[1];
    bool stretch= false;
    for (;;) {
      if (is_func (glyph, WITH) && N (glyph) >= 3) {
        for (int i=0; i+1<N (glyph)-1; i+=2)
          if (glyph[i] == "math-accent-stretch" && glyph[i+1] == "true")
            stretch= true;
        glyph= glyph[N (glyph)-1];
      }
      else if (is_func (glyph, CONCAT, 1) && !has_metadata (glyph))
        glyph= glyph[0];
      else break;
    }
    if (is_func (glyph, NAMED_SYMBOL, 1) && is_atomic (glyph[0])) {
      const string old= glyph[0]->label;
      const std::string prefix= "texmacs:";
      if (ascii (old).rfind (prefix, 0) != 0)
        throw std::runtime_error ("Unknown accent identity at " + path_text (where));
      glyph= tree (string ("<") *
                   string (old.data () + prefix.size (),
                           N (old) - int (prefix.size ())) * ">");
    }
    if (!is_atomic (glyph))
      throw std::runtime_error ("Non-atomic accent descriptor at " + path_text (where));
    const string canonical= unicode_accent (glyph->label, stretch);
    source_path body_path= where; body_path.push_back (0);
    tree body= walk (source[0], true, body_path);
    tree result (MATH_ACCENT, tree (canonical),
                 tree (L (source) == WIDE ? "o" : "u"), body);
    athena::node::metadata meta= athena::node::get (source) ?
      *athena::node::get (source) : athena::node::metadata ();
    meta.properties.emplace ("math:accent-stretch", athena::node::property (stretch));
    athena::node::set (result, meta);
    return result;
  }

  tree annotation (const tree& source, source_path where) {
    if (N (source) != 2)
      throw std::runtime_error ("Invalid mathematical annotation at " + path_text (where));
    source_path base_path= where; base_path.push_back (0);
    source_path modifier_path= where; modifier_path.push_back (1);
    const tree base= walk (source[0], true, base_path);
    // The editor may have left an entirely empty legacy script placeholder
    // inside an ABOVE arrow label. It has no glyph and no source identity;
    // keeping a nested baseless MathScripts would invent an annotation.
    const bool empty_script_placeholder=
      script_label (L (source[1])) && N (source[1]) == 1 &&
      is_atomic (source[1][0]) && source[1][0] == "" &&
      !has_metadata (source[1]) && !has_metadata (source[1][0]);
    const tree modifier= empty_script_placeholder ? tree ("") :
      walk (source[1], true, modifier_path);
    tree result (MATH_ANNOTATION, base,
      L (source) == BELOW ? modifier : tree (""),
      L (source) == ABOVE ? modifier : tree (""));
    inherited_header (source, result);
    return result;
  }

  tree named_symbol (const tree& source, const source_path& where) {
    if (!is_func (source, NAMED_SYMBOL, 1) || !is_atomic (source[0]))
      throw std::runtime_error ("Noncanonical historical named symbol at " + path_text (where));
    // The v3 namespace is a semantic symbol registry, not a second glyph
    // spelling system. Translate only identities with audited meanings.
    static const std::map<std::string,string> semantic= {
      {"texmacs:mathd", "differential-d"},
      {"texmacs:idotsint", "multiple-integral-with-dots"},
      {"texmacs:backassign", "backward-assignment"},
      {"texmacs:longhookrightarrow", "long-hookrightarrow"}
    };
    const std::string old= ascii (source[0]->label);
    if (old == "cork:3C" || old == "cork:3E") {
      // cork-escaped-to-ascii.json defines these bytes as &lt; and &gt;.
      // They are absent from corktounicode.json, so the legacy importer
      // preserved them as byte identities, not semantic named symbols.
      tree scalar (old == "cork:3C" ? "<" : ">");
      tree result= scalar;
      if (has_metadata (source[0])) {
        inherited_header (source[0], scalar);
        // Keep both identity owners rather than merging or dropping headers.
        result= tree (CONCAT, scalar);
      }
      inherited_header (source, result);
      return result;
    }
    if (old == "texmacs:comma" || old == "texmacs:textdegree") {
      tree result (old == "texmacs:comma" ? "," : "°");
      inherited_header (source, result);
      return result;
    }
    constexpr std::string_view prefix= "texmacs:";
    if (old.rfind (prefix, 0) != 0 ||
        !athena::text::standard_named_symbols ().lookup (old))
      throw std::runtime_error ("Unregistered named-symbol identity at " +
                                path_text (where) + ": " + old);
    const auto found= semantic.find (old);
    const std::string canonical= found == semantic.end () ?
      old.substr (prefix.size ()) : ascii (found->second);
    tree result (MATH_SYMBOL, tree (string (canonical.data (), int (canonical.size ()))));
    inherited_header (source, result);
    return result;
  }

  tree variant (const tree& source, source_path where) {
    if (N (source) != 1)
      throw std::runtime_error ("Invalid mathematical variant at " + path_text (where));
    const string old= as_string (L (source));
    static const std::map<std::string,string> styles= {
      {"math-alpha-bbb", "bb"}, {"math-up", "rm"},
      {"math-alpha-bold", "mbf"}, {"math-alpha-bold-up", "mbfu"},
      {"math-alpha-frak", "frak"}, {"math-alpha-cal", "cal"},
      {"math-it", "it"}, {"math-bf", "bf"},
      {"math-ss", "ss"}, {"math-tt", "tt"},
      {"math-sl", "sl"}, {"math-cal", "cal"}
    };
    auto found= styles.find (ascii (old));
    if (found == styles.end ())
      throw std::runtime_error ("Unknown mathvariant at " + path_text (where));
    where.push_back (0);
    tree result (MATH_VARIANT, tree (found->second), walk (source[0], true, where));
    inherited_header (source, result);
    return result;
  }

  tree long_arrow (const tree& source, source_path where) {
    if (N (source) != 2 && N (source) != 3)
      throw std::runtime_error ("Invalid historical long arrow at " + path_text (where));
    tree glyph= source[0];
    if (is_func (glyph, CONCAT, 1) && !has_metadata (glyph)) glyph= glyph[0];
    if (is_func (glyph, NAMED_SYMBOL, 1)) {
      if (!is_atomic (glyph[0]))
        throw std::runtime_error ("Malformed long-arrow identity at " + path_text (where));
      const std::string identity= ascii (glyph[0]->label);
      // Historical rubber font recipes all describe an actual Unicode glyph
      // stretched by LONG_ARROW. Native MathLongArrow needs that glyph only.
      static const std::map<std::string,string> rubber= {
        {"texmacs:rubber-equal", "="},
        {"texmacs:rubber-equiv", "≡"},
        {"texmacs:rubber-rightarrow", "→"},
        {"texmacs:rubber-leftarrow", "←"}
      };
      const auto found= rubber.find (identity);
      if (found != rubber.end ()) glyph= tree (found->second);
      else {
        source_path glyph_path= where; glyph_path.push_back (0);
        glyph= named_symbol (glyph, glyph_path);
      }
    }
    if (!is_func (glyph, MATH_SYMBOL, 1) &&
        (!is_atomic (glyph) || !math_v3::glyph (glyph->label)))
      throw std::runtime_error (
        "Unsupported long-arrow descriptor at " + path_text (where));
    source_path above_path= where; above_path.push_back (1);
    const tree above= walk (source[1], true, above_path);
    tree below ("");
    if (N (source) == 3) {
      source_path below_path= where; below_path.push_back (2);
      below= walk (source[2], true, below_path);
    }
    tree result (MATH_LONG_ARROW, glyph, below, above);
    inherited_header (source, result);
    return result;
  }

  tree delimit (const tree& source, const source_path& where, bool math) {
    if (N (source) != 3)
      throw std::runtime_error ("Nonternary historical delimiter at " + path_text (where));
    string left= unicode_delimiter (source[0]);
    string right= unicode_delimiter (source[2]);
    source_path body_path= where; body_path.push_back (1);
    struct delimiter_scope {
      int& depth;
      explicit delimiter_scope (int& value): depth (value) { ++depth; }
      ~delimiter_scope () { --depth; }
    } scope (delimiter_depth_);
    tree body= walk (source[1], math, body_path);
    if (left == "" && right == "") {
      // A pair of invisible endpoints is purely a presentation wrapper. Its
      // contents are unchanged. When the wrapper owns identity/properties,
      // preserve them on an explicitly identified CONCAT, rather than
      // discarding a UUID or transferring it onto a different semantic node.
      if (has_metadata (source[0]) || has_metadata (source[2]))
        throw std::runtime_error (
          "Invisible delimiter endpoint owns source identity at " +
          path_text (where));
      if (has_metadata (source)) {
        tree identified (CONCAT, body);
        inherited_header (source, identified);
        return identified;
      }
      return body;
    }
    if (!math_v3::glyph (left, true) || !math_v3::glyph (right, true))
      throw std::runtime_error ("Unrecognized historical delimiter at " + path_text (where));
    tree result= math_v3::delimited (left, right,
      L (source) == VAR_AROUND ? 'a' : 'f', body);
    inherited_header (source, result);
    if (has_metadata (source[0]) || has_metadata (source[2]))
      issue (where, "DELIMITER_ENDPOINT_METADATA",
             "Endpoint metadata cannot attach independently to a v3 delimiter glyph");
    ++report_.delimiters;
    return result;
  }

  tree display (const tree& source, source_path where) {
    if (N (source) != 1)
      throw std::runtime_error ("Invalid historical display math at " + path_text (where));
    tree content= source[0];
    const bool display_grid= is_compound (source, "eqnarray") ||
                            is_compound (source, "eqnarray*") ||
                            is_compound (source, "align") ||
                            is_compound (source, "align*");
    const std::string display_kind=
      is_compound (source, "eqnarray") || is_compound (source, "eqnarray*") ?
      "eqnarray" : "align";
    const bool multiline= is_func (content, DOCUMENT) && N (content) != 1;
    tree body;
    if (multiline) {
      // Multiple paragraph children are an explicit ordered display stack,
      // not an ambiguous expression. MATH_GRID(kind=stack) preserves each
      // paragraph, including its UUID and nested MathBlock/MathGrid content.
      // No user-authored expression is inferred or reassociated.
      array<tree> rows;
      for (int i=0; i<N (content); ++i) {
        source_path location= where;
        location.push_back (0); location.push_back (i);
        row_marker marker;
        tree line= walk (strip_row_marker (content[i], marker, location), true, location);
        tree row (ROW, tree (CELL, line));
        apply_row_marker (row, marker, true);
        rows << row;
      }
      if (N (rows) == 0) rows << tree (ROW, tree (CELL, tree ("")));
      body= tree (MATH_GRID, rows);
      athena::node::metadata m= athena::node::get (content) ?
        *athena::node::get (content) : athena::node::metadata ();
      m.properties["math:grid-kind"]= athena::node::property (std::string ("stack"));
      athena::node::set (body, m);
      ++report_.grids;
    }
    else {
      if (is_func (content, DOCUMENT, 1)) content= content[0];
      body= display_grid && (is_func (content, TABLE) || is_func (content, TFORMAT)) ?
            grid (content, where, display_kind) : walk (content, true, where);
    }
    if (display_grid && !is_func (body, MATH_GRID)) {
      issue (where, "DISPLAY_GRID_SHAPE",
             "Expected a table-based grid inside the display math environment");
      throw std::runtime_error ("Cannot construct v3 MathGrid from display source");
    }
    if (display_grid && !multiline) {
      athena::node::metadata header= athena::node::get (body) ?
        *athena::node::get (body) : athena::node::metadata ();
      header.properties["math:grid-kind"]= athena::node::property (
        display_kind);
      athena::node::set (body, header);
    }
    tree result (MATH_BLOCK, body);
    athena::node::metadata meta= athena::node::get (source) ?
      *athena::node::get (source) : athena::node::metadata ();
    // Shipped env-math eqnarray AND align delegate to their starred forms.
    // Only equation advances the counter implicitly; grid rows use explicit
    // eq-number/no-number/eq-lab markers, not LaTeX environment-name semantics.
    meta.properties.emplace ("math:block-numbered",
      athena::node::property (is_compound (source, "equation")));
    athena::node::set (result, meta);
    ++report_.math_blocks;
    return result;
  }

  struct row_marker {
    std::optional<tree> source;
    source_path where;
  };

  static bool is_row_marker (const tree& source) {
    return is_compound (source, "eq-number") ||
           is_compound (source, "no-number") || is_compound (source, "eq-lab");
  }

  tree strip_row_marker (const tree& source, row_marker& marker, source_path where) {
    if (is_row_marker (source)) {
      const int arity= is_compound (source, "eq-lab") ? 1 : 0;
      if (N (source) != arity)
        throw std::runtime_error ("Malformed equation row marker at " + path_text (where));
      if (marker.source)
        throw std::runtime_error ("Multiple/conflicting equation row markers at " +
                                  path_text (where));
      marker.source= source;
      marker.where= std::move (where);
      return tree ("");
    }
    // Only row-content sequences are transparent. Do not hoist through math
    // operands, formatting/custom macros, or nested tables/displays. Any marker
    // not consumed here must be rejected by walk, or handled by its own grid.
    if (!is_func (source, CONCAT) && !is_func (source, DOCUMENT)) return copy (source);
    tree result (L (source));
    for (int i=0; i<N (source); ++i) {
      source_path child= where; child.push_back (i);
      result << strip_row_marker (source[i], marker, std::move (child));
    }
    inherited_header (source, result);
    return result;
  }

  void apply_row_marker (tree& row, const row_marker& marker, bool allowed) {
    if (!marker.source) return;
    const auto fail= [&] (const char* reason) {
      throw std::runtime_error (std::string (reason) + " at " + path_text (marker.where));
    };
    if (!allowed) fail ("Equation row marker requires an eqnarray/align/stack grid");
    athena::node::metadata meta= athena::node::get (row) ?
      *athena::node::get (row) : athena::node::metadata ();
    if (meta.properties.count (math_v3::row_numbering_key) ||
        meta.properties.count (math_v3::row_tag_key))
      fail ("Equation row marker conflicts with existing typed row policy/tag");
    const tree& source= *marker.source;
    if (const auto* old= athena::node::get (source)) {
      if (!old->id.empty ()) {
        if (!meta.id.empty ()) fail ("Cannot preserve both row and equation marker UUIDs");
        meta.id= old->id;
      }
      for (const auto& [key, value]: old->properties) {
        if (key == math_v3::row_numbering_key || key == math_v3::row_tag_key ||
            !meta.properties.emplace (key, value).second)
          fail ("Cannot relocate equation marker metadata without a property collision");
      }
    }
    athena::node::set (row, meta);
    if (is_compound (source, "eq-lab")) {
      source_path child= marker.where; child.push_back (0);
      // env-math evaluates the argument in text mode. Explicit math subtrees
      // are still converted by walk; no label text is flattened or inferred.
      math_v3::set_row_tag (row, walk (source[0], false, std::move (child)));
    }
    else math_v3::set_row_numbering (row, is_compound (source, "eq-number") ?
      math_v3::row_numbering::numbered : math_v3::row_numbering::unnumbered);
  }

  tree grid (const tree& source, source_path where, const std::string& kind= "matrix") {
    // In the old model TFORMAT stores presentation directives as siblings of
    // TABLE. Rewrite one-child TFORMAT immediately; do not leave an invisible
    // adapter TFORMAT or DOCUMENT for the new Reader to reconstruct later.
    const tree& table= L (source) == TFORMAT ? source[N (source)-1] : source;
    if (!is_func (table, TABLE))
      throw std::runtime_error ("Malformed mathematical table at " + path_text (where));
    array<tree> rows;
    for (int i=0; i<N (table); ++i) {
      source_path child_path= where; child_path.push_back (i);
      if (!is_func (table[i], ROW))
        throw std::runtime_error ("MathGrid contains non-row child at " +
                                  path_text (child_path));
      array<tree> cells;
      row_marker marker;
      for (int j=0; j<N (table[i]); ++j) {
        source_path cell_path= child_path; cell_path.push_back (j);
        const tree& old_cell= table[i][j];
        tree stripped;
        if (is_func (old_cell, CELL, 1)) {
          source_path body_path= cell_path; body_path.push_back (0);
          stripped= tree (CELL, strip_row_marker (old_cell[0], marker, body_path));
          inherited_header (old_cell, stripped);
        }
        else stripped= strip_row_marker (old_cell, marker, cell_path);
        tree cell= walk (stripped, true, cell_path);
        if (!is_func (cell, CELL, 1)) {
          // A bare row child is unambiguously a cell body in legacy TABLE.
          // Add the required v3 CELL container, retaining any identity on
          // the original expression rather than inventing or moving one.
          cell= tree (CELL, cell);
        }
        cells << cell;
      }
      tree row (ROW, cells);
      inherited_header (table[i], row);
      apply_row_marker (row, marker, kind == "eqnarray" || kind == "align" || kind == "stack");
      rows << row;
    }
    if (L (source) == TFORMAT) {
      // Directives are resolved on the actual cells, not persisted as the
      // TeXmacs CWITH/TFORMAT programming language. Canonical v3 cells own a
      // typed math:cell-format dictionary of presentation properties.
      const std::map<std::string,std::string> keys= {
        {"cell-lborder", "border-left"},
        {"cell-rborder", "border-right"},
        {"cell-bborder", "border-bottom"},
        {"cell-tborder", "border-top"},
        {"cell-background", "background"},
        {"cell-height", "height"},
        {"cell-vmode", "height-mode"},
        {"cell-halign", "align"},
        {"color", "color"}
      };
      for (int q=0; q<N (source)-1; ++q) {
        const tree& directive= source[q];
        source_path directive_path= where; directive_path.push_back (q);
        if (!is_func (directive, CWITH, 6))
          throw std::runtime_error (
            "Unsupported mathematical grid directive at " + path_text (directive_path));
        for (int k=0; k<6; ++k)
          if (!is_atomic (directive[k]))
            throw std::runtime_error (
              "Grid directive is not scalar at " + path_text (directive_path));
        const auto variable= keys.find (ascii (directive[4]->label));
        if (variable == keys.end ())
          throw std::runtime_error ("Unknown native MathGrid cell format '" +
                                    ascii (directive[4]->label) + "' at " +
                                    path_text (directive_path));
        int values[4];
        for (int k=0; k<4; ++k) {
          if (!is_int (directive[k]))
            throw std::runtime_error ("Invalid MathGrid index at " +
                                      path_text (directive_path));
          values[k]= as_int (directive[k]);
          if (values[k] == 0)
            throw std::runtime_error ("MathGrid indices cannot be zero at " +
                                      path_text (directive_path));
        }
        const auto pos= [] (int index, int count) {
          return index > 0 ? index - 1 : count + index;
        };
        const int nr= N (rows);
        const std::string scalar= ascii (directive[5]->label);
        bool matched= false;
        for (int i=0; i<nr; ++i) {
          if (i < pos (values[0], nr) || i > pos (values[1], nr)) continue;
          const int nc= N (rows[i]);
          for (int j=0; j<nc; ++j) {
            if (j < pos (values[2], nc) || j > pos (values[3], nc)) continue;
            matched= true;
            tree& cell= rows[i][j];
            athena::node::metadata meta= athena::node::get (cell) ?
              *athena::node::get (cell) : athena::node::metadata ();
            athena::node::property::dictionary formats;
            const auto found= meta.properties.find ("math:cell-format");
            if (found != meta.properties.end ()) {
              const auto* previous= std::get_if<athena::node::property::dictionary> (
                &found->second.data);
              if (!previous)
                throw std::runtime_error ("MathGrid cell has invalid existing formats");
              formats= *previous;
            }
            formats[variable->second]= athena::node::property (scalar);
            meta.properties["math:cell-format"]= athena::node::property (
              std::move (formats));
            athena::node::set (cell, meta);
          }
        }
        if (!matched)
          issue (directive_path, "MATH_GRID_FORMAT_OUTSIDE_RANGE",
                 "Old cell formatting directive matched no cells");
      }
    }
    tree result (MATH_GRID, rows);
    athena::node::metadata meta= athena::node::get (source) ?
      *athena::node::get (source) : athena::node::metadata ();
    meta.properties["math:grid-kind"]= athena::node::property (kind);
    athena::node::set (result, meta);
    ++report_.grids;
    return result;
  }

  tree walk (const tree& source, bool math, source_path where,
             bool explicit_standalone_math= false) {
    if (is_atomic (source)) return math ? tokenized (source) : copy (source);
    if (is_compound (source, "fint", 0)) {
      tree operator_source (BIG, "fint");
      inherited_header (source, operator_source);
      return operator_atom (operator_source, where);
    }
    if (is_compound (source, "ohm", 0)) {
      tree result ("Ω");
      inherited_header (source, result);
      return result;
    }
    if (is_compound (source, "mathllap", 1)) {
      auto child= where; child.push_back (0);
      tree result (RESIZE, walk (source[0], true, child), "1r", "", "1r", "");
      inherited_header (source, result);
      return result;
    }
    if (is_compound (source, "kern", 1)) {
      tree result (SPACE, legacy_length (source[0]));
      inherited_header (source, result);
      return result;
    }
    if (is_compound (source, "raise", 2)) {
      auto child= where; child.push_back (1);
      tree result (MOVE, walk (source[1], math, child), "0pt", legacy_length (source[0]));
      inherited_header (source, result);
      return result;
    }
    if (is_compound (source, "bbox*", 2)) {
      if (!is_atomic (source[0]) || has_metadata (source[0]))
        throw std::runtime_error ("Unsupported structured/annotated bbox options");
      std::string options= ascii (source[0]->label);
      options.erase (std::remove (options.begin (), options.end (), '*'), options.end ());
      static const std::regex syntax (
        "([0-9]+(?:\\.[0-9]+)?px), *border: *([0-9]+(?:\\.[0-9]+)?px) *solid *([a-zA-Z]+|#[0-9a-fA-F]{6})");
      std::smatch match;
      if (!std::regex_match (options, match, syntax))
        throw std::runtime_error ("Unsupported bbox options: " + options);
      auto text= [&] (int index) { return tree (string (match[index].str ().c_str ())); };
      auto child= where; child.push_back (1);
      tree result (WITH);
      result << tree (ORNAMENT_SHAPE) << tree ("classic")
             << tree (ORNAMENT_BORDER) << text (2)
             << tree (ORNAMENT_HPADDING) << text (1)
             << tree (ORNAMENT_VPADDING) << text (1)
             << tree (ORNAMENT_COLOR) << tree ("none")
             << tree (ORNAMENT_SUNNY_COLOR) << text (3)
             << tree (ORNAMENT_SHADOW_COLOR) << text (3)
             << tree (ORNAMENT_SWELL) << tree ("0")
             << tree (ORNAMENT, walk (source[1], math, child));
      inherited_header (source, result);
      return result;
    }
    if (is_compound (source, ">", 4) && is_func (source[1], DOCUMENT) &&
        is_atomic (source[3])) {
      tree content (DOCUMENT);
      auto first= where; first.push_back (0);
      auto body= where; body.push_back (1);
      auto title= where; title.push_back (2);
      content << walk (source[0], false, first)
              << walk (source[1], false, body)
              << tree (HLINK, walk (source[2], false, title), copy (source[3]));
      tree result= compound ("quote-env", content);
      inherited_header (source, result);
      return result;
    }
    if (is_row_marker (source))
      throw std::runtime_error ("Equation marker outside supported row content at " +
                                path_text (where));
    const tree_label label= L (source);
    if (label == MID) {
      if (N (source) != 1 || !is_atomic (source[0]))
        throw std::runtime_error ("Invalid historical middle delimiter at " +
                                  path_text (where));
      const string identity= unicode_delimiter (source[0]);
      if (!math_v3::glyph (identity))
        throw std::runtime_error ("Invalid Unicode middle delimiter at " +
                                  path_text (where));
      // Historical MID is a *presentation* class, not proof that its glyph is
      // a delimiter. In A/B and A\B the slash is a lexical mathematical
      // operator, including when the old editor inserted MID for spacing or
      // resizing. Do not change the mathematical expression into a delimiter.
      // Vertical bars, in contrast, retain their explicit separator class
      // independent of an enclosing DELIMITED pair (e.g. f|_U or set-builder
      // notation). A separator is not an unmatched left/right bracket.
      tree result= (identity == "/" || identity == "\\") ?
        tree (identity) : tree (MATH_MIDDLE, tree (identity));
      inherited_header (source, result);
      return result;
    }
    if (label == AROUND || label == VAR_AROUND)
      return delimit (source, where, true);
    if (label == BIG) return operator_atom (source, where);
    if (label == BIG_AROUND) {
      if (N (source) != 2)
        throw std::runtime_error ("Invalid historical operator scope at " + path_text (where));
      tree old (CONCAT, tree (BIG, source[0]));
      if (is_func (source[1], CONCAT))
        for (int i=0; i<N (source[1]); ++i) old << source[1][i];
      else old << source[1];
      tree result= sequence (old, true, where);
      inherited_header (source, result);
      return result;
    }
    if (label == WIDE || label == VAR_WIDE) return accent (source, where);
    if (label == LONG_ARROW) return long_arrow (source, where);
    if (label == ABOVE || label == BELOW) return annotation (source, where);
    if (label == NAMED_SYMBOL) return named_symbol (source, where);
    if (is_compound (source, "math-alpha-bbb", 1) ||
        is_compound (source, "math-alpha-bold", 1) ||
        is_compound (source, "math-alpha-bold-up", 1) ||
        is_compound (source, "math-alpha-frak", 1) ||
        is_compound (source, "math-alpha-cal", 1) ||
        is_compound (source, "math-up", 1) ||
        is_compound (source, "math-it", 1) ||
        is_compound (source, "math-bf", 1) ||
        is_compound (source, "math-ss", 1) ||
        is_compound (source, "math-tt", 1) ||
        is_compound (source, "math-sl", 1) ||
        is_compound (source, "math-cal", 1))
      return variant (source, where);
    if (label == CONCAT) return sequence (source, math, where);
    if (label == MATH_SCRIPTS || label == DELIMITED || label == BIG_OPERATOR)
      return copy (source);
    if (is_compound (source, "equation", 1) ||
        is_compound (source, "equation*", 1) ||
        is_compound (source, "eqnarray", 1) ||
        is_compound (source, "eqnarray*", 1) ||
        is_compound (source, "align", 1) ||
        is_compound (source, "align*", 1))
      return display (source, where);
    if (math && (label == TFORMAT || label == TABLE))
      return grid (source, where);
    if (math && script_label (label)) {
      if (N (source) != 1)
        throw std::runtime_error ("Invalid historical script at " + path_text (where));
      // An entire <math><rsup>op</rsup></math> is an authored, standalone
      // mathematical modifier, such as ^{op} or ^{-1} in prose. A five-slot
      // MathScripts with an empty base is the *exact* v3 representation.
      // Inside an accent, evaluation bar, or other structural body, by
      // contrast, an orphan script can affect how that structure is laid out.
      if (!explicit_standalone_math)
        issue (where, "SCRIPT_WITHOUT_SEQUENCE_BASE",
               "Script is the body of a compound mathematical structure");
      source_path child_path= where; child_path.push_back (0);
      tree value= walk (source[0], true, child_path);
      return wrap_script (tree (""), source, value, script_slot (label), where);
    }
    const bool math_body= math || is_compound (source, "math", 1) ||
                          locally_enters_math_mode (source);
    const auto roles= athena::document_node::standard_source_role (source);
    array<tree> children;
    for (int i=0; i<N (source); ++i) {
      // Identifiers and string/URL arguments are data, not formula operands.
      // Preserve their exact tree and metadata even inside a mathematical row.
      const int field_type= standard_drd_for_thread ()->get_type_child (source, i);
      const bool data= math_body && roles && i < int (roles->children.size ()) &&
        roles->children[i] == athena::document_node::child_role::data;
      if (data || field_type == TYPE_IDENTIFIER || field_type == TYPE_STRING ||
          field_type == TYPE_URL) {
        children << copy (source[i]);
        continue;
      }
      bool content= math_body;
      if (label == WITH && i < N (source)-1) content= false;
      if (label == WIDE || label == VAR_WIDE)
        content= math_body && i == 0;
      if (label == NAMED_SYMBOL || label == BIG)
        content= false;
      source_path child_path= where; child_path.push_back (i);
      children << walk (source[i], content, child_path,
                       is_compound (source, "math", 1) && i == N (source)-1);
    }
    tree result (label, children);
    inherited_header (source, result);
    return result;
  }

  void rewrite_nested_metadata (tree& value, source_path path) {
    if (is_row_marker (value))
      throw std::runtime_error ("Unconverted equation row marker at " + path_text (path));
    if (const auto* present= athena::node::get (value)) {
      bool modified= false;
      athena::node::metadata replacement= *present;
      for (auto& [name, property]: replacement.properties) {
        (void) name;
        if (!has_rich_value (property)) continue;
        rewrite_property_rich (property, path);
        modified= true;
      }
      if (modified) athena::node::set (value, replacement);
    }
    if (is_compound (value))
      for (int i=0; i<N (value); ++i) {
        source_path child= path;
        child.push_back (i);
        rewrite_nested_metadata (value[i], std::move (child));
      }
  }

public:
  explicit converter (bool strict): strict_ (strict) {}

  math_v3_migration migrate (const tree& source) {
    report_.document= walk (source, false, {});
    // Rich trees in typed metadata are source objects too, even when nested
    // inside a dictionary/list. A v3 Vault must not hide v2 mathematics there.
    rewrite_nested_metadata (report_.document, {});
    std::set<std::string> old_ids, new_ids;
    source_ids (source, old_ids);
    source_ids (report_.document, new_ids);
    std::unordered_set<std::string> removed;
    for (const auto& id: old_ids)
      if (!new_ids.count (id)) {
        report_.removed_identities.push_back (id);
        removed.insert (id);
      }
    if (!removed.empty ())
      report_.document= prune_removed_reference_records (report_.document, removed);
    return std::move (report_);
  }
};

} // namespace

math_v3_migration migrate_math_v2_tree (const tree& source, bool strict) {
  return converter (strict).migrate (source);
}

void validate_cold_v3_migration (const tree& source) {
  check_cold_tree (source, "root");
}

} // namespace athena::document
