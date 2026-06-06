/******************************************************************************
* MODULE     : QTMAbout.hpp
* DESCRIPTION: Qt About dialog for TeXmacs Wyvern Edition
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMABOUT_HPP
#define QTMABOUT_HPP

#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>

class QTMAbout : public QDialog {
  Q_OBJECT

public:
  QTMAbout (QWidget* parent = nullptr);
  ~QTMAbout ();

private:
  QVBoxLayout* layout;
  QLabel*      logoLabel;
  QLabel*      infoLabel;
  QPushButton* closeButton;
};

// Glue function
void help_about_qt ();

#endif // QTMABOUT_HPP
