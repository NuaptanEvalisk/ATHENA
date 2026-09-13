/******************************************************************************
* MODULE     : QTMAudmapPane.cpp
* DESCRIPTION: Workspace ADS pane hosting the current instance's AUDMAP REPL
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "QTMAudmap.hpp"
#include "QTMAudmapTerminal.hpp"
#include "QTMMainTabWindow.hpp"
#include <DockWidget.h>
#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QThread>

void audmap_repl_show () {
  Q_ASSERT (QThread::currentThread () == qApp->thread ());
  static QPointer<ads::CDockWidget> dock;
  auto* window= QTMMainTabWindow::topTabWindow ();
  if (!window || !window->dockManager ()) return;
  if (!dock) {
    const auto endpoint= qt_audmap_discovery_file ();
    if (endpoint.isEmpty ()) {
      QMessageBox::warning (window, "AUDMAP REPL", "The AUDMAP service is not running.");
      return;
    }
    const QDir bin (QDir (qEnvironmentVariable ("ATHENA_PATH")).filePath ("bin"));
    const auto executable= bin.absoluteFilePath ("athena-audmap-repl");
    dock= new ads::CDockWidget ("AUDMAP REPL");
    dock->setObjectName ("athena-audmap-repl");
    dock->resize (900, 360);
    dock->setWidget (new QTMAudmapTerminal (executable, endpoint),
                     ads::CDockWidget::ForceNoScrollArea);
    dock->setFeature (ads::CDockWidget::DockWidgetDeleteOnClose, true);
  }
  window->showAdsDockWidget (dock, ads::BottomDockWidgetArea);
  dock->widget ()->setFocus ();
}
