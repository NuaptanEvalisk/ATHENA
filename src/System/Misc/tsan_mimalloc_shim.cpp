/*
 * ThreadSanitizer must own the process-wide allocation interceptors.  ATHENA
 * still uses its mi_* call sites in sanitizer builds, but routes them through
 * the allocator observed by TSan instead of linking mimalloc's libc overrides.
 */

#include <mimalloc.h>

#include <cstdlib>
#include <limits>
#include <new>

extern "C" void*
mi_new (size_t size) {
  if (void* allocation= std::malloc (size == 0 ? 1 : size)) return allocation;
  throw std::bad_alloc ();
}

extern "C" void*
mi_new_n (size_t count, size_t size) {
  if (size != 0 && count > std::numeric_limits<size_t>::max () / size)
    throw std::bad_array_new_length ();
  return mi_new (count * size);
}

extern "C" void*
mi_new_aligned (size_t size, size_t alignment) {
  void* allocation= nullptr;
  if (alignment < sizeof (void*)) alignment= sizeof (void*);
  if (posix_memalign (&allocation, alignment, size == 0 ? 1 : size) == 0)
    return allocation;
  throw std::bad_alloc ();
}

extern "C" void
mi_free (void* allocation) noexcept {
  std::free (allocation);
}

extern "C" void
mi_process_info (size_t* elapsed_msecs, size_t* user_msecs,
                 size_t* system_msecs, size_t* current_rss, size_t* peak_rss,
                 size_t* current_commit, size_t* peak_commit,
                 size_t* page_faults) noexcept {
  size_t* outputs[]= {elapsed_msecs, user_msecs, system_msecs, current_rss,
                      peak_rss, current_commit, peak_commit, page_faults};
  for (size_t* output: outputs)
    if (output != nullptr) *output= 0;
}
