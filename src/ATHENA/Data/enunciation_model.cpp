/******************************************************************************
* MODULE     : enunciation_model.cpp
* DESCRIPTION: Declarative enunciations and explicit detached-source conversion
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/enunciation_model.hpp"
#include "file.hpp"
#include "url.hpp"

#include <stdexcept>
#include <utility>

namespace athena::enunciation {
namespace {

using json= nlohmann::json;
constexpr std::size_t maximum_registry_bytes= 1024 * 1024;

std::string native_string (const string& value) {
  return std::string (value.data (), N(value));
}

std::string tag_name (const tree& source) {
  return is_compound (source) ? native_string (as_string (L(source))) : "";
}

void require (bool condition, const std::string& message) {
  if (!condition) throw std::invalid_argument ("Enunciation registry: " + message);
}

std::string text_field (const json& object, const char* key,
                        bool optional= false) {
  const auto found= object.find (key);
  if (found == object.end () && optional) return {};
  require (found != object.end () && found->is_string (),
           std::string ("expected string field ") + key);
  auto result= found->get<std::string> ();
  require ((optional || !result.empty ()) && result.size () <= 1024 &&
           result.find ('\0') == std::string::npos,
           std::string ("invalid string field ") + key);
  return result;
}

bool bool_field (const json& object, const char* key) {
  const auto found= object.find (key);
  require (found != object.end () && found->is_boolean (),
           std::string ("expected boolean field ") + key);
  return found->get<bool> ();
}

template<typename T> const T* property (const tree& source, const char* key) {
  const auto* metadata= node::get (source);
  if (!metadata) return nullptr;
  const auto found= metadata->properties.find (key);
  return found == metadata->properties.end () ? nullptr :
    std::get_if<T> (&found->second.data);
}

template<typename T> bool optional_property (const tree& source, const char* key) {
  const auto* metadata= node::get (source);
  return !metadata || metadata->properties.count (key) == 0 ||
    property<T> (source, key) != nullptr;
}

node::metadata detached_metadata (const tree& source) {
  node::metadata result;
  if (const auto* metadata= node::get (source)) {
    result.id= metadata->id;
    for (const auto& entry: metadata->properties)
      result.properties.emplace (entry.first, node::copy_property (entry.second));
  }
  return result;
}

bool add_property (node::metadata& metadata, const std::string& key,
                   node::property value) {
  const auto found= metadata.properties.find (key);
  if (found != metadata.properties.end ()) return node::equal (found->second, value);
  metadata.properties.emplace (key, std::move (value));
  return true;
}

class transformer {
  const registry& declarations;
  const conversion_options& options;
  conversion_result result;
  std::vector<int> where;

  tree unchanged (const tree& source, conversion_issue issue,
                  const std::string& detail) {
    result.diagnostics.push_back ({where, tag_name (source), issue, detail});
    return copy (source);
  }

  tree visit (const tree& source) {
    if (!is_compound (source)) return copy (source);
    if (is_enunciation (source)) {
      if (!is_canonical (source))
        return unchanged (source, conversion_issue::invalid_shape,
                          "Expected one DOCUMENT body and typed properties");
      tree target (label (), 1);
      node::copy_metadata (source, target);
      where.push_back (0);
      target[0]= visit (source[0]);
      where.pop_back ();
      return target;
    }
    if (is_func (source, MACRO) || is_func (source, XMACRO) ||
        is_func (source, QUOTE) || is_func (source, QUASI) ||
        is_func (source, QUASIQUOTE)) return copy (source);

    const auto* legacy= declarations.legacy (tag_name (source));
    if (!legacy) {
      tree target (L(source), N(source));
      if (node::get (source)) node::set (target, detached_metadata (source));
      for (int i= 0; i < N(source); ++i) {
        where.push_back (i);
        target[i]= visit (source[i]);
        where.pop_back ();
      }
      return target;
    }

    const int body_index= legacy->layout == legacy_layout::body ? 0 : 1;
    if (N(source) != body_index + 1)
      return unchanged (source, conversion_issue::invalid_shape,
                        "Legacy argument count does not match declaration");

    bool numbered= legacy->numbered;
    if (!legacy->numbering_preference.empty ()) {
      const auto preference= options.numbering_preferences.find (
        legacy->numbering_preference);
      if (preference == options.numbering_preferences.end ())
        return unchanged (source, conversion_issue::missing_numbering_preference,
                          legacy->numbering_preference);
      numbered= preference->second;
    }

    node::metadata metadata= detached_metadata (source);
    const auto set= [&] (const char* key, node::property::value value) {
      return add_property (metadata, key, node::property (std::move (value)));
    };
    if (!set ("kind", legacy->kind) || !set ("numbered", numbered) ||
        !set ("legacy-tag", legacy->tag) ||
        (!legacy->variant.empty () && !set ("variant", legacy->variant)) ||
        !optional_property<std::string> (source, "variant") ||
        !optional_property<node::reference> (source, "target") ||
        !optional_property<node::rich_text> (source, "name"))
      return unchanged (source, conversion_issue::conflicting_property,
                        "Existing property conflicts with legacy declaration");

    // A title argument is source content, not a prefix to strip or re-parse.
    if (body_index != 0) {
      if (!set ("name", node::rich_text {copy (source[0])}))
        return unchanged (source, conversion_issue::conflicting_property,
                          "Existing name differs from explicit legacy title");
    }
    else if (metadata.properties.count ("name") == 0)
      set ("name", node::rich_text {tree ("")});

    where.push_back (body_index);
    tree body= visit (source[body_index]);
    where.pop_back ();
    if (!is_func (body, DOCUMENT)) body= tree (DOCUMENT, body);
    tree target (label (), body);
    node::set (target, metadata);
    ++result.converted;
    return target;
  }

public:
  transformer (const registry& declarations2, const conversion_options& options2):
    declarations (declarations2), options (options2) {}

  conversion_result convert (const tree& source) {
    result.source= visit (source);
    return std::move (result);
  }
};

} // namespace

tree_label label () { return make_tree_label ("enunciation"); }

bool is_enunciation (const tree& source) {
  return is_compound (source) && L(source) == label ();
}

bool is_canonical (const tree& source) {
  const auto* kind= property<std::string> (source, "kind");
  return is_enunciation (source) && N(source) == 1 &&
    is_func (source[0], DOCUMENT) && kind && !kind->empty () &&
    property<node::rich_text> (source, "name") &&
    property<bool> (source, "numbered") &&
    optional_property<std::string> (source, "variant") &&
    optional_property<node::reference> (source, "target");
}

registry::registry (std::string_view source) {
  require (source.size () <= maximum_registry_bytes, "size limit exceeded");
  declaration_= json::parse (source.begin (), source.end ());
  require (declaration_.is_object () && declaration_.contains ("version") &&
           declaration_["version"].is_number_integer () &&
           declaration_["version"] == 1 && declaration_.contains ("kinds") &&
           declaration_["kinds"].is_array (), "unsupported schema");

  for (const auto& entry: declaration_["kinds"]) {
    require (entry.is_object (), "expected kind object");
    kind_definition definition;
    definition.kind= text_field (entry, "kind");
    definition.display_name= text_field (entry, "display_name");
    definition.role= text_field (entry, "role");
    definition.category= text_field (entry, "category");
    require (entry.contains ("render") && entry["render"].is_object (),
             "expected render object");
    const auto& render= entry["render"];
    definition.render.style= text_field (render, "style");
    definition.render.counter= text_field (render, "counter", true);
    definition.render.title_mode= text_field (render, "title_mode");
    definition.render.qed= bool_field (render, "qed");
    if (entry.contains ("variants")) {
      require (entry["variants"].is_object (), "expected variants object");
      for (const auto& variant: entry["variants"].items ()) {
        require (!variant.key ().empty () && variant.key ().size () <= 1024 &&
                 variant.key ().find ('\0') == std::string::npos &&
                 variant.value ().is_object (), "invalid variant declaration");
        variant_definition value;
        value.display_name= text_field (variant.value (), "display_name");
        value.render= definition.render;
        value.render.style= text_field (variant.value (), "render_style");
        value.declaration= variant.value ();
        definition.variants.emplace (variant.key (), std::move (value));
      }
    }
    definition.declaration= entry;
    require (kinds_.emplace (definition.kind, definition).second,
             "duplicate kind " + definition.kind);
    require (entry.contains ("legacy") && entry["legacy"].is_array (),
             "expected legacy array");

    for (const auto& alias: entry["legacy"]) {
      require (alias.is_object (), "expected legacy object");
      legacy_definition legacy;
      legacy.tag= text_field (alias, "tag");
      require (legacy.tag != "enunciation", "canonical tag cannot be a legacy alias");
      legacy.kind= definition.kind;
      legacy.variant= text_field (alias, "variant", true);
      legacy.numbered= bool_field (alias, "numbered");
      legacy.numbering_preference= text_field (alias, "numbering_preference", true);
      legacy.artifact_base= text_field (alias, "artifact_base", true);
      const auto layout= text_field (alias, "layout", true);
      require (layout.empty () || layout == "body" || layout == "title-body" ||
               layout == "subject-body", "unknown legacy layout");
      if (layout == "title-body") legacy.layout= legacy_layout::title_body;
      if (layout == "subject-body") legacy.layout= legacy_layout::subject_body;
      legacy.render= definition.render;
      if (!legacy.variant.empty ()) {
        const auto variant= definition.variants.find (legacy.variant);
        require (variant != definition.variants.end (), "undeclared legacy variant");
        legacy.render= variant->second.render;
      }
      if (legacy.layout == legacy_layout::title_body)
        legacy.render.title_mode= "complete";
      if (legacy.layout == legacy_layout::subject_body)
        legacy.render.title_mode= "subject";
      if (alias.contains ("render_style"))
        legacy.render.style= text_field (alias, "render_style");
      legacy.declaration= alias;
      require (legacy_.emplace (legacy.tag, legacy).second,
               "duplicate legacy tag " + legacy.tag);

      // Star support is declared per family, never guessed for arbitrary tags.
      if (alias.contains ("starred_tag")) {
        legacy.tag= text_field (alias, "starred_tag");
        require (legacy.tag != "enunciation", "canonical tag cannot be a starred alias");
        legacy.numbered= false;
        legacy.numbering_preference.clear ();
        legacy.artifact_base= text_field (alias, "starred_artifact_base", true);
        require (legacy_.emplace (legacy.tag, legacy).second,
                 "duplicate starred tag " + legacy.tag);
      }
    }
  }
}

const kind_definition* registry::kind (std::string_view name) const {
  const auto found= kinds_.find (name);
  return found == kinds_.end () ? nullptr : &found->second;
}

const legacy_definition* registry::legacy (std::string_view tag) const {
  const auto found= legacy_.find (tag);
  return found == legacy_.end () ? nullptr : &found->second;
}

const kind_definition* registry::definition (const tree& source) const {
  if (is_enunciation (source)) {
    const auto* name= property<std::string> (source, "kind");
    return name ? kind (*name) : nullptr;
  }
  const auto* alias= legacy (tag_name (source));
  return alias ? kind (alias->kind) : nullptr;
}

bool registry::recognizes (const tree& source) const {
  return is_enunciation (source) || legacy (tag_name (source)) != nullptr;
}

std::string registry::kind_name (const tree& source) const {
  if (is_enunciation (source)) {
    const auto* name= property<std::string> (source, "kind");
    return name ? *name : "";
  }
  const auto* alias= legacy (tag_name (source));
  return alias ? alias->kind : "";
}

std::string registry::variant_name (const tree& source) const {
  if (is_enunciation (source)) {
    const auto* name= property<std::string> (source, "variant");
    return name ? *name : "";
  }
  const auto* alias= legacy (tag_name (source));
  return alias ? alias->variant : "";
}

int registry::body_index (const tree& source) const {
  if (is_enunciation (source))
    return N(source) == 1 && is_func (source[0], DOCUMENT) ? 0 : -1;
  const auto* alias= legacy (tag_name (source));
  if (!alias) return -1;
  const int index= alias->layout == legacy_layout::body ? 0 : 1;
  return N(source) == index + 1 ? index : -1;
}

bool registry::matches_filter (const tree& source, std::string_view filter) const {
  if (!recognizes (source) || body_index (source) < 0) return false;
  const auto name= kind_name (source);
  if (filter.empty () || filter == name) return true;
  const auto* alias= legacy (filter);
  if (!alias || alias->kind != name) return false;
  if (!alias->variant.empty ()) return variant_name (source) == alias->variant;
  // Explicit unnumbered aliases remain available to callers with saved filters.
  if (!filter.empty () && filter.back () == '*') {
    if (is_enunciation (source)) {
      const auto* numbered= property<bool> (source, "numbered");
      return numbered && !*numbered;
    }
    return tag_name (source) == filter;
  }
  return true;
}

std::vector<filter_entry> registry::filters () const {
  std::vector<filter_entry> result;
  for (const auto& entry: declaration_["kinds"]) {
    const auto* value= kind (entry["kind"].get<std::string> ());
    result.push_back ({value->display_name, value->kind});
    for (const auto& variant: value->variants) {
      // Use a declared source alias, not an invented kind/variant spelling.
      for (const auto& alias: legacy_) {
        if (alias.second.kind == value->kind && alias.second.variant == variant.first &&
            alias.second.layout == legacy_layout::body) {
          result.push_back ({variant.second.display_name, alias.first});
          break;
        }
      }
    }
  }
  return result;
}

std::string registry::color_kind (std::string_view source_tag) const {
  const auto* alias= legacy (source_tag);
  if (!alias || alias->layout == legacy_layout::title_body) return "";
  return alias->kind;
}

std::string registry::category (const tree& source) const {
  const auto* value= definition (source);
  return value ? value->category : "";
}

std::string registry::role (const tree& source) const {
  const auto* value= definition (source);
  return value ? value->role : "";
}

std::string registry::display_name (const tree& source) const {
  const auto* value= definition (source);
  if (!value) return "";
  std::string variant;
  if (is_enunciation (source)) {
    if (const auto* name= property<std::string> (source, "variant")) variant= *name;
  }
  else if (const auto* alias= legacy (tag_name (source))) variant= alias->variant;
  const auto found= value->variants.find (variant);
  return found == value->variants.end () ? value->display_name : found->second.display_name;
}

const render_metadata* registry::rendering (const tree& source) const {
  if (!is_enunciation (source)) {
    const auto* alias= legacy (tag_name (source));
    return alias ? &alias->render : nullptr;
  }
  const auto* value= definition (source);
  if (!value) return nullptr;
  const auto* variant= property<std::string> (source, "variant");
  const std::string variant_name= variant ? *variant : "";
  if (const auto* tag= property<std::string> (source, "legacy-tag")) {
    const auto* alias= legacy (*tag);
    if (alias && alias->kind == value->kind && alias->variant == variant_name)
      return &alias->render;
  }
  const auto found= value->variants.find (variant_name);
  if (found != value->variants.end ()) return &found->second.render;
  return &value->render;
}

const registry& standard_registry () {
  static const registry declarations= [] {
    string source;
    if (load_string (url ("$ATHENA_PATH/misc/enunciations.json"), source, false))
      throw std::runtime_error ("Cannot read $ATHENA_PATH/misc/enunciations.json");
    return registry (std::string_view (source.data (), N(source)));
  } ();
  return declarations;
}

conversion_result convert_detached_source (
    const tree& source, const registry& declarations,
    const conversion_options& options) {
  return transformer (declarations, options).convert (source);
}

conversion_result convert_detached_source (
    const tree& source, const conversion_options& options) {
  return convert_detached_source (source, standard_registry (), options);
}

} // namespace athena::enunciation
