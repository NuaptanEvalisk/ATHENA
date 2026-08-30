/******************************************************************************
* MODULE     : QTMErrorMessagesPane.cpp
* DESCRIPTION: Qt ADS pane for error and warning messages
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMErrorMessagesPane.hpp"
#include "QTMMainTabWindow.hpp"
#include "basic.hpp"
#include "convert.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"
#include "tree.hpp"

#include <DockAreaWidget.h>
#include <DockContainerWidget.h>
#include <DockSplitter.h>
#include <DockWidget.h>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSize>
#include <QSizeGrip>
#include <QStringList>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <iostream>

static QTMErrorMessagesPane* error_messages_widget= nullptr;
static ads::CDockWidget* error_messages_dock= nullptr;

static void
acknowledge_error_messages () {
  try { call ("acknowledge-debug-messages"); }
  catch (...) {}
}

static void
set_error_messages_area_height (ads::CDockWidget* dock) {
  if (dock == nullptr || dock->isInFloatingContainer ()) return;
  ads::CDockAreaWidget* area= dock->dockAreaWidget ();
  if (area == nullptr || dock->dockContainer () == nullptr) return;
  ads::CDockSplitter* splitter= area->parentSplitter ();
  if (splitter == nullptr) return;
  QList<int> sizes= splitter->sizes ();
  if (sizes.size () < 2) return;
  int total= 0;
  for (int size : sizes) total += size;
  if (total <= 0) return;
  int target= qBound (260, total / 3, 420);
  int areaIndex= splitter->indexOf (area);
  if (areaIndex < 0 || areaIndex >= sizes.size ()) return;
  int delta= target - sizes[areaIndex];
  if (delta == 0) return;
  sizes[areaIndex]= target;
  int other= areaIndex == 0 ? 1 : 0;
  sizes[other]= qMax (120, sizes[other] - delta);
  splitter->setSizes (sizes);
}

static QString
debug_tree_text (const tree& t) {
  if (is_atomic (t)) return utf8_to_qstring (t->label);
  return to_qstring (tree_to_scheme (t));
}

static QString
message_type (const QString& channel) {
  if (channel.endsWith ("-error")) return channel.left (channel.size () - 6);
  if (channel.endsWith ("-warning")) return channel.left (channel.size () - 8);
  if (channel.endsWith ("-bench")) return channel.left (channel.size () - 6);
  if (channel.startsWith ("debug-")) return channel.mid (6);
  return channel;
}

static bool
is_error_channel (const QString& channel) {
  return channel.endsWith ("-error");
}

static bool
is_warning_channel (const QString& channel) {
  return channel.endsWith ("-warning");
}

QTMErrorMessagesPane::QTMErrorMessagesPane (QWidget* parent)
  : QWidget (parent),
    categoryBox (new QComboBox (this)),
    limitBox (new QComboBox (this)),
    detailsCheck (new QCheckBox ("Details", this)),
    refreshButton (new QPushButton ("Refresh", this)),
    clearButton (new QPushButton ("Clear", this)),
    floatingSizeGrip (new QSizeGrip (this)),
    statusLabel (new QLabel (this)),
    messageTree (new QTreeWidget (this)),
    refreshTimer (new QTimer (this)) {
  categoryBox->addItem ("All");
  categoryBox->addItem ("Errors");
  categoryBox->addItem ("Warnings");

  limitBox->addItem ("Last 25", 25);
  limitBox->addItem ("Last 100", 100);
  limitBox->addItem ("Last 250", 250);
  limitBox->addItem ("Last 1000", 1000);
  limitBox->addItem ("All", 1000000);
  limitBox->setCurrentIndex (1);

  messageTree->setColumnCount (3);
  messageTree->setHeaderLabels (QStringList () << "Kind" << "Channel" << "Message");
  messageTree->setAlternatingRowColors (true);
  messageTree->setRootIsDecorated (false);
  messageTree->setSelectionMode (QAbstractItemView::ExtendedSelection);
  messageTree->header ()->setStretchLastSection (true);
  messageTree->header ()->setSectionResizeMode (0, QHeaderView::ResizeToContents);
  messageTree->header ()->setSectionResizeMode (1, QHeaderView::ResizeToContents);

  QHBoxLayout* controls= new QHBoxLayout ();
  controls->addWidget (new QLabel ("Show", this));
  controls->addWidget (categoryBox);
  controls->addWidget (limitBox);
  controls->addWidget (detailsCheck);
  controls->addStretch ();
  controls->addWidget (refreshButton);
  controls->addWidget (clearButton);

  floatingSizeGrip->hide ();
  QHBoxLayout* gripRow= new QHBoxLayout ();
  gripRow->setContentsMargins (0, 0, 0, 0);
  gripRow->addWidget (statusLabel);
  gripRow->addStretch ();
  gripRow->addWidget (floatingSizeGrip, 0, Qt::AlignRight | Qt::AlignBottom);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->setContentsMargins (8, 8, 8, 8);
  layout->addLayout (controls);
  layout->addWidget (messageTree, 1);
  layout->addLayout (gripRow);

  connect (categoryBox, QOverload<int>::of (&QComboBox::currentIndexChanged),
           this, [this] () { refresh (); });
  connect (limitBox, QOverload<int>::of (&QComboBox::currentIndexChanged),
           this, [this] () { refresh (); });
  connect (detailsCheck, &QCheckBox::toggled,
           this, [this] () { refresh (); });
  connect (refreshButton, &QPushButton::clicked,
           this, [this] () { refresh (); });
  connect (clearButton, &QPushButton::clicked,
           this, [this] () { clearMessages (); });
  connect (refreshTimer, &QTimer::timeout,
           this, [this] () { refresh (); });

  refreshTimer->setInterval (750);
  refreshTimer->start ();
  refresh ();
}

QSize
QTMErrorMessagesPane::sizeHint () const {
  return QSize (900, 360);
}

void
QTMErrorMessagesPane::setFloatingResizeGripVisible (bool visible) {
  floatingSizeGrip->setVisible (visible);
}

QString
QTMErrorMessagesPane::selectedCategory () const {
  return categoryBox->currentText ();
}

int
QTMErrorMessagesPane::messageLimit () const {
  return limitBox->currentData ().toInt ();
}

void
QTMErrorMessagesPane::rebuildCategories () {
  QString current= categoryBox->currentText ();
  QSet<QString> seen;
  tree messages= get_debug_messages ("Error messages", 1000000);
  for (int i=0; i<N(messages); i++) {
    tree m= messages[i];
    if (!is_func (m, TUPLE, 3) || !is_atomic (m[0])) continue;
    seen.insert (message_type (utf8_to_qstring (m[0]->label)));
  }

  categoryBox->blockSignals (true);
  categoryBox->clear ();
  categoryBox->addItem ("All");
  categoryBox->addItem ("Errors");
  categoryBox->addItem ("Warnings");
  QStringList categories= QStringList (seen.values ());
  categories.sort (Qt::CaseInsensitive);
  for (const QString& category : categories)
    if (category != "Errors" && category != "Warnings" && category != "All")
      categoryBox->addItem (category);
  int index= categoryBox->findText (current);
  categoryBox->setCurrentIndex (index >= 0 ? index : 0);
  categoryBox->blockSignals (false);
}

void
QTMErrorMessagesPane::refresh () {
  rebuildCategories ();
  QString category= selectedCategory ();
  tree messages= get_debug_messages ("Error messages", messageLimit ());

  messageTree->clear ();
  int shown= 0;
  for (int i=0; i<N(messages); i++) {
    tree m= messages[i];
    if (!is_func (m, TUPLE, 3) || !is_atomic (m[0]) || !is_atomic (m[1]))
      continue;

    QString channel= utf8_to_qstring (m[0]->label);
    QString type= message_type (channel);
    if (category == "Errors" && !is_error_channel (channel)) continue;
    if (category == "Warnings" && !is_warning_channel (channel)) continue;
    if (category != "All" && category != "Errors" && category != "Warnings" &&
        type != category)
      continue;

    QString text= utf8_to_qstring (m[1]->label);
    QTreeWidgetItem* item= new QTreeWidgetItem (messageTree);
    item->setText (0, type);
    item->setText (1, channel);
    item->setText (2, text);
    if (is_error_channel (channel))
      item->setForeground (2, QColor ("#b00020"));
    else if (is_warning_channel (channel))
      item->setForeground (2, QColor ("#7a3b00"));

    bool hasDetails= !(is_atomic (m[2]) && m[2]->label == "");
    if (detailsCheck->isChecked () && hasDetails) {
      QTreeWidgetItem* detail= new QTreeWidgetItem (item);
      detail->setText (2, debug_tree_text (m[2]));
      item->setExpanded (true);
    }
    shown++;
  }
  if (shown == 0) {
    QTreeWidgetItem* item= new QTreeWidgetItem (messageTree);
    item->setText (2, "No error or warning messages.");
    item->setFlags (item->flags () & ~Qt::ItemIsSelectable);
  }
  statusLabel->setText (QString ("%1 message%2 shown")
                        .arg (shown)
                        .arg (shown == 1 ? "" : "s"));
}

void
QTMErrorMessagesPane::clearMessages () {
  acknowledge_error_messages ();
  clear_debug_messages ();
  refresh ();
}

void
error_messages_show () {
  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr || win->dockManager () == nullptr) {
    QMessageBox::warning (QApplication::activeWindow (), "Error messages",
                          "No active ATHENA window.");
    return;
  }

  if (error_messages_widget == nullptr) {
    error_messages_widget= new QTMErrorMessagesPane ();
    error_messages_widget->resize (900, 360);
    QObject::connect (error_messages_widget, &QObject::destroyed, [] () {
      error_messages_widget= nullptr;
      error_messages_dock= nullptr;
    });
  }

  if (error_messages_dock == nullptr) {
    error_messages_dock= new ads::CDockWidget ("Error messages");
    error_messages_dock->setObjectName ("athena-error-messages");
    error_messages_dock->resize (900, 360);
    error_messages_dock->setWidget (error_messages_widget);
    error_messages_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QTMErrorMessagesPane* pane= error_messages_widget;
    ads::CDockWidget* dock= error_messages_dock;
    QObject::connect (dock, &ads::CDockWidget::topLevelChanged,
                      pane, [pane, dock] (bool) {
                        pane->setFloatingResizeGripVisible (
                          dock->isInFloatingContainer ());
                      });
    QObject::connect (error_messages_dock, &QObject::destroyed, [] () {
      error_messages_dock= nullptr;
    });
    QObject::connect (error_messages_dock, &ads::CDockWidget::closed,
                      [] () { acknowledge_error_messages (); });
  }

  win->showAdsDockWidget (error_messages_dock, ads::BottomDockWidgetArea);

  error_messages_widget->refresh ();
  error_messages_widget->setFloatingResizeGripVisible (
    error_messages_dock->isInFloatingContainer ());
  set_error_messages_area_height (error_messages_dock);
  QTimer::singleShot (0, win, [] () {
    set_error_messages_area_height (error_messages_dock);
  });
  error_messages_widget->setFocus ();
}
