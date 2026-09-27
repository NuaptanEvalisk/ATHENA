/******************************************************************************
* MODULE     : enunciation_model.hpp
* DESCRIPTION: Declarative enunciations and explicit detached-source conversion
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_ENUNCIATION_MODEL_HPP
#define ATHENA_ENUNCIATION_MODEL_HPP

#include "Kernel/Types/node_metadata.hpp"

#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace athena::enunciation {

// Native extension label; automatic source creation remains separately gated.
tree_label label ();
bool is_enunciation (const tree& source);
// Structural/type validation, deliberately accepting unknown kinds/variants.
bool is_canonical (const tree& source);

struct render_metadata {
  std::string style;
  std::string counter;
  // complete/subject distinguish explicit legacy slots from body-title styles.
  std::string title_mode;
  std::string text;
  std::string number_format;
  bool body_only= false;
  bool qed= false;
};

struct variant_definition {
  std::string display_name;
  render_metadata render;
  nlohmann::json declaration;
};

struct kind_definition {
  std::string kind;
  std::string display_name;
  std::string role;
  std::string category;
  render_metadata render;
  std::map<std::string, variant_definition, std::less<>> variants;
  nlohmann::json declaration;
};

enum class legacy_layout { body, title_body, subject_body };

struct legacy_definition {
  std::string tag;
  std::string kind;
  std::string variant;
  bool numbered= false;
  // Nonempty means conversion needs this preference supplied by the caller.
  std::string numbering_preference;
  // Legacy extraction policy, not persistent identity. Empty means not indexed
  // by the pre-node-ID artifact extractor; canonical kinds use their schema.
  std::string artifact_base;
  legacy_layout layout= legacy_layout::body;
  render_metadata render;
  nlohmann::json declaration;
};

struct filter_entry {
  std::string label;
  std::string key;
};

class registry {
public:
  explicit registry (std::string_view json);
  const kind_definition* kind (std::string_view name) const;
  const legacy_definition* legacy (std::string_view tag) const;
  const kind_definition* definition (const tree& source) const;
  bool recognizes (const tree& source) const;
  std::string category (const tree& source) const;
  std::string role (const tree& source) const;
  std::string display_name (const tree& source) const;
  std::string kind_name (const tree& source) const;
  std::string variant_name (const tree& source) const;
  // -1 for unknown/malformed legacy shapes. Metadata does not shift indices.
  int body_index (const tree& source) const;
  bool matches_filter (const tree& source, std::string_view filter) const;
  std::vector<filter_entry> filters () const;
  // A source environment color, not a render-* helper. Empty for helpers.
  std::string color_kind (std::string_view source_tag) const;
  const render_metadata* rendering (const tree& source) const;
  // Includes unknown fields verbatim as JSON values; never rewritten on load.
  const nlohmann::json& declaration () const { return declaration_; }
  const std::map<std::string, kind_definition, std::less<>>& kinds () const {
    return kinds_;
  }
  const std::map<std::string, legacy_definition, std::less<>>& legacy_tags () const {
    return legacy_;
  }

private:
  nlohmann::json declaration_;
  std::map<std::string, kind_definition, std::less<>> kinds_;
  std::map<std::string, legacy_definition, std::less<>> legacy_;
};

// Lazy explicit access to $ATHENA_PATH/misc/enunciations.json. Throws on bad
// or missing data; there is no compiled fallback list or startup side effect.
const registry& standard_registry ();

struct conversion_options {
  std::map<std::string, bool> numbering_preferences;
};

enum class conversion_issue {
  invalid_shape, conflicting_property, missing_numbering_preference
};

struct conversion_diagnostic {
  std::vector<int> where;
  std::string tag;
  conversion_issue issue;
  std::string detail;
};

struct conversion_result {
  tree source;
  std::size_t converted= 0;
  std::vector<conversion_diagnostic> diagnostics;
};

// Explicit opt-in only, on an owner-local detached document body/fragment.
// Returns a deep copy, retaining node IDs and all typed properties. Never
// allocates IDs, edits live trees, consults preferences, or creates proof links.
// Canonical/unknown-kind enunciations, executable quotations/macros, malformed
// legacy nodes, and conflicting properties are preserved, not guessed at.
// The canonical body is one DOCUMENT child. Only explicit title/subject slots
// become name:rich_text; body text is never mined for names, authors or years.
// legacy-tag:string retains the original title mode and rendering provenance.
// Existing target:reference is retained; textual labels are not node IDs.
conversion_result convert_detached_source (
  const tree& source, const registry& declarations,
  const conversion_options& options= {});
conversion_result convert_detached_source (
  const tree& source, const conversion_options& options= {});

} // namespace athena::enunciation

#endif // ATHENA_ENUNCIATION_MODEL_HPP
