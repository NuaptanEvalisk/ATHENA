/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "device_identity.hpp"
#include "device_key_internal.hpp"
#include <libsecret/secret.h>
#include <cstring>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <algorithm>

namespace athena::hodarium::detail {
namespace {
const SecretSchema schema= {"org.athena.Hodarium.Device", SECRET_SCHEMA_NONE,
  {{"handle", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}}};
struct object_free { void operator() (gpointer p) const { if (p) g_object_unref (p); } };
template<typename T> using object= std::unique_ptr<T, object_free>;
std::mutex calls_mutex;
std::vector<GCancellable*> calls;
struct call_budget {
  object<GCancellable> cancel{g_cancellable_new ()};
  std::mutex mutex;
  std::condition_variable wake;
  bool finished= false;
  std::thread deadline;
  call_budget (): deadline ([this] {
    std::unique_lock lock (mutex);
    if (!wake.wait_for (lock, std::chrono::seconds (15), [this] { return finished; }))
      g_cancellable_cancel (cancel.get ());
  }) {
    std::lock_guard lock (calls_mutex);
    calls.push_back (cancel.get ());
  }
  ~call_budget () {
    { std::lock_guard lock (calls_mutex);
      calls.erase (std::remove (calls.begin (), calls.end (), cancel.get ()), calls.end ()); }
    { std::lock_guard lock (mutex); finished= true; }
    wake.notify_one (); deadline.join ();
  }
};
struct hash_free { void operator() (GHashTable* p) const { g_hash_table_unref (p); } };
struct list_free { void operator() (GList* p) const { g_list_free_full (p, g_object_unref); } };
struct value_free { void operator() (SecretValue* p) const { secret_value_unref (p); } };
struct error {
  GError* value= nullptr;
  ~error () { if (value) g_error_free (value); }
  void check () const {
    if (!value) return;
    if (g_error_matches (value, SECRET_ERROR, SECRET_ERROR_IS_LOCKED))
      throw key_store_error (key_store_failure::locked,
        "Unlock the system secret store to use Hodarium");
    throw key_store_error (key_store_failure::unavailable,
      "Hodarium cannot access the system Secret Service");
  }
};
object<SecretService> service (GCancellable* cancel) {
  error e;
  object<SecretService> result (secret_service_open_sync (SECRET_TYPE_SERVICE,
    "org.freedesktop.secrets", SECRET_SERVICE_OPEN_SESSION, cancel, &e.value));
  e.check ();
  if (!result) throw key_store_error (key_store_failure::unavailable,
    "No system Secret Service is available for Hodarium");
  return result;
}
std::unique_ptr<GHashTable, hash_free> attributes (const std::string& handle) {
  return std::unique_ptr<GHashTable, hash_free> (
    secret_attributes_build (&schema, "handle", handle.c_str (), nullptr));
}
}

void store_device_seed (const std::string& handle, const unsigned char* seed) {
  call_budget budget;
  auto connection= service (budget.cancel.get ());
  error e;
  object<SecretCollection> collection (secret_collection_for_alias_sync (
    connection.get (), SECRET_COLLECTION_DEFAULT, SECRET_COLLECTION_NONE,
    budget.cancel.get (), &e.value));
  e.check ();
  if (!collection) throw key_store_error (key_store_failure::unavailable,
    "Create a default system secret collection before enabling Hodarium");
  if (secret_collection_get_locked (collection.get ()))
    throw key_store_error (key_store_failure::locked,
      "Unlock the system secret collection before enabling Hodarium");
  auto attrs= attributes (handle);
  std::unique_ptr<SecretValue, value_free> value (secret_value_new (
    reinterpret_cast<const char*> (seed), 32, "application/octet-stream"));
  object<SecretItem> item (secret_item_create_sync (collection.get (), &schema,
    attrs.get (), "ATHENA Hodarium device identity", value.get (),
    SECRET_ITEM_CREATE_NONE, budget.cancel.get (), &e.value));
  e.check ();
  if (!item) throw key_store_error (key_store_failure::unavailable,
    "Could not store Hodarium identity in the system secret collection");
}
void load_device_seed (const std::string& handle, unsigned char* seed) {
  call_budget budget;
  auto connection= service (budget.cancel.get ());
  auto attrs= attributes (handle);
  error e;
  // Include locked matches but neither prompt nor automatically unlock them.
  std::unique_ptr<GList, list_free> items (secret_service_search_sync (
    connection.get (), &schema, attrs.get (), SECRET_SEARCH_ALL, budget.cancel.get (), &e.value));
  e.check ();
  if (!items) throw key_store_error (key_store_failure::missing,
    "Hodarium device key is missing; re-enrollment is required");
  if (items->next) throw key_store_error (key_store_failure::corrupt,
    "Multiple system secrets match this Hodarium identity");
  auto* item= SECRET_ITEM (items->data);
  if (secret_item_get_locked (item)) throw key_store_error (key_store_failure::locked,
    "Unlock the system secret store to use Hodarium");
  if (!secret_item_load_secret_sync (item, budget.cancel.get (), &e.value)) {
    e.check ();
    throw key_store_error (key_store_failure::unavailable, "Cannot load Hodarium identity");
  }
  std::unique_ptr<SecretValue, value_free> value (secret_item_get_secret (item));
  if (!value) throw key_store_error (key_store_failure::locked,
    "Hodarium secret is not accessible; unlock the system secret store");
  gsize size= 0;
  const auto* bytes= secret_value_get (value.get (), &size);
  if (size != 32) throw key_store_error (key_store_failure::corrupt,
    "Invalid protected Hodarium identity");
  std::memcpy (seed, bytes, size);
}
} // namespace athena::hodarium::detail

namespace athena::hodarium {
void cancel_device_key_operations () {
  std::vector<detail::object<GCancellable>> active;
  {
    std::lock_guard lock (detail::calls_mutex);
    for (auto* call: detail::calls)
      active.emplace_back (G_CANCELLABLE (g_object_ref (call)));
  }
  for (const auto& call: active) g_cancellable_cancel (call.get ());
}
} // namespace athena::hodarium
