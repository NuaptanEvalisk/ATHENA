/******************************************************************************
* MODULE     : QTMTablePropertiesPane.cpp
* DESCRIPTION: Native Qt table and cell property panes
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
*/

#include "QTMTablePropertiesPane.hpp"

#include "QTMMainTabWindow.hpp"
#include "new_buffer.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"
#include "server.hpp"

#include <DockWidget.h>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString qs (string s) { return utf8_to_qstring (s); }
string ts (const QString& s) { return from_qstring_utf8 (s); }

class QTMTablePropertiesPane: public QWidget {
public:
  explicit QTMTablePropertiesPane (bool cell, QWidget* parent=nullptr)
    : QWidget (parent), cellMode (cell), refreshTimer (new QTimer (this)) {
    QVBoxLayout* root= new QVBoxLayout (this);
    root->setContentsMargins (8, 8, 8, 8);
    QTabWidget* tabs= new QTabWidget (this);
    tabs->addTab (buildSizeTab (), "Size");
    tabs->addTab (buildSpacingTab (), "Borders & spacing");
    tabs->addTab (buildLayoutTab (), "Layout");
    root->addWidget (tabs, 1);
    refreshTimer->setInterval (500);
    connect (refreshTimer, &QTimer::timeout, this, [this] () { refresh (); });
    refreshTimer->start ();
    refresh ();
  }

  void retarget (url buffer) { targetBuffer= buffer; refresh (); }

private:
  QWidget* page (QFormLayout*& form) {
    QWidget* body= new QWidget (this);
    form= new QFormLayout (body);
    form->setFieldGrowthPolicy (QFormLayout::AllNonFixedFieldsGrow);
    QScrollArea* scroll= new QScrollArea (this);
    scroll->setWidgetResizable (true);
    scroll->setWidget (body);
    return scroll;
  }

  QString get (const QString& key) const {
    if (is_none (targetBuffer)) return QString ();
    try {
      object value= qt_call_in_buffer (
        targetBuffer, cellMode ? "cell-get-format" : "table-get-format",
        object (ts (key)));
      return qs (as_string (value));
    }
    catch (...) { return QString (); }
  }

  void set (const QString& key, const QString& value) {
    if (loading || is_none (targetBuffer)) return;
    try {
      qt_call_in_buffer (
        targetBuffer, cellMode ? "cell-set-format*" : "table-set-format*",
        object (ts (key)), object (ts (value)));
    }
    catch (...) {}
  }

  int integer (const char* function, int fallback) const {
    if (is_none (targetBuffer)) return fallback;
    try { return as_int (qt_call_in_buffer (targetBuffer, function)); }
    catch (...) { return fallback; }
  }

  QLineEdit* line (QFormLayout* form, const QString& label,
                   const QString& key) {
    QLineEdit* edit= new QLineEdit (this);
    lines[key]= edit;
    form->addRow (label + ":", edit);
    connect (edit, &QLineEdit::editingFinished, this,
             [this, key, edit] () { set (key, edit->text ()); });
    return edit;
  }

  QComboBox* mapped (QFormLayout* form, const QString& label,
                     const QString& key,
                     const QList<QPair<QString,QString>>& values) {
    QComboBox* box= new QComboBox (this);
    for (const auto& pair: values) box->addItem (pair.first, pair.second);
    combos[key]= box;
    form->addRow (label + ":", box);
    connect (box, QOverload<int>::of (&QComboBox::currentIndexChanged),
             this, [this, key, box] (int index) {
      if (index >= 0) set (key, box->itemData (index).toString ());
    });
    return box;
  }

  QWidget* buildSizeTab () {
    QFormLayout* form;
    QWidget* result= page (form);
    const QList<QPair<QString,QString>> modes=
      {{"Auto","auto"}, {"Exact","exact"}, {"Minimal","max"}, {"Maximal","min"}};
    if (cellMode) {
      mapped (form, "Width mode", "cell-hmode", modes);
      line (form, "Width", "cell-width");
      line (form, "Horizontal stretch", "cell-hpart");
      mapped (form, "Height mode", "cell-vmode", modes);
      line (form, "Height", "cell-height");
      line (form, "Vertical stretch", "cell-vpart");
      mapped (form, "Text height correction", "cell-vcorrect",
              {{"Off","n"}, {"Bottom","b"}, {"Top","t"}, {"Both","a"}});
    }
    else {
      rows= new QSpinBox (this); rows->setRange (1, 10000);
      columns= new QSpinBox (this); columns->setRange (1, 10000);
      form->addRow ("Rows:", rows);
      form->addRow ("Columns:", columns);
      connect (rows, QOverload<int>::of (&QSpinBox::valueChanged),
               this, [this] (int value) {
        if (!loading && !is_none (targetBuffer))
          try { qt_call_in_buffer (targetBuffer, "table-set-extents",
                                   object (value), object (columns->value ())); }
          catch (...) {}
      });
      connect (columns, QOverload<int>::of (&QSpinBox::valueChanged),
               this, [this] (int value) {
        if (!loading && !is_none (targetBuffer))
          try { qt_call_in_buffer (targetBuffer, "table-set-extents",
                                   object (rows->value ()), object (value)); }
          catch (...) {}
      });
      line (form, "Minimum rows", "table-min-rows");
      line (form, "Maximum rows", "table-max-rows");
      line (form, "Minimum columns", "table-min-cols");
      line (form, "Maximum columns", "table-max-cols");
      mapped (form, "Width mode", "table-hmode", modes);
      line (form, "Width", "table-width");
      mapped (form, "Height mode", "table-vmode", modes);
      line (form, "Height", "table-height");
    }
    return result;
  }

  QWidget* buildSpacingTab () {
    QFormLayout* form;
    QWidget* result= page (form);
    const QString prefix= cellMode ? "cell-" : "table-";
    line (form, "Left border", prefix + "lborder");
    line (form, "Right border", prefix + "rborder");
    line (form, "Top border", prefix + "tborder");
    line (form, "Bottom border", prefix + "bborder");
    line (form, "Left padding", prefix + "lsep");
    line (form, "Right padding", prefix + "rsep");
    line (form, "Top padding", prefix + "tsep");
    line (form, "Bottom padding", prefix + "bsep");
    return result;
  }

  QWidget* buildLayoutTab () {
    QFormLayout* form;
    QWidget* result= page (form);
    if (cellMode) {
      mapped (form, "Horizontal alignment", "cell-halign",
              {{"Left","l"}, {"Center","c"}, {"Right","r"},
               {"Decimal dot","L."}, {"Decimal comma","L,"}});
      mapped (form, "Vertical alignment", "cell-valign",
              {{"Top","t"}, {"Center","c"}, {"Bottom","b"},
               {"Baseline","B"}});
      mapped (form, "Line wrapping", "cell-hyphen",
              {{"Off","n"}, {"Top","t"}, {"Center","c"}, {"Bottom","b"}});
      mapped (form, "Block content", "cell-block",
              {{"Never","no"}, {"When wrapping","auto"}, {"Always","yes"}});
    }
    else {
      mapped (form, "Horizontal alignment", "table-halign",
              {{"Left","l"}, {"Center","c"}, {"Right","r"}});
      mapped (form, "Vertical alignment", "table-valign",
              {{"Axis","f"}, {"Top","t"}, {"Center","c"}, {"Bottom","b"},
               {"Top baseline","T"}, {"Center baseline","C"},
               {"Bottom baseline","B"}});
      pageBreak= new QCheckBox ("Allow page breaking", this);
      form->addRow (QString (), pageBreak);
      connect (pageBreak, &QCheckBox::toggled, this,
               [this] (bool checked) { set ("table-hyphen", checked ? "y" : "n"); });
    }
    return result;
  }

  void refresh () {
    url currentBuffer= get_current_buffer_safe ();
    if (!is_none (currentBuffer) &&
        (is_none (targetBuffer) ||
         as_string (currentBuffer) != as_string (targetBuffer)))
      targetBuffer= currentBuffer;
    if (is_none (targetBuffer)) return;
    loading= true;
    for (auto it= lines.begin (); it != lines.end (); ++it) {
      QString value= get (it.key ());
      if (!it.value ()->hasFocus ()) it.value ()->setText (value);
    }
    for (auto it= combos.begin (); it != combos.end (); ++it) {
      QString value= get (it.key ());
      int index= it.value ()->findData (value);
      if (index >= 0) it.value ()->setCurrentIndex (index);
    }
    if (!cellMode) {
      rows->setValue (integer ("table-nr-rows", 1));
      columns->setValue (integer ("table-nr-columns", 1));
      pageBreak->setChecked (get ("table-hyphen") == "y");
    }
    loading= false;
  }

  bool cellMode;
  bool loading= false;
  url targetBuffer;
  QMap<QString,QLineEdit*> lines;
  QMap<QString,QComboBox*> combos;
  QSpinBox* rows= nullptr;
  QSpinBox* columns= nullptr;
  QCheckBox* pageBreak= nullptr;
  QTimer* refreshTimer;
};

QTMTablePropertiesPane* cellWidget= nullptr;
ads::CDockWidget* cellDock= nullptr;
QTMTablePropertiesPane* tableWidget= nullptr;
ads::CDockWidget* tableDock= nullptr;

void showPane (bool cell) {
  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr || win->dockManager () == nullptr) {
    QMessageBox::warning (QApplication::activeWindow (),
                          cell ? "Cell properties" : "Table properties",
                          "No active ATHENA window.");
    return;
  }
  QTMTablePropertiesPane*& widget= cell ? cellWidget : tableWidget;
  ads::CDockWidget*& dock= cell ? cellDock : tableDock;
  if (widget == nullptr) widget= new QTMTablePropertiesPane (cell);
  widget->retarget (get_current_buffer_safe ());
  if (dock == nullptr) {
    dock= new ads::CDockWidget (cell ? "Cell properties" : "Table properties");
    dock->setObjectName (cell ? "athena-cell-properties" : "athena-table-properties");
    dock->setWidget (widget);
    dock->setFeature (ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QObject::connect (dock, &QObject::destroyed, [cell] () {
      if (cell) { cellDock= nullptr; cellWidget= nullptr; }
      else { tableDock= nullptr; tableWidget= nullptr; }
    });
  }
  win->showAdsDockWidget (dock, ads::RightDockWidgetArea);
  widget->show ();
  widget->setFocus ();
}

} // namespace

void cell_properties_pane_show () {
  if (qt_defer_to_main_thread (cell_properties_pane_show)) return;
  showPane (true);
}

void table_properties_pane_show () {
  if (qt_defer_to_main_thread (table_properties_pane_show)) return;
  showPane (false);
}
