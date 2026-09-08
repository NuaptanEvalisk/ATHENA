#include "QTMCompletionPopup.hpp"
#include <QLineEdit>
#include <QVBoxLayout>
#include <QtTest>

class CompletionPopupTest: public QObject {
  Q_OBJECT
private slots:
  void navigatesAcceptsAndCancels () {
    QWidget window;
    auto* layout= new QVBoxLayout (&window);
    auto* editor= new QLineEdit (&window);
    layout->addWidget (editor);
    window.resize (500, 300);
    window.show ();
    editor->setFocus ();
    QVERIFY (QTest::qWaitForWindowExposed (&window));
    QVector<QPair<std::uint64_t, int>> choices;
    QTMCompletionPopup popup (editor, [&] (auto session, int row) {
      choices.append ({session, row});
    });
    QStringList candidates {"alpha", "alpine", "algebra"};
    auto show= [&] (std::uint64_t session) {
      editor->setText ("al");
      editor->setFocus ();
      popup.present (session, candidates, editor->mapToGlobal (QPoint (0, editor->height ())), 0);
      QVERIFY (popup.isVisible ());
      QCOMPARE (editor->text (), QString ("al"));
    };
    show (1);
    if (!qEnvironmentVariableIsEmpty ("ATHENA_COMPLETION_SCREENSHOT"))
      QVERIFY (popup.grab ().save (qEnvironmentVariable ("ATHENA_COMPLETION_SCREENSHOT")));
    QTest::keyClick (editor, Qt::Key_Down);
    QCOMPARE (popup.currentRow (), 1);
    QCOMPARE (editor->text (), QString ("al"));
    QTest::keyClick (editor, Qt::Key_Tab);
    QCOMPARE (choices.size (), 1);
    QCOMPARE (choices.back ().first, std::uint64_t (1));
    QCOMPARE (choices.back ().second, 1);
    QVERIFY (!popup.isVisible ());
    show (2);
    QTest::keyClick (editor, Qt::Key_Return);
    QCOMPARE (choices.back ().second, 0);
    show (3);
    QTest::keyClick (editor, Qt::Key_Escape);
    QCOMPARE (choices.back ().second, -1);
    QCOMPARE (editor->text (), QString ("al"));
    show (4);
    popup.dismiss (3);
    QVERIFY (popup.isVisible ());
    popup.select (3, 2);
    QCOMPARE (popup.currentRow (), 0);
    QTest::keyClick (editor, Qt::Key_X);
    QCOMPARE (choices.back ().second, -1);
    QVERIFY (editor->text ().contains ('x'));
    show (5);
    QRect item= popup.visualItemRect (popup.item (2));
    QTest::mouseClick (popup.viewport (), Qt::LeftButton, Qt::NoModifier, item.center ());
    QCoreApplication::processEvents ();
    QCOMPARE (choices.back ().first, std::uint64_t (5));
    QCOMPARE (choices.back ().second, 2);
    QCOMPARE (choices.size (), 5);
    show (6);
    popup.hide ();
    QCoreApplication::processEvents ();
    QCOMPARE (choices.back ().second, -1);
  }
};

QTEST_MAIN (CompletionPopupTest)
#include "completion_popup_test.moc"
