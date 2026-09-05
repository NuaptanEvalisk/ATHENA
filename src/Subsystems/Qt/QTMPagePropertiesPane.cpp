/******************************************************************************
* MODULE     : QTMPagePropertiesPane.cpp
* DESCRIPTION: Native ADS pane for document page properties
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMPagePropertiesPane.hpp"

#include "QTMMainTabWindow.hpp"
#include "new_buffer.hpp"
#include "qt_utilities.hpp"
#include "qt_widget.hpp"
#include "scheme.hpp"
#include "tm_window.hpp"

#include <DockWidget.h>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

static QTMPagePropertiesPane* page_properties_pane_widget= nullptr;
static ads::CDockWidget* page_properties_pane_dock= nullptr;

static QString
qs (string s) {
  return to_qstring (s);
}

static string
tm_string (const QString& s) {
  return from_qstring (s);
}

static bool
is_header_aux_buffer (url u) {
  QString s= qs (as_string (u));
  return s.startsWith ("tmfs://aux/page-odd-header") ||
         s.startsWith ("tmfs://aux/page-even-header") ||
         s.startsWith ("tmfs://aux/page-odd-footer") ||
         s.startsWith ("tmfs://aux/page-even-footer");
}

static QString
upper_page_type (const QString& value) {
  return value.toUpper ();
}

static QString
lower_page_type (const QString& value) {
  return value.toLower ();
}

QTMPagePropertiesPane::QTMPagePropertiesPane (QWidget* parent)
  : QWidget (parent), loading (false), tabs (new QTabWidget (this)),
    timer (new QTimer (this)), renderingCombo (nullptr),
    pageTypeCombo (nullptr), orientationCombo (nullptr),
    firstPageCombo (nullptr), cropMarksCombo (nullptr),
    userPageWidget (nullptr), pageWidthEdit (nullptr),
    pageHeightEdit (nullptr), marginsTab (nullptr), marginsLayout (nullptr),
    widthMarginCheck (nullptr), sameScreenMarginsCheck (nullptr),
    pageBreakingCombo (nullptr), pageShrinkCombo (nullptr),
    pageExtendCombo (nullptr), pageFlexibilityCombo (nullptr),
    headersContainer (nullptr), headersLayout (nullptr) {
  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->setContentsMargins (6, 6, 6, 6);
  layout->setSpacing (6);
  layout->addWidget (tabs);

  tabs->addTab (buildFormatTab (), "Format");
  tabs->addTab (buildMarginsTab (), "Margins");
  tabs->addTab (buildBreakingTab (), "Page Breaking");
  tabs->addTab (buildHeadersTab (), "Headers");

  timer->setInterval (700);
  connect (timer, &QTimer::timeout, this, [this] () {
    refreshFromCurrentBuffer ();
  });
  timer->start ();
  refreshFromCurrentBuffer ();
}

QSize
QTMPagePropertiesPane::sizeHint () const {
  return QSize (420, 720);
}

QWidget*
QTMPagePropertiesPane::formPage (QVBoxLayout*& layout) {
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
QTMPagePropertiesPane::combo (const QStringList& values, bool editable) {
  QComboBox* box= new QComboBox (this);
  box->setEditable (editable);
  box->addItems (values);
  box->setSizeAdjustPolicy (QComboBox::AdjustToMinimumContentsLengthWithIcon);
  return box;
}

QLineEdit*
QTMPagePropertiesPane::lineEdit () {
  QLineEdit* edit= new QLineEdit (this);
  edit->setClearButtonEnabled (true);
  return edit;
}

void
QTMPagePropertiesPane::addRow (QFormLayout* form, const QString& label,
                               QWidget* control) {
  form->addRow (label + ":", control);
}

void
QTMPagePropertiesPane::addStringRow (QFormLayout* form, const QString& label,
                                     const QString& variable) {
  QLineEdit* edit= lineEdit ();
  marginEdits[variable]= edit;
  addRow (form, label, edit);
  connect (edit, &QLineEdit::editingFinished, this, [this, edit, variable] () {
    if (!loading) setString (variable, edit->text ());
  });
}

void
QTMPagePropertiesPane::addResetButton (QVBoxLayout* layout,
                                       const QStringList& variables) {
  QHBoxLayout* row= new QHBoxLayout ();
  row->addStretch (1);
  QPushButton* reset= new QPushButton ("Reset", this);
  row->addWidget (reset);
  layout->addLayout (row);
  connect (reset, &QPushButton::clicked, this, [this, variables] () {
    resetVariables (variables);
    refreshAll ();
  });
}

QWidget*
QTMPagePropertiesPane::buildFormatTab () {
  QVBoxLayout* layout;
  QWidget* scroll= formPage (layout);
  QWidget* page= qobject_cast<QScrollArea*> (scroll)->widget ();
  QFormLayout* form= new QFormLayout ();
  form->setFieldGrowthPolicy (QFormLayout::AllNonFixedFieldsGrow);

  renderingCombo= combo (QStringList () << "Paper" << "Scroll" << "Reflow"
                                        << "Slides" << "Book" << "Panorama");
  addRow (form, "Page rendering", renderingCombo);
  connect (renderingCombo, &QComboBox::currentTextChanged,
           this, [this] (const QString& text) {
    if (!loading) {
      qt_call_in_buffer (targetBuffer, "init-page-rendering",
            object (tm_string (encodeRendering (text))));
      refreshFormat ();
    }
  });

  pageTypeCombo= combo (QStringList (), false);
  addRow (form, "Page type", pageTypeCombo);
  connect (pageTypeCombo, &QComboBox::currentTextChanged,
           this, [this] (const QString& text) {
    if (!loading) {
      QString value= lower_page_type (text);
      setString ("page-type", value);
      if (value != "user") {
        setString ("page-width", "auto");
        setString ("page-height", "auto");
      }
      refreshFormat ();
    }
  });

  orientationCombo= combo (QStringList () << "portrait" << "landscape");
  addRow (form, "Orientation", orientationCombo);
  connect (orientationCombo, &QComboBox::currentTextChanged,
           this, [this] (const QString& text) {
    if (!loading) setString ("page-orientation", text);
  });

  firstPageCombo= combo (QStringList (), true);
  addRow (form, "First page", firstPageCombo);
  connect (firstPageCombo, &QComboBox::currentTextChanged,
           this, [this] (const QString& text) {
    if (!loading) setString ("page-first", text);
  });

  cropMarksCombo= combo (QStringList () << "None" << "A3" << "A4"
                                        << "Letter");
  addRow (form, "Crop marks", cropMarksCombo);
  connect (cropMarksCombo, &QComboBox::currentTextChanged,
           this, [this] (const QString& text) {
    if (!loading) setString ("page-crop-marks", encodeCropMarks (text));
  });

  layout->addLayout (form);

  userPageWidget= new QGroupBox ("User page size", page);
  QFormLayout* userForm= new QFormLayout (userPageWidget);
  pageWidthEdit= lineEdit ();
  pageHeightEdit= lineEdit ();
  userForm->addRow ("Page width:", pageWidthEdit);
  userForm->addRow ("Page height:", pageHeightEdit);
  connect (pageWidthEdit, &QLineEdit::editingFinished, this, [this] () {
    if (!loading) setString ("page-width", pageWidthEdit->text ());
  });
  connect (pageHeightEdit, &QLineEdit::editingFinished, this, [this] () {
    if (!loading) setString ("page-height", pageHeightEdit->text ());
  });
  layout->addWidget (userPageWidget);

  addResetButton (layout, QStringList ()
    << "page-medium" << "page-type" << "page-orientation" << "page-border"
    << "page-packet" << "page-offset" << "page-width" << "page-height"
    << "page-crop-marks");
  layout->addStretch (1);
  return scroll;
}

QWidget*
QTMPagePropertiesPane::buildMarginsTab () {
  QVBoxLayout* layout;
  QWidget* scroll= formPage (layout);
  marginsTab= qobject_cast<QScrollArea*> (scroll)->widget ();
  marginsLayout= layout;
  return scroll;
}

QWidget*
QTMPagePropertiesPane::buildBreakingTab () {
  QVBoxLayout* layout;
  QWidget* scroll= formPage (layout);
  QFormLayout* form= new QFormLayout ();
  form->setFieldGrowthPolicy (QFormLayout::AllNonFixedFieldsGrow);

  pageBreakingCombo= combo (QStringList () << "LibreOffice flavor"
                                           << "TeX flavor");
  addRow (form, "Page breaking algorithm", pageBreakingCombo);
  connect (pageBreakingCombo, &QComboBox::currentTextChanged,
           this, [this] (const QString& text) {
    if (!loading) setString ("page-breaking", encodeBreaking (text));
  });

  pageShrinkCombo= combo (QStringList (), true);
  addRow (form, "Allowed page height reduction", pageShrinkCombo);
  connect (pageShrinkCombo, &QComboBox::currentTextChanged,
           this, [this] (const QString& text) {
    if (!loading) setString ("page-shrink", text);
  });

  pageExtendCombo= combo (QStringList (), true);
  addRow (form, "Allowed page height extension", pageExtendCombo);
  connect (pageExtendCombo, &QComboBox::currentTextChanged,
           this, [this] (const QString& text) {
    if (!loading) setString ("page-extend", text);
  });

  pageFlexibilityCombo= combo (QStringList (), true);
  addRow (form, "Vertical space stretchability", pageFlexibilityCombo);
  connect (pageFlexibilityCombo, &QComboBox::currentTextChanged,
           this, [this] (const QString& text) {
    if (!loading) setString ("page-flexibility", text);
  });

  layout->addLayout (form);
  addResetButton (layout, QStringList ()
    << "page-breaking" << "page-shrink" << "page-extend"
    << "page-flexibility");
  layout->addStretch (1);
  return scroll;
}

QWidget*
QTMPagePropertiesPane::buildHeadersTab () {
  QVBoxLayout* layout;
  QWidget* scroll= formPage (layout);
  headersContainer= qobject_cast<QScrollArea*> (scroll)->widget ();
  headersLayout= layout;

  QHBoxLayout* buttons= new QHBoxLayout ();
  buttons->addWidget (new QLabel ("Insert:", headersContainer));
  QPushButton* tab= new QPushButton ("Tab", headersContainer);
  QPushButton* page= new QPushButton ("Page number", headersContainer);
  QPushButton* apply= new QPushButton ("Apply headers", headersContainer);
  buttons->addWidget (tab);
  buttons->addWidget (page);
  buttons->addStretch (1);
  buttons->addWidget (apply);
  headersLayout->addLayout (buttons);
  connect (tab, &QPushButton::clicked, this, [this] () { insertHeaderTab (); });
  connect (page, &QPushButton::clicked,
           this, [this] () { insertHeaderPageNumber (); });
  connect (apply, &QPushButton::clicked, this, [this] () { applyHeaders (); });
  headersLayout->addStretch (1);
  return scroll;
}

void
QTMPagePropertiesPane::refreshFromCurrentBuffer () {
  url current= get_current_buffer_safe ();
  if (!is_none (current) && !is_header_aux_buffer (current) &&
      as_string (current) != as_string (targetBuffer))
    setTargetBuffer (current);
}

void
QTMPagePropertiesPane::setTargetBuffer (url buffer) {
  targetBuffer= buffer;
  refreshAll ();
}

bool
QTMPagePropertiesPane::targetLooksUsable () const {
  return !is_none (targetBuffer);
}

QString
QTMPagePropertiesPane::getString (const QString& variable) const {
  if (!targetLooksUsable ()) return "";
  try {
    return qs (as_string (qt_call_in_buffer (targetBuffer, "get-init-env",
                                object (tm_string (variable)))));
  }
  catch (...) {
    return "";
  }
}

bool
QTMPagePropertiesPane::getBool (const QString& variable) const {
  if (!targetLooksUsable ()) return false;
  try {
    return as_bool (qt_call_in_buffer (targetBuffer, "style-has?",
                         object (tm_string (variable))));
  }
  catch (...) {
    return false;
  }
}

void
QTMPagePropertiesPane::setString (const QString& variable,
                                  const QString& value) {
  if (!targetLooksUsable ()) return;
  try {
    qt_call_in_buffer (targetBuffer, "init-env", object (tm_string (variable)),
          object (tm_string (value)));
  }
  catch (...) {}
}

void
QTMPagePropertiesPane::resetVariables (const QStringList& variables) {
  if (!targetLooksUsable ()) return;
  array<object> args;
  for (const QString& variable: variables)
    args << object (tm_string (variable));
  try { qt_call_in_buffer (targetBuffer, "init-default", args); }
  catch (...) {}
}

QString
QTMPagePropertiesPane::decodeRendering (const QString& value) const {
  if (value == "automatic") return "Reflow";
  if (value == "papyrus") return "Scroll";
  if (value == "beamer") return "Slides";
  if (value.isEmpty ()) return "Paper";
  QString out= value;
  out[0]= out[0].toUpper ();
  return out;
}

QString
QTMPagePropertiesPane::encodeRendering (const QString& value) const {
  if (value == "Reflow") return "automatic";
  if (value == "Scroll") return "papyrus";
  if (value == "Slides") return "beamer";
  return value.toLower ();
}

QString
QTMPagePropertiesPane::decodeCropMarks (const QString& value) const {
  return value.isEmpty () ? QString ("None") : value.toUpper ();
}

QString
QTMPagePropertiesPane::encodeCropMarks (const QString& value) const {
  return value == "None" ? QString ("") : value.toLower ();
}

QString
QTMPagePropertiesPane::decodeBreaking (const QString& value) const {
  if (value == "sloppy") return "LibreOffice flavor";
  if (value == "professional") return "TeX flavor";
  return value;
}

QString
QTMPagePropertiesPane::encodeBreaking (const QString& value) const {
  if (value == "LibreOffice flavor") return "sloppy";
  if (value == "TeX flavor") return "professional";
  return value;
}

QStringList
QTMPagePropertiesPane::pageSizeValues () const {
  QStringList values;
  if (getBool ("beamer-style"))
    values << "16:9" << "8:5" << "4:3" << "5:4" << "USER";
  else
    values << "A3" << "A4" << "A5" << "B4" << "B5" << "LETTER"
           << "LEGAL" << "EXECUTIVE" << "USER";
  QString current= upper_page_type (getString ("page-type"));
  if (!current.isEmpty () && !values.contains (current)) values.prepend (current);
  return values;
}

void
QTMPagePropertiesPane::refreshAll () {
  loading= true;
  refreshFormat ();
  rebuildMarginsTab ();
  refreshBreaking ();
  refreshHeaders ();
  loading= false;
}

void
QTMPagePropertiesPane::refreshFormat () {
  if (renderingCombo == nullptr) return;
  QSignalBlocker b1 (renderingCombo);
  QSignalBlocker b2 (pageTypeCombo);
  QSignalBlocker b3 (orientationCombo);
  QSignalBlocker b4 (firstPageCombo);
  QSignalBlocker b5 (cropMarksCombo);
  QSignalBlocker b6 (pageWidthEdit);
  QSignalBlocker b7 (pageHeightEdit);

  QString rendering;
  try {
    rendering= qs (as_string (qt_call_in_buffer (
      targetBuffer, "get-init-page-rendering")));
  }
  catch (...) {
    rendering= getString ("page-medium");
  }
  renderingCombo->setCurrentText (decodeRendering (rendering));

  pageTypeCombo->clear ();
  pageTypeCombo->addItems (pageSizeValues ());
  QString pageType= upper_page_type (getString ("page-type"));
  if (pageType.isEmpty ()) pageType= "A4";
  pageTypeCombo->setCurrentText (pageType);

  orientationCombo->setCurrentText (getString ("page-orientation"));
  firstPageCombo->clear ();
  QString first= getString ("page-first");
  firstPageCombo->addItems (QStringList () << first << "");
  firstPageCombo->setCurrentText (first);
  cropMarksCombo->setCurrentText (decodeCropMarks (getString ("page-crop-marks")));

  bool userPage= lower_page_type (pageType) == "user";
  userPageWidget->setVisible (userPage);
  pageWidthEdit->setText (getString ("page-width"));
  pageHeightEdit->setText (getString ("page-height"));
}

void
QTMPagePropertiesPane::rebuildMarginsTab () {
  if (marginsLayout == nullptr) return;
  while (QLayoutItem* item= marginsLayout->takeAt (0)) {
    if (item->widget () != nullptr) item->widget ()->deleteLater ();
    delete item;
  }
  marginEdits.clear ();

  bool latex= getBool ("std-latex-dtd");
  if (latex) {
    QLabel* note= new QLabel ("This style specifies page margins in the TeX way",
                              marginsTab);
    note->setWordWrap (true);
    marginsLayout->addWidget (note);
  }

  QGroupBox* toggles= new QGroupBox ("Margin behavior", marginsTab);
  QVBoxLayout* toggleLayout= new QVBoxLayout (toggles);
  if (!latex) {
    widthMarginCheck= new QCheckBox ("Determine margins from text width",
                                     toggles);
    toggleLayout->addWidget (widthMarginCheck);
    connect (widthMarginCheck, &QCheckBox::toggled, this, [this] (bool on) {
      if (!loading) {
        setString ("page-width-margin", on ? "true" : "false");
        rebuildMarginsTab ();
      }
    });
  }
  else widthMarginCheck= nullptr;

  sameScreenMarginsCheck= new QCheckBox ("Same screen margins as on paper",
                                         toggles);
  toggleLayout->addWidget (sameScreenMarginsCheck);
  connect (sameScreenMarginsCheck, &QCheckBox::toggled,
           this, [this] (bool on) {
    if (!loading) {
      setString ("page-screen-margin", on ? "false" : "true");
      rebuildMarginsTab ();
    }
  });
  marginsLayout->addWidget (toggles);

  if (!latex) {
    QGroupBox* paper= new QGroupBox ("Margins on paper", marginsTab);
    QFormLayout* form= new QFormLayout (paper);
    form->setFieldGrowthPolicy (QFormLayout::AllNonFixedFieldsGrow);
    if (getString ("page-width-margin") == "true") {
      addStringRow (form, "Text width", "par-width");
      addStringRow (form, "Odd page shift", "page-odd-shift");
      addStringRow (form, "Even page shift", "page-even-shift");
      addStringRow (form, "Top", "page-top");
      addStringRow (form, "Bottom", "page-bot");
    }
    else {
      addStringRow (form, "(Odd page) Left", "page-odd");
      addStringRow (form, "(Even page) Left", "page-even");
      addStringRow (form, "(Odd page) Right", "page-right");
      addStringRow (form, "Top", "page-top");
      addStringRow (form, "Bottom", "page-bot");
    }
    marginsLayout->addWidget (paper);
  }
  else {
    QGroupBox* horizontal= new QGroupBox ("Horizontal margins", marginsTab);
    QFormLayout* hform= new QFormLayout (horizontal);
    hform->setFieldGrowthPolicy (QFormLayout::AllNonFixedFieldsGrow);
    addStringRow (hform, "oddsidemargin", "tex-odd-side-margin");
    addStringRow (hform, "evensidemargin", "tex-even-side-margin");
    addStringRow (hform, "textwidth", "tex-text-width");
    addStringRow (hform, "linewidth", "tex-line-width");
    addStringRow (hform, "columnwidth", "tex-column-width");
    marginsLayout->addWidget (horizontal);

    QGroupBox* vertical= new QGroupBox ("Vertical margins", marginsTab);
    QFormLayout* vform= new QFormLayout (vertical);
    vform->setFieldGrowthPolicy (QFormLayout::AllNonFixedFieldsGrow);
    addStringRow (vform, "topmargin", "tex-top-margin");
    addStringRow (vform, "headheight", "tex-head-height");
    addStringRow (vform, "headsep", "tex-head-sep");
    addStringRow (vform, "textheight", "tex-text-height");
    addStringRow (vform, "footskip", "tex-foot-skip");
    marginsLayout->addWidget (vertical);
  }

  if (getString ("page-screen-margin") == "true") {
    QGroupBox* screen= new QGroupBox ("Margins on screen", marginsTab);
    QFormLayout* form= new QFormLayout (screen);
    form->setFieldGrowthPolicy (QFormLayout::AllNonFixedFieldsGrow);
    addStringRow (form, "Left", "page-screen-left");
    addStringRow (form, "Right", "page-screen-right");
    addStringRow (form, "Top", "page-screen-top");
    addStringRow (form, "Bottom", "page-screen-bot");
    marginsLayout->addWidget (screen);
  }

  QStringList vars;
  if (latex)
    vars << "tex-odd-side-margin" << "tex-even-side-margin"
         << "tex-text-width" << "tex-line-width" << "tex-column-width"
         << "tex-top-margin" << "tex-head-height" << "tex-head-sep"
         << "tex-text-height" << "tex-foot-skip";
  else
    vars << "page-odd" << "page-even" << "page-right" << "page-top"
         << "page-bot" << "par-width" << "page-odd-shift"
         << "page-even-shift";
  vars << "page-screen-left" << "page-screen-right" << "page-screen-top"
       << "page-screen-bot" << "page-width-margin" << "page-screen-margin";
  addResetButton (marginsLayout, vars);
  marginsLayout->addStretch (1);
  refreshMargins ();
}

void
QTMPagePropertiesPane::refreshMargins () {
  if (widthMarginCheck != nullptr) {
    QSignalBlocker blocker (widthMarginCheck);
    widthMarginCheck->setChecked (getString ("page-width-margin") == "true");
  }
  if (sameScreenMarginsCheck != nullptr) {
    QSignalBlocker blocker (sameScreenMarginsCheck);
    sameScreenMarginsCheck->setChecked (
      getString ("page-screen-margin") != "true");
  }

  for (auto it= marginEdits.begin (); it != marginEdits.end (); ++it) {
    QSignalBlocker blocker (it.value ());
    it.value ()->setText (getString (it.key ()));
  }
}

void
QTMPagePropertiesPane::refreshBreaking () {
  if (pageBreakingCombo == nullptr) return;
  QSignalBlocker b1 (pageBreakingCombo);
  QSignalBlocker b2 (pageShrinkCombo);
  QSignalBlocker b3 (pageExtendCombo);
  QSignalBlocker b4 (pageFlexibilityCombo);

  pageBreakingCombo->setCurrentText (decodeBreaking (getString ("page-breaking")));

  auto setEditableValues= [] (QComboBox* box, const QString& current,
                              const QStringList& base) {
    box->clear ();
    QStringList values;
    if (!current.isEmpty ()) values << current;
    for (const QString& value: base)
      if (!values.contains (value)) values << value;
    box->addItems (values);
    box->setCurrentText (current);
  };
  setEditableValues (pageShrinkCombo, getString ("page-shrink"),
                     QStringList () << "0cm" << "0.5cm" << "1cm" << "");
  setEditableValues (pageExtendCombo, getString ("page-extend"),
                     QStringList () << "0cm" << "0.5cm" << "1cm" << "");
  setEditableValues (pageFlexibilityCombo, getString ("page-flexibility"),
                     QStringList () << "0" << "0.25" << "0.5" << "0.75"
                                    << "1" << "");
}

void
QTMPagePropertiesPane::refreshHeaders () {
  rebuildHeaderEditors ();
}

void
QTMPagePropertiesPane::rebuildHeaderEditors () {
  if (headersLayout == nullptr) return;
  while (headersLayout->count () > 1) {
    QLayoutItem* item= headersLayout->takeAt (1);
    if (item->widget () != nullptr) item->widget ()->deleteLater ();
    delete item;
  }
  headerWidgets.clear ();
  headerQtWidgets.clear ();

  struct HeaderSpec { const char* variable; const char* label; };
  const HeaderSpec specs[] = {
    { "page-odd-header", "Odd page header" },
    { "page-even-header", "Even page header" },
    { "page-odd-footer", "Odd page footer" },
    { "page-even-footer", "Even page footer" }
  };

  tree style= compound ("style", tuple ("generic", "gui-base"));
  for (const HeaderSpec& spec: specs) {
    QGroupBox* group= new QGroupBox (spec.label, headersContainer);
    QVBoxLayout* groupLayout= new QVBoxLayout (group);
    tree body;
    try {
      body= as_tree (qt_call_in_buffer (targetBuffer, "get-init-tree",
                           object (string (spec.variable))));
    }
    catch (...) {
      body= "";
    }
    tree doc (DOCUMENT, body);
    url inputUrl (string ("tmfs://aux/") * string (spec.variable));
    widget input= texmacs_input_widget (doc, style, inputUrl);
    QWidget* qwid= concrete (input)->as_qwidget (group);
    qwid->setMinimumHeight (70);
    qwid->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Fixed);
    groupLayout->addWidget (qwid);
    headerWidgets[spec.variable]= input;
    headerQtWidgets[spec.variable]= qwid;
    headersLayout->addWidget (group);
  }
  headersLayout->addStretch (1);
}

void
QTMPagePropertiesPane::applyHeaders () {
  if (!targetLooksUsable ()) return;
  QStringList variables;
  variables << "page-odd-header" << "page-even-header"
            << "page-odd-footer" << "page-even-footer";
  for (const QString& variable: variables) {
    try {
      url inputUrl (string ("tmfs://aux/") * tm_string (variable));
      tree body= get_buffer_body (inputUrl);
      if (is_func (body, DOCUMENT, 1)) body= body[0];
      qt_call_in_buffer (targetBuffer, "init-env-tree",
            object (tm_string (variable)), object (body));
    }
    catch (...) {}
  }
  try { qt_call_in_buffer (targetBuffer, "refresh-window"); }
  catch (...) {}
}

void
QTMPagePropertiesPane::insertHeaderTab () {
  if (!is_header_aux_buffer (get_current_buffer_safe ())) return;
  try { qt_call_in_buffer (get_current_buffer_safe (), "make-htab", object ("5mm")); }
  catch (...) {}
}

void
QTMPagePropertiesPane::insertHeaderPageNumber () {
  if (!is_header_aux_buffer (get_current_buffer_safe ())) return;
  try { qt_call_in_buffer (get_current_buffer_safe (), "make", symbol_object ("page-the-page")); }
  catch (...) {}
}

void
page_properties_pane_show () {
  if (qt_defer_to_main_thread (page_properties_pane_show)) return;
  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr || win->dockManager () == nullptr) {
    QMessageBox::warning (QApplication::activeWindow (), "Page properties",
                          "No active ATHENA window.");
    return;
  }

  if (page_properties_pane_widget == nullptr) {
    page_properties_pane_widget= new QTMPagePropertiesPane ();
    QObject::connect (page_properties_pane_widget, &QObject::destroyed, [] () {
      page_properties_pane_widget= nullptr;
      page_properties_pane_dock= nullptr;
    });
  }

  if (page_properties_pane_dock == nullptr) {
    page_properties_pane_dock= new ads::CDockWidget ("Page properties");
    page_properties_pane_dock->setObjectName ("athena-page-properties-pane");
    page_properties_pane_dock->resize (420, 720);
    page_properties_pane_dock->setWidget (page_properties_pane_widget);
    page_properties_pane_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QObject::connect (page_properties_pane_dock, &QObject::destroyed, [] () {
      page_properties_pane_dock= nullptr;
    });
    win->showAdsDockWidget (page_properties_pane_dock,
                            ads::RightDockWidgetArea);
  }

  win->showAdsDockWidget (page_properties_pane_dock, ads::RightDockWidgetArea);
  page_properties_pane_widget->refreshFromCurrentBuffer ();
  page_properties_pane_widget->setFocus ();
}
