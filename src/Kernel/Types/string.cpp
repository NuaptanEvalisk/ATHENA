
/******************************************************************************
* MODULE     : string.cpp
* DESCRIPTION: Fixed size strings with reference counting.
*              Zero characters can be part of string.
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "string.hpp"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/******************************************************************************
* Low level routines and constructors
******************************************************************************/

static inline int
round_length (int n) {
  unsigned int u = (unsigned int) n;
  u = (u + 3) & ~3u;
  if (u < 24) return (int) u;
  return 1 << (32 - __builtin_clz (u - 1));
}

string_rep::string_rep (int n2):
  n(n2), a ((n==0)?((char*) NULL):tm_new_array<char> (round_length(n))) {}

struct dummy_string_rep_type : public string_rep {
  dummy_string_rep_type() : string_rep(0) {
    this->ref_count = 1000000000;
  }
};
static dummy_string_rep_type the_dummy_string_rep;
string_rep* dummy_string_rep = &the_dummy_string_rep;

void
string_rep::resize (int m) {
  int nn= round_length (n);
  int mm= round_length (m);
  if (mm != nn) {
    if (mm != 0) {
      char* b= tm_new_array<char> (mm);
      int k= (m<n? m: n);
      if (k > 0) memcpy (b, a, k);
      if (nn != 0) tm_delete_array (a);
      a= b;
    }
    else if (nn != 0) tm_delete_array (a);
  }
  n= m;
}

string::string (char c) {
  rep= tm_new<string_rep> (1);
  rep->a[0]=c;
}

string::string (char c, int n) {
  rep= tm_new<string_rep> (n);
  if (n > 0) memset (rep->a, c, n);
}

string::string (const char* a) {
  int n=strlen(a);
  rep= tm_new<string_rep> (n);
  if (n > 0) memcpy (rep->a, a, n);
}

string::string (const char* a, int n) {
  rep= tm_new<string_rep> (n);
  if (n > 0) memcpy (rep->a, a, n);
}

/******************************************************************************
* Common routines for strings
******************************************************************************/

bool
string::operator == (const char* s) const {
  int i, n= rep->n;
  char* S= rep->a;
  for (i=0; i<n; i++) {
    if (s[i]!=S[i]) return false;
    if (s[i]=='\0') return false;
  }
  return (s[i]=='\0');
}

bool
string::operator != (const char* s) const {
  int i, n= rep->n;
  char* S= rep->a;
  for (i=0; i<n; i++) {
    if (s[i]!=S[i]) return true;
    if (s[i]=='\0') return true;
  }
  return (s[i]!='\0');
}

bool
string::operator == (const string& a) const {
  if (rep->n != a.rep->n) return false;
  if (rep->n == 0) return true;
  return memcmp (rep->a, a.rep->a, rep->n) == 0;
}

bool
string::operator != (const string& a) const {
  if (rep->n != a.rep->n) return true;
  if (rep->n == 0) return false;
  return memcmp (rep->a, a.rep->a, rep->n) != 0;
}

string
string::operator () (int begin, int end) const {
  if (end <= begin) return string();

  begin = max(min(rep->n, begin), 0);
  end = max(min(rep->n, end), 0);
  int n= end-begin;
  string r (n);
  if (n > 0) memcpy (&r[0], rep->a + begin, n);
  return r;
}

string
copy (const string& s) {
  int n=N(s);
  string r (n);
  if (n > 0) memcpy (&r[0], &s[0], n);
  return r;
}

string&
operator << (string& a, char x) {
  a->resize (N(a)+ 1);
  a [N(a)-1]=x;
  return a;
}

string&
operator << (string& a, const string& b) {
  int k1= N(a), k2=N(b);
  a->resize (k1+k2);
  if (k2 > 0) memcpy (&a[k1], &b[0], k2);
  return a;
}

string
operator * (const string& a, const string& b) {
  int n1=N(a), n2=N(b);
  string c(n1+n2);
  if (n1 > 0) memcpy(&c[0], &a[0], n1);
  if (n2 > 0) memcpy(&c[n1], &b[0], n2);
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
  out->write (&a[0], N(a));
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
