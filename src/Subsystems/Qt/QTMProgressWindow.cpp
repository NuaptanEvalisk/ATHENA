/******************************************************************************
 * MODULE     : QTMProgressWindow.cpp
 * DESCRIPTION: Minimal progress window shared by startup and blocking work
 * COPYRIGHT  : (C) 2026  Felix Evalisk
 ******************************************************************************
 * This software falls under the GNU general public license version 3 or later.
 * It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
 * in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
 ******************************************************************************/

#include "QTMProgressWindow.hpp"

#include <QApplication>
#include <QGuiApplication>
#include <QLabel>
#include <QProgressBar>
#include <QScreen>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int progress_window_width= 420;

Qt::WindowFlags
progress_window_flags (QWidget* parent) {
  Qt::WindowFlags kind= parent == nullptr ? Qt::Window : Qt::Dialog;
  return kind | Qt::CustomizeWindowHint | Qt::WindowTitleHint;
}

}

QTMProgressWindow::QTMProgressWindow (const QString& title, bool busy,
                                      QWidget* parent):
  QWidget (parent, progress_window_flags (parent)),
  messageLabel (new QLabel (this)),
  progressBar (new QProgressBar (this)),
  retainedHeight (0) {
  setWindowTitle (title);
  setFixedWidth (progress_window_width);

  messageLabel->setWordWrap (true);
  messageLabel->setAlignment (Qt::AlignLeft | Qt::AlignVCenter);
  messageLabel->setSizePolicy (QSizePolicy::Preferred, QSizePolicy::Minimum);

  progressBar->setMinimumHeight (progressBar->sizeHint ().height ());
  setBusy (busy);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->setContentsMargins (20, 18, 20, 18);
  layout->setSpacing (12);
  layout->addWidget (messageLabel);
  layout->addWidget (progressBar);
  setLayout (layout);
}

void
QTMProgressWindow::setMessage (const QString& message) {
  messageLabel->setText (message);
  messageLabel->setToolTip (message);
  adjustSize ();
  setFixedWidth (progress_window_width);
  retainedHeight= std::max (retainedHeight, height ());
  setMinimumHeight (retainedHeight);
  if (height () < retainedHeight) resize (width (), retainedHeight);
}

void
QTMProgressWindow::setProgress (int progress) {
  progressBar->setRange (0, 100);
  progressBar->setValue (std::clamp (progress, 0, 100));
  progressBar->setTextVisible (true);
}

void
QTMProgressWindow::setBusy (bool busy) {
  if (busy) {
    progressBar->setRange (0, 0);
    progressBar->setTextVisible (false);
  }
  else setProgress (0);
}

void
QTMProgressWindow::centerOnScreen () {
  QScreen* screen= QGuiApplication::primaryScreen ();
  if (screen == nullptr) return;
  QRect available= screen->availableGeometry ();
  move (available.center () - rect ().center ());
}

void
QTMProgressWindow::centerOn (QWidget* widget) {
  if (widget == nullptr) {
    centerOnScreen ();
    return;
  }
  QWidget* top= widget->window ();
  move (top->frameGeometry ().center () - rect ().center ());
}
