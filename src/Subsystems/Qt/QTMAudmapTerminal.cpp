/******************************************************************************
* MODULE     : QTMAudmapTerminal.cpp
* DESCRIPTION: GUI-owned QTermWidget session for the standalone AUDMAP client
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "QTMAudmapTerminal.hpp"
#include <qtermwidget.h>
#include <QApplication>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QProcessEnvironment>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

QTMAudmapTerminal::QTMAudmapTerminal (QString executable, QString endpoint,
    QWidget* parent): QWidget (parent),
    executable_ (std::move (executable)), endpoint_ (std::move (endpoint)) {
  Q_ASSERT (QThread::currentThread () == qApp->thread ());
  setObjectName ("athena-audmap-terminal");
  setProperty ("athenaOwnsKeyInput", true);
  layout_= new QVBoxLayout (this);
  layout_->setContentsMargins (0, 0, 0, 0);
  auto* toolbar= new QHBoxLayout;
  status_= new QLabel (this);
  status_->setObjectName ("audmap-repl-status");
  status_->setTextFormat (Qt::PlainText);
  status_->setWordWrap (true);
  toolbar->addWidget (status_, 1);
  restart_= new QToolButton (this);
  restart_->setObjectName ("audmap-repl-restart");
  restart_->setIcon (QIcon::fromTheme ("view-refresh"));
  restart_->setToolTip ("Restart AUDMAP REPL");
  restart_->setAccessibleName (restart_->toolTip ());
  toolbar->addWidget (restart_);
  layout_->addLayout (toolbar);
  connect (restart_, &QToolButton::clicked, this, [this] { start (); });
  start ();
}

QTMAudmapTerminal::~QTMAudmapTerminal () {
  delete terminal_.data ();
}

void QTMAudmapTerminal::start () {
  // A force-closed client may still have tickets until the server timeout.
  // Use a fresh key so a restarted REPL cannot accidentally reuse those IDs.
  delete terminal_.data ();
  identity_directory_= std::make_unique<QTemporaryDir> ();
  if (!identity_directory_->isValid ()) {
    status_->setText ("Cannot create a private REPL identity directory.");
    return;
  }
  if (!QFileInfo (executable_).isExecutable () || !QFileInfo (endpoint_).isFile ()) {
    status_->setText ("AUDMAP REPL executable or current instance endpoint is unavailable.");
    return;
  }
  auto* terminal= new QTermWidget (0, this);
  terminal_= terminal;
  terminal->setObjectName ("audmap-repl-pty");
  terminal->setTerminalFont (QFontDatabase::systemFont (QFontDatabase::FixedFont));
  terminal->setHistorySize (5000);
  terminal->setScrollBarPosition (QTermWidget::ScrollBarRight);
  terminal->setFlowControlEnabled (false);
  terminal->setAutoClose (true);
  terminal->setEnvironment (QProcessEnvironment::systemEnvironment ().toStringList ());
  terminal->setWorkingDirectory (QFileInfo (executable_).absolutePath ());
  terminal->setShellProgram (executable_);
  terminal->setArgs ({"--endpoint", endpoint_, "--identity", identity_directory_->filePath ("key.json")});
  layout_->addWidget (terminal, 1);
  setFocusProxy (terminal);
  status_->setText ("AUDMAP REPL");
  connect (terminal, &QTermWidget::finished, this, [this] {
    status_->setText ("REPL exited");
  });
  terminal->startShellProgram ();
  // finished() is not emitted on every termination path in QTermWidget.
  // Its public process ID still transitions to zero after the child is reaped.
  auto* completion= new QTimer (terminal);
  completion->setInterval (250);
  connect (completion, &QTimer::timeout, this, [this, terminal, completion] {
    if (terminal->getShellPID () <= 0) {
      status_->setText ("REPL exited");
      completion->stop ();
    }
  });
  completion->start ();
  terminal->setFocus ();
}
