/******************************************************************************
* MODULE     : QTMPersonsExplorer.hpp
* DESCRIPTION: Persons explorer for semantic person tags in an ATHENA vault
* COPYRIGHT  : (C) 2026  Nuaptan
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMPERSONSEXPLORER_HPP
#define QTMPERSONSEXPLORER_HPP

#include "path.hpp"
#include "url.hpp"

#include <QMap>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <vector>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSizeGrip;
class QSplitter;
class QTimer;

struct QTMPersonOccurrence {
  QString relativePath;
  url     file;
  path    where;
};

class QTMPersonsExplorer : public QWidget {
public:
  QTMPersonsExplorer (QWidget* parent = nullptr);

  QSize sizeHint () const override;
  void setFloatingResizeGripVisible (bool visible);
  void refresh ();

private:
  void scanChunk ();
  void finishScan ();
  void showOccurrences (QListWidgetItem* person);
  void openOccurrence (QListWidgetItem* occurrence);
  QString relativePath (url file) const;

  QListWidget* people;
  QListWidget* occurrences;
  QLabel*      status;
  QPushButton* refreshButton;
  QSizeGrip*   floatingSizeGrip;
  QSplitter*  splitter;
  QTimer*     scanTimer;

  std::vector<url> scanFiles;
  int scanIndex;
  QMap<QString, std::vector<QTMPersonOccurrence>> index;
};

QStringList qtm_vault_person_names ();
void persons_explorer_show ();

#endif // QTMPERSONSEXPLORER_HPP
