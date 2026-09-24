/******************************************************************************
* MODULE     : legacy_document_import.cpp
* DESCRIPTION: Role-aware Cork tree import preserving symbols and source positions
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "legacy_document_import.hpp"
#include "legacy_document_reader.hpp"
#include "drd_std.hpp"
#include "drd_info.hpp"
#include "file.hpp"
#include "analyze.hpp"
#include <algorithm>
#include <limits>
#include <memory>
#include <set>

namespace athena::document {
namespace {
std::string bytes (const string& s) { return {as_charp (s), static_cast<std::size_t> (N (s))}; }
tree atom (const std::string& s) {
  if (s.size () > static_cast<std::size_t> (std::numeric_limits<int>::max ()))
    throw codec_exception (codec_error::resource_limit, "Imported atom too large");
  return tree (string (s.data (), static_cast<int> (s.size ())));
}
document_path child_path (document_path path, int index) { path.push_back (index); return path; }

legacy_text_role role_for_type (int type, legacy_text_role inherited) {
  switch (type) {
  case TYPE_CODE: return legacy_text_role::code;
  case TYPE_VARIABLE: case TYPE_ARGUMENT: case TYPE_BINDING:
  case TYPE_IDENTIFIER: case TYPE_URL:
  case TYPE_BOOLEAN: case TYPE_INTEGER: case TYPE_LENGTH:
  case TYPE_NUMERIC: case TYPE_COLOR: case TYPE_DURATION:
  case TYPE_FONT_SIZE: case TYPE_GRAPHICAL_ID:
    return legacy_text_role::identifier;
  case TYPE_STRING: return legacy_text_role::scalar;
  default: return inherited;
  }
}

legacy_text_role standard_role (const tree& parent, int i, legacy_text_role inherited) {
  // A code block or scalar container does not turn ordinary descendants into
  // presentation text. Explicit child types can further restrict a field.
  if (L (parent) < START_EXTENSIONS) {
    return role_for_type (
      standard_drd_for_thread ()->get_type_child (parent, i), inherited);
  }
  return inherited;
}

tree find_preamble (const tree& t) {
  if (is_atomic (t)) return "";
  if ((is_compound (t, "hide-preamble", 1) ||
       is_compound (t, "show-preamble", 1))) return t[0];
  for (int i=0; i<N(t); ++i) {
    tree found= find_preamble (t[i]);
    if (found != "") return found;
  }
  return "";
}

tree find_document_field (const tree& document, string label) {
  if (!is_func (document, DOCUMENT)) return "";
  for (int i=0; i<N(document); ++i)
    if (is_compound (document[i], label, 1)) return document[i][0];
  return "";
}

class contract_compiler {
  drd_info drd;
  hashmap<string,tree> environment;
  std::vector<tree> properties;
  std::set<std::string> loaded;
  legacy_import_limits limits;
  std::optional<std::filesystem::path> document_path;

  url resolve_package (string package,
                       const std::optional<std::filesystem::path>& base) {
    string filename= ends (package, ".ts") ? package : package * ".ts";
    const std::filesystem::path requested (
      std::string (filename.data (), (std::size_t) N(filename)));
    bool safe_relative= !requested.empty () && !requested.is_absolute ();
    for (const auto& component: requested)
      if (component == "..") safe_relative= false;
    if (!safe_relative) return url_none ();
    if (base) {
      url base_url= url_system (string (base->string ().c_str ()));
      url local= resolve (expand (head (base_url) * url_ancestor () * filename));
      if (!is_none (local)) return local;
    }
    return resolve (url ("$ATHENA_STYLE_PATH") * filename);
  }

  void load_package (string package,
                     const std::optional<std::filesystem::path>& base) {
    if (loaded.size () >= 128)
      throw codec_exception (codec_error::resource_limit,
                             "Too many legacy style dependencies");
    url resolved= resolve_package (package, base);
    if (is_none (resolved)) return;
    string key= as_string (resolved, URL_SYSTEM);
    std::string identity (key.data (), (std::size_t) N(key));
    if (!loaded.insert (identity).second) return;
    string source;
    if (load_string (resolved, source, false)) return;
    if ((std::size_t) N(source) > limits.codec.input_bytes)
      throw codec_exception (codec_error::resource_limit,
                             "Legacy style contract exceeds import budget");
    tree package_tree= read_legacy_markup (
      std::string_view (as_charp (source), (std::size_t) N(source)), limits.codec);
    std::optional<std::filesystem::path> package_path;
    if (N(key) != 0) package_path= std::filesystem::path (identity);
    scan (package_tree, package_path);
  }

  void scan (const tree& t, const std::optional<std::filesystem::path>& base) {
    if (is_atomic (t)) return;
    if (is_func (t, MACRO) || is_func (t, XMACRO)) return;
    if ((is_func (t, ASSIGN, 2) || is_func (t, PROVIDE, 2)) &&
        is_atomic (t[0])) {
      string name= t[0]->label;
      if (is_func (t, ASSIGN) || !environment->contains (name))
        environment (name)= t[1];
      return;
    }
    if (is_func (t, DRD_PROPS)) {
      properties.push_back (copy (t));
      return;
    }
    if (is_func (t, USE_PACKAGE)) {
      for (int i=0; i<N(t); ++i)
        if (is_atomic (t[i])) load_package (t[i]->label, base);
      return;
    }
    for (int i=0; i<N(t); ++i) scan (t[i], base);
  }

public:
  contract_compiler (const tree& source, legacy_import_limits import_limits,
                     const legacy_import_context& context):
    drd ("legacy-import", standard_drd_for_thread ()),
    environment (UNINIT), limits (import_limits), document_path (context.source_path) {
    tree style= find_document_field (source, "style");
    if (is_atomic (style) && style != "") load_package (style->label, document_path);
    else if (is_tuple (style))
      for (int i=0; i<N(style); ++i)
        if (is_atomic (style[i])) load_package (style[i]->label, document_path);
    tree preamble= find_preamble (source);
    if (preamble != "") scan (preamble, document_path);
    // Explicit contracts freeze their slots before heuristic inference so a
    // macro body cannot weaken a declared scalar/code/identifier role.
    for (const auto& property: properties) apply_drd_properties (drd, property);
    drd->heuristic_init (environment);
  }

  legacy_text_role role (const tree& parent, int child,
                         legacy_text_role inherited) {
    return role_for_type (drd->get_type_child (parent, child), inherited);
  }
};

legacy_slot_policy compile_contract_policy (
  const tree& source, legacy_import_limits limits,
  const legacy_import_context& context, const legacy_slot_policy& caller) {
  auto contracts= std::make_shared<contract_compiler> (source, limits, context);
  return [contracts, caller] (const tree& parent, int child,
                              legacy_text_role inherited) {
    auto role= contracts->role (parent, child, inherited);
    return caller ? caller (parent, child, role) : role;
  };
}

class importer {
  const legacy_cork_table& table;
  codec_limits limits;
  std::size_t position_limit, positions= 0;
  const legacy_slot_policy& policy;
  std::size_t nodes= 0, input_bytes= 0, output_bytes= 0;
  std::vector<legacy_node_mapping> mappings;

  void count (std::size_t n, std::size_t& used, std::size_t max) {
    if (used > max || n > max - used)
      throw codec_exception (codec_error::resource_limit, "Legacy document exceeds import budget");
    used += n;
  }
  tree text (const tree& source, const document_path& old, const document_path& dest,
             legacy_text_role role, bool extensible_delimiter) {
    const auto raw= bytes (source->label);
    count (raw.size (), input_bytes, limits.input_bytes);
    std::string_view encoded= raw;
    // Legacy around* applies make_large before typeset_large: each removes
    // one wrapper. Thus <<less>> in a delimiter slot denotes <less>, not a
    // malformed content token. Do not apply this rule to ordinary text.
    const bool wrapped= extensible_delimiter && raw.size () >= 5 &&
      raw.compare (0, 2, "<<") == 0 && raw.compare (raw.size () - 2, 2, ">>") == 0 &&
      raw.find_first_of ("<>", 2) == raw.size () - 2;
    if (wrapped) encoded= encoded.substr (1, encoded.size () - 2);
    std::vector<legacy_text_piece> pieces;
    try {
      pieces= table.decode (encoded, role, limits.text_bytes - output_bytes,
                            position_limit - positions);
    }
    catch (const legacy_text_error& error) {
      throw legacy_text_error (error.byte + (wrapped ? 1 : 0), error.what ());
    }
    if (wrapped) {
      for (auto& piece: pieces) { ++piece.begin; ++piece.end; }
      pieces.front ().begin= 0;
      pieces.back ().end= raw.size ();
    }
    count (std::max<std::size_t> (pieces.size (), 1), positions, position_limit);
    const bool structured= std::any_of (pieces.begin (), pieces.end (), [] (const auto& p) {
      return p.kind == legacy_piece_kind::named_symbol;
    });
    legacy_node_mapping map {old, dest, {}};
    tree result (CONCAT);
    std::string run;
    for (const auto& p: pieces) {
      count (p.value.size (), output_bytes, limits.text_bytes);
      document_path target= structured ? child_path (dest, N (result)) : dest;
      if (p.kind == legacy_piece_kind::named_symbol) {
        if (!run.empty ()) {
          result << atom (run); run.clear (); target= child_path (dest, N (result));
          count (1, nodes, limits.nodes);
        }
        result << compound ("named-symbol", atom (p.value));
        count (2, nodes, limits.nodes);
        map.spans.push_back ({p.begin, p.end, {target, 0}, {target, 1}});
      }
      else {
        auto begin= run.size ();
        run += p.value;
        map.spans.push_back ({p.begin, p.end, {target, begin}, {target, run.size ()}, p.byte_identity});
      }
    }
    if (!structured) result= atom (run);
    else if (!run.empty ()) { result << atom (run); count (1, nodes, limits.nodes); }
    // Empty text still has a relocatable position, including empty arguments.
    if (pieces.empty ()) map.spans.push_back ({0, 0, {dest, 0}, {dest, 0}});
    mappings.push_back (std::move (map));
    return result;
  }

  tree visit (const tree& source, const document_path& old, const document_path& dest,
              legacy_text_role role, std::size_t depth, bool extensible_delimiter= false) {
    if (depth > limits.depth) throw codec_exception (codec_error::resource_limit, "Legacy tree too deep");
    count (1, nodes, limits.nodes);
    if (is_atomic (source)) {
      try { return text (source, old, dest, role, extensible_delimiter); }
      catch (const legacy_text_error& error) { throw legacy_document_error (old, error); }
    }
    if (is_func (source, RAW_DATA)) {
      if (N (source) != 1 || !is_atomic (source[0]))
        throw codec_exception (codec_error::invalid_structure, "Invalid legacy binary payload");
      count (N (source[0]->label), input_bytes, limits.input_bytes);
      count (N (source[0]->label), output_bytes, limits.text_bytes);
      count (1, nodes, limits.nodes);
      mappings.push_back ({old, dest, {}});
      // Payload positions are byte identities, never text cursor offsets.
      mappings.push_back ({child_path (old, 0), child_path (dest, 0), {}});
      return copy (source);
    }
    std::string name;
    auto old_name= bytes (as_string (L (source)));
    count (old_name.size (), input_bytes, limits.input_bytes);
    for (const auto& p: table.decode (old_name, legacy_text_role::identifier,
                                     limits.text_bytes - output_bytes)) name += p.value;
    count (name.size (), output_bytes, limits.text_bytes);
    tree result (make_tree_label (atom (name)->label), 0);
    mappings.push_back ({old, dest, {}});
    for (int i= 0; i < N (source); ++i) {
      auto from= child_path (old, i);
      bool version= is_compound (source[i], "TeXmacs", 1) && is_atomic (source[i][0]);
      if ((is_func (source[i], APPLY, 2) || is_func (source[i], EXPAND, 2)) &&
          source[i][0] == "TeXmacs" && is_atomic (source[i][1])) version= true;
      if (depth == 0 && version) {
        count (2, nodes, limits.nodes);
        count (N (source[i][N (source[i]) - 1]->label), input_bytes, limits.input_bytes);
        mappings.push_back ({std::move (from), std::nullopt, {}});
        continue;
      }
      auto child_role= standard_role (source, i, role);
      if (policy) child_role= policy (source, i, child_role);
      const bool delimiter= is_func (source, VAR_AROUND, 3) && (i == 0 || i == 2);
      result << visit (source[i], from, child_path (dest, N (result)), child_role,
                       depth + 1, delimiter);
    }
    return result;
  }
public:
  importer (const legacy_cork_table& t, legacy_import_limits l, const legacy_slot_policy& p):
    table (t), limits (l.codec), position_limit (l.positions), policy (p) {}
  legacy_document_result run (const tree& source) {
    if (!is_func (source, DOCUMENT))
      throw codec_exception (codec_error::invalid_structure, "Expected legacy document envelope");
    init_std_drd ();
    auto result= visit (source, {}, {}, legacy_text_role::content, 0);
    // Enforce the actual output shape/size, including symbol-induced nodes.
    write_xml (result, xml_kind::document, limits);
    return {std::move (result), std::move (mappings)};
  }
};
} // namespace

std::optional<document_position> legacy_document_result::relocate (
  const document_path& path, std::size_t byte, boundary_affinity affinity) const {
  const auto found= std::lower_bound (mappings.begin (), mappings.end (), path,
    [] (const auto& map, const auto& key) { return map.source < key; });
  if (found != mappings.end () && found->source == path && found->destination) {
    const auto& map= *found;
    std::optional<document_position> before, after;
    for (const auto& span: map.spans) {
      if (span.byte_identity && byte > span.begin && byte < span.end)
        return document_position {span.first.node, span.first.offset + byte - span.begin};
      if (span.end == byte) before= span.last;
      if (span.begin == byte) after= span.first;
    }
    if (affinity == boundary_affinity::preceding) return before ? before : after;
    return after ? after : before;
  }
  return std::nullopt;
}

std::optional<document_path> legacy_document_result::relocate_node (const document_path& path) const {
  const auto found= std::lower_bound (mappings.begin (), mappings.end (), path,
    [] (const auto& map, const auto& key) { return map.source < key; });
  if (found == mappings.end () || found->source != path) return std::nullopt;
  return found->destination;
}

legacy_document_result import_legacy_document (
  const tree& source, const legacy_cork_table& table, legacy_import_limits limits,
  const legacy_slot_policy& policy, const legacy_import_context& context) {
  auto effective= compile_contract_policy (source, limits, context, policy);
  return importer (table, limits, effective).run (source);
}

legacy_document_result import_legacy_document_bytes (
  std::string_view input, const legacy_cork_table& table, legacy_import_limits limits,
  const legacy_slot_policy& policy, const legacy_import_context& context) {
  tree source;
  if (input.substr (0, 9) == "<TeXmacs|") source= read_legacy_markup (input, limits.codec);
  else source= read_legacy_scheme (input, limits.codec);
  return import_legacy_document (source, table, limits, policy, context);
}
} // namespace athena::document
