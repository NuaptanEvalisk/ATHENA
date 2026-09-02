#ifndef QTM_CODEX_COMPLETION_HPP
#define QTM_CODEX_COMPLETION_HPP

#include "array.hpp"
#include "string.hpp"

void qtm_codex_initialize_models (string bridge, string home);
array<string> qtm_codex_completion_options (const string& bridge,
                                            const string& home);

#endif // QTM_CODEX_COMPLETION_HPP
