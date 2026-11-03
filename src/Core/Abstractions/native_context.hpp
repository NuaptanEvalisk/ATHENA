// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include <cassert>

// A synchronous, owner-thread stack for native handles. Restoring the parent
// precedes destruction of the retained handle, including on C++ exceptions.
// Neither the scope nor current() may cross threads.
template<typename T>
class native_context_scope {
  T value {};
  native_context_scope* previous= nullptr;
  bool installed= false;

  static native_context_scope*& head () {
    static thread_local native_context_scope* slot= nullptr;
    return slot;
  }

public:
  native_context_scope ()= default;
  native_context_scope (const native_context_scope&)= delete;
  native_context_scope& operator= (const native_context_scope&)= delete;
  native_context_scope (native_context_scope&&)= delete;
  native_context_scope& operator= (native_context_scope&&)= delete;

  ~native_context_scope () noexcept {
    if (!installed) return;
    assert (head () == this);
    head ()= previous;
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
