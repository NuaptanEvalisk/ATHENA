/******************************************************************************
* MODULE     : QTMAudmapTerminal.hpp
* DESCRIPTION: PTY-backed AUDMAP REPL terminal and restart controls
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include <QWidget>
#include <QPointer>
#include <QTemporaryDir>
#include <memory>

class QTermWidget;
class QLabel;
class QToolButton;
class QVBoxLayout;

class QTMAudmapTerminal final: public QWidget {
  QString executable_, endpoint_;
  std::unique_ptr<QTemporaryDir> identity_directory_;
  QPointer<QTermWidget> terminal_;
  QLabel* status_;
  QToolButton* restart_;
  QVBoxLayout* layout_;
  void start ();
public:
  QTMAudmapTerminal (QString executable, QString endpoint,
                    QWidget* parent= nullptr);
  ~QTMAudmapTerminal () override;
};
