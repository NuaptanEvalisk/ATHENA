/******************************************************************************
* MODULE     : interop_document_source.hpp
* DESCRIPTION: Complete owner-local document source projections for AUDM
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "new_data.hpp"
#include <string>

std::string interop_document_source_error (const tree& source);

// Refresh known fields while retaining unknown source attributes and unchanged
// native node identities. The body is an owner-local alias, not a snapshot.
void refresh_interop_document_source (tree& source, const tree& body, new_data data);
// Preserve attributes outside new_data while keeping the existing save policy
// for standard fields, including auxiliary data and transient viewport values.
void append_interop_source_attributes (tree& snapshot, const tree& source);
