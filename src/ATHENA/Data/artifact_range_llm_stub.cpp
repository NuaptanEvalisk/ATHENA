/******************************************************************************
* MODULE     : artifact_range_llm_stub.cpp
* DESCRIPTION: Inference-free artifact range selection fallback
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/artifact_range_llm.hpp"

#include <algorithm>
#include <regex>
#include <set>

bool athena_artifact_range_model_available () { return false; }
bool athena_artifact_range_model_available (const std::string&) { return false; }
std::string athena_artifact_range_model_path () { return {}; }
void athena_artifact_range_model_release () {}
int athena_artifact_range_batch_size () { return 1; }

std::string
athena_artifact_definition_range_cache_contract (const std::string&) {
  return "athena-artifact-range/no-local-inference/v1";
}

std::vector<int>
athena_artifact_parse_definition_range_output (
  const std::string& output,
  const std::vector<std::pair<int,std::string>>& paragraphs,
  bool fallback_to_paragraph_zero) {
  auto invalid= [fallback_to_paragraph_zero] () {
    return fallback_to_paragraph_zero ? std::vector<int> {0}:
                                        std::vector<int> {};
  };
  std::smatch match;
  if (!std::regex_search (output, match, std::regex ("\\[([^\\]]*)\\]")))
    return invalid ();
  std::string body= match[1].str ();
  if (body.find_first_not_of (" \t\r\n") == std::string::npos) return invalid ();
  static const std::regex list_pattern (
    "^\\s*-?[0-9]+\\s*(,\\s*-?[0-9]+\\s*)*$");
  if (!std::regex_match (body, list_pattern)) return invalid ();
  std::set<int> allowed;
  for (const auto& paragraph: paragraphs) allowed.insert (paragraph.first);
  std::vector<int> offsets;
  std::regex integer ("-?[0-9]+");
  try {
    for (std::sregex_iterator it (body.begin (), body.end (), integer), end;
         it != end; ++it) {
      int offset= std::stoi (it->str ());
      if (!allowed.count (offset) ||
          std::find (offsets.begin (), offsets.end (), offset) != offsets.end ())
        return invalid ();
      offsets.push_back (offset);
    }
  }
  catch (const std::exception&) { return invalid (); }
  if (offsets.empty () || !std::is_sorted (offsets.begin (), offsets.end ()) ||
      offsets.front () > 0 || offsets.back () < 0)
    return invalid ();
  std::vector<int> contiguous;
  for (int offset= offsets.front (); offset <= offsets.back (); ++offset) {
    if (!allowed.count (offset)) return invalid ();
    contiguous.push_back (offset);
  }
  return contiguous;
}

std::vector<std::vector<int>>
athena_artifact_select_definition_ranges_progressively (
  const std::vector<AthenaArtifactRangeRequest>& requests,
  const AthenaArtifactRangePass& pass,
  const std::atomic<bool>* cancelled,
  std::atomic<size_t>* completed,
  bool fallback_to_paragraph_zero) {
  struct Window {
    int available_left= 0;
    int available_right= 0;
    int visible_left= 0;
    int visible_right= 0;
    int left_step= 1;
    int right_step= 1;
  };

  std::vector<std::vector<int>> result (requests.size ());
  if (fallback_to_paragraph_zero)
    for (auto& item: result) item= {0};
  if (completed) completed->store (0);
  if (requests.empty () || (cancelled && cancelled->load ())) return result;

  std::vector<Window> windows (requests.size ());
  std::vector<size_t> pending;
  for (size_t i= 0; i < requests.size (); ++i) {
    if (requests[i].paragraphs.empty ()) {
      if (completed) completed->fetch_add (1);
      continue;
    }
    Window& window= windows[i];
    window.available_left= requests[i].paragraphs.front ().first;
    window.available_right= requests[i].paragraphs.back ().first;
    bool has_focus= false;
    for (const auto& paragraph: requests[i].paragraphs)
      has_focus= has_focus || paragraph.first == 0;
    if (!has_focus) {
      if (completed) completed->fetch_add (1);
      continue;
    }
    window.visible_left= std::max (-1, window.available_left);
    window.visible_right= std::min (1, window.available_right);
    if (window.available_left == 0 && window.available_right == 0) {
      result[i]= {0};
      if (completed) completed->fetch_add (1);
      continue;
    }
    pending.push_back (i);
  }

  while (!pending.empty () && !(cancelled && cancelled->load ())) {
    std::vector<AthenaArtifactRangeRequest> wave;
    wave.reserve (pending.size ());
    for (size_t index: pending) {
      AthenaArtifactRangeRequest request;
      request.keyword_latex= requests[index].keyword_latex;
      const Window& window= windows[index];
      for (const auto& paragraph: requests[index].paragraphs)
        if (paragraph.first >= window.visible_left &&
            paragraph.first <= window.visible_right)
          request.paragraphs.push_back (paragraph);
      wave.push_back (std::move (request));
    }

    std::vector<std::vector<int>> selected= pass ? pass (wave):
                                                  std::vector<std::vector<int>> {};
    selected.resize (wave.size ());
    std::vector<size_t> next;
    for (size_t i= 0; i < pending.size (); ++i) {
      size_t index= pending[i];
      Window& window= windows[index];
      std::vector<int> choice= std::move (selected[i]);
      if (choice.empty () && fallback_to_paragraph_zero) choice= {0};
      bool grow_left= !choice.empty () &&
        choice.front () == window.visible_left &&
        window.visible_left > window.available_left;
      bool grow_right= !choice.empty () &&
        choice.back () == window.visible_right &&
        window.visible_right < window.available_right;
      if (grow_left) {
        window.visible_left= std::max (
          window.available_left, window.visible_left - window.left_step);
        window.left_step *= 2;
      }
      if (grow_right) {
        window.visible_right= std::min (
          window.available_right, window.visible_right + window.right_step);
        window.right_step *= 2;
      }
      if (grow_left || grow_right) {
        next.push_back (index);
        continue;
      }
      result[index]= std::move (choice);
      if (completed) completed->fetch_add (1);
    }
    pending= std::move (next);
  }
  return result;
}

std::vector<std::vector<int>>
athena_artifact_select_definition_ranges (
  const std::vector<AthenaArtifactRangeRequest>& requests,
  const std::string&, const std::atomic<bool>* cancelled,
  std::atomic<size_t>* completed, bool fallback_to_paragraph_zero) {
  std::vector<std::vector<int>> result (requests.size ());
  if (fallback_to_paragraph_zero)
    for (auto& item: result) item= {0};
  if (completed) completed->store (requests.size ());
  if (cancelled && cancelled->load ()) return result;
  return result;
}

std::vector<int>
athena_artifact_select_definition_range (
  const std::string&,
  const std::vector<std::pair<int,std::string>>&) {
  return {0};
}

std::vector<int>
athena_artifact_select_definition_range (
  const std::string&,
  const std::vector<std::pair<int,std::string>>&,
  const std::string&, const std::atomic<bool>*) {
  return {0};
}
