/******************************************************************************
* MODULE     : QTMPagePropertiesPane.hpp
* DESCRIPTION: Native ADS pane for document page properties
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMPAGEPROPERTIESPANE_HPP
#define QTMPAGEPROPERTIESPANE_HPP

#include "url.hpp"
#include "widget.hpp"

#include <QHash>
#include <QPointer>
#include <QSize>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QGroupBox;
class QLineEdit;
class QTabWidget;
class QTimer;
class QVBoxLayout;

class QTMPagePropertiesPane : public QWidget {
public:
  QTMPagePropertiesPane (QWidget* parent = nullptr);
  QSize sizeHint () const override;

  void refreshFromCurrentBuffer ();

private:
  QWidget* buildFormatTab ();
  QWidget* buildMarginsTab ();
  QWidget* buildBreakingTab ();
  QWidget* buildHeadersTab ();

  void rebuildMarginsTab ();
  void rebuildHeaderEditors ();
  void refreshAll ();
  void refreshFormat ();
  void refreshMargins ();
  void refreshBreaking ();
  void refreshHeaders ();

  QWidget* formPage (QVBoxLayout*& layout);
  QComboBox* combo (const QStringList& values, bool editable = false);
  QLineEdit* lineEdit ();
  void addRow (QFormLayout* form, const QString& label, QWidget* control);
  void addStringRow (QFormLayout* form, const QString& label,
                     const QString& variable);
  void addResetButton (QVBoxLayout* layout, const QStringList& variables);

  QString getString (const QString& variable) const;
  bool getBool (const QString& variable) const;
  void setString (const QString& variable, const QString& value);
  void resetVariables (const QStringList& variables);
  bool targetLooksUsable () const;
  void setTargetBuffer (url buffer);

  QString decodeRendering (const QString& value) const;
  QString encodeRendering (const QString& value) const;
  QString decodeCropMarks (const QString& value) const;
  QString encodeCropMarks (const QString& value) const;
  QString decodeBreaking (const QString& value) const;
  QString encodeBreaking (const QString& value) const;
  QStringList pageSizeValues () const;

  void applyHeaders ();
  void insertHeaderTab ();
  void insertHeaderPageNumber ();

  url targetBuffer;
  bool loading;

  QTabWidget* tabs;
  QTimer* timer;

  QComboBox* renderingCombo;
  QComboBox* pageTypeCombo;
  QComboBox* orientationCombo;
  QComboBox* firstPageCombo;
  QComboBox* cropMarksCombo;
  QWidget* userPageWidget;
  QLineEdit* pageWidthEdit;
  QLineEdit* pageHeightEdit;

  QWidget* marginsTab;
  QVBoxLayout* marginsLayout;
  QCheckBox* widthMarginCheck;
  QCheckBox* sameScreenMarginsCheck;
  QHash<QString,QLineEdit*> marginEdits;

  QComboBox* pageBreakingCombo;
  QComboBox* pageShrinkCombo;
  QComboBox* pageExtendCombo;
  QComboBox* pageFlexibilityCombo;

  QWidget* headersContainer;
  QVBoxLayout* headersLayout;
  QHash<QString,widget> headerWidgets;
  QHash<QString,QPointer<QWidget> > headerQtWidgets;
};

void page_properties_pane_show ();

#endif // QTMPAGEPROPERTIESPANE_HPP
