/******************************************************************************
* MODULE     : QTMMetadataPropertiesPane.hpp
* DESCRIPTION: Native ADS pane for document metadata
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMMETADATAPROPERTIESPANE_HPP
#define QTMMETADATAPROPERTIESPANE_HPP

#include "url.hpp"

#include <QHash>
#include <QSize>
#include <QWidget>

class QLineEdit;
class QTimer;

class QTMMetadataPropertiesPane : public QWidget {
public:
  QTMMetadataPropertiesPane (QWidget* parent = nullptr);
  QSize sizeHint () const override;

  void refreshFromCurrentBuffer ();

private:
  void buildUi ();
  void refreshAll ();
  void setTargetBuffer (url buffer);
  bool targetLooksUsable () const;
  QString getMetadata (const QString& key) const;
  void setMetadata (const QString& variable, const QString& value);
  void resetMetadata ();

  url targetBuffer;
  bool loading;
  QTimer* timer;
  QHash<QString,QLineEdit*> edits;
};

void metadata_properties_pane_show ();

#endif // QTMMETADATAPROPERTIESPANE_HPP
