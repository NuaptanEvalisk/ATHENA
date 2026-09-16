
/******************************************************************************
* MODULE     : queryxml_test.cpp
* COPYRIGHT  : (C) 2019-2021  Darcy Shen
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>

#include "convert.hpp"
#include "drd_std.hpp"

class TestQueryXML: public QObject {
  Q_OBJECT

private slots:
  void test_find_first_element_by_name ();
  void test_parse_xml_length ();
  void test_svg_xml_roundtrip ();
};

void
TestQueryXML::test_find_first_element_by_name () {
  init_std_drd ();
  string svg_content= "<svg xmlns=\"http://www.w3.org/2000/svg\"\
class=\"railroad-diagram\" width=\"521\" height=\"110\" viewBox=\"0 0 521 110\">\
</svg>";
  tree t= parse_xml (svg_content);
  // print_tree (t);
  tree result= find_first_element_by_name (t, "svg");
  // print_tree (result);
  string width= get_attr_from_element (result, "width", "");
  string height= get_attr_from_element (result, "height", "");
  QCOMPARE (width, "521");
  QCOMPARE (height, "110");
}

void
TestQueryXML::test_parse_xml_length () {
  QCOMPARE (parse_xml_length ("10"), 8);
  QCOMPARE (parse_xml_length ("10px"), 8);
  QCOMPARE (parse_xml_length ("10pt"), 10);
}

void
TestQueryXML::test_svg_xml_roundtrip () {
  string source=
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<svg xmlns=\"http://www.w3.org/2000/svg\" "
    "xmlns:athena=\"https://www.texmacs.org/\" viewBox=\"0 0 10 10\">"
    "<g athena:texmacscode=\"A&amp;&quot;B\"><text>x &amp; y</text></g>"
    "</svg>";

  scheme_tree parsed= parse_xml (source);
  string serialized= serialize_xml (parsed);
  QVERIFY (occurs ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>", serialized));
  QVERIFY (occurs ("xmlns:athena=\"https://www.texmacs.org/\"", serialized));
  QVERIFY (occurs ("athena:texmacscode=\"A&amp;&quot;B\"", serialized));
  QVERIFY (occurs ("<text>x &amp; y</text>", serialized));

  tree reparsed= parse_xml (serialized);
  tree svg= find_first_element_by_name (reparsed, "svg");
  QVERIFY (is_tuple (svg));
  QCOMPARE (get_attr_from_element (svg, "viewBox", ""), "0 0 10 10");
}

QTEST_MAIN(TestQueryXML)
#include "queryxml_test.moc"
