/******************************************************************************
* MODULE     : native_latex_commands.cpp
* DESCRIPTION: Native JSON LaTeX command registry
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************/

#include "native_latex_commands.hpp"
#include "editor.hpp"
#include "file.hpp"
#include "generic_editor_commands.hpp"
#include "Scheme/Scheme/native_interfaces.hpp"
#include "Subsystems/Qt/QTMReverseHierarchyGraph.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <unordered_map>
#include <vector>

namespace {

string native_string (const QJsonValue& value) {
  ASSERT (value.isString (), "native LaTeX command string expected");
  const QString text= value.toString ();
  const QByteArray bytes= text.toUtf8 ();
  ASSERT (QString::fromUtf8 (bytes) == text, "invalid UTF-8 LaTeX command string");
  return string (bytes.constData (), bytes.size ());
}

tree native_tree (const QJsonValue& value) {
  if (value.isString ()) return tree (native_string (value));
  ASSERT (value.isArray (), "native LaTeX command tree must be string or array");
  const QJsonArray values= value.toArray ();
  ASSERT (!values.isEmpty () && values[0].isString (),
          "native LaTeX command compound tree needs a tag");
  tree result (as_tree_label (native_string (values[0])), values.size () - 1);
  for (int i=1; i<values.size (); ++i) result[i-1]= native_tree (values[i]);
  return result;
}

void make_section (editor ed, string tag) {
  if (ed->selection_active_any ()) {
    if (!ed->selection_active_small ()) return;
    ed->make_compound (as_tree_label (tag));
    return;
  }
  if (!ed->make_return_after ()) ed->make_compound (as_tree_label (tag));
}

void make_equation_like (editor ed, string tag) {
  if (ed->selection_active_any () && !ed->selection_active_small ()) return;
  ed->make_compound (as_tree_label (tag));
  ed->ensure_trailing_proof_paragraph ();
}

void make_doc_data (editor ed) {
  if (ed->selection_active_any () && !ed->selection_active_small ()) return;
  tree value (as_tree_label ("doc-data"),
              tree (as_tree_label ("doc-title"), tree ("")));
  ed->insert_tree (value, path (0, 0, 0));
}

void make_abstract_data (editor ed) {
  tree value (as_tree_label ("abstract-data"),
              tree (as_tree_label ("abstract"), tree ("")));
  ed->insert_tree (value, path (0, 0, 0));
}

void make_aux (editor ed, string env, string var, string fallback) {
  string aux= ed->defined_at_cursor (var) ? ed->get_env_string (var) : fallback;
  if (ed->make_return_after ()) return;
  tree value (as_tree_label (env), tree (aux), tree (DOCUMENT, ""));
  ed->insert_tree (value);
}

void make_item (editor ed) {
  if (ed->make_return_after ()) return;
  tree descriptions (TUPLE, 5);
  descriptions[0]= "description";
  descriptions[1]= "description-compact";
  descriptions[2]= "description-aligned";
  descriptions[3]= "description-dash";
  descriptions[4]= "description-long";
  const string label= ed->inside_which (descriptions);
  ed->make_compound (as_tree_label (label == "" ? "item" : "item*"));
}

struct latex_binding {
  string name;
  string help;
  QJsonArray actions;
};

struct native_latex_registry {
  std::vector<latex_binding> bindings;
  std::unordered_map<std::string,int> lookup;

  native_latex_registry () {
    string source;
    bool failed= load_string (
      url ("$ATHENA_PATH/misc/input/latex-commands.json"), source, false);
    ASSERT (!failed, "cannot read latex-commands.json");
    QJsonParseError error;
    QJsonDocument document= QJsonDocument::fromJson (
      QByteArray (source.data (), N(source)), &error);
    ASSERT (error.error == QJsonParseError::NoError && document.isObject (),
            "invalid latex-commands.json");
    const QJsonObject root= document.object ();
    ASSERT (root.value ("version").toInt () == 1 &&
            root.value ("string_encoding") == "utf-8" &&
            root.value ("bindings").isArray (),
            "unsupported native LaTeX command schema");
    const QJsonArray values= root.value ("bindings").toArray ();
    bindings.reserve ((std::size_t) values.size ());
    for (const QJsonValue& value: values) {
      ASSERT (value.isObject (), "native LaTeX command binding expected");
      const QJsonObject object= value.toObject ();
      ASSERT (object.value ("command").isString () &&
              object.value ("help").isString () &&
              object.value ("actions").isArray () &&
              !object.value ("actions").toArray ().isEmpty (),
              "invalid native LaTeX command binding");
      latex_binding binding;
      binding.name= native_string (object.value ("command"));
      binding.help= native_string (object.value ("help"));
      binding.actions= object.value ("actions").toArray ();
      bindings.push_back (std::move (binding));
      const string& name= bindings.back ().name;
      lookup[std::string (name.data (), (std::size_t) N(name))]=
        (int) bindings.size () - 1;
    }
  }
};

native_latex_registry& registry () {
  static native_latex_registry value;
  return value;
}

void execute_action (const QJsonObject& action) {
  editor ed= get_current_editor ();
  ASSERT (!is_nil (ed), "native LaTeX command without editor");
  const string op= native_string (action.value ("op"));
  if (op == "insert-tree") ed->insert_tree (native_tree (action.value ("value")));
  else if (op == "make-with")
    ed->make_with (native_string (action.value ("var")),
                   native_string (action.value ("value")));
  else if (op == "make")
    ed->make_compound (as_tree_label (native_string (action.value ("tag"))));
  else if (op == "emulate-keyboard")
    ed->emulate_keyboard (native_string (action.value ("keys")));
  else if (op == "insert-big")
    ed->insert_tree (tree (BIG, native_tree (action.value ("value"))));
  else if (op == "bracket-open")
    ed->math_bracket_open (native_string (action.value ("left")),
                           native_string (action.value ("right")), 1);
  else if (op == "bracket-close")
    ed->math_bracket_close (native_string (action.value ("right")),
                            native_string (action.value ("left")), 1);
  else if (op == "make-wide" || op == "make-wide-under") {
    const string value= native_string (action.value ("value"));
    const bool stretch= action.value ("stretch").toBool (false);
    if (op == "make-wide") ed->make_wide (value, stretch);
    else ed->make_wide_under (value, stretch);
  }
  else if (op == "make-space") ed->make_space (native_string (action.value ("value")));
  else if (op == "make-vspace-after")
    ed->make_vspace_after (native_string (action.value ("value")));
  else if (op == "make-fraction") ed->make_fraction ();
  else if (op == "make-sqrt") ed->make_sqrt ();
  else if (op == "make-neg") ed->make_neg ();
  else if (op == "make-section")
    make_section (ed, native_string (action.value ("tag")));
  else if (op == "make-equation-like")
    make_equation_like (ed, native_string (action.value ("tag")));
  else if (op == "make-doc-data") make_doc_data (ed);
  else if (op == "make-abstract-data") make_abstract_data (ed);
  else if (op == "make-aux")
    make_aux (ed, native_string (action.value ("env")),
              native_string (action.value ("var")),
              native_string (action.value ("fallback")));
  else if (op == "make-item") make_item (ed);
  else if (op == "make-label") generic_make_label ();
  else if (op == "make-cd") athena_make_commutative_diagram ();
  else if (op == "insert-reverse-hierarchy-graph") reverse_hierarchy_graph_insert ();
  else FAILED ("unknown native LaTeX command action");
}

void run_binding (int index) {
  auto& r= registry ();
  ASSERT (index >= 0 && index < (int) r.bindings.size (),
          "invalid native LaTeX command binding");
  for (const QJsonValue& value: r.bindings[(std::size_t) index].actions) {
    ASSERT (value.isObject (), "native LaTeX command action must be an object");
    execute_action (value.toObject ());
  }
}

class native_latex_command_rep final: public command_rep {
  int index;
public:
  explicit native_latex_command_rep (int i): index (i) {}
  void apply () override { run_binding (index); }
  tm_ostream& print (tm_ostream& out) override {
    return out << "<native-latex-command>";
  }
};

} // namespace

bool native_latex_get_command (string which, string& help, command& cmd) {
  auto& r= registry ();
  const std::string key (which.data (), (std::size_t) N(which));
  auto found= r.lookup.find (key);
  if (found == r.lookup.end ()) return false;
  const int index= found->second;
  help= r.bindings[(std::size_t) index].help;
  cmd= command (tm_new<native_latex_command_rep> (index));
  return true;
}

int native_latex_command_count () {
  return (int) registry ().bindings.size ();
}
