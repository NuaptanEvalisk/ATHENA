/******************************************************************************
* MODULE     : main.cpp
* DESCRIPTION: Independent Qt desktop dialogs and AUDMAP subscription worker
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "workflows.hpp"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
class CourseDialog final: public QDialog {
  QLineEdit* filter;
  QListWidget* list;
public:
  CourseDialog (const QStringList& courses, const QString& title) {
    setWindowTitle (title);
    resize (560, 440);
    auto* layout= new QVBoxLayout (this);
    filter= new QLineEdit (this);
    filter->setPlaceholderText ("Filter courses");
    list= new QListWidget (this);
    for (int i= 0; i < courses.size (); ++i) {
      auto* item= new QListWidgetItem (courses[i], list);
      item->setData (Qt::UserRole, i);
    }
    list->setCurrentRow (0);
    auto* buttons= new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget (filter);
    layout->addWidget (list, 1);
    layout->addWidget (buttons);
    filter->installEventFilter (this);
    connect (filter, &QLineEdit::textChanged, this, [this, buttons] (const QString& text) {
      int first= -1;
      for (int i= 0; i < list->count (); ++i) {
        const bool match= list->item (i)->text ().contains (text, Qt::CaseInsensitive);
        list->item (i)->setHidden (!match);
        if (first < 0 && match) first= i;
      }
      list->setCurrentRow (first);
      buttons->button (QDialogButtonBox::Ok)->setEnabled (first >= 0);
    });
    connect (buttons, &QDialogButtonBox::accepted, this, [this] {
      if (list->currentItem () && !list->currentItem ()->isHidden ()) accept ();
    });
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect (list, &QListWidget::itemActivated, this, [this] { accept (); });
    filter->setFocus ();
  }
  int choice () const { return list->currentItem ()->data (Qt::UserRole).toInt (); }
  bool eventFilter (QObject* watched, QEvent* event) override {
    if (watched == filter && event->type () == QEvent::KeyPress) {
      auto* key= static_cast<QKeyEvent*> (event);
      if (key->key () == Qt::Key_Up || key->key () == Qt::Key_Down) {
        int row= list->currentRow ();
        for (int n= 0; n < list->count (); ++n) {
          row= (row + (key->key () == Qt::Key_Up ? -1 : 1) + list->count ()) % list->count ();
          if (!list->item (row)->isHidden ()) { list->setCurrentRow (row); break; }
        }
        return true;
      }
    }
    return QDialog::eventFilter (watched, event);
  }
};
}

int main (int argc, char** argv) {
  QApplication app (argc, argv);
  app.setApplicationName ("nIKIS Workflows");
  app.setQuitOnLastWindowClosed (false);
  int result= 0;
  std::thread worker ([&] {
    auto report= [] (const QString& message) {
      QMetaObject::invokeMethod (qApp, [message] {
        QMessageBox::critical (nullptr, "nIKIS Workflows", message);
      }, Qt::BlockingQueuedConnection);
    };
    try {
      std::ifstream input ((QCoreApplication::applicationDirPath () + "/settings.json").toStdString ());
      const auto settings= nikis::value::parse (input);
      athena::audmap::options options;
      options.endpoint= qgetenv ("ATHENA_AUDMAP_ENDPOINT").toStdString ();
      options.identity= qgetenv ("ATHENA_AUDMAP_IDENTITY").toStdString ();
      options.name= "nIKIS Workflows";
      auto choose= [] (const QStringList& courses, const QString& title) {
        int selected= -1;
        QMetaObject::invokeMethod (qApp, [&] {
          CourseDialog dialog (courses, title);
          if (dialog.exec () == QDialog::Accepted) selected= dialog.choice ();
        }, Qt::BlockingQueuedConnection);
        return selected;
      };
      for (;;) {
        athena::audmap::client connection (options);
        nikis::serve (connection, settings, choose, report);
      }
    }
    catch (const std::exception& error) {
      result= 1;
      std::cerr << "nIKIS Workflows stopped: " << error.what () << std::endl;
      report (QString::fromUtf8 (error.what ()) +
        "\n\nThe connection stopped. A request may already have completed; check the document before retrying.");
      QMetaObject::invokeMethod (qApp, &QApplication::quit, Qt::QueuedConnection);
    }
  });
  app.exec ();
  worker.join ();
  return result;
}
