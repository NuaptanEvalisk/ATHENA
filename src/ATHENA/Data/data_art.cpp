/******************************************************************************
* MODULE     : data_art.cpp
* DESCRIPTION: Deterministic DataArt cover generation
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "data_art.hpp"
#include "VTK/vtk_surface_renderer.hpp"
#include "actor_transport.hpp"
#include "convert.hpp"
#include "new_buffer.hpp"
#include "new_view.hpp"
#include "new_window.hpp"
#include "scheme_execution_context.hpp"
#include "boot.hpp"
#include "buffer_actor.hpp"
#include "buffer_state.hpp"
#include "node_metadata.hpp"
#include "file.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QMetaObject>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t grid_size= 130;
constexpr double grid_minimum= -5.0;
constexpr double grid_maximum= 5.0;
constexpr double pi= 3.141592653589793238462643383279502884;
// Render on a modest staging canvas and crop the final PNG around the actual
// surface.  This keeps DataArt large on the page without clipping view-angle
// extremes just to eliminate empty background.
constexpr int image_width= 1800;
constexpr int image_height= 900;
constexpr double surface_camera_zoom= 1.20;

enum class SurfaceKind { algebraic, fourier, radial };

struct DataArtParameters {
  SurfaceKind surface= SurfaceKind::algebraic;
  std::array<double, 9> algebraic_coefficients {};
  std::array<std::array<double, 4>, 5> fourier_terms {};
  std::array<double, 5> radial_coefficients {};
  std::array<double, 3> background {};
  int elevation= 30;
  int azimuth= 0;
  std::size_t palette= 0;
};

#include "data_art_palettes.inc"

using Palette= std::array<std::uint32_t, 256>;

constexpr std::array<const Palette*, 9> palettes {
  &palette_viridis,
  &palette_plasma,
  &palette_inferno,
  &palette_magma,
  &palette_cividis,
  &palette_coolwarm,
  &palette_ocean,
  &palette_gist_earth,
  &palette_cubehelix
};

std::string
to_std (string value) {
  return std::string (as_charp (value), (std::size_t) N(value));
}

string
to_tm (const std::string& value) {
  return string (value.data (), (int) value.size ());
}

string
export_snapshot_on_gui_owner (
  url source, url output, tree document, tree body,
  athena::document::xml_format_version version) {
  if (current_scheme_execution_context () != nullptr)
    return "DataArt transient export entered outside the GUI owner";

  url transient= url_none ();
  try {
    tree covered_document= change_doc_attr (
      std::move (document), "body", std::move (body));
    transient= make_new_buffer ();
    // Match buffer-copy's ownership/order: initialize a real view/editor first,
    // then install the source document state.  Non-headless view initialization
    // derives editor state from the initial scratch document, so populating the
    // document before view creation can later erase/reinitialize that state.
    (void) get_new_view (transient);
    if (!replace_buffer_tree (transient, std::move (covered_document), version))
      throw std::runtime_error ("DataArt snapshot replacement rejected");
    // Relative links, transclusions and other document-context lookups must
    // resolve exactly as they do for the source buffer.
    set_master_buffer (transient, source);
    bool failed= buffer_export (transient, output, "pdf");
    kill_buffer (transient);
    transient= url_none ();
    return failed ? "DataArt transient PDF export failed" : string ("");
  }
  catch (...) {
    if (!is_none (transient)) {
      try { kill_buffer (transient); }
      catch (...) {}
    }
    return "DataArt transient PDF export threw an exception";
  }
}

bool
is_continuation (unsigned char value) {
  return (value & 0xc0u) == 0x80u;
}

// The retired Python implementation read its seed with UTF-8 errors="ignore".
// Preserve that byte-level compatibility for legacy TeXmacs/Cork strings.
std::string
utf8_ignore_invalid (std::string_view input) {
  std::string output;
  output.reserve (input.size ());
  std::size_t i= 0;
  while (i < input.size ()) {
    const unsigned char first= (unsigned char) input[i];
    if (first <= 0x7fu) {
      output.push_back ((char) first);
      ++i;
      continue;
    }

    std::size_t length= 0;
    std::uint32_t codepoint= 0;
    std::uint32_t minimum= 0;
    if (first >= 0xc2u && first <= 0xdfu) {
      length= 2; codepoint= first & 0x1fu; minimum= 0x80u;
    }
    else if (first >= 0xe0u && first <= 0xefu) {
      length= 3; codepoint= first & 0x0fu; minimum= 0x800u;
    }
    else if (first >= 0xf0u && first <= 0xf4u) {
      length= 4; codepoint= first & 0x07u; minimum= 0x10000u;
    }
    else {
      ++i;
      continue;
    }

    if (i + length > input.size ()) {
      ++i;
      continue;
    }
    bool valid= true;
    for (std::size_t j=1; j<length; ++j) {
      const unsigned char byte= (unsigned char) input[i + j];
      if (!is_continuation (byte)) { valid= false; break; }
      codepoint= (codepoint << 6) | (byte & 0x3fu);
    }
    if (!valid || codepoint < minimum || codepoint > 0x10ffffu ||
        (codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
      ++i;
      continue;
    }
    output.append (input.substr (i, length));
    i += length;
  }
  return output;
}

double
unit_byte (const QByteArray& digest, std::size_t index) {
  return (unsigned char) digest[(qsizetype) index] / 255.0;
}

double
unit_word (const QByteArray& digest, std::size_t index) {
  const std::uint32_t value=
    ((std::uint32_t) (unsigned char) digest[(qsizetype) index] << 8) |
    (std::uint32_t) (unsigned char) digest[(qsizetype) index + 1];
  return value / 65535.0;
}

DataArtParameters
parameters_for_seed (string seed) {
  const std::string sanitized= utf8_ignore_invalid (to_std (seed));
  const QByteArray bytes (sanitized.data (), (qsizetype) sanitized.size ());
  const QByteArray digest= QCryptographicHash::hash (
    bytes, QCryptographicHash::Sha256);

  DataArtParameters parameters;
  parameters.surface= (SurfaceKind) ((unsigned char) digest[0] % 3u);
  if (parameters.surface == SurfaceKind::algebraic) {
    for (std::size_t i=0; i<parameters.algebraic_coefficients.size (); ++i)
      parameters.algebraic_coefficients[i]=
        unit_byte (digest, 1 + i) * 4.0 - 2.0;
  }
  else if (parameters.surface == SurfaceKind::fourier) {
    for (std::size_t i=0; i<parameters.fourier_terms.size (); ++i) {
      const std::size_t offset= 1 + i * 4;
      parameters.fourier_terms[i]= {
        unit_byte (digest, offset) * 0.55 + 0.18,
        unit_byte (digest, offset + 1) * 4.0 - 2.0,
        unit_byte (digest, offset + 2) * 4.0 - 2.0,
        unit_byte (digest, offset + 3) * 2.0 * pi
      };
    }
  }
  else {
    for (std::size_t i=0; i<parameters.radial_coefficients.size (); ++i)
      parameters.radial_coefficients[i]=
        unit_word (digest, 1 + i * 2) * 4.0 - 2.0;
  }

  parameters.background= {
    unit_byte (digest, 24) * 0.16 + 0.82,
    unit_byte (digest, 25) * 0.16 + 0.82,
    unit_byte (digest, 26) * 0.16 + 0.82
  };
  parameters.elevation= (int) (unit_byte (digest, 27) * 46.0) + 14;
  parameters.azimuth= (int) (unit_word (digest, 28) * 360.0);
  parameters.palette=
    (std::size_t) ((int) (unit_byte (digest, 30) * palettes.size ())) %
    palettes.size ();
  return parameters;
}

double
surface_value (const DataArtParameters& parameters, double x, double y) {
  if (parameters.surface == SurfaceKind::algebraic) {
    const auto& c= parameters.algebraic_coefficients;
    return c[0] + c[1] * x + c[2] * y + c[3] * x * x + c[4] * y * y +
           c[5] * x * y + c[6] * x * x * x + c[7] * y * y * y +
           c[8] * x * y * y;
  }
  if (parameters.surface == SurfaceKind::fourier) {
    double z= 0.0;
    for (const auto& term: parameters.fourier_terms)
      z += term[0] * std::sin (term[1] * x + term[2] * y + term[3]);
    return z;
  }
  const auto& c= parameters.radial_coefficients;
  const double radius= std::sqrt (x * x + y * y);
  const double theta= std::atan2 (y, x);
  return c[0] * std::tanh (c[1] * radius) *
         std::cos (c[2] * theta + c[3] * radius + c[4]);
}

athena::vtk_render::SurfaceRenderRequest
build_request (const DataArtParameters& parameters) {
  athena::vtk_render::SurfaceRenderRequest request;
  request.rows= grid_size;
  request.columns= grid_size;
  request.x_coordinates.resize (grid_size);
  request.y_coordinates.resize (grid_size);
  request.z_values.resize (grid_size * grid_size);
  request.cell_scalars.resize ((grid_size - 1) * (grid_size - 1));

  const double step=
    (grid_maximum - grid_minimum) / (double) (grid_size - 1);
  for (std::size_t i=0; i<grid_size; ++i) {
    request.x_coordinates[i]= grid_minimum + step * (double) i;
    request.y_coordinates[i]= grid_minimum + step * (double) i;
  }
  for (std::size_t row=0; row<grid_size; ++row)
    for (std::size_t column=0; column<grid_size; ++column) {
      const std::size_t index= row * grid_size + column;
      request.z_values[index]= surface_value (
        parameters, request.x_coordinates[column], request.y_coordinates[row]);
    }
  for (std::size_t row=0; row+1<grid_size; ++row)
    for (std::size_t column=0; column+1<grid_size; ++column) {
      const std::size_t top_left= row * grid_size + column;
      const std::size_t top_right= top_left + 1;
      const std::size_t bottom_left= top_left + grid_size;
      const std::size_t bottom_right= bottom_left + 1;
      request.cell_scalars[row * (grid_size - 1) + column]=
        0.25 * (request.z_values[top_left] + request.z_values[top_right] +
                request.z_values[bottom_left] + request.z_values[bottom_right]);
    }

  request.palette= *palettes[parameters.palette];
  request.background= parameters.background;
  request.box_aspect= {16.0, 7.0, 5.0};
  request.elevation_degrees= (double) parameters.elevation;
  request.azimuth_degrees= (double) parameters.azimuth;
  request.camera_zoom= surface_camera_zoom;
  request.surface_alpha= 0.96;
  request.crop_to_content= true;
  request.crop_margin_fraction= 0.06;
  request.width= image_width;
  request.height= image_height;
  const double viewport_width= (double) image_height / (double) image_width;
  request.viewport= {
    0.5 * (1.0 - viewport_width), 0.0,
    0.5 * (1.0 + viewport_width), 1.0
  };
  return request;
}

} // namespace

string
athena_data_art_generate (string seed, url output) {
  const std::filesystem::path path (to_std (concretize (output)));
  if (path.empty ()) return "DataArt output path is empty";

  std::error_code ec;
  if (!path.parent_path ().empty ()) {
    std::filesystem::create_directories (path.parent_path (), ec);
    if (ec)
      return to_tm (
        "could not create DataArt output directory: " + ec.message ());
  }

  const DataArtParameters parameters= parameters_for_seed (seed);
  const athena::vtk_render::SurfaceRenderRequest request=
    build_request (parameters);
  std::string error;
  if (!athena::vtk_render::render_surface_png (request, path, error))
    return to_tm (error.empty () ? "DataArt rendering failed" : error);
  return "";
}

int
athena_data_art_source_version () {
  const auto* context= current_scheme_execution_context ();
  if (!context || !context->actor || !context->actor->current_state ())
    throw std::runtime_error ("DataArt snapshot requires its source actor");
  const auto version= context->actor->current_state ()->storage_version;
  int number;
  switch (version) {
  case athena::document::xml_format_version::v1: number= 1; break;
  case athena::document::xml_format_version::v2: number= 2; break;
  case athena::document::xml_format_version::v3: number= 3; break;
  default: throw std::runtime_error ("Unknown DataArt source version");
  }
  return number;
}

string
athena_data_art_export_snapshot (
  url source, url output, tree document, tree body, int source_version) {
  using athena::document::xml_format_version;
  xml_format_version version;
  switch (source_version) {
  case 1: version= xml_format_version::v1; break;
  case 2: version= xml_format_version::v2; break;
  case 3: version= xml_format_version::v3; break;
  default: return "Unknown DataArt snapshot source version";
  }
  if (current_scheme_execution_context () == nullptr)
    return export_snapshot_on_gui_owner (
      std::move (source), std::move (output), std::move (document),
      std::move (body), version);

  // A headless caller such as website generation has the Qt owner waiting for
  // its source BufferActor. Asking that actor to synchronously re-enter Qt
  // would deadlock; headless orchestration must call this function globally
  // after first obtaining its detached snapshot from the source actor.
  if (headless_mode)
    return "DataArt headless export requires global snapshot orchestration";

  QCoreApplication* app= QCoreApplication::instance ();
  if (app == nullptr)
    return "DataArt transient export has no Qt application";

  athena_blob_id document_id=
    actor_tree_registry::instance ().store (std::move (document));
  athena_blob_id body_id=
    actor_tree_registry::instance ().store (std::move (body));
  std::string source_text= to_std (as_string (source));
  std::string output_text= to_std (as_string (output));
  std::string result;

  bool invoked= QMetaObject::invokeMethod (
    app,
    [document_id, body_id, version, source_text= std::move (source_text),
     output_text= std::move (output_text), &result] {
      tree document2= actor_tree_registry::instance ().take (document_id);
      tree body2= actor_tree_registry::instance ().take (body_id);
      string error= export_snapshot_on_gui_owner (
        url (to_tm (source_text)), url (to_tm (output_text)),
        std::move (document2), std::move (body2), version);
      result= to_std (error);
    },
    Qt::BlockingQueuedConnection);
  if (!invoked) {
    (void) actor_tree_registry::instance ().discard (document_id);
    (void) actor_tree_registry::instance ().discard (body_id);
    return "DataArt transient export could not enter the Qt owner";
  }
  return to_tm (result);
}

namespace {

tree cover_document (url cover, string width) {
  return tree (DOCUMENT, tree (VAR_VSPACE, "1fn"),
               compound ("center", tree (IMAGE, as_string (cover), width, "", "", "")),
               tree (VSPACE, "1.5fn"));
}

bool is_cover_document (tree t, string width) {
  return is_func (t, DOCUMENT, 3) && is_func (t[0], VAR_VSPACE, 1) && t[0][0] == "1fn" &&
    is_func (t[1], make_tree_label ("center"), 1) && is_compound (t[1][0]) &&
    L(t[1][0]) == IMAGE && N(t[1][0]) >= 2 && t[1][0][1] == width &&
    is_func (t[2], VSPACE, 1) && t[2][0] == "1.5fn";
}

bool cover_present (tree body) {
  if (is_atomic (body) || L(body) != DOCUMENT) return false;
  for (int i=0; i<N(body); ++i) {
    if (is_cover_document (body[i], "0.92par")) return true;
    if (!is_func (body[i], make_tree_label ("doc-data"))) continue;
    for (int j=0; j<N(body[i]); ++j) {
      tree child= body[i][j];
      if (is_func (child, make_tree_label ("doc-misc")) &&
          is_cover_document (child[0], "0.72par")) return true;
    }
  }
  return false;
}

tree insert_cover (tree body, url cover) {
  if (is_atomic (body) || L(body) != DOCUMENT) return body;
  int position= 0;
  if (N(body) > 0 && is_compound (body[0])) {
    string tag= as_string (L(body[0]));
    if (tag == "doc-data" || tag == "title" || tag == "doc-title" ||
        tag == "doc-title-block")
      position= 1;
  }
  tree result (DOCUMENT);
  for (int i=0; i<=N(body); ++i) {
    if (i == position) result << cover_document (cover, "0.92par");
    if (i < N(body)) result << body[i];
  }
  athena::node::copy_metadata (body, result);
  return result;
}

} // namespace

bool athena_data_art_enabled () {
  return get_preference ("texmacs->pdf:data-art cover", "off") == "on";
}

bool athena_data_art_pdf_target (object target) {
  return is_url (target) && suffix (as_url (target)) == "pdf";
}

bool athena_data_art_cover_present (url source) {
  return cover_present (get_buffer_body (source));
}

static url generate_cover (url source, tree body) {
  url cover= glue (url_temp (), ".png");
  string seed= "ATHENA DataArt cover seed\n" * as_string (source) * "\n\n" *
    tree_to_scheme (body);
  string error= athena_data_art_generate (seed, cover);
  if (error == "" && exists (cover)) return cover;
  if (exists (cover)) remove (cover);
  std_warning << "data-art cover generation failed"
              << (error == "" ? string ("") : ": " * error) << "\n";
  return url_none ();
}

url athena_data_art_generate_cover (url source) {
  return generate_cover (source, get_buffer_body (source));
}

tree athena_data_art_body_with_cover (tree body, url cover) {
  tree snapshot= copy (body);
  return cover_present (snapshot)? snapshot: insert_cover (snapshot, cover);
}

tree athena_data_art_doc_data_with_cover (tree body, url cover) {
  tree snapshot= copy (body);
  if (cover_present (snapshot) || is_atomic (snapshot) || L(snapshot) != DOCUMENT)
    return snapshot;
  for (int i=0; i<N(snapshot); ++i)
    if (is_compound (snapshot[i]) && L(snapshot[i]) == make_tree_label ("doc-data")) {
      snapshot[i] << compound ("doc-misc", cover_document (cover, "0.72par"));
      return snapshot;
    }
  return insert_cover (snapshot, cover);
}

tree athena_data_art_export_payload (tree document, tree body, url cover, int version) {
  if (version < 1 || version > 3) throw std::runtime_error ("Invalid DataArt source version");
  return tree (TUPLE, copy (document), copy (body), as_string (cover), as_string (version));
}

object athena_data_art_prepare_export (url source, object target) {
  if (!athena_data_art_enabled () || !athena_data_art_pdf_target (target))
    return object (false);
  tree body= get_buffer_body (source);
  if (cover_present (body)) return object (false);
  url cover= generate_cover (source, body);
  if (is_none (cover)) return object (false);
  body= insert_cover (body, cover);
  return object (athena_data_art_export_payload (
    get_buffer_tree (source), body, cover, athena_data_art_source_version ()));
}

void athena_data_art_insert_cover (url source, url cover) {
  set_buffer_body (source, athena_data_art_body_with_cover (get_buffer_body (source), cover));
}

void athena_data_art_insert_doc_data_cover (url source, url cover) {
  set_buffer_body (source, athena_data_art_doc_data_with_cover (get_buffer_body (source), cover));
}
