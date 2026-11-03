// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#ifndef ATHENA_TMFS_HPP
#define ATHENA_TMFS_HPP
#include "tree.hpp"
#include "url.hpp"

tree tmfs_load (url source);
string tmfs_title (url source);
bool tmfs_permission (url source, string permission);
url tmfs_master (url source);
string tmfs_encode_url (url source);
url tmfs_decode_url (string source);
#endif
