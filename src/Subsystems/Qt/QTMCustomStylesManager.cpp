/******************************************************************************
* MODULE     : QTMCustomStylesManager.cpp
* DESCRIPTION: Qt ADS pane for managing custom ATHENA styles
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMCustomStylesManager.hpp"
#include "QTMMainTabWindow.hpp"
#include "boot.hpp"
#include "scheme.hpp"
#include "server.hpp"
#include "sys_utils.hpp"
#include "qt_utilities.hpp"

#include <DockWidget.h>
#include <QAbstractItemView>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSizeGrip>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

static QTMCustomStylesManager* custom_styles_manager_widget= nullptr;
static ads::CDockWidget* custom_styles_manager_dock= nullptr;

static QToolButton*
custom_styles_button (const QString& text, const QIcon& icon) {
  QToolButton* button= new QToolButton ();
  button->setText (text);
  button->setIcon (icon);
  button->setToolButtonStyle (Qt::ToolButtonTextBesideIcon);
  button->setAutoRaise (true);
  return button;
}

QTMCustomStylesManager::QTMCustomStylesManager (QWidget* parent)
  : QWidget (parent),
    list (new QListWidget (this)),
    pathLabel (new QLabel (this)),
    statusLabel (new QLabel (this)),
    floatingSizeGrip (new QSizeGrip (this)) {
  QVBoxLayout* root= new QVBoxLayout (this);
  root->setContentsMargins (8, 8, 8, 8);
  root->setSpacing (6);

  QHBoxLayout* toolbar= new QHBoxLayout ();
  toolbar->setContentsMargins (0, 0, 0, 0);

  QStyle* style= QApplication::style ();
  QToolButton* install= custom_styles_button (
    "Install", style->standardIcon (QStyle::SP_DialogOpenButton));
  QToolButton* uninstall= custom_styles_button (
    "Uninstall", style->standardIcon (QStyle::SP_TrashIcon));
  QToolButton* activate= custom_styles_button (
    "Activate", style->standardIcon (QStyle::SP_DialogApplyButton));
  QToolButton* refreshButton= custom_styles_button (
    "Refresh", style->standardIcon (QStyle::SP_BrowserReload));

  toolbar->addWidget (install);
  toolbar->addWidget (uninstall);
  toolbar->addWidget (activate);
  toolbar->addStretch (1);
  toolbar->addWidget (refreshButton);
  root->addLayout (toolbar);

  pathLabel->setTextInteractionFlags (Qt::TextSelectableByMouse);
  pathLabel->setWordWrap (true);
  pathLabel->setTextFormat (Qt::PlainText);
  root->addWidget (pathLabel);

  list->setSelectionMode (QAbstractItemView::SingleSelection);
  list->setContextMenuPolicy (Qt::CustomContextMenu);
  root->addWidget (list, 1);

  statusLabel->setTextInteractionFlags (Qt::TextSelectableByMouse);
  root->addWidget (statusLabel);

  QHBoxLayout* bottom= new QHBoxLayout ();
  bottom->setContentsMargins (0, 0, 0, 0);
  bottom->addStretch (1);
  bottom->addWidget (floatingSizeGrip);
  root->addLayout (bottom);
  floatingSizeGrip->hide ();

  connect (install, &QToolButton::clicked,
           this, [this] () { installStyle (); });
  connect (uninstall, &QToolButton::clicked,
           this, [this] () { uninstallSelectedStyle (); });
  connect (activate, &QToolButton::clicked,
           this, [this] () { activateSelectedStyle (); });
  connect (refreshButton, &QToolButton::clicked,
           this, [this] () { refresh (); });
  connect (list, &QListWidget::itemDoubleClicked,
           this, [this] (QListWidgetItem*) { activateSelectedStyle (); });
  connect (list, &QListWidget::customContextMenuRequested,
           this, [this] (const QPoint& pos) { showContextMenu (pos); });

  refresh ();
}

QSize
QTMCustomStylesManager::sizeHint () const {
  return QSize (420, 560);
}

void
QTMCustomStylesManager::setFloatingResizeGripVisible (bool visible) {
  floatingSizeGrip->setVisible (visible);
}

QString
QTMCustomStylesManager::stylesDirectory () const {
  QString home= to_qstring (get_env ("ATHENA_HOME_PATH"));
  if (home.isEmpty ()) return QString ();
  return QDir (home).filePath ("styles");
}

QString
QTMCustomStylesManager::selectedStyleName () const {
  QListWidgetItem* item= list->currentItem ();
  return item == nullptr ? QString () : item->data (Qt::UserRole).toString ();
}

QString
QTMCustomStylesManager::selectedStylePath () const {
  QString name= selectedStyleName ();
  if (name.isEmpty ()) return QString ();
  return QDir (stylesDirectory ()).filePath (name + ".ts");
}

void
QTMCustomStylesManager::refresh () {
  QString dirPath= stylesDirectory ();
  pathLabel->setText (QString ("Custom styles directory: %1").arg (dirPath));
  list->clear ();

  if (dirPath.isEmpty ()) {
    statusLabel->setText ("ATHENA_HOME_PATH is not set.");
    return;
  }

  QDir dir (dirPath);
  if (!dir.exists ()) dir.mkpath (".");

  QFileInfoList files= dir.entryInfoList (
    QStringList () << "*.ts", QDir::Files | QDir::Readable, QDir::Name);
  for (const QFileInfo& info: files) {
    QString name= info.completeBaseName ();
    QListWidgetItem* item= new QListWidgetItem (name, list);
    item->setData (Qt::UserRole, name);
    item->setToolTip (info.absoluteFilePath ());
  }

  statusLabel->setText (QString ("%1 custom style(s)").arg (files.size ()));
}

void
QTMCustomStylesManager::showError (const QString& message) const {
  QMessageBox::warning (const_cast<QTMCustomStylesManager*> (this),
                        "Custom styles manager", message);
}

void
QTMCustomStylesManager::showInfo (const QString& message) const {
  QMessageBox::information (const_cast<QTMCustomStylesManager*> (this),
                            "Custom styles manager", message);
}

void
QTMCustomStylesManager::installStyle () {
  QString dirPath= stylesDirectory ();
  if (dirPath.isEmpty ()) {
    showError ("ATHENA_HOME_PATH is not set.");
    return;
  }
  QDir dir (dirPath);
  if (!dir.exists () && !dir.mkpath (".")) {
    showError (QString ("Could not create custom styles directory:\n%1")
               .arg (dirPath));
    return;
  }

  QString source= QFileDialog::getOpenFileName (
    this, "Install custom style", QDir::homePath (),
    "TeXmacs styles (*.ts);;All files (*)");
  if (source.isEmpty ()) return;

  QFileInfo sourceInfo (source);
  if (sourceInfo.suffix () != "ts") {
    showError ("Please select a TeXmacs stylesheet file ending in .ts.");
    return;
  }

  QString target= dir.filePath (sourceInfo.fileName ());
  if (QFileInfo (target).absoluteFilePath () == sourceInfo.absoluteFilePath ()) {
    refresh ();
    QString styleName= sourceInfo.completeBaseName ();
    QList<QListWidgetItem*> hits= list->findItems (styleName, Qt::MatchExactly);
    if (!hits.isEmpty ()) list->setCurrentItem (hits.first ());
    showInfo (QString ("Style is already installed: %1").arg (styleName));
    return;
  }
  if (QFileInfo::exists (target)) {
    int answer= QMessageBox::question (
      this, "Custom styles manager",
      QString ("Replace existing custom style?\n\n%1").arg (target),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    if (!QFile::remove (target)) {
      showError (QString ("Could not replace existing style:\n%1").arg (target));
      return;
    }
  }

  if (!QFile::copy (source, target)) {
    showError (QString ("Could not install style:\n%1").arg (target));
    return;
  }

  get_server ()->style_clear_cache ();
  refresh ();
  QString styleName= QFileInfo (target).completeBaseName ();
  QList<QListWidgetItem*> hits= list->findItems (styleName, Qt::MatchExactly);
  if (!hits.isEmpty ()) list->setCurrentItem (hits.first ());
  showInfo (QString ("Installed custom style: %1").arg (styleName));
}

void
QTMCustomStylesManager::uninstallSelectedStyle () {
  QString name= selectedStyleName ();
  QString path= selectedStylePath ();
  if (name.isEmpty () || path.isEmpty ()) {
    showError ("Please select a custom style to uninstall.");
    return;
  }

  int answer= QMessageBox::question (
    this, "Custom styles manager",
    QString ("Uninstall custom style '%1'?\n\n%2").arg (name, path),
    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (answer != QMessageBox::Yes) return;

  if (!QFile::remove (path)) {
    showError (QString ("Could not remove style:\n%1").arg (path));
    return;
  }

  get_server ()->style_clear_cache ();
  refresh ();
}

void
QTMCustomStylesManager::activateSelectedStyle () {
  QString name= selectedStyleName ();
  if (name.isEmpty ()) {
    showError ("Please select a custom style to activate.");
    return;
  }

  call ("set-main-style", object (from_qstring (name)));
  get_server ()->style_clear_cache ();
  statusLabel->setText (QString ("Activated style: %1").arg (name));
}

void
QTMCustomStylesManager::openStylesDirectory () {
  QString dirPath= stylesDirectory ();
  if (dirPath.isEmpty ()) return;
  QDir dir (dirPath);
  if (!dir.exists ()) dir.mkpath (".");
  QDesktopServices::openUrl (QUrl::fromLocalFile (dirPath));
}

void
QTMCustomStylesManager::showContextMenu (const QPoint& pos) {
  QMenu menu (this);
  QAction* activate= menu.addAction ("Activate");
  QAction* uninstall= menu.addAction ("Uninstall");
  menu.addSeparator ();
  QAction* install= menu.addAction ("Install...");
  QAction* openDir= menu.addAction ("Open styles directory");
  QAction* picked= menu.exec (list->viewport ()->mapToGlobal (pos));
  if (picked == activate) activateSelectedStyle ();
  else if (picked == uninstall) uninstallSelectedStyle ();
  else if (picked == install) installStyle ();
  else if (picked == openDir) openStylesDirectory ();
}

void
custom_styles_manager_show () {
  if (qt_defer_to_main_thread (custom_styles_manager_show)) return;
  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr) {
    QMessageBox::warning (QApplication::activeWindow (),
                          "Custom styles manager",
                          "No active ATHENA window.");
    return;
  }

  if (custom_styles_manager_widget == nullptr) {
    custom_styles_manager_widget= new QTMCustomStylesManager ();
    custom_styles_manager_widget->resize (420, 560);
    QObject::connect (custom_styles_manager_widget, &QObject::destroyed, [] () {
      custom_styles_manager_widget= nullptr;
      custom_styles_manager_dock= nullptr;
    });
  }

  if (custom_styles_manager_dock == nullptr) {
    custom_styles_manager_dock= new ads::CDockWidget ("Custom styles manager");
    custom_styles_manager_dock->setObjectName ("athena-custom-styles-manager");
    custom_styles_manager_dock->resize (420, 560);
    custom_styles_manager_dock->setWidget (custom_styles_manager_widget);
    custom_styles_manager_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QTMCustomStylesManager* pane= custom_styles_manager_widget;
    ads::CDockWidget* dock= custom_styles_manager_dock;
    QObject::connect (dock, &ads::CDockWidget::topLevelChanged,
                      pane, [pane, dock] (bool) {
                        pane->setFloatingResizeGripVisible (
                          dock->isInFloatingContainer ());
                      });
    QObject::connect (custom_styles_manager_dock, &QObject::destroyed, [] () {
      custom_styles_manager_dock= nullptr;
    });
    win->showAdsDockWidget (custom_styles_manager_dock,
                            ads::RightDockWidgetArea);
  }

  custom_styles_manager_widget->refresh ();
  win->showAdsDockWidget (custom_styles_manager_dock,
                          ads::RightDockWidgetArea);
  custom_styles_manager_widget->setFloatingResizeGripVisible (
    custom_styles_manager_dock->isInFloatingContainer ());
  QTimer::singleShot (0, custom_styles_manager_widget,
                      [] () { custom_styles_manager_widget->refresh (); });
  custom_styles_manager_widget->setFocus ();
}
