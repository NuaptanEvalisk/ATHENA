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
#include <set>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace {

constexpr int context_tokens= 4096;
constexpr int output_tokens= 48;
constexpr int artifact_threads= 6;
constexpr int prefill_batch_tokens= 512;
constexpr int prefill_padding_quantum= 128;
constexpr int default_batch_size= 8;
constexpr int maximum_batch_size= 16;

void range_log (const std::string& message) {
  athena_spdlog_info ("artifacts: " + message);
}

void range_warning (const std::string& message) {
  athena_spdlog_warning ("artifacts: " + message);
}

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

int configured_batch_size () {
  const char* value= std::getenv ("ATHENA_ARTIFACT_RANGE_BATCH_SIZE");
  if (!value || !*value) return default_batch_size;
  char* end= nullptr;
  long parsed= std::strtol (value, &end, 10);
  if (!end || *end != '\0') return default_batch_size;
  return std::clamp ((int) parsed, 1, maximum_batch_size);
}

int configured_gpu_layers () {
  const char* value= std::getenv ("ATHENA_ARTIFACT_RANGE_GPU_LAYERS");
  if (!value || !*value) return -1;
  char* end= nullptr;
  long parsed= std::strtol (value, &end, 10);
  if (!end || *end != '\0') return -1;
  return (int) parsed;
}

void batch_add (llama_batch& batch, llama_token token, llama_pos position,
                llama_seq_id sequence, bool logits) {
  int index= batch.n_tokens++;
  batch.token[index]= token;
  batch.pos[index]= position;
  batch.n_seq_id[index]= 1;
  batch.seq_id[index][0]= sequence;
  batch.logits[index]= logits;
}

std::vector<llama_token> tokenize (const llama_vocab* vocab,
                                   const std::string& text,
                                   bool add_special= true) {
  int32_t n= llama_tokenize (vocab, text.data (), (int32_t) text.size (),
                             nullptr, 0, add_special, true);
  if (n == 0) return {};
  if (n < 0) n= -n;
  std::vector<llama_token> out ((size_t) n);
  n= llama_tokenize (vocab, text.data (), (int32_t) text.size (), out.data (),
                     n, add_special, true);
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

const std::string& prompt_prefix () {
  static const std::string value=
         "You select which nearby paragraphs constitute the mathematical "
         "definition of a bold keyword in an ATHENA note.\n"
         "Return ONLY one bracketed comma-separated list of paragraph integers, "
         "for example [-1, 0, 1]. If the bold text is not a mathematical term "
         "being defined, return []. Do not explain, reason, use prose, or emit "
         "markdown. For a definition, paragraph 0 MUST be included. Select only "
         "supplied integers and the smallest contiguous semantic range that "
         "defines the keyword; exclude headings, emphasis, step numbers, "
         "answers, examples, later consequences, conversation, and unrelated "
         "text. A displayed formula attached to a paragraph is part of "
         "that paragraph. When a shared paragraph catalog is supplied, each "
         "question maps its local paragraph integers to catalog identifiers; "
         "inspect only those mapped catalog entries and return the local "
         "integers.\n\n"
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
         "Example keyword: 2\n"
         "=== BEGIN PARAGRAPH 0 ===\n2. Apply the preceding construction.\n"
         "=== END PARAGRAPH 0 ===\nAnswer: []\n\n"
         "Shared paragraph catalog example:\n"
         "=== BEGIN CATALOG C0 ===\nWe now introduce a useful class.\n"
         "=== END CATALOG C0 ===\n"
         "=== BEGIN CATALOG C1 ===\nA bounded operator is called compact "
         "when it maps bounded sets to relatively compact sets.\n"
         "=== END CATALOG C1 ===\n"
         "=== BEGIN CATALOG C2 ===\nThe identity is not compact in infinite "
         "dimension.\n=== END CATALOG C2 ===\n"
         "Keyword (LaTeX): compact operator\n"
         "Candidate mapping (local integer = catalog identifier):\n"
         "-1 = C0\n0 = C1\n1 = C2\nAnswer: [0]\n\n"
         ;
  return value;
}

struct SharedPromptWave {
  std::string catalog;
  std::vector<std::vector<std::pair<int,int>>> mappings;
};

SharedPromptWave make_shared_prompt_wave (
  const std::vector<AthenaArtifactRangeRequest>& requests,
  size_t base, int count) {
  SharedPromptWave wave;
  wave.mappings.resize ((size_t) count);
  std::unordered_map<std::string,int> catalog_ids;
  std::vector<std::string> paragraphs;
  for (int lane=0; lane<count; lane++) {
    const auto& request= requests[base + (size_t) lane];
    for (const auto& paragraph: request.paragraphs) {
      auto found= catalog_ids.find (paragraph.second);
      int id;
      if (found == catalog_ids.end ()) {
        id= (int) paragraphs.size ();
        catalog_ids.emplace (paragraph.second, id);
        paragraphs.push_back (paragraph.second);
      }
      else id= found->second;
      wave.mappings[(size_t) lane].push_back ({paragraph.first, id});
    }
  }
  std::ostringstream out;
  out << "Shared paragraph catalog for the following questions:\n";
  for (size_t id=0; id<paragraphs.size (); id++)
    out << "=== BEGIN CATALOG C" << id << " ===\n" << paragraphs[id]
        << "\n=== END CATALOG C" << id << " ===\n";
  wave.catalog= out.str ();
  return wave;
}

std::string make_shared_prompt_suffix (
  const std::string& keyword,
  const std::vector<std::pair<int,int>>& mapping) {
  std::ostringstream out;
  out << "Keyword (LaTeX): " << keyword << "\n"
      << "Candidate mapping (local integer = catalog identifier):\n";
  for (const auto& entry: mapping)
    out << entry.first << " = C" << entry.second << "\n";
  out << "Answer:";
  return out.str ();
}

class RangeModel {
public:
  ~RangeModel () { unload (); }

  std::vector<std::vector<int>> select_many (
    const std::vector<AthenaArtifactRangeRequest>& requests,
    const std::string& model_path, int requested_parallelism,
    const std::atomic<bool>* cancelled, std::atomic<size_t>* completed,
    bool fallback_to_paragraph_zero) {
    std::lock_guard<std::mutex> guard (mutex);
    std::vector<std::vector<int>> results (requests.size ());
    if (fallback_to_paragraph_zero)
      for (std::vector<int>& result: results) result= {0};
    if (completed) completed->store (0);
    if (requests.empty () || (cancelled && cancelled->load ())) return results;
    int parallelism= std::clamp (
      requested_parallelism, 1,
      std::min (maximum_batch_size, (int) requests.size ()));
    if (!load (model_path, parallelism)) return results;
    const llama_vocab* vocab= llama_model_get_vocab (model);
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
    if (!prepare_prefix_cache ()) return results;
    llama_memory_t memory= llama_get_memory (ctx);
    range_log ("definition-range batch started: requests=" +
               std::to_string (requests.size ()) + ", parallelism=" +
               std::to_string (parallelism) + ", cached-prefix-tokens=" +
               std::to_string (prefix_tokens));
    auto all_started= std::chrono::steady_clock::now ();
    auto reset_shared_cache= [&] (bool rebuild_prefix) {
      llama_memory_clear (memory, true);
      prefix_cached= false;
      if (!rebuild_prefix) return true;
      bool ready= prepare_prefix_cache ();
      if (ready)
        range_log ("definition-range fixed prompt cache rebuilt for next "
                   "catalog group");
      return ready;
    };

    struct State {
      size_t request= 0;
      int lane= 0;
      std::vector<llama_token> suffix;
      std::string answer;
      int generated= 0;
      int n_past= 0;
      int batch_index= -1;
      llama_token pending_token= 0;
      bool has_pending_token= false;
      bool done= false;
      bool counted= false;
    };

    for (size_t base=0; base<requests.size ();) {
      if (cancelled && cancelled->load ()) break;
      int count= (int) std::min<size_t> ((size_t) parallelism,
                                        requests.size () - base);
      SharedPromptWave wave;
      std::vector<llama_token> catalog_tokens;
      std::vector<std::vector<llama_token>> suffixes;
      while (count > 0) {
        wave= make_shared_prompt_wave (requests, base, count);
        catalog_tokens= tokenize (vocab, wave.catalog, false);
        suffixes.clear ();
        int maximum_suffix= 0;
        int total_suffix= 0;
        for (int lane=0; lane<count; lane++) {
          suffixes.push_back (tokenize (
            vocab, make_shared_prompt_suffix (
              requests[base + (size_t) lane].keyword_latex,
              wave.mappings[(size_t) lane]), false));
          maximum_suffix= std::max (
            maximum_suffix, (int) suffixes.back ().size ());
          total_suffix += (int) suffixes.back ().size ();
        }
        int required_kv= prefix_tokens + (int) catalog_tokens.size () +
                         total_suffix + count * output_tokens;
        if (!catalog_tokens.empty () &&
            prefix_tokens + (int) catalog_tokens.size () + maximum_suffix +
              output_tokens < context_tokens &&
            required_kv < context_tokens + 256 * parallelism)
          break;
        count--;
      }
      if (count == 0) {
        range_warning ("definition-range request exceeds model context");
        if (completed) completed->fetch_add (1);
        base++;
        continue;
      }

      llama_memory_seq_rm (memory, 0, prefix_tokens, -1);
      range_log ("definition-range shared catalog prefill started: requests=" +
                 std::to_string (count) + ", catalog-tokens=" +
                 std::to_string (catalog_tokens.size ()));
      auto catalog_started= std::chrono::steady_clock::now ();
      int catalog_chunks= 0;
      bool catalog_ok= true;
      for (size_t offset=0; offset<catalog_tokens.size ();) {
        int take= std::min (
          prefill_batch_tokens, (int) (catalog_tokens.size () - offset));
        llama_batch batch= llama_batch_init (take, 0, 1);
        for (int i=0; i<take; i++)
          batch_add (batch, catalog_tokens[offset + (size_t) i],
                     prefix_tokens + (int) offset + i, 0, false);
        int status= llama_decode (ctx, batch);
        llama_batch_free (batch);
        if (status != 0) {
          range_warning ("definition-range shared catalog prefill failed");
          catalog_ok= false;
          break;
        }
        offset += (size_t) take;
        catalog_chunks++;
      }
      int shared_tokens= prefix_tokens + (int) catalog_tokens.size ();
      auto catalog_ms= std::chrono::duration_cast<std::chrono::milliseconds> (
        std::chrono::steady_clock::now () - catalog_started).count ();
      range_log ("definition-range shared catalog cached: requests=" +
                 std::to_string (count) + ", catalog-tokens=" +
                 std::to_string (catalog_tokens.size ()) + ", chunks=" +
                 std::to_string (catalog_chunks) + ", elapsed-ms=" +
                 std::to_string (catalog_ms));
      if (!catalog_ok) {
        if (completed) completed->fetch_add ((size_t) count);
        base += (size_t) count;
        bool more= base < requests.size () &&
                   !(cancelled && cancelled->load ());
        if (!reset_shared_cache (more)) break;
        continue;
      }

      std::vector<State> states ((size_t) count);
      int total_dynamic_tokens= 0;
      for (int lane=0; lane<count; lane++) {
        State& state= states[(size_t) lane];
        state.request= base + (size_t) lane;
        state.lane= lane;
        state.suffix= std::move (suffixes[(size_t) lane]);
        if (state.suffix.empty ()) {
          state.done= true;
          if (completed) completed->fetch_add (1);
          continue;
        }
        state.n_past= shared_tokens + (int) state.suffix.size ();
        total_dynamic_tokens += (int) state.suffix.size ();
      }

      for (int lane=0; lane<count; lane++) {
        llama_seq_id sequence= lane + 1;
        llama_memory_seq_rm (memory, sequence, -1, -1);
        llama_memory_seq_cp (memory, 0, sequence, 0, shared_tokens);
        llama_sampler_reset (samplers[(size_t) lane]);
      }

      if (total_dynamic_tokens == 0) {
        base += (size_t) count;
        bool more= base < requests.size () &&
                   !(cancelled && cancelled->load ());
        if (!reset_shared_cache (more)) break;
        continue;
      }
      auto finish= [&] (State& state) {
        state.done= true;
        if (!state.counted) {
          state.counted= true;
          if (completed) completed->fetch_add (1);
        }
      };
      auto accept_sample= [&] (State& state, llama_token token) {
        if (llama_vocab_is_eog (vocab, token)) {
          finish (state);
          return;
        }
        state.generated++;
        state.answer += token_piece (vocab, token);
        if (state.answer.find (']') != std::string::npos ||
            state.generated >= output_tokens) {
          finish (state);
          return;
        }
        state.pending_token= token;
        state.has_pending_token= true;
      };

      auto prompt_started= std::chrono::steady_clock::now ();
      llama_batch prompt= llama_batch_init (total_dynamic_tokens, 0, 1);
      for (State& state: states) {
        if (state.done) continue;
        for (size_t token=0; token<state.suffix.size (); token++) {
          bool logits= token + 1 == state.suffix.size ();
          if (logits) state.batch_index= prompt.n_tokens;
          batch_add (prompt, state.suffix[token],
                     shared_tokens + (int) token, state.lane + 1, logits);
        }
      }
      int prompt_status= llama_decode (ctx, prompt);
      llama_batch_free (prompt);
      if (prompt_status != 0) {
        range_warning ("definition-range question batch prefill failed");
        for (State& state: states) if (!state.done) finish (state);
      }
      else
        for (State& state: states)
          if (!state.done)
            accept_sample (state, llama_sampler_sample (
              samplers[(size_t) state.lane], ctx, state.batch_index));
      auto prompt_ms= std::chrono::duration_cast<std::chrono::milliseconds> (
        std::chrono::steady_clock::now () - prompt_started).count ();
      range_log ("definition-range question batch evaluated: requests=" +
                 std::to_string (count) + ", evaluated-tokens=" +
                 std::to_string (total_dynamic_tokens) + ", elapsed-ms=" +
                 std::to_string (prompt_ms));

      auto generation_started= std::chrono::steady_clock::now ();
      int pending_generation= 0;
      for (const State& state: states)
        if (!state.done && state.has_pending_token) pending_generation++;
      if (pending_generation > 0) {
        llama_batch first_tokens=
          llama_batch_init (pending_generation, 0, 1);
        for (State& state: states) {
          if (state.done || !state.has_pending_token) continue;
          state.batch_index= first_tokens.n_tokens;
          batch_add (first_tokens, state.pending_token, state.n_past++,
                     state.lane + 1, true);
          state.has_pending_token= false;
        }
        int status= llama_decode (ctx, first_tokens);
        llama_batch_free (first_tokens);
        if (status != 0) {
          range_warning ("definition-range first generation batch failed");
          for (State& state: states) if (!state.done) finish (state);
        }
      }

      for (int step=1; step<output_tokens; step++) {
        if (cancelled && cancelled->load ()) break;
        int active= 0;
        for (const State& state: states) if (!state.done) active++;
        if (active == 0) break;
        llama_batch next= llama_batch_init (active, 0, 1);
        for (State& state: states) {
          if (state.done) continue;
          llama_token token= llama_sampler_sample (
            samplers[(size_t) state.lane], ctx, state.batch_index);
          accept_sample (state, token);
          if (state.done) continue;
          state.batch_index= next.n_tokens;
          batch_add (next, state.pending_token, state.n_past++, state.lane + 1,
                     true);
          state.has_pending_token= false;
        }
        if (next.n_tokens == 0) {
          llama_batch_free (next);
          break;
        }
        int status= llama_decode (ctx, next);
        llama_batch_free (next);
        if (status != 0) {
          range_warning ("definition-range batched generation decode failed");
          break;
        }
      }
      auto generation_ms= std::chrono::duration_cast<std::chrono::milliseconds> (
        std::chrono::steady_clock::now () - generation_started).count ();
      for (State& state: states) {
        if (!state.done) finish (state);
        const AthenaArtifactRangeRequest& request= requests[state.request];
        results[state.request]= athena_artifact_parse_definition_range_output (
          state.answer, request.paragraphs, fallback_to_paragraph_zero);
        std::string output_log= "definition-range model output: request=" +
          std::to_string (state.request + 1);
        if (fallback_to_paragraph_zero)
          output_log += ", keyword=\"" + request.keyword_latex +
                        "\", answer=" + trim (state.answer);
        else
          output_log += ", valid=" +
                        std::string (results[state.request].empty () ?
                                     "false": "true");
        output_log += ", generated-tokens=" +
                      std::to_string (state.generated);
        range_log (output_log);
      }
      range_log ("definition-range generation batch complete: requests=" +
                 std::to_string (count) + ", elapsed-ms=" +
                 std::to_string (generation_ms));
      base += (size_t) count;
      bool more= base < requests.size () &&
                 !(cancelled && cancelled->load ());
      if (!reset_shared_cache (more)) break;
    }
    auto all_ms= std::chrono::duration_cast<std::chrono::milliseconds> (
      std::chrono::steady_clock::now () - all_started).count ();
    range_log ("definition-range batch complete: requests=" +
               std::to_string (requests.size ()) + ", elapsed-ms=" +
               std::to_string (all_ms));
    return results;
  }

private:
  friend void ::athena_artifact_range_model_release ();

  bool decode_padded (const std::vector<llama_token>& tokens, int start_pos,
                      bool logits_on_last, int& logits_index) {
    if (tokens.empty ()) return false;
    llama_memory_t memory= llama_get_memory (ctx);
    logits_index= -1;
    for (size_t offset=0; offset<tokens.size ();) {
      int take= std::min (
        prefill_batch_tokens, (int) (tokens.size () - offset));
      bool final= offset + (size_t) take == tokens.size ();
      llama_batch batch= llama_batch_init (prefill_batch_tokens, 0, 1);
      for (int i=0; i<take; i++) {
        bool logits= final && logits_on_last && i + 1 == take;
        if (logits) logits_index= batch.n_tokens;
        batch_add (batch, tokens[offset + (size_t) i],
                   start_pos + (int) offset + i, 0, logits);
      }
      int padded_tokens= final
        ? std::min (prefill_batch_tokens,
                    ((batch.n_tokens + prefill_padding_quantum - 1) /
                     prefill_padding_quantum) * prefill_padding_quantum)
        : batch.n_tokens;
      int padding= padded_tokens - batch.n_tokens;
      for (int i=0; i<padding; i++)
        batch_add (batch, tokens.back (),
                   start_pos + (int) tokens.size () + i, 0, false);
      int status= llama_decode (ctx, batch);
      llama_batch_free (batch);
      if (padding > 0)
        llama_memory_seq_rm (
          memory, 0, start_pos + (int) tokens.size (),
          start_pos + (int) tokens.size () + padding);
      if (status != 0) return false;
      offset += (size_t) take;
    }
    return !logits_on_last || logits_index >= 0;
  }

  bool prepare_prefix_cache () {
    llama_memory_t memory= llama_get_memory (ctx);
    if (prefix_cached) {
      if (llama_memory_seq_rm (memory, 0, prefix_tokens, -1)) return true;
      range_warning ("could not trim dynamic definition-range KV cache; "
                     "rebuilding its fixed prompt prefix");
      prefix_cached= false;
    }
    llama_memory_clear (memory, true);
    const llama_vocab* vocab= llama_model_get_vocab (model);
    std::vector<llama_token> tokens= tokenize (vocab, prompt_prefix ());
    if (tokens.empty () || (int) tokens.size () + output_tokens >= context_tokens)
      return false;
    auto started= std::chrono::steady_clock::now ();
    int ignored_logits= -1;
    if (!decode_padded (tokens, 0, false, ignored_logits)) {
      llama_memory_clear (memory, true);
      return false;
    }
    prefix_tokens= (int) tokens.size ();
    prefix_cached= true;
    auto elapsed= std::chrono::duration_cast<std::chrono::milliseconds> (
      std::chrono::steady_clock::now () - started).count ();
    range_log ("definition-range fixed prompt cached: tokens=" +
               std::to_string (prefix_tokens) + ", elapsed-ms=" +
               std::to_string (elapsed));
    return true;
  }

  bool load (const std::string& path, int parallelism) {
    int gpu_layers= configured_gpu_layers ();
    if (model && path == loaded_path && parallelism <= parallel_capacity &&
        gpu_layers == loaded_gpu_layers)
      return true;
    unload ();
    if (!exists (url_system (std_tm (path)))) {
      if (warned_path != path) {
        range_warning ("definition-range GGUF model not found at " + path +
                       "; bold artifacts will use paragraph 0");
        warned_path= path;
      }
      return false;
    }
    auto started= std::chrono::steady_clock::now ();
    range_log ("loading definition-range model: " + path);
    athena_llama_runtime_initialize ();
    llama_model_params mp= llama_model_default_params ();
    mp.n_gpu_layers= gpu_layers;
    model= llama_model_load_from_file (path.c_str (), mp);
    if (!model) {
      range_warning ("could not load definition-range model " + path);
      return false;
    }
    llama_context_params cp= llama_context_default_params ();
    parallel_capacity= parallelism;
    cp.n_seq_max= (uint32_t) parallel_capacity + 1;
    cp.n_ctx= context_tokens + 256 * (uint32_t) parallel_capacity;
    // The admission check permits the combined per-sequence suffixes to fill
    // the logical context.  n_batch must cover that same bound; n_ubatch still
    // limits each physical compute chunk to keep GPU work bounded.
    cp.n_batch= cp.n_ctx;
    cp.n_ubatch= prefill_batch_tokens;
    cp.kv_unified= true;
    // Intel's SYCL Flash Attention path is substantially slower for the
    // short, heavily-prefilled artifact-range workload.
    cp.flash_attn_type= LLAMA_FLASH_ATTN_TYPE_DISABLED;
    cp.n_threads= std::min (
      artifact_threads,
      std::max (1, (int) std::thread::hardware_concurrency ()));
    cp.n_threads_batch= cp.n_threads;
    range_log ("definition-range runtime: gpu-layers=" +
               (gpu_layers < 0 ? std::string ("all") :
                                 std::to_string (gpu_layers)) +
               ", threads=" + std::to_string (cp.n_threads) +
               ", parallel-sequences=" +
               std::to_string (parallel_capacity) + ", batch-tokens=" +
               std::to_string (cp.n_batch) + ", microbatch-tokens=" +
               std::to_string (cp.n_ubatch));
    ctx= llama_init_from_model (model, cp);
    if (!ctx) { unload (); return false; }
    for (int lane=0; lane<parallel_capacity; lane++) {
      llama_sampler* sampler=
        llama_sampler_chain_init (llama_sampler_chain_default_params ());
      llama_sampler_chain_add (sampler, llama_sampler_init_greedy ());
      samplers.push_back (sampler);
    }
    loaded_path= path;
    loaded_gpu_layers= gpu_layers;
    auto elapsed= std::chrono::duration_cast<std::chrono::milliseconds> (
      std::chrono::steady_clock::now () - started).count ();
    range_log ("definition-range model ready in " +
               std::to_string (elapsed) + " ms");
    return true;
  }

  void unload () {
    for (llama_sampler* sampler: samplers) llama_sampler_free (sampler);
    samplers.clear ();
    if (ctx) llama_free (ctx);
    if (model) llama_model_free (model);
    ctx= nullptr; model= nullptr; loaded_path.clear ();
    prefix_cached= false;
    prefix_tokens= 0;
    parallel_capacity= 0;
    loaded_gpu_layers= 0;
  }

  std::mutex mutex;
  llama_model* model= nullptr;
  llama_context* ctx= nullptr;
  std::vector<llama_sampler*> samplers;
  std::string loaded_path;
  std::string warned_path;
  bool prefix_cached= false;
  int prefix_tokens= 0;
  int parallel_capacity= 0;
  int loaded_gpu_layers= 0;
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
      range_warning ("definition-range GGUF model not found at " + path +
                     "; bold artifacts will use paragraph 0");
      warned_path= path;
    }
  }
  return available;
}

void
athena_artifact_range_model_release () {
  RangeModel& model= range_model ();
  std::lock_guard<std::mutex> guard (model.mutex);
  model.unload ();
}

int
athena_artifact_range_batch_size () {
  return configured_batch_size ();
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
  if (trim (body).empty ()) return {};
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

  if (std::find (offsets.begin (), offsets.end (), 0) == offsets.end () ||
      !std::is_sorted (offsets.begin (), offsets.end ()))
    return invalid ();
  for (size_t i=1; i<offsets.size (); i++)
    if (offsets[i] != offsets[i - 1] + 1) return invalid ();
  return offsets;
}

std::vector<std::vector<int>>
athena_artifact_select_definition_ranges (
  const std::vector<AthenaArtifactRangeRequest>& requests,
  const std::string& model_path, const std::atomic<bool>* cancelled,
  std::atomic<size_t>* completed, bool fallback_to_paragraph_zero) {
  return range_model ().select_many (
    requests, model_path, configured_batch_size (), cancelled, completed,
    fallback_to_paragraph_zero);
}

std::vector<int>
athena_artifact_select_definition_range (
  const std::string& keyword_latex,
  const std::vector<std::pair<int,std::string>>& paragraphs) {
  AthenaArtifactRangeRequest request {keyword_latex, paragraphs};
  auto results= range_model ().select_many (
    {request}, configured_model (), 1, nullptr, nullptr, true);
  return results.empty () ? std::vector<int> {0} : results[0];
}

std::vector<int>
athena_artifact_select_definition_range (
  const std::string& keyword_latex,
  const std::vector<std::pair<int,std::string>>& paragraphs,
  const std::string& model_path, const std::atomic<bool>* cancelled) {
  AthenaArtifactRangeRequest request {keyword_latex, paragraphs};
  auto results= range_model ().select_many (
    {request}, model_path, 1, cancelled, nullptr, true);
  return results.empty () ? std::vector<int> {0} : results[0];
}
