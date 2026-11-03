/******************************************************************************
* MODULE     : QTMParagraphPropertiesPane.hpp
* DESCRIPTION: Native ADS pane for document paragraph properties
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMPARAGRAPHPROPERTIESPANE_HPP
#define QTMPARAGRAPHPROPERTIESPANE_HPP

#include "url.hpp"

#include <QHash>
#include <QSize>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QTabWidget;
class QTimer;
class QVBoxLayout;

class QTMParagraphPropertiesPane : public QWidget {
public:
  QTMParagraphPropertiesPane (QWidget* parent = nullptr);
  QSize sizeHint () const override;

  void refreshFromCurrentBuffer ();

private:
  QWidget* buildBasicTab ();
  QWidget* buildAdvancedTab ();

  QWidget* formPage (QVBoxLayout*& layout);
  QComboBox* combo (const QStringList& values, bool editable = false);
  void addRow (QFormLayout* form, const QString& label, QWidget* control);
  void addComboRow (QFormLayout* form, const QString& label,
                    const QString& variable, const QStringList& values,
                    bool editable = true);
  void addResetButton (QVBoxLayout* layout);
  void setComboValues (QComboBox* box, const QString& current,
                       const QStringList& base);

  void refreshAll ();
  void refreshBasic ();
  void refreshAdvanced ();
  void refreshColumnSeparationVisibility ();
  void setTargetBuffer (url buffer);
  bool targetLooksUsable () const;
  QString getString (const QString& variable) const;
  void setString (const QString& variable, const QString& value);
  void resetParagraphVariables ();

  url targetBuffer;
  bool loading;
  QTabWidget* tabs;
  QTimer* timer;

  QComboBox* alignmentCombo;
  QComboBox* firstIndentCombo;
  QComboBox* interlineCombo;
  QComboBox* interparagraphCombo;
  QComboBox* columnsCombo;
  QComboBox* columnsSepCombo;
  QWidget* columnsSepLabel;
  QWidget* columnsSepControl;

  QComboBox* lineBreakingCombo;
  QComboBox* extraInterlineCombo;
  QComboBox* minimalLineSepCombo;
  QComboBox* horizontalCollapseCombo;
  QComboBox* flexibilityCombo;
  QComboBox* cjkSpacingCombo;
  QComboBox* stretchCombo;
  QComboBox* compressionCombo;
  QComboBox* expansionCombo;
  QComboBox* contractionCombo;
  QCheckBox* marginKerningCheck;

  QHash<QString,QComboBox*> combos;
};

void paragraph_properties_pane_show ();

#endif // QTMPARAGRAPHPROPERTIESPANE_HPP
