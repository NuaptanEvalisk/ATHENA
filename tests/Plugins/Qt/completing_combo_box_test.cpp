/******************************************************************************
* MODULE     : completing_combo_box_test.cpp
* DESCRIPTION: Tests for explicit editable combo completion commits
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "Qt/QTMCompletingComboBox.hpp"

#include <QLineEdit>
#include <QShortcut>
#include <QtTest/QtTest>

class TestCompletingComboBox: public QObject {
  Q_OBJECT

private slots:
  void tabCommitsCompletion ();
  void returnCommitsCompletion ();
  void typedContainsCompletionCommitsOnReturn ();
  void returnCompletionOverridesParentShortcut ();
  void tabWithoutCompletionRemainsNavigation ();
  void containsPopupCompletionUsesWholeModelEntry ();
  void listCompletionCommitsSelectedItem ();
  void listCompletionRejectsAlreadyCommittedItem ();
};

static QTMCompletingComboBox*
makeCombo (QWidget* parent) {
  QTMCompletingComboBox* combo= new QTMCompletingComboBox (parent);
  combo->setInsertPolicy (QComboBox::NoInsert);
  combo->addItems ({"", "Algebra", "Universe", "Universal properties"});
  combo->completer ()->setCaseSensitivity (Qt::CaseInsensitive);
  combo->completer ()->setFilterMode (Qt::MatchContains);
  return combo;
}

void
TestCompletingComboBox::tabCommitsCompletion () {
  QWidget host;
  QTMCompletingComboBox* combo= makeCombo (&host);
  host.show ();
  combo->setEditText ("verse");
  combo->completer ()->setCompletionPrefix ("verse");
  QVERIFY (combo->completer ()->setCurrentRow (0));
  combo->completer ()->complete ();
  QCoreApplication::processEvents ();

  QTest::keyClick (combo->lineEdit (), Qt::Key_Tab);
  QCOMPARE (combo->currentText (), QString ("Universe"));
  QVERIFY (!combo->lineEdit ()->hasSelectedText ());
}

void
TestCompletingComboBox::returnCommitsCompletion () {
  QWidget host;
  QTMCompletingComboBox* combo= makeCombo (&host);
  host.show ();
  combo->setEditText ("properties");
  combo->completer ()->setCompletionPrefix ("properties");
  QVERIFY (combo->completer ()->setCurrentRow (0));
  combo->completer ()->complete ();
  QCoreApplication::processEvents ();

  QTest::keyClick (combo->lineEdit (), Qt::Key_Return);
  QCOMPARE (combo->currentText (), QString ("Universal properties"));
  QVERIFY (!combo->lineEdit ()->hasSelectedText ());
}

void
TestCompletingComboBox::typedContainsCompletionCommitsOnReturn () {
  QWidget host;
  QTMCompletingComboBox* combo= new QTMCompletingComboBox (&host);
  const QString expected= "Book - Introduction to Manifolds (Tu)";
  combo->addItems ({"", expected, "Book - Introduction to Algebra"});
  combo->completer ()->setCaseSensitivity (Qt::CaseInsensitive);
  combo->completer ()->setFilterMode (Qt::MatchContains);
  int shortcutActivations= 0;
  QShortcut shortcut (QKeySequence (Qt::Key_Return), &host);
  shortcut.setContext (Qt::WidgetWithChildrenShortcut);
  connect (&shortcut, &QShortcut::activated,
           [&] () { shortcutActivations++; });
  host.show ();
  combo->setFocus ();
  QCoreApplication::processEvents ();

  QTest::keyClicks (combo->lineEdit (), "mani");
  QCoreApplication::processEvents ();
  QCOMPARE (combo->currentText (), QString ("mani"));
  QVERIFY (combo->completer ()->popup () != nullptr);
  QVERIFY (combo->completer ()->popup ()->isVisible ());
  QTest::keyClick (combo->lineEdit (), Qt::Key_Return);
  QCOMPARE (combo->currentText (), expected);
  QCOMPARE (shortcutActivations, 0);
}

void
TestCompletingComboBox::returnCompletionOverridesParentShortcut () {
  QWidget host;
  QTMCompletingComboBox* combo= makeCombo (&host);
  int shortcutActivations= 0;
  QShortcut shortcut (QKeySequence (Qt::Key_Return), &host);
  shortcut.setContext (Qt::WidgetWithChildrenShortcut);
  connect (&shortcut, &QShortcut::activated,
           [&] () { shortcutActivations++; });
  host.show ();
  combo->setEditText ("gebra");
  combo->completer ()->setCompletionPrefix ("gebra");
  QVERIFY (combo->completer ()->setCurrentRow (0));
  combo->completer ()->complete ();
  combo->setFocus ();
  QCoreApplication::processEvents ();

  QTest::keyClick (combo->lineEdit (), Qt::Key_Return);
  QCOMPARE (combo->currentText (), QString ("Algebra"));
  QCOMPARE (shortcutActivations, 0);
}

void
TestCompletingComboBox::tabWithoutCompletionRemainsNavigation () {
  QWidget host;
  QTMCompletingComboBox* combo= makeCombo (&host);
  QLineEdit* next= new QLineEdit (&host);
  combo->setEditText ("missing");
  combo->completer ()->setCompletionPrefix ("missing");
  combo->setFocus ();
  host.setTabOrder (combo, next);
  host.show ();
  QCoreApplication::processEvents ();

  QTest::keyClick (combo->lineEdit (), Qt::Key_Tab);
  QCOMPARE (combo->currentText (), QString ("missing"));
  QVERIFY (next->hasFocus ());
}

void
TestCompletingComboBox::containsPopupCompletionUsesWholeModelEntry () {
  QWidget host;
  QTMCompletingComboBox* combo= new QTMCompletingComboBox (&host);
  const QString expected= "Book - Introduction to Manifolds (Tu)";
  combo->addItems ({"", expected, "Book - Introduction to Algebra"});
  combo->completer ()->setCaseSensitivity (Qt::CaseInsensitive);
  combo->completer ()->setFilterMode (Qt::MatchContains);
  host.show ();
  combo->setFocus ();
  QCoreApplication::processEvents ();

  QTest::keyClicks (combo->lineEdit (), "mani");
  QCoreApplication::processEvents ();
  QCOMPARE (combo->currentText (), QString ("mani"));
  QVERIFY (combo->completer ()->popup () != nullptr);
  QVERIFY (combo->completer ()->popup ()->isVisible ());
  QTest::keyClick (combo->completer ()->popup (), Qt::Key_Tab);
  QCOMPARE (combo->currentText (), expected);
}

void
TestCompletingComboBox::listCompletionCommitsSelectedItem () {
  QLineEdit edit;
  QListWidget list;
  QListWidgetItem* first= new QListWidgetItem ("Notes/Algebra.ath", &list);
  first->setData (Qt::UserRole, "Notes/Algebra");
  QListWidgetItem* second=
    new QListWidgetItem ("Notes/Analysis.ath", &list);
  second->setData (Qt::UserRole, "Notes/Analysis");
  list.setCurrentItem (second);
  edit.setText ("anal");

  QVERIFY (qtm_commit_list_completion (&edit, &list, Qt::UserRole));
  QCOMPARE (edit.text (), QString ("Notes/Analysis"));
  QCOMPARE (edit.cursorPosition (), edit.text ().size ());
}

void
TestCompletingComboBox::listCompletionRejectsAlreadyCommittedItem () {
  QLineEdit edit;
  QListWidget list;
  QListWidgetItem* item= new QListWidgetItem ("Notes/Algebra.ath", &list);
  item->setData (Qt::UserRole, "Notes/Algebra");
  list.setCurrentItem (item);
  edit.setText ("Notes/Algebra");

  QVERIFY (!qtm_commit_list_completion (&edit, &list, Qt::UserRole));
  QCOMPARE (edit.text (), QString ("Notes/Algebra"));
}

QTEST_MAIN (TestCompletingComboBox)
#include "completing_combo_box_test.moc"
