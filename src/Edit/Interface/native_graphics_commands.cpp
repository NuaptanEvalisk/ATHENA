// Copyright (C) 2001-2011 Joris van der Hoeven and Henri Lesourd,
// 2026 ATHENA contributors. GPL-3.0-or-later.
#include "native_graphics_commands.hpp"
#include "native_graphics_grid.hpp"
#include "native_interfaces.hpp"
#include "editor.hpp"
#include "file.hpp"
#include "Data/NodeContracts/node_groups.hpp"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <cmath>
#include <map>

namespace {
string text (QString value) {
  QByteArray bytes= value.toUtf8 ();
  return string (bytes.constData (), bytes.size ());
}
struct graphics_defaults {
  std::vector<string> names;
  std::map<string, tree> values;
  graphics_defaults () {
    string source;
    ASSERT (!load_string (url ("$ATHENA_PATH/misc/input/graphics-properties.json"), source, false),
            "cannot read graphics-properties.json");
    QJsonParseError error;
    auto document= QJsonDocument::fromJson (QByteArray (source.data (), N(source)), &error);
    ASSERT (error.error == QJsonParseError::NoError && document.isObject () &&
            document.object ().value ("version").toInt () == 1,
            "invalid graphics-properties.json");
    for (QJsonValue value: document.object ().value ("properties").toArray ()) {
      QJsonArray pair= value.toArray ();
      ASSERT (pair.size () == 2 && pair[0].isString () && pair[1].isString (),
              "invalid graphics default property");
      names.push_back (text (pair[0].toString ()));
      values.emplace (names.back (), text (pair[1].toString ()));
    }
  }
};
const graphics_defaults& defaults () { static const graphics_defaults result; return result; }

tree attribute_owner (tree t) {
  if (!is_func (t, WITH)) {
    const path ip= obtain_ip (t);
    if (ip_attached (ip) && !is_nil (ip)) {
      tree root= get_current_editor ()->the_root ();
      tree parent= subtree (root, reverse (ip->next));
      if (is_func (parent, WITH)) return parent;
    }
  }
  return t;
}
}

const std::vector<string>& graphics_property_names () { return defaults ().names; }
tree graphics_default_property (string name) {
  if (starts (name, "gr-")) name= name (3, N(name));
  const auto found= defaults ().values.find (name);
  return found == defaults ().values.end () ? tree ("default") : found->second;
}

tree graphics_object_attribute (tree object, string name) {
  tree owner= attribute_owner (object);
  if (is_func (owner, WITH))
    for (int i=N(owner)-3; i>=0; i-=2)
      if (owner[i] == name) return owner[i+1];
  return graphics_default_property (name);
}

void graphics_set_object_attribute (tree object, string name, tree value) {
  tree owner= attribute_owner (object);
  if (is_func (owner, WITH)) {
    for (int i=N(owner)-3; i>=0; i-=2)
      if (owner[i] == name) { tree_assign (owner[i+1], value); return; }
    tree_insert (owner, N(owner)-1, tree (TUPLE, name, value));
  }
  else tree_insert_node (owner, 2, tree (WITH, name, value));
}

string graphics_vertical_alignment (tree object) {
  string tag= as_string (L(object));
  return athena::contracts::standard_node_groups ().contains (
    "graphical-long-text-tag", std::string (tag.data (), N(tag))) ?
    string ("doc-at-valign") : string ("text-at-valign");
}

void graphics_insert (tree properties) {
  if (N(properties) == 0) {
    properties << "gr-mode" << tuple ("hand-edit", "penscript")
      << "gr-frame" << tuple ("scale", "1cm", tuple ("0.5gw", "0.5gh"))
      << "gr-geometry" << tuple ("geometry", "1par", "0.6par");
  }
  tree source (WITH);
  for (int i=0; i<N(properties); ++i) source << properties[i];
  source << tree (GRAPHICS, "");
  get_current_editor ()->insert_tree (source, path (N(properties), 1));
}

void graphics_make (object options) {
  array<object> values= as_array_object (options);
  ASSERT (N(values) % 2 == 0, "graphics properties require name/value pairs");
  tree properties (TUPLE);
  for (int i=0; i<N(values); ++i) properties << content_to_tree (values[i]);
  graphics_insert (properties);
}

void graphics_update_proviso (tree source, string value) {
  if (is_atomic (source)) return;
  if (is_func (source, WITH))
    for (int i=0; i+1<N(source)-1; i+=2)
      if (source[i] == "gr-proviso" && is_compound (source[i+1]) &&
          N(source[i+1]) == 1 && is_atomic (source[i+1][0]))
        tree_assign (source[i+1][0], value);
  for (int i=0; i<N(source); ++i) graphics_update_proviso (source[i], value);
}

void graphics_rotate (double angle, bool xz) {
  editor ed= get_current_editor ();
  tree matrix= ed->get_env_value ("gr-transformation");
  tree result= copy (matrix);
  const int first= xz ? 0 : 1;
  const double c= std::cos (angle), s= std::sin (angle);
  for (int j=0; j<N(matrix[first]); ++j) {
    const double a= as_double (matrix[first][j]), b= as_double (matrix[2][j]);
    result[first][j]= as_string (c*a+s*b);
    result[2][j]= as_string (-s*a+c*b);
  }
  ed->native_graphics_set_property ("gr-transformation", result);
}

void graphics_grid_subdivisions (int divisions) {
  native_graphics_grid_command ("aspect", tuple ("detailed", as_string (divisions)), true);
}
