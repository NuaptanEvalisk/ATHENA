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
#include <chat.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <thread>

namespace {

constexpr int base_context_tokens= 8192;
constexpr int context_tokens_per_lane= 768;
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

struct ChatEnvelope {
  std::string prefix;
  std::string suffix;
};

ChatEnvelope chat_envelope (const common_chat_templates* templates,
                            const std::string& instructions) {
  static const std::string marker=
    "\n<ATHENA_ARTIFACT_DYNAMIC_REQUEST_9F4738C1>\n";
  std::string content= instructions + marker;
  if (!templates) return {instructions, ""};
  common_chat_templates_inputs inputs;
  inputs.messages.push_back ({"user", content});
  inputs.add_generation_prompt= true;
  inputs.use_jinja= true;
  inputs.enable_thinking= false;
  std::string prompt= common_chat_templates_apply (templates, inputs).prompt;
  size_t split= prompt.find (marker);
  if (split == std::string::npos) return {instructions, ""};
  return {prompt.substr (0, split), prompt.substr (split + marker.size ())};
}

const std::string& definition_range_prompt () {
  static const std::string value=
         "The marked span has already been verified as the semantic name of "
         "a mathematical artifact. Select which nearby paragraphs constitute "
         "its definition in an ATHENA note.\n"
         "Return ONLY one bracketed sorted list of paragraph integers, for "
         "example [-1, 0, 1]. You may give every integer or only sorted range "
         "endpoints; ATHENA includes all intervening paragraphs. Do not "
         "explain, reason, use prose, or emit markdown. Paragraph 0 MUST be "
         "included. Select only "
         "supplied integers and the smallest contiguous semantic range that "
         "defines the marked span; exclude headings, examples, later "
         "consequences, conversation, and unrelated text. Stop before a "
         "neighboring paragraph that begins an independent definition or "
         "introduces a different marked expression. If the supplied context "
         "ends while the definition still continues, include that boundary "
         "paragraph; ATHENA will then supply more context. A displayed formula "
         "attached to a paragraph is part of "
         "that paragraph.\n\n"
         "=== BEGIN PARAGRAPH -1 ===\nWe now introduce a useful class.\n"
         "=== END PARAGRAPH -1 ===\n"
         "=== BEGIN PARAGRAPH 0 ===\nA bounded operator is called "
         "\\textbf{compact} when it maps bounded sets to relatively compact "
         "sets.\n=== END PARAGRAPH 0 ===\n"
         "=== BEGIN PARAGRAPH 1 ===\nThe identity is not compact in infinite "
         "dimension.\n=== END PARAGRAPH 1 ===\n"
         "Keyword (LaTeX): compact operator\nAnswer: [0]\n\n"
         "=== BEGIN PARAGRAPH 0 ===\nA map p:E\\to X is a "
         "\\textbf{covering space} if the following local condition holds.\n"
         "=== END PARAGRAPH 0 ===\n"
         "=== BEGIN PARAGRAPH 1 ===\nEvery x\\in X has an open neighborhood U "
         "whose inverse image is a disjoint union of open sets mapped "
         "homeomorphically onto U.\n=== END PARAGRAPH 1 ===\n"
         "Keyword (LaTeX): covering space\nAnswer: [0, 1]\n\n"
         ;
  return value;
}

std::string make_context_prefix (const AthenaArtifactRangeRequest& request) {
  std::ostringstream out;
  for (const auto& paragraph: request.paragraphs)
    out << "=== BEGIN PARAGRAPH " << paragraph.first << " ===\n"
        << paragraph.second << "\n=== END PARAGRAPH " << paragraph.first
        << " ===\n";
  return out.str ();
}

std::string make_keyword_tail (const AthenaArtifactRangeRequest& request) {
  std::ostringstream out;
  out << "Keyword (LaTeX): " << request.keyword_latex << "\nAnswer:";
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
    ChatEnvelope envelope= chat_envelope (
      chat_templates.get (), definition_range_prompt ());
    if (!prepare_prefix_cache (envelope.prefix)) return results;
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
      bool ready= prepare_prefix_cache (envelope.prefix);
      if (ready)
        range_log ("definition-range fixed prompt cache rebuilt for next "
                   "request group");
      return ready;
    };

    struct State {
      size_t request= 0;
      int lane= 0;
      const std::vector<llama_token>* context= nullptr;
      const std::vector<llama_token>* tail= nullptr;
      std::string answer;
      int generated= 0;
      int n_past= 0;
      int batch_index= -1;
      llama_token pending_token= 0;
      bool has_pending_token= false;
      bool done= false;
      bool counted= false;
    };

    struct TokenizedRequest {
      std::vector<llama_token> context;
      std::vector<llama_token> tail;
    };
    std::vector<TokenizedRequest> tokenized;
    tokenized.reserve (requests.size ());
    for (const AthenaArtifactRangeRequest& request: requests) {
      TokenizedRequest item;
      item.context= tokenize (vocab, make_context_prefix (request), false);
      item.tail= tokenize (
        vocab, make_keyword_tail (request) + envelope.suffix, false);
      tokenized.push_back (std::move (item));
    }

    struct PackedGroup {
      std::vector<size_t> requests;
      std::vector<size_t> context_representatives;
      int dynamic_tokens= 0;
    };
    auto has_context= [&] (const PackedGroup& group, size_t request) {
      for (size_t representative: group.context_representatives)
        if (tokenized[representative].context == tokenized[request].context)
          return true;
      return false;
    };
    std::vector<size_t> order (requests.size ());
    for (size_t i=0; i<order.size (); i++) order[i]= i;
    std::stable_sort (order.begin (), order.end (), [&] (size_t a, size_t b) {
      return tokenized[a].context.size () + tokenized[a].tail.size () >
             tokenized[b].context.size () + tokenized[b].tail.size ();
    });
    std::vector<PackedGroup> groups;
    for (size_t request: order) {
      int context_tokens= (int) tokenized[request].context.size ();
      int tail_tokens= (int) tokenized[request].tail.size ();
      int request_tokens= context_tokens + tail_tokens;
      if (tail_tokens == 0 ||
          prefix_tokens + request_tokens + output_tokens >= context_capacity) {
        if (request_tokens > 0)
          range_warning (
            "definition-range request requires " +
            std::to_string (prefix_tokens + request_tokens + output_tokens) +
            " tokens, exceeding the " + std::to_string (context_capacity) +
            "-token model context");
        if (completed) completed->fetch_add (1);
        continue;
      }
      size_t best= groups.size ();
      int best_remaining= context_capacity;
      for (size_t i=0; i<groups.size (); i++) {
        const PackedGroup& group= groups[i];
        int count= (int) group.requests.size () + 1;
        if (count > parallelism) continue;
        int marginal_context= has_context (group, request) ? 0: context_tokens;
        int required= prefix_tokens + group.dynamic_tokens +
                      marginal_context + tail_tokens +
                      count * output_tokens;
        if (required >= context_capacity) continue;
        int remaining= context_capacity - required;
        if (remaining < best_remaining) {
          best= i;
          best_remaining= remaining;
        }
      }
      if (best == groups.size ()) {
        groups.push_back (PackedGroup ());
        best= groups.size () - 1;
      }
      PackedGroup& group= groups[best];
      if (!has_context (group, request)) {
        group.context_representatives.push_back (request);
        group.dynamic_tokens += context_tokens;
      }
      group.requests.push_back (request);
      group.dynamic_tokens += tail_tokens;
    }
    range_log ("definition-range token packing: groups=" +
               std::to_string (groups.size ()) + ", requests=" +
               std::to_string (requests.size ()));

    for (size_t group_index=0; group_index<groups.size (); group_index++) {
      if (cancelled && cancelled->load ()) break;
      PackedGroup& group= groups[group_index];
      int count= (int) group.requests.size ();

      int shared_tokens= prefix_tokens;

      std::vector<State> states ((size_t) count);
      int total_tail_tokens= 0;
      int naive_context_tokens= 0;
      for (int lane=0; lane<count; lane++) {
        State& state= states[(size_t) lane];
        state.request= group.requests[(size_t) lane];
        state.lane= lane;
        state.context= &tokenized[state.request].context;
        state.tail= &tokenized[state.request].tail;
        if (state.tail->empty ()) {
          state.done= true;
          if (completed) completed->fetch_add (1);
          continue;
        }
        state.n_past= shared_tokens + (int) state.context->size () +
                      (int) state.tail->size ();
        naive_context_tokens += (int) state.context->size ();
        total_tail_tokens += (int) state.tail->size ();
      }

      for (int lane=0; lane<count; lane++) {
        llama_seq_id sequence= lane + 1;
        llama_memory_seq_rm (memory, sequence, -1, -1);
        llama_sampler_reset (samplers[(size_t) lane]);
      }

      if (total_tail_tokens == 0) {
        bool more= group_index + 1 < groups.size () &&
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

      struct SharedContext {
        int leader= 0;
        std::vector<int> lanes;
      };
      std::vector<SharedContext> contexts;
      for (int lane=0; lane<count; lane++) {
        if (states[(size_t) lane].done) continue;
        size_t context= contexts.size ();
        for (size_t i=0; i<contexts.size (); i++)
          if (*states[(size_t) contexts[i].leader].context ==
              *states[(size_t) lane].context) {
            context= i;
            break;
          }
        if (context == contexts.size ())
          contexts.push_back ({lane, {lane}});
        else contexts[context].lanes.push_back (lane);
      }

      int unique_context_tokens= 0;
      for (const SharedContext& context: contexts) {
        const State& leader= states[(size_t) context.leader];
        unique_context_tokens += (int) leader.context->size ();
        llama_memory_seq_cp (
          memory, 0, leader.lane + 1, 0, shared_tokens);
      }

      auto prompt_started= std::chrono::steady_clock::now ();
      int prompt_status= 0;
      if (unique_context_tokens > 0) {
        llama_batch context_batch=
          llama_batch_init (unique_context_tokens, 0, 1);
        for (const SharedContext& context: contexts) {
          const State& leader= states[(size_t) context.leader];
          for (size_t token=0; token<leader.context->size (); token++)
            batch_add (context_batch, (*leader.context)[token],
                       shared_tokens + (int) token, leader.lane + 1, false);
        }
        prompt_status= llama_decode (ctx, context_batch);
        llama_batch_free (context_batch);
      }
      if (prompt_status != 0) {
        range_warning ("definition-range shared context prefill failed");
        for (State& state: states) if (!state.done) finish (state);
      }
      else {
        for (const SharedContext& context: contexts) {
          const State& leader= states[(size_t) context.leader];
          int end= shared_tokens + (int) leader.context->size ();
          for (size_t i=1; i<context.lanes.size (); i++)
            llama_memory_seq_cp (
              memory, leader.lane + 1, context.lanes[i] + 1, 0, end);
        }

        llama_batch tail_batch= llama_batch_init (total_tail_tokens, 0, 1);
        for (State& state: states) {
          if (state.done) continue;
          int start= shared_tokens + (int) state.context->size ();
          for (size_t token=0; token<state.tail->size (); token++) {
            bool logits= token + 1 == state.tail->size ();
            if (logits) state.batch_index= tail_batch.n_tokens;
            batch_add (tail_batch, (*state.tail)[token], start + (int) token,
                       state.lane + 1, logits);
          }
        }
        prompt_status= llama_decode (ctx, tail_batch);
        llama_batch_free (tail_batch);
        if (prompt_status != 0) {
          range_warning ("definition-range keyword batch prefill failed");
          for (State& state: states) if (!state.done) finish (state);
        }
      }
      if (prompt_status == 0)
        for (State& state: states)
          if (!state.done)
            accept_sample (state, llama_sampler_sample (
              samplers[(size_t) state.lane], ctx, state.batch_index));
      auto prompt_ms= std::chrono::duration_cast<std::chrono::milliseconds> (
        std::chrono::steady_clock::now () - prompt_started).count ();
      range_log ("definition-range question batch evaluated: requests=" +
                 std::to_string (count) + ", unique-contexts=" +
                 std::to_string (contexts.size ()) + ", evaluated-tokens=" +
                 std::to_string (unique_context_tokens + total_tail_tokens) +
                 ", reused-context-tokens=" +
                 std::to_string (naive_context_tokens -
                                 unique_context_tokens) + ", elapsed-ms=" +
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
      bool more= group_index + 1 < groups.size () &&
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

  bool prepare_prefix_cache (const std::string& prompt) {
    llama_memory_t memory= llama_get_memory (ctx);
    if (prefix_cached) {
      if (llama_memory_seq_rm (memory, 0, prefix_tokens, -1)) return true;
      range_warning ("could not trim dynamic definition-range KV cache; "
                     "rebuilding its fixed prompt prefix");
      prefix_cached= false;
    }
    llama_memory_clear (memory, true);
    const llama_vocab* vocab= llama_model_get_vocab (model);
    std::vector<llama_token> tokens= tokenize (vocab, prompt);
    if (tokens.empty () ||
        (int) tokens.size () + output_tokens >= context_capacity)
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
    chat_templates= common_chat_templates_init (model, "");
    if (!chat_templates)
      range_warning ("model has no usable chat template; artifact prompts "
                     "will use plain text");
    llama_context_params cp= llama_context_default_params ();
    parallel_capacity= parallelism;
    cp.n_seq_max= (uint32_t) parallel_capacity + 1;
    cp.n_ctx= base_context_tokens +
              context_tokens_per_lane * (uint32_t) parallel_capacity;
    context_capacity= (int) cp.n_ctx;
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
    chat_templates.reset ();
    if (model) llama_model_free (model);
    ctx= nullptr; model= nullptr; loaded_path.clear ();
    prefix_cached= false;
    prefix_tokens= 0;
    parallel_capacity= 0;
    context_capacity= 0;
    loaded_gpu_layers= 0;
  }

  std::mutex mutex;
  llama_model* model= nullptr;
  llama_context* ctx= nullptr;
  common_chat_templates_ptr chat_templates;
  std::vector<llama_sampler*> samplers;
  std::string loaded_path;
  std::string warned_path;
  bool prefix_cached= false;
  int prefix_tokens= 0;
  int parallel_capacity= 0;
  int context_capacity= 0;
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

std::string
athena_artifact_definition_range_cache_contract (
  const std::string& model_path) {
  namespace fs= std::filesystem;
  AthenaArtifactRangeRequest format_example {
    "<KEYWORD>", {{-1, "<PREVIOUS>"}, {0, "<FOCUS>"}, {1, "<NEXT>"}}
  };
  std::ostringstream out;
  out << definition_range_prompt () << "\nDYNAMIC REQUEST\n"
      << make_context_prefix (format_example)
      << make_keyword_tail (format_example)
      << "\nMODEL\n" << model_path << '\n';
  std::error_code error;
  fs::path path (model_path);
  if (fs::is_regular_file (path, error)) {
    auto size= fs::file_size (path, error);
    if (!error) out << size << '\n';
    error.clear ();
    auto modified= fs::last_write_time (path, error);
    if (!error) out << modified.time_since_epoch ().count () << '\n';
  }
  return out.str ();
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
  if (trim (body).empty ()) return invalid ();
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

  if (!std::is_sorted (offsets.begin (), offsets.end ()) ||
      offsets.front () > 0 || offsets.back () < 0)
    return invalid ();
  std::vector<int> contiguous;
  for (int offset=offsets.front (); offset<=offsets.back (); offset++) {
    if (!allowed.count (offset)) return invalid ();
    contiguous.push_back (offset);
  }
  return contiguous;
}

std::vector<std::vector<int>>
athena_artifact_select_definition_ranges_progressively (
  const std::vector<AthenaArtifactRangeRequest>& requests,
  const AthenaArtifactRangePass& pass, const std::atomic<bool>* cancelled,
  std::atomic<size_t>* completed, bool fallback_to_paragraph_zero) {
  struct Window {
    int available_left= 0;
    int available_right= 0;
    int visible_left= 0;
    int visible_right= 0;
    int left_step= 1;
    int right_step= 1;
  };

  std::vector<std::vector<int>> results (requests.size ());
  if (fallback_to_paragraph_zero)
    for (std::vector<int>& result: results) result= {0};
  if (completed) completed->store (0);
  if (requests.empty () || (cancelled && cancelled->load ())) return results;

  std::vector<Window> windows (requests.size ());
  std::vector<size_t> pending;
  for (size_t i=0; i<requests.size (); i++) {
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
      results[i]= {0};
      if (completed) completed->fetch_add (1);
      continue;
    }
    pending.push_back (i);
  }

  size_t wave_number= 0;
  while (!pending.empty () && !(cancelled && cancelled->load ())) {
    wave_number++;
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
    range_log ("definition-range adaptive wave=" +
               std::to_string (wave_number) + ", requests=" +
               std::to_string (wave.size ()));
    std::vector<std::vector<int>> selected= pass (wave);
    selected.resize (wave.size ());
    std::vector<size_t> next;
    for (size_t i=0; i<pending.size (); i++) {
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
      results[index]= std::move (choice);
      if (completed) completed->fetch_add (1);
    }
    pending= std::move (next);
  }
  return results;
}

std::vector<std::vector<int>>
athena_artifact_select_definition_ranges (
  const std::vector<AthenaArtifactRangeRequest>& requests,
  const std::string& model_path, const std::atomic<bool>* cancelled,
  std::atomic<size_t>* completed, bool fallback_to_paragraph_zero) {
  return athena_artifact_select_definition_ranges_progressively (
    requests,
    [&] (const std::vector<AthenaArtifactRangeRequest>& wave) {
      return range_model ().select_many (
        wave, model_path, configured_batch_size (), cancelled, nullptr,
        fallback_to_paragraph_zero);
    }, cancelled, completed, fallback_to_paragraph_zero);
}

std::vector<int>
athena_artifact_select_definition_range (
  const std::string& keyword_latex,
  const std::vector<std::pair<int,std::string>>& paragraphs) {
  AthenaArtifactRangeRequest request {keyword_latex, paragraphs};
  auto results= athena_artifact_select_definition_ranges (
    {request}, configured_model (), nullptr, nullptr, true);
  return results.empty () ? std::vector<int> {0} : results[0];
}

std::vector<int>
athena_artifact_select_definition_range (
  const std::string& keyword_latex,
  const std::vector<std::pair<int,std::string>>& paragraphs,
  const std::string& model_path, const std::atomic<bool>* cancelled) {
  AthenaArtifactRangeRequest request {keyword_latex, paragraphs};
  auto results= athena_artifact_select_definition_ranges (
    {request}, model_path, cancelled, nullptr, true);
  return results.empty () ? std::vector<int> {0} : results[0];
}
