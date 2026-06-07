/******************************************************************************
* MODULE     : GoogleCloudTodo.hpp
* DESCRIPTION: Cloud todo-list synchronization with Google Tasks
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef GOOGLECLOUDTODO_HPP
#define GOOGLECLOUDTODO_HPP

#include "tree.hpp"
#include "url.hpp"

void google_cloud_todo_sync_buffer (url name, bool notifyDisconnected= true);
void google_cloud_todo_sync_open_buffers (bool notifyDisconnected= false);
void google_cloud_todo_push_item (tree item, bool completed);

#endif // GOOGLECLOUDTODO_HPP
