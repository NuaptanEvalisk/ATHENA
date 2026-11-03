// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace athena::prooftrack {

// Both the persistent reverse index and screen markers must recognize the
// same enunciation types. "law" denotes a mathematical/scientific law.
inline bool endpoint_kind (std::string_view kind) {
  return kind == "proof" || kind == "theorem" ||
         kind == "proposition" || kind == "lemma" ||
         kind == "corollary" || kind == "question" ||
         kind == "example" || kind == "remark" || kind == "law" ||
         kind == "warning" || kind == "note" || kind == "quote-env";
}

} // namespace athena::prooftrack
