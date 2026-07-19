/******************************************************************************
* MODULE     : artifact_range_llm.cpp
* DESCRIPTION: Small-LLM paragraph range selection for bold-text artifacts
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "ATHENA/Data/artifact_range_llm.hpp"
#include "ATHENA/Data/llama_runtime.hpp"

#include "boot.hpp"
#include "file.hpp"
#include "Scheme/scheme.hpp"
#include "tm_ostream.hpp"

#include <llama.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <regex>
#include <sstream>
#include <thread>

namespace {

constexpr int context_tokens= 4096;
constexpr int output_tokens= 48;

std::string tm_std (string s) {
  return std::string (as_charp (s), (size_t) N(s));
}

string std_tm (const std::string& s) { return string (s.data (), (int) s.size ()); }

std::string trim (std::string s) {
  auto space= [] (unsigned char c) { return std::isspace (c) != 0; };
  s.erase (s.begin (), std::find_if (s.begin (), s.end (),
                                     [&] (unsigned char c) { return !space(c); }));
  s.erase (std::find_if (s.rbegin (), s.rend (),
                         [&] (unsigned char c) { return !space(c); }).base (),
           s.end ());
  return s;
}

std::string configured_model () {
  const char* environment= std::getenv ("ATHENA_ARTIFACT_RANGE_MODEL");
  if (environment && *environment) return environment;
  std::string value= trim (tm_std (get_preference (
    "artifacts definition range model", "")));
  if (value.empty ())
    value= tm_std (concretize (url_system (
      "$ATHENA_PATH/tools/artifacts/artifact-range-model.gguf")));
  else if (value[0] == '~' || value.find ('$') != std::string::npos)
    value= tm_std (concretize (url_system (std_tm (value))));
  return value;
}

std::vector<llama_token> tokenize (const llama_vocab* vocab,
                                   const std::string& text) {
  int32_t n= llama_tokenize (vocab, text.data (), (int32_t) text.size (),
                             nullptr, 0, true, true);
  if (n == 0) return {};
  if (n < 0) n= -n;
  std::vector<llama_token> out ((size_t) n);
  n= llama_tokenize (vocab, text.data (), (int32_t) text.size (), out.data (),
                     n, true, true);
  if (n < 0) n= -n;
  out.resize ((size_t) n);
  return out;
}

std::string token_piece (const llama_vocab* vocab, llama_token token) {
  char small[64];
  int32_t n= llama_token_to_piece (vocab, token, small, sizeof (small), 0,
                                   false);
  if (n >= 0) return std::string (small, (size_t) n);
  std::vector<char> large ((size_t) -n);
  n= llama_token_to_piece (vocab, token, large.data (), (int32_t) large.size (),
                           0, false);
  return n > 0 ? std::string (large.data (), (size_t) n) : std::string ();
}

std::string make_prompt (
  const std::string& keyword,
  const std::vector<std::pair<int,std::string>>& paragraphs) {
  std::ostringstream out;
  out << "You select which nearby paragraphs constitute the mathematical "
         "definition of a bold keyword in an ATHENA note.\n"
         "Return ONLY one bracketed comma-separated list of paragraph integers, "
         "for example [-1, 0, 1]. Do not explain, reason, use prose, or emit "
         "markdown. Paragraph 0 MUST always be included. Select only supplied "
         "integers. Select the smallest contiguous semantic range that defines "
         "the keyword; exclude examples, later consequences, conversation, and "
         "unrelated text. A displayed formula attached to a paragraph is part of "
         "that paragraph.\n\n"
         "Example keyword: compact operator\n"
         "=== BEGIN PARAGRAPH -1 ===\nWe now introduce a useful class.\n"
         "=== END PARAGRAPH -1 ===\n"
         "=== BEGIN PARAGRAPH 0 ===\nA bounded operator is called "
         "\\textbf{compact} when it maps bounded sets to relatively compact "
         "sets.\n=== END PARAGRAPH 0 ===\n"
         "=== BEGIN PARAGRAPH 1 ===\nThe identity is not compact in infinite "
         "dimension.\n=== END PARAGRAPH 1 ===\nAnswer: [0]\n\n"
         "Example keyword: covering space\n"
         "=== BEGIN PARAGRAPH 0 ===\nA map p:E\\to X is a "
         "\\textbf{covering space} if the following local condition holds.\n"
         "=== END PARAGRAPH 0 ===\n"
         "=== BEGIN PARAGRAPH 1 ===\nEvery x\\in X has an open neighborhood U "
         "whose inverse image is a disjoint union of open sets mapped "
         "homeomorphically onto U.\n=== END PARAGRAPH 1 ===\nAnswer: [0, 1]\n\n"
      << "Keyword (LaTeX): " << keyword << "\n";
  for (const auto& paragraph: paragraphs)
    out << "=== BEGIN PARAGRAPH " << paragraph.first << " ===\n"
        << paragraph.second << "\n=== END PARAGRAPH " << paragraph.first
        << " ===\n";
  out << "Answer:";
  return out.str ();
}

std::vector<int> parse_result (
  const std::string& text,
  const std::vector<std::pair<int,std::string>>& paragraphs) {
  std::vector<int> allowed;
  for (const auto& p: paragraphs) allowed.push_back (p.first);
  std::vector<int> out;
  std::smatch match;
  if (!std::regex_search (text, match, std::regex ("\\[([^\\]]*)\\]")))
    return {0};
  std::regex integer ("-?[0-9]+");
  std::string body= match[1].str ();
  for (std::sregex_iterator it (body.begin (), body.end (), integer), end;
       it != end; ++it) {
    int value= std::stoi (it->str ());
    if (std::find (allowed.begin (), allowed.end (), value) != allowed.end () &&
        std::find (out.begin (), out.end (), value) == out.end ())
      out.push_back (value);
  }
  if (std::find (out.begin (), out.end (), 0) == out.end ()) out.push_back (0);
  std::sort (out.begin (), out.end ());
  return out;
}

class RangeModel {
public:
  ~RangeModel () { unload (); }

  std::vector<int> select (
    const std::string& keyword,
    const std::vector<std::pair<int,std::string>>& paragraphs,
    const std::string& model_path, const std::atomic<bool>* cancelled) {
    std::lock_guard<std::mutex> guard (mutex);
    if (cancelled && cancelled->load ()) return {0};
    if (!load (model_path)) return {0};
    std::string prompt= make_prompt (keyword, paragraphs);
    const llama_vocab* vocab= llama_model_get_vocab (model);
    std::vector<llama_token> input= tokenize (vocab, prompt);
    if (input.empty () || (int) input.size () + output_tokens >= context_tokens)
      return {0};
    cout << "[artifacts] definition-range inference: keyword=\""
         << keyword.c_str () << "\", candidate-paragraphs="
         << paragraphs.size () << ", input-tokens=" << input.size () << LF;
    llama_set_abort_callback (
      ctx,
      [] (void* data) {
        const std::atomic<bool>* flag=
          static_cast<const std::atomic<bool>*> (data);
        return flag && flag->load ();
      }, const_cast<std::atomic<bool>*> (cancelled));
    struct AbortReset {
      llama_context* context;
      ~AbortReset () { llama_set_abort_callback (context, nullptr, nullptr); }
    } reset {ctx};
    llama_memory_clear (llama_get_memory (ctx), true);
    llama_sampler_reset (sampler);
    llama_batch batch= llama_batch_get_one (input.data (), (int32_t) input.size ());
    if (llama_decode (ctx, batch) != 0) return {0};
    std::string answer;
    for (int i=0; i<output_tokens; i++) {
      if (cancelled && cancelled->load ()) return {0};
      llama_token token= llama_sampler_sample (sampler, ctx, -1);
      if (llama_vocab_is_eog (vocab, token)) break;
      answer += token_piece (vocab, token);
      llama_batch next= llama_batch_get_one (&token, 1);
      if (llama_decode (ctx, next) != 0) break;
      if (answer.find (']') != std::string::npos) break;
    }
    cout << "[artifacts] definition-range model output: "
         << trim (answer).c_str () << LF;
    return parse_result (answer, paragraphs);
  }

private:
  bool load (const std::string& path) {
    if (model && path == loaded_path) return true;
    unload ();
    if (!exists (url_system (std_tm (path)))) {
      if (warned_path != path) {
        std_warning << "artifacts: definition-range GGUF model not found at "
                    << path.c_str () << "; bold artifacts will use paragraph 0"
                    << LF;
        warned_path= path;
      }
      return false;
    }
    auto started= std::chrono::steady_clock::now ();
    cout << "[artifacts] loading definition-range model: "
         << path.c_str () << LF;
    athena_llama_runtime_initialize ();
    llama_model_params mp= llama_model_default_params ();
    mp.n_gpu_layers= 0;
    model= llama_model_load_from_file (path.c_str (), mp);
    if (!model) {
      std_warning << "artifacts: could not load definition-range model "
                  << path.c_str () << LF;
      return false;
    }
    llama_context_params cp= llama_context_default_params ();
    cp.n_ctx= context_tokens;
    cp.n_batch= context_tokens;
    cp.n_threads= std::max (1u, std::thread::hardware_concurrency ());
    cp.n_threads_batch= cp.n_threads;
    ctx= llama_init_from_model (model, cp);
    if (!ctx) { unload (); return false; }
    sampler= llama_sampler_chain_init (llama_sampler_chain_default_params ());
    llama_sampler_chain_add (sampler, llama_sampler_init_greedy ());
    loaded_path= path;
    auto elapsed= std::chrono::duration_cast<std::chrono::milliseconds> (
      std::chrono::steady_clock::now () - started).count ();
    cout << "[artifacts] definition-range model ready in " << elapsed
         << " ms" << LF;
    return true;
  }

  void unload () {
    if (sampler) llama_sampler_free (sampler);
    if (ctx) llama_free (ctx);
    if (model) llama_model_free (model);
    sampler= nullptr; ctx= nullptr; model= nullptr; loaded_path.clear ();
  }

  std::mutex mutex;
  llama_model* model= nullptr;
  llama_context* ctx= nullptr;
  llama_sampler* sampler= nullptr;
  std::string loaded_path;
  std::string warned_path;
};

RangeModel& range_model () { static RangeModel model; return model; }

} // namespace

bool
athena_artifact_range_model_available () {
  return athena_artifact_range_model_available (configured_model ());
}

std::string
athena_artifact_range_model_path () {
  return configured_model ();
}

bool
athena_artifact_range_model_available (const std::string& path) {
  static std::mutex warning_mutex;
  static std::string warned_path;
  bool available= exists (url_system (std_tm (path)));
  if (!available) {
    std::lock_guard<std::mutex> guard (warning_mutex);
    if (warned_path != path) {
      std_warning << "artifacts: definition-range GGUF model not found at "
                  << path.c_str () << "; bold artifacts will use paragraph 0"
                  << LF;
      warned_path= path;
    }
  }
  return available;
}

std::vector<int>
athena_artifact_select_definition_range (
  const std::string& keyword_latex,
  const std::vector<std::pair<int,std::string>>& paragraphs) {
  return range_model ().select (keyword_latex, paragraphs, configured_model (),
                                nullptr);
}

std::vector<int>
athena_artifact_select_definition_range (
  const std::string& keyword_latex,
  const std::vector<std::pair<int,std::string>>& paragraphs,
  const std::string& model_path, const std::atomic<bool>* cancelled) {
  return range_model ().select (keyword_latex, paragraphs, model_path,
                                cancelled);
}
