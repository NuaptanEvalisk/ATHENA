/******************************************************************************
* MODULE     : font_selector_preview_test.cpp
* DESCRIPTION: regression tests for native font selector previews
*******************************************************************************/

#include "Subsystems/Qt/QTMFontSelector.hpp"

#include <QtTest/QtTest>

#include <QComboBox>
#include <QFontDatabase>
#include <QLabel>
#include <QWidget>

class FontSelectorPreviewTest: public QObject {
  Q_OBJECT

private slots:
  void usesPhysicalFamiliesForPagellaPreviews ();
};

void
FontSelectorPreviewTest::usesPhysicalFamiliesForPagellaPreviews () {
  if (!QFontDatabase::families ().contains ("TeX Gyre Pagella") ||
      !QFontDatabase::families ().contains ("TeX Gyre Pagella Math"))
    QSKIP ("TeX Gyre Pagella fonts are not installed");

  QTMFontSelector selector ("TeX Gyre Pagella", "Regular", "10",
                            "TeX Gyre Pagella", "Font preview test", false);

  QWidget* main= selector.findChild<QWidget*> ("fontSelectorMainPreview");
  QWidget* cal= selector.findChild<QWidget*> ("fontSubfontPreview_cal");
  QWidget* frak= selector.findChild<QWidget*> ("fontSubfontPreview_frak");
  QComboBox* bold=
    selector.findChild<QComboBox*> ("fontSubfontSelector_bold");
  QVERIFY (main != nullptr);
  QVERIFY (cal != nullptr);
  QVERIFY (frak != nullptr);
  QVERIFY (bold != nullptr);

  // The previews are self-painted widgets rather than ordinary labels, so
  // application/UI font resynchronization cannot replace their content font.
  QVERIFY (qobject_cast<QLabel*> (main) == nullptr);
  QVERIFY (qobject_cast<QLabel*> (cal) == nullptr);
  QVERIFY (qobject_cast<QLabel*> (frak) == nullptr);
  QCOMPARE (main->property ("athenaPreviewFontFamily").toString (),
            QString ("TeX Gyre Pagella"));
  QCOMPARE (cal->property ("athenaPreviewFontFamily").toString (),
            QString ("TeX Gyre Pagella Math"));
  QCOMPARE (frak->property ("athenaPreviewFontFamily").toString (),
            QString ("TeX Gyre Pagella Math"));

  // Simulate the Wayland UI font resync that previously clobbered QLabel
  // previews.  The self-painted content font is stored independently.
  main->setFont (QApplication::font ());
  cal->setFont (QApplication::font ());
  frak->setFont (QApplication::font ());
  QCOMPARE (main->property ("athenaPreviewFontFamily").toString (),
            QString ("TeX Gyre Pagella"));
  QCOMPARE (cal->property ("athenaPreviewFontFamily").toString (),
            QString ("TeX Gyre Pagella Math"));
  QCOMPARE (frak->property ("athenaPreviewFontFamily").toString (),
            QString ("TeX Gyre Pagella Math"));

  QVERIFY (bold->count () > 1);
  for (int i=0; i<bold->count (); ++i)
    QVERIFY2 (!bold->itemText (i).trimmed ().isEmpty (),
              "Subfont selector contains a blank family entry");
}

QTEST_MAIN (FontSelectorPreviewTest)
#include "font_selector_preview_test.moc"
