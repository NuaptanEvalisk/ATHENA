/******************************************************************************
* MODULE     : interop_filesystem.hpp
* DESCRIPTION: Vault filesystem accessors shared with native document resolution
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "interop_vault_resource.hpp"
#include "../../System/Files/confined_filesystem.hpp"

namespace athena::interop {
class filesystem_resource final: public vault_resource {
  const vault_context_handle context;
  const std::shared_ptr<const athena::filesystem::confined_root> root;
  const std::filesystem::path relative;
  const athena::filesystem::entry pinned;
public:
  filesystem_resource (vault_context_handle,
    std::shared_ptr<const athena::filesystem::confined_root>, std::filesystem::path,
    athena::filesystem::entry);
  vault_context_handle captured_vault () const override { return context; }
  const std::shared_ptr<const athena::filesystem::confined_root>& filesystem_root () const { return root; }
  const std::filesystem::path& relative_path () const { return relative; }
  athena::filesystem::entry current () const;
  std::string type () const override;
  std::string identity () const override;
  value properties () const override;
  value inspect () const override;
  operation_result operate (const std::string&, const value&) const override;
};
std::shared_ptr<const resolver> filesystem_resolver ();
} // namespace athena::interop
