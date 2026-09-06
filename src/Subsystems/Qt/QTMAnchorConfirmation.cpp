/******************************************************************************
* MODULE     : QTMAnchorConfirmation.cpp
* DESCRIPTION: Asynchronous confirmation of document anchor changes
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#include "QTMAnchorConfirmation.hpp"
#include "qt_utilities.hpp"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

void
qt_anchor_enunciations_confirm (
    QString wraps, QString dead, QString headings, QString notes,
    std::function<void (bool)> completion) {
  qt_post_to_main_thread ([wraps, dead, headings, notes,
                          completion= std::move (completion)] () mutable {
    notes.replace ("<<<ATHENA-ANCHOR-ACTION>>>", "\n");
    notes.replace ("\\r\\n", "\n");
    notes.replace ("\\n", "\n");
    notes.replace ("\\t", "\t");
    notes.replace ("\r\n", "\n");
    notes.replace ('\r', '\n');

    QStringList items;
    QString current;
    for (QChar c: notes) {
      if (c == '\n' || c == '\t' || c.unicode () == 0x1e ||
          c.unicode () == 0x00af) {
        QString trimmed= current.trimmed ();
        if (!trimmed.isEmpty ()) items << trimmed;
        current.clear ();
      }
      else current.append (c);
    }
    QString trimmed= current.trimmed ();
    if (!trimmed.isEmpty ()) items << trimmed;

    if (items.size () <= 1 && !notes.trimmed ().isEmpty ()) {
      QString compact= notes.simplified ();
      QStringList split;
      int start= 0;
      for (int i=1; i<compact.size (); i++) {
        bool boundary= compact.mid (i).startsWith ("wrap ") ||
          compact.mid (i).startsWith ("anchor heading: ") ||
          compact.mid (i).startsWith ("remove dead anchors: ");
        if (boundary && compact.at (i - 1).isSpace ()) {
          QString item= compact.mid (start, i - start).trimmed ();
          if (!item.isEmpty ()) split << item;
          start= i;
        }
      }
      QString item= compact.mid (start).trimmed ();
      if (!item.isEmpty ()) split << item;
      if (split.size () > items.size ()) items= split;
    }

    auto* dialog= new QDialog (QApplication::activeWindow ());
    dialog->setObjectName ("athena-anchor-confirmation");
    dialog->setWindowTitle ("Anchor structures");
    dialog->resize (760, 480);
    auto* layout= new QVBoxLayout (dialog);
    auto* intro= new QLabel (
      QString ("ATHENA will wrap %1 enunciation(s), add %2 heading anchor(s), "
               "and remove %3 dead anchor pair(s). Review the planned changes "
               "before applying them.").arg (wraps, headings, dead), dialog);
    intro->setWordWrap (true);
    layout->addWidget (intro);
    layout->addWidget (new QLabel ("Planned actions:", dialog));
    auto* list= new QListWidget (dialog);
    list->setAlternatingRowColors (true);
    list->setSelectionMode (QAbstractItemView::NoSelection);
    list->setWordWrap (true);
    list->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    list->setMinimumHeight (300);
    if (items.isEmpty ())
      list->addItem ("No individual action details are available.");
    else list->addItems (items);
    layout->addWidget (list, 1);
    auto* buttons= new QDialogButtonBox (dialog);
    auto* apply= buttons->addButton ("Apply", QDialogButtonBox::AcceptRole);
    auto* cancel= buttons->addButton (QDialogButtonBox::Cancel);
    cancel->setDefault (true);
    apply->setAutoDefault (false);
    QObject::connect (buttons, &QDialogButtonBox::accepted,
                      dialog, &QDialog::accept);
    QObject::connect (buttons, &QDialogButtonBox::rejected,
                      dialog, &QDialog::reject);
    layout->addWidget (buttons);
    QObject::connect (dialog, &QDialog::finished, dialog,
                      [dialog, completion= std::move (completion), done= false]
                      (int result) mutable {
      if (done) return;
      done= true;
      dialog->deleteLater ();
      completion (result == QDialog::Accepted);
    });
    dialog->setModal (true);
    dialog->show ();
  });
}
