/******************************************************************************
* MODULE     : buffer_state.hpp
* DESCRIPTION: BufferActor-owned mutable document state
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef BUFFER_STATE_HPP
#define BUFFER_STATE_HPP

#include "link.hpp"
#include "new_data.hpp"
#include "new_document.hpp"
#include "url.hpp"

class buffer_actor;

// This object is created, used, and destroyed by one BufferActor thread.  It
// is deliberately absent from tm_buffer_rep, whose fields belong to the Qt /
// Server thread.
struct buffer_document_state {
  buffer_actor* actor;
  url name;
  url master;
  string title;
  bool read_only;
  // Actor-authoritative timestamp. The UI metadata is only a delayed mirror.
  int last_save;
  tree document;
  path root_path;
  new_data data;
  link_repository links;
  bool notifier_attached;

  buffer_document_state (buffer_actor* actor2, string name2, string master2,
                         string title2, bool read_only2, int last_save2):
    actor (actor2), name (url (std::move (name2))),
    master (url (std::move (master2))), title (std::move (title2)),
    read_only (read_only2), last_save (last_save2),
    document (make_document_tree ()), root_path (0),
    data (), links (), notifier_attached (false) {}

  buffer_document_state (const buffer_document_state&)= delete;
  buffer_document_state& operator = (const buffer_document_state&)= delete;

  ~buffer_document_state () { clean_observers (document); }
};

#endif // defined BUFFER_STATE_HPP
