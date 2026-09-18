
/******************************************************************************
* MODULE     : tm_dialogue.cpp
* DESCRIPTION: Dialogues
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "tm_frame.hpp"
#include "tm_dialogue.hpp"
#include "tm_window.hpp"
#include "convert.hpp"
#include "file.hpp"
#include "analyze.hpp"
#include "message.hpp"
#include "gui_text.hpp"
#include "actor_ui_bridge.hpp"
#include "buffer_actor.hpp"
#include "editor.hpp"
#include "scheme_execution_context.hpp"
#include "QTMNativeDialogs.hpp"

#include <memory>
#include <vector>

/******************************************************************************
* File chooser completion
******************************************************************************/

struct chooser_completion_state {
  object fun;
  command actor_fun;
  widget chooser;
  bool actor_bound;
  athena_actor_id actor_id;
  athena_view_id view_id;
  SchemeCapabilitySet capabilities;

  chooser_completion_state (object fun2):
    fun (fun2), actor_fun (as_actor_command (fun2)), actor_bound (false),
    actor_id (ATHENA_NO_ACTOR), view_id (ATHENA_NO_VIEW),
    capabilities (SCHEME_CAPABILITY_NONE) {
    const SchemeExecutionContext* context= current_scheme_execution_context ();
    if (context != nullptr && context->actor_id != ATHENA_NO_ACTOR) {
      actor_bound= true;
      actor_id= context->actor_id;
      view_id= context->view_id;
      capabilities= context->capabilities;
    }
  }
};

class chooser_command_rep: public command_rep {
  std::shared_ptr<chooser_completion_state> state;
public:
  chooser_command_rep (std::shared_ptr<chooser_completion_state> state2):
    state (std::move (state2)) {}
  void apply ();
  tm_ostream& print (tm_ostream& out) {
    return out << "<command chooser>"; }
};

void
dispatch_actor_chooser_result (
    command actor_fun, string expression,
    athena_actor_id actor_id, athena_view_id view_id,
    SchemeCapabilitySet capabilities) {
  expression.ensure_transferable ();
  athena_continuation_id id=
    actor_continuation_registry::instance ().store (
      [expression= std::move (expression),
       actor_fun= std::move (actor_fun)] () mutable {
        object arg= eval (expression);
        actor_fun (list_object (arg));
      });
  if (!buffer_actor::submit_to (
        actor_id, actor_command_kind::run_native_continuation,
        view_id, ATHENA_NO_BLOB, ATHENA_NO_BLOB,
        capabilities, id))
    (void) actor_continuation_registry::instance ().discard (id);
}

void
chooser_command_rep::apply () {
  if (is_nil (state->chooser)) return;
  string s_arg= get_string_input (state->chooser);
  if (s_arg == "#f") return;
  if (N(s_arg) == 0) s_arg= "\"\"";
  if (state->actor_bound) {
    dispatch_actor_chooser_result (
      state->actor_fun, std::move (s_arg), state->actor_id,
      state->view_id, state->capabilities);
    return;
  }
  object arg= string_to_object (s_arg);
  object args= list_object (arg);
  exec_delayed (scheme_cmd (cons (state->fun, args)));
}

/*
static int
gcd (int i, int j) {
  if (i<j)  return gcd (j, i);
  if (j==0) return i;
  return gcd (j, i%j);
}
*/

void
tm_frame_rep::choose_file (object fun, string title, string type,
			   string prompt, url name) {
  auto state= std::make_shared<chooser_completion_state> (fun);
  command  cb  = tm_new<chooser_command_rep> (state);
  widget   wid = file_chooser_widget (cb, type, prompt);
  state->chooser= wid;
  if (!is_scratch (name)) {
    set_directory (wid, as_string (head (name)));
    if ((type != "image") && (type != "")) {
      url u= tail (name);
      string old_suf= suffix (u);
      string new_suf= format_to_suffix (type);
      if ((suffix_to_format (suffix (u)) != type) &&
          (old_suf != "") && (new_suf != ""))
        {
          u= unglue (u, N(old_suf) + 1);
          u= glue (u, "." * new_suf);
        }
      set_file (wid, as_string (u));
    }
  }
  else set_directory (wid, ".");
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context != nullptr && context->editor != nullptr &&
      context->view_id != ATHENA_NO_VIEW) {
    athena_resource_id chooser_id= actor_ui_store_widget (std::move (wid));
    if (!context->editor->publish_ui_text_pair (
          actor_command_kind::ui_choose_file, std::move (title),
          std::move (type), chooser_id))
      (void) actor_ui_discard_widget (chooser_id);
    return;
  }
  (void) plain_window_widget (wid, ui_text (title));
  if (type == "directory") send_keyboard_focus (get_directory (wid));
  else send_keyboard_focus (get_file (wid));
}

/******************************************************************************
* Interactive commands
******************************************************************************/

static string
get_prompt (scheme_tree p, int i) {
  if (is_atomic (p[i]) && is_quoted (p[i]->label))
    return ui_text (scm_unquote (p[i]->label));
  else if (is_tuple (p[i]) && N(p[i])>0) {
    if (is_atomic (p[i][0]) && is_quoted (p[i][0]->label))
      return ui_text (scm_unquote (p[i][0]->label));
    return ui_text (scheme_tree_to_tree (p[i][0]));
  }
  return "Input:";
}

static string
get_type (scheme_tree p, int i) {
  if (is_tuple (p[i]) && N(p[i])>1 &&
      is_atomic (p[i][1]) && is_quoted (p[i][1]->label))
    return scm_unquote (p[i][1]->label);
  return "string";
}

static array<string>
get_proposals (scheme_tree p, int i) {
  array<string> a;
  if (is_tuple (p[i]) && N(p[i]) >= 2) {
    int j, n= N(p[i]);
    for (j=2; j<n; j++)
      if (is_atomic (p[i][j]) && is_quoted (p[i][j]->label))
        a << scm_unquote (p[i][j]->label);
  }
  return a;
}

void
tm_frame_rep::interactive (object fun, scheme_tree p) {
  ASSERT (is_tuple (p), "tuple expected");
  if (N(p) == 0) {
    string ret= object_to_string (call (fun));
    if (ret != "" && ret != "<unspecified>" && ret != "#<unspecified>")
      set_message (verbatim (ret), "interactive command");
  }
  else {
    int i, n= N(p);
    std::vector<QTMInteractiveField> fields;
    fields.reserve (n);
    for (i=0; i<n; i++) {
      QTMInteractiveField field;
      field.prompt= get_prompt (p, i);
      field.type= get_type (p, i);
      field.proposals= get_proposals (p, i);
      fields.push_back (std::move (field));
    }
    string title= "Enter data";
    if (ends (fields[0].prompt, "?")) title= "Question";
    array<string> answers= qtm_interactive_dialog (title, fields);
    if (N(answers) != n) return;

    array<object> args (n);
    object learn= null_object ();
    for (i=n-1; i>=0; --i) {
      args[i]= object (answers[i]);
      object learned= fields[i].type == "password" ? object ("") : args[i];
      learn= cons (cons (object (as_string (i)), learned), learn);
    }
    call ("learn-interactive", fun, learn);
    object cmd_args= as_list_object (args);
    const SchemeExecutionContext* context= current_scheme_execution_context ();
    if (context != nullptr && context->actor_id != ATHENA_NO_ACTOR)
      as_actor_command (fun) (cmd_args);
    else
      exec_delayed (scheme_cmd (cons (fun, cmd_args)));
  }
}
