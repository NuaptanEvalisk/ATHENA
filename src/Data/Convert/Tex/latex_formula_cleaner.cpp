/******************************************************************************
* MODULE     : latex_formula_cleaner.cpp
* DESCRIPTION: llama.cpp backed cleanup of imported LaTeX formula snippets
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "latex_formula_cleaner.hpp"

#include "ATHENA/Data/llama_runtime.hpp"
#include "boot.hpp"
#include "file.hpp"
#include "Scheme/scheme.hpp"
#include "tm_ostream.hpp"

#include <llama.h>

#include <algorithm>
#include <cctype>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int cleaner_context_tokens= 2048;
constexpr int cleaner_max_new_tokens= 1024;

std::string
tm_to_std_string (string s) {
  return std::string (as_charp (s), N(s));
}

string
std_to_tm_string (const std::string& s) {
  return string (s.c_str ());
}

std::string
trim (std::string s) {
  auto is_space= [] (unsigned char c) { return std::isspace (c) != 0; };
  s.erase (s.begin (), std::find_if (s.begin (), s.end (),
                                     [&] (unsigned char c) {
                                       return !is_space (c);
                                     }));
  s.erase (std::find_if (s.rbegin (), s.rend (),
                         [&] (unsigned char c) {
                           return !is_space (c);
                         }).base (), s.end ());
  return s;
}

bool
ends_with (const std::string& s, const std::string& suffix) {
  return s.size () >= suffix.size () &&
         s.compare (s.size () - suffix.size (), suffix.size (), suffix) == 0;
}

bool
looks_like_full_latex_document (const std::string& latex) {
  return latex.find ("\\documentclass") != std::string::npos ||
         latex.find ("\\begin{document}") != std::string::npos ||
         latex.find ("\\end{document}") != std::string::npos;
}

bool
contains (const std::string& s, const std::string& needle) {
  return s.find (needle) != std::string::npos;
}

bool
accept_cleaner_output (const std::string& input, const std::string& output) {
  if (output.empty ()) return false;
  if (contains (output, "<|im_") || contains (output, "[INST]") ||
      contains (output, "[/INST]") || contains (output, "<s>"))
    return false;

  size_t max_len= std::max<size_t> (160, (3 * input.size ()) + 32);
  if (output.size () > max_len) return false;

  if (!contains (input, "\\begin{") && contains (output, "\\begin{"))
    return false;
  if (!contains (input, "\\text") && contains (output, "\\text{"))
    return false;
  return true;
}

std::string
default_model_path () {
  return tm_to_std_string (
    concretize (url_system ("$ATHENA_PATH/tools/formula-cleaner/formula-cleaner.gguf")));
}

std::string
configured_model_path () {
  string p= get_preference ("latex->texmacs:intelligent-formula-cleaner-model",
                            "");
  std::string path= trim (tm_to_std_string (p));
  if (path.empty ()) return default_model_path ();
  if (path.find ('$') != std::string::npos || path.find ('~') == 0)
    return tm_to_std_string (concretize (url_system (std_to_tm_string (path))));
  return path;
}

std::vector<llama_token>
tokenize (const llama_vocab* vocab, const std::string& text, bool add_special) {
  int32_t n= llama_tokenize (vocab, text.c_str (), (int32_t) text.size (),
                             nullptr, 0, add_special, true);
  if (n == 0) return {};
  if (n < 0) n= -n;
  std::vector<llama_token> tokens ((size_t) n);
  int32_t actual= llama_tokenize (vocab, text.c_str (),
                                  (int32_t) text.size (), tokens.data (), n,
                                  add_special, true);
  if (actual < 0) actual= -actual;
  if (actual < 0) return {};
  tokens.resize ((size_t) actual);
  return tokens;
}

std::string
piece (const llama_vocab* vocab, llama_token token) {
  char small[64];
  int32_t n= llama_token_to_piece (vocab, token, small, (int32_t) sizeof (small),
                                   0, false);
  if (n >= 0) return std::string (small, (size_t) n);
  std::vector<char> buf ((size_t) (-n));
  n= llama_token_to_piece (vocab, token, buf.data (), (int32_t) buf.size (),
                           0, false);
  if (n <= 0) return "";
  return std::string (buf.data (), (size_t) n);
}

class FormulaCleaner {
public:
  ~FormulaCleaner () {
    if (sampler != nullptr) llama_sampler_free (sampler);
    if (ctx != nullptr) llama_free (ctx);
    if (model != nullptr) llama_model_free (model);
  }

  std::string clean (const std::string& latex) {
    std::lock_guard<std::mutex> lock (mutex);
    if (!ensure_loaded ()) return latex;

    const llama_vocab* vocab= llama_model_get_vocab (model);
    // llama.cpp adds the model's BOS token when add_special is true.
    std::string prompt= "[INST] " + latex + " [/INST] ";
    std::vector<llama_token> tokens= tokenize (vocab, prompt, true);
    if (tokens.empty ()) return latex;
    if ((int) tokens.size () + 32 >= cleaner_context_tokens) {
      std_warning << "formula cleaner: input is too large for the configured "
                  << "llama.cpp context; using original LaTeX" << LF;
      return latex;
    }

    llama_memory_clear (llama_get_memory (ctx), true);
    llama_sampler_reset (sampler);

    llama_batch batch= llama_batch_get_one (tokens.data (),
                                            (int32_t) tokens.size ());
    if (llama_decode (ctx, batch) != 0) {
      std_warning << "formula cleaner: llama.cpp prompt decode failed" << LF;
      return latex;
    }

    std::string out;
    int max_tokens= std::min<int> (cleaner_max_new_tokens,
                                   std::max<int> (32, 2 * (int) tokens.size ()));
    for (int i=0; i<max_tokens; i++) {
      llama_token token= llama_sampler_sample (sampler, ctx, -1);
      if (llama_vocab_is_eog (vocab, token)) break;
      out += piece (vocab, token);

      llama_batch next= llama_batch_get_one (&token, 1);
      if (llama_decode (ctx, next) != 0) {
        std_warning << "formula cleaner: llama.cpp generation decode failed"
                    << LF;
        break;
      }
      if ((int) tokens.size () + i + 2 >= cleaner_context_tokens) break;
    }

    out= trim (out);
    size_t eos= out.find ("</s>");
    if (eos != std::string::npos) out= trim (out.substr (0, eos));
    if (!accept_cleaner_output (latex, out)) {
      std_warning << "formula cleaner: rejected implausible model output; "
                  << "using original LaTeX" << LF;
      return latex;
    }
    return out;
  }

private:
  bool ensure_loaded () {
    std::string path= configured_model_path ();
    if (path == loaded_path && model != nullptr && ctx != nullptr &&
        sampler != nullptr)
      return true;
    unload ();

    if (path.empty ()) return false;
    if (ends_with (path, ".safetensors") || ends_with (path, ".bin")) {
      warn_once ("formula cleaner: configured model is not a GGUF file; "
                 "llama.cpp needs a merged/converted .gguf model");
      return false;
    }
    if (!exists (url_system (std_to_tm_string (path)))) {
      warn_once ("formula cleaner: GGUF model not found at " + path +
                 "; using deterministic LaTeX importer");
      return false;
    }

    athena_llama_runtime_initialize ();

    llama_model_params mparams= llama_model_default_params ();
    mparams.n_gpu_layers= 0;
    model= llama_model_load_from_file (path.c_str (), mparams);
    if (model == nullptr) {
      std_warning << "formula cleaner: failed to load GGUF model at "
                  << path.c_str () << LF;
      return false;
    }

    llama_context_params cparams= llama_context_default_params ();
    cparams.n_ctx= cleaner_context_tokens;
    cparams.n_batch= cleaner_context_tokens;
    cparams.n_threads= std::max (1, (int) std::thread::hardware_concurrency ());
    cparams.n_threads_batch= cparams.n_threads;
    ctx= llama_init_from_model (model, cparams);
    if (ctx == nullptr) {
      std_warning << "formula cleaner: failed to initialize llama.cpp context"
                  << LF;
      unload ();
      return false;
    }

    sampler= llama_sampler_chain_init (llama_sampler_chain_default_params ());
    llama_sampler_chain_add (sampler, llama_sampler_init_greedy ());
    loaded_path= path;
    std_warning << "formula cleaner: loaded llama.cpp model " << path.c_str ()
                << LF;
    return true;
  }

  void unload () {
    if (sampler != nullptr) {
      llama_sampler_free (sampler);
      sampler= nullptr;
    }
    if (ctx != nullptr) {
      llama_free (ctx);
      ctx= nullptr;
    }
    if (model != nullptr) {
      llama_model_free (model);
      model= nullptr;
    }
    loaded_path.clear ();
  }

  void warn_once (const std::string& message) {
    if (message == last_warning) return;
    last_warning= message;
    std_warning << message.c_str () << LF;
  }

private:
  std::mutex mutex;
  std::string loaded_path;
  std::string last_warning;
  llama_model* model= nullptr;
  llama_context* ctx= nullptr;
  llama_sampler* sampler= nullptr;
};

FormulaCleaner&
cleaner () {
  static FormulaCleaner instance;
  return instance;
}

} // namespace

string
clean_latex_formula_with_llama (string latex) {
  if (get_preference ("latex->texmacs:intelligent-formula-cleaner", "off") !=
      "on")
    return latex;

  std::string input= tm_to_std_string (latex);
  if (looks_like_full_latex_document (input)) return latex;

  std::string output= cleaner ().clean (input);
  return std_to_tm_string (output);
}
