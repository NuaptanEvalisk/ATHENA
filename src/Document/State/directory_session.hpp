// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include "Document/State/auxiliary_contributions.hpp"
#include <filesystem>
#include <functional>
#include <map>
#include <memory>

namespace athena::derived {
// Owner-side results are native trees transferred from the worker, not decoded
// cache bytes or handles into a mutable worker tree.
class directory_session {
public:
  directory_session (std::filesystem::path source, std::function<void()> ready);
  ~directory_session ();
  void submit (contribution_log, hashmap<string,tree> references,
               bool page_references_changed);
  bool receive ();
  const std::map<std::string,tree>& views () const;
  bool wait ();
private:
  struct impl;
  std::unique_ptr<impl> data;
};
}
