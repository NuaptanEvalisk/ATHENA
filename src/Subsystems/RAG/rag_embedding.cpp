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

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <thread>

#include <ggml-backend.h>
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
  llama_backend_init ();
  llama_ready= true;
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

static float
vector_norm (const std::vector<float>& xs) {
  double sum= 0.0;
  for (float x: xs) sum += double (x) * double (x);
  return float (std::sqrt (sum));
}

} // namespace

struct RagEmbedder::Impl {
  llama_model* model= nullptr;
  llama_context* ctx= nullptr;
  int dim= 0;
  std::string fingerprint;
};

static std::vector<llama_token>
tokenize_text (llama_model* model, const std::string& text) {
  const llama_vocab* vocab= llama_model_get_vocab (model);
  if (vocab == nullptr) return {};
  int n= -llama_tokenize (vocab, text.c_str (), int (text.size ()),
                          nullptr, 0, true, false);
  if (n <= 0) return {};
  std::vector<llama_token> tokens ((size_t) n);
  int got= llama_tokenize (vocab, text.c_str (), int (text.size ()),
                           tokens.data (), n, true, false);
  if (got <= 0) return {};
  tokens.resize (size_t (got));
  if (int (tokens.size ()) > embedding_context_tokens)
    tokens.resize (embedding_context_tokens);
  return tokens;
}

static void
normalize_vector (std::vector<float>& xs) {
  float norm= vector_norm (xs);
  if (norm > 0.0f)
    for (float& x: xs) x /= norm;
}

RagEmbedder::RagEmbedder ()
  : impl (new Impl) {}

RagEmbedder::~RagEmbedder () {
  if (impl->ctx != nullptr) llama_free (impl->ctx);
  if (impl->model != nullptr) llama_model_free (impl->model);
  delete impl;
}

bool
RagEmbedder::open (const std::string& model_path,
                   const std::string& device_mode,
                   int threads) {
  if (model_path.empty ()) return false;
  if (impl->model != nullptr) return true;
  if (!fs::exists (model_path)) {
    std_warning << "rag embedding: model not found: "
                << model_path.c_str () << "\n";
    return false;
  }

  ensure_llama_ready ();
  llama_log_set (llama_log_bridge, nullptr);

  llama_model_params mparams= llama_model_default_params ();
  bool cpu_only= device_mode == "cpu";
  if (cpu_only) mparams.n_gpu_layers= 0;
  impl->model= llama_model_load_from_file (model_path.c_str (), mparams);
  if (impl->model == nullptr) {
    std_warning << "rag embedding: failed to load GGUF model "
                << model_path.c_str () << "\n";
    return false;
  }

  llama_context_params cparams= llama_context_default_params ();
  cparams.n_ctx= embedding_context_tokens * embedding_max_sequences;
  cparams.n_batch= embedding_context_tokens;
  cparams.n_ubatch= embedding_context_tokens;
  cparams.n_seq_max= embedding_max_sequences;
  unsigned hw= std::max (1u, std::thread::hardware_concurrency ());
  if (threads > 0) {
    cparams.n_threads= threads;
    cparams.n_threads_batch= threads;
  }
  else {
    cparams.n_threads= std::max (1u, hw / 2);
    cparams.n_threads_batch= hw;
  }
  cparams.embeddings= true;
  cparams.pooling_type= LLAMA_POOLING_TYPE_UNSPECIFIED;
  cparams.attention_type= LLAMA_ATTENTION_TYPE_UNSPECIFIED;
  if (cpu_only) {
    cparams.offload_kqv= false;
    cparams.op_offload= false;
  }
  impl->ctx= llama_init_from_model (impl->model, cparams);
  if (impl->ctx == nullptr) {
    std_warning << "rag embedding: failed to initialize context for "
                << model_path.c_str () << "\n";
    llama_model_free (impl->model);
    impl->model= nullptr;
    return false;
  }

  impl->dim= llama_model_n_embd (impl->model);
  std::error_code ec;
  fs::file_time_type mt= fs::last_write_time (model_path, ec);
  uintmax_t size= fs::file_size (model_path, ec);
  impl->fingerprint= model_path + ":" + std::to_string (size) + ":" +
                     std::to_string (
                       ec? 0:
                       std::chrono::duration_cast<std::chrono::nanoseconds> (
                         mt.time_since_epoch ()).count ());
  io_info << "rag embedding: loaded " << model_path.c_str () << " dim="
          << impl->dim << "\n";
  return true;
}

bool
RagEmbedder::available () const {
  return impl->model != nullptr && impl->ctx != nullptr && impl->dim > 0;
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
    tokenized[i]= tokenize_text (impl->model, clipped);
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

    llama_batch batch= llama_batch_init (total_tokens, 0, 1);
    batch.n_tokens= total_tokens;
    std::vector<int> last_index (selected.size (), -1);
    int cursor= 0;
    for (size_t k=0; k<selected.size (); k++) {
      size_t i= selected[k];
      const std::vector<llama_token>& toks= tokenized[i];
      llama_seq_id seq= llama_seq_id (k);
      for (size_t j=0; j<toks.size (); j++) {
        batch.token[cursor]= toks[j];
        batch.pos[cursor]= llama_pos (j);
        batch.n_seq_id[cursor]= 1;
        batch.seq_id[cursor][0]= seq;
        batch.logits[cursor]= 1;
        last_index[k]= cursor;
        cursor++;
      }
    }
    batch.n_tokens= cursor;

    int rc= llama_model_has_encoder (impl->model)
      ? llama_encode (impl->ctx, batch)
      : llama_decode (impl->ctx, batch);
    if (rc != 0) {
      std_warning << "rag embedding: llama batch evaluation failed rc="
                  << rc << "\n";
      llama_batch_free (batch);
      done_texts += selected.size ();
      if (progress) progress (done_texts, texts.size ());
      cursor_text= selected.back () + 1;
      continue;
    }

    llama_synchronize (impl->ctx);
    for (size_t k=0; k<selected.size (); k++) {
      size_t i= selected[k];
      llama_seq_id seq= llama_seq_id (k);
      float* raw= llama_get_embeddings_seq (impl->ctx, seq);
      if (raw == nullptr && last_index[k] >= 0)
        raw= llama_get_embeddings_ith (impl->ctx, last_index[k]);
      if (raw == nullptr) continue;
      out[i]= std::vector<float> (raw, raw + impl->dim);
      normalize_vector (out[i]);
    }
    llama_batch_free (batch);
    done_texts += selected.size ();
    if (progress) progress (done_texts, texts.size ());
    cursor_text= selected.back () + 1;
  }
  return out;
}

} // namespace athena::rag
