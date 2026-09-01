
/******************************************************************************
* MODULE     : tm_buffer.hpp
* DESCRIPTION: TeXmacs main data structures (buffers, views and windows)
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef TM_BUFFER_H
#define TM_BUFFER_H
#include "new_data.hpp"
#include "Data/new_buffer.hpp"
#include "link.hpp"
#include "new_document.hpp"

class tm_buffer_rep;
class tm_view_rep;
class buffer_actor;
typedef tm_buffer_rep* tm_buffer;
typedef tm_view_rep*   tm_view;

url  create_window_id ();
void destroy_window_id (url);

class tm_buffer_rep {
public:
  new_buffer buf;         // file related information
  new_data data;          // data associated to document
  array<tm_view> vws;     // views attached to buffer
  tree document;          // actor-owned edit-tree root
  path rp;                // path to the document inside document
  link_repository lns;    // global links
  bool notify;            // notify modifications to scheme
  buffer_actor* actor;    // exclusive execution owner

  tm_buffer_rep (url name);
  ~tm_buffer_rep ();

  void attach_notifier ();
  bool needs_to_be_saved ();
  bool needs_to_be_autosaved ();
};

inline tm_buffer nil_buffer () { return (tm_buffer) NULL; }
inline bool is_nil (tm_buffer buf) { return buf == NULL; }

#endif // defined TM_BUFFER_H
