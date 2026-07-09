/******************************************************************************
* MODULE     : rag_embedding.cpp
* DESCRIPTION: Optional llama.cpp embeddings for Continuous RAG
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "rag_embedding.hpp"

#include "tm_ostream.hpp"

#include <sodium.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include <ggml-backend.h>
#include <common.h>
#include <llama.h>

namespace fs = std::filesystem;

namespace athena::rag {
namespace {

static bool llama_ready= false;
static std::mutex llama_ready_mutex;
constexpr int embedding_context_tokens= 8192;
constexpr int embedding_batch_tokens= 4096;
constexpr int embedding_max_sequences= 8;

static void
ensure_llama_ready () {
  std::lock_guard<std::mutex> lock (llama_ready_mutex);
  if (llama_ready) return;
  ggml_backend_load_all ();
  common_init ();
  llama_backend_init ();
  llama_ready= true;
}

static std::string
stable_file_fingerprint (const std::string& model_path) {
  if (sodium_init () < 0) return model_path;
  std::ifstream in (model_path, std::ios::binary);
  if (!in) return model_path;
  crypto_generichash_state state;
  crypto_generichash_init (&state, nullptr, 0, crypto_generichash_BYTES);
  std::vector<char> buffer (1024 * 1024);
  while (in) {
    in.read (buffer.data (), std::streamsize (buffer.size ()));
    std::streamsize got= in.gcount ();
    if (got > 0)
      crypto_generichash_update (
        &state, reinterpret_cast<const unsigned char*> (buffer.data ()),
        size_t (got));
  }
  unsigned char digest[crypto_generichash_BYTES];
  crypto_generichash_final (&state, digest, sizeof (digest));
  static const char* hex= "0123456789abcdef";
  std::string out= "blake2b:";
  out.reserve (out.size () + sizeof (digest) * 2);
  for (unsigned char c: digest) {
    out.push_back (hex[(c >> 4) & 15]);
    out.push_back (hex[c & 15]);
  }
  return out;
}

static void
llama_log_bridge (ggml_log_level level, const char* text, void*) {
  if (text == nullptr) return;
  std::string line (text);
  bool progress_dots= !line.empty ();
  for (char c: line)
    if (c != '.' && c != ' ' && c != '\t' && c != '\r' && c != '\n') {
      progress_dots= false;
      break;
    }
  if (progress_dots) return;
  if (level >= GGML_LOG_LEVEL_ERROR)
    std_error << "rag llama.cpp: " << line.c_str ();
  else if (level >= GGML_LOG_LEVEL_WARN)
    std_warning << "rag llama.cpp: " << line.c_str ();
}

} // namespace

struct RagEmbedder::Impl {
  common_init_result_ptr init;
  int dim= 0;
  std::string fingerprint;

  llama_model* model () const {
    return init? init->model (): nullptr;
  }

  llama_context* context () const {
    return init? init->context (): nullptr;
  }
};

static std::vector<llama_token>
tokenize_text (llama_context* ctx, const std::string& text) {
  if (ctx == nullptr) return {};
  std::vector<llama_token> tokens= common_tokenize (ctx, text, true, true);
  if (int (tokens.size ()) > embedding_context_tokens)
    tokens.resize (embedding_context_tokens);
  return tokens;
}

RagEmbedder::RagEmbedder ()
  : impl (new Impl) {}

RagEmbedder::~RagEmbedder () {
  delete impl;
}

bool
RagEmbedder::open (const std::string& model_path,
                   const std::string& device_mode,
                   int threads) {
  if (model_path.empty ()) return false;
  if (impl->model () != nullptr) return true;
  if (!fs::exists (model_path)) {
    std_warning << "rag embedding: model not found: "
                << model_path.c_str () << "\n";
    return false;
  }

  ensure_llama_ready ();
  llama_log_set (llama_log_bridge, nullptr);

  common_params params;
  params.model.path= model_path;
  params.embedding= true;
  params.n_ctx= embedding_context_tokens;
  params.n_batch= embedding_context_tokens;
  params.n_ubatch= embedding_context_tokens;
  params.n_parallel= embedding_max_sequences;
  params.pooling_type= LLAMA_POOLING_TYPE_UNSPECIFIED;
  params.attention_type= LLAMA_ATTENTION_TYPE_UNSPECIFIED;
  params.flash_attn_type= LLAMA_FLASH_ATTN_TYPE_DISABLED;
  params.embd_normalize= 2;
  params.warmup= false;

  bool cpu_only= device_mode == "cpu";
  if (cpu_only) params.n_gpu_layers= 0;
  else params.n_gpu_layers= 99;

  unsigned hw= std::max (1u, std::thread::hardware_concurrency ());
  if (threads > 0) {
    params.cpuparams.n_threads= threads;
    params.cpuparams_batch.n_threads= threads;
  }
  else {
    params.cpuparams.n_threads= std::max (1u, hw / 2);
    params.cpuparams_batch.n_threads= hw;
  }

  llama_numa_init (params.numa);
  impl->init= common_init_from_params (params);
  if (!impl->init || impl->model () == nullptr || impl->context () == nullptr) {
    std_warning << "rag embedding: failed to initialize context for "
                << model_path.c_str () << "\n";
    impl->init.reset ();
    return false;
  }

  impl->dim= llama_model_n_embd_out (impl->model ());
  impl->fingerprint= stable_file_fingerprint (model_path);
  io_info << "rag embedding: loaded " << model_path.c_str () << " dim="
          << impl->dim << "\n";
  return true;
}

bool
RagEmbedder::available () const {
  return impl->model () != nullptr && impl->context () != nullptr &&
         impl->dim > 0;
}

int
RagEmbedder::dimension () const {
  return impl->dim;
}

std::string
RagEmbedder::model_fingerprint () const {
  return impl->fingerprint;
}

std::vector<float>
RagEmbedder::embed (const std::string& text) {
  std::vector<std::vector<float>> many= embed_many ({ text });
  return many.empty () ? std::vector<float> () : many[0];
}

std::vector<std::vector<float>>
RagEmbedder::embed_many (
  const std::vector<std::string>& texts,
  const std::function<void(size_t,size_t)>& progress) {
  std::vector<std::vector<float>> out (texts.size ());
  if (!available () || texts.empty ()) return out;

  std::vector<std::vector<llama_token>> tokenized (texts.size ());
  for (size_t i=0; i<texts.size (); i++) {
    if (texts[i].empty ()) continue;
    std::string clipped= texts[i];
    if (clipped.size () > 12000) clipped.resize (12000);
    tokenized[i]= tokenize_text (impl->context (), clipped);
  }

  size_t cursor_text= 0;
  size_t done_texts= 0;
  while (cursor_text < texts.size ()) {
    while (cursor_text < texts.size () && tokenized[cursor_text].empty ()) {
      cursor_text++;
      done_texts++;
      if (progress) progress (done_texts, texts.size ());
    }
    if (cursor_text >= texts.size ()) break;

    std::vector<size_t> selected;
    int total_tokens= 0;
    size_t scan= cursor_text;
    while (scan < texts.size ()) {
      int n= int (tokenized[scan].size ());
      if (n == 0) {
        scan++;
        continue;
      }
      if (int (selected.size ()) >= embedding_max_sequences) break;
      if (total_tokens > 0 && total_tokens + n > embedding_batch_tokens)
        break;
      total_tokens += n;
      selected.push_back (scan);
      scan++;
      if (total_tokens >= embedding_batch_tokens) break;
    }
    if (selected.empty ()) {
      cursor_text++;
      done_texts++;
      if (progress) progress (done_texts, texts.size ());
      continue;
    }
    if (total_tokens <= 0) break;

    llama_batch batch= llama_batch_init (embedding_context_tokens, 0, 1);
    std::vector<int> last_index (selected.size (), -1);
    for (size_t k=0; k<selected.size (); k++) {
      size_t i= selected[k];
      const std::vector<llama_token>& toks= tokenized[i];
      llama_seq_id seq= llama_seq_id (k);
      for (size_t j=0; j<toks.size (); j++) {
        common_batch_add (batch, toks[j], llama_pos (j), { seq }, true);
        last_index[k]= batch.n_tokens - 1;
      }
    }

    llama_memory_clear (llama_get_memory (impl->context ()), true);
    int rc= llama_decode (impl->context (), batch);
    if (rc != 0) {
      std_warning << "rag embedding: llama batch evaluation failed rc="
                  << rc << "\n";
      llama_batch_free (batch);
      done_texts += selected.size ();
      if (progress) progress (done_texts, texts.size ());
      cursor_text= selected.back () + 1;
      continue;
    }

    llama_synchronize (impl->context ());
    for (size_t k=0; k<selected.size (); k++) {
      size_t i= selected[k];
      llama_seq_id seq= llama_seq_id (k);
      const float* raw= llama_get_embeddings_seq (impl->context (), seq);
      if (raw == nullptr && last_index[k] >= 0)
        raw= llama_get_embeddings_ith (impl->context (), last_index[k]);
      if (raw == nullptr) continue;
      out[i].assign ((size_t) impl->dim, 0.0f);
      common_embd_normalize (raw, out[i].data (), impl->dim, 2);
    }
    llama_batch_free (batch);
    done_texts += selected.size ();
    if (progress) progress (done_texts, texts.size ());
    cursor_text= selected.back () + 1;
  }
  return out;
}

} // namespace athena::rag
