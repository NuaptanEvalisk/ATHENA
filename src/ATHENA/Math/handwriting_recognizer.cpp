/******************************************************************************
* MODULE     : handwriting_recognizer.cpp
* DESCRIPTION: Hand TeX single-symbol handwriting recognition
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "handwriting_recognizer.hpp"

#include <net.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace {

constexpr int canvas_size= 1000;
constexpr int image_size= 64;

bool
same_stroke (const athena_handwriting_stroke& left,
             const athena_handwriting_stroke& right) {
  if (left.size () != right.size ()) return false;
  for (size_t i=0; i<left.size (); i++)
    if (left[i].x != right[i].x || left[i].y != right[i].y) return false;
  return true;
}

float
correct_scale (float value) {
  if (value < 4.0f) return value;
  if (value < 8.0f) return -1.0f / std::pow (2.71828f, value - 4.0f) + 5.0f;
  return 2.0614754f + 3.0f *
    (1.0f - 1.0f / (1.0f + std::pow (2.71828f, -0.3f * (value - 20.0f))));
}

void
draw_line (std::vector<unsigned char>& pixels, int x0, int y0, int x1,
           int y1) {
  int dx= std::abs (x1 - x0);
  int sx= x0 < x1 ? 1 : -1;
  int dy= -std::abs (y1 - y0);
  int sy= y0 < y1 ? 1 : -1;
  int error= dx + dy;
  while (true) {
    if (x0 >= 0 && x0 < image_size && y0 >= 0 && y0 < image_size)
      pixels[(size_t) y0 * image_size + x0]= 0;
    if (x0 == x1 && y0 == y1) break;
    int twice= 2 * error;
    if (twice >= dy) { error += dy; x0 += sx; }
    if (twice <= dx) { error += dx; y0 += sy; }
  }
}

std::vector<std::string>
split_tabs (const std::string& line) {
  std::vector<std::string> result;
  size_t start= 0;
  while (start <= line.size ()) {
    size_t tab= line.find ('\t', start);
    if (tab == std::string::npos) {
      result.push_back (line.substr (start));
      break;
    }
    result.push_back (line.substr (start, tab - start));
    start= tab + 1;
  }
  return result;
}

} // namespace

struct athena_handwriting_recognizer::implementation {
  explicit implementation (std::string directory2)
    : directory (std::move (directory2)), loaded (false) {}

  bool load (std::string& error) {
    std::lock_guard<std::mutex> guard (mutex);
    if (loaded) return true;

    namespace fs= std::filesystem;
    fs::path root (directory);
    fs::path param= root / "handtex.ncnn.param";
    fs::path model= root / "handtex.ncnn.bin";
    fs::path encodings= root / "encodings.txt";
    fs::path symbols= root / "symbols.tsv";
    fs::path similarities= root / "similarity-groups.tsv";
    for (const fs::path& path: {param, model, encodings, symbols, similarities})
      if (!fs::is_regular_file (path)) {
        error= "missing handwriting model asset: " + path.string ();
        return false;
      }

    labels.clear ();
    std::ifstream label_input (encodings);
    for (std::string line; std::getline (label_input, line); )
      if (!line.empty ()) labels.push_back (line);
    if (labels.size () != 1775) {
      error= "invalid Hand TeX encoding table";
      return false;
    }

    commands.clear ();
    std::ifstream symbol_input (symbols);
    for (std::string line; std::getline (symbol_input, line); ) {
      std::vector<std::string> fields= split_tabs (line);
      if (fields.size () == 2) commands[fields[0]]= fields[1];
    }

    groups.clear ();
    std::ifstream similarity_input (similarities);
    for (std::string line; std::getline (similarity_input, line); ) {
      std::vector<std::string> group= split_tabs (line);
      if (group.empty ()) continue;
      for (const std::string& key: group) groups[key]= group;
    }

    net.opt.use_vulkan_compute= false;
    net.opt.num_threads= (int) std::max (1u, std::thread::hardware_concurrency ());
    if (net.load_param (param.string ().c_str ()) != 0 ||
        net.load_model (model.string ().c_str ()) != 0) {
      error= "could not load the Hand TeX ncnn model";
      net.clear ();
      return false;
    }
    loaded= true;
    return true;
  }

  std::string directory;
  ncnn::Net net;
  std::vector<std::string> labels;
  std::unordered_map<std::string, std::string> commands;
  std::unordered_map<std::string, std::vector<std::string>> groups;
  std::mutex mutex;
  bool loaded;
};

athena_handwriting_recognizer::athena_handwriting_recognizer (
  const std::string& asset_directory)
  : impl (std::make_unique<implementation> (asset_directory)) {}

athena_handwriting_recognizer::~athena_handwriting_recognizer ()= default;

bool
athena_handwriting_recognizer::available (std::string& error) {
  return impl->load (error);
}

std::vector<float>
athena_handwriting_recognizer::preprocess (
  const std::vector<athena_handwriting_stroke>& strokes,
  float viewport_width, float viewport_height) {
  std::vector<float> result ((size_t) image_size * image_size, 1.0f);
  if (strokes.empty () || viewport_width <= 0.0f || viewport_height <= 0.0f)
    return result;

  std::vector<athena_handwriting_stroke> unique_strokes;
  for (const auto& stroke: strokes) {
    bool duplicate= false;
    for (const auto& previous: unique_strokes)
      if (same_stroke (stroke, previous)) { duplicate= true; break; }
    if (!duplicate) unique_strokes.push_back (stroke);
  }

  float initial= canvas_size / std::max (viewport_width, viewport_height);
  float min_x= std::numeric_limits<float>::max ();
  float min_y= min_x;
  float max_x= std::numeric_limits<float>::lowest ();
  float max_y= max_x;
  size_t points= 0;
  for (const auto& stroke: unique_strokes)
    for (const auto& point: stroke) {
      float x= point.x * initial;
      float y= point.y * initial;
      min_x= std::min (min_x, x); max_x= std::max (max_x, x);
      min_y= std::min (min_y, y); max_y= std::max (max_y, y);
      points++;
    }
  if (points == 0) return result;

  float width= max_x - min_x;
  float height= max_y - min_y;
  float span= std::max (width, height);
  float scale= points == 1 || span <= 0.0f ? 1.0f :
    correct_scale (canvas_size / span);
  float offset_x= points == 1 ? canvas_size / 2.0f - min_x :
    std::nearbyint ((canvas_size - width * scale) / 2.0f - min_x * scale);
  float offset_y= points == 1 ? canvas_size / 2.0f - min_y :
    std::nearbyint ((canvas_size - height * scale) / 2.0f - min_y * scale);

  std::vector<unsigned char> pixels ((size_t) image_size * image_size, 255);
  auto raster_point= [=] (const athena_handwriting_point& point) {
    int normalized_x= points == 1 ? canvas_size / 2 :
      (int) (point.x * initial * scale + offset_x);
    int normalized_y= points == 1 ? canvas_size / 2 :
      (int) (point.y * initial * scale + offset_y);
    return std::pair<int, int> (
      (int) std::nearbyint (normalized_x * (image_size * 0.9f) / canvas_size +
                            image_size * 0.05f),
      (int) std::nearbyint (normalized_y * (image_size * 0.9f) / canvas_size +
                            image_size * 0.05f));
  };

  for (const auto& stroke: unique_strokes) {
    if (stroke.empty ()) continue;
    std::vector<std::pair<int, int>> raster_stroke;
    for (const auto& point: stroke) {
      auto current= raster_point (point);
      if (raster_stroke.empty () || raster_stroke.back () != current)
        raster_stroke.push_back (current);
    }
    auto previous= raster_stroke.front ();
    if (raster_stroke.size () == 1)
      draw_line (pixels, previous.first, previous.second,
                 previous.first + 1, previous.second + 1);
    for (size_t i=1; i<raster_stroke.size (); i++) {
      auto current= raster_stroke[i];
      draw_line (pixels, previous.first, previous.second,
                 current.first, current.second);
      previous= current;
    }
  }
  for (size_t i=0; i<pixels.size (); i++)
    result[i]= 2.0f * ((float) pixels[i] / 255.0f) - 1.0f;
  return result;
}

std::vector<athena_handwriting_prediction>
athena_handwriting_recognizer::recognize (
  const std::vector<athena_handwriting_stroke>& strokes,
  float viewport_width, float viewport_height, int maximum_results,
  std::string& error) {
  if (!impl->load (error)) return {};
  std::vector<float> pixels= preprocess (strokes, viewport_width, viewport_height);

  ncnn::Mat input (image_size, image_size, 1);
  std::copy (pixels.begin (), pixels.end (), (float*) input.data);
  ncnn::Mat output;
  {
    std::lock_guard<std::mutex> guard (impl->mutex);
    ncnn::Extractor extractor= impl->net.create_extractor ();
    if (extractor.input ("in0", input) != 0 ||
        extractor.extract ("out0", output) != 0) {
      error= "Hand TeX inference failed";
      return {};
    }
  }
  size_t output_size= (size_t) output.w * (size_t) output.elempack;
  if (output.dims >= 2) output_size *= (size_t) output.h;
  if (output.dims >= 3) output_size *= (size_t) output.c;
  if (output.dims >= 4) output_size *= (size_t) output.d;
  if (output_size != impl->labels.size ()) {
    error= "Hand TeX model output does not match its encoding table: dims=" +
      std::to_string (output.dims) + " w=" + std::to_string (output.w) +
      " h=" + std::to_string (output.h) + " c=" +
      std::to_string (output.c) + " elempack=" +
      std::to_string (output.elempack) + " logical-size=" +
      std::to_string (output_size);
    return {};
  }

  const float* logits= output;
  float maximum= *std::max_element (logits, logits + output_size);
  std::vector<float> probabilities (output_size);
  float sum= 0.0f;
  for (size_t i=0; i<probabilities.size (); i++) {
    probabilities[i]= std::exp (logits[i] - maximum);
    sum += probabilities[i];
  }
  for (float& value: probabilities) value /= sum;

  std::vector<size_t> order (probabilities.size ());
  for (size_t i=0; i<order.size (); i++) order[i]= i;
  size_t leaders= std::min<size_t> (20, order.size ());
  std::partial_sort (order.begin (), order.begin () + leaders, order.end (),
    [&] (size_t left, size_t right) {
      return probabilities[left] > probabilities[right];
    });

  std::vector<athena_handwriting_prediction> result;
  std::unordered_set<std::string> emitted;
  for (size_t n=0; n<leaders && (int) result.size ()<maximum_results; n++) {
    size_t index= order[n];
    const std::string& leader= impl->labels[index];
    auto group_it= impl->groups.find (leader);
    std::vector<std::string> one= {leader};
    const std::vector<std::string>& group=
      group_it == impl->groups.end () ? one : group_it->second;
    for (const std::string& key: group) {
      auto command= impl->commands.find (key);
      if (command == impl->commands.end () ||
          !emitted.insert (command->second).second)
        continue;
      result.push_back ({key, command->second, probabilities[index]});
      if ((int) result.size () >= maximum_results) break;
    }
  }
  return result;
}
