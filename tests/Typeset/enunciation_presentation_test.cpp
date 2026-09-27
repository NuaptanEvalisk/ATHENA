/******************************************************************************
* MODULE     : enunciation_presentation_test.cpp
* DESCRIPTION: Native enunciation execution, typesetting and inert title checks
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QApplication>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include "boot.hpp"
#include "data_cache.hpp"
#include "drd_std.hpp"
#include "gui.hpp"
#include "scheme.hpp"
#include "server.hpp"
#include "typesetter.hpp"
#include "Bridge/impl_typesetter.hpp"
#include "analyze.hpp"
#include "convert.hpp"
#include "enunciation_presentation.hpp"
#include "formatter.hpp"
#include "Format/format.hpp"

bool headless_mode= true;
bool is_headless () { return true; }

namespace {
namespace node= athena::node;
namespace en= athena::enunciation;

struct Environment {
  drd_info drd {"enunciation-presentation", std_drd};
  hashmap<string,tree> h1 {UNINIT}, h2 {UNINIT}, h3 {UNINIT};
  hashmap<string,tree> h4 {UNINIT}, h5 {UNINIT}, h6 {UNINIT};
  edit_env env {drd, url_none (), h1, h2, h3, h4, h5, h6};
  Environment () {
    env->write_default_env ();
    const string root (std::getenv ("ATHENA_PATH"));
    env->exec (tree (USE_PACKAGE, root * "/styles/generic.ats"));
    env->write (FONT, "TeX Gyre Pagella");
    env->write ("athena-radioactive-links-suppressed", "true");
    env->write ("page-medium", "automatic");
    env->read_only= true;
    env->update ();
  }
};

tree source (const char* kind= "theorem", bool numbered= true) {
  tree t (en::label (), tree (DOCUMENT, "BodyToken"));
  node::metadata metadata;
  metadata.id= "11111111-1111-4111-8111-111111111111";
  metadata.properties["kind"]= node::property (std::string (kind));
  metadata.properties["name"]= node::property (node::rich_text {tree ("")});
  metadata.properties["numbered"]= node::property (numbered);
  node::set (t, metadata);
  return t;
}

bool has_tag (const tree& t, tree_label tag) {
  if (is_atomic (t)) return false;
  if (L(t) == tag) return true;
  for (int i= 0; i < N(t); ++i) if (has_tag (t[i], tag)) return true;
  return false;
}

string visible_text (box b) {
  if (N(b) == 0) {
    tree description= (tree) b;
    return is_atomic (description) ? description->label : string ();
  }
  string result;
  for (int i= 0; i < N(b); ++i) result << visible_text (b[i]);
  return result;
}

box lazy_box (edit_env env, tree body) {
  lazy content= make_lazy (env, body, path (0));
  lazy lines= content->produce (LAZY_VSTREAM,
    make_format_vstream (env->as_length ("25em"), 0, 0));
  return (box) lines->produce (LAZY_BOX, make_format_none ());
}
} // namespace

class EnunciationPresentationTest: public QObject {
  Q_OBJECT
private slots:
  void executionAndCursorEnvironment () {
    Environment context;
    edit_env env= context.env;
    tree t= source (), before= copy (t);
    QVERIFY (context.drd->correct_arity (en::label (), 1));
    QVERIFY (context.drd->is_accessible_child (t, 0));
    QVERIFY (env->provides ("next-theorem"));
    tree plan= native_enunciation_macro (env.operator-> (), t);
    QVERIFY (is_func (plan, MACRO, 2));
    QVERIFY (has_tag (plan, ARG));
    QVERIFY (!has_tag (plan, en::label ()));
    tree first= env->exec (t);
    QVERIFY (!has_tag (first, _ERROR));
    QCOMPARE (env->exec (compound ("the-theorem")), tree ("1"));
    env->exec_until (t, path (0, 0, 0));
    QCOMPARE (env->exec (compound ("the-theorem")), tree ("2"));
    QCOMPARE (env->read (FONT_SHAPE), tree ("italic"));
    env->exec (source ("theorem", false));
    QCOMPARE (env->exec (compound ("the-theorem")), tree ("2"));
    QCOMPARE (t, before);
  }

  void titlePropertiesAreInert () {
    Environment context;
    edit_env env= context.env;
    tree t= source ("theorem", false);
    auto metadata= *node::get (t);
    tree math= compound ("math", tree (CONCAT, "x", tree (RSUB, "1")));
    metadata.properties["name"]= node::property (node::rich_text {
      tree (CONCAT, compound ("strong", "Named"), " ", math,
            tree (ASSIGN, "title-side-effect", "bad"))});
    metadata.properties["attribution"]= node::property (node::property::list {
      node::property (node::rich_text {tree ("Author")})});
    metadata.properties["year"]= node::property (std::string ("19XX"));
    metadata.properties["target"]= node::property (node::reference {
      "22222222-2222-4222-8222-222222222222"});
    metadata.properties["test:unknown"]= node::property (node::rich_text {
      tree (EXTERN, "unexpected-script")});
    node::set (t, metadata);
    tree before= copy (t);
    tree plan= native_enunciation_macro (env.operator-> (), t);
    QVERIFY (!has_tag (plan, ASSIGN));
    QVERIFY (!has_tag (plan, EXTERN));
    QVERIFY (has_tag (plan, RSUB));
    QVERIFY (has_tag (plan, WITH));
    QVERIFY (has_tag (plan, HLINK));
    env->exec (t);
    QVERIFY (!env->provides ("title-side-effect"));
    QCOMPARE (t, before);
    tree unknown= source ("Future kind", false);
    box b= typeset_as_concat (env, unknown, path (0));
    QVERIFY (occurs ("Future kind", visible_text (b)));
    QVERIFY (occurs ("BodyToken", visible_text (b)));
  }

  void actualTypesettingAndIncrementalProperties () {
    Environment context;
    edit_env env= context.env;
    tree t= source ("theorem", false);
    auto metadata= *node::get (t);
    metadata.properties["name"]= node::property (node::rich_text {tree ("FirstName")});
    node::set (t, metadata);
    box inline_box= typeset_as_concat (env, t, path (0));
    QVERIFY (occurs ("FirstName", visible_text (inline_box)));
    QVERIFY (occurs ("BodyToken", visible_text (inline_box)));
    box lazy= lazy_box (env, tree (DOCUMENT, t));
    QVERIFY (occurs ("FirstName", visible_text (lazy)));
    QVERIFY (occurs ("BodyToken", visible_text (lazy)));
    tree doc (DOCUMENT, t);
    typesetter ts= new_typesetter (env, doc, path (0));
    // This fixture owns its source instead of addressing the active editor.
    ts->screen_tree= true;
    struct Delete { typesetter ts; ~Delete () { delete_typesetter (ts); } } cleanup {ts};
    SI x1= 0, y1= 0, x2= 0, y2= 0;
    box first= typeset (ts, x1, y1, x2, y2);
    QVERIFY2 (occurs ("FirstName", visible_text (first)),
              as_charp (tree_to_scheme ((tree) first)));
    QVERIFY (occurs ("BodyToken", visible_text (first)));
    metadata.properties["name"]= node::property (node::rich_text {tree ("SecondName")});
    tree changed= copy (t);
    node::set (changed, metadata);
    notify_assign (ts, path (0), changed);
    box second= typeset (ts, x1, y1, x2, y2);
    QVERIFY (occurs ("SecondName", visible_text (second)));
    QVERIFY (!occurs ("FirstName", visible_text (second)));
    bool found= false;
    path source_position (0, 0, 0, path (0, 3));
    path cursor= second->find_box_path (source_position, found);
    QVERIFY (found);
    QCOMPARE (second->find_tree_path (cursor), source_position);
    QCOMPARE (node::id (changed), node::id (t));
  }

  void legacyTitlesProofsAndQuotes () {
    Environment context;
    edit_env env= context.env;
    for (const char* tag: {"theorem", "theorem*", "proof", "proof-alternative",
                          "proof-standard", "quote-env"}) {
      tree legacy= compound (tag, tree (DOCUMENT, "BodyToken"));
      auto converted= en::convert_detached_source (legacy);
      QVERIFY (converted.diagnostics.empty ());
      tree rendered= env->exec (converted.source);
      QVERIFY2 (!has_tag (rendered, _ERROR), as_charp (tree_to_scheme (rendered)));
      box b= lazy_box (env, tree (DOCUMENT, converted.source));
      QVERIFY2 (occurs ("BodyToken", visible_text (b)), tag);
    }
    auto converted= en::convert_detached_source (
      compound ("render-theorem", "Exact original title", tree (DOCUMENT, "BodyToken")));
    tree macro= native_enunciation_macro (env.operator-> (), converted.source);
    QVERIFY (macro[1] == tree (DOCUMENT,
      compound ("render-theorem", "Exact original title", tree (ARG, "body"))));
    tree reference= native_enunciation_rich_text (tree (REFERENCE, "old-label"));
    QVERIFY (is_func (reference, HLINK, 2));
    QVERIFY (has_tag (reference, GET_BINDING));
    QCOMPARE (reference[1], tree ("#old-label"));
  }
};

static void run_tests (int argc, char** argv) {
  init_system_state ();
  gui_open (argc, argv);
  int result;
  {
    server sv;
    EnunciationPresentationTest test;
    result= QTest::qExec (&test, argc, argv);
  }
  gui_close ();
  release_boot_lock ();
  std::exit (result);
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

#include "enunciation_presentation_test.moc"
