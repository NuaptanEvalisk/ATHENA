#ifndef QTM_CODEX_COMPLETION_HPP
#define QTM_CODEX_COMPLETION_HPP

#include "array.hpp"
#include "command.hpp"
#include "string.hpp"

void qtm_codex_initialize_models (string bridge, string home);
array<string> qtm_codex_completion_options (const string& bridge,
                                             const string& home);
void qtm_codex_run_completion_async (
  string bridge, string home, string input, string output, string model,
  string effort, string service_tier, string web_search,
  array<string> image_paths, command callback);

#endif // QTM_CODEX_COMPLETION_HPP
