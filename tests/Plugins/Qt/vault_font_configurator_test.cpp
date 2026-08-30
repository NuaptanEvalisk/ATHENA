#include "Subsystems/Qt/QTMVaultFontConfigurator.hpp"
#include "convert.hpp"

#include <QtTest/QtTest>

class VaultFontConfiguratorTest: public QObject {
  Q_OBJECT

private slots:
  void rewritesProfileWithoutDisturbingOtherMetadata ();
};

static tree
initialValue (tree document, string key) {
  tree initial= extract (document, "initial");
  for (int i=0; i<N(initial); ++i)
    if (is_func (initial[i], ASSOCIATE, 2) &&
        initial[i][0] == key)
      return initial[i][1];
  return tree (UNINIT);
}

void
VaultFontConfiguratorTest::rewritesProfileWithoutDisturbingOtherMetadata () {
  tree styles (TUPLE, "generic", "pagella-font", "custom-package");
  tree initial (COLLECTION);
  initial << tree (ASSOCIATE, "font", "pagella")
          << tree (ASSOCIATE, "font-family", "ss")
          << tree (ASSOCIATE, "language", "english")
          << tree (ASSOCIATE, "page-medium", "paper");
  tree document (DOCUMENT);
  document << compound ("TeXmacs", "2.1.4")
           << compound ("style", styles)
           << compound ("body", tree (DOCUMENT, "Content"))
           << compound ("initial", initial);

  string profile=
    "bold cjk=Noto Serif CJK SC,italic cjk=Noto Serif CJK SC,"
    "cjk=Noto Serif CJK SC,typewriter=JetBrains Mono,"
    "math=STIX Two Math,libertine";
  tree rewritten= athena_document_with_font_profile (document, profile);

  QCOMPARE (initialValue (rewritten, "font"), tree (profile));
  QCOMPARE (initialValue (rewritten, "font-family"), tree ("rm"));
  QCOMPARE (initialValue (rewritten, "language"), tree ("english"));
  QCOMPARE (initialValue (rewritten, "page-medium"), tree ("paper"));

  tree rewrittenStyles= extract (rewritten, "style");
  QCOMPARE (N(rewrittenStyles), 2);
  QCOMPARE (rewrittenStyles[0], tree ("generic"));
  QCOMPARE (rewrittenStyles[1], tree ("custom-package"));
  QCOMPARE (extract (rewritten, "body"), tree (DOCUMENT, "Content"));
}

QTEST_MAIN (VaultFontConfiguratorTest)
#include "vault_font_configurator_test.moc"
