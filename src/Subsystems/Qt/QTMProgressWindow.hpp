/******************************************************************************
 * MODULE     : QTMProgressWindow.hpp
 * DESCRIPTION: Minimal progress window shared by startup and blocking work
 * COPYRIGHT  : (C) 2026  Felix Evalisk
 ******************************************************************************
 * This software falls under the GNU general public license version 3 or later.
 * It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
 * in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
 ******************************************************************************/

#ifndef QTMPROGRESSWINDOW_HPP
#define QTMPROGRESSWINDOW_HPP

#include <QWidget>

class QLabel;
class QProgressBar;

class QTMProgressWindow final: public QWidget {
public:
  explicit QTMProgressWindow (const QString& title,
                              bool busy= false,
                              QWidget* parent= nullptr);

  void setMessage (const QString& message);
  void setProgress (int progress);
  void setBusy (bool busy);
  void centerOnScreen ();
  void centerOn (QWidget* widget);

private:
  QLabel* messageLabel;
  QProgressBar* progressBar;
  int retainedHeight;
};

#endif // QTMPROGRESSWINDOW_HPP
