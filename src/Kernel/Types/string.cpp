
/******************************************************************************
* MODULE     : string.cpp
* DESCRIPTION: Inline/COW byte strings with standard storage.
*              Zero characters can be part of string.
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "string.hpp"
#include <cstring>
#include <stdio.h>
#include <stdexcept>

/******************************************************************************
* Low level routines and constructors
******************************************************************************/

static string::storage_type::size_type
string_size (int n) {
  if (n < 0) throw std::length_error ("negative ATHENA string size");
  return static_cast<string::storage_type::size_type> (n);
}

string::shared_storage*
string::make_rep (storage_type initial) {
  void* raw= fast_new (sizeof (shared_storage));
  try {
    return new (raw) shared_storage (std::move (initial));
  }
  catch (...) {
    fast_delete (raw);
    throw;
  }
}

void
string::retain (shared_storage* storage) noexcept {
  if (storage != nullptr)
    storage->refs.fetch_add (1, std::memory_order_relaxed);
}

void
string::release (shared_storage* storage) noexcept {
  if (storage != nullptr &&
      storage->refs.fetch_sub (1, std::memory_order_release) == 1) {
    std::atomic_thread_fence (std::memory_order_acquire);
    storage->~shared_storage ();
    fast_delete (storage);
  }
}

void
string::assign_inline (const char* source, std::size_t size) noexcept {
  rep= nullptr;
  inline_size= static_cast<unsigned char> (size);
  if (size != 0) std::memcpy (inline_data, source, size);
  inline_data[size]= '\0';
}

string::storage_type&
string::writable_heap () {
  if (rep == nullptr) {
    storage_type initial (inline_data, inline_size);
    rep= make_rep (std::move (initial));
    inline_size= 0;
    inline_data[0]= '\0';
  }
  if (rep->refs.load (std::memory_order_acquire) != 1) {
    shared_storage* previous= rep;
    rep= make_rep (previous->value);
    release (previous);
  }
  return rep->value;
}

string::string (const string& other) noexcept:
  rep (nullptr), inline_size (0), inline_data {} {
  if (other.rep == nullptr)
    assign_inline (other.inline_data, other.inline_size);
  else {
    rep= other.rep;
    retain (rep);
  }
}

string::string (string&& other) noexcept:
  rep (nullptr), inline_size (0), inline_data {} {
  if (other.rep == nullptr)
    assign_inline (other.inline_data, other.inline_size);
  else {
    rep= other.rep;
    other.rep= nullptr;
  }
  other.inline_size= 0;
  other.inline_data[0]= '\0';
}

string&
string::operator = (const string& other) noexcept {
  if (this == &other) return *this;
  if (other.rep == nullptr) {
    release (rep);
    assign_inline (other.inline_data, other.inline_size);
  }
  else if (rep != other.rep) {
    retain (other.rep);
    release (rep);
    rep= other.rep;
    inline_size= 0;
    inline_data[0]= '\0';
  }
  return *this;
}

string&
string::operator = (string&& other) noexcept {
  if (this != &other) {
    release (rep);
    if (other.rep == nullptr) {
      assign_inline (other.inline_data, other.inline_size);
      other.inline_size= 0;
      other.inline_data[0]= '\0';
    }
    else {
      rep= other.rep;
      inline_size= 0;
      inline_data[0]= '\0';
      other.rep= nullptr;
      other.inline_size= 0;
      other.inline_data[0]= '\0';
    }
  }
  return *this;
}

string::~string () {
  release (rep);
}

string::string (int n): rep (nullptr), inline_size (0), inline_data {} {
  storage_type::size_type size= string_size (n);
  if (size <= inline_capacity) {
    inline_size= static_cast<unsigned char> (size);
    inline_data[size]= '\0';
  }
  else rep= make_rep (storage_type (size, '\0'));
}

string::string (char c): string (c, 1) {}

string::string (char c, int n):
  rep (nullptr), inline_size (0), inline_data {} {
  storage_type::size_type size= string_size (n);
  if (size <= inline_capacity) {
    inline_size= static_cast<unsigned char> (size);
    if (size != 0) std::memset (inline_data, c, size);
    inline_data[size]= '\0';
  }
  else rep= make_rep (storage_type (size, c));
}

string::string (const char* s):
  rep (nullptr), inline_size (0), inline_data {} {
  std::size_t size= std::strlen (s);
  if (size <= inline_capacity) assign_inline (s, size);
  else rep= make_rep (storage_type (s, size));
}

string::string (const char* s, int n):
  rep (nullptr), inline_size (0), inline_data {} {
  storage_type::size_type size= string_size (n);
  if (size <= inline_capacity) assign_inline (s, size);
  else rep= make_rep (storage_type (s, size));
}

string
string::transferable (int n) {
  storage_type::size_type size= string_size (n);
  string result;
  result.rep= make_rep (storage_type (size, '\0'));
  return result;
}

void
string::ensure_transferable () {
  if (rep == nullptr) (void) writable_heap ();
}

char*
string::mutable_data () {
  return rep == nullptr ? inline_data : writable_heap ().data ();
}

void
string::set (int i, char c) {
  if (rep == nullptr) inline_data[i]= c;
  else writable_heap ()[static_cast<storage_type::size_type> (i)]= c;
}

void
string::resize (int n) {
  storage_type::size_type size= string_size (n);
  if (rep == nullptr) {
    if (size <= inline_capacity) {
      if (size > inline_size)
        std::memset (inline_data + inline_size, '\0', size - inline_size);
      inline_size= static_cast<unsigned char> (size);
      inline_data[size]= '\0';
    }
    else {
      storage_type initial (inline_data, inline_size);
      initial.resize (size);
      rep= make_rep (std::move (initial));
      inline_size= 0;
      inline_data[0]= '\0';
    }
  }
  else if (size <= inline_capacity) {
    shared_storage* previous= rep;
    std::size_t copied=
      size < previous->value.size () ? size : previous->value.size ();
    if (copied != 0)
      std::memcpy (inline_data, previous->value.data (), copied);
    if (size > copied)
      std::memset (inline_data + copied, '\0', size - copied);
    rep= nullptr;
    inline_size= static_cast<unsigned char> (size);
    inline_data[size]= '\0';
    release (previous);
  }
  else writable_heap ().resize (size);
}

void
string::reserve (int n) {
  storage_type::size_type size= string_size (n);
  if (rep == nullptr && size <= inline_capacity) return;
  writable_heap ().reserve (size);
}

void
string::append (char c) {
  if (rep == nullptr && inline_size < inline_capacity) {
    inline_data[inline_size++]= c;
    inline_data[inline_size]= '\0';
  }
  else writable_heap ().push_back (c);
}

void
string::append (const string& s) {
  if (s.empty ()) return;
  storage_type snapshot;
  const char* source= s.data ();
  std::size_t source_size= static_cast<std::size_t> (s.size ());
  if (this == &s) {
    snapshot.assign (source, source_size);
    source= snapshot.data ();
  }
  std::size_t combined= static_cast<std::size_t> (size ()) + source_size;
  if (rep == nullptr && combined <= inline_capacity) {
    std::memcpy (inline_data + inline_size, source, source_size);
    inline_size= static_cast<unsigned char> (combined);
    inline_data[inline_size]= '\0';
  }
  else writable_heap ().append (source, source_size);
}

/******************************************************************************
* Common routines for strings
******************************************************************************/

bool
string::operator == (const char* s) const {
  std::size_t other_size= std::strlen (s);
  return static_cast<std::size_t> (size ()) == other_size &&
    (other_size == 0 || std::memcmp (data (), s, other_size) == 0);
}

bool
string::operator != (const char* s) const {
  return !(*this == s);
}

bool
string::operator == (const string& a) const {
  if (rep != nullptr && rep == a.rep) return true;
  int length= size ();
  return length == a.size () &&
    (length == 0 || std::memcmp (data (), a.data (), length) == 0);
}

bool
string::operator != (const string& a) const {
  return !(*this == a);
}

string
string::operator () (int begin, int end) const {
  if (end <= begin) return string();

  begin = max(min(N (*this), begin), 0);
  end = max(min(N (*this), end), 0);
  int n= end-begin;
  return n > 0 ? string (data () + begin, n) : string ();
}

string
copy (const string& s) {
  return N(s) == 0 ? string () : string (s.data (), N(s));
}

string&
operator << (string& a, char x) {
  a.append (x);
  return a;
}

string&
operator << (string& a, const string& b) {
  a.append (b);
  return a;
}

string
operator * (const string& a, const string& b) {
  string c;
  c.reserve (N(a) + N(b));
  c.append (a);
  c.append (b);
  return c;
}

string
operator * (const char* a, const string& b) {
  return string (a) * b;
}

string
operator * (const string& a, const char* b) {
  return a * string (b);
}

bool
operator < (const string& s1, const string& s2) {
  int i;
  for (i=0; i<N(s1); i++) {
    if (i>=N(s2)) return false;
    if (s1[i]<s2[i]) return true;
    if (s2[i]<s1[i]) return false;
  }
  return i<N(s2);
}

bool
operator <= (const string& s1, const string& s2) {
  int i;
  for (i=0; i<N(s1); i++) {
    if (i>=N(s2)) return false;
    if (s1[i]<s2[i]) return true;
    if (s2[i]<s1[i]) return false;
  }
  return true;
}

tm_ostream&
operator << (tm_ostream& out, const string& a) {
  out->write (a.data (), N(a));
  return out;
}

int
hash (const string& s) {
  int i, h=0, n=N(s);
  for (i=0; i<n; i++) {
    h=(h<<9)+(h>>23);
    h=h+((int) s[i]);
  }
  return h;
}

/******************************************************************************
* Conversion routines
******************************************************************************/

bool
as_bool (const string& s) {
  return (s == "true" || s == "#t");
}

int
as_int (const string& s) {
  int i=0, n=N(s), val=0;
  if (n==0) return 0;
  if (s[0]=='-') i++;
  while (i<n) {
    if (s[i]<'0') break;
    if (s[i]>'9') break;
    val *= 10;
    val += (int) (s[i]-'0');
    i++;
  }
  if (s[0]=='-') val=-val;
  return val;
}


long int
as_long_int (const string& s) {
  int i=0, n=N(s);
  long int val=0;
  if (n==0) return 0;
  if (s[0]=='-') i++;
  while (i<n) {
    if (s[i]<'0') break;
    if (s[i]>'9') break;
    val *= 10;
    val += (int) (s[i]-'0');
    i++;
  }
  if (s[0]=='-') val=-val;
  return val;
}

double
as_double (const string& s) {
  double x= 0.0;
  {
    int i, n= N(s);
    STACK_NEW_ARRAY (buf, char, n+1);
    for (i=0; i<n; i++) buf[i]=s[i];
    buf[n]='\0';
    sscanf (buf, "%lf", &x);
    STACK_DELETE_ARRAY (buf);
  } // in order to avoid segmentation fault due to compiler bug
  return x;
}

char*
as_charp (const string& s) {
  int i, n= N(s);
  char *s2= tm_new_array<char> (n+1);
  for (i=0; i<n; i++) s2[i]=s[i];
  s2[n]= '\0';
  return s2;
}

string
as_string_bool (bool f) {
  if (f) return string ("true");
  else return string ("false");
}

string
as_string (int i) {
  char buf[64];
#ifdef HAVE_SNPRINTF
  snprintf (buf, 64, "%i", i);
#else
  sprintf (buf, "%i", i);
#endif
  // sprintf (buf, "%i\0", i);
  return string (buf);
}

string
as_string (unsigned int i) {
  char buf[64];
#ifdef HAVE_SNPRINTF
  snprintf (buf, 64, "%u", i);
#else
  sprintf (buf, "%u", i);
#endif
  // sprintf (buf, "%u\0", i);
  return string (buf);
}

string
as_string (long int i) {
  char buf[64];
#ifdef HAVE_SNPRINTF
  snprintf (buf, 64, "%li", i);
#else
  sprintf (buf, "%li", i);
#endif
  // sprintf (buf, "%li\0", i);
  return string (buf);
}

string
as_string (long long int i) {
  char buf[64];
#ifdef HAVE_SNPRINTF  
#  ifdef OS_MINGW
  snprintf (buf, 64, "%I64d", i);
#  else
  snprintf (buf, 64, "%lli", i);
#  endif
#else
#  ifdef OS_MINGW
  sprintf (buf, "%I64d", i);
#  else
  sprintf (buf, "%lli", i);
#  endif
#endif
  // sprintf (buf, "%lli\0", i);
  return string (buf);
}

string
as_string (unsigned long int i) {
  char buf[64];
#ifdef HAVE_SNPRINTF    
  snprintf (buf, 64, "%lu", i);
#else
  sprintf (buf, "%lu", i);
#endif
  // sprintf (buf, "%lu\0", i);
  return string (buf);
}

string
as_string (double x) {
  char buf[64];
#ifdef HAVE_SNPRINTF    
  snprintf (buf, 64, "%g", x);
#else
  sprintf (buf, "%g", x);
#endif
  // sprintf (buf, "%g\0", x);
  return string(buf);
}

string
as_string (const char* s) {
  return string (s);
}

string
as_string (const unsigned char* s) {
    return string(reinterpret_cast<const char*>(s));
}

bool
is_empty (const string& s) {
  return N(s) == 0;
}

bool
is_bool (const string& s) {
  return (s == "true") || (s == "false");
}

bool
is_int (const string& s) {
  int i=0, n=N(s);
  if (n==0) return false;
  if (s[i]=='+') i++;
  if (s[i]=='-') i++;
  if (i==n) return false;
  for (; i<n; i++)
    if ((s[i]<'0') || (s[i]>'9')) return false;
  return true;
}

bool
is_double (const string& s) {
  int i=0, n=N(s);
  if (n==0) return false;
  if (s[i]=='+') i++;
  if (s[i]=='-') i++;
  if (i==n) return false;
  for (; i< n; i++)
    if ((s[i]<'0') || (s[i]>'9')) break;
  if (i==n) return true;
  if (s[i]=='.') {
    i++;
    if (i==n) return false;
    for (; i< n; i++)
      if ((s[i]<'0') || (s[i]>'9')) break;
  }
  if (i==n) return true;
  if (s[i++]!='e') return false;
  if (s[i]=='+') i++;
  if (s[i]=='-') i++;
  if (i==n) return false;
  for (; i< n; i++)
    if ((s[i]<'0') || (s[i]>'9')) return false;
  return true;
}

bool
is_charp (const string& s) { (void) s;
  return true;
}

bool
is_quoted (const string& s) {
  int n=N(s);
  return (n>=2) && (s[0]=='\"') && (s[n-1]=='\"');
}

bool
is_id (const string& s) {
  int i=0, n=N(s);
  if (n==0) return false;
  for (i=0; i< n; i++) {
    if ((i>0) && (s[i]>='0') && (s[i]<='9')) continue;
    if ((s[i]>='a') && (s[i]<='z')) continue;
    if ((s[i]>='A') && (s[i]<='Z')) continue;
    if (s[i]=='_') continue;
    return false;
  }
  return true;
}

/******************************************************************************
* Error messages
******************************************************************************/

static void (*the_wait_handler) (string, string, int) = NULL;

void
set_wait_handler (void (*routine) (string, string, int)) {
  the_wait_handler= routine; }

void
system_wait (const string& message, const string& argument, int level) {
  if (the_wait_handler == NULL) {
    if (DEBUG_AUTO) {
      if (message == "") cout << "ATHENA] Done" << LF;
      else {
	if (argument == "") cout << "ATHENA] " << message << LF;
	else cout << "ATHENA] " << message << " " << argument << LF;
	cout << "ATHENA] Please wait..." << LF;
      }
    }
  }
  else the_wait_handler (message, argument, level);
}
