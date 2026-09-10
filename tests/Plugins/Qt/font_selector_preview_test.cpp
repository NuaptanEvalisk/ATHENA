/******************************************************************************
* MODULE     : font_selector_preview_test.cpp
* DESCRIPTION: regression tests for native font selector previews
*******************************************************************************/

#include "Subsystems/Qt/QTMFontSelector.hpp"

#include <QtTest/QtTest>

#include <QComboBox>
#include <QDialog>
#include <QFontDatabase>
#include <QLabel>
#include <QThread>
#include <QTimer>
#include <QWidget>

#include <atomic>

namespace {

std::atomic<bool> affinityWarning {false};
QtMessageHandler previousHandler= nullptr;

void
captureMessage (QtMsgType type, const QMessageLogContext& context,
                const QString& message) {
  if (message.contains ("another thread") ||
      message.contains ("different thread") ||
      message.contains ("event dispatcher has already been destroyed") ||
      message.contains ("beginResetModel called"))
    affinityWarning.store (true);
  if (previousHandler) previousHandler (type, context, message);
}

class FontSelectorWorker: public QThread {
public:
  array<string> result;

protected:
  void run () override {
    result= native_font_selector_dialog (
      "TeX Gyre Pagella", "Regular", "10", "TeX Gyre Pagella",
      "Font selector thread test");
  }
};

} // namespace

class FontSelectorPreviewTest: public QObject {
  Q_OBJECT

private slots:
  void initTestCase ();
  void cleanupTestCase ();
  void init ();
  void usesPhysicalFamiliesForPagellaPreviews ();
  void nativeDialogRunsOnGuiThreadFromWorker ();
};

void
FontSelectorPreviewTest::initTestCase () {
  previousHandler= qInstallMessageHandler (captureMessage);
}

void
FontSelectorPreviewTest::cleanupTestCase () {
  qInstallMessageHandler (previousHandler);
}

void
FontSelectorPreviewTest::init () {
  affinityWarning.store (false);
}

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

void
FontSelectorPreviewTest::nativeDialogRunsOnGuiThreadFromWorker () {
  FontSelectorWorker worker;
  bool sawDialog= false;

  QTimer closer;
  connect (&closer, &QTimer::timeout, this, [&] {
    auto* dialog= qobject_cast<QDialog*> (QApplication::activeModalWidget ());
    if (dialog == nullptr) return;
    closer.stop ();
    sawDialog= true;
    QCOMPARE (dialog->thread (), qApp->thread ());
    dialog->reject ();
  });
  closer.start (10);

  worker.start ();
  QTRY_VERIFY_WITH_TIMEOUT (worker.isFinished (), 4000);
  closer.stop ();
  worker.wait ();

  QVERIFY (sawDialog);
  QCOMPARE (N(worker.result), 0);
  QVERIFY (!affinityWarning.load ());
}

QTEST_MAIN (FontSelectorPreviewTest)
#include "font_selector_preview_test.moc"
