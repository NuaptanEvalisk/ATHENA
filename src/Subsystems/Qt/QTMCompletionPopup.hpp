/******************************************************************************
* MODULE     : QTMCompletionPopup.hpp
* DESCRIPTION: KDE completion list for actor-owned document input
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#ifndef QTM_COMPLETION_POPUP_HPP
#define QTM_COMPLETION_POPUP_HPP

#include <KCompletionBox>
#include <cstdint>
#include <functional>

class QTMCompletionPopup final: public KCompletionBox {
public:
  using Choice= std::function<void(std::uint64_t, int)>;
  QTMCompletionPopup (QWidget* editor, Choice choice);
  void present (std::uint64_t session, const QStringList& items,
                QPoint anchor, int selected);
  void select (std::uint64_t session, int row);
  void dismiss (std::uint64_t session);
  void cancel ();

protected:
  bool eventFilter (QObject* object, QEvent* event) override;
  void hideEvent (QHideEvent* event) override;
  QPoint globalPositionHint () const override;

private:
  QWidget* editor_;
  Choice choice_;
  std::uint64_t session_= 0;
  QPoint anchor_;
  void finish (int row);
};

#endif
