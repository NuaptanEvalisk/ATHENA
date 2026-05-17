/******************************************************************************
* MODULE     : QTMOutlinePane.cpp
* DESCRIPTION: Live document outline pane
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMOutlinePane.hpp"
#include "QTMMainTabWindow.hpp"
#include "editor.hpp"
#include "qt_utilities.hpp"

#include <DockWidget.h>
#include <QApplication>
#include <QHeaderView>
#include <QMessageBox>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <algorithm>

static QTMOutlinePane* outline_pane_widget= nullptr;
static ads::CDockWidget* outline_pane_dock= nullptr;

static std::string
tree_tag (const tree& t) {
  if (!is_compound (t)) return "";
  return std::string (as_charp (as_string (L(t))));
}

static int
heading_level (const tree& t) {
  std::string tag= tree_tag (t);
  if (tag == "part") return 1;
  if (tag == "chapter") return 1;
  if (tag == "section") return 1;
  if (tag == "subsection") return 2;
  if (tag == "subsubsection") return 3;
  if (tag == "paragraph") return 4;
  if (tag == "subparagraph") return 5;
  return 0;
}

static bool
is_title_tree (const tree& t) {
  std::string tag= tree_tag (t);
  return tag == "title" || tag == "doc-title" ||
         tag == "tmdoc-title" || tag == "tmweb-title";
}

static bool
is_cjk (const QChar& ch) {
  uint u= ch.unicode ();
  return (u >= 0x3400 && u <= 0x9fff) ||
         (u >= 0xf900 && u <= 0xfaff) ||
         (u >= 0x3040 && u <= 0x30ff) ||
         (u >= 0xac00 && u <= 0xd7af);
}

static int
word_count_text (const QString& text) {
  int count= 0;
  bool inWord= false;
  for (int i=0; i<text.size (); i++) {
    QChar ch= text[i];
    if (is_cjk (ch)) {
      if (inWord) inWord= false;
      count++;
    }
    else if (ch.isLetterOrNumber ()) {
      if (!inWord) {
        count++;
        inWord= true;
      }
    }
    else if (ch != '\'' && ch != QChar (0x2019)) {
      inWord= false;
    }
  }
  return count;
}

static void
append_plain_text (const tree& t, QString& out) {
  if (is_atomic (t)) {
    QString s= to_qstring (t->label);
    if (!s.trimmed ().isEmpty ()) {
      if (!out.isEmpty ()) out += " ";
      out += s;
    }
    return;
  }
  if (!is_compound (t)) return;

  std::string tag= tree_tag (t);
  if (tag == "label" || tag == "reference" || tag == "pageref" ||
      tag == "image" || tag == "include" || tag == "bibliography")
    return;

  for (int i=0; i<N(t); i++) append_plain_text (t[i], out);
}

static QString
plain_text (const tree& t) {
  QString out;
  append_plain_text (t, out);
  return out.simplified ();
}

static QString
heading_title (const tree& t) {
  if (is_compound (t) && N(t) > 0) {
    QString title= plain_text (t[0]);
    if (!title.isEmpty ()) return title;
  }
  QString fallback= plain_text (t);
  return fallback.isEmpty () ? QString ("Untitled") : fallback;
}

class OutlineBuilder {
public:
  OutlineBuilder (QVector<QTMOutlinePane::Entry>& entries2, path rootPath2)
    : entries (entries2), rootPath (rootPath2) {}

  void scan (const tree& t, path rel= path ()) {
    if (is_atomic (t)) {
      addWords (word_count_text (to_qstring (t->label)));
      return;
    }
    if (!is_compound (t)) return;

    if (is_title_tree (t)) {
      QTMOutlinePane::Entry entry;
      entry.level= 0;
      entry.title= heading_title (t);
      entry.words= 0;
      entry.treePath= rootPath * rel;
      entries.append (entry);
      return;
    }

    int level= heading_level (t);
    if (level > 0) {
      while (!openIndexes.empty () &&
             entries[openIndexes.back ()].level >= level)
        openIndexes.pop_back ();

      QTMOutlinePane::Entry entry;
      entry.level= level;
      entry.title= heading_title (t);
      entry.words= 0;
      entry.treePath= rootPath * rel;
      entries.append (entry);
      openIndexes.push_back (entries.size () - 1);
      return;
    }

    std::string tag= tree_tag (t);
    if (tag == "label" || tag == "reference" || tag == "pageref" ||
        tag == "image" || tag == "include" || tag == "bibliography")
      return;

    for (int i=0; i<N(t); i++) scan (t[i], rel * i);
  }

private:
  void addWords (int words) {
    if (words <= 0) return;
    for (int index: openIndexes) entries[index].words += words;
  }

  QVector<QTMOutlinePane::Entry>& entries;
  QVector<int> openIndexes;
  path rootPath;
};

QTMOutlinePane::QTMOutlinePane (QWidget* parent)
  : QWidget (parent), tree (new QTreeWidget (this)), timer (new QTimer (this)) {
  tree->setColumnCount (2);
  tree->setHeaderLabels (QStringList () << "Outline" << "Words");
  tree->setAlternatingRowColors (true);
  tree->setUniformRowHeights (true);
  tree->header ()->setSectionResizeMode (0, QHeaderView::Stretch);
  tree->header ()->setSectionResizeMode (1, QHeaderView::ResizeToContents);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->setContentsMargins (0, 0, 0, 0);
  layout->addWidget (tree);

  timer->setInterval (500);
  connect (timer, &QTimer::timeout, this, [this] () { refresh (); });
  connect (tree, &QTreeWidget::itemActivated,
           this, [this] (QTreeWidgetItem* item) { activateItem (item); });
  connect (tree, &QTreeWidget::itemDoubleClicked,
           this, [this] (QTreeWidgetItem* item) { activateItem (item); });
  timer->start ();
  refresh ();
}

QSize
QTMOutlinePane::sizeHint () const {
  return QSize (320, 600);
}

void
QTMOutlinePane::refresh () {
  editor ed= get_current_editor ();
  if (is_nil (ed)) return;

  class tree doc= ed->the_buffer ();
  QString signature= to_qstring (as_string (hash (doc))) + ":" +
                     to_qstring (as_string (N(doc)));
  if (signature == lastSignature) return;
  lastSignature= signature;

  entries.clear ();
  OutlineBuilder builder (entries, ed->the_buffer_path ());
  builder.scan (doc);

  tree->clear ();
  QVector<QTreeWidgetItem*> parents;
  for (int i=0; i<entries.size (); i++) {
    const Entry& entry= entries[i];
    QTreeWidgetItem* item= new QTreeWidgetItem ();
    item->setText (0, entry.title);
    item->setText (1, entry.words > 0 ? QString::number (entry.words) : "");
    item->setData (0, Qt::UserRole, i);

    int level= std::max (0, entry.level);
    while (parents.size () > level) parents.pop_back ();

    QTreeWidgetItem* parent= nullptr;
    for (int j=parents.size () - 1; j >= 0; j--)
      if (parents[j] != nullptr) {
        parent= parents[j];
        break;
      }

    if (parent == nullptr) tree->addTopLevelItem (item);
    else parent->addChild (item);

    if (parents.size () <= level) parents.resize (level + 1);
    parents[level]= item;
  }
  tree->expandAll ();
}

void
QTMOutlinePane::activateItem (QTreeWidgetItem* item) {
  if (item == nullptr) return;
  int index= item->data (0, Qt::UserRole).toInt ();
  if (index < 0 || index >= entries.size ()) return;

  editor ed= get_current_editor ();
  if (is_nil (ed)) return;
  ed->focus_on_this_editor ();
  ed->go_to_start (entries[index].treePath);
}

void
outline_pane_show () {
  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr || win->dockManager () == nullptr) {
    QMessageBox::warning (QApplication::activeWindow (), "Outline",
                          "No active ATHENA window.");
    return;
  }

  if (outline_pane_widget == nullptr) {
    outline_pane_widget= new QTMOutlinePane ();
    QObject::connect (outline_pane_widget, &QObject::destroyed, [] () {
      outline_pane_widget= nullptr;
      outline_pane_dock= nullptr;
    });
  }

  if (outline_pane_dock == nullptr) {
    outline_pane_dock= new ads::CDockWidget ("Outline");
    outline_pane_dock->setObjectName ("athena-outline-pane");
    outline_pane_dock->resize (320, 600);
    outline_pane_dock->setWidget (outline_pane_widget);
    outline_pane_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QObject::connect (outline_pane_dock, &QObject::destroyed, [] () {
      outline_pane_dock= nullptr;
    });
    win->dockManager ()->addDockWidget (
      ads::RightDockWidgetArea, outline_pane_dock);
    win->restoreAdsLayoutState ();
  }

  outline_pane_dock->show ();
  outline_pane_dock->raise ();
  outline_pane_widget->setFocus ();
}
