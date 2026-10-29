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
#include "Data/NodeContracts/editing_query.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include "boot.hpp"
#include "data_cache.hpp"
#include "Data/NodeContracts/execution_state.hpp"
#include "gui.hpp"
#include "scheme.hpp"
#include "server.hpp"
#include "typesetter.hpp"
#include "Bridge/impl_typesetter.hpp"
#include "analyze.hpp"
#include "convert.hpp"
#include "enunciation_presentation.hpp"
#include "enunciation_surround.hpp"
#include "Data/NodeContracts/contract_declaration.hpp"
#include "new_style.hpp"
#include "Typeset/Env/local_environment.hpp"
#include "formatter.hpp"
#include "Format/format.hpp"
#include "ATHENA/Data/vault.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "Xml/athena_document_xml.hpp"
#include "ATHENA/Data/node_reference_export.hpp"
#include "Line/lazy_vstream.hpp"
#include "Pdf/pdf_hummus_renderer.hpp"
#include "printer.hpp"

bool headless_mode= true;
bool is_headless () { return true; }

namespace {
namespace node= athena::node;
namespace en= athena::enunciation;
namespace nr= athena::node_reference;

void identify (tree& t, const char* id) {
  node::metadata metadata;
  if (node::get (t)) metadata= *node::get (t);
  metadata.id= id;
  node::set (t, metadata);
}

struct Environment {
  athena::contracts::evaluation_state contract_state {athena::contracts::builtin_contracts ().snapshot ()};
  hashmap<string,tree> h1 {UNINIT}, h2 {UNINIT}, h3 {UNINIT};
  hashmap<string,tree> h4 {UNINIT}, h5 {UNINIT}, h6 {UNINIT};
  edit_env env {contract_state, url_none (), h1, h2, h3, h4, h5, h6};
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
  if (b->w () == 0 && b->h () == 0) return string ();
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

box render_destination_fixture (tree t, int route) {
  Environment context;
  if (route == 0) return typeset_as_concat (context.env, t, path (0));
  if (route == 1) return lazy_box (context.env, t);
  typesetter ts= new_typesetter (context.env, t, path (0));
  ts->screen_tree= true;
  struct Delete { typesetter ts; ~Delete () { delete_typesetter (ts); } } cleanup {ts};
  SI x1= 0, y1= 0, x2= 0, y2= 0;
  return typeset (ts, x1, y1, x2, y2);
}

bool same_extents (box a, box b) {
  return a->x1 == b->x1 && a->y1 == b->y1 &&
         a->x2 == b->x2 && a->y2 == b->y2 &&
         a->x3 == b->x3 && a->y3 == b->y3 &&
         a->x4 == b->x4 && a->y4 == b->y4;
}

box text_box_containing (box b, string token) {
  // Linked Unicode lines are mapped leaves, not TEXT_BOX instances.
  if (N(b) == 0) {
    tree text= (tree) b;
    if (is_atomic (text) && occurs (token, text->label)) return b;
  }
  for (int i=0; i<N(b); ++i) {
    box found= text_box_containing (b[i], token);
    if (!is_nil (found)) return found;
  }
  return box ();
}

bool text_position (box b, string token, SI& x, SI& y, SI& width) {
  if (N(b) == 0) {
    tree description= (tree) b;
    if (is_atomic (description) && occurs (token, description->label)) {
      x= b->x1;
      y= 0;
      width= b->w ();
      return true;
    }
  }
  for (int i= 0; i < N(b); ++i)
    if (text_position (b[i], token, x, y, width)) {
      x+= b->sx (i);
      y+= b->sy (i);
      return true;
    }
  return false;
}

bool has_link_target (box b, const AthenaArtifactRecord& record) {
  AthenaArtifactRadioactiveMatch match;
  match.uuids= {record.uuid};
  match.disambiguation_key= athena_artifact_radioactive_key (record);
  string destination (athena_artifact_radioactive_destination (match).c_str ());
  for (SI x= b->x1; x < b->x2; x+= PIXEL) {
    rectangles rs;
    if (b->message ("link-target", x, (b->y1 + b->y2) / 2, rs) ==
        tree (TUPLE, "link-target", destination)) return true;
  }
  return false;
}
} // namespace

class EnunciationPresentationTest: public QObject {
  Q_OBJECT
private slots:
  void sourceIdentityPrefixesPreserveLocalDefinitions () {
    const string name ("identity-prefix-probe");
    const auto label= make_tree_label (name);
    const auto macro= [] (const char* value) {
      return tree (MACRO, "argument", tree (value));
    };
    tree body (DOCUMENT,
      tree (ASSIGN, name, macro ("first")),
      tree (CONCAT, "before ", tree (label, "payload")),
      tree (WITH, name, macro ("local"),
        tree (CONCAT, "inside ", tree (label, "payload"))),
      tree (ASSIGN, name, macro ("later")),
      tree (CONCAT, "after ", tree (label, "payload")));
    tree document (DOCUMENT, compound ("style", tuple ("generic")),
                   compound ("body", body));
    const auto entries= prepare_document_source_contracts (document);
    const auto read= [&] (const std::vector<int>& where) -> tree {
      const auto context= entries (where);
      if (!context) return tree (UNINIT);
      const auto binding= context->definition (label);
      return binding ? tree (binding->value->expression) : tree (UNINIT);
    };
    QCOMPARE (read ({1, 1}), macro ("first"));
    QCOMPARE (read ({2, 2, 1}), macro ("local"));
    QCOMPARE (read ({4, 1}), macro ("later"));
    // Rewinding must not reuse a later paragraph's effective bindings.
    QCOMPARE (read ({1, 1}), macro ("first"));

    // The source owner must resolve authored preamble definitions before
    // interpreting later custom macro fields, even when that preamble is not
    // presented as part of the editable paragraph sequence.
    tree prelude (DOCUMENT, tree (ASSIGN, name, macro ("preamble")));
    tree with_preamble (DOCUMENT, compound ("style", tuple ("generic")),
      compound ("body", tree (DOCUMENT,
        compound ("hide-preamble", prelude),
        tree (CONCAT, "body ", tree (label, "payload")))));
    const auto preamble_entry= prepare_document_source_contracts (with_preamble) ({1, 1});
    QVERIFY (preamble_entry);
    const auto preamble_binding= preamble_entry->definition (label);
    QVERIFY (preamble_binding);
    QCOMPARE (tree (preamble_binding->value->expression), macro ("preamble"));
  }

  void cachedContractReadsTrackReferenceAndAttachmentProviders () {
    using namespace athena::contracts;
    Environment context;
    edit_env env= context.env;
    context.h2 ("external-reference")= tree (TUPLE, "initial", "1");
    context.h6 ("external-attachment")= "attachment-one";
    const auto capture= [&] (auto reader) {
      contract_event_log log;
      contract_event_capture records (env->contract_events, log, &context.contract_state.contracts);
      reader ();
      records.commit ();
      return log;
    };
    auto reads= capture ([&] {
      QCOMPARE (env->exec (tree (GET_BINDING, "external-reference")), tree ("initial"));
      QCOMPARE (env->exec (tree (GET_ATTACHMENT, "external-attachment")),
                tree ("attachment-one"));
      // Identical provider reads need just one receipt per name and source.
      (void) env->exec (tree (GET_BINDING, "external-reference"));
      (void) env->exec (tree (GET_ATTACHMENT, "external-attachment"));
    });
    QVERIFY (reads && reads->records.size () == 2);
    QVERIFY (env->can_replay_events (reads));
    context.h1 ("external-reference")= context.h2["external-reference"];
    QVERIFY (!env->can_replay_events (reads)); // Same value, new provider.
    context.h1->reset ("external-reference");
    context.h6 ("external-attachment")= "attachment-two";
    QVERIFY (!env->can_replay_events (reads));
    context.h6 ("external-attachment")= "attachment-one";
    QVERIFY (env->can_replay_events (reads));

    const auto missing= capture ([&] {
      QCOMPARE (env->exec (tree (GET_BINDING, "absent-reference")), tree (UNINIT));
    });
    context.h2 ("absent-reference")= tree (TUPLE, "now-present", "2");
    QVERIFY (!env->can_replay_events (missing));

    // SET_BINDING also changes touched/redefinition state, which the current
    // contract-only replay cannot publish. A cached block containing it must
    // therefore not be admitted as a pure variable/declaration effect replay.
    const auto modified= capture ([&] {
      (void) env->exec (tree (SET_BINDING, "external-reference", "rebound"));
    });
    QVERIFY (!env->can_replay_events (modified));
  }
  void withPackageEvaluatesNestedDeclarationsAndRestoresOnlyItsWrites () {
    Environment context;
    QTemporaryDir root;
    QVERIFY (root.isValid ());
    const QString child= root.filePath ("child.ats");
    const QString parent= root.filePath ("parent.ats");
    const auto document= [] (const QByteArray& body) {
      return QByteArray ("<?xml version='1.0'?><athena-document version='1' text-model='utf-8'>"
        "<node tag='document'><node tag='body'><node tag='document'>") + body +
        "</node></node></node></athena-document>";
    };
    const auto save= [] (const QString& name, const QByteArray& content) {
      QFile file (name);
      return file.open (QIODevice::WriteOnly) && file.write (content) == content.size ();
    };
    QVERIFY (save (child, document (
      "<node tag='assign'><text>package-child</text><node tag='macro'>"
      "<text>x</text><node tag='arg'><text>x</text></node></node></node>")));
    QVERIFY (save (parent, document (
      "<node tag='use-package'><text>" + child.toUtf8 () + "</text></node>"
      "<node tag='assign'><text>package-parent</text><text>scoped</text></node>"
      "<node tag='drd-props'><text>package-child</text><text>name</text>"
      "<text>Nested child</text></node>"
      "<node tag='with'><text>internal-only</text><text>temporary</text>"
      "<text>invisible</text></node>")));
    const string package (parent.toUtf8 ().constData ());
    context.env->write ("package-parent", "outside");
    const auto child_label= make_tree_label ("package-child");
    athena::contracts::contract_event_log cached;
    {
      athena::contracts::contract_event_capture record (
        context.env->contract_events, cached, &context.contract_state.contracts);
      scoped_package_environment scope (*context.env.operator-> (), tree (package));
      QCOMPARE (context.env->read ("package-parent"), tree ("scoped"));
      QVERIFY (context.contract_state.contracts.definition (child_label));
      QVERIFY (context.contract_state.contracts.declaration_history (child_label));
      scope.finish ();
      record.commit ();
    }
    QCOMPARE (context.env->read ("package-parent"), tree ("outside"));
    QVERIFY (!context.contract_state.contracts.definition (child_label));
    QVERIFY (!context.contract_state.contracts.declaration_history (child_label));
    QVERIFY (context.env->can_replay_events (cached));
    const tree result= context.env->exec (tree (WITH_PACKAGE, package,
      tree (DOCUMENT,
        tree (ASSIGN, "outside-write", "preserved"),
        tree (ASSIGN, "internal-only", "body-write"),
        tree (VALUE, "package-parent"))));
    QVERIFY (is_document (result));
    QCOMPARE (result[2], tree ("scoped"));
    QCOMPARE (context.env->read ("package-parent"), tree ("outside"));
    QCOMPARE (context.env->read ("outside-write"), tree ("preserved"));
    QCOMPARE (context.env->read ("internal-only"), tree ("body-write"));
    QVERIFY (!context.contract_state.contracts.definition (child_label));
    QVERIFY (save (child, document (
      "<node tag='assign'><text>package-child</text><node tag='macro'>"
      "<text>x</text><text>changed nested package</text></node></node>")));
    QVERIFY (!context.env->can_replay_events (cached));
  }
  void focusedTitleSlotsAreScreenOnly () {
    Environment context;
    tree t= source ();
    auto metadata= *node::get (t);
    node::property::list names {node::property (node::rich_text {tree ("FirstName")}),
      node::property (node::rich_text {tree ("SecondName")})};
    metadata.properties["name"]= node::property (names);
    node::set (t, metadata);
    QVERIFY (en::is_canonical (t));
    tree normal= native_enunciation_macro (context.env.operator-> (), t);
    QVERIFY (!has_tag (normal, ORNAMENT));
    context.env->editing_enunciation_id= node::id (t);
    tree focused= native_enunciation_macro (context.env.operator-> (), t);
    QVERIFY (has_tag (focused, ORNAMENT));
    QVERIFY (has_tag (focused, LOCUS));
    box rendered= lazy_box (context.env, t);
    QVERIFY (rendered->w () > 0 && rendered->h () > 0);
    const string displayed= visible_text (rendered);
    QVERIFY2 (occurs ("FirstName", displayed), as_charp (displayed));
    QVERIFY (occurs ("SecondName", displayed));
    QVERIFY (occurs ("Year", displayed));
    QVERIFY (occurs ("Attribution", displayed));
    context.env->write (PAGE_PRINTED, "true");
    QCOMPARE (native_enunciation_macro (context.env.operator-> (), t), normal);
    context.env->write (PAGE_PRINTED, "false");
    tree definition= source ("definition");
    context.env->editing_enunciation_id= node::id (definition);
    QVERIFY (has_tag (native_enunciation_macro (context.env.operator-> (), definition), ORNAMENT));
  }

  void nativeDisplayStartsSeparateEnunciationParagraph () {
    tree grid (MATH_GRID,
      tree (ROW, tree (CELL, "x"), tree (CELL, "="), tree (CELL, "y")));
    node::metadata grid_metadata;
    grid_metadata.properties["math:grid-kind"]= node::property (std::string ("eqnarray"));
    node::set (grid, grid_metadata);
    for (tree formula: {tree ("x"), grid}) {
      tree block (MATH_BLOCK, formula);
      node::metadata metadata;
      metadata.properties["math:block-numbered"]= node::property (false);
      node::set (block, metadata);
      // The migrated corollary retains both document layers and whole-body bold.
      for (tree display: {block, grid})
      for (tree body: {tree (DOCUMENT, display),
                      tree (DOCUMENT, tree (WITH, "font-series", "bold",
                                            tree (DOCUMENT, display)))}) {
        Environment context;
        QVERIFY (athena_enunciation_starts_display (context.env, body));
        tree surround= compound ("enunciation-surround", "HeadingToken", "", body);
        tree rewritten= athena_enunciation_surround_rewrite (context.env, surround);
        QVERIFY (is_func (rewritten, DOCUMENT, 2));
        QCOMPARE (rewritten[1], body);
        QCOMPARE (rewritten[0],
          tree (WITH, "par-mode", "left", tree (CONCAT, tree (NO_INDENT), "HeadingToken")));

        // Macro argument indirection is the active native enunciation route.
        context.env->macro_arg= list<hashmap<string,tree> > (
          hashmap<string,tree> (UNINIT), context.env->macro_arg);
        context.env->macro_arg->item ("body")= body;
        QVERIFY (athena_enunciation_starts_display (context.env, tree (ARG, "body")));

        tree statement= source ("corollary");
        node::metadata statement_metadata= *node::get (statement);
        statement_metadata.properties["name"]= node::property (
          node::rich_text {tree ("HeadingToken")});
        node::set (statement, statement_metadata);
        statement[0]= body;
        for (int route: {1, 2}) {
          box rendered= render_destination_fixture (statement, route);
          SI title_x, title_y, title_w, formula_x, formula_y, formula_w;
          QVERIFY (text_position (rendered, "HeadingToken", title_x, title_y, title_w));
          QVERIFY (text_position (rendered, "x", formula_x, formula_y, formula_w));
          QVERIFY (formula_y < title_y);
          // A lone formula is centered across the whole paragraph, not the
          // space remaining to the right of its title.
          if (is_atomic (formula) && is_func (display, MATH_BLOCK))
            QVERIFY (abs (2 * formula_x + formula_w -
                          rendered->x1 - rendered->x2) < context.env->as_length ("1em"));
        }
      }
    }
    Environment context;
    for (tree body: {tree (DOCUMENT, compound ("math", "x")),
                    tree (DOCUMENT, compound ("math", grid)),
                    tree (DOCUMENT, "A statement", compound ("equation*", "x"))}) {
      QVERIFY (!athena_enunciation_starts_display (context.env, body));
      tree surround= compound ("enunciation-surround", "HeadingToken", "", body);
      QCOMPARE (athena_enunciation_surround_rewrite (context.env, surround),
                tree (SURROUND, "HeadingToken", "", body));
    }
  }

  void proofEquationArrayCellSelections () {
    for (const char* tag: {"eqnarray", "eqnarray*"})
    for (int proof: {0, 1, 2}) {
      tree table (TABLE,
        tree (ROW, tree (CELL, "u"), tree (CELL, "="), tree (CELL, "A(t)")),
        tree (ROW, tree (CELL, "v"), tree (CELL, "="), tree (CELL, "B(t)")));
      tree equation= compound (tag, tree (DOCUMENT, tree (TFORMAT, table)));
      tree document (DOCUMENT, equation);
      path table_path= path (0, 0, 0) * 0 * 0;
      if (proof) {
        tree statement= proof == 1? source ("proof", false):
                                   compound ("proof", "");
        statement[0]= tree (DOCUMENT, "Define", equation);
        document= tree (DOCUMENT, statement);
        table_path= path (0, 0, 0) * 1 * 0 * 0 * 0;
      }
      box rendered= render_destination_fixture (document, 2);
      for (int row= 0; row < 2; ++row) {
        SI previous_right= 0;
        SI bottom= 0, top= 0;
        for (int column= 0; column < 3; ++column) {
          path cell= table_path * row * column;
          selection selected= rendered->find_check_selection (cell * 0, cell * 1);
          QVERIFY (selected->valid);
          QCOMPARE (N(selected->rs), 1);
          rectangle bounds= selected->rs->item;
          QVERIFY (bounds->x2 > bounds->x1);
          if (column) {
            QCOMPARE (bounds->x1, previous_right);
            QCOMPARE (bounds->y1, bottom);
            QCOMPARE (bounds->y2, top);
          }
          bottom= bounds->y1;
          top= bounds->y2;
          previous_right= bounds->x2;
        }
      }
    }
  }

  void exportDestinationsPreservePresentation () {
    tree atom ("office affinity AV Token");
    identify (atom, "22222222-2222-4222-8222-222222222222");
    tree formatted (WITH, FONT_SHAPE, "italic", atom);
    identify (formatted, "33333333-3333-4333-8333-333333333333");
    tree paragraph (CONCAT, "Prefix ", formatted, " suffix");
    identify (paragraph, "44444444-4444-4444-8444-444444444444");
    tree theorem= source ("theorem", false);
    theorem[0]= tree (DOCUMENT, paragraph);
    identify (theorem[0], "55555555-5555-4555-8555-555555555555");
    tree document (DOCUMENT, theorem);
    identify (document, "66666666-6666-4666-8666-666666666666");
    const std::string revision= nr::export_source_revision (document);

    for (int route: {0, 1, 2}) {
      // Automatic bridge pages own their "?" page number; bare boxes use
      // the sentinel supplied to collect_page_numbers below.
      const tree expected_page= route == 2? tree ("?"): tree ("page");
      box screen= render_destination_fixture (document, route);
      hashmap<string,tree> screen_pages (UNINIT), exported_pages (UNINIT);
      screen->collect_page_numbers (screen_pages, "page");
      box exported;
      {
        nr::export_node_destination_scope scope;
        exported= render_destination_fixture (document, route);
        exported->collect_page_numbers (exported_pages, "page");
        for (tree target: {atom, formatted, paragraph, theorem[0], theorem, document}) {
          string label= nr::export_node_destination (target);
          QVERIFY (label != "");
          QCOMPARE (screen_pages[label], tree (UNINIT));
          QVERIFY2 (exported_pages[label] == expected_page,
            qPrintable (QString ("route=%1 destination=%2 actual=%3").arg (route)
              .arg (QString::fromUtf8 (as_charp (label)))
              .arg (QString::fromUtf8 (as_charp (tree_to_scheme (exported_pages[label]))))));
        }
      }
      QVERIFY (!nr::export_node_destinations_active ());
      QCOMPARE (nr::export_node_destination (document), string ());
      QCOMPARE (visible_text (exported), visible_text (screen));
      QVERIFY (same_extents (screen, exported));
      QVERIFY (nr::export_source_revision (document) == revision);
    }
  }

  void exportDestinationsPreserveAdjacentTextShaping () {
    tree first ("of"), second ("fice AV affinity");
    identify (first, "22222222-2222-4222-8222-222222222222");
    identify (second, "33333333-3333-4333-8333-333333333333");
    tree document (DOCUMENT, tree (CONCAT, first, second));
    for (int route: {0, 1, 2}) {
      const tree expected_page= route == 2? tree ("?"): tree ("page");
      box screen= render_destination_fixture (document, route);
      nr::export_node_destination_scope scope;
      box exported= render_destination_fixture (document, route);
      QCOMPARE (visible_text (exported), visible_text (screen));
      QVERIFY (same_extents (screen, exported));
      hashmap<string,tree> pages (UNINIT);
      exported->collect_page_numbers (pages, "page");
      for (tree target: {first, second}) {
        string label= nr::export_node_destination (target);
        QVERIFY2 (pages[label] == expected_page,
          qPrintable (QString ("route=%1 destination=%2 actual=%3").arg (route)
            .arg (QString::fromUtf8 (as_charp (label)))
            .arg (QString::fromUtf8 (as_charp (tree_to_scheme (pages[label]))))));
      }
    }
  }

  void exportDestinationsSkipHiddenAndUnselectedContent () {
    tree hidden_text ("HiddenToken"), omitted ("OmittedToken");
    identify (hidden_text, "22222222-2222-4222-8222-222222222222");
    identify (omitted, "33333333-3333-4333-8333-333333333333");
    tree hidden (HIDDEN, hidden_text);
    identify (hidden, "44444444-4444-4444-8444-444444444444");
    tree setting (ASSIGN, "destination-test-setting", "value");
    identify (setting, "55555555-5555-4555-8555-555555555555");
    tree document (DOCUMENT, hidden, setting,
                   tree (IF, "false", omitted, "VisibleToken"));
    const std::string revision= nr::export_source_revision (document);
    for (int route: {0, 1, 2}) {
      box screen= render_destination_fixture (document, route);
      nr::export_node_destination_scope scope;
      box exported= render_destination_fixture (document, route);
      hashmap<string,tree> pages (UNINIT);
      exported->collect_page_numbers (pages, "page");
      for (tree target: {hidden_text, hidden, omitted, setting})
        QCOMPARE (pages[nr::export_node_destination (target)], tree (UNINIT));
      QVERIFY (occurs ("VisibleToken", visible_text (exported)));
      QCOMPARE (visible_text (exported), visible_text (screen));
      QVERIFY (same_extents (screen, exported));
      QVERIFY (nr::export_source_revision (document) == revision);
    }
  }

  void exportDestinationsBelongToFirstLazyLine () {
    Environment context;
    tree paragraph ("A long paragraph with enough words to wrap onto several lines "
                    "while keeping its destination on the first rendered line only.");
    identify (paragraph, "22222222-2222-4222-8222-222222222222");
    tree document (DOCUMENT, paragraph, "Later paragraph");
    identify (document, "33333333-3333-4333-8333-333333333333");
    nr::export_node_destination_scope scope;
    lazy content= make_lazy (context.env, document, path (0));
    lazy produced= content->produce (LAZY_VSTREAM,
      make_format_vstream (context.env->as_length ("8em"), 0, 0));
    lazy_vstream stream= (lazy_vstream) produced;
    int first= -1, rendered= 0;
    hashmap<string,tree> pages (UNINIT);
    for (int i= 0; i < N(stream->l); ++i)
      if (stream->l[i]->type == PAGE_LINE_ITEM) {
        if (first == -1) first= i;
        ++rendered;
        stream->l[i]->b->collect_page_numbers (pages, as_string (i));
      }
    QVERIFY (rendered > 1);
    QCOMPARE (pages[nr::export_node_destination (paragraph)], tree (as_string (first)));
    QCOMPARE (pages[nr::export_node_destination (document)], tree (as_string (first)));
  }

  void exportTranscludedDestinationsStayOutOfScreenCache () {
    tree target= source ("theorem", false);
    const auto id= node::id (target);
    auto snapshot= std::make_shared<athena::node_location::result> ();
    snapshot->state= athena::node_location::status::resolved;
    athena::node_location::item item;
    item.id= id;
    item.state= athena::node_location::status::resolved;
    item.fragment_xml= athena::document::write_xml_v2 (
      target, athena::document::xml_kind::fragment);
    snapshot->items.push_back (item);
    nr::view view {snapshot, 1};
    auto present= [&] {
      return athena_artifact_radioactive_suppress_definitions (nr::display (view));
    };
    tree screen= present ();
    string label;
    {
      nr::export_node_destination_scope scope;
      label= nr::export_node_destination (target);
      tree printed= present ();
      box body= render_destination_fixture (printed, 1);
      hashmap<string,tree> pages (UNINIT);
      body->collect_page_numbers (pages, "page");
      QCOMPARE (pages[label], tree ("page"));
    }
    QCOMPARE (present (), screen);
    QVERIFY (!occurs (label, visible_text (render_destination_fixture (screen, 1))));
  }

  void exportDestinationsResolvePdfLinks () {
    QVERIFY (!QStandardPaths::findExecutable ("qpdf").isEmpty ());
    QVERIFY (!QStandardPaths::findExecutable ("pdftotext").isEmpty ());
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    const QString requested= qEnvironmentVariable ("ATHENA_NODE_PDF_TEST_OUTPUT");
    const QString filename= requested.isEmpty ()?
      directory.filePath ("node-destinations.pdf"): requested;
    tree target ("TargetToken");
    identify (target, "22222222-2222-4222-8222-222222222222");
    const string local= "tmfs://wikilink/22222222-2222-4222-8222-222222222222";
    const string outside= "tmfs://wikilink/33333333-3333-4333-8333-333333333333";
    const string missing= "tmfs://transclude/44444444-4444-4444-8444-444444444444";
    tree links (CONCAT);
    links << tree (HLINK, tree (WITH, COLOR, "red", "LocalToken"), local)
          << tree (" ") << tree (HLINK, "OutsideToken", outside)
          << tree (" ") << tree (HLINK, "MissingToken", missing)
          << tree (" ") << tree (HLINK, "WebToken", "https://example.invalid/target");
    tree document (DOCUMENT, links, target);
    const std::string revision= nr::export_source_revision (document);
    {
      Environment context;
      context.env->write (PAGE_PRINTED, "true");
      context.env->update ();
      nr::export_node_destination_scope scope;
      box body= lazy_box (context.env, document);
      hashmap<string,tree> labels (UNINIT);
      body->collect_page_numbers (labels, "1");
      QCOMPARE (labels[nr::export_node_destination (target)], tree ("1"));
      QVERIFY (nr::export_node_link (local, labels) != "");
      QCOMPARE (nr::export_node_link (outside, labels), string ());
      QCOMPARE (nr::export_node_link (missing, labels), string ());
      renderer pdf= printer (url_system (string (filename.toUtf8 ().constData ())),
        600, 1, "a4", false, 21.0, 29.7);
      struct Delete { renderer pdf; ~Delete () { tm_delete (pdf); } } cleanup {pdf};
      QVERIFY (pdf->is_started ());
      pdf_hummus_set_link_resolver (pdf, [labels] (string uri) {
        return nr::export_node_link (uri, labels);
      });
      pdf->move_origin (400 * PIXEL, -600 * PIXEL);
      rectangles damage;
      body->redraw (pdf, path (0), damage);
    }
    auto run= [] (QString program, QStringList arguments, QByteArray& output) {
      QProcess process;
      process.start (program, arguments);
      if (!process.waitForStarted () || !process.waitForFinished (30000)) return false;
      output= process.readAllStandardOutput ();
      return process.exitStatus () == QProcess::NormalExit && process.exitCode () == 0;
    };
    QByteArray output;
    QVERIFY (run ("qpdf", {"--check", filename}, output));
    QVERIFY (run ("pdftotext", {"-raw", "-nopgbrk", "-enc", "UTF-8", filename, "-"}, output));
    for (const char* token: {"LocalToken", "OutsideToken", "MissingToken", "WebToken", "TargetToken"})
      QVERIFY2 (output.contains (token), token);
    QVERIFY (run ("qpdf", {"--json", "--json-key=qpdf", filename}, output));
    QVERIFY (!output.contains ("tmfs://"));
    const QJsonDocument json= QJsonDocument::fromJson (output);
    QVERIFY (json.isObject ());
    const QJsonArray sections= json.object ().value ("qpdf").toArray ();
    QCOMPARE (sections.size (), 2);
    const QJsonObject objects= sections[1].toObject ();
    int internal= 0, external= 0;
    for (auto it= objects.begin (); it != objects.end (); ++it) {
      const QJsonObject value= it.value ().toObject ().value ("value").toObject ();
      if (value.value ("/Subtype").toString () != "/Link") continue;
      if (value.contains ("/Dest")) {
        QVERIFY (value.value ("/Dest").isArray ());
        QVERIFY (!value.value ("/Dest").toArray ().isEmpty ());
        ++internal;
      }
      else {
        const QJsonObject action= value.value ("/A").toObject ();
        QCOMPARE (action.value ("/S").toString (), QString ("/URI"));
        QCOMPARE (action.value ("/URI").toString (),
                  QString ("u:https://example.invalid/target"));
        ++external;
      }
    }
    QCOMPARE (internal, 1);
    QCOMPARE (external, 1);
    QVERIFY (nr::export_source_revision (document) == revision);
  }

  void hybridInputRefreshesReferencedText () {
    Environment context;
    tree document (DOCUMENT, tree (INACTIVE, tree (HYBRID, "")));
    typesetter ts= new_typesetter (context.env, document, path (0));
    ts->screen_tree= true;
    struct Delete { typesetter ts; ~Delete () { delete_typesetter (ts); } } cleanup {ts};
    SI x1= 0, y1= 0, x2= 0, y2= 0;
    auto render= [&] { return typeset (ts, x1, y1, x2, y2); };
    render ();
    notify_insert (ts, path (0, 0, 0, 0), tree ("the"));
    QCOMPARE (ts->br->st[0][0][0], tree ("the"));
    QVERIFY (occurs ("the", visible_text (render ())));
    notify_insert (ts, path (0, 0, 0, 3), tree ("orem"));
    box typed= render ();
    QVERIFY (occurs ("theorem", visible_text (typed)));
    bool found= false;
    path position (0, 0, 0, path (0, 7));
    path cursor= typed->find_box_path (position, found);
    QVERIFY (found);
    QCOMPARE (typed->find_tree_path (cursor), position);
    notify_remove (ts, path (0, 0, 0, 3), 4);
    box erased= render ();
    QVERIFY (occurs ("the", visible_text (erased)));
    QVERIFY (!occurs ("theorem", visible_text (erased)));
    notify_remove (ts, path (0, 0, 0, 0), 3);
    QVERIFY (!occurs ("the", visible_text (render ())));
  }

  void hybridArgumentFieldsPreserveSourcePaths () {
    Environment context;
    context.env->write ("macro-input-test",
      tree (MACRO, "title", "body", tree (CONCAT, tree (ARG, "title"), tree (ARG, "body"))));
    tree argument= compound ("math", tree (FRAC, "a", "b"));
    tree input (HYBRID, "macro-input-test", tree (TUPLE, "Title", argument), "");
    tree document (DOCUMENT, tree (INACTIVE, input));
    typesetter ts= new_typesetter (context.env, document, path (0));
    ts->screen_tree= true;
    struct Delete { typesetter ts; ~Delete () { delete_typesetter (ts); } } cleanup {ts};
    SI x1= 0, y1= 0, x2= 0, y2= 0;
    box rendered= typeset (ts, x1, y1, x2, y2);
    QVERIFY (occurs ("Title", visible_text (rendered)));
    QVERIFY (occurs ("body", visible_text (rendered)));
    for (path position: {path (0, 0, 0) * 0 * 3,
                         path (0, 0, 0) * 1 * 0 * 2,
                         path (0, 0, 0) * 1 * 1 * 0 * 0 * 1}) {
      bool found= false;
      path box_path= rendered->find_box_path (position, found);
      QVERIFY (found);
      QCOMPARE (rendered->find_tree_path (box_path), position);
    }
    QByteArray screenshot= qgetenv ("ATHENA_MACRO_INPUT_SCREENSHOT");
    if (!screenshot.isEmpty ())
      make_raster_image (url (screenshot.constData ()), rendered, 1.5);
  }

  void executionAndCursorEnvironment () {
    Environment context;
    edit_env env= context.env;
    tree t= source (), before= copy (t);
    QVERIFY (athena::contracts::editing_query {context.contract_state.contracts}.describe (en::label ()).layout.accepts (1));
    QVERIFY (athena::contracts::editing_query {context.contract_state.contracts}.accessible (t, 0));
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
    QCOMPARE (env->read (FONT_SERIES), tree ("bold"));
    env->exec (source ("theorem", false));
    QCOMPARE (env->exec (compound ("the-theorem")), tree ("2"));
    QCOMPARE (t, before);
  }

  void htmlExpansionAdvancesCounterWithoutEmittingAssignments () {
    Environment context;
    edit_env env= context.env;
    const tree original= source ("theorem", true);
    const tree before= copy (original);
    env->write ("html-export", "true");
    tree expanded= env->exec (original);
    QVERIFY (!has_tag (expanded, ASSIGN));
    QCOMPARE (env->exec (compound ("the-theorem")), tree ("1"));
    QCOMPARE (original, before);

    env->exec (source ("theorem", false));
    QCOMPARE (env->exec (compound ("the-theorem")), tree ("1"));
  }

  void selectiveBoldBodyCursorEnvironment () {
    struct Case { const char* kind; const char* series; };
    const Case cases[]= {
      {"theorem", "bold"}, {"proposition", "bold"},
      {"corollary", "bold"}, {"lemma", "bold"},
      {"axiom", "medium"}, {"conjecture", "medium"},
      {"proof", "medium"}, {"definition", "medium"}
    };
    for (const auto& item: cases)
      for (bool legacy: {false, true}) {
        Environment context;
        edit_env env= context.env;
        env->write (FONT_SERIES, "medium");
        tree t= source (item.kind, false);
        if (legacy) {
          auto converted= en::convert_detached_source (
            compound (item.kind, tree (DOCUMENT, "BodyToken")));
          QVERIFY (converted.diagnostics.empty ());
          QVERIFY (en::is_canonical (converted.source));
          t= converted.source;
        }
        env->exec_until (t, path (0, 0, 0));
        QVERIFY2 (env->read (FONT_SERIES) == tree (item.series),
                  qPrintable (QString ("%1 (%2)").arg (item.kind,
                    legacy ? "converted legacy" : "native")));
      }
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

  void proofTargetDoesNotTurnHeadingIntoLink () {
    Environment context;
    tree proof= source ("proof", false);
    auto metadata= *node::get (proof);
    const std::string target= "22222222-2222-4222-8222-222222222222";
    metadata.properties["target"]= node::property (node::reference {target});
    node::set (proof, metadata);
    tree plan= native_enunciation_macro (context.env.operator-> (), proof);
    QVERIFY (!has_tag (plan, HLINK));
    QVERIFY (has_tag (plan, ARG));
    const auto* linked= std::get_if<node::reference> (
      &node::get (proof)->properties.at ("target").data);
    QVERIFY (linked != nullptr);
    QCOMPARE (linked->id, target);
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

  void radioactiveDictionaryChangesPreserveNestedParagraphs () {
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    std::filesystem::path root (directory.path ().toStdString ());
    AthenaVaultfileInfo info;
    std::string error;
    QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());
    AthenaArtifactsBuildResult built;
    QVERIFY2 (athena_artifacts_build (root, {}, true, {}, built, error),
              error.c_str ());
    QCOMPARE (vault_load (url_system (root.string ().c_str ()), "test", "vault.sqlite"),
              string (""));
    struct CloseVault { ~CloseVault () { vault_close (); } } close;
    auto vault= vault_capture_context ();
    QVERIFY (vault != nullptr);
    auto record= [] (const char* id, const char* name) {
      AthenaArtifactRecord result;
      result.uuid= id;
      result.type= "definition";
      result.relative_path= "dictionary.ath";
      result.display_text= name;
      result.semantic_names= {name};
      return result;
    };
    std::vector<AthenaArtifactRecord> records {
      record ("spectral-theorem", "spectral theorem")};
    auto publish= [&] {
      athena_artifact_radioactive_overlay (
        vault->incarnation, "dictionary.ath", 7301, records);
    };
    publish ();

    Environment context;
    edit_env env= context.env;
    env->write ("athena-radioactive-links-suppressed", "false");
    env->write ("athena-radioactive-links-in-transclusion", "true");
    env->write (RADIOACTIVE_LINK_COLOR, "preserve");
    env->update ();
    tree theorem= source ("theorem", false);
    theorem[0]= tree (DOCUMENT, "AffectedToken spectral lemma",
                     "StableToken quiescent invariant");
    tree document (DOCUMENT, theorem);
    tree original= copy (document);
    typesetter ts= new_typesetter (env, document, path (0));
    ts->screen_tree= true;
    struct Delete { typesetter ts; ~Delete () { delete_typesetter (ts); } } cleanup {ts};
    SI x1= 0, y1= 0, x2= 0, y2= 0;
    auto render= [&] { return typeset (ts, x1, y1, x2, y2); };
    box first= render ();
    box affected= text_box_containing (first, "AffectedToken");
    box stable= text_box_containing (first, "StableToken");
    QVERIFY (!is_nil (affected));
    QVERIFY (!is_nil (stable));

    // The same queried first word is insufficient to invalidate cached boxes.
    records.push_back (record ("spectral-proposition", "spectral proposition"));
    publish ();
    QVERIFY (!refresh_radioactive_links (ts));
    QCOMPARE (ts->br->status & VALID_MASK, PROCESSED);
    box nonmatching= render ();
    box same_affected= text_box_containing (nonmatching, "AffectedToken");
    box same_stable= text_box_containing (nonmatching, "StableToken");
    QVERIFY (!is_nil (same_affected));
    QVERIFY (!is_nil (same_stable));
    QVERIFY (same_affected.operator-> () == affected.operator-> ());
    QVERIFY (same_stable.operator-> () == stable.operator-> ());

    records.push_back (record ("spectral-lemma", "spectral lemma"));
    publish ();
    QVERIFY (refresh_radioactive_links (ts));
    box changed= render ();
    box new_affected= text_box_containing (changed, "AffectedToken");
    box retained_stable= text_box_containing (changed, "StableToken");
    QVERIFY (!is_nil (new_affected));
    QVERIFY (!is_nil (retained_stable));
    QVERIFY (new_affected.operator-> () != affected.operator-> ());
    QVERIFY (retained_stable.operator-> () == stable.operator-> ());
    QVERIFY (has_link_target (new_affected, records[2]));
    QVERIFY (!refresh_radioactive_links (ts));

    // A later hit proves the untouched paragraph retained its dependencies.
    records.push_back (record ("quiescent-invariant", "quiescent invariant"));
    publish ();
    QVERIFY (refresh_radioactive_links (ts));
    box later= render ();
    box retained_affected= text_box_containing (later, "AffectedToken");
    box new_stable= text_box_containing (later, "StableToken");
    QVERIFY (!is_nil (retained_affected));
    QVERIFY (!is_nil (new_stable));
    QVERIFY (retained_affected.operator-> () == new_affected.operator-> ());
    QVERIFY (new_stable.operator-> () != stable.operator-> ());
    QVERIFY (has_link_target (retained_affected, records[2]));
    QVERIFY (has_link_target (new_stable, records[3]));
    QVERIFY (!refresh_radioactive_links (ts));
    QCOMPARE (ts->br->st, original);
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
      compound ("render-bold-theorem", "Exact original title", tree (ARG, "body"))));
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
