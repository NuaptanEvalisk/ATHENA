/******************************************************************************
* MODULE     : QTMCommutativeDiagramArrowPane.cpp
* DESCRIPTION: Native commutative-diagram arrow properties pane
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
*/

#include "QTMCommutativeDiagramArrowPane.hpp"

#include "QTMMainTabWindow.hpp"
#include "new_buffer.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"

#include <DockWidget.h>
#include <QApplication>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString qs (string s) { return utf8_to_qstring (s); }
string ts (const QString& s) { return from_qstring_utf8 (s); }

class QTMCommutativeDiagramArrowPane: public QWidget {
public:
  explicit QTMCommutativeDiagramArrowPane (QWidget* parent=nullptr)
    : QWidget (parent) {
    QVBoxLayout* outer= new QVBoxLayout (this);
    QScrollArea* scroll= new QScrollArea (this);
    scroll->setWidgetResizable (true);
    QWidget* body= new QWidget (scroll);
    QVBoxLayout* layout= new QVBoxLayout (body);
    QFormLayout* form= new QFormLayout ();

    addCombo (form, "Edge type", "edge-type",
              {"arrow","adjunction","corner","corner-inverse"}, "arrow");
    addCombo (form, "Tail", "tail",
              {"mono","none","maps-to","top-hook","bottom-hook","arrowhead"},
              "none");
    addCombo (form, "Body", "body",
              {"solid","none","dashed","dotted","squiggly","barred",
               "double-barred","bullet-solid","bullet-hollow"}, "solid");
    addCombo (form, "Head", "head",
              {"arrowhead","none","epi","top-harpoon","bottom-harpoon"},
              "arrowhead");
    addCombo (form, "Level", "level", {"1","2","3","4"}, "1");
    addLine (form, "Curve (-5..5)", "curve", "0");
    addLine (form, "Transverse offset", "offset", "0");
    addLine (form, "Shorten source (%)", "shorten-source", "0");
    addLine (form, "Shorten target (%)", "shorten-target", "0");
    addLine (form, "Loop radius (-5..5)", "loop-radius", "3");
    addLine (form, "Loop angle (-180..180)", "loop-angle", "0");
    addCombo (form, "Label alignment", "label-alignment",
              {"left","centre","over","right"}, "left");
    addLine (form, "Label position (0..100)", "label-position", "50");
    addLine (form, "Arrow colour", "color", "black");
    addLine (form, "Label colour", "label-color", "black");
    layout->addLayout (form);

    QHBoxLayout* actions= new QHBoxLayout ();
    QPushButton* reverse= new QPushButton ("Reverse", body);
    QPushButton* flipArrow= new QPushButton ("Flip arrow", body);
    QPushButton* flipLabel= new QPushButton ("Flip label", body);
    actions->addWidget (reverse);
    actions->addWidget (flipArrow);
    actions->addWidget (flipLabel);
    layout->addLayout (actions);
    layout->addStretch (1);
    scroll->setWidget (body);
    outer->addWidget (scroll);

    connect (reverse, &QPushButton::clicked, this,
             [this] () { invoke ("cd-reverse-selected-arrow"); });
    connect (flipArrow, &QPushButton::clicked, this,
             [this] () { invoke ("cd-flip-selected-arrow"); });
    connect (flipLabel, &QPushButton::clicked, this,
             [this] () { invoke ("cd-flip-selected-label"); });
    QTimer* timer= new QTimer (this);
    timer->setInterval (350);
    connect (timer, &QTimer::timeout, this, [this] () { refresh (); });
    timer->start ();
  }

  void retarget (url buffer) { targetBuffer= buffer; refresh (); }

private:
  QString get (const QString& key, const QString& fallback) const {
    if (is_none (targetBuffer)) return fallback;
    try {
      return qs (as_string (qt_call_in_buffer (
        targetBuffer, "cd-selected-option", object (ts (key)),
        object (ts (fallback)))));
    }
    catch (...) { return fallback; }
  }
  void set (const QString& key, const QString& value) {
    if (loading || is_none (targetBuffer)) return;
    try { qt_call_in_buffer (targetBuffer, "cd-set-selected-option",
                             object (ts (key)), object (ts (value))); }
    catch (...) {}
  }
  void invoke (const char* function) {
    if (is_none (targetBuffer)) return;
    try { qt_call_in_buffer (targetBuffer, function); }
    catch (...) {}
    refresh ();
  }
  void addCombo (QFormLayout* form, const QString& label, const QString& key,
                 const QStringList& values, const QString& fallback) {
    QComboBox* box= new QComboBox (this);
    box->addItems (values);
    combos[key]= box; defaults[key]= fallback;
    form->addRow (label + ":", box);
    connect (box, &QComboBox::currentTextChanged, this,
             [this, key] (const QString& value) { set (key, value); });
  }
  void addLine (QFormLayout* form, const QString& label, const QString& key,
                const QString& fallback) {
    QLineEdit* edit= new QLineEdit (this);
    lines[key]= edit; defaults[key]= fallback;
    form->addRow (label + ":", edit);
    connect (edit, &QLineEdit::editingFinished, this,
             [this, key, edit] () { set (key, edit->text ()); });
  }
  void refresh () {
    url currentBuffer= get_current_buffer_safe ();
    if (!is_none (currentBuffer) &&
        (is_none (targetBuffer) ||
         as_string (currentBuffer) != as_string (targetBuffer)))
      targetBuffer= currentBuffer;
    if (is_none (targetBuffer)) return;
    loading= true;
    for (auto it= combos.begin (); it != combos.end (); ++it) {
      QString value= get (it.key (), defaults.value (it.key ()));
      int index= it.value ()->findText (value);
      if (index >= 0) it.value ()->setCurrentIndex (index);
    }
    for (auto it= lines.begin (); it != lines.end (); ++it)
      if (!it.value ()->hasFocus ())
        it.value ()->setText (get (it.key (), defaults.value (it.key ())));
    loading= false;
  }

  bool loading= false;
  url targetBuffer;
  QMap<QString,QComboBox*> combos;
  QMap<QString,QLineEdit*> lines;
  QMap<QString,QString> defaults;
};

QTMCommutativeDiagramArrowPane* arrowWidget= nullptr;
ads::CDockWidget* arrowDock= nullptr;

} // namespace

void commutative_diagram_arrow_pane_show () {
  if (qt_defer_to_main_thread (commutative_diagram_arrow_pane_show)) return;
  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr || win->dockManager () == nullptr) {
    QMessageBox::warning (QApplication::activeWindow (),
                          "Commutative Diagram Arrow", "No active ATHENA window.");
    return;
  }
  if (arrowWidget == nullptr) arrowWidget= new QTMCommutativeDiagramArrowPane;
  arrowWidget->retarget (get_current_buffer_safe ());
  if (arrowDock == nullptr) {
    arrowDock= new ads::CDockWidget ("Commutative Diagram Arrow");
    arrowDock->setObjectName ("athena-commutative-diagram-arrow");
    arrowDock->resize (620, 620);
    arrowDock->setWidget (arrowWidget);
    arrowDock->setFeature (ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QObject::connect (arrowDock, &QObject::destroyed, [] () {
      arrowDock= nullptr; arrowWidget= nullptr;
    });
    win->dockManager ()->addDockWidgetFloating (arrowDock);
    win->scheduleAdsLayoutRestore (arrowDock);
  }
  else if (arrowDock->dockAreaWidget () == nullptr ||
           arrowDock->dockContainer () == nullptr)
    win->dockManager ()->addDockWidgetFloating (arrowDock);
  arrowDock->toggleView (true);
  arrowDock->show ();
  arrowDock->raise ();
}
