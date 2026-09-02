/******************************************************************************
* MODULE     : QTMMetadataPropertiesPane.cpp
* DESCRIPTION: Native ADS pane for document metadata
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMMetadataPropertiesPane.hpp"

#include "QTMMainTabWindow.hpp"
#include "new_buffer.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"

#include <DockManager.h>
#include <DockWidget.h>
#include <QApplication>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

static QTMMetadataPropertiesPane* metadata_properties_pane_widget= nullptr;
static ads::CDockWidget* metadata_properties_pane_dock= nullptr;

static QString
qs (string s) {
  return to_qstring (s);
}

static string
tm_string (const QString& s) {
  return from_qstring (s);
}

struct MetadataField {
  const char* label;
  const char* key;
  const char* variable;
};

static const MetadataField metadata_fields[] = {
  { "Title", "title", "global-title" },
  { "Author", "author", "global-author" },
  { "Subject", "subject", "global-subject" },
  { "Created time", "created-time", "global-created-time" },
  { "Modified time", "modified-time", "global-modified-time" },
  { "Content hash", "content-hash", "global-content-hash" }
};

QTMMetadataPropertiesPane::QTMMetadataPropertiesPane (QWidget* parent)
  : QWidget (parent), loading (false), timer (new QTimer (this)) {
  buildUi ();
  timer->setInterval (700);
  connect (timer, &QTimer::timeout, this, [this] () {
    refreshFromCurrentBuffer ();
  });
  timer->start ();
  refreshFromCurrentBuffer ();
}

QSize
QTMMetadataPropertiesPane::sizeHint () const {
  return QSize (480, 320);
}

void
QTMMetadataPropertiesPane::buildUi () {
  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->setContentsMargins (10, 10, 10, 10);
  layout->setSpacing (10);

  QFormLayout* form= new QFormLayout ();
  form->setFieldGrowthPolicy (QFormLayout::AllNonFixedFieldsGrow);
  for (const MetadataField& field: metadata_fields) {
    QLineEdit* edit= new QLineEdit (this);
    edit->setClearButtonEnabled (true);
    edits[field.variable]= edit;
    form->addRow (QString (field.label) + ":", edit);
    connect (edit, &QLineEdit::editingFinished, this, [this, edit, field] () {
      if (!loading) setMetadata (field.variable, edit->text ());
    });
  }
  layout->addLayout (form);

  QHBoxLayout* buttons= new QHBoxLayout ();
  buttons->addStretch (1);
  QPushButton* reset= new QPushButton ("Reset", this);
  buttons->addWidget (reset);
  layout->addLayout (buttons);
  connect (reset, &QPushButton::clicked, this, [this] () {
    resetMetadata ();
    refreshAll ();
  });
  layout->addStretch (1);
}

void
QTMMetadataPropertiesPane::refreshFromCurrentBuffer () {
  url current= get_current_buffer_safe ();
  if (!is_none (current) && as_string (current) != as_string (targetBuffer))
    setTargetBuffer (current);
}

void
QTMMetadataPropertiesPane::setTargetBuffer (url buffer) {
  targetBuffer= buffer;
  refreshAll ();
}

bool
QTMMetadataPropertiesPane::targetLooksUsable () const {
  return !is_none (targetBuffer);
}

QString
QTMMetadataPropertiesPane::getMetadata (const QString& key) const {
  if (!targetLooksUsable ()) return "";
  try {
    return qs (as_string (call ("buffer-get-metadata", object (targetBuffer),
                                object (tm_string (key)))));
  }
  catch (...) {
    return "";
  }
}

void
QTMMetadataPropertiesPane::setMetadata (const QString& variable,
                                        const QString& value) {
  if (!targetLooksUsable ()) return;
  try {
    call ("initial-set", object (targetBuffer), object (tm_string (variable)),
          object (tm_string (value)));
  }
  catch (...) {}
}

void
QTMMetadataPropertiesPane::resetMetadata () {
  if (!targetLooksUsable ()) return;
  array<object> args;
  args << object (targetBuffer);
  for (const MetadataField& field: metadata_fields)
    args << object (string (field.variable));
  try { call ("initial-default", args); }
  catch (...) {}
}

void
QTMMetadataPropertiesPane::refreshAll () {
  loading= true;
  for (const MetadataField& field: metadata_fields) {
    QLineEdit* edit= edits[field.variable];
    if (edit == nullptr) continue;
    QSignalBlocker blocker (edit);
    edit->setText (getMetadata (field.key));
  }
  loading= false;
}

void
metadata_properties_pane_show () {
  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr || win->dockManager () == nullptr) {
    QMessageBox::warning (QApplication::activeWindow (), "Document metadata",
                          "No active ATHENA window.");
    return;
  }

  if (metadata_properties_pane_widget == nullptr) {
    metadata_properties_pane_widget= new QTMMetadataPropertiesPane ();
    QObject::connect (metadata_properties_pane_widget, &QObject::destroyed,
                      [] () {
      metadata_properties_pane_widget= nullptr;
      metadata_properties_pane_dock= nullptr;
    });
  }

  if (metadata_properties_pane_dock == nullptr) {
    metadata_properties_pane_dock= new ads::CDockWidget ("Document metadata");
    metadata_properties_pane_dock->setObjectName (
      "athena-metadata-properties-pane");
    metadata_properties_pane_dock->resize (480, 320);
    metadata_properties_pane_dock->setWidget (metadata_properties_pane_widget);
    metadata_properties_pane_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QObject::connect (metadata_properties_pane_dock, &QObject::destroyed,
                      [] () {
      metadata_properties_pane_dock= nullptr;
    });
    win->dockManager ()->addDockWidgetFloating (metadata_properties_pane_dock);
    win->scheduleAdsLayoutRestore (metadata_properties_pane_dock);
  }
  else if (metadata_properties_pane_dock->dockAreaWidget () == nullptr ||
           metadata_properties_pane_dock->dockContainer () == nullptr)
    win->dockManager ()->addDockWidgetFloating (metadata_properties_pane_dock);

  metadata_properties_pane_dock->toggleView (true);
  metadata_properties_pane_dock->show ();
  metadata_properties_pane_dock->raise ();
  metadata_properties_pane_widget->refreshFromCurrentBuffer ();
  metadata_properties_pane_widget->setFocus ();
}
