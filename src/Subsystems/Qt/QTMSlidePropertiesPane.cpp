/******************************************************************************
* MODULE     : QTMSlidePropertiesPane.cpp
* DESCRIPTION: Native slide background properties pane
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
*/

#include "QTMSlidePropertiesPane.hpp"

#include "QTMMainTabWindow.hpp"
#include "QTMNativeDialogs.hpp"
#include "new_buffer.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"
#include "tree.hpp"

#include <DockWidget.h>
#include <QApplication>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {

string tmString (const QString& s) { return from_qstring_utf8 (s); }
QString qString (string s) { return utf8_to_qstring (s); }

QString atomText (const tree& t, const QString& fallback= QString ()) {
  return is_atomic (t) ? qString (t->label) : fallback;
}

tree findEffect (tree effect, const string& kind) {
  if (!is_compound (effect) || N(effect) < 2) return tree ("");
  if (as_string (L(effect)) == kind) return effect;
  return findEffect (effect[0], kind);
}

array<string> backgroundInitial (tree current, const QString& mode) {
  array<string> out;
  if (!is_compound (current, "pattern") || N(current) < 3) return out;
  out << (is_atomic (current[0]) ? current[0]->label : string (""))
      << (is_atomic (current[1]) ? current[1]->label : string ("100%"))
      << (is_atomic (current[2]) ? current[2]->label : string ("100%"));
  tree effect= N(current) >= 4 ? current[3] : tree ("");
  if (mode == "gradient") {
    tree gradient= findEffect (effect, "eff-gradient");
    out << (is_compound (gradient, "eff-gradient") && N(gradient) >= 1 ?
              atomText (gradient[0], "0").toUtf8 ().constData () : string ("0"))
        << (is_compound (gradient, "eff-gradient") && N(gradient) >= 2 ?
              atomText (gradient[1], "black").toUtf8 ().constData () : string ("black"))
        << (is_compound (gradient, "eff-gradient") && N(gradient) >= 3 ?
              atomText (gradient[2], "white").toUtf8 ().constData () : string ("white"));
  }
  else {
    tree recol= findEffect (effect, "eff-recolor");
    tree skin= findEffect (effect, "eff-skin");
    out << (is_compound (recol, "eff-recolor") && N(recol) >= 2 ?
              recol[1]->label : string (""))
        << (is_compound (skin, "eff-skin") && N(skin) >= 2 ?
              skin[1]->label : string (""));
  }
  return out;
}

tree backgroundTree (const QString& mode, const array<string>& values) {
  if (N(values) < 3) return tree ("");
  tree source (values[0]), width (values[1]), height (values[2]);
  if (mode == "gradient" && N(values) >= 6)
    return compound ("pattern", tree ("athena-gradient-vertical"), width, height,
                     compound ("eff-gradient", tree (values[3]),
                               tree (values[4]), tree (values[5])));
  tree effect ("0");
  if (N(values) >= 4 && values[3] != "")
    effect= compound ("eff-recolor", effect, tree (values[3]));
  if (N(values) >= 5 && values[4] != "")
    effect= compound ("eff-skin", effect, tree (values[4]));
  return effect == tree ("0") ? compound ("pattern", source, width, height) :
    compound ("pattern", source, width, height, effect);
}

class QTMSlidePropertiesPane: public QWidget {
public:
  explicit QTMSlidePropertiesPane (QWidget* parent=nullptr): QWidget (parent) {
    QVBoxLayout* layout= new QVBoxLayout (this);
    layout->setContentsMargins (10, 10, 10, 10);
    QLabel* heading= new QLabel ("Slide background", this);
    QFont font= heading->font (); font.setBold (true); heading->setFont (font);
    layout->addWidget (heading);
    current= new QLabel (this);
    current->setWordWrap (true);
    layout->addWidget (current);
    QPushButton* color= new QPushButton ("Color…", this);
    QPushButton* pattern= new QPushButton ("Pattern…", this);
    QPushButton* gradient= new QPushButton ("Gradient…", this);
    QPushButton* picture= new QPushButton ("Picture…", this);
    layout->addWidget (color);
    layout->addWidget (pattern);
    layout->addWidget (gradient);
    layout->addWidget (picture);
    layout->addStretch (1);
    connect (color, &QPushButton::clicked, this, [this] () {
      QColor c= QColorDialog::getColor (Qt::white, this, "Slide background color",
                                        QColorDialog::ShowAlphaChannel);
      if (c.isValid ()) setBackground (tree (tmString (c.name (QColor::HexArgb))));
    });
    connect (pattern, &QPushButton::clicked, this, [this] () { choose ("pattern"); });
    connect (gradient, &QPushButton::clicked, this, [this] () { choose ("gradient"); });
    connect (picture, &QPushButton::clicked, this, [this] () { choose ("picture"); });
    QTimer* timer= new QTimer (this);
    timer->setInterval (500);
    connect (timer, &QTimer::timeout, this, [this] () { refresh (); });
    timer->start ();
  }

  void retarget (url buffer) { targetBuffer= buffer; refresh (); }

private:
  tree background () const {
    if (is_none (targetBuffer)) return tree ("");
    try { return as_tree (qt_call_in_buffer (targetBuffer, "slide-get-bg-color")); }
    catch (...) { return tree (""); }
  }
  void setBackground (tree value) {
    if (is_none (targetBuffer)) return;
    try { qt_call_in_buffer (targetBuffer, "slide-set-bg-color", object (value)); }
    catch (...) {}
    refresh ();
  }
  void choose (const QString& mode) {
    tree old= background ();
    array<string> initial= backgroundInitial (old, mode);
    if (N(initial) == 0) {
      initial << (mode == "gradient" ? string ("athena-gradient-vertical") :
                                       string ("$ATHENA_PATH/misc/patterns/neutral-pattern.png"))
              << (mode == "pattern" ? string ("1cm") : string ("100%"))
              << (mode == "pattern" ? string ("100@") : string ("100%"));
      if (mode == "gradient")
        initial << string ("0") << string ("black") << string ("white");
      else initial << string ("") << string ("");
    }
    array<string> values= qtm_background_selector_dialog (tmString (mode), initial);
    if (N(values) >= 3) setBackground (backgroundTree (mode, values));
  }
  void refresh () {
    url currentBuffer= get_current_buffer_safe ();
    if (!is_none (currentBuffer) &&
        (is_none (targetBuffer) ||
         as_string (currentBuffer) != as_string (targetBuffer)))
      targetBuffer= currentBuffer;
    tree value= background ();
    if (is_atomic (value)) current->setText ("Current: " + qString (value->label));
    else current->setText (
      "Current: " + qString (object_to_string (tree_to_stree (value))));
  }
  url targetBuffer;
  QLabel* current= nullptr;
};

QTMSlidePropertiesPane* slideWidget= nullptr;
ads::CDockWidget* slideDock= nullptr;

} // namespace

void slide_properties_pane_show () {
  if (qt_defer_to_main_thread (slide_properties_pane_show)) return;
  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr || win->dockManager () == nullptr) return;
  if (slideWidget == nullptr) slideWidget= new QTMSlidePropertiesPane;
  slideWidget->retarget (get_current_buffer_safe ());
  if (slideDock == nullptr) {
    slideDock= new ads::CDockWidget ("Slide properties");
    slideDock->setObjectName ("athena-slide-properties");
    slideDock->setWidget (slideWidget);
    slideDock->setFeature (ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QObject::connect (slideDock, &QObject::destroyed, [] () {
      slideDock= nullptr; slideWidget= nullptr;
    });
  }
  win->showAdsDockWidget (slideDock, ads::RightDockWidgetArea);
}
