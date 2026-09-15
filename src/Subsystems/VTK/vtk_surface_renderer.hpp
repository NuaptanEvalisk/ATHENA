/******************************************************************************
* MODULE     : vtk_surface_renderer.hpp
* DESCRIPTION: Offscreen VTK surface rendering adapter
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_VTK_SURFACE_RENDERER_HPP
#define ATHENA_VTK_SURFACE_RENDERER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace athena::vtk_render {

struct SurfaceRenderRequest {
  std::size_t rows= 0;
  std::size_t columns= 0;
  std::vector<double> x_coordinates;
  std::vector<double> y_coordinates;
  std::vector<double> z_values;
  std::vector<double> cell_scalars;

  std::array<std::uint32_t, 256> palette {};
  std::array<double, 3> background {1.0, 1.0, 1.0};
  std::array<double, 3> box_aspect {1.0, 1.0, 1.0};
  std::array<double, 4> viewport {0.0, 0.0, 1.0, 1.0};

  double elevation_degrees= 30.0;
  double azimuth_degrees= -60.0;
  double view_angle_degrees= 30.0;
  double camera_zoom= 1.0;
  double surface_alpha= 1.0;
  bool crop_to_content= false;
  double crop_margin_fraction= 0.06;
  int width= 800;
  int height= 600;
  int multisamples= 8;
};

bool render_surface_png (const SurfaceRenderRequest& request,
                         const std::filesystem::path& output,
                         std::string& error);

} // namespace athena::vtk_render

#endif // defined ATHENA_VTK_SURFACE_RENDERER_HPP
