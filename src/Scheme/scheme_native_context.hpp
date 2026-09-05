/******************************************************************************
* MODULE     : scheme_native_context.hpp
* DESCRIPTION: Thread-owned native context across synchronous Scheme calls
* COPYRIGHT  : (C) 2026
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef SCHEME_NATIVE_CONTEXT_HPP
#define SCHEME_NATIVE_CONTEXT_HPP

#include <cassert>

// The Guile backend registers an explicit unwind handler in a non-rewindable
// dynamic-wind frame.  Both normal exit and Scheme non-local exit run cleanup.
// These functions must be called on a thread entered into the Scheme runtime.
void scheme_begin_native_scope (void (*cleanup) (void*), void* data);
void scheme_end_native_scope ();

// A native handle T must have an empty default value.  Default construction,
// assignment and destruction used by cleanup must not throw or enter Scheme.
// Handles are retained and released only on the calling thread.  Neither this
// scope nor a pointer returned by current() may be sent to another thread.
//
// Register cleanup BEFORE bind(): a Guile exit during registration then cannot
// leave a retained native handle or a pointer to an abandoned stack frame.
// Cleanup also releases the handle when Guile skips C++ stack destructors.
template<typename T>
class scheme_native_context {
  T value;
  scheme_native_context* previous;
  bool installed;

  static scheme_native_context*& head () {
    static thread_local scheme_native_context* slot= nullptr;
    return slot;
  }

  static void unwind (void* raw) noexcept {
    auto* self= static_cast<scheme_native_context*> (raw);
    if (!self->installed) return;
    assert (head () == self);
    head ()= self->previous;
    self->installed= false;
    // Do not leave native references for a destructor that a Scheme throw or
    // continuation escape can bypass.  Restore the parent before releasing T.
    self->value= T ();
  }

public:
  scheme_native_context (): value (), previous (nullptr), installed (false) {
    scheme_begin_native_scope (&unwind, this);
  }

  scheme_native_context (const scheme_native_context&)= delete;
  scheme_native_context& operator= (const scheme_native_context&)= delete;
  scheme_native_context (scheme_native_context&&)= delete;
  scheme_native_context& operator= (scheme_native_context&&)= delete;

  ~scheme_native_context () noexcept {
    // Explicit end invokes unwind as well; value is empty before its ordinary
    // C++ destructor runs.  A C++ exception also follows this path.
    scheme_end_native_scope ();
  }

  void bind (const T& next) {
    assert (!installed);
    value= next;
    previous= head ();
    head ()= this;
    installed= true;
  }

  static const T* current () noexcept {
    return head () == nullptr ? nullptr : &head ()->value;
  }
};

#endif // SCHEME_NATIVE_CONTEXT_HPP
