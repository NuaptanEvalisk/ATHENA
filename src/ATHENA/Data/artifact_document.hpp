/******************************************************************************
* MODULE     : artifact_document.hpp
* DESCRIPTION: TMFS documents for navigating semantic artifacts
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#ifndef ATHENA_ARTIFACT_DOCUMENT_HPP
#define ATHENA_ARTIFACT_DOCUMENT_HPP

#include "ATHENA/Data/artifacts.hpp"

tree athena_artifact_disambiguation_document (
  const std::vector<AthenaArtifactRecord>& records, string preferred_font);

tree athena_artifact_disambiguation_page (string disambiguation_key);

#endif // ATHENA_ARTIFACT_DOCUMENT_HPP
