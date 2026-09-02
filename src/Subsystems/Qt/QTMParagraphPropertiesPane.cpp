/******************************************************************************
* MODULE     : QTMParagraphPropertiesPane.cpp
* DESCRIPTION: Native ADS pane for document paragraph properties
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMParagraphPropertiesPane.hpp"

#include "QTMMainTabWindow.hpp"
#include "new_buffer.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"

#include <DockWidget.h>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

static QTMParagraphPropertiesPane* paragraph_properties_pane_widget= nullptr;
static ads::CDockWidget* paragraph_properties_pane_dock= nullptr;

static QString
qs (string s) {
  return to_qstring (s);
}

static string
tm_string (const QString& s) {
  return from_qstring (s);
}

static QStringList
paragraph_variables () {
  return QStringList ()
    << "par-mode" << "par-flexibility" << "par-hyphen" << "par-spacing"
    << "par-kerning-stretch" << "par-kerning-reduce" << "par-expansion"
    << "par-contraction" << "par-kerning-margin" << "par-width"
    << "par-left" << "par-right" << "par-first" << "par-no-first"
    << "par-sep" << "par-hor-sep" << "par-ver-sep" << "par-line-sep"
    << "par-par-sep" << "par-fnote-sep" << "par-columns"
    << "par-columns-sep";
}

QTMParagraphPropertiesPane::QTMParagraphPropertiesPane (QWidget* parent)
  : QWidget (parent), loading (false), tabs (new QTabWidget (this)),
    timer (new QTimer (this)), alignmentCombo (nullptr),
    firstIndentCombo (nullptr), interlineCombo (nullptr),
    interparagraphCombo (nullptr), columnsCombo (nullptr),
    columnsSepCombo (nullptr), columnsSepLabel (nullptr),
    columnsSepControl (nullptr), lineBreakingCombo (nullptr),
    extraInterlineCombo (nullptr), minimalLineSepCombo (nullptr),
    horizontalCollapseCombo (nullptr), flexibilityCombo (nullptr),
    cjkSpacingCombo (nullptr), stretchCombo (nullptr),
    compressionCombo (nullptr), expansionCombo (nullptr),
    contractionCombo (nullptr), marginKerningCheck (nullptr) {
  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->setContentsMargins (6, 6, 6, 6);
  layout->setSpacing (6);
  layout->addWidget (tabs);

  tabs->addTab (buildBasicTab (), "Basic");
  tabs->addTab (buildAdvancedTab (), "Advanced");

  timer->setInterval (700);
  connect (timer, &QTimer::timeout, this, [this] () {
    refreshFromCurrentBuffer ();
  });
  timer->start ();
  refreshFromCurrentBuffer ();
}

QSize
QTMParagraphPropertiesPane::sizeHint () const {
  return QSize (420, 640);
}

QWidget*
QTMParagraphPropertiesPane::formPage (QVBoxLayout*& layout) {
  QScrollArea* scroll= new QScrollArea (this);
  scroll->setWidgetResizable (true);
  QWidget* page= new QWidget (scroll);
  layout= new QVBoxLayout (page);
  layout->setContentsMargins (10, 10, 10, 10);
  layout->setSpacing (10);
  scroll->setWidget (page);
  return scroll;
}

QComboBox*
QTMParagraphPropertiesPane::combo (const QStringList& values, bool editable) {
  QComboBox* box= new QComboBox (this);
  box->setEditable (editable);
  box->addItems (values);
  box->setSizeAdjustPolicy (QComboBox::AdjustToMinimumContentsLengthWithIcon);
  return box;
}

void
QTMParagraphPropertiesPane::addRow (QFormLayout* form, const QString& label,
                                    QWidget* control) {
  form->addRow (label + ":", control);
}

void
QTMParagraphPropertiesPane::addComboRow (QFormLayout* form,
                                         const QString& label,
                                         const QString& variable,
                                         const QStringList& values,
                                         bool editable) {
  QComboBox* box= combo (values, editable);
  combos[variable]= box;
  addRow (form, label, box);
  connect (box, &QComboBox::currentTextChanged,
           this, [this, variable] (const QString& text) {
    if (!loading) {
      setString (variable, text);
      if (variable == "par-columns") refreshColumnSeparationVisibility ();
    }
  });
}

void
QTMParagraphPropertiesPane::addResetButton (QVBoxLayout* layout) {
  QHBoxLayout* row= new QHBoxLayout ();
  row->addStretch (1);
  QPushButton* reset= new QPushButton ("Reset", this);
  row->addWidget (reset);
  layout->addLayout (row);
  connect (reset, &QPushButton::clicked, this, [this] () {
    resetParagraphVariables ();
    refreshAll ();
  });
}

QWidget*
QTMParagraphPropertiesPane::buildBasicTab () {
  QVBoxLayout* layout;
  QWidget* scroll= formPage (layout);
  QFormLayout* form= new QFormLayout ();
  form->setFieldGrowthPolicy (QFormLayout::AllNonFixedFieldsGrow);

  addComboRow (form, "Alignment", "par-mode",
               QStringList () << "left" << "center" << "right" << "justify",
               false);
  alignmentCombo= combos["par-mode"];

  addComboRow (form, "First indentation", "par-first",
               QStringList () << "0tab" << "1tab" << "-1tab" << "");
  firstIndentCombo= combos["par-first"];

  addComboRow (form, "Interline space", "par-sep",
               QStringList () << "0fn" << "0.2fn" << "0.5fn" << "1fn"
                              << "");
  interlineCombo= combos["par-sep"];

  addComboRow (form, "Interparagraph space", "par-par-sep",
               QStringList () << "0fn" << "0.3333fn" << "0.5fn"
                              << "0.6666fn" << "1fn" << "0.5fns" << "");
  interparagraphCombo= combos["par-par-sep"];

  addComboRow (form, "Number of columns", "par-columns",
               QStringList () << "1" << "2" << "3" << "4" << "5" << "6",
               false);
  columnsCombo= combos["par-columns"];

  QLabel* sepLabel= new QLabel ("Column separation:", this);
  columnsSepCombo= combo (QStringList () << "1fn" << "2fn" << "3fn" << "",
                          true);
  combos["par-columns-sep"]= columnsSepCombo;
  form->addRow (sepLabel, columnsSepCombo);
  columnsSepLabel= sepLabel;
  columnsSepControl= columnsSepCombo;
  connect (columnsSepCombo, &QComboBox::currentTextChanged,
           this, [this] (const QString& text) {
    if (!loading) setString ("par-columns-sep", text);
  });

  layout->addLayout (form);
  addResetButton (layout);
  layout->addStretch (1);
  return scroll;
}

QWidget*
QTMParagraphPropertiesPane::buildAdvancedTab () {
  QVBoxLayout* layout;
  QWidget* scroll= formPage (layout);
  QFormLayout* form= new QFormLayout ();
  form->setFieldGrowthPolicy (QFormLayout::AllNonFixedFieldsGrow);

  addComboRow (form, "Line breaking", "par-hyphen",
               QStringList () << "normal" << "professional", false);
  lineBreakingCombo= combos["par-hyphen"];

  addComboRow (form, "Extra interline space", "par-line-sep",
               QStringList () << "0fn" << "0.025fns" << "0.05fns"
                              << "0.1fns" << "0.2fns" << "0.5fns"
                              << "1fns" << "");
  extraInterlineCombo= combos["par-line-sep"];

  addComboRow (form, "Minimal line separation", "par-ver-sep",
               QStringList () << "0fn" << "0.1fn" << "0.2fn" << "0.5fn"
                              << "1fn" << "");
  minimalLineSepCombo= combos["par-ver-sep"];

  addComboRow (form, "Horizontal collapse distance", "par-hor-sep",
               QStringList () << "0.1fn" << "0.2fn" << "0.5fn" << "1fn"
                              << "2fn" << "5fn" << "10fn" << "100fn"
                              << "");
  horizontalCollapseCombo= combos["par-hor-sep"];

  addComboRow (form, "Space stretchability", "par-flexibility",
               QStringList () << "1" << "2" << "4" << "1000" << "");
  flexibilityCombo= combos["par-flexibility"];

  addComboRow (form, "CJK spacing", "par-spacing",
               QStringList () << "plain" << "quanjiao" << "banjiao"
                              << "hangmobanjiao" << "kaiming",
               false);
  cjkSpacingCombo= combos["par-spacing"];

  addComboRow (form, "Intercharacter stretching", "par-kerning-stretch",
               QStringList () << "auto" << "tolerant" << "0" << "0.02"
                              << "0.05" << "0.1" << "0.2" << "0.5"
                              << "1" << "");
  stretchCombo= combos["par-kerning-stretch"];

  addComboRow (form, "Intercharacter compression", "par-kerning-reduce",
               QStringList () << "auto" << "0" << "0.01" << "0.02"
                              << "0.03" << "0.05" << "0.1" << "0.2"
                              << "");
  compressionCombo= combos["par-kerning-reduce"];

  addComboRow (form, "Character expansion", "par-expansion",
               QStringList () << "auto" << "tolerant" << "0" << "0.01"
                              << "0.02" << "0.05" << "0.1" << "0.2"
                              << "");
  expansionCombo= combos["par-expansion"];

  addComboRow (form, "Character contraction", "par-contraction",
               QStringList () << "auto" << "tolerant" << "0" << "0.01"
                              << "0.02" << "0.05" << "0.1" << "0.2"
                              << "");
  contractionCombo= combos["par-contraction"];

  marginKerningCheck= new QCheckBox ("Use margin kerning (protrusion)", this);
  form->addRow ("", marginKerningCheck);
  connect (marginKerningCheck, &QCheckBox::toggled,
           this, [this] (bool on) {
    if (!loading) setString ("par-kerning-margin", on ? "true" : "false");
  });

  layout->addLayout (form);
  addResetButton (layout);
  layout->addStretch (1);
  return scroll;
}

void
QTMParagraphPropertiesPane::refreshFromCurrentBuffer () {
  url current= get_current_buffer_safe ();
  if (!is_none (current) && as_string (current) != as_string (targetBuffer))
    setTargetBuffer (current);
}

void
QTMParagraphPropertiesPane::setTargetBuffer (url buffer) {
  targetBuffer= buffer;
  refreshAll ();
}

bool
QTMParagraphPropertiesPane::targetLooksUsable () const {
  return !is_none (targetBuffer);
}

QString
QTMParagraphPropertiesPane::getString (const QString& variable) const {
  if (!targetLooksUsable ()) return "";
  try {
    return qs (as_string (call ("initial-get", object (targetBuffer),
                                object (tm_string (variable)))));
  }
  catch (...) {
    return "";
  }
}

void
QTMParagraphPropertiesPane::setString (const QString& variable,
                                       const QString& value) {
  if (!targetLooksUsable ()) return;
  try {
    call ("initial-set", object (targetBuffer), object (tm_string (variable)),
          object (tm_string (value)));
  }
  catch (...) {}
}

void
QTMParagraphPropertiesPane::resetParagraphVariables () {
  if (!targetLooksUsable ()) return;
  array<object> args;
  args << object (targetBuffer);
  for (const QString& variable: paragraph_variables ())
    args << object (tm_string (variable));
  try { call ("initial-default", args); }
  catch (...) {}
}

void
QTMParagraphPropertiesPane::setComboValues (QComboBox* box,
                                            const QString& current,
                                            const QStringList& base) {
  if (box == nullptr) return;
  QSignalBlocker blocker (box);
  box->clear ();
  QStringList values;
  if (!current.isEmpty ()) values << current;
  for (const QString& value: base)
    if (!values.contains (value)) values << value;
  box->addItems (values);
  box->setCurrentText (current);
}

void
QTMParagraphPropertiesPane::refreshAll () {
  loading= true;
  refreshBasic ();
  refreshAdvanced ();
  loading= false;
}

void
QTMParagraphPropertiesPane::refreshBasic () {
  setComboValues (alignmentCombo, getString ("par-mode"),
                  QStringList () << "left" << "center" << "right"
                                 << "justify");
  setComboValues (firstIndentCombo, getString ("par-first"),
                  QStringList () << "0tab" << "1tab" << "-1tab" << "");
  setComboValues (interlineCombo, getString ("par-sep"),
                  QStringList () << "0fn" << "0.2fn" << "0.5fn" << "1fn"
                                 << "");
  setComboValues (interparagraphCombo, getString ("par-par-sep"),
                  QStringList () << "0fn" << "0.3333fn" << "0.5fn"
                                 << "0.6666fn" << "1fn" << "0.5fns"
                                 << "");
  setComboValues (columnsCombo, getString ("par-columns"),
                  QStringList () << "1" << "2" << "3" << "4" << "5"
                                 << "6");
  setComboValues (columnsSepCombo, getString ("par-columns-sep"),
                  QStringList () << "1fn" << "2fn" << "3fn" << "");
  refreshColumnSeparationVisibility ();
}

void
QTMParagraphPropertiesPane::refreshAdvanced () {
  setComboValues (lineBreakingCombo, getString ("par-hyphen"),
                  QStringList () << "normal" << "professional");
  setComboValues (extraInterlineCombo, getString ("par-line-sep"),
                  QStringList () << "0fn" << "0.025fns" << "0.05fns"
                                 << "0.1fns" << "0.2fns" << "0.5fns"
                                 << "1fns" << "");
  setComboValues (minimalLineSepCombo, getString ("par-ver-sep"),
                  QStringList () << "0fn" << "0.1fn" << "0.2fn"
                                 << "0.5fn" << "1fn" << "");
  setComboValues (horizontalCollapseCombo, getString ("par-hor-sep"),
                  QStringList () << "0.1fn" << "0.2fn" << "0.5fn"
                                 << "1fn" << "2fn" << "5fn" << "10fn"
                                 << "100fn" << "");
  setComboValues (flexibilityCombo, getString ("par-flexibility"),
                  QStringList () << "1" << "2" << "4" << "1000" << "");
  setComboValues (cjkSpacingCombo, getString ("par-spacing"),
                  QStringList () << "plain" << "quanjiao" << "banjiao"
                                 << "hangmobanjiao" << "kaiming");
  setComboValues (stretchCombo, getString ("par-kerning-stretch"),
                  QStringList () << "auto" << "tolerant" << "0" << "0.02"
                                 << "0.05" << "0.1" << "0.2" << "0.5"
                                 << "1" << "");
  setComboValues (compressionCombo, getString ("par-kerning-reduce"),
                  QStringList () << "auto" << "0" << "0.01" << "0.02"
                                 << "0.03" << "0.05" << "0.1" << "0.2"
                                 << "");
  setComboValues (expansionCombo, getString ("par-expansion"),
                  QStringList () << "auto" << "tolerant" << "0" << "0.01"
                                 << "0.02" << "0.05" << "0.1" << "0.2"
                                 << "");
  setComboValues (contractionCombo, getString ("par-contraction"),
                  QStringList () << "auto" << "tolerant" << "0" << "0.01"
                                 << "0.02" << "0.05" << "0.1" << "0.2"
                                 << "");
  if (marginKerningCheck != nullptr) {
    QSignalBlocker blocker (marginKerningCheck);
    marginKerningCheck->setChecked (getString ("par-kerning-margin") == "true");
  }
}

void
QTMParagraphPropertiesPane::refreshColumnSeparationVisibility () {
  bool visible= getString ("par-columns") != "1";
  if (columnsSepLabel != nullptr) columnsSepLabel->setVisible (visible);
  if (columnsSepControl != nullptr) columnsSepControl->setVisible (visible);
}

void
paragraph_properties_pane_show () {
  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr || win->dockManager () == nullptr) {
    QMessageBox::warning (QApplication::activeWindow (), "Paragraph properties",
                          "No active ATHENA window.");
    return;
  }

  if (paragraph_properties_pane_widget == nullptr) {
    paragraph_properties_pane_widget= new QTMParagraphPropertiesPane ();
    QObject::connect (paragraph_properties_pane_widget, &QObject::destroyed,
                      [] () {
      paragraph_properties_pane_widget= nullptr;
      paragraph_properties_pane_dock= nullptr;
    });
  }

  if (paragraph_properties_pane_dock == nullptr) {
    paragraph_properties_pane_dock= new ads::CDockWidget ("Paragraph properties");
    paragraph_properties_pane_dock->setObjectName (
      "athena-paragraph-properties-pane");
    paragraph_properties_pane_dock->resize (420, 640);
    paragraph_properties_pane_dock->setWidget (paragraph_properties_pane_widget);
    paragraph_properties_pane_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QObject::connect (paragraph_properties_pane_dock, &QObject::destroyed,
                      [] () {
      paragraph_properties_pane_dock= nullptr;
    });
    win->showAdsDockWidget (paragraph_properties_pane_dock,
                            ads::RightDockWidgetArea);
  }

  win->showAdsDockWidget (paragraph_properties_pane_dock,
                          ads::RightDockWidgetArea);
  paragraph_properties_pane_widget->refreshFromCurrentBuffer ();
  paragraph_properties_pane_widget->setFocus ();
}
