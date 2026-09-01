
/******************************************************************************
* MODULE     : fast_alloc.hpp
* DESCRIPTION: see fast_alloc.cpp
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef FAST_ALLOC_H
#define FAST_ALLOC_H

#include "config.h"
#include "tm_configure.hpp"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mimalloc.h>
#include <new>
#include <type_traits>

#include "tm_ostream.hpp"

/******************************************************************************
* General purpose allocation routines
******************************************************************************/

extern void* fast_new (size_t s);
extern void  fast_delete (void* ptr);

extern int   mem_used ();
extern void  mem_info ();

/******************************************************************************
* Fast new and delete
******************************************************************************/

#ifndef NO_FAST_ALLOC

#ifdef OLD_GNU_COMPILER
inline void* operator new   (size_t s, void* loc) { return loc; }
inline void* operator new[] (size_t s, void* loc) { return loc; }
#else
#include <new>
#endif

template<typename C> inline C*
tm_new () {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C ();
  return (C*) ptr;
}

template<typename C, typename A1> inline C*
tm_new (const A1& a1) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2> inline C*
tm_new (const A1& a1, const A2& a2) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2> inline C*
tm_new (const A1& a1, A2& a2) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2> inline C*
tm_new (A1& a1, const A2& a2) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2> inline C*
tm_new (A1& a1, A2& a2) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3> inline C*
tm_new (const A1& a1, A2& a2, A3& a3) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3> inline C*
tm_new (A1& a1, A2& a2, const A3& a3) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3> inline C*
tm_new (A1& a1, A2& a2, A3& a3) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3, a4);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4> inline C*
tm_new (const A1& a1, A2& a2, A3& a3, A4& a4) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3, a4);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3, a4, a5);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5> inline C*
tm_new (const A1& a1, A2& a2, A3& a3, A4& a4, A5& a5) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3, a4, a5);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5> inline C*
tm_new (A1& a1, A2& a2, A3& a3, A4& a4, A5& a5) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3, a4, a5);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5, const A6& a6) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3, a4, a5, a6);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6> inline C*
tm_new (A1& a1, const A2& a2, A3& a3, A4& a4, A5& a5, A6& a6) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3, a4, a5, a6);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6, typename A7> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5, const A6& a6,
	const A7& a7) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3, a4, a5, a6, a7);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6, typename A7> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	A4& a4, const A5& a5, const A6& a6,
	const A7& a7) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3, a4, a5, a6, a7);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6,
	 typename A7, typename A8> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5, const A6& a6,
	const A7& a7, const A8& a8) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3, a4, a5, a6, a7, a8);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6,
         typename A7, typename A8> inline C*
tm_new (A1& a1, const A2& a2, A3& a3, A4& a4, A5& a5, A6& a6, A7& a7, A8& a8) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3, a4, a5, a6, a7, a8);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6,
	 typename A7, typename A8, typename A9> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5, const A6& a6,
	const A7& a7, const A8& a8, const A9& a9) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3, a4, a5, a6, a7, a8, a9);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6,
	 typename A7, typename A8, typename A9,
	 typename A10> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5, const A6& a6,
	const A7& a7, const A8& a8, const A9& a9,
	const A10& a10) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6,
	 typename A7, typename A8, typename A9,
	 typename A10, typename A11> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5, const A6& a6,
	const A7& a7, const A8& a8, const A9& a9,
	const A10& a10, const A11& a11) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6,
	 typename A7, typename A8, typename A9,
	 typename A10, typename A11, typename A12> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5, const A6& a6,
	const A7& a7, const A8& a8, const A9& a9,
	const A10& a10, const A11& a11, const A12& a12) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6,
	 typename A7, typename A8, typename A9,
	 typename A10, typename A11, typename A12,
	 typename A13> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5, const A6& a6,
	const A7& a7, const A8& a8, const A9& a9,
	const A10& a10, const A11& a11, const A12& a12,
	const A13& a13) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3, a4, a5, a6, a7, a8, a9, a10,
		      a11, a12, a13);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6,
	 typename A7, typename A8, typename A9,
	 typename A10, typename A11, typename A12,
	 typename A13, typename A14> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5, const A6& a6,
	const A7& a7, const A8& a8, const A9& a9,
	const A10& a10, const A11& a11, const A12& a12,
	const A13& a13, const A14& a14) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3, a4, a5, a6, a7, a8, a9, a10,
		      a11, a12, a13, a14);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6,
	 typename A7, typename A8, typename A9,
	 typename A10, typename A11, typename A12,
	 typename A13, typename A14, typename A15> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5, const A6& a6,
	const A7& a7, const A8& a8, const A9& a9,
	const A10& a10, const A11& a11, const A12& a12,
	const A13& a13, const A14& a14, const A15& a15) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3, a4, a5, a6, a7, a8, a9, a10,
		      a11, a12, a13, a14, a15);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6,
	 typename A7, typename A8, typename A9,
	 typename A10, typename A11, typename A12,
	 typename A13, typename A14, typename A15,
	 typename A16, typename A17> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5, const A6& a6,
	const A7& a7, const A8& a8, const A9& a9,
	const A10& a10, const A11& a11, const A12& a12,
	const A13& a13, const A14& a14, const A15& a15,
	const A16& a16, const A17& a17) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3, a4, a5, a6, a7, a8, a9, a10,
		      a11, a12, a13, a14, a15, a16, a17);
  return (C*) ptr;
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6,
	 typename A7, typename A8, typename A9,
	 typename A10, typename A11, typename A12,
	 typename A13, typename A14, typename A15,
	 typename A16, typename A17, typename A18,
         typename A19, typename A20, typename A21> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5, const A6& a6,
	const A7& a7, const A8& a8, const A9& a9,
	const A10& a10, const A11& a11, const A12& a12,
	const A13& a13, const A14& a14, const A15& a15,
	const A16& a16, const A17& a17, const A18& a18,
        const A19& a19, const A20& a20, const A21& a21) {
  void* ptr= fast_new (sizeof (C));
  (void) new (ptr) C (a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12,
                      a13, a14, a15, a16, a17, a18, a19, a20, a21);
  return (C*) ptr;
}

template<typename C> inline void
tm_delete (C* ptr) {
  ptr -> ~C ();
  fast_delete ((void*) ptr);
}

namespace tm_memory_detail {

struct array_header {
  void* allocation;
  int count;
#ifdef DEBUG_ON
  int count_copy;
  int count_complement;
#endif
};

constexpr uint32_t array_guard= 0x55AA55AAu;

template<typename C> constexpr size_t
array_alignment () {
  return alignof (C) > alignof (array_header)
           ? alignof (C)
           : alignof (array_header);
}

template<typename C> inline array_header*
array_header_for (C* ptr) {
  return reinterpret_cast<array_header*> (ptr) - 1;
}

} // namespace tm_memory_detail

template<typename C> inline C*
tm_new_array (int n) {
  using namespace tm_memory_detail;
  if (n < 0) throw std::bad_array_new_length ();

  constexpr size_t alignment= array_alignment<C> ();
  constexpr size_t guard_size=
#ifdef DEBUG_ON
    sizeof (array_guard);
#else
    0;
#endif
  const size_t count= static_cast<size_t> (n);
  const size_t overhead= sizeof (array_header) + alignment - 1 + guard_size;
  if (count > (std::numeric_limits<size_t>::max () - overhead) / sizeof (C))
    throw std::bad_array_new_length ();

  const size_t payload_size= count * sizeof (C);
  void* allocation= mi_new_aligned (overhead + payload_size, alignment);
  uintptr_t start= reinterpret_cast<uintptr_t> (allocation) +
                   sizeof (array_header);
  uintptr_t aligned= (start + alignment - 1) & ~(alignment - 1);
  C* result= reinterpret_cast<C*> (aligned);
  array_header* header= array_header_for (result);
  header->allocation= allocation;
  header->count= n;
#ifdef DEBUG_ON
  header->count_copy= n;
  header->count_complement= ~n;
  std::memcpy (reinterpret_cast<char*> (result) + payload_size,
               &array_guard, sizeof (array_guard));
#endif

  if constexpr (std::is_trivial_v<C>) {
    std::memset (result, 0, payload_size);
  }
  else {
    int constructed= 0;
    try {
      for (; constructed < n; ++constructed)
        (void) new (static_cast<void*> (result + constructed)) C ();
    }
    catch (...) {
      while (constructed > 0) result[--constructed].~C ();
      mi_free (allocation);
      throw;
    }
  }
  return result;
}

template<typename C> inline void
tm_delete_array (C* ptr) {
  using namespace tm_memory_detail;
  if (ptr == nullptr) return;
  array_header* header= array_header_for (ptr);
  const int n= header->count;
#ifdef DEBUG_ON
  if (header->count_copy != n || header->count_complement != ~n)
    printf ("tm_delete_array size metadata mismatch\n");
  uint32_t guard= 0;
  std::memcpy (&guard, reinterpret_cast<char*> (ptr) +
                      static_cast<size_t> (n) * sizeof (C), sizeof (guard));
  if (guard != array_guard)
    printf ("tm_delete_array buffer overflow\n");
#endif
  if constexpr (!std::is_trivially_destructible_v<C>)
    for (int i= n; i > 0; --i) ptr[i - 1].~C ();
  mi_free (header->allocation);
}


#endif // !defined(NO_FAST_ALLOC)

/******************************************************************************
* Slow new and delete
******************************************************************************/

#ifdef NO_FAST_ALLOC

#ifndef NO_FAST_ALLOC
#ifdef OS_IRIX
void* operator new (size_t s) throw(std::bad_alloc);
void  operator delete (void* ptr) throw();
void* operator new[] (size_t s) throw(std::bad_alloc);
void  operator delete[] (void* ptr) throw();
#else
void* operator new (size_t s);
void  operator delete (void* ptr);
void* operator new[] (size_t s);
void  operator delete[] (void* ptr);
#endif
#endif // not defined NO_FAST_ALLOC

template<typename C> inline C*
tm_new () {
  return new C ();
}

template<typename C, typename A1> inline C*
tm_new (const A1& a1) {
  return new C (a1);
}

template<typename C, typename A1, typename A2> inline C*
tm_new (const A1& a1, const A2& a2) {
  return new C (a1, a2);
}

template<typename C, typename A1, typename A2> inline C*
tm_new (const A1& a1, A2& a2) {
  return new C (a1, a2);
}

template<typename C, typename A1, typename A2> inline C*
tm_new (A1& a1, const A2& a2) {
  return new C (a1, a2);
}

template<typename C, typename A1, typename A2> inline C*
tm_new (A1& a1, A2& a2) {
  return new C (a1, a2);
}

template<typename C, typename A1, typename A2, typename A3> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3) {
  return new C (a1, a2, a3);
}

template<typename C, typename A1, typename A2, typename A3> inline C*
tm_new (const A1& a1, A2& a2, A3& a3) {
  return new C (a1, a2, a3);
}

template<typename C, typename A1, typename A2, typename A3> inline C*
tm_new (A1& a1, A2& a2, const A3& a3) {
  return new C (a1, a2, a3);
}

template<typename C, typename A1, typename A2, typename A3> inline C*
tm_new (A1& a1, A2& a2, A3& a3) {
  return new C (a1, a2, a3);
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4) {
  return new C (a1, a2, a3, a4);
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4> inline C*
tm_new (const A1& a1, A2& a2, A3& a3, A4& a4) {
  return new C (a1, a2, a3, a4);
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5) {
  return new C (a1, a2, a3, a4, a5);
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5> inline C*
tm_new (const A1& a1, A2& a2, A3& a3, A4& a4, A5& a5) {
  return new C (a1, a2, a3, a4, a5);
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5> inline C*
tm_new (A1& a1, A2& a2, A3& a3, A4& a4, A5& a5) {
  return new C (a1, a2, a3, a4, a5);
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5, const A6& a6) {
  return new C (a1, a2, a3, a4, a5, a6);
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6> inline C*
tm_new (A1& a1, const A2& a2, A3& a3, A4& a4, A5& a5, A6& a6) {
  return new C (a1, a2, a3, a4, a5, a6);
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6, typename A7> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5, const A6& a6,
	const A7& a7) {
  return new C (a1, a2, a3, a4, a5, a6, a7);
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6, typename A7> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	A4& a4, const A5& a5, const A6& a6,
	const A7& a7) {
  return new C (a1, a2, a3, a4, a5, a6, a7);
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6,
	 typename A7, typename A8> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5, const A6& a6,
	const A7& a7, const A8& a8) {
  return new C (a1, a2, a3, a4, a5, a6, a7, a8);
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6,
	 typename A7, typename A8> inline C*
tm_new (A1& a1, const A2& a2, A3& a3, A4& a4, A5& a5, A6& a6, A7& a7, A8& a8) {
  return new C (a1, a2, a3, a4, a5, a6, a7, a8);
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6,
	 typename A7, typename A8, typename A9> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5, const A6& a6,
	const A7& a7, const A8& a8, const A9& a9) {
  return new C (a1, a2, a3, a4, a5, a6, a7, a8, a9);
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6,
	 typename A7, typename A8, typename A9,
	 typename A10> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5, const A6& a6,
	const A7& a7, const A8& a8, const A9& a9,
	const A10& a10) {
  return new C (a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6,
	 typename A7, typename A8, typename A9,
	 typename A10, typename A11> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5, const A6& a6,
	const A7& a7, const A8& a8, const A9& a9,
	const A10& a10, const A11& a11) {
  return new C (a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6,
	 typename A7, typename A8, typename A9,
	 typename A10, typename A11, typename A12> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5, const A6& a6,
	const A7& a7, const A8& a8, const A9& a9,
	const A10& a10, const A11& a11, const A12& a12) {
  return new C (a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6,
	 typename A7, typename A8, typename A9,
	 typename A10, typename A11, typename A12,
	 typename A13> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5, const A6& a6,
	const A7& a7, const A8& a8, const A9& a9,
	const A10& a10, const A11& a11, const A12& a12,
	const A13& a13) {
  return new C (a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13);
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6,
	 typename A7, typename A8, typename A9,
	 typename A10, typename A11, typename A12,
	 typename A13, typename A14> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5, const A6& a6,
	const A7& a7, const A8& a8, const A9& a9,
	const A10& a10, const A11& a11, const A12& a12,
	const A13& a13, const A14& a14) {
  return new C (a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14);
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6,
	 typename A7, typename A8, typename A9,
	 typename A10, typename A11, typename A12,
	 typename A13, typename A14, typename A15> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5, const A6& a6,
	const A7& a7, const A8& a8, const A9& a9,
	const A10& a10, const A11& a11, const A12& a12,
	const A13& a13, const A14& a14, const A15& a15) {
  return new C (a1, a2, a3, a4, a5, a6, a7, a8, a9,
                a10, a11, a12, a13, a14, a15);
}

template<typename C, typename A1, typename A2, typename A3,
	 typename A4, typename A5, typename A6,
	 typename A7, typename A8, typename A9,
	 typename A10, typename A11, typename A12,
	 typename A13, typename A14, typename A15,
         typename A16, typename A17, typename A18,
         typename A19, typename A20, typename A21> inline C*
tm_new (const A1& a1, const A2& a2, const A3& a3,
	const A4& a4, const A5& a5, const A6& a6,
	const A7& a7, const A8& a8, const A9& a9,
	const A10& a10, const A11& a11, const A12& a12,
	const A13& a13, const A14& a14, const A15& a15,
        const A16& a16, const A17& a17, const A18& a18,
        const A19& a19, const A20& a20, const A21& a21) {
  return new C (a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12,
                a13, a14, a15, a16, a17, a18, a19, a20, a21);
}

template<typename C> inline void
tm_delete (C* ptr) {
  delete ptr;
}

template<typename C> inline C*
tm_new_array (int n) {
  return new C[n];
}

template<typename C> inline void
tm_delete_array (C* Ptr) {
  delete[] Ptr;
}

#endif // defined(NO_FAST_ALLOC)

#endif // defined FAST_ALLOC_H
