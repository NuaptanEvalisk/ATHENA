
/******************************************************************************
* MODULE     : string.hpp
* DESCRIPTION: Inline/COW byte strings with mimalloc storage.
*              Zero characters are allowed in strings.
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef STRING_H
#define STRING_H
#include "basic.hpp"
#include <atomic>
#include <cstddef>
#include <mimalloc.h>
#include <string>
#include <utility>

class string {
public:
  using storage_type=
    std::basic_string<char, std::char_traits<char>, mi_stl_allocator<char>>;

private:
  // Keep the object at 32 bytes while covering the short tokens which dominate
  // typesetting. Longer values share immutable standard-string storage.
  static constexpr std::size_t inline_capacity= 22;

  struct shared_storage {
    std::atomic<unsigned int> refs;
    storage_type value;

    explicit shared_storage (storage_type&& initial):
      refs (1), value (std::move (initial)) {}
  };

  shared_storage* rep;
  unsigned char inline_size;
  char inline_data[inline_capacity + 1];

  static shared_storage* make_rep (storage_type initial);
  static void retain (shared_storage* storage) noexcept;
  static void release (shared_storage* storage) noexcept;
  void assign_inline (const char* source, std::size_t size) noexcept;
  storage_type& writable_heap ();

public:
  string () noexcept: rep (nullptr), inline_size (0), inline_data {} {}
  string (const string& other) noexcept;
  string (string&& other) noexcept;
  string& operator = (const string& other) noexcept;
  string& operator = (string&& other) noexcept;
  ~string ();

  string (int n);
  string (char c);
  string (char c, int n);
  string (const char *s);
  string (const char *s, int n);

  inline char operator [] (int i) const noexcept {
    return data ()[i]; }
  inline const char* data () const noexcept {
    return rep == nullptr ? inline_data : rep->value.data (); }
  inline const char* c_str () const noexcept {
    return rep == nullptr ? inline_data : rep->value.c_str (); }
  inline int size () const noexcept {
    return rep == nullptr ? static_cast<int> (inline_size) :
      static_cast<int> (rep->value.size ()); }
  inline bool empty () const noexcept {
    return rep == nullptr ? inline_size == 0 : rep->value.empty (); }

  char* mutable_data ();
  void set (int i, char c);
  void resize (int n);
  void reserve (int n);
  void append (char c);
  void append (const string& s);

  bool operator == (const char* s) const;
  bool operator != (const char* s) const;
  bool operator == (const string& s) const;
  bool operator != (const string& s) const;
  string operator () (int start, int end) const;
};

inline int N (const string& a) { return a.size (); }
string   copy (const string& a);
tm_ostream& operator << (tm_ostream& out, const string& a);
string&  operator << (string& a, char);
string&  operator << (string& a, const string& b);
string   operator * (const char* a, const string& b);
string   operator * (const string& a, const string& b);
string   operator * (const string& a, const char* b);
bool     operator < (const string& a, const string& b);
bool     operator <= (const string& a, const string& b);
int      hash (const string& s);

bool     as_bool   (const string& s);
int      as_int    (const string& s);
long int as_long_int (const string& s);
double   as_double (const string& s);
char*    as_charp  (const string& s);
string   as_string_bool (bool f);
string   as_string (int i);
string   as_string (unsigned int i);
string   as_string (long int i);
string   as_string (long long int i);
string   as_string (unsigned long int i);
string   as_string (double x);
string   as_string (const char* s);
string   as_string (const unsigned char* s);
bool     is_empty  (const string& s);
bool     is_bool   (const string& s);
bool     is_int    (const string& s);
bool     is_double (const string& s);
bool     is_charp  (const string& s);

bool  is_quoted (const string& s);
bool  is_id     (const string& s);

void  set_wait_handler (void (*) (string, string, int));
void  system_wait (const string& message, const string& argument= "", int level= 0);

template<typename C> inline string
print_to_string (C x) {
  string buf;
  tm_ostream out= string_ostream (buf);
  out << x;
  return buf;
}

/******************************************************************************
* C-style strings with automatic memory management
******************************************************************************/

class c_string;
class c_string_rep: concrete_struct {
  char* value;
  
private:
  inline c_string_rep (c_string_rep &): concrete_struct () {}
    // disable copy constructor
  inline c_string_rep& operator=(c_string_rep&) { return *this; }
    // disable assignment
  
public:
  inline c_string_rep (char* v = NULL): value (v) {}
  inline ~c_string_rep () { if (value != NULL) tm_delete_array (value); }
  friend class c_string;
};

class c_string {
  CONCRETE(c_string);
public:
  inline c_string ():
    rep (tm_new<c_string_rep> ()) {}
  inline c_string (int len):
    rep (tm_new<c_string_rep> (tm_new_array<char> (len))) {}
  inline c_string (const string& s):
    rep (tm_new<c_string_rep> (as_charp (s))) {}
  inline operator char* () const { return rep->value; }
};
CONCRETE_CODE(c_string);

#endif // defined STRING_H
