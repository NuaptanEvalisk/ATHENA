#include <libguile.h>
#include <gc.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>

int main (int argc, char** argv) {
  if (argc != 2) return 2;
  scm_init_guile ();
  // Neither a caller nor GC_ENABLE_INCREMENTAL may restore the unsafe policy.
  GC_enable_incremental ();
  if (GC_is_incremental_mode ()) {
    std::fprintf (stderr, "Incremental GC is unsafe with native plugin unloading\n");
    return 1;
  }
  for (int iteration= 0; iteration < 32; ++iteration) {
    void* library= dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { std::fprintf (stderr, "%s\n", dlerror ()); return 1; }
    auto store= reinterpret_cast<void (*) (void*)> (dlsym (library, "store_root"));
    auto read= reinterpret_cast<void* (*) ()> (dlsym (library, "read_root"));
    if (!store || !read) return 1;
    auto value= static_cast<char*> (GC_malloc (64));
    std::strcpy (value, "native plugin root");
    store (value);
    GC_gcollect ();
    if (std::strcmp (static_cast<char*> (read ()), "native plugin root")) return 1;
    GC_start_incremental_collection ();
    GC_collect_a_little ();
    if (dlclose (library) != 0) return 1;
    GC_gcollect ();
    for (int allocation= 0; allocation < 4096; ++allocation)
      scm_c_make_bytevector (4096);
  }
  std::puts ("PASS: native plugin roots remain valid and unloading leaves no stale GC work");
}
