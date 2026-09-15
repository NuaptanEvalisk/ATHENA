/******************************************************************************
* MODULE     : vtk_surface_renderer.cpp
* DESCRIPTION: Offscreen VTK surface rendering adapter
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "vtk_surface_renderer.hpp"

#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkDoubleArray.h>
#include <vtkImageData.h>
#include <vtkLookupTable.h>
#include <vtkNew.h>
#include <vtkPNGWriter.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>
#include <vtkWindowToImageFilter.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace athena::vtk_render {
namespace {

constexpr double pi= 3.141592653589793238462643383279502884;

bool
valid_request (const SurfaceRenderRequest& request, std::string& error) {
  if (request.rows < 2 || request.columns < 2) {
    error= "surface grid must contain at least two rows and columns";
    return false;
  }
  if (request.x_coordinates.size () != request.columns ||
      request.y_coordinates.size () != request.rows ||
      request.z_values.size () != request.rows * request.columns ||
      request.cell_scalars.size () !=
        (request.rows - 1) * (request.columns - 1)) {
    error= "surface grid dimensions do not match coordinate/scalar data";
    return false;
  }
  if (request.width <= 0 || request.height <= 0) {
    error= "surface image dimensions must be positive";
    return false;
  }
  for (double value: request.box_aspect)
    if (!(value > 0.0) || !std::isfinite (value)) {
      error= "surface box aspect must contain finite positive values";
      return false;
    }
  if (!(request.viewport[0] >= 0.0 &&
        request.viewport[0] < request.viewport[2] &&
        request.viewport[2] <= 1.0 &&
        request.viewport[1] >= 0.0 &&
        request.viewport[1] < request.viewport[3] &&
        request.viewport[3] <= 1.0)) {
    error= "surface viewport must be a non-empty normalized rectangle";
    return false;
  }
  if (!(request.camera_zoom > 0.0) || !std::isfinite (request.camera_zoom)) {
    error= "surface camera zoom must be finite and positive";
    return false;
  }
  if (!(request.crop_margin_fraction >= 0.0) ||
      !std::isfinite (request.crop_margin_fraction)) {
    error= "surface crop margin must be finite and non-negative";
    return false;
  }
  return true;
}

double
normalized_coordinate (double value, double minimum, double maximum,
                       double extent) {
  if (!(maximum > minimum)) return 0.0;
  const double middle= 0.5 * (minimum + maximum);
  return extent * (value - middle) / (maximum - minimum);
}

bool
is_png_file (const std::filesystem::path& output) {
  std::ifstream file (output, std::ios::binary);
  if (!file) return false;
  unsigned char signature[8] {};
  file.read (reinterpret_cast<char*> (signature), sizeof (signature));
  static constexpr unsigned char expected[8] {
    0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a
  };
  return file.gcount () == (std::streamsize) sizeof (signature) &&
         std::equal (std::begin (signature), std::end (signature),
                     std::begin (expected));
}

vtkSmartPointer<vtkImageData>
crop_surface_image (vtkImageData* image, double margin_fraction) {
  if (image == nullptr) return nullptr;

  int dimensions[3] {};
  image->GetDimensions (dimensions);
  const int width= dimensions[0];
  const int height= dimensions[1];
  const int components= image->GetNumberOfScalarComponents ();
  if (width <= 0 || height <= 0 || components < 3) return image;

  const auto* corner=
    static_cast<const unsigned char*> (image->GetScalarPointer (0, 0, 0));
  if (corner == nullptr) return image;
  const unsigned char background[3] {corner[0], corner[1], corner[2]};

  int min_x= width;
  int min_y= height;
  int max_x= -1;
  int max_y= -1;
  constexpr int tolerance= 4;
  for (int y=0; y<height; ++y)
    for (int x=0; x<width; ++x) {
      const auto* pixel= static_cast<const unsigned char*> (
        image->GetScalarPointer (x, y, 0));
      if (pixel == nullptr) continue;
      const int delta= std::max ({
        std::abs ((int) pixel[0] - (int) background[0]),
        std::abs ((int) pixel[1] - (int) background[1]),
        std::abs ((int) pixel[2] - (int) background[2])});
      if (delta <= tolerance) continue;
      min_x= std::min (min_x, x);
      min_y= std::min (min_y, y);
      max_x= std::max (max_x, x);
      max_y= std::max (max_y, y);
    }

  if (max_x < min_x || max_y < min_y) return image;
  const int content_width= max_x - min_x + 1;
  const int content_height= max_y - min_y + 1;
  const int margin_x=
    std::max (1, (int) std::ceil (margin_fraction * content_width));
  const int margin_y=
    std::max (1, (int) std::ceil (margin_fraction * content_height));
  min_x= std::max (0, min_x - margin_x);
  min_y= std::max (0, min_y - margin_y);
  max_x= std::min (width - 1, max_x + margin_x);
  max_y= std::min (height - 1, max_y + margin_y);

  const int cropped_width= max_x - min_x + 1;
  const int cropped_height= max_y - min_y + 1;
  vtkSmartPointer<vtkImageData> cropped= vtkSmartPointer<vtkImageData>::New ();
  cropped->SetDimensions (cropped_width, cropped_height, 1);
  cropped->AllocateScalars (image->GetScalarType (), components);
  const std::size_t row_bytes=
    (std::size_t) cropped_width * (std::size_t) components *
    (std::size_t) image->GetScalarSize ();
  for (int y=0; y<cropped_height; ++y) {
    const void* source= image->GetScalarPointer (min_x, min_y + y, 0);
    void* destination= cropped->GetScalarPointer (0, y, 0);
    std::memcpy (destination, source, row_bytes);
  }
  return cropped;
}

} // namespace

bool
render_surface_png (const SurfaceRenderRequest& request,
                    const std::filesystem::path& output,
                    std::string& error) {
  error.clear ();
  if (!valid_request (request, error)) return false;

  const auto x_range= std::minmax_element (request.x_coordinates.begin (),
                                           request.x_coordinates.end ());
  const auto y_range= std::minmax_element (request.y_coordinates.begin (),
                                           request.y_coordinates.end ());
  const auto z_range= std::minmax_element (request.z_values.begin (),
                                           request.z_values.end ());
  const auto scalar_range= std::minmax_element (request.cell_scalars.begin (),
                                                request.cell_scalars.end ());

  vtkNew<vtkPoints> points;
  points->SetNumberOfPoints ((vtkIdType) (request.rows * request.columns));
  for (std::size_t row=0; row<request.rows; ++row) {
    for (std::size_t column=0; column<request.columns; ++column) {
      const std::size_t index= row * request.columns + column;
      const double x= normalized_coordinate (
        request.x_coordinates[column], *x_range.first, *x_range.second,
        request.box_aspect[0]);
      const double y= normalized_coordinate (
        request.y_coordinates[row], *y_range.first, *y_range.second,
        request.box_aspect[1]);
      const double z= normalized_coordinate (
        request.z_values[index], *z_range.first, *z_range.second,
        request.box_aspect[2]);
      points->SetPoint ((vtkIdType) index, x, y, z);
    }
  }

  vtkNew<vtkCellArray> cells;
  vtkNew<vtkDoubleArray> scalars;
  scalars->SetName ("surface-scalar");
  scalars->SetNumberOfComponents (1);
  scalars->Allocate ((vtkIdType) request.cell_scalars.size ());
  for (std::size_t row=0; row+1<request.rows; ++row) {
    for (std::size_t column=0; column+1<request.columns; ++column) {
      vtkIdType ids[4] {
        (vtkIdType) (row * request.columns + column),
        (vtkIdType) (row * request.columns + column + 1),
        (vtkIdType) ((row + 1) * request.columns + column + 1),
        (vtkIdType) ((row + 1) * request.columns + column)
      };
      cells->InsertNextCell (4, ids);
      const std::size_t scalar_index= row * (request.columns - 1) + column;
      scalars->InsertNextValue (request.cell_scalars[scalar_index]);
    }
  }

  vtkNew<vtkPolyData> surface;
  surface->SetPoints (points);
  surface->SetPolys (cells);
  surface->GetCellData ()->SetScalars (scalars);

  double scalar_min= *scalar_range.first;
  double scalar_max= *scalar_range.second;
  if (!(scalar_max > scalar_min)) {
    scalar_min -= 0.5;
    scalar_max += 0.5;
  }

  vtkNew<vtkLookupTable> lookup;
  lookup->SetNumberOfTableValues (256);
  lookup->SetTableRange (scalar_min, scalar_max);
  lookup->SetScaleToLinear ();
  lookup->Build ();
  const double alpha= std::clamp (request.surface_alpha, 0.0, 1.0);
  for (std::size_t i=0; i<request.palette.size (); ++i) {
    const std::uint32_t rgb= request.palette[i];
    const double red= ((rgb >> 16) & 0xffu) / 255.0;
    const double green= ((rgb >> 8) & 0xffu) / 255.0;
    const double blue= (rgb & 0xffu) / 255.0;
    lookup->SetTableValue ((vtkIdType) i,
      alpha * red + (1.0 - alpha) * request.background[0],
      alpha * green + (1.0 - alpha) * request.background[1],
      alpha * blue + (1.0 - alpha) * request.background[2], 1.0);
  }

  vtkNew<vtkPolyDataMapper> mapper;
  mapper->SetInputData (surface);
  mapper->SetLookupTable (lookup);
  mapper->SetScalarRange (scalar_min, scalar_max);
  mapper->SetScalarModeToUseCellData ();
  mapper->SetColorModeToMapScalars ();
  mapper->UseLookupTableScalarRangeOn ();
  mapper->InterpolateScalarsBeforeMappingOff ();

  vtkNew<vtkActor> actor;
  actor->SetMapper (mapper);
  actor->GetProperty ()->LightingOff ();
  actor->GetProperty ()->EdgeVisibilityOff ();
  actor->GetProperty ()->SetInterpolationToFlat ();

  vtkNew<vtkRenderer> background_renderer;
  background_renderer->SetLayer (0);
  background_renderer->SetBackground (
    request.background[0], request.background[1], request.background[2]);

  vtkNew<vtkRenderer> surface_renderer;
  surface_renderer->SetLayer (1);
  surface_renderer->SetViewport (request.viewport[0], request.viewport[1],
                                 request.viewport[2], request.viewport[3]);
  surface_renderer->AddActor (actor);

  vtkCamera* camera= surface_renderer->GetActiveCamera ();
  const double elevation= request.elevation_degrees * pi / 180.0;
  const double azimuth= request.azimuth_degrees * pi / 180.0;
  const double cos_elevation= std::cos (elevation);
  camera->SetFocalPoint (0.0, 0.0, 0.0);
  camera->SetPosition (cos_elevation * std::cos (azimuth),
                       cos_elevation * std::sin (azimuth),
                       std::sin (elevation));
  camera->SetViewUp (0.0, 0.0, 1.0);
  camera->SetViewAngle (request.view_angle_degrees);
  surface_renderer->ResetCamera ();
  camera->Zoom (request.camera_zoom);
  surface_renderer->ResetCameraClippingRange ();

  vtkNew<vtkRenderWindow> window;
  window->SetNumberOfLayers (2);
  window->AddRenderer (background_renderer);
  window->AddRenderer (surface_renderer);
  window->SetSize (request.width, request.height);
  window->SetMultiSamples (std::max (0, request.multisamples));
  window->SetOffScreenRendering (1);
  window->Render ();

  vtkNew<vtkWindowToImageFilter> capture;
  capture->SetInput (window);
  capture->SetInputBufferTypeToRGBA ();
  capture->ReadFrontBufferOff ();
  capture->Update ();

  vtkSmartPointer<vtkImageData> output_image= capture->GetOutput ();
  if (request.crop_to_content)
    output_image= crop_surface_image (
      capture->GetOutput (), request.crop_margin_fraction);

  vtkNew<vtkPNGWriter> writer;
  const std::string output_name= output.string ();
  writer->SetFileName (output_name.c_str ());
  writer->SetInputData (output_image);
  writer->Write ();
  window->Finalize ();

  if (!is_png_file (output)) {
    error= "VTK did not produce a valid PNG image";
    return false;
  }
  return true;
}

} // namespace athena::vtk_render
