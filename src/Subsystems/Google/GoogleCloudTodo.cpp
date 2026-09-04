/******************************************************************************
* MODULE     : GoogleCloudTodo.cpp
* DESCRIPTION: Cloud todo-list synchronization with Google Tasks
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "GoogleCloudTodo.hpp"

#include "GoogleOAuth.hpp"
#include "GoogleTasksClient.hpp"
#include "QTMToast.hpp"
#include "actor_transport.hpp"
#include "boot.hpp"
#include "buffer_actor.hpp"
#include "new_buffer.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"
#include "scheme_execution_context.hpp"
#include "tm_data.hpp"

#include <QHash>
#include <QSet>
#include <QSharedPointer>

#include <functional>

namespace {

struct CloudTodoItem {
  QString title;
  QString normalized;
  bool    completed= false;
};

struct CloudTodoTaskState {
  QString id;
  bool    completed= false;
};

struct CloudTodoRewrite {
  tree                    doc;
  QVector<CloudTodoItem>  items;
  bool                    hasCloud= false;
  bool                    changed= false;
};

static tree_label
cloud_todo_item_label () {
  static tree_label lab= make_tree_label ("cloud-todo-item");
  return lab;
}

static tree_label
cloud_done_item_label () {
  static tree_label lab= make_tree_label ("cloud-done-item");
  return lab;
}

static tree_label
cloud_todo_list_label () {
  static tree_label lab= make_tree_label ("cloud-todo-list");
  return lab;
}

static tree_label
item_label () {
  static tree_label lab= make_tree_label ("item");
  return lab;
}

static bool
is_cloud_marker (tree t) {
  return is_compound (t) &&
         (L(t) == cloud_todo_item_label () ||
          L(t) == cloud_done_item_label ());
}

static bool
is_cloud_todo_marker (tree t, bool inCloudList) {
  return is_cloud_marker (t) ||
         (inCloudList && is_compound (t) && L(t) == item_label ());
}

static bool
is_completed_marker (tree t) {
  return is_compound (t) && L(t) == cloud_done_item_label ();
}

static bool
skip_text_node (tree t) {
  if (!is_compound (t)) return false;
  string tag= as_string (L(t));
  return tag == "label" || tag == "reference" || tag == "pageref" ||
         tag == "image" || tag == "item" || tag == "item*" ||
         tag == "todo-item" || tag == "done-item";
}

static void
append_visible_text (tree t, string& out) {
  if (is_atomic (t)) {
    out << t->label;
    return;
  }
  if (is_cloud_marker (t) || skip_text_node (t)) return;
  for (int i=0; i<N(t); i++) {
    if (N(out) > 0) out << " ";
    append_visible_text (t[i], out);
  }
}

static QString
item_title (tree row) {
  string raw;
  append_visible_text (row, raw);
  return to_qstring (raw).simplified ();
}

static QString
normalize_title (const QString& title) {
  return title.simplified ().toCaseFolded ();
}

static void
show_cloud_todo_toast (const QString& title, const QString& body) {
  qtm_show_toast (from_qstring (body), from_qstring (title));
}

static QString
preferred_task_list_id () {
  return to_qstring (
    get_preference ("google tasks cloud todo list id", ""));
}

static void
with_task_list_id (std::function<void(const QString&, const QString&)> cont) {
  QString preferred= preferred_task_list_id ().trimmed ();
  GoogleTasksClient::instance ().listTaskLists (
    [preferred, cont] (const QVector<GoogleTaskList>& lists,
                       const QString& error) {
      if (!error.isEmpty ()) {
        cont (QString (), error);
        return;
      }
      if (lists.isEmpty ()) {
        cont (QString (), "No Google task lists found.");
        return;
      }
      if (!preferred.isEmpty ()) {
        for (const GoogleTaskList& list: lists)
          if (list.id == preferred) {
            cont (list.id, QString ());
            return;
          }
      }
      cont (lists[0].id, QString ());
    });
}

static QHash<QString, CloudTodoTaskState>
task_state_map (const QVector<GoogleTask>& tasks) {
  QHash<QString, CloudTodoTaskState> map;
  for (const GoogleTask& task: tasks) {
    QString key= normalize_title (task.title);
    if (key.isEmpty () || map.contains (key)) continue;
    CloudTodoTaskState state;
    state.id= task.id;
    state.completed= task.status == "completed";
    map.insert (key, state);
  }
  return map;
}

static void
create_google_task (const QString& listId, const CloudTodoItem& item) {
  GoogleTasksClient::instance ().insertTaskDetailed (
    listId, item.title,
    [listId, item] (bool ok, const GoogleTask& task, const QString&) {
      if (ok && item.completed && !task.id.isEmpty ())
        GoogleTasksClient::instance ().setTaskCompleted (
          listId, task.id, true, [] (bool, const QString&) {});
    });
}

static tree
rewrite_cloud_todos (tree t, const QHash<QString, CloudTodoTaskState>& tasks,
                     CloudTodoRewrite& state, bool inCloudList) {
  if (is_atomic (t)) return t;

  bool hereCloudList= inCloudList || L(t) == cloud_todo_list_label ();
  tree out (L(t), N(t));
  for (int i=0; i<N(t); i++)
    out[i]= rewrite_cloud_todos (t[i], tasks, state, hereCloudList);

  for (int i=0; i<N(out); i++) {
    if (!is_cloud_todo_marker (out[i], hereCloudList)) continue;

    state.hasCloud= true;
    QString title= item_title (out);
    QString normalized= normalize_title (title);
    if (normalized.isEmpty ()) continue;

    CloudTodoItem item;
    item.title= title;
    item.normalized= normalized;
    item.completed= is_completed_marker (out[i]);
    state.items << item;

    if (!tasks.contains (normalized)) continue;
    bool googleCompleted= tasks.value (normalized).completed;
    tree_label wanted= googleCompleted? cloud_done_item_label ():
                                      cloud_todo_item_label ();
    if (L(out[i]) != wanted) {
      out[i]= tree (wanted, N(out[i]));
      state.changed= true;
    }
  }

  return out;
}

static QVector<CloudTodoItem>
collect_cloud_todos (tree t) {
  CloudTodoRewrite state;
  QHash<QString, CloudTodoTaskState> empty;
  rewrite_cloud_todos (t, empty, state, false);
  return state.items;
}

static void
sync_buffer_with_tasks (url name, const QString& listId,
                        const QVector<GoogleTask>& tasks) {
  if (!contains (name, get_all_buffers ())) return;
  tree doc= get_buffer_tree (name);
  QHash<QString, CloudTodoTaskState> map= task_state_map (tasks);
  CloudTodoRewrite state;
  state.doc= rewrite_cloud_todos (doc, map, state, false);
  if (!state.hasCloud) return;

  if (state.changed) {
    set_buffer_tree (name, state.doc);
    pretend_buffer_modified (name);
  }

  QSet<QString> created;
  for (const CloudTodoItem& item: state.items) {
    if (map.contains (item.normalized) || created.contains (item.normalized))
      continue;
    created.insert (item.normalized);
    create_google_task (listId, item);
  }
}

static void
push_item_with_tasks (const QString& listId, const CloudTodoItem& item,
                      const QVector<GoogleTask>& tasks) {
  QHash<QString, CloudTodoTaskState> map= task_state_map (tasks);
  if (map.contains (item.normalized)) {
    CloudTodoTaskState state= map.value (item.normalized);
    if (state.completed != item.completed)
      GoogleTasksClient::instance ().setTaskCompleted (
        listId, state.id, item.completed, [] (bool, const QString&) {});
    return;
  }

  create_google_task (listId, item);
}

static bool
google_tasks_connected () {
  return !GoogleOAuth::instance ().clientId ().trimmed ().isEmpty () &&
         GoogleOAuth::instance ().hasRefreshToken ();
}

} // namespace

void
google_cloud_todo_sync_buffer (url name, bool notifyDisconnected) {
  if (headless_mode || is_none (name)) return;
  tm_buffer buffer= concrete_buffer (name);
  if (is_nil (buffer)) return;

  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context == nullptr || context->actor != buffer->actor) {
    athena_continuation_id continuationId=
      actor_continuation_registry::instance ().store (
        [name= std::move (name), notifyDisconnected] () mutable {
          google_cloud_todo_sync_buffer (
            std::move (name), notifyDisconnected);
        });
    actor_command_ticket ticket= buffer_actor::submit_to (
      buffer->actor->id (), actor_command_kind::run_native_continuation,
      ATHENA_NO_VIEW, ATHENA_NO_BLOB, ATHENA_NO_BLOB,
      SCHEME_CAPABILITY_BUFFER | SCHEME_CAPABILITY_GLOBAL,
      continuationId);
    if (!ticket)
      (void) actor_continuation_registry::instance ().discard (continuationId);
    return;
  }

  tree doc= get_buffer_tree (name);
  QVector<CloudTodoItem> items= collect_cloud_todos (doc);
  if (items.isEmpty ()) return;

  if (!google_tasks_connected ()) {
    if (notifyDisconnected)
      show_cloud_todo_toast (
        "Cloud todo list",
        "Google Tasks is not connected; cloud todo items were not synchronized.");
    return;
  }

  with_task_list_id ([name] (const QString& listId, const QString& error) {
    if (!error.isEmpty ()) {
      show_cloud_todo_toast ("Cloud todo list", error);
      return;
    }
    GoogleTasksClient::instance ().listTasks (
      listId, true,
      [name, listId] (const QVector<GoogleTask>& tasks,
                      const QString& taskError) {
        if (!taskError.isEmpty ()) {
          show_cloud_todo_toast ("Cloud todo list", taskError);
          return;
        }
        sync_buffer_with_tasks (name, listId, tasks);
      });
  });
}

void
google_cloud_todo_sync_open_buffers (bool notifyDisconnected) {
  array<url> buffers= get_all_buffers ();
  for (int i=0; i<N(buffers); i++)
    google_cloud_todo_sync_buffer (buffers[i], notifyDisconnected);
}

void
google_cloud_todo_push_item (tree row, bool completed) {
  QString title= item_title (row);
  QString normalized= normalize_title (title);
  if (title.isEmpty () || normalized.isEmpty ()) return;

  if (!google_tasks_connected ()) {
    show_cloud_todo_toast (
      "Cloud todo list",
      "Google Tasks is not connected; the cloud todo change was not synchronized.");
    return;
  }

  CloudTodoItem item;
  item.title= title;
  item.normalized= normalized;
  item.completed= completed;

  with_task_list_id ([item] (const QString& listId, const QString& error) {
    if (!error.isEmpty ()) {
      show_cloud_todo_toast ("Cloud todo list", error);
      return;
    }
    GoogleTasksClient::instance ().listTasks (
      listId, true,
      [listId, item] (const QVector<GoogleTask>& tasks,
                      const QString& taskError) {
        if (!taskError.isEmpty ()) {
          show_cloud_todo_toast ("Cloud todo list", taskError);
          return;
        }
        push_item_with_tasks (listId, item, tasks);
      });
  });
}
