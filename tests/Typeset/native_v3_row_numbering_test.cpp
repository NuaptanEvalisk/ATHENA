/******************************************************************************
* MODULE     : native_v3_row_numbering_test.cpp
* DESCRIPTION: Native MathGrid counters, references and row number geometry
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include <QApplication>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <cstdlib>
#include <stdexcept>
#include <utility>
#include "boot.hpp"
#include "data_cache.hpp"
#include "Data/NodeContracts/execution_state.hpp"
#include "gui.hpp"
#include "scheme.hpp"
#include "server.hpp"
#include "typesetter.hpp"
#include "Table/table.hpp"
#include "math_tree_v3.hpp"
#include "node_metadata.hpp"
#include "ATHENA/Data/node_reference_export.hpp"
#include "Xml/athena_document_xml.hpp"
#include "analyze.hpp"

bool headless_mode= true;
bool is_headless () { return true; }

namespace {
namespace math= athena::math_v3;
namespace node= athena::node;
namespace nr= athena::node_reference;

struct Environment {
  athena::contracts::evaluation_state contract_state {athena::contracts::builtin_contracts ().snapshot ()};
  hashmap<string,tree> h1 {UNINIT}, h2 {UNINIT}, h3 {UNINIT};
  hashmap<string,tree> h4 {UNINIT}, h5 {UNINIT}, h6 {UNINIT};
  edit_env env {contract_state, url_none (), h1, h2, h3, h4, h5, h6};
  Environment () {
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (MODE, "text");
    env->write (PAGE_PRINTED, "true");
    env->write ("athena-radioactive-links-suppressed", "true");
    env->write ("test-equation-counter", "0");
    env->write ("next-equation", tree (MACRO,
      tree (ASSIGN, "test-equation-counter",
            tree (PLUS, tree (VALUE, "test-equation-counter"), "1"))));
    env->write ("the-equation", tree (MACRO,
      tree (VALUE, "test-equation-counter")));
    env->write (THE_LABEL, "outside");
    env->write ("the-tags", tree (TUPLE, "outside-pending"));
    env->read_only= true;
    env->update ();
  }
};

tree native_grid (const char* kind, tree rows) {
  tree t (MATH_GRID, N(rows));
  for (int i= 0; i < N(rows); ++i) t[i]= rows[i];
  node::metadata meta;
  meta.properties["math:grid-kind"]= node::property (std::string (kind));
  node::set (t, meta);
  return t;
}

tree block (tree content, bool numbered) {
  tree t (MATH_BLOCK, content);
  node::metadata meta;
  meta.properties["math:block-numbered"]= node::property (numbered);
  node::set (t, meta);
  return t;
}

tree row (const char* body, const char* label) {
  return tree (ROW, tree (CELL, tree (CONCAT, body, tree (LABEL, label))));
}

void identify (tree& t, const char* id) {
  auto meta= node::get (t) ? *node::get (t) : node::metadata ();
  meta.id= id;
  node::set (t, meta);
}

std::string snapshot (const tree& t) {
  return athena::document::write_xml_v3 (t, athena::document::xml_kind::fragment);
}

string visible_text (box b) {
  // Shaped decorations also expose their text as a tree, but are not TEXT_BOX.
  if (N(b) == 0) {
    const tree value= tree (b);
    return is_atomic (value) ? value->label : string ("");
  }
  string result;
  for (int i= 0; i < N(b); ++i) result << visible_text (b[i]);
  return result;
}

struct Position { SI x= 0, y= 0, right= 0; };
bool locate (box b, string token, Position& out, SI x= 0, SI y= 0) {
  if (N(b) == 0 && occurs (token, visible_text (b))) {
    out= {x + b->x1, y, x + b->x2};
    return true;
  }
  for (int i= 0; i < N(b); ++i)
    if (locate (b[i], token, out, x + b->sx(i), y + b->sy(i))) return true;
  return false;
}
}

class NativeV3RowNumberingTest: public QObject {
  Q_OBJECT
private slots:
  void countersBindingsAndDestinations () {
    Environment f;
    tree source= native_grid ("align", tree (TUPLE,
      row ("a", "first"), row ("b", "omitted"),
      row ("c", "custom"), row ("d", "last")));
    math::set_row_numbering (source[1], math::row_numbering::unnumbered);
    math::set_row_tag (source[2], "T");
    identify (source[0], "11111111-1111-4111-8111-111111111111");
    identify (source[2], "22222222-2222-4222-8222-222222222222");
    identify (source[3], "33333333-3333-4333-8333-333333333333");
    identify (source, "44444444-4444-4444-8444-444444444444");
    const auto before= snapshot (source);
    nr::export_node_destination_scope destinations;
    box b= typeset_as_concat (f.env, block (source, true), path (0));
    QCOMPARE (f.env->read ("test-equation-counter"), tree ("2"));
    for (const auto& entry: {std::pair {"first", "1"}, {"custom", "T"}, {"last", "2"}})
      QCOMPARE (f.env->local_ref[entry.first][0], tree (entry.second));
    QCOMPARE (f.env->local_ref["omitted"][0], tree (""));
    hashmap<string,tree> pages (UNINIT);
    b->collect_page_numbers (pages, "page-seven");
    for (const char* label: {"first", "custom", "last"})
      QCOMPARE (pages[label], tree ("page-seven"));
    for (int i: {0, 2, 3}) {
      const string key= nr::export_node_destination (source[i]);
      QCOMPARE (pages[key], tree ("page-seven"));
      QCOMPARE (f.env->local_ref[key][0], tree (i == 0 ? "1" : i == 2 ? "T" : "2"));
      QVERIFY (!is_nil (b->find_tag (key)));
    }
    QCOMPARE (pages[nr::export_node_destination (source)], tree ("page-seven"));
    QCOMPARE (f.env->read (THE_LABEL), tree ("outside"));
    QCOMPARE (f.env->read ("the-tags"), tree (TUPLE, "outside-pending"));
    QCOMPARE (f.env->read (MODE), tree ("text"));
    QCOMPARE (f.env->read (MATH_DISPLAY), tree ("false"));
    QVERIFY (snapshot (source) == before);
  }

  void numberBoxesFollowRowBaselines () {
    Environment f;
    f.env->write (MODE, "math");
    f.env->update ();
    tree source= native_grid ("stack", tree (TUPLE, row ("x", "first"), row ("y", "last")));
    table layout (f.env);
    layout->typeset (source, path (0), true);
    layout->handle_decorations ();
    layout->handle_span ();
    layout->merge_borders ();
    layout->position_columns (true);
    layout->finish_horizontal ();
    layout->position_rows ();
    layout->finish ();
    Position first, last;
    QVERIFY (locate (layout->b, "1", first));
    QVERIFY (locate (layout->b, "2", last));
    QVERIFY (first.x > layout->T[0][0]->x2);
    QVERIFY (last.x > layout->T[1][0]->x2);
    QCOMPARE (first.y, layout->T[0][0]->y1 + layout->T[0][0]->yoff);
    QCOMPARE (last.y, layout->T[1][0]->y1 + layout->T[1][0]->yoff);
    QVERIFY (first.y > last.y);
    QVERIFY (layout->b->x2 >= first.right);
    QVERIFY (layout->b->x2 >= last.right);
  }

  void emptyAndNativeMathTagsDoNotAdvance () {
    for (tree tag: {tree (""), tree (CONCAT, "Case ", compound ("math", "x")),
                   math::scripts ("x", "", "", "i", "")}) {
      Environment f;
      tree source= native_grid ("eqnarray", tree (TUPLE, row ("x", "custom")));
      math::set_row_numbering (source[0], math::row_numbering::unnumbered);
      math::set_row_tag (source[0], tag);
      const auto before= snapshot (source);
      box b= typeset_as_concat (f.env, block (source, false), path (0));
      QCOMPARE (f.env->read ("test-equation-counter"), tree ("0"));
      QCOMPARE (f.env->local_ref["custom"][0], tag);
      QVERIFY (occurs ("(", visible_text (b)));
      QVERIFY (occurs (")", visible_text (b)));
      QVERIFY (b->w () > 0 && b->h () > 0);
      QVERIFY (snapshot (source) == before);
    }
  }

  void explicitNumberedOverridesUnnumberedBlock () {
    Environment f;
    tree source= native_grid ("align", tree (TUPLE, row ("x", "yes"), row ("y", "no")));
    math::set_row_numbering (source[0], math::row_numbering::numbered);
    (void) typeset_as_concat (f.env, block (source, false), path (0));
    QCOMPARE (f.env->read ("test-equation-counter"), tree ("1"));
    QCOMPARE (f.env->local_ref["yes"][0], tree ("1"));
    QCOMPARE (f.env->local_ref["no"][0], tree (""));
  }

  void nestedAndTagGridsDoNotInherit () {
    Environment f;
    tree nested= native_grid ("align", tree (TUPLE, row ("n", "inner")));
    tree source= native_grid ("align", tree (TUPLE,
      tree (ROW, tree (CELL, tree (CONCAT, nested, tree (LABEL, "outer")))),
      row ("x", "custom"), row ("y", "last")));
    math::set_row_tag (source[1], nested);
    (void) typeset_as_concat (f.env, block (source, true), path (0));
    QCOMPARE (f.env->read ("test-equation-counter"), tree ("2"));
    QCOMPARE (f.env->local_ref["outer"][0], tree ("1"));
    QCOMPARE (f.env->local_ref["inner"][0], tree (""));
    QCOMPARE (f.env->local_ref["last"][0], tree ("2"));
  }

  void nestedMathBlockExecutesOnce () {
    Environment f;
    tree inner= block (native_grid ("stack", tree (TUPLE, row ("x", "inner"))), true);
    tree source= native_grid ("matrix", tree (TUPLE, tree (ROW, tree (CELL, inner))));
    (void) typeset_as_concat (f.env, source, path (0));
    QCOMPARE (f.env->read ("test-equation-counter"), tree ("1"));
    QCOMPARE (f.env->local_ref["inner"][0], tree ("1"));
  }

  void wholeBlockKindsAndRejectedOverrides () {
    for (const char* kind: {"matrix", "cases", "generic"}) {
      Environment f;
      tree source= native_grid (kind, tree (TUPLE, row ("x", "a"), row ("y", "b")));
      (void) typeset_as_concat (f.env, block (source, true), path (0));
      QCOMPARE (f.env->read ("test-equation-counter"), tree ("1"));
      for (bool tag: {false, true}) {
        tree invalid= copy (source);
        if (tag) math::set_row_tag (invalid[0], "");
        else math::set_row_numbering (invalid[0], math::row_numbering::unnumbered);
        QVERIFY_EXCEPTION_THROWN (
          typeset_as_concat (f.env, block (invalid, true), path (0)), std::invalid_argument);
        QCOMPARE (f.env->read ("test-equation-counter"), tree ("1"));
      }
    }
  }

  void inlineAndNestedOverridesAreRejected () {
    for (bool tag: {false, true}) {
      Environment f;
      tree source= native_grid ("align", tree (TUPLE, row ("x", "invalid")));
      if (tag) math::set_row_tag (source[0], "");
      else math::set_row_numbering (source[0], math::row_numbering::numbered);
      QVERIFY_EXCEPTION_THROWN (typeset_as_concat (f.env, source, path (0)), std::invalid_argument);
      QCOMPARE (f.env->read ("test-equation-counter"), tree ("0"));
      tree outer= native_grid ("align", tree (TUPLE, tree (ROW, tree (CELL, source))));
      QVERIFY_EXCEPTION_THROWN (
        typeset_as_concat (f.env, block (outer, false), path (0)), std::invalid_argument);
      QCOMPARE (f.env->read (MODE), tree ("text"));
      QCOMPARE (f.env->read (THE_LABEL), tree ("outside"));
    }
  }

  void tallCustomTagsReserveRowHeight () {
    Environment f;
    tree source= native_grid ("stack", tree (TUPLE, row ("x", "a"), row ("y", "b")));
    box plain= typeset_as_concat (f.env, block (source, false), path (0));
    math::set_row_tag (source[0], tree (FRAC, tree (FRAC, "1", "2"), "3"));
    box tagged= typeset_as_concat (f.env, block (source, false), path (0));
    QVERIFY (tagged->h () > plain->h ());
    QVERIFY (tagged->w () > plain->w ());
    QCOMPARE (f.env->read ("test-equation-counter"), tree ("0"));
  }
};

static void run_tests (int argc, char** argv) {
  init_system_state ();
  gui_open (argc, argv);
  int result;
  {
    server sv;
    NativeV3RowNumberingTest test;
    result= QTest::qExec (&test, argc, argv);
  }
  std::_Exit (result);
}

int main (int argc, char** argv) {
  QApplication app (argc, argv);
  QTemporaryDir profile;
  if (!profile.isValid ()) return 1;
  qputenv ("ATHENA_HOME_PATH", profile.path ().toUtf8 ());
  cache_initialize ();
  init_athena ();
  start_scheme (argc, argv, run_tests);
  return 1;
}

#include "native_v3_row_numbering_test.moc"
