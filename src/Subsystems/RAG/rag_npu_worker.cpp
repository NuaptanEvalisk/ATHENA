/******************************************************************************
* MODULE     : rag_npu_worker.cpp
* DESCRIPTION: Isolated OpenVINO/NPU embedding worker for Continuous RAG
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <openvino/openvino.hpp>
#include <openvino/runtime/properties.hpp>

#include <llama.h>
#include <nlohmann/json.hpp>

#include "rag_embedding_contract.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef __linux__
#include <sys/prctl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr const char* protocol_name= "athena-rag-npu-v1";

std::mutex output_mutex;

void
write_message (json value) {
  std::lock_guard<std::mutex> guard (output_mutex);
  std::cout << value.dump () << '\n' << std::flush;
}

void
tokenizer_log (ggml_log_level level, const char* text, void*) noexcept {
  if (text == nullptr || level == GGML_LOG_LEVEL_NONE) return;
  try {
    // CONT is a continuation, not a severity above ERROR.
    thread_local ggml_log_level previous= GGML_LOG_LEVEL_INFO;
    if (level == GGML_LOG_LEVEL_CONT) level= previous;
    else previous= level;
    const char* severity= level == GGML_LOG_LEVEL_ERROR ? "error" :
                          level == GGML_LOG_LEVEL_WARN ? "warning" : "debug";
    write_message ({{"type", "log"}, {"source", "tokenizer"},
                    {"level", severity}, {"text", text}});
  }
  catch (...) {} // Never unwind through the C library's log callback.
}

[[noreturn]] void
fast_exit (int code) noexcept {
#ifdef __linux__
  _exit (code);
#else
  std::_Exit (code);
#endif
}

void
termination_signal (int) {
  fast_exit (0);
}

struct Options {
  fs::path model;
  fs::path tokenizer;
  fs::path cache_dir;
  int max_tokens= athena::rag::bge_m3_max_tokens;
};

Options
parse_options (int argc, char** argv) {
  Options options;
  for (int i=1; i<argc; ++i) {
    std::string arg= argv[i];
    auto value= [&] () -> std::string {
      if (i + 1 >= argc)
        throw std::runtime_error ("missing value for " + arg);
      return argv[++i];
    };
    if (arg == "--model") options.model= fs::path (value ());
    else if (arg == "--tokenizer-gguf") options.tokenizer= fs::path (value ());
    else if (arg == "--cache-dir") options.cache_dir= fs::path (value ());
    else if (arg == "--max-tokens") options.max_tokens= std::stoi (value ());
    else throw std::runtime_error ("unknown argument: " + arg);
  }
  if (options.model.empty ()) throw std::runtime_error ("--model is required");
  if (options.tokenizer.empty ())
    throw std::runtime_error ("--tokenizer-gguf is required");
  if (options.max_tokens != athena::rag::bge_m3_max_tokens)
    throw std::runtime_error (
      "BGE-M3 shared embedding space requires --max-tokens 8192");
  return options;
}

std::string
port_name (const ov::Output<const ov::Node>& port) {
  const auto names= port.get_names ();
  if (!names.empty ()) return *names.begin ();
  try { return port.get_any_name (); }
  catch (...) { return {}; }
}

bool
name_contains (const std::string& name, const std::string& needle) {
  return name.find (needle) != std::string::npos;
}

std::string
llama_metadata (const llama_model* model, const char* key) {
  std::vector<char> buffer (512, '\0');
  int32_t written= llama_model_meta_val_str (
    model, key, buffer.data (), buffer.size ());
  if (written >= static_cast<int32_t> (buffer.size ())) {
    buffer.assign (static_cast<std::size_t> (written) + 1, '\0');
    written= llama_model_meta_val_str (
      model, key, buffer.data (), buffer.size ());
  }
  if (written <= 0) return {};
  return std::string (buffer.data (), static_cast<std::size_t> (written));
}

std::string
lower_ascii (std::string value) {
  for (char& c: value)
    if (c >= 'A' && c <= 'Z') c= static_cast<char> (c - 'A' + 'a');
  return value;
}

class BgeM3Tokenizer {
public:
  explicit BgeM3Tokenizer (const fs::path& path) {
    llama_backend_init ();
    llama_model_params params= llama_model_default_params ();
    params.vocab_only= true;
    params.n_gpu_layers= 0;
    // A vocabulary-only load must not enumerate/initialize SYCL GPU devices.
    ggml_backend_dev_t devices[]= {nullptr};
    params.devices= devices;
    model_= llama_model_load_from_file (path.string ().c_str (), params);
    if (model_ == nullptr)
      throw std::runtime_error ("failed to load GGUF tokenizer vocabulary");
    vocab_= llama_model_get_vocab (model_);
    if (vocab_ == nullptr)
      throw std::runtime_error ("GGUF model has no tokenizer vocabulary");
    if (llama_vocab_n_tokens (vocab_) != athena::rag::bge_m3_vocab_size ||
        llama_vocab_bos (vocab_) != athena::rag::bge_m3_bos_token ||
        llama_vocab_pad (vocab_) != athena::rag::bge_m3_pad_token ||
        llama_vocab_eos (vocab_) != athena::rag::bge_m3_eos_token)
      throw std::runtime_error (
        "tokenizer is not the expected BAAI/bge-m3 vocabulary contract");
    if (lower_ascii (llama_metadata (model_, "general.architecture")) != "bert" ||
        llama_metadata (model_, "bert.embedding_length") != "1024" ||
        llama_metadata (model_, "bert.pooling_type") != "2" ||
        lower_ascii (llama_metadata (model_, "tokenizer.ggml.model")) != "t5")
      throw std::runtime_error (
        "tokenizer GGUF is not the expected BAAI/bge-m3 model contract");
  }

  ~BgeM3Tokenizer () {
    if (model_ != nullptr) llama_model_free (model_);
    llama_backend_free ();
  }

  std::vector<std::int64_t> tokenize (std::string text, int max_tokens) const {
    if (text.size () > athena::rag::bge_m3_input_byte_limit)
      text.resize (athena::rag::bge_m3_input_byte_limit);
    int32_t required= llama_tokenize (
      vocab_, text.data (), static_cast<int32_t> (text.size ()), nullptr, 0,
      true, true);
    if (required == 0) return {};
    if (required > 0)
      throw std::runtime_error ("unexpected llama tokenizer sizing result");
    std::vector<llama_token> raw (static_cast<std::size_t> (-required));
    int32_t count= llama_tokenize (
      vocab_, text.data (), static_cast<int32_t> (text.size ()), raw.data (),
      static_cast<int32_t> (raw.size ()), true, true);
    if (count < 0)
      throw std::runtime_error ("failed to tokenize embedding text");
    raw.resize (static_cast<std::size_t> (count));
    if (static_cast<int> (raw.size ()) > max_tokens)
      raw.resize (static_cast<std::size_t> (max_tokens));
    std::vector<std::int64_t> out;
    out.reserve (raw.size ());
    for (llama_token token: raw) out.push_back (static_cast<std::int64_t> (token));
    return out;
  }

private:
  llama_model* model_= nullptr;
  const llama_vocab* vocab_= nullptr;
};

class BgeM3NpuEngine {
public:
  explicit BgeM3NpuEngine (Options options)
    : options_ (std::move (options)), tokenizer_ (options_.tokenizer) {
    if (!fs::is_regular_file (options_.model))
      throw std::runtime_error ("OpenVINO model file does not exist");
    validate_model_config (options_.model);
    if (!options_.cache_dir.empty ()) {
      std::error_code ec;
      fs::create_directories (options_.cache_dir, ec);
      if (ec) throw std::runtime_error (
        "could not create OpenVINO cache directory: " + ec.message ());
      core_.set_property ("NPU", ov::cache_dir (options_.cache_dir.string ()));
    }
    const std::vector<std::string> devices= core_.get_available_devices ();
    if (std::find (devices.begin (), devices.end (), "NPU") == devices.end ())
      throw std::runtime_error ("OpenVINO reports no NPU device");
    base_model_= core_.read_model (options_.model.string ());
    validate_model (*base_model_);
  }

  const std::string& space_id () const noexcept { return space_id_; }
  int dimension () const noexcept { return athena::rag::bge_m3_embedding_dimension; }
  int max_tokens () const noexcept { return options_.max_tokens; }

  std::vector<std::int64_t> tokenize (const std::string& text) const {
    return tokenizer_.tokenize (text, options_.max_tokens);
  }

  std::vector<float> embed (
    const std::vector<std::int64_t>& tokens, const std::function<bool ()>& current,
    const std::function<void (ov::InferRequest*)>& publish_request) {
    if (tokens.empty ()) return {};
    const int bucket= bucket_for (static_cast<int> (tokens.size ()), options_.max_tokens);
    try {
      ov::CompiledModel& compiled= compiled_for (bucket);
      if (!current ()) return {};

      ov::InferRequest request= compiled.create_infer_request ();
      fill_inputs (request, compiled, tokens, bucket);
      publish_request (&request);
      try {
        request.start_async ();
        request.wait ();
      }
      catch (...) {
        publish_request (nullptr);
        if (!current ()) return {};
        throw;
      }
      publish_request (nullptr);
      if (!current ()) return {};
      return dense_output (request.get_output_tensor (dense_output_index (compiled)));
    }
    catch (const std::exception& error) {
      throw std::runtime_error (
        "BGE-M3 tokens=" + std::to_string (tokens.size ()) +
        " bucket=" + std::to_string (bucket) + ": " + error.what ());
    }
  }

private:
  static void validate_model_config (const fs::path& model_path) {
    const fs::path config_path= model_path.parent_path () / "config.json";
    std::ifstream input (config_path);
    if (!input)
      throw std::runtime_error (
        "BGE-M3 OpenVINO model directory is missing config.json");
    json config;
    input >> config;
    const std::string model_type= lower_ascii (
      config.value ("model_type", std::string ()));
    if ((model_type != "xlm-roberta" && model_type != "xlm_roberta") ||
        config.value ("hidden_size", 0) !=
          athena::rag::bge_m3_embedding_dimension ||
        config.value ("num_hidden_layers", 0) != 24 ||
        config.value ("num_attention_heads", 0) != 16 ||
        config.value ("intermediate_size", 0) != 4096 ||
        config.value ("vocab_size", 0) != athena::rag::bge_m3_vocab_size ||
        config.value ("bos_token_id", -1) != athena::rag::bge_m3_bos_token ||
        config.value ("pad_token_id", -1) != athena::rag::bge_m3_pad_token ||
        config.value ("eos_token_id", -1) != athena::rag::bge_m3_eos_token ||
        config.value ("type_vocab_size", 0) != 1)
      throw std::runtime_error (
        "OpenVINO model config does not match the BAAI/bge-m3 encoder contract");
  }

  static int bucket_for (int tokens, int maximum) {
    static const int buckets[]= {64, 128, 256, 512, 1024, 2048, 4096, 8192};
    for (int bucket: buckets)
      if (tokens <= bucket) return std::min (bucket, maximum);
    return maximum;
  }

  void validate_model (const ov::Model& model) {
    bool input_ids= false;
    bool attention= false;
    for (const auto& input: model.inputs ()) {
      const std::string name= port_name (input);
      input_ids= input_ids || name_contains (name, "input_ids");
      attention= attention || name_contains (name, "attention_mask");
      if (input.get_partial_shape ().rank ().is_static () &&
          input.get_partial_shape ().rank ().get_length () != 2)
        throw std::runtime_error ("BGE-M3 encoder input is not rank 2: " + name);
    }
    if (!input_ids || !attention)
      throw std::runtime_error (
        "BGE-M3 OpenVINO model must expose input_ids and attention_mask");
    if (model.outputs ().empty ())
      throw std::runtime_error ("BGE-M3 OpenVINO model has no outputs");
  }

  ov::CompiledModel& compiled_for (int sequence) {
    auto found= compiled_.find (sequence);
    if (found != compiled_.end ()) return found->second;
    // Each BGE-M3 graph retains over a GB of device allocations. Keeping all
    // buckets resident exhausts Meteor Lake's 2 GiB SHAVE address heap, even
    // with plenty of host RAM. Disk caching still avoids recompiling a bucket.
    compiled_.clear ();
    std::shared_ptr<ov::Model> model= base_model_->clone ();
    std::map<std::string,ov::PartialShape> shapes;
    for (const auto& input: model->inputs ()) {
      const std::string name= port_name (input);
      if (name.empty ())
        throw std::runtime_error ("OpenVINO model input has no stable name");
      shapes[name]= ov::PartialShape {1, sequence};
    }
    model->reshape (shapes);
    ov::AnyMap properties;
    properties[ov::hint::performance_mode.name ()]=
      ov::hint::PerformanceMode::LATENCY;
    ov::CompiledModel value= core_.compile_model (model, "NPU", properties);
    (void) dense_output_index (value);
    return compiled_.emplace (sequence, std::move (value)).first->second;
  }

  static std::size_t dense_output_index (const ov::CompiledModel& model) {
    std::optional<std::size_t> token_output;
    const auto outputs= model.outputs ();
    for (std::size_t i=0; i<outputs.size (); ++i) {
      const ov::PartialShape shape= outputs[i].get_partial_shape ();
      if (!shape.rank ().is_static () || shape.rank ().get_length () < 2 ||
          !shape[shape.rank ().get_length () - 1].is_static () ||
          shape[shape.rank ().get_length () - 1].get_length () !=
            athena::rag::bge_m3_embedding_dimension)
        continue;
      // Prefer an already pooled [batch,1024] output.  If the exported encoder
      // exposes only [batch,sequence,1024], dense_output() deliberately takes
      // the first token, which is BGE-M3's CLS/BOS embedding.
      if (shape.rank ().get_length () == 2) return i;
      if (!token_output) token_output= i;
    }
    if (token_output) return *token_output;
    throw std::runtime_error (
      "BGE-M3 encoder has no output with 1024 dense dimensions");
  }

  static void assign_ids (ov::Tensor& tensor,
                          const std::vector<std::int64_t>& values,
                          std::int64_t fill) {
    const std::size_t n= tensor.get_size ();
    if (tensor.get_element_type () == ov::element::i64) {
      auto* data= tensor.data<std::int64_t> ();
      std::fill (data, data + n, fill);
      std::copy_n (values.begin (), std::min (values.size (), n), data);
      return;
    }
    if (tensor.get_element_type () == ov::element::i32) {
      auto* data= tensor.data<std::int32_t> ();
      std::fill (data, data + n, static_cast<std::int32_t> (fill));
      for (std::size_t i=0; i<std::min (values.size (), n); ++i)
        data[i]= static_cast<std::int32_t> (values[i]);
      return;
    }
    throw std::runtime_error ("unsupported BGE-M3 integer input type");
  }

  static void fill_inputs (
    ov::InferRequest& request, const ov::CompiledModel& compiled,
    const std::vector<std::int64_t>& tokens, int sequence) {
    for (const auto& input: compiled.inputs ()) {
      const std::string name= port_name (input);
      ov::Tensor tensor (input.get_element_type (), ov::Shape {1,
                         static_cast<std::size_t> (sequence)});
      if (name_contains (name, "input_ids"))
      assign_ids (tensor, tokens, athena::rag::bge_m3_pad_token);
      else if (name_contains (name, "attention_mask")) {
        std::vector<std::int64_t> mask (tokens.size (), 1);
        assign_ids (tensor, mask, 0);
      }
      else if (name_contains (name, "token_type_ids"))
        assign_ids (tensor, {}, 0);
      else
        throw std::runtime_error ("unsupported BGE-M3 encoder input: " + name);
      request.set_tensor (name, tensor);
    }
  }

  static std::vector<float> dense_output (const ov::Tensor& tensor) {
    const ov::Shape shape= tensor.get_shape ();
    if (shape.size () < 2 || shape.back () != athena::rag::bge_m3_embedding_dimension)
      throw std::runtime_error ("unexpected BGE-M3 encoder output shape");
    std::vector<float> out (athena::rag::bge_m3_embedding_dimension);
    if (tensor.get_element_type () == ov::element::f32) {
      const float* data= tensor.data<const float> ();
      std::copy_n (data, athena::rag::bge_m3_embedding_dimension, out.begin ());
    }
    else if (tensor.get_element_type () == ov::element::f16) {
      const ov::float16* data= tensor.data<const ov::float16> ();
      for (int i=0; i<athena::rag::bge_m3_embedding_dimension; ++i)
        out[i]= static_cast<float> (data[i]);
    }
    else if (tensor.get_element_type () == ov::element::bf16) {
      const ov::bfloat16* data= tensor.data<const ov::bfloat16> ();
      for (int i=0; i<athena::rag::bge_m3_embedding_dimension; ++i)
        out[i]= static_cast<float> (data[i]);
    }
    else throw std::runtime_error ("unsupported BGE-M3 encoder output type");

    double sum= 0.0;
    for (float value: out) sum += double (value) * double (value);
    if (!std::isfinite (sum) || sum <= 0.0)
      throw std::runtime_error ("BGE-M3 produced a non-finite or zero vector");
    const float scale= static_cast<float> (1.0 / std::sqrt (sum));
    for (float& value: out) value *= scale;
    return out;
  }

  Options options_;
  BgeM3Tokenizer tokenizer_;
  ov::Core core_;
  std::shared_ptr<ov::Model> base_model_;
  std::map<int,ov::CompiledModel> compiled_;
  const std::string space_id_= athena::rag::bge_m3_embedding_space_id;
};

struct Task {
  std::string key;
  std::uint64_t generation= 0;
  std::vector<std::string> texts;
};

class Worker {
public:
  explicit Worker (Options options): engine_ (std::move (options)) {
    thread_= std::thread ([this] { run (); });
  }

  ~Worker () {
    {
      std::lock_guard<std::mutex> guard (mutex_);
      stopping_= true;
    }
    condition_.notify_all ();
    cancel_active ();
    if (thread_.joinable ()) thread_.join ();
  }

  json ready_message () const {
    return {{"type", "ready"}, {"protocol", protocol_name},
            {"space_id", engine_.space_id ()},
            {"dimension", engine_.dimension ()},
            {"max_tokens", engine_.max_tokens ()}, {"device", "NPU"}};
  }

  void submit (Task task) {
    std::optional<ov::InferRequest> cancel;
    {
      std::lock_guard<std::mutex> guard (mutex_);
      auto& latest= latest_[task.key];
      if (task.generation <= latest.generation) return;
      latest= {task.generation, false};
      queue_.erase (
        std::remove_if (queue_.begin (), queue_.end (),
          [&] (const Task& queued) { return queued.key == task.key; }),
        queue_.end ());
      if (active_request_ && active_key_ == task.key &&
          active_generation_ < task.generation)
        cancel= *active_request_;
      queue_.push_back (std::move (task));
    }
    if (cancel) {
      try { cancel->cancel (); }
      catch (...) {}
    }
    condition_.notify_one ();
  }

  void cancel (const std::string& key, std::uint64_t generation) {
    std::optional<ov::InferRequest> request;
    {
      std::lock_guard<std::mutex> guard (mutex_);
      auto& latest= latest_[key];
      if (latest.generation <= generation) latest= {generation, true};
      queue_.erase (
        std::remove_if (queue_.begin (), queue_.end (),
          [&] (const Task& task) {
            return task.key == key && task.generation <= generation;
          }), queue_.end ());
      if (active_request_ && active_key_ == key &&
          active_generation_ <= generation)
        request= *active_request_;
    }
    if (request) {
      try { request->cancel (); }
      catch (...) {}
    }
  }

  void cancel_active () {
    std::optional<ov::InferRequest> request;
    {
      std::lock_guard<std::mutex> guard (mutex_);
      if (active_request_) request= *active_request_;
    }
    if (request) {
      try { request->cancel (); }
      catch (...) {}
    }
  }

private:
  bool current (const Task& task) const {
    std::lock_guard<std::mutex> guard (mutex_);
    auto found= latest_.find (task.key);
    return !stopping_ && found != latest_.end () &&
           found->second.generation == task.generation &&
           !found->second.cancelled;
  }

  void publish_request (const Task& task, ov::InferRequest* request) {
    std::lock_guard<std::mutex> guard (mutex_);
    if (request == nullptr) {
      active_request_.reset ();
      active_key_.clear ();
      active_generation_= 0;
      return;
    }
    active_request_= *request;
    active_key_= task.key;
    active_generation_= task.generation;
  }

  void run () {
    while (true) {
      Task task;
      {
        std::unique_lock<std::mutex> lock (mutex_);
        condition_.wait (lock, [&] { return stopping_ || !queue_.empty (); });
        if (stopping_) return;
        task= std::move (queue_.front ());
        queue_.pop_front ();
      }
      if (!current (task)) {
        write_message ({{"type", "dropped"}, {"key", task.key},
                        {"generation", task.generation}});
        continue;
      }

      try {
        struct Input {
          std::size_t index;
          std::vector<std::int64_t> tokens;
        };
        std::vector<Input> inputs;
        inputs.reserve (task.texts.size ());
        for (std::size_t i=0; i<task.texts.size () && current (task); ++i)
          inputs.push_back ({i, engine_.tokenize (task.texts[i])});
        // Keep one bucket resident while processing all its inputs. Return
        // vectors in original chunk order, independent of execution order.
        std::stable_sort (inputs.begin (), inputs.end (),
          [] (const Input& a, const Input& b) { return a.tokens.size () < b.tokens.size (); });
        std::vector<std::vector<float>> vectors (task.texts.size ());
        std::size_t completed= 0;
        for (const Input& input: inputs) {
          if (!current (task)) break;
          std::vector<float> vector= engine_.embed (
            input.tokens, [&] { return current (task); },
            [&] (ov::InferRequest* request) { publish_request (task, request); });
          if (!current (task)) break;
          if (vector.empty ())
            throw std::runtime_error ("NPU embedding returned no vector");
          vectors[input.index]= std::move (vector);
          ++completed;
        }
        if (!current (task) || completed != task.texts.size ()) {
          write_message ({{"type", "dropped"}, {"key", task.key},
                          {"generation", task.generation}});
          continue;
        }
        write_message ({{"type", "result"}, {"key", task.key},
                        {"generation", task.generation},
                        {"space_id", engine_.space_id ()},
                        {"dimension", engine_.dimension ()},
                        {"vectors", vectors}});
      }
      catch (const std::exception& error) {
        publish_request (task, nullptr);
        if (!current (task))
          write_message ({{"type", "dropped"}, {"key", task.key},
                          {"generation", task.generation}});
        else {
          write_message ({{"type", "fatal"}, {"key", task.key},
                          {"generation", task.generation},
                          {"error", error.what ()}});
          // A failed graph initialization can leave the driver graph partly
          // initialized. Recovery must not reuse that process or its graphs.
          fast_exit (1);
        }
      }
    }
  }

  BgeM3NpuEngine engine_;
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<Task> queue_;
  struct Generation {
    std::uint64_t generation= 0;
    bool cancelled= false;
  };
  std::unordered_map<std::string,Generation> latest_;
  std::optional<ov::InferRequest> active_request_;
  std::string active_key_;
  std::uint64_t active_generation_= 0;
  bool stopping_= false;
  std::thread thread_;
};

Task
parse_task (const json& message) {
  Task task;
  task.key= message.at ("key").get<std::string> ();
  const json& generation= message.at ("generation");
  task.generation= generation.is_string ()
    ? static_cast<std::uint64_t> (std::stoull (generation.get<std::string> ()))
    : generation.get<std::uint64_t> ();
  task.texts= message.at ("texts").get<std::vector<std::string>> ();
  if (task.key.empty () || task.generation == 0)
    throw std::runtime_error ("invalid embedding task identity");
  if (task.texts.size () > 4096)
    throw std::runtime_error ("embedding task contains too many texts");
  return task;
}

} // namespace

int
main (int argc, char** argv) {
#ifdef __linux__
  // The editor owns this worker.  If it disappears without a shutdown message,
  // terminate the compute process instead of continuing as an orphan.
  (void) prctl (PR_SET_PDEATHSIG, SIGTERM);
  if (getppid () == 1) return 0;
#endif
  signal (SIGTERM, termination_signal);
  signal (SIGINT, termination_signal);

  try {
    llama_log_set (tokenizer_log, nullptr);
    Options options= parse_options (argc, argv);
    Worker worker (std::move (options));
    write_message (worker.ready_message ());
    std::string line;
    while (std::getline (std::cin, line)) {
      if (line.empty ()) continue;
      try {
        json message= json::parse (line);
        const std::string op= message.value ("op", "");
        if (op == "embed") worker.submit (parse_task (message));
        else if (op == "cancel") {
          const json& generation= message.at ("generation");
          std::uint64_t value= generation.is_string ()
            ? static_cast<std::uint64_t> (
                std::stoull (generation.get<std::string> ()))
            : generation.get<std::uint64_t> ();
          worker.cancel (message.at ("key").get<std::string> (), value);
        }
        else if (op == "shutdown") {
          worker.cancel_active ();
          fast_exit (0);
        }
        else if (op == "ping")
          write_message ({{"type", "pong"}, {"protocol", protocol_name}});
        else
          write_message ({{"type", "protocol-error"},
                          {"error", "unknown worker operation"}});
      }
      catch (const std::exception& error) {
        write_message ({{"type", "protocol-error"}, {"error", error.what ()}});
      }
    }
    worker.cancel_active ();
    return 0;
  }
  catch (const std::exception& error) {
    write_message ({{"type", "fatal"}, {"error", error.what ()}});
    return 1;
  }
}
