/******************************************************************************
* MODULE     : namespaces_template_test.cpp
* DESCRIPTION: Tests for namespace filename template field validation
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************/

#include "ATHENA/Data/namespaces.hpp"

#include <QtTest/QtTest>

#include <string>

class NamespacesTemplateTest: public QObject {
  Q_OBJECT

private slots:
  void acceptsValidFieldValues ();
  void rejectsInvalidFieldValues ();
};

void
NamespacesTemplateTest::acceptsValidFieldValues () {
  athena_namespace_definition ns;
  ns.templ= "%s-%w-%c-%d-%N-%R";
  array<string> values;
  values << string ("title") << string ("word") << string ("x")
         << string ("-2") << string ("2") << string ("XIV");
  string stem;
  string error;
  QVERIFY2 (athena_namespace_build_stem (ns, values, stem, error),
            as_charp (error));
  QCOMPARE (stem, string ("title-word-x--2-2-XIV"));
}

void
NamespacesTemplateTest::rejectsInvalidFieldValues () {
  struct InvalidValue {
    const char* placeholder;
    const char* value;
  };
  const InvalidValue invalid[]= {
    {"%s", ""},       {"%w", "two words"}, {"%c", "ab"},
    {"%d", "2a"},     {"%N", "0"},         {"%R", "2"},
  };

  for (const InvalidValue& test: invalid) {
    athena_namespace_definition ns;
    ns.templ= test.placeholder;
    array<string> values;
    values << string (test.value);
    string stem;
    string error;
    QVERIFY (!athena_namespace_build_stem (ns, values, stem, error));
    QVERIFY2 (std::string (as_charp (error)).find (test.placeholder) !=
                std::string::npos,
              as_charp (error));
  }
}

QTEST_MAIN (NamespacesTemplateTest)
#include "namespaces_template_test.moc"
