/******************************************************************************
* MODULE     : font_selection.cpp
* DESCRIPTION: Unicode-native font selection over ATHENA's shared font database
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "font_selection.hpp"
#include "font_database.hpp"
#include "font.hpp"
#include "Freetype/tt_face.hpp"
#include "Freetype/free_type.hpp"
#include <unicode/uchar.h>
#include <unicode/uscript.h>
#include <unicode/utf8.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace athena::text {
namespace {

void validate_request (const font_request& request) {
  require_utf8 (request.primary.file.file_utf8);
  require_utf8 (request.language);
  if (request.primary.file.file_utf8.empty () ||
      request.primary.file.file_utf8.find ('\0') != std::string::npos ||
      request.language.find ('\0') != std::string::npos ||
      request.primary.point_size <= 0 ||
      request.primary.horizontal_dpi <= 0 ||
      request.primary.vertical_dpi <= 0)
    throw std::invalid_argument ("Invalid font selection request");
}

bool is_variation_selector (char32_t scalar) {
  return (scalar >= 0xfe00 && scalar <= 0xfe0f) ||
         (scalar >= 0xe0100 && scalar <= 0xe01ef);
}

bool needs_font_glyph (char32_t scalar) {
  if (scalar == 0) return false;
  if (is_variation_selector (scalar)) return false;
  const auto type= u_charType (static_cast<UChar32> (scalar));
  if (type == U_CONTROL_CHAR || type == U_FORMAT_CHAR ||
      type == U_SURROGATE || type == U_UNASSIGNED)
    return false;
  return !u_hasBinaryProperty (
    static_cast<UChar32> (scalar), UCHAR_DEFAULT_IGNORABLE_CODE_POINT);
}

struct coverage_requirement {
  std::vector<char32_t> scalars;
  std::vector<std::pair<char32_t,char32_t>> variations;
};

coverage_requirement required_coverage (
    std::string_view source, std::size_t begin, std::size_t end) {
  coverage_requirement result;
  std::optional<char32_t> previous;
  int32_t at= static_cast<int32_t> (begin);
  while (at < static_cast<int32_t> (end)) {
    UChar32 scalar;
    U8_NEXT (source.data (), at, static_cast<int32_t> (end), scalar);
    if (scalar < 0) throw std::invalid_argument ("Invalid UTF-8 font slice");
    const auto value= static_cast<char32_t> (scalar);
    if (is_variation_selector (value)) {
      if (previous) result.variations.emplace_back (*previous, value);
      continue;
    }
    previous= value;
    if (!needs_font_glyph (value)) continue;
    if (std::find (result.scalars.begin (), result.scalars.end (), value) ==
        result.scalars.end ())
      result.scalars.push_back (value);
  }
  return result;
}

bool joining_sensitive (
    std::string_view source, std::size_t begin, std::size_t end) {
  int32_t at= static_cast<int32_t> (begin);
  while (at < static_cast<int32_t> (end)) {
    UChar32 scalar;
    U8_NEXT (source.data (), at, static_cast<int32_t> (end), scalar);
    if (scalar < 0) throw std::invalid_argument ("Invalid UTF-8 font slice");
    const auto joining= static_cast<UJoiningType> (
      u_getIntPropertyValue (scalar, UCHAR_JOINING_TYPE));
    if (joining == U_JT_JOIN_CAUSING || joining == U_JT_DUAL_JOINING ||
        joining == U_JT_LEFT_JOINING || joining == U_JT_RIGHT_JOINING)
      return true;
  }
  return false;
}

bool sequence_sensitive (
    std::string_view source, std::size_t begin, std::size_t end) {
  int scalars= 0;
  bool emoji= false;
  int32_t at= static_cast<int32_t> (begin);
  while (at < static_cast<int32_t> (end)) {
    UChar32 scalar;
    U8_NEXT (source.data (), at, static_cast<int32_t> (end), scalar);
    if (scalar < 0) throw std::invalid_argument ("Invalid UTF-8 font slice");
    ++scalars;
    emoji= emoji || scalar == 0x200d ||
      is_variation_selector (static_cast<char32_t> (scalar)) ||
      u_hasBinaryProperty (scalar, UCHAR_EMOJI) ||
      u_hasBinaryProperty (scalar, UCHAR_EMOJI_MODIFIER);
  }
  return scalars > 1 && emoji;
}

std::string cjk_preferred_family (const std::vector<char32_t>& scalars) {
  for (char32_t scalar: scalars) {
    UErrorCode status= U_ZERO_ERROR;
    const UScriptCode script= uscript_getScript (
      static_cast<UChar32> (scalar), &status);
    if (U_FAILURE (status)) continue;
    const char* range= nullptr;
    if (script == USCRIPT_HAN) range= "cjk";
    else if (script == USCRIPT_HIRAGANA || script == USCRIPT_KATAKANA)
      range= "hiragana";
    else if (script == USCRIPT_HANGUL) range= "hangul";
    if (range != nullptr) {
      string family= default_cjk_font_name_for_range (string (range));
      if (family != "")
        return std::string (family.data (), N(family));
    }
  }
  return {};
}

std::string unicode_range_feature (const std::vector<char32_t>& scalars) {
  for (char32_t scalar: scalars) {
    UErrorCode status= U_ZERO_ERROR;
    const UScriptCode script= uscript_getScript (
      static_cast<UChar32> (scalar), &status);
    if (U_FAILURE (status)) continue;
    if (script == USCRIPT_HAN) return "cjk";
    if (script == USCRIPT_HIRAGANA || script == USCRIPT_KATAKANA)
      return "hiragana";
    if (script == USCRIPT_HANGUL) return "hangul";
    if (script == USCRIPT_GREEK) return "greek";
    if (script == USCRIPT_CYRILLIC) return "cyrillic";
  }
  return {};
}

void append_unique (
    std::vector<std::string>& values, std::string value) {
  if (value.empty ()) return;
  if (std::find (values.begin (), values.end (), value) == values.end ())
    values.push_back (std::move (value));
}

std::vector<std::string>
policy_families (
    const font_request& request, const std::vector<char32_t>& scalars,
    const font_database_view& view) {
  std::vector<std::string> result;
  const std::string cjk= cjk_preferred_family (scalars);
  append_unique (result, cjk);

  if (!request.fallback.family.empty ()) {
    string family (request.fallback.family.data (), request.fallback.family.size ());
    string variant (request.fallback.variant.data (), request.fallback.variant.size ());
    string series (request.fallback.series.data (), request.fallback.series.size ());
    string shape (request.fallback.shape.data (), request.fallback.shape.size ());
    const std::string range= unicode_range_feature (scalars);
    if (!range.empty ()) {
      string range_variant (range.data (), range.size ());
      if (variant != "" && variant != "rm")
        range_variant= variant * "-" * range_variant;
      array<string> logical=
        logical_font (family, range_variant, series, shape);
      logical= apply_substitutions (logical);
      if (N(logical) > 0)
        append_unique (
          result, std::string (logical[0].data (), N(logical[0])));
    }
    array<string> logical= logical_font (family, variant, series, shape);
    logical= apply_substitutions (logical);
    if (N(logical) > 0) {
      append_unique (
        result, std::string (logical[0].data (), N(logical[0])));
      bool sans= false, mono= false;
      for (int i=1; i<N(logical); ++i) {
        sans= sans || logical[i] == "sansserif";
        mono= mono || logical[i] == "typewriter" || logical[i] == "mono";
      }
      const char* generic= mono ? "monospace" : sans ? "sans-serif" : "serif";
      auto found= view.generic_families.find (generic);
      if (found != view.generic_families.end ())
        append_unique (result, found->second);
    }
  }
  return result;
}

bool contains_family (
    const font_database_face& face, std::string_view family) {
  const auto key= font_database_family_key (family);
  return std::any_of (
    face.families.begin (), face.families.end (),
    [&] (const std::string& current) {
      return font_database_family_key (current) == key;
    });
}

} // namespace

struct font_catalog::impl {
  font_domain& owner= current_font_domain ();
  std::shared_ptr<const font_database_view> view= font_database_native_view ();
  std::map<std::string,tt_face> loaded_faces;
  std::unordered_map<std::string,std::size_t> fallback_cache;

  void check () const {
    owner.check_owner ();
    if (&current_font_domain () != &owner)
      throw std::logic_error ("Font catalog belongs to another font domain");
  }

  void synchronize_view () {
    auto current= font_database_native_view ();
    if (current->generation == view->generation) return;
    view= std::move (current);
    fallback_cache.clear ();
  }

  tt_face face (const font_file_source& source) {
    const std::string key= font_database_physical_key (source);
    auto found= loaded_faces.find (key);
    if (found != loaded_faces.end ()) return found->second;
    tt_face result= load_tt_face (source);
    loaded_faces.emplace (key, result);
    return result;
  }

  bool covers (const font_file_source& source,
               const coverage_requirement& required) {
    if (required.scalars.empty () && required.variations.empty ()) return true;
    const auto known= view->physical_faces.find (font_database_face_key (source));
    if (known != view->physical_faces.end () && required.variations.empty ()) {
      const auto& metadata= view->faces[known->second];
      return std::all_of (
        required.scalars.begin (), required.scalars.end (),
        [&] (char32_t scalar) {
          return font_database_supports (metadata, scalar);
        });
    }
    tt_face physical= face (source);
    if (physical->bad_face) return false;
    for (char32_t scalar: required.scalars)
      if (ft_get_char_index (
            physical->ft_face, static_cast<unsigned int> (scalar)) == 0)
        return false;
    for (const auto& variation: required.variations) {
      const FT_Int is_default= FT_Face_GetCharVariantIsDefault (
        physical->ft_face, static_cast<FT_ULong> (variation.first),
        static_cast<FT_ULong> (variation.second));
      if (is_default < 0) return false;
      if (is_default == 0 &&
          FT_Face_GetCharVariantIndex (
            physical->ft_face, static_cast<FT_ULong> (variation.first),
            static_cast<FT_ULong> (variation.second)) == 0)
        return false;
    }
    return true;
  }

  bool supports_sequence (
      const font_file_source& source, const font_request& request,
      std::string_view text, std::size_t begin, std::size_t end,
      std::string_view script) {
    if (!sequence_sensitive (text, begin, end)) return true;
    shaping_options options;
    options.script.assign (script.data (), script.size ());
    options.language= request.language;
    options.features= request.features;
    options.max_glyphs= 64;
    const auto shaped= shape_freetype_utf8 (
      source, request.primary.point_size,
      request.primary.horizontal_dpi, request.primary.vertical_dpi,
      text, begin, end, options);
    if (shaped.missing_glyphs || shaped.glyphs.empty ()) return false;
    const std::size_t cluster= shaped.glyphs.front ().byte;
    return std::all_of (
      shaped.glyphs.begin (), shaped.glyphs.end (),
      [cluster] (const positioned_glyph& glyph) {
        return glyph.index != 0 && glyph.byte == cluster;
      });
  }

  std::optional<std::size_t> record_for (const font_file_source& source) const {
    const auto found= view->physical_faces.find (font_database_face_key (source));
    if (found == view->physical_faces.end ()) return std::nullopt;
    return found->second;
  }

  long long score_candidate (
      std::size_t candidate, const font_request& request,
      const std::vector<char32_t>& scalars,
      const std::optional<std::size_t>& primary_record,
      const std::vector<std::string>& policy) const {
    const auto& face= view->faces[candidate];
    long long score= static_cast<long long> (candidate);
    for (std::size_t i=0; i<policy.size (); ++i)
      if (contains_family (face, policy[i])) {
        score-= 2000000000LL -
                static_cast<long long> (i) * 10000000LL;
        break;
      }
    if (primary_record) {
      const auto& primary= view->faces[*primary_record];
      bool same_family= false;
      for (const auto& family: primary.families)
        if (contains_family (face, family)) {
          same_family= true;
          break;
        }
      if (!same_family) score+= 1000000000LL;
      score+= 1000LL * std::llabs (
        static_cast<long long> (face.weight) - primary.weight);
      score+= 100LL * std::llabs (
        static_cast<long long> (face.width) - primary.width);
      if (face.slant != primary.slant) score+= 100000LL;
      if (face.spacing != primary.spacing) score+= 1000000LL;
    }
    const bool emoji= std::any_of (
      scalars.begin (), scalars.end (), [] (char32_t scalar) {
        return u_hasBinaryProperty (
          static_cast<UChar32> (scalar), UCHAR_EMOJI);
      });
    if (emoji && face.color) score-= 500000000LL;
    (void) request;
    (void) scalars;
    return score;
  }

  std::optional<font_file_source> fallback (
      const font_request& request, const coverage_requirement& required,
      std::string_view text= {}, std::size_t begin= 0, std::size_t end= 0,
      std::string_view script= {}) {
    if (required.scalars.empty () && required.variations.empty ())
      return request.primary.file;
    const auto& scalars= required.scalars;
    std::string key= std::to_string (view->generation) + ":" +
                     font_database_physical_key (request.primary.file);
    const auto append_key= [&] (std::string_view value) {
      key+= ":" + std::to_string (value.size ()) + ":";
      key.append (value.data (), value.size ());
    };
    append_key (request.fallback.family);
    append_key (request.fallback.variant);
    append_key (request.fallback.series);
    append_key (request.fallback.shape);
    append_key (request.language);
    for (char32_t scalar: scalars)
      key+= ":" + std::to_string (static_cast<std::uint32_t> (scalar));
    for (const auto& variation: required.variations)
      key+= ":v" + std::to_string (
               static_cast<std::uint32_t> (variation.first)) +
             "-" + std::to_string (
               static_cast<std::uint32_t> (variation.second));
    if (!text.empty () && begin < end && sequence_sensitive (text, begin, end)) {
      key+= ":seq:" + std::to_string (end - begin) + ":";
      key.append (text.data () + begin, end - begin);
    }
    auto cached= fallback_cache.find (key);
    if (cached != fallback_cache.end ()) {
      if (cached->second == std::numeric_limits<std::size_t>::max ())
        return std::nullopt;
      return view->faces[cached->second].file;
    }

    const auto primary_record= record_for (request.primary.file);
    const auto policy= policy_families (request, scalars, *view);
    const std::string primary_key= font_database_face_key (request.primary.file);
    std::vector<std::size_t> preferred;
    const auto add_family= [&] (std::string_view family) {
      const auto found= view->families.find (font_database_family_key (family));
      if (found == view->families.end ()) return;
      for (std::size_t candidate: found->second)
        if (std::find (preferred.begin (), preferred.end (), candidate) ==
            preferred.end ())
          preferred.push_back (candidate);
    };
    for (const auto& family: policy) add_family (family);
    if (primary_record)
      for (const auto& family: view->faces[*primary_record].families)
        add_family (family);

    std::optional<std::size_t> best;
    long long best_score= std::numeric_limits<long long>::max ();
    const auto consider= [&] (std::size_t candidate) {
      const auto& face= view->faces[candidate];
      if (font_database_face_key (face.file) == primary_key) return;
      if (!std::all_of (
            scalars.begin (), scalars.end (),
            [&] (char32_t scalar) {
              return font_database_supports (face, scalar);
            }))
        return;
      if (!required.variations.empty () && !covers (face.file, required))
        return;
      if (!text.empty () && begin < end &&
          !supports_sequence (
            face.file, request, text, begin, end, script))
        return;
      const long long score=
        score_candidate (candidate, request, scalars, primary_record, policy);
      if (!best || score < best_score) {
        best= candidate;
        best_score= score;
      }
    };
    for (std::size_t candidate: preferred) consider (candidate);
    if (best) {
      if (fallback_cache.size () >= 4096) fallback_cache.clear ();
      fallback_cache.emplace (std::move (key), *best);
      return view->faces[*best].file;
    }

    struct fallback_pool {
      const std::vector<std::size_t>* page= nullptr;
      const font_database_page_shortlist* shortlist= nullptr;
      std::uint32_t offset= 0;
      std::size_t count= std::numeric_limits<std::size_t>::max ();
    } pool;
    for (char32_t scalar: scalars) {
      const std::uint32_t page=
        static_cast<std::uint32_t> (scalar) & ~std::uint32_t (0xff);
      const auto found= view->unicode_pages.find (page);
      if (found == view->unicode_pages.end ()) {
        if (fallback_cache.size () >= 4096) fallback_cache.clear ();
        fallback_cache.emplace (
          std::move (key), std::numeric_limits<std::size_t>::max ());
        return std::nullopt;
      }
      const std::uint32_t offset=
        static_cast<std::uint32_t> (scalar) - page;
      const auto hot= view->unicode_shortlists.find (page);
      const std::size_t count=
        hot == view->unicode_shortlists.end () ?
          found->second.size () :
          hot->second.offsets[offset + 1] - hot->second.offsets[offset];
      if (count == 0) {
        if (fallback_cache.size () >= 4096) fallback_cache.clear ();
        fallback_cache.emplace (
          std::move (key), std::numeric_limits<std::size_t>::max ());
        return std::nullopt;
      }
      if (count < pool.count) {
        pool.page= &found->second;
        pool.shortlist=
          hot == view->unicode_shortlists.end () ? nullptr : &hot->second;
        pool.offset= offset;
        pool.count= count;
      }
    }
    if (pool.page == nullptr && !required.variations.empty ()) {
      const std::uint32_t base= static_cast<std::uint32_t> (
        required.variations.front ().first);
      const std::uint32_t page= base & ~std::uint32_t (0xff);
      const auto found= view->unicode_pages.find (page);
      if (found != view->unicode_pages.end ()) {
        pool.page= &found->second;
        const auto hot= view->unicode_shortlists.find (page);
        pool.shortlist=
          hot == view->unicode_shortlists.end () ? nullptr : &hot->second;
        pool.offset= base - page;
        pool.count= pool.shortlist == nullptr ?
          pool.page->size () :
          pool.shortlist->offsets[pool.offset + 1] -
            pool.shortlist->offsets[pool.offset];
      }
    }
    if (pool.page == nullptr) return std::nullopt;
    constexpr std::size_t max_global_candidates= 256;
    if (pool.shortlist != nullptr) {
      const std::uint32_t begin= pool.shortlist->offsets[pool.offset];
      const std::uint32_t end= pool.shortlist->offsets[pool.offset + 1];
      for (std::uint32_t at= begin; at<end; ++at)
        consider (pool.shortlist->faces[at]);
    }
    else {
      std::size_t checked= 0;
      for (std::size_t candidate: *pool.page) {
        if (checked++ >= max_global_candidates) break;
        consider (candidate);
      }
    }
    if (fallback_cache.size () >= 4096) fallback_cache.clear ();
    fallback_cache.emplace (
      std::move (key),
      best ? *best : std::numeric_limits<std::size_t>::max ());
    if (!best) return std::nullopt;
    return view->faces[*best].file;
  }
};

font_catalog::font_catalog (): state_ (std::make_unique<impl> ()) {}
font_catalog::~font_catalog () { state_->owner.check_owner (); }

font_request font_request_with_italic (font_request request, bool italic) {
  validate_request (request);
  if (!request.fallback.family.empty ())
    request.fallback.shape= italic ? "italic" : "right";
  auto view= font_database_native_view ();
  const auto source= view->physical_faces.find (
    font_database_face_key (request.primary.file));
  if (source == view->physical_faces.end ()) return request;
  const auto& base= view->faces[source->second];

  std::vector<std::string> family_keys;
  for (const auto& family: base.families) {
    array<string> logical;
    logical << string (family.data (), family.size ())
            << string (italic ? "italic" : "upright");
    logical= apply_substitutions (logical);
    if (N(logical) > 0)
      family_keys.push_back (
        font_database_family_key (
          std::string (logical[0].data (), N(logical[0]))));
    family_keys.push_back (font_database_family_key (family));
  }
  std::optional<std::size_t> best;
  long long best_score= std::numeric_limits<long long>::max ();
  for (std::size_t family_rank=0; family_rank<family_keys.size (); ++family_rank) {
    const auto members= view->families.find (family_keys[family_rank]);
    if (members == view->families.end ()) continue;
    for (std::size_t candidate: members->second) {
      const auto& face= view->faces[candidate];
      const int slant_penalty= italic ?
        (face.slant == 1 ? 0 : face.slant == 2 ? 1 : 100) :
        (face.slant == 0 ? 0 : 100);
      const long long score=
        static_cast<long long> (family_rank) * 1000000000LL +
        static_cast<long long> (slant_penalty) * 10000000LL +
        1000LL * std::llabs (static_cast<long long> (face.weight) - base.weight) +
        100LL * std::llabs (static_cast<long long> (face.width) - base.width) +
        (face.spacing == base.spacing ? 0 : 1000000LL) +
        static_cast<long long> (candidate);
      if (!best || score < best_score) {
        best= candidate;
        best_score= score;
      }
    }
  }
  if (!best) return request;
  const auto selected= view->faces[*best].file;
  if (font_database_face_key (selected) ==
      font_database_face_key (request.primary.file)) {
    request.primary.file.file_utf8= selected.file_utf8;
    request.primary.file.face_index= selected.face_index;
  }
  else request.primary.file= selected;
  return request;
}

std::vector<selected_font_run> font_catalog::select (
    const std::string& source, const font_request& request, std::uint8_t base_level,
    const std::vector<font_style_span>& styles,
    const std::vector<script_run>* script_ranges) {
  state_->check ();
  state_->synchronize_view ();
  require_utf8 (source);
  validate_request (request);
  if (source.size () > 16 * 1024 * 1024 || base_level > 1)
    throw std::invalid_argument ("Invalid font selection request");
  if (styles.size () > 100000) throw std::length_error ("Too many font style spans");
  std::size_t previous= 0;
  for (const auto& span: styles) {
    validate_request (span.request);
    if (span.begin < previous || span.begin >= span.end || span.end > source.size () ||
        !scalar_boundary (source, span.begin) || !scalar_boundary (source, span.end) ||
        span.request.direction != request.direction)
      throw std::invalid_argument ("Invalid paragraph font style range");
    previous= span.end;
  }
  std::vector<selected_font_run> result;
  if (source.empty ()) return result;
  if (request.math_variant != math_alphabet::normal ||
      std::any_of (styles.begin (), styles.end (), [] (const font_style_span& span) {
        return span.request.math_variant != math_alphabet::normal;
      })) {
    // Itemize the glyph-selection projection, not the plain source letters:
    // fallback must cover the actual mathematical alphabet. Map all results
    // back to original bytes before publishing any font run or caret geometry.
    std::string rendered;
    std::vector<std::size_t> originals {0}, projected {0};
    std::size_t style= 0;
    for (int32_t at= 0; at<static_cast<int32_t> (source.size ());) {
      while (style < styles.size () && styles[style].end <= static_cast<std::size_t> (at)) ++style;
      const auto& active= style < styles.size () && styles[style].begin <= static_cast<std::size_t> (at) ?
        styles[style].request : request;
      UChar32 c;
      U8_NEXT (source.data (), at, static_cast<int32_t> (source.size ()), c);
      const auto glyph= math_variant_character (c, active.math_variant);
      char encoded[4];
      int32_t length= 0;
      U8_APPEND_UNSAFE (encoded, length, glyph);
      rendered.append (encoded, length);
      originals.push_back (at);
      projected.push_back (rendered.size ());
    }
    const auto translate= [] (std::size_t position, const auto& from, const auto& to) {
      const auto found= std::lower_bound (from.begin (), from.end (), position);
      if (found == from.end () || *found != position)
        throw std::logic_error ("Math font projection splits a Unicode scalar");
      return to[found - from.begin ()];
    };
    auto plain= request;
    plain.math_variant= math_alphabet::normal;
    auto projected_styles= styles;
    for (auto& span: projected_styles) {
      span.begin= translate (span.begin, originals, projected);
      span.end= translate (span.end, originals, projected);
      span.request.math_variant= math_alphabet::normal;
    }
    for (auto run: select (rendered, plain, base_level, projected_styles, nullptr)) {
      run.begin= translate (run.begin, projected, originals);
      const auto end= translate (run.end, projected, originals);
      while (run.begin < end) {
        const auto span= std::lower_bound (styles.begin (), styles.end (), run.begin,
          [] (const font_style_span& s, std::size_t at) { return s.end <= at; });
        const bool inside= span != styles.end () && span->begin <= run.begin;
        run.end= span == styles.end () ? end : std::min (end, inside ? span->end : span->begin);
        run.math_variant= inside ? span->request.math_variant : request.math_variant;
        result.push_back (run);
        run.begin= run.end;
      }
    }
    return result;
  }
  const auto locate_style= [&] (std::size_t byte) {
    return std::lower_bound (styles.begin (), styles.end (), byte,
      [] (const font_style_span& span, std::size_t at) { return span.end <= at; });
  };
  const auto append= [&] (
      std::size_t begin, std::size_t end, const font_request& active,
      const font_file_source& font) {
    if (!result.empty () && begin > 0 &&
        source[begin - 1] != '\0' && source[begin] != '\0' &&
        result.back ().end == begin &&
        result.back ().font.file_utf8 == font.file_utf8 &&
        result.back ().font.face_index == font.face_index &&
        result.back ().font.design_coords == font.design_coords &&
        result.back ().point_size == active.primary.point_size &&
        result.back ().language == active.language &&
        result.back ().horizontal_dpi == active.primary.horizontal_dpi &&
        result.back ().vertical_dpi == active.primary.vertical_dpi &&
        result.back ().math_variant == active.math_variant &&
        result.back ().features == active.features)
      result.back ().end= end;
    else result.push_back ({
      begin, end, font, active.primary.point_size, active.language,
      active.primary.horizontal_dpi, active.primary.vertical_dpi,
      active.math_variant, active.features});
  };
  std::optional<unicode_paragraph> local_analysis;
  if (script_ranges == nullptr) {
    local_analysis.emplace (source, request.direction, request.language);
    script_ranges= &local_analysis->scripts ();
  }
  grapheme_cursor graphemes (source);
  auto script= script_ranges->begin ();
  for (std::size_t at= 0; at<source.size ();) {
    const auto span= locate_style (at);
    const bool inside= span != styles.end () && span->begin <= at;
    const auto& active= inside ? span->request : request;
    const std::size_t style_end=
      span == styles.end () ? source.size () :
      (inside ? span->end : span->begin);
    while (script != script_ranges->end () && script->end <= at) ++script;
    const std::size_t script_end=
      script == script_ranges->end () ? source.size () : script->end;
    const std::size_t segment_end= std::min (style_end, script_end);
    if (segment_end <= at || segment_end > source.size ())
      throw std::logic_error ("Font selection segment did not advance");

    if (joining_sensitive (source, at, segment_end)) {
      const auto required= required_coverage (source, at, segment_end);
      font_file_source selected= active.primary.file;
      if (!state_->covers (selected, required) ||
          !state_->supports_sequence (
            selected, active, source, at, segment_end,
            script == script_ranges->end () ? std::string_view () :
              std::string_view (script->script))) {
        auto fallback= state_->fallback (
          active, required, source, at, segment_end,
          script == script_ranges->end () ? std::string_view () :
            std::string_view (script->script));
        if (fallback) selected= std::move (*fallback);
      }
      append (at, segment_end, active, selected);
      at= segment_end;
      continue;
    }

    while (at < segment_end) {
      std::size_t next= source[at] == '\0' ? at + 1 :
        std::min (graphemes.next (at), segment_end);
      if (next <= at || next > source.size ())
        throw std::logic_error ("Font selection did not advance");
      const auto required= required_coverage (source, at, next);
      font_file_source selected= active.primary.file;
      if (!state_->covers (selected, required) ||
          !state_->supports_sequence (
            selected, active, source, at, next,
            script == script_ranges->end () ? std::string_view () :
              std::string_view (script->script))) {
        auto fallback= state_->fallback (
          active, required, source, at, next,
          script == script_ranges->end () ? std::string_view () :
            std::string_view (script->script));
        if (fallback) selected= std::move (*fallback);
      }
      append (at, next, active, selected);
      at= next;
    }
  }
  (void) base_level;
  return result;
}

font_catalog& current_font_catalog () {
  return font_domain_local<font_catalog> ();
}

font_request font_request_from_source (const physical_font_source& source,
                                       std::string language) {
  font_request request;
  request.primary= source;
  request.language= std::move (language);
  validate_request (request);
  return request;
}

font_paragraph::font_paragraph (std::string source, font_request request, font_catalog& catalog):
  font_paragraph (std::move (source), std::move (request), {}, catalog) {}

font_paragraph::font_paragraph (std::string source, font_request request,
                                const std::vector<font_style_span>& styles, font_catalog& catalog):
  source_ (std::move (source)), analysis_ (source_, request.direction, request.language),
  request_ (std::move (request)), styles_ (styles),
  fonts_ (catalog.select (
    source_, request_, analysis_.base_level (), styles, &analysis_.scripts ())),
  owner_ (&current_font_domain ()) {}

unicode_paragraph& font_paragraph::analysis () {
  owner_->check_owner ();
  if (owner_ != &current_font_domain ())
    throw std::logic_error ("Paragraph belongs to another font domain");
  return analysis_;
}

shaped_line font_paragraph::line (std::size_t begin, std::size_t end,
                                 const shaping_options& options, double horizontal_scale,
                                 const item_splitter& split) {
  if (!std::isfinite (horizontal_scale) || horizontal_scale <= 0)
    throw std::invalid_argument ("Invalid horizontal font scale");
  auto locate= [&] (std::size_t byte) {
    return std::lower_bound (fonts_.begin (), fonts_.end (), byte,
      [] (const selected_font_run& run, std::size_t at) { return run.end <= at; });
  };
  return shape_line (analysis (), begin, end,
    [&] (std::string_view source, const shaping_item& item, const shaping_options& o) {
      const auto font= locate (item.run.begin);
      if (font == fonts_.end () || font->begin > item.run.begin || font->end < item.run.end)
        throw std::logic_error ("Shaping item crosses selected font boundary");
      auto selected= o;
      selected.math_variant= font->math_variant;
      for (const auto& feature: font->features) {
        auto existing= std::find_if (
          selected.features.begin (), selected.features.end (),
          [&] (const open_type_feature& current) {
            return current.tag == feature.tag;
          });
        if (existing == selected.features.end ())
          selected.features.push_back (feature);
        else *existing= feature;
      }
      if (selected.language.empty () || selected.language == "und") selected.language= font->language;
      const double scaled= std::round (font->horizontal_dpi * horizontal_scale);
      if (!std::isfinite (scaled) || scaled < 1 || scaled > std::numeric_limits<int>::max ())
        throw std::invalid_argument ("Invalid horizontal font scale");
      return shape_freetype_utf8 (font->font, font->point_size,
        static_cast<int> (scaled), font->vertical_dpi, source, item.run.begin, item.run.end, selected);
    }, options, [&] (const shaping_item& item) {
      std::vector<std::size_t> cuts;
      for (auto font= locate (item.run.begin); font != fonts_.end () && font->end < item.run.end; ++font)
        cuts.push_back (font->end);
      if (split) {
        auto extra= split (item);
        std::size_t previous= item.run.begin;
        for (const auto byte: extra) {
          if (byte <= previous || byte >= item.run.end || !scalar_boundary (source_, byte))
            throw std::invalid_argument ("Invalid additional shaping boundary");
          previous= byte;
        }
        cuts.insert (cuts.end (), extra.begin (), extra.end ());
        std::sort (cuts.begin (), cuts.end ());
        cuts.erase (std::unique (cuts.begin (), cuts.end ()), cuts.end ());
      }
      return cuts;
    });
}

} // namespace athena::text
