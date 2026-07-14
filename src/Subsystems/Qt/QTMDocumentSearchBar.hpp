/******************************************************************************
* MODULE     : QTMDocumentSearchBar.hpp
* DESCRIPTION: Native in-document search UI
* COPYRIGHT  : (C) 2026 Nuaptan
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#ifndef ATHENA_QTMDOCUMENTSEARCHBAR_HPP
#define ATHENA_QTMDOCUMENTSEARCHBAR_HPP

#include "editor.hpp"

#include <QFrame>
#include <QIcon>
#include <QPointer>

class QCheckBox;
class QLabel;
class QLineEdit;
class QToolButton;
class QTMWidget;

class QTMDocumentSearchBar final: public QFrame {
public:
  explicit QTMDocumentSearchBar (QTMWidget* canvas);

  void open (editor ed);
  void closeSearch ();
  void navigate (bool forward, bool extreme= false);

  static void showForCurrentEditor ();
  static void navigateCurrent (bool forward);
  static void closeCurrent ();

protected:
  bool eventFilter (QObject* watched, QEvent* event) override;

private:
  QPointer<QTMWidget> canvas;
  editor searchEditor;
  QLineEdit* queryEdit;
  QCheckBox* caseSensitive;
  QLabel* resultLabel;

  void updateSearch ();
  void updateResultLabel ();
  void positionBar ();
  QToolButton* makeButton (const QIcon& icon, const QString& tooltip);
};

void document_search_open ();
void document_search_next (bool forward);
void document_search_close ();

#endif
