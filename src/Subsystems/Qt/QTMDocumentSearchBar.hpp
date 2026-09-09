/******************************************************************************
* MODULE     : QTMDocumentSearchBar.hpp
* DESCRIPTION: Native in-document search UI
* COPYRIGHT  : (C) 2026 Nuaptan
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#ifndef ATHENA_QTMDOCUMENTSEARCHBAR_HPP
#define ATHENA_QTMDOCUMENTSEARCHBAR_HPP

#include "actor_transport.hpp"
#include <deque>

#include <QFrame>
#include <QIcon>
#include <QPointer>

class QCheckBox;
class QLabel;
class QLineEdit;
class QToolButton;
class QTimer;
class QTMWidget;

class QTMDocumentSearchBar final: public QFrame {
public:
  explicit QTMDocumentSearchBar (QTMWidget* canvas);

  void open (bool replace= false);
  void closeSearch ();
  void navigate (bool forward, bool extreme= false);

  static void showForCurrentEditor (bool replace= false);
  static void navigateCurrent (bool forward);
  static void closeCurrent ();
  static void acceptState (QTMWidget* canvas, athena_view_id view,
                           std::uint64_t generation, int current, int total,
                           int replacementStatus);

protected:
  bool eventFilter (QObject* watched, QEvent* event) override;

private:
  QPointer<QTMWidget> canvas;
  athena_actor_id actorId= ATHENA_NO_ACTOR;
  athena_view_id viewId= ATHENA_NO_VIEW;
  std::uint64_t generation= 0;
  std::uint64_t inFlight= 0;
  struct Pending {
    actor_command_kind kind;
    std::uint64_t generation;
    QString text;
    bool flag1= false;
    bool flag2= false;
  };
  std::deque<Pending> pending;
  QTimer* dispatchTimer;
  int currentResult= 0;
  int totalResults= 0;
  QLineEdit* queryEdit;
  QCheckBox* caseSensitive;
  QLabel* resultLabel;
  QWidget* replaceRow;
  QLineEdit* replacementEdit;
  QLabel* replacementLabel;
  QToolButton* replaceOne;
  QToolButton* replaceAll;

  void updateSearch ();
  void replaceMatches (bool all);
  void dispatchPending ();
  void updateResultLabel ();
  void positionBar ();
  QToolButton* makeButton (const QIcon& icon, const QString& tooltip);
};

void document_search_open ();
void document_replace_open ();
void document_search_next (bool forward);
void document_search_close ();

#endif
