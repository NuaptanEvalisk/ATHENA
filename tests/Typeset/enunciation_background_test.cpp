/******************************************************************************
* MODULE     : enunciation_background_test.cpp
* DESCRIPTION: Tests for incremental enunciation background injection
*******************************************************************************/

#include "enunciation_surround.hpp"
#include <QtTest/QtTest>

class TestEnunciationBackground: public QObject {
  Q_OBJECT

private slots:
  void injectsColorIntoDedicatedPrimitive ();
  void buildsDedicatedFallbackWrapper ();
  void leavesSourceDefinitionUnchanged ();
};

static tree
default_render_body () {
  return tree (
    COMPOUND, tree ("padded*"),
    tree (COMPOUND, tree ("enunciation-surround"),
          tree (ARG, "which"), tree (COMPOUND, tree ("yes-indent*")),
          tree (ARG, "body")));
}

void
TestEnunciationBackground::buildsDedicatedFallbackWrapper () {
  tree wrapper (COMPOUND, tree ("athena-enunciation-background"),
                tree ("#ddeeff"), tree ("custom theorem body"));
  QVERIFY (athena_is_enunciation_background (wrapper));

  tree rendered= athena_enunciation_background_render_rewrite (wrapper);
  QVERIFY (is_func (rendered, WITH));
  QVERIFY (is_func (rendered[N(rendered)-1], ORNAMENT));
  QCOMPARE (rendered[N(rendered)-1][0]->label,
            string ("custom theorem body"));
}

void
TestEnunciationBackground::injectsColorIntoDedicatedPrimitive () {
  tree source= default_render_body ();
  bool found= false;
  tree colored= athena_set_enunciation_surround_color (
    source, tree ("#ffccdd"), found);

  QVERIFY (found);
  tree primitive= colored[1];
  QVERIFY (athena_is_enunciation_surround (primitive));
  QVERIFY (athena_enunciation_surround_has_color (primitive));
  QCOMPARE (primitive[4]->label, string ("#ffccdd"));
  QVERIFY (!is_func (colored, ORNAMENT));
  QVERIFY (!is_func (primitive, ORNAMENT));
}

void
TestEnunciationBackground::leavesSourceDefinitionUnchanged () {
  tree source= default_render_body ();
  tree before= copy (source);
  bool found= false;
  (void) athena_set_enunciation_surround_color (
    source, tree ("#ffccdd"), found);

  QVERIFY (found);
  QVERIFY (source == before);
  QVERIFY (!athena_enunciation_surround_has_color (source[1]));
}

QTEST_MAIN(TestEnunciationBackground)
#include "enunciation_background_test.moc"
