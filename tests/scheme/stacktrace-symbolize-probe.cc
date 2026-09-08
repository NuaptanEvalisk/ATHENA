#include "System/Misc/stacktrace_symbolize.hpp"
#include <execinfo.h>
#include <iostream>

#ifdef PROBE_LIBRARY
extern "C" void library_frame () {
  void* frames[32];
  int count= backtrace (frames, 32);
  auto descriptions= athena_symbolize_stack (frames, count);
  for (const auto& description: descriptions)
    if (!description.empty ()) std::cout << description << '\n';
  void* unknown= reinterpret_cast<void*> (1);
  if (!athena_symbolize_stack (&unknown, 1)[0].empty ()) std::abort ();
}
#else
extern "C" void library_frame ();
int main () { library_frame (); }
#endif
