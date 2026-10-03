/******************************************************************************
* MODULE     : namespaces_sorter.cpp
* DESCRIPTION: Sandboxed Luau and structural namespace sorters
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "namespaces_private.hpp"

#include "vault.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUuid>

#include <lua.h>
#include <lualib.h>
#include <luacode.h>

#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <queue>
#include <set>
#include <thread>
#include <unordered_map>

namespace athena_namespaces {

namespace fs= std::filesystem;
using clock_type= std::chrono::steady_clock;

constexpr size_t luau_hard_memory_limit= 32u * 1024u * 1024u;
constexpr size_t luau_operation_memory_budget= 4u * 1024u * 1024u;
constexpr size_t luau_source_size_limit= 1u * 1024u * 1024u;
constexpr size_t sorter_host_memory_budget= 64u * 1024u * 1024u;
constexpr auto luau_module_time_budget= std::chrono::milliseconds (250);
constexpr auto luau_call_time_budget= std::chrono::milliseconds (100);
constexpr auto luau_sort_time_budget= std::chrono::seconds (5);
constexpr const char* composition_format= "athena-namespace-sorter-composition";

static std::string
read_file_std (const fs::path& path) {
  std::ifstream in (path, std::ios::binary);
  if (!in.good ()) return "";
  std::ostringstream ss;
  ss << in.rdbuf ();
  return ss.str ();
}

static uint64_t
fnv1a (std::string_view text) {
  uint64_t hash= 1469598103934665603ULL;
  for (unsigned char c: text) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return hash;
}

static void
hash_append (uint64_t& hash, std::string_view text) {
  for (unsigned char c: text) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  hash ^= 0xff;
  hash *= 1099511628211ULL;
}

static std::string
hex_hash (uint64_t hash) {
  static const char digits[]= "0123456789abcdef";
  std::string out (16, '0');
  for (int i=15; i>=0; --i) {
    out[(size_t) i]= digits[hash & 15];
    hash >>= 4;
  }
  return out;
}

static fs::path
resolve_sorter_path (string sorter_path) {
  fs::path path (tm_to_std (sorter_path));
  if (path.empty () || path.is_absolute ()) return path;
  if (!vault_active ()) return path;
  return fs::path (tm_to_std (concretize (vault_get_root ()))) / path;
}

static fs::path
resolve_sorter_path (const vault_context_handle& context, string sorter_path) {
  fs::path path (tm_to_std (sorter_path));
  if (path.empty () || path.is_absolute () || !context) return path;
  return context->root / path;
}

static std::string
lua_error_text (lua_State* L) {
  size_t size= 0;
  const char* message= lua_tolstring (L, -1, &size);
  return message == nullptr ? "unknown Luau error" : std::string (message, size);
}

struct luau_limits {
  size_t bytes= 0;
  bool operation_active= false;
  size_t operation_limit= 0;
  bool timed_out= false;
  clock_type::time_point deadline {};

  void begin (std::chrono::milliseconds duration) {
    operation_active= true;
    operation_limit= bytes > std::numeric_limits<size_t>::max () -
                              luau_operation_memory_budget
      ? std::numeric_limits<size_t>::max ()
      : bytes + luau_operation_memory_budget;
    timed_out= false;
    deadline= clock_type::now () + duration;
  }

  void end () { operation_active= false; }
};

static void*
athena_luau_alloc (void* opaque, void* ptr, size_t old_size, size_t new_size) {
  auto* limits= static_cast<luau_limits*> (opaque);
  if (new_size == 0) {
    std::free (ptr);
    limits->bytes= old_size <= limits->bytes ? limits->bytes - old_size : 0;
    return nullptr;
  }
  size_t base= old_size <= limits->bytes ? limits->bytes - old_size : 0;
  if (new_size > std::numeric_limits<size_t>::max () - base) return nullptr;
  size_t next= base + new_size;
  if (next > luau_hard_memory_limit ||
      (limits->operation_active && next > limits->operation_limit))
    return nullptr;
  void* result= std::realloc (ptr, new_size);
  if (result != nullptr) limits->bytes= next;
  return result;
}

static void
athena_luau_interrupt (lua_State* L, int gc) {
  auto* limits= static_cast<luau_limits*> (lua_callbacks (L)->userdata);
  if (limits == nullptr || !limits->operation_active ||
      clock_type::now () <= limits->deadline)
    return;
  limits->timed_out= true;
  if (gc < 0) luaL_error (L, "ATHENA namespace sorter execution timed out");
}

static int
lua_byte_compare (lua_State* L) {
  size_t an= 0, bn= 0;
  const char* a= luaL_checklstring (L, 1, &an);
  const char* b= luaL_checklstring (L, 2, &bn);
  size_t common= std::min (an, bn);
  int cmp= common == 0 ? 0 : std::memcmp (a, b, common);
  if (cmp == 0) cmp= (an > bn) - (an < bn);
  lua_pushinteger (L, (cmp > 0) - (cmp < 0));
  return 1;
}

static int
lua_int64_compare (lua_State* L) {
  int64_t a= luaL_checkinteger64 (L, 1);
  int64_t b= luaL_checkinteger64 (L, 2);
  lua_pushinteger (L, (a > b) - (a < b));
  return 1;
}

static int
lua_roman_value (lua_State* L) {
  size_t size= 0;
  const char* text= luaL_checklstring (L, 1, &size);
  int value= parse_roman_value (std::string_view (text, size));
  lua_pushnumber (L, value);
  return 1;
}

static void
open_luau_library (lua_State* L, const char* name, lua_CFunction open) {
  lua_pushcfunction (L, open, nullptr);
  lua_pushstring (L, name);
  lua_call (L, 1, 0);
}

struct compiled_sorter {
  std::thread::id owner= std::this_thread::get_id ();
  std::string path;
  std::string revision;
  mutable luau_limits limits;
  lua_State* L= nullptr;
  int key_ref= LUA_NOREF;
  int compare_ref= LUA_NOREF;

  ~compiled_sorter () {
    if (L != nullptr) {
      ASSERT (owner == std::this_thread::get_id (),
              "Luau namespace sorter destroyed outside its owning thread");
      lua_close (L);
    }
  }
};

struct sorter_cache_entry {
  std::string revision;
  sorter_handle compiled;
  std::string error;
};

static thread_local std::map<std::string, sorter_cache_entry> sorter_cache;

static void
install_luau_environment (compiled_sorter& sorter) {
  lua_State* L= sorter.L;
  open_luau_library (L, "", luaopen_base);
  open_luau_library (L, LUA_TABLIBNAME, luaopen_table);
  open_luau_library (L, LUA_STRLIBNAME, luaopen_string);
  open_luau_library (L, LUA_MATHLIBNAME, luaopen_math);
  open_luau_library (L, LUA_UTF8LIBNAME, luaopen_utf8);
  open_luau_library (L, LUA_BITLIBNAME, luaopen_bit32);
  open_luau_library (L, LUA_INTLIBNAME, luaopen_integer);

  // Sorting must not depend on process time or hidden mutable randomness.
  lua_getglobal (L, LUA_MATHLIBNAME);
  if (lua_istable (L, -1)) {
    lua_pushnil (L);
    lua_setfield (L, -2, "random");
    lua_pushnil (L);
    lua_setfield (L, -2, "randomseed");
  }
  lua_pop (L, 1);

  lua_newtable (L);
  lua_pushcfunction (L, lua_byte_compare, "byte_compare");
  lua_setfield (L, -2, "byte_compare");
  lua_pushcfunction (L, lua_int64_compare, "int64_compare");
  lua_setfield (L, -2, "int64_compare");
  lua_pushcfunction (L, lua_roman_value, "roman_value");
  lua_setfield (L, -2, "roman_value");
  lua_setreadonly (L, -1, true);
  lua_setglobal (L, "athena");
  luaL_sandbox (L);
}

static sorter_handle
compile_luau_sorter (const fs::path& path, const std::string& source,
                     const std::string& revision, string& error) {
  auto sorter= std::make_shared<compiled_sorter> ();
  sorter->path= path.string ();
  sorter->revision= revision;
  sorter->L= lua_newstate (athena_luau_alloc, &sorter->limits);
  if (sorter->L == nullptr) {
    error= "Cannot initialize Luau VM for namespace sorter.";
    return nullptr;
  }
  lua_callbacks (sorter->L)->userdata= &sorter->limits;
  lua_callbacks (sorter->L)->interrupt= athena_luau_interrupt;
  install_luau_environment (*sorter);

  lua_CompileOptions options {};
  options.optimizationLevel= 1;
  options.debugLevel= 1;
  size_t bytecode_size= 0;
  char* bytecode= luau_compile (source.data (), source.size (), &options,
                                &bytecode_size);
  if (bytecode == nullptr) {
    error= "Luau compiler ran out of memory for namespace sorter: " *
           std_to_tm (path.string ());
    return nullptr;
  }
  int loaded= luau_load (sorter->L, path.string ().c_str (), bytecode,
                         bytecode_size, 0);
  std::free (bytecode);
  if (loaded != LUA_OK) {
    std::string message= lua_error_text (sorter->L);
    lua_settop (sorter->L, 0);
    error= std_to_tm ("Luau sorter compile/load failed [" + path.string () +
                      "]: " + message);
    return nullptr;
  }

  sorter->limits.begin (luau_module_time_budget);
  int status= lua_pcall (sorter->L, 0, 1, 0);
  bool timed_out= sorter->limits.timed_out;
  sorter->limits.end ();
  if (status != LUA_OK || timed_out) {
    std::string message= lua_error_text (sorter->L);
    if (timed_out && status == LUA_OK)
      message= "ATHENA namespace sorter execution timed out";
    lua_settop (sorter->L, 0);
    error= std_to_tm ("Luau sorter module initialization failed [" +
                      path.string () + "]: " + message);
    return nullptr;
  }
  if (lua_type (sorter->L, -1) != LUA_TTABLE) {
    error= std_to_tm ("Luau sorter module must return a table [" +
                      path.string () + "].");
    lua_settop (sorter->L, 0);
    return nullptr;
  }

  lua_getfield (sorter->L, -1, "version");
  bool version_ok= false;
  if (lua_type (sorter->L, -1) == LUA_TINTEGER) {
    int is_integer= 0;
    version_ok= lua_tointeger64 (sorter->L, -1, &is_integer) == 1 &&
                is_integer != 0;
  }
  else if (lua_type (sorter->L, -1) == LUA_TNUMBER)
    version_ok= lua_tonumber (sorter->L, -1) == 1.0;
  lua_pop (sorter->L, 1);
  if (!version_ok) {
    error= std_to_tm ("Luau sorter must declare version = 1 [" +
                       path.string () + "].");
    lua_settop (sorter->L, 0);
    return nullptr;
  }

  lua_getfield (sorter->L, -1, "key");
  bool has_key= lua_type (sorter->L, -1) == LUA_TFUNCTION;
  if (has_key) sorter->key_ref= lua_ref (sorter->L, -1);
  else if (lua_type (sorter->L, -1) != LUA_TNIL) {
    error= std_to_tm ("Luau sorter field 'key' must be a function [" +
                      path.string () + "].");
    lua_settop (sorter->L, 0);
    return nullptr;
  }
  lua_pop (sorter->L, 1);

  lua_getfield (sorter->L, -1, "compare");
  bool has_compare= lua_type (sorter->L, -1) == LUA_TFUNCTION;
  if (has_compare) sorter->compare_ref= lua_ref (sorter->L, -1);
  else if (lua_type (sorter->L, -1) != LUA_TNIL) {
    error= std_to_tm ("Luau sorter field 'compare' must be a function [" +
                      path.string () + "].");
    lua_settop (sorter->L, 0);
    return nullptr;
  }
  lua_pop (sorter->L, 1);
  lua_settop (sorter->L, 0);

  if (has_key == has_compare) {
    error= std_to_tm ("Luau sorter must export exactly one of key or compare [" +
                      path.string () + "].");
    return nullptr;
  }
  return sorter;
}

static sorter_handle
load_luau_sorter_path (const fs::path& path, string& error) {
  error= "";
  if (path.extension () != ".luau") {
    if (path.extension () == ".c")
      error= std_to_tm ("Legacy C namespace sorter is no longer executable: " +
                        path.string () +
                        ". Run --upgrade-vault-sorters with an explicit sorter map.");
    else
      error= std_to_tm ("Namespace sorter must be a .luau script or a generated "
                        ".json composition descriptor: " + path.string ());
    return nullptr;
  }
  std::ifstream probe (path, std::ios::binary);
  if (!probe.good ()) {
    error= std_to_tm ("Cannot open Luau namespace sorter: " + path.string ());
    return nullptr;
  }
  std::error_code size_error;
  uintmax_t source_size= fs::file_size (path, size_error);
  if (size_error || source_size > luau_source_size_limit) {
    error= std_to_tm (size_error
      ? "Cannot determine Luau namespace sorter source size: " + path.string ()
      : "Luau namespace sorter exceeds the 1 MiB source limit: " + path.string ());
    return nullptr;
  }
  std::ostringstream buffer;
  buffer << probe.rdbuf ();
  std::string source= buffer.str ();
  std::string revision= hex_hash (fnv1a (source));
  std::string key= path.lexically_normal ().string ();
  sorter_cache_entry& entry= sorter_cache[key];
  if (entry.revision == revision) {
    if (entry.compiled) return entry.compiled;
    if (!entry.error.empty ()) {
      error= std_to_tm (entry.error);
      return nullptr;
    }
  }
  entry= sorter_cache_entry {};
  entry.revision= revision;
  sorter_handle compiled= compile_luau_sorter (path, source, revision, error);
  if (!compiled) {
    entry.error= tm_to_std (error);
    return nullptr;
  }
  entry.compiled= compiled;
  return compiled;
}

sorter_handle
load_sorter (string sorter_path, string& error) {
  fs::path path= resolve_sorter_path (sorter_path);
  if (path.empty ()) {
    error= "Namespace sorter path is empty.";
    return nullptr;
  }
  return load_luau_sorter_path (path, error);
}

enum class key_kind { string_value, integer_value, number_value, boolean_value };

struct key_atom {
  key_kind kind= key_kind::string_value;
  std::string text;
  int64_t integer= 0;
  double number= 0;
  bool boolean= false;
};

using sort_key= std::vector<key_atom>;

struct order_relation {
  size_t size= 0;
  std::string signature;
  std::function<int(size_t,size_t)> compare;
  bool weak_order= false;
  std::vector<std::shared_ptr<const order_relation>> constraints;
  std::vector<std::string> constraint_labels;
};

static int
byte_compare (const std::string& a, const std::string& b) {
  size_t common= std::min (a.size (), b.size ());
  int cmp= common == 0 ? 0 : std::memcmp (a.data (), b.data (), common);
  if (cmp == 0) cmp= (a.size () > b.size ()) - (a.size () < b.size ());
  return (cmp > 0) - (cmp < 0);
}

static int
compare_atom (const key_atom& a, const key_atom& b) {
  ASSERT (a.kind == b.kind, "heterogeneous Luau key atom comparison");
  switch (a.kind) {
  case key_kind::string_value: return byte_compare (a.text, b.text);
  case key_kind::integer_value: return (a.integer > b.integer) - (a.integer < b.integer);
  case key_kind::number_value: return (a.number > b.number) - (a.number < b.number);
  case key_kind::boolean_value: return (a.boolean > b.boolean) - (a.boolean < b.boolean);
  }
  return 0;
}

static int
compare_key (const sort_key& a, const sort_key& b) {
  size_t common= std::min (a.size (), b.size ());
  for (size_t i=0; i<common; ++i) {
    int cmp= compare_atom (a[i], b[i]);
    if (cmp != 0) return cmp;
  }
  return (a.size () > b.size ()) - (a.size () < b.size ());
}

static std::string
member_fingerprint (const namespace_records<athena_namespace_match>& members) {
  uint64_t hash= 1469598103934665603ULL;
  for (const auto& member: members) {
    hash_append (hash, tm_to_std (member.stem));
    hash_append (hash, std::to_string (member.captures.size ()));
    for (size_t i=0; i<member.captures.size (); ++i) {
      hash_append (hash, tm_to_std (member.captures[i]));
      hash_append (hash, i < member.capture_types.size ()
                         ? tm_to_std (member.capture_types[i]) : "<missing>");
    }
  }
  return hex_hash (hash);
}

struct relation_cache_entry {
  std::shared_ptr<const order_relation> relation;
  uint64_t age= 0;
};

static thread_local std::unordered_map<std::string, relation_cache_entry>
  relation_cache;
static thread_local uint64_t relation_cache_age= 0;

static std::shared_ptr<const order_relation>
relation_cache_get (const std::string& key) {
  auto found= relation_cache.find (key);
  if (found == relation_cache.end ()) return nullptr;
  found->second.age= ++relation_cache_age;
  return found->second.relation;
}

static void
relation_cache_put (std::string key,
                    std::shared_ptr<const order_relation> relation) {
  relation_cache[std::move (key)]= {std::move (relation), ++relation_cache_age};
  if (relation_cache.size () <= 64) return;
  auto oldest= relation_cache.begin ();
  for (auto it= relation_cache.begin (); it != relation_cache.end (); ++it)
    if (it->second.age < oldest->second.age) oldest= it;
  relation_cache.erase (oldest);
}

static bool
parse_int64_exact (const std::string& text, int64_t& value) {
  if (text.empty ()) return false;
  const char* begin= text.data ();
  const char* end= begin + text.size ();
  auto result= std::from_chars (begin, end, value, 10);
  return result.ec == std::errc () && result.ptr == end;
}

static bool
push_fields (lua_State* L, const athena_namespace_match& member,
             std::string& error) {
  if (member.captures.size () != member.capture_types.size ()) {
    error= "namespace capture types do not match captures";
    return false;
  }
  lua_createtable (L, (int) member.captures.size (), 0);
  for (size_t i=0; i<member.captures.size (); ++i) {
    std::string text= tm_to_std (member.captures[i]);
    std::string type= tm_to_std (member.capture_types[i]);
    lua_createtable (L, 0, 4);
    lua_pushlstring (L, text.data (), text.size ());
    lua_setfield (L, -2, "text");
    lua_pushlstring (L, type.data (), type.size ());
    lua_setfield (L, -2, "type");
    if (type == "int" || type == "positive-int") {
      int64_t value= 0;
      if (!parse_int64_exact (text, value)) {
        error= "invalid exact signed 64-bit integer capture: " + text;
        lua_pop (L, 2);
        return false;
      }
      lua_pushinteger64 (L, value);
      lua_setfield (L, -2, "integer");
    }
    else if (type == "roman") {
      lua_pushnumber (L, parse_roman_value (text));
      lua_setfield (L, -2, "roman");
    }
    else if (type != "string" && type != "word" && type != "char") {
      error= "unsupported namespace field type: " + type;
      lua_pop (L, 2);
      return false;
    }
    lua_setreadonly (L, -1, true);
    lua_rawseti (L, -2, (int) i + 1);
  }
  lua_setreadonly (L, -1, true);
  return true;
}

static bool
decode_key (lua_State* L, sort_key& key, std::string& error) {
  if (lua_type (L, -1) != LUA_TTABLE) {
    error= "key(fields) must return an array table";
    return false;
  }
  int count= lua_objlen (L, -1);
  if (count < 0 || count > 256) {
    error= "key(fields) may return at most 256 atoms";
    return false;
  }
  key.clear ();
  key.reserve ((size_t) count);
  for (int i=1; i<=count; ++i) {
    lua_rawgeti (L, -1, i);
    key_atom atom;
    int type= lua_type (L, -1);
    if (type == LUA_TSTRING) {
      size_t size= 0;
      const char* text= lua_tolstring (L, -1, &size);
      atom.kind= key_kind::string_value;
      atom.text.assign (text, size);
    }
    else if (type == LUA_TINTEGER) {
      int exact= 0;
      atom.integer= lua_tointeger64 (L, -1, &exact);
      if (!exact) {
        error= "integer key atom was not representable as signed 64-bit";
        lua_pop (L, 1);
        return false;
      }
      atom.kind= key_kind::integer_value;
    }
    else if (type == LUA_TNUMBER) {
      atom.number= lua_tonumber (L, -1);
      if (!std::isfinite (atom.number)) {
        error= "numeric key atoms must be finite (NaN and infinity are rejected)";
        lua_pop (L, 1);
        return false;
      }
      atom.kind= key_kind::number_value;
    }
    else if (type == LUA_TBOOLEAN) {
      atom.kind= key_kind::boolean_value;
      atom.boolean= lua_toboolean (L, -1) != 0;
    }
    else {
      error= "key atoms must be strings, exact int64 integers, finite numbers, or booleans";
      lua_pop (L, 1);
      return false;
    }
    key.push_back (std::move (atom));
    lua_pop (L, 1);
  }
  return true;
}

static bool
call_key (const compiled_sorter& sorter, const athena_namespace_match& member,
          sort_key& key, std::string& error) {
  lua_State* L= sorter.L;
  lua_settop (L, 0);
  lua_getref (L, sorter.key_ref);
  if (!push_fields (L, member, error)) {
    lua_settop (L, 0);
    return false;
  }
  sorter.limits.begin (luau_call_time_budget);
  int status= lua_pcall (L, 1, 1, 0);
  bool timed_out= sorter.limits.timed_out;
  sorter.limits.end ();
  if (status != LUA_OK || timed_out) {
    error= lua_error_text (L);
    if (timed_out && status == LUA_OK)
      error= "ATHENA namespace sorter execution timed out";
    lua_settop (L, 0);
    return false;
  }
  bool ok= decode_key (L, key, error);
  lua_settop (L, 0);
  return ok;
}

static bool
call_compare (const compiled_sorter& sorter,
              const athena_namespace_match& a,
              const athena_namespace_match& b, int& result,
              std::string& error) {
  lua_State* L= sorter.L;
  lua_settop (L, 0);
  lua_getref (L, sorter.compare_ref);
  if (!push_fields (L, a, error) || !push_fields (L, b, error)) {
    lua_settop (L, 0);
    return false;
  }
  sorter.limits.begin (luau_call_time_budget);
  int status= lua_pcall (L, 2, 1, 0);
  bool timed_out= sorter.limits.timed_out;
  sorter.limits.end ();
  if (status != LUA_OK || timed_out) {
    error= lua_error_text (L);
    if (timed_out && status == LUA_OK)
      error= "ATHENA namespace sorter execution timed out";
    lua_settop (L, 0);
    return false;
  }
  bool exact= false;
  int64_t value= 0;
  if (lua_type (L, -1) == LUA_TINTEGER) {
    int is_integer= 0;
    value= lua_tointeger64 (L, -1, &is_integer);
    exact= is_integer != 0;
  }
  else if (lua_type (L, -1) == LUA_TNUMBER) {
    double number= lua_tonumber (L, -1);
    exact= std::isfinite (number) && std::floor (number) == number &&
           number >= -1.0 && number <= 1.0;
    if (exact) value= (int64_t) number;
  }
  if (!exact || value < -1 || value > 1) {
    error= "compare(a,b) must return exactly -1, 0, or 1";
    lua_settop (L, 0);
    return false;
  }
  result= (int) value;
  lua_settop (L, 0);
  return true;
}

static bool
validate_key_shapes (const std::vector<sort_key>& keys, std::string& error) {
  std::vector<key_kind> expected;
  std::vector<bool> established;
  for (size_t i=0; i<keys.size (); ++i) {
    if (expected.size () < keys[i].size ()) {
      expected.resize (keys[i].size ());
      established.resize (keys[i].size (), false);
    }
    for (size_t k=0; k<keys[i].size (); ++k) {
      if (!established[k]) {
        expected[k]= keys[i][k].kind;
        established[k]= true;
      }
      else if (expected[k] != keys[i][k].kind) {
        error= "heterogeneous Luau key atom types at key position " +
               std::to_string (k + 1) + " for member " +
               std::to_string (i + 1);
        return false;
      }
    }
  }
  return true;
}

static bool
validate_compare_matrix (const std::vector<int8_t>& matrix, size_t n,
                         std::string& error) {
  auto at= [&] (size_t i, size_t j) { return (int) matrix[i * n + j]; };
  for (size_t i=0; i<n; ++i) {
    if (at (i, i) != 0) {
      error= "compare(a,a) must return 0 for member " + std::to_string (i + 1);
      return false;
    }
    for (size_t j=i + 1; j<n; ++j)
      if (at (i, j) != -at (j, i)) {
        error= "compare must be sign-antisymmetric for members " +
               std::to_string (i + 1) + " and " + std::to_string (j + 1);
        return false;
      }
  }
  // Zero supplies no precedence edge; it need not be transitive.
  // Cycle detection happens when the constraints are ordered for publication.
  return true;
}

static std::shared_ptr<const order_relation>
evaluate_direct (const sorter_handle& sorter,
                 const namespace_records<athena_namespace_match>& members,
                 string& error) {
  if (!sorter) return nullptr;
  ASSERT (sorter->owner == std::this_thread::get_id (),
          "Luau namespace sorter used outside its owning thread");
  std::string fingerprint= member_fingerprint (members);
  std::string cache_key= sorter->path + "#" + sorter->revision + "#" + fingerprint;
  if (auto cached= relation_cache_get (cache_key)) return cached;

  auto relation= std::make_shared<order_relation> ();
  relation->size= members.size ();
  relation->signature= sorter->revision + ":" + fingerprint;
  const auto sort_deadline= clock_type::now () + luau_sort_time_budget;
  if (sorter->key_ref != LUA_NOREF) {
    relation->weak_order= true;
    if (members.size () > sorter_host_memory_budget / sizeof (sort_key)) {
      error= "Namespace sorter key cache exceeds the 64 MiB host memory budget.";
      return nullptr;
    }
    auto keys= std::make_shared<std::vector<sort_key>> (members.size ());
    size_t host_bytes= members.size () * sizeof (sort_key);
    for (size_t i=0; i<members.size (); ++i) {
      if (clock_type::now () > sort_deadline) {
        error= std_to_tm ("Luau namespace sorter exceeded the 5 second sort budget [" +
                          sorter->path + "].");
        return nullptr;
      }
      std::string detail;
      if (!call_key (*sorter, members[i], (*keys)[i], detail)) {
        error= std_to_tm ("Luau namespace sorter key failed [" + sorter->path +
                          ", member '" + tm_to_std (members[i].stem) + "']: " + detail);
        return nullptr;
      }
      size_t key_bytes= (*keys)[i].size () * sizeof (key_atom);
      for (const key_atom& atom: (*keys)[i])
        if (atom.kind == key_kind::string_value) {
          if (atom.text.size () > sorter_host_memory_budget -
                                  std::min (sorter_host_memory_budget, key_bytes)) {
            error= "Namespace sorter key cache exceeds the 64 MiB host memory budget.";
            return nullptr;
          }
          key_bytes += atom.text.size ();
        }
      if (key_bytes > sorter_host_memory_budget - host_bytes) {
        error= "Namespace sorter key cache exceeds the 64 MiB host memory budget.";
        return nullptr;
      }
      host_bytes += key_bytes;
    }
    std::string detail;
    if (!validate_key_shapes (*keys, detail)) {
      error= std_to_tm ("Luau namespace sorter key contract failed [" +
                        sorter->path + "]: " + detail);
      return nullptr;
    }
    relation->compare= [keys] (size_t a, size_t b) {
      return compare_key ((*keys)[a], (*keys)[b]);
    };
  }
  else {
    size_t n= members.size ();
    if (n != 0 &&
        (n > std::numeric_limits<size_t>::max () / n ||
         n * n > sorter_host_memory_budget)) {
      error= "Namespace compare relation exceeds the 64 MiB host memory budget.";
      return nullptr;
    }
    auto matrix= std::make_shared<std::vector<int8_t>> (n * n, 0);
    try {
      for (size_t i=0; i<n; ++i)
        for (size_t j=0; j<n; ++j) {
          if (clock_type::now () > sort_deadline) {
            error= std_to_tm ("Luau namespace comparator exceeded the 5 second sort budget [" +
                              sorter->path + "].");
            return nullptr;
          }
          int value= 0;
          std::string detail;
          if (!call_compare (*sorter, members[i], members[j], value, detail)) {
            error= std_to_tm ("Luau namespace sorter compare failed [" +
                              sorter->path + ", members '" +
                              tm_to_std (members[i].stem) + "' / '" +
                              tm_to_std (members[j].stem) + "']: " + detail);
            return nullptr;
          }
          (*matrix)[i * n + j]= (int8_t) value;
        }
    }
    catch (const std::bad_alloc&) {
      error= "Not enough memory to validate namespace compare relation.";
      return nullptr;
    }
    std::string detail;
    if (!validate_compare_matrix (*matrix, n, detail)) {
      error= std_to_tm ("Luau namespace sorter compare contract failed [" +
                        sorter->path + "]: " + detail);
      return nullptr;
    }
    relation->compare= [matrix, n] (size_t a, size_t b) {
      return (int) (*matrix)[a * n + b];
    };
  }
  relation_cache_put (cache_key, relation);
  return relation;
}

static bool constraint_relation_order (
  const std::vector<std::shared_ptr<const order_relation>>& parents,
  const std::vector<std::string>& parent_labels,
  const namespace_records<athena_namespace_match>& members,
  std::vector<size_t>& output, string& error);

static bool
apply_relation (const order_relation& relation,
                namespace_records<athena_namespace_match>& members,
                string& error) {
  if (relation.size != members.size ()) {
    error= "Namespace sorter relation/member size mismatch.";
    return false;
  }
  std::vector<size_t> order (members.size ());
  std::iota (order.begin (), order.end (), 0);
  if (relation.weak_order) {
    std::stable_sort (order.begin (), order.end (), [&] (size_t a, size_t b) {
      return relation.compare (a, b) < 0;
    });
  }
  else if (!constraint_relation_order (
             {std::make_shared<const order_relation> (relation)},
             {"sorter"}, members, order, error)) return false;
  members.reorder (order);
  return true;
}

bool
sort_namespace_members (const sorter_handle& sorter,
                        namespace_records<athena_namespace_match>& members,
                        string& error) {
  error= "";
  auto relation= evaluate_direct (sorter, members, error);
  if (!relation) return false;
  return apply_relation (*relation, members, error);
}

struct composition_part {
  bool child= false;
  size_t child_index= 0;
  std::string literal;
};

struct composition_field {
  std::string type;
  std::vector<composition_part> parts;
};

struct composition_parent {
  std::string uuid;
  std::string name;
  std::string templ;
  std::vector<composition_field> projection;
};

struct composition_plan {
  std::string mode;
  std::string product_template;
  std::vector<composition_parent> parents;
  std::string revision;
  fs::path path;
};

static bool
json_string_member (const QJsonObject& object, const char* name,
                    std::string& value, std::string& error) {
  QJsonValue item= object.value (name);
  if (!item.isString ()) {
    error= std::string ("composition field '") + name + "' must be a string";
    return false;
  }
  value= item.toString ().toStdString ();
  return true;
}

static bool
parse_composition (const fs::path& path, composition_plan& plan,
                   string& error) {
  if (!fs::exists (path)) {
    error= std_to_tm ("Cannot read namespace composition descriptor: " + path.string ());
    return false;
  }
  std::error_code size_error;
  uintmax_t source_size= fs::file_size (path, size_error);
  if (size_error || source_size > luau_source_size_limit) {
    error= std_to_tm (size_error
      ? "Cannot determine namespace composition descriptor size: " + path.string ()
      : "Namespace composition descriptor exceeds the 1 MiB size limit: " + path.string ());
    return false;
  }
  std::string source= read_file_std (path);
  QJsonParseError parse_error;
  QJsonDocument document= QJsonDocument::fromJson (
    QByteArray (source.data (), (qsizetype) source.size ()), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject ()) {
    error= std_to_tm ("Invalid namespace composition JSON [" + path.string () +
                      "]: " + parse_error.errorString ().toStdString ());
    return false;
  }
  QJsonObject root= document.object ();
  if (root.value ("format").toString ().toStdString () != composition_format ||
      !root.value ("version").isDouble () || root.value ("version").toInt () != 1) {
    error= std_to_tm ("Unsupported namespace composition descriptor format/version: " +
                      path.string ());
    return false;
  }
  std::string detail;
  if (!json_string_member (root, "mode", plan.mode, detail) ||
      !json_string_member (root, "product_template", plan.product_template, detail)) {
    error= std_to_tm ("Invalid namespace composition [" + path.string () + "]: " + detail);
    return false;
  }
  if (plan.mode != "restricted" && plan.mode != "lexicographic" &&
      plan.mode != "constraint-union") {
    error= std_to_tm ("Unknown namespace composition mode '" + plan.mode +
                      "' [" + path.string () + "].");
    return false;
  }
  QJsonValue parents_value= root.value ("parents");
  if (!parents_value.isArray ()) {
    error= std_to_tm ("Namespace composition parents must be an array: " + path.string ());
    return false;
  }
  QJsonArray parents= parents_value.toArray ();
  if ((plan.mode == "restricted" && parents.size () != 1) ||
      (plan.mode != "restricted" && parents.size () != 2)) {
    error= std_to_tm ("Namespace composition has the wrong parent count for mode '" +
                      plan.mode + "': " + path.string ());
    return false;
  }
  plan.parents.clear ();
  for (const QJsonValue& parent_value: parents) {
    if (!parent_value.isObject ()) {
      error= "Namespace composition parent must be an object.";
      return false;
    }
    QJsonObject parent_object= parent_value.toObject ();
    composition_parent parent;
    if (!json_string_member (parent_object, "uuid", parent.uuid, detail) ||
        !json_string_member (parent_object, "name", parent.name, detail) ||
        !json_string_member (parent_object, "template", parent.templ, detail)) {
      error= std_to_tm ("Invalid namespace composition parent: " + detail);
      return false;
    }
    if (parent.uuid.empty ()) {
      error= "Namespace composition parent UUID cannot be empty.";
      return false;
    }
    QJsonValue projection_value= parent_object.value ("projection");
    if (!projection_value.isArray ()) {
      error= "Namespace composition parent projection must be an array.";
      return false;
    }
    for (const QJsonValue& field_value: projection_value.toArray ()) {
      if (!field_value.isObject ()) {
        error= "Namespace composition projection field must be an object.";
        return false;
      }
      QJsonObject field_object= field_value.toObject ();
      composition_field field;
      if (!json_string_member (field_object, "type", field.type, detail)) {
        error= std_to_tm (detail);
        return false;
      }
      QJsonValue parts_value= field_object.value ("parts");
      if (!parts_value.isArray ()) {
        error= "Namespace composition projection parts must be an array.";
        return false;
      }
      for (const QJsonValue& part_value: parts_value.toArray ()) {
        if (!part_value.isObject ()) {
          error= "Namespace composition projection part must be an object.";
          return false;
        }
        QJsonObject part_object= part_value.toObject ();
        bool has_child= part_object.contains ("child_field");
        bool has_literal= part_object.contains ("literal");
        if (has_child == has_literal) {
          error= "Projection part must contain exactly one of child_field or literal.";
          return false;
        }
        composition_part part;
        if (has_child) {
          QJsonValue child= part_object.value ("child_field");
          int index= child.toInt (-1);
          if (!child.isDouble () || index < 1) {
            error= "Projection child_field must be a positive one-based integer.";
            return false;
          }
          part.child= true;
          part.child_index= (size_t) index - 1;
        }
        else {
          if (!part_object.value ("literal").isString ()) {
            error= "Projection literal must be a string.";
            return false;
          }
          part.literal= part_object.value ("literal").toString ().toStdString ();
        }
        field.parts.push_back (std::move (part));
      }
      parent.projection.push_back (std::move (field));
    }
    plan.parents.push_back (std::move (parent));
  }
  plan.path= path;
  plan.revision= hex_hash (fnv1a (source));
  return true;
}

static bool
build_stem_from_template (string templ, const std::vector<string>& captures,
                          string& stem, string& error) {
  std::vector<template_token> tokens;
  if (!parse_template (templ, tokens, error)) return false;
  std::string out;
  size_t field= 0;
  for (const auto& token: tokens) {
    if (!token.field) out += token.literal;
    else {
      if (field >= captures.size ()) {
        error= "Projection did not provide enough fields for parent template.";
        return false;
      }
      out += tm_to_std (captures[field++]);
    }
  }
  if (field != captures.size ()) {
    error= "Projection provided too many fields for parent template.";
    return false;
  }
  stem= std_to_tm (out);
  return true;
}

static bool
project_members (const composition_parent& spec,
                 const athena_namespace_definition& parent,
                 const namespace_records<athena_namespace_match>& members,
                 namespace_records<athena_namespace_match>& projected,
                 string& error) {
  std::vector<athena_namespace_match> values;
  values.reserve (members.size ());
  for (const auto& member: members) {
    athena_namespace_match item;
    item.file_path= member.file_path;
    item.ambiguous= member.ambiguous;
    for (const composition_field& field: spec.projection) {
      std::string text;
      for (const composition_part& part: field.parts) {
        if (!part.child) text += part.literal;
        else {
          if (part.child_index >= member.captures.size ()) {
            error= std_to_tm ("Namespace composition projection references missing child field " +
                              std::to_string (part.child_index + 1) + ".");
            return false;
          }
          text += tm_to_std (member.captures[part.child_index]);
        }
      }
      ns_field_type field_type;
      if (field.type == "string") field_type= ns_string_field;
      else if (field.type == "word") field_type= ns_word_field;
      else if (field.type == "char") field_type= ns_char_field;
      else if (field.type == "int") field_type= ns_int_field;
      else if (field.type == "positive-int") field_type= ns_pos_int_field;
      else if (field.type == "roman") field_type= ns_roman_field;
      else {
        error= std_to_tm ("Unknown projected namespace field type: " + field.type);
        return false;
      }
      if (!field_value_satisfies_type (field_type, text)) {
        error= std_to_tm ("Projected value '" + text + "' does not satisfy parent field type '" +
                          field.type + "' for namespace " + tm_to_std (parent.name) + ".");
        return false;
      }
      item.captures.push_back (std_to_tm (text));
      item.capture_types.push_back (std_to_tm (field.type));
    }
    if (!build_stem_from_template (parent.templ, item.captures, item.stem, error))
      return false;
    values.push_back (std::move (item));
  }
  projected= namespace_records<athena_namespace_match> (std::move (values));
  return true;
}

static std::shared_ptr<const order_relation> evaluate_namespace_relation (
  const vault_context_handle& context, const athena_namespace_definition& ns,
  const namespace_records<athena_namespace_match>& members,
  std::vector<std::string>& dependency_stack, string& error);

static std::shared_ptr<const order_relation>
constant_relation (size_t size, const std::string& signature) {
  auto relation= std::make_shared<order_relation> ();
  relation->size= size;
  relation->signature= signature;
  relation->compare= [] (size_t, size_t) { return 0; };
  relation->weak_order= true;
  return relation;
}

static std::shared_ptr<const order_relation>
stem_relation (const namespace_records<athena_namespace_match>& members,
               const std::string& signature) {
  auto stems= std::make_shared<std::vector<std::string>> ();
  stems->reserve (members.size ());
  for (const auto& member: members) stems->push_back (tm_to_std (member.stem));
  auto relation= std::make_shared<order_relation> ();
  relation->size= members.size ();
  relation->signature= signature;
  relation->compare= [stems] (size_t a, size_t b) {
    return byte_compare ((*stems)[a], (*stems)[b]);
  };
  relation->weak_order= true;
  return relation;
}

static void
stable_relation_order (const order_relation& relation, std::vector<size_t>& order) {
  order.resize (relation.size);
  std::iota (order.begin (), order.end (), 0);
  std::stable_sort (order.begin (), order.end (), [&] (size_t a, size_t b) {
    return relation.compare (a, b) < 0;
  });
}

static bool
constraint_relation_order (
  const std::vector<std::shared_ptr<const order_relation>>& parents,
  const std::vector<std::string>& parent_labels,
  const namespace_records<athena_namespace_match>& members,
  std::vector<size_t>& output, string& error) {
  const size_t n= members.size ();
  struct node { bool item= false; size_t item_index= 0; std::string label; };
  std::vector<node> nodes;
  nodes.reserve (n + parents.size () * (n + 1));
  for (size_t i=0; i<n; ++i)
    nodes.push_back ({true, i, "member:" + tm_to_std (members[i].stem)});
  std::vector<std::vector<size_t>> edges (n);
  std::vector<size_t> indegree (n, 0);
  auto add_node= [&] (node value) {
    nodes.push_back (std::move (value));
    edges.emplace_back ();
    indegree.push_back (0);
    return nodes.size () - 1;
  };
  auto add_edge= [&] (size_t from, size_t to) {
    edges[from].push_back (to);
    ++indegree[to];
  };

  std::function<void(const order_relation&, const std::string&)> append;
  append= [&] (const order_relation& parent, const std::string& label) {
    if (!parent.constraints.empty ()) {
      for (size_t p=0; p<parent.constraints.size (); ++p)
        append (*parent.constraints[p], parent.constraint_labels[p]);
      return;
    }
    if (!parent.weak_order) {
      for (size_t a=0; a<n; ++a)
        for (size_t b=a + 1; b<n; ++b) {
          int cmp= parent.compare (a, b);
          if (cmp < 0) add_edge (a, b);
          else if (cmp > 0) add_edge (b, a);
        }
      return;
    }
    // Only key/stem relations have transitive equality classes. Their edges
    // can be represented by group barriers instead of quadratic pair lists.
    std::vector<size_t> order;
    stable_relation_order (parent, order);
    std::vector<std::vector<size_t>> groups;
    for (size_t item: order) {
      if (groups.empty () || parent.compare (groups.back ().front (), item) != 0)
        groups.emplace_back ();
      groups.back ().push_back (item);
    }
    size_t before= add_node ({false, 0, label + ":start"});
    for (size_t g=0; g<groups.size (); ++g) {
      size_t after= add_node ({false, 0, label +
                                          ":group-" + std::to_string (g + 1)});
      for (size_t item: groups[g]) {
        add_edge (before, item);
        add_edge (item, after);
      }
      before= after;
    }
  };
  for (size_t p=0; p<parents.size (); ++p)
    append (*parents[p], p < parent_labels.size ()
      ? parent_labels[p] : "parent-" + std::to_string (p + 1));

  std::priority_queue<size_t, std::vector<size_t>, std::greater<size_t>> ready_items;
  std::queue<size_t> ready_virtual;
  for (size_t i=0; i<nodes.size (); ++i)
    if (indegree[i] == 0) {
      if (nodes[i].item) ready_items.push (nodes[i].item_index);
      else ready_virtual.push (i);
    }
  auto release_virtual= [&] () {
    while (!ready_virtual.empty ()) {
      size_t current= ready_virtual.front ();
      ready_virtual.pop ();
      for (size_t target: edges[current])
        if (--indegree[target] == 0) {
          if (nodes[target].item) ready_items.push (nodes[target].item_index);
          else ready_virtual.push (target);
        }
    }
  };
  release_virtual ();
  output.clear ();
  output.reserve (n);
  std::vector<bool> emitted (n, false);
  while (!ready_items.empty ()) {
    size_t item= ready_items.top ();
    ready_items.pop ();
    if (emitted[item]) continue;
    emitted[item]= true;
    output.push_back (item);
    for (size_t target: edges[item])
      if (--indegree[target] == 0) {
        if (nodes[target].item) ready_items.push (nodes[target].item_index);
        else ready_virtual.push (target);
      }
    release_virtual ();
  }
  if (output.size () != n) {
    std::vector<int> state (nodes.size (), 0);
    std::vector<size_t> stack;
    std::vector<size_t> witness;
    std::function<bool(size_t)> dfs= [&] (size_t u) {
      state[u]= 1;
      stack.push_back (u);
      for (size_t v: edges[u]) {
        if (indegree[v] == 0) continue;
        if (state[v] == 0 && dfs (v)) return true;
        if (state[v] == 1) {
          auto found= std::find (stack.begin (), stack.end (), v);
          witness.assign (found, stack.end ());
          witness.push_back (v);
          return true;
        }
      }
      stack.pop_back ();
      state[u]= 2;
      return false;
    };
    for (size_t i=0; i<nodes.size () && witness.empty (); ++i)
      if (indegree[i] != 0 && state[i] == 0) dfs (i);
    std::string message= "namespace ordering constraint cycle";
    if (!witness.empty ()) {
      message += ": ";
      for (size_t i=0; i<witness.size (); ++i) {
        if (i != 0) message += " -> ";
        message += nodes[witness[i]].label;
      }
    }
    error= std_to_tm (message);
    return false;
  }
  return true;
}

static std::shared_ptr<const order_relation>
constraint_union_relation (
  const std::vector<std::shared_ptr<const order_relation>>& parents,
  const std::vector<std::string>& parent_labels,
  const namespace_records<athena_namespace_match>& members,
  const std::string& signature, string& error) {
  std::vector<size_t> order;
  if (!constraint_relation_order (parents, parent_labels, members, order, error))
    return nullptr;
  auto relation= std::make_shared<order_relation> ();
  relation->size= members.size ();
  relation->signature= signature;
  relation->constraints= parents;
  relation->constraint_labels= parent_labels;
  // Keep the parent constraints, not the ranks of one linear extension.
  relation->compare= [parents] (size_t a, size_t b) {
    for (const auto& parent: parents) {
      int cmp= parent->compare (a, b);
      if (cmp != 0) return cmp;
    }
    return 0;
  };
  return relation;
}

static std::shared_ptr<const order_relation>
evaluate_composition (const vault_context_handle& context,
                      const athena_namespace_definition& ns,
                      const namespace_records<athena_namespace_match>& members,
                      const composition_plan& plan,
                      std::vector<std::string>& dependency_stack,
                      string& error) {
  if (tm_to_std (ns.templ) != plan.product_template) {
    error= std_to_tm ("Namespace composition template is stale for '" +
                      tm_to_std (ns.name) + "'; regenerate the composition descriptor.");
    return nullptr;
  }
  std::string identity= tm_to_std (ns.uuid);
  if (identity.empty ()) identity= "path:" + plan.path.string ();
  auto cycle= std::find (dependency_stack.begin (), dependency_stack.end (), identity);
  if (cycle != dependency_stack.end ()) {
    std::string message= "namespace sorter dependency cycle: ";
    for (auto it= cycle; it != dependency_stack.end (); ++it) {
      if (it != cycle) message += " -> ";
      message += *it;
    }
    message += " -> " + identity;
    error= std_to_tm (message);
    return nullptr;
  }
  dependency_stack.push_back (identity);
  struct pop_guard {
    std::vector<std::string>& stack;
    ~pop_guard () { stack.pop_back (); }
  } guard {dependency_stack};

  std::vector<std::shared_ptr<const order_relation>> parent_relations;
  std::vector<std::string> parent_labels;
  std::string signature= plan.revision + ":" + tm_to_std (ns.uuid) + ":" +
                         tm_to_std (ns.templ) + ":" + member_fingerprint (members);
  for (const composition_parent& spec: plan.parents) {
    std::shared_ptr<const athena_namespace_definition> parent;
    string lookup_error;
    auto status= athena_namespace_get_by_uuid (context, std_to_tm (spec.uuid),
                                               parent, lookup_error);
    if (status != namespace_query_status::ok || !parent) {
      error= lookup_error == ""
        ? std_to_tm ("Namespace composition parent UUID no longer exists: " + spec.uuid)
        : lookup_error;
      return nullptr;
    }
    if (tm_to_std (parent->templ) != spec.templ) {
      error= std_to_tm ("Namespace composition projection is stale because parent '" +
                        tm_to_std (parent->name) + "' changed template; regenerate the composition.");
      return nullptr;
    }
    namespace_records<athena_namespace_match> projected;
    if (!project_members (spec, *parent, members, projected, error)) return nullptr;
    auto relation= evaluate_namespace_relation (context, *parent, projected,
                                                dependency_stack, error);
    if (!relation) return nullptr;
    signature += ":" + spec.uuid + ":" + relation->signature;
    parent_labels.push_back (tm_to_std (parent->name) + " [" + spec.uuid + "]");
    parent_relations.push_back (std::move (relation));
  }

  if (plan.mode == "restricted") return parent_relations.front ();
  if (plan.mode == "lexicographic") {
    auto parents= std::make_shared<
      std::vector<std::shared_ptr<const order_relation>>> (std::move (parent_relations));
    auto relation= std::make_shared<order_relation> ();
    relation->size= members.size ();
    relation->signature= signature;
    relation->weak_order= std::all_of (parents->begin (), parents->end (),
      [] (const auto& parent) { return parent->weak_order; });
    relation->compare= [parents] (size_t a, size_t b) {
      for (const auto& parent: *parents) {
        int cmp= parent->compare (a, b);
        if (cmp != 0) return cmp;
      }
      return 0;
    };
    return relation;
  }
  return constraint_union_relation (
    parent_relations, parent_labels, members, signature, error);
}

static std::shared_ptr<const order_relation>
evaluate_namespace_relation (
  const vault_context_handle& context, const athena_namespace_definition& ns,
  const namespace_records<athena_namespace_match>& members,
  std::vector<std::string>& dependency_stack, string& error) {
  error= "";
  std::string base= tm_to_std (ns.uuid) + ":" + tm_to_std (ns.templ) + ":" +
                    member_fingerprint (members);
  if (ns.sorter_trivial) return constant_relation (members.size (), "trivial:" + base);
  if (ns.sorter_path == "") return stem_relation (members, "stem:" + base);
  fs::path path= resolve_sorter_path (context, ns.sorter_path);
  if (path.extension () == ".luau") {
    sorter_handle sorter= load_luau_sorter_path (path, error);
    return sorter ? evaluate_direct (sorter, members, error) : nullptr;
  }
  if (path.extension () == ".c") {
    error= std_to_tm ("Namespace '" + tm_to_std (ns.name) +
                      "' still references legacy C sorter '" + path.string () +
                      "'. Run --upgrade-vault-sorters with an explicit sorter map.");
    return nullptr;
  }
  if (path.extension () != ".json") {
    error= std_to_tm ("Unsupported namespace sorter artifact: " + path.string () +
                      ". Expected .luau or generated .json composition.");
    return nullptr;
  }
  composition_plan plan;
  if (!parse_composition (path, plan, error)) return nullptr;
  return evaluate_composition (context, ns, members, plan, dependency_stack, error);
}

bool
sort_namespace_members (const vault_context_handle& context,
                        const athena_namespace_definition& ns,
                        namespace_records<athena_namespace_match>& members,
                        string& error) {
  std::vector<std::string> dependency_stack;
  auto relation= evaluate_namespace_relation (context, ns, members,
                                              dependency_stack, error);
  if (!relation) return false;
  return apply_relation (*relation, members, error);
}

static QJsonArray
projection_json (const derivation_result& mapping) {
  QJsonArray fields;
  for (const parent_field_expr& source: mapping.fields) {
    QJsonArray parts;
    for (const field_fragment& fragment: source.parts) {
      if (fragment.child)
        parts.append (QJsonObject {{"child_field", fragment.child_index + 1}});
      else parts.append (QJsonObject {{"literal", QString::fromStdString (fragment.literal)}});
    }
    fields.append (QJsonObject {
      {"type", QString::fromLatin1 (field_type_name (source.type))},
      {"parts", parts}
    });
  }
  return fields;
}

static QJsonObject
parent_json (const athena_namespace_definition& parent,
             const derivation_result& mapping) {
  return QJsonObject {
    {"uuid", QString::fromStdString (tm_to_std (parent.uuid))},
    {"name", QString::fromStdString (tm_to_std (parent.name))},
    {"template", QString::fromStdString (tm_to_std (parent.templ))},
    {"projection", projection_json (mapping)}
  };
}

static std::string
safe_file_component (const std::string& text) {
  std::string out;
  for (unsigned char c: text) {
    if (std::isalnum (c) || c == '-' || c == '_') out.push_back ((char) std::tolower (c));
    else if (std::isspace (c)) out.push_back ('-');
  }
  return out.empty () ? "namespace" : out;
}

static bool
write_composition_descriptor (const vault_context_handle& context,
                              const QJsonObject& object,
                              const std::string& stem,
                              string& sorter_path, string& error) {
  if (!context) { error= "No vault context."; return false; }
  fs::path directory= context->root / ".athena" / "ns-sorters";
  std::error_code ec;
  fs::create_directories (directory, ec);
  if (ec) {
    error= std_to_tm ("Cannot create namespace sorter directory: " + ec.message ());
    return false;
  }
  fs::path file= directory /
    (stem + "-" + QUuid::createUuid ().toString (QUuid::WithoutBraces).toStdString () +
     ".json");
  QByteArray bytes= QJsonDocument (object).toJson (QJsonDocument::Indented);
  std::ofstream out (file, std::ios::binary | std::ios::trunc);
  if (!out.good ()) {
    error= "Cannot write namespace composition descriptor.";
    return false;
  }
  out.write (bytes.constData (), bytes.size ());
  out.close ();
  if (!out.good ()) {
    fs::remove (file, ec);
    error= "Cannot finish namespace composition descriptor write.";
    return false;
  }
  composition_plan validation;
  if (!parse_composition (file, validation, error)) {
    fs::remove (file, ec);
    return false;
  }
  fs::path relative= fs::relative (file, context->root, ec);
  sorter_path= std_to_tm (ec ? file.string () : relative.generic_string ());
  return true;
}

} // namespace athena_namespaces

using namespace athena_namespaces;

bool
athena_namespace_sorter_source (const athena_namespace_definition& ns,
                                string& source, string& error) {
  error= "";
  if (ns.sorter_trivial) {
    source= "Built-in trivial sorter: every pair of files compares equal.";
    return true;
  }
  if (ns.sorter_path == "") {
    source= "Built-in bytewise stem sorter.";
    return true;
  }
  fs::path path= resolve_sorter_path (ns.sorter_path);
  if (path.extension () == ".c") {
    error= std_to_tm ("Legacy C namespace sorter is no longer supported: " + path.string ());
    return false;
  }
  std::ifstream in (path, std::ios::binary);
  if (!in.good ()) {
    error= std_to_tm ("Cannot open namespace sorter artifact: " + path.string ());
    return false;
  }
  std::ostringstream contents;
  contents << in.rdbuf ();
  source= std_to_tm (contents.str ());
  return true;
}

bool
athena_namespace_generate_product_sorter (
  const athena_namespace_definition& first,
  const athena_namespace_definition& second,
  string product_template, string composition_mode,
  string& sorter_path, string& error) {
  return athena_namespace_generate_product_sorter (
    vault_capture_context (), first, second, product_template, composition_mode,
    sorter_path, error);
}

bool
athena_namespace_generate_product_sorter (
  const vault_context_handle& context,
  const athena_namespace_definition& first,
  const athena_namespace_definition& second,
  string product_template, string composition_mode,
  string& sorter_path, string& error) {
  if (!context || !vault_context_is_current (context)) {
    error= "No active vault.";
    return false;
  }
  if (first.uuid == "" || second.uuid == "") {
    error= "Product sorter parents require stable namespace UUIDs.";
    return false;
  }
  if ((!first.sorter_trivial && first.sorter_path == "") ||
      (!second.sorter_trivial && second.sorter_path == "")) {
    error= "Both product parents need explicit or trivial sorters.";
    return false;
  }
  std::string mode= tm_to_std (composition_mode);
  if (mode != "lexicographic-first" && mode != "lexicographic-second" &&
      mode != "constraint-union") {
    error= "Product sorter requires an explicit mode: lexicographic-first, "
           "lexicographic-second, or constraint-union.";
    return false;
  }
  derivation_result first_map, second_map;
  if (!template_derivation_mapping (product_template, first.templ, false,
                                    first_map, error)) {
    if (error == "") error= "Product template does not derive from " * first.name * ".";
    return false;
  }
  if (!template_derivation_mapping (product_template, second.templ, false,
                                    second_map, error)) {
    if (error == "") error= "Product template does not derive from " * second.name * ".";
    return false;
  }
  QJsonArray parents;
  if (mode == "lexicographic-second") {
    parents.append (parent_json (second, second_map));
    parents.append (parent_json (first, first_map));
  }
  else {
    parents.append (parent_json (first, first_map));
    parents.append (parent_json (second, second_map));
  }
  QJsonObject descriptor {
    {"format", composition_format},
    {"version", 1},
    {"mode", mode == "constraint-union" ? "constraint-union" : "lexicographic"},
    {"product_template", QString::fromStdString (tm_to_std (product_template))},
    {"parents", parents}
  };
  std::string stem= "product-" + safe_file_component (tm_to_std (first.name)) +
                    "-" + safe_file_component (tm_to_std (second.name));
  return write_composition_descriptor (context, descriptor, stem, sorter_path, error);
}

bool
athena_namespace_generate_restricted_sorter (
  const athena_namespace_definition& parent,
  string product_template, string& sorter_path, string& error) {
  return athena_namespace_generate_restricted_sorter (
    vault_capture_context (), parent, product_template, sorter_path, error);
}

bool
athena_namespace_generate_restricted_sorter (
  const vault_context_handle& context,
  const athena_namespace_definition& parent,
  string product_template, string& sorter_path, string& error) {
  if (!context || !vault_context_is_current (context)) {
    error= "No active vault.";
    return false;
  }
  if (parent.uuid == "") {
    error= "Restricted sorter parent requires a stable namespace UUID.";
    return false;
  }
  if (!parent.sorter_trivial && parent.sorter_path == "") {
    error= "The semi-concrete parent needs an explicit or trivial sorter.";
    return false;
  }
  derivation_result mapping;
  if (!template_derivation_mapping (product_template, parent.templ, false,
                                    mapping, error)) {
    if (error == "") error= "Product template does not derive from " * parent.name * ".";
    return false;
  }
  QJsonObject descriptor {
    {"format", composition_format},
    {"version", 1},
    {"mode", "restricted"},
    {"product_template", QString::fromStdString (tm_to_std (product_template))},
    {"parents", QJsonArray {parent_json (parent, mapping)}}
  };
  std::string stem= "restricted-" + safe_file_component (tm_to_std (parent.name));
  return write_composition_descriptor (context, descriptor, stem, sorter_path, error);
}
