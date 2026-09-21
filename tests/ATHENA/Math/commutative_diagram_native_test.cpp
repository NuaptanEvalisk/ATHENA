/******************************************************************************
* MODULE     : commutative_diagram_native_test.cpp
* DESCRIPTION: Native commutative diagram geometry regressions
*******************************************************************************
*/

#include <QtTest/QtTest>
#include <cmath>

#include "ATHENA/Math/commutative_diagram_native.hpp"

namespace {

tree vertex (const char* id, const char* x, const char* y) {
  tree result (make_tree_label ("cd-vertex"), 4);
  result[0]= id;
  result[1]= x;
  result[2]= y;
  result[3]= compound ("math", tree (id));
  return result;
}

tree arrow (const char* id, const char* source, const char* target,
            tree options) {
  tree result (make_tree_label ("cd-arrow"), 5);
  result[0]= id;
  result[1]= source;
  result[2]= target;
  result[3]= compound ("math", tree (""));
  result[4]= options;
  return result;
}

tree sample_body (double offset= 0.0, double curve= 2.0) {
  tree options= cd_default_arrow_options ();
  tree a= arrow ("f", "a", "b", options);
  a[4]= cd_options_with (a, "offset", as_string (offset));
  a[4]= cd_options_with (a, "curve", as_string (curve));
  tree body (make_tree_label ("cd-body"), 4);
  body[0]= "";
  body[1]= vertex ("a", "-2.5", "-1.5");
  body[2]= vertex ("b", "2.5", "1.5");
  body[3]= a;
  return body;
}

bool near (double a, double b, double epsilon= 1.0e-8) {
  return std::fabs (a-b) <= epsilon;
}

} // namespace

class TestNativeCommutativeDiagram: public QObject {
  Q_OBJECT
private slots:
  void offsetMovesWholeBezierTransversely ();
  void hitTestingUsesNativeGeometry ();
  void loopGeometryAndOptionsStayNative ();
  void sessionStateIsEphemeral ();
};

void
TestNativeCommutativeDiagram::offsetMovesWholeBezierTransversely () {
  tree baseline_body= sample_body (0.0, 2.0);
  tree shifted_body= sample_body (3.0, 2.0);
  cd_geometry baseline, shifted;
  QVERIFY (cd_arrow_geometry (baseline_body, baseline_body[3], baseline));
  QVERIFY (cd_arrow_geometry (shifted_body, shifted_body[3], shifted));

  double length= std::sqrt (34.0);
  cd_point expected (-3.0/length*0.24, 5.0/length*0.24);
  for (int i=0; i<4; ++i) {
    QVERIFY (near (shifted.p[i].x-baseline.p[i].x, expected.x));
    QVERIFY (near (shifted.p[i].y-baseline.p[i].y, expected.y));
  }
}

void
TestNativeCommutativeDiagram::hitTestingUsesNativeGeometry () {
  tree body= sample_body (2.0, 3.0);
  cd_geometry geometry;
  QVERIFY (cd_arrow_geometry (body, body[3], geometry));
  cd_point middle= cd_bezier_point (geometry, 0.5);
  int index= -1;
  tree found= cd_nearest_arrow (body, middle, 0.01, &index);
  QCOMPARE (index, 3);
  QCOMPARE (cd_arrow_id (found), string ("f"));

  auto session= cd_begin_session (body, path (0));
  cd_select (session, "arrow", "f");
  cd_hit handle= cd_hit_test (body, session, geometry.p[0]);
  QCOMPARE (handle.kind, string ("handle"));
  QCOMPARE (handle.part, string ("source"));
}

void
TestNativeCommutativeDiagram::loopGeometryAndOptionsStayNative () {
  tree body (make_tree_label ("cd-body"), 3);
  body[0]= "";
  body[1]= vertex ("a", "1", "2");
  tree loop= arrow ("loop", "a", "a", cd_default_arrow_options ());
  loop[4]= cd_options_with (loop, "loop-angle", "90");
  loop[4]= cd_options_with (loop, "loop-radius", "4");
  body[2]= loop;

  cd_geometry geometry;
  QVERIFY (cd_arrow_geometry (body, body[2], geometry));
  QVERIFY (!near (geometry.p[0].x, geometry.p[3].x) ||
           !near (geometry.p[0].y, geometry.p[3].y));
  QCOMPARE (cd_option (body[2], "loop-angle", ""), string ("90"));
  QCOMPARE (cd_option (body[2], "missing", "fallback"), string ("fallback"));
}

void
TestNativeCommutativeDiagram::sessionStateIsEphemeral () {
  tree body= sample_body ();
  tree before= copy (body);
  auto session= cd_begin_session (body, path (4, 2));
  cd_select (session, "vertex", "a");
  cd_set_hover (session, "arrow", "f");
  session->has_drag_point= true;
  session->drag_point= cd_point (4.0, 1.0);
  QVERIFY (body == before);
  auto looked_up= cd_lookup_session (body);
  QVERIFY (looked_up != nullptr);
  QCOMPARE (looked_up->selected_id, string ("a"));
  QCOMPARE (looked_up->hover_id, string ("f"));
}

QTEST_MAIN (TestNativeCommutativeDiagram)
#include "commutative_diagram_native_test.moc"
