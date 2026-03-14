/******************************************************************************
* MODULE     : QTMAbout.cpp
* DESCRIPTION: Qt About dialog for TeXmacs Wyvern Edition
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMAbout.hpp"
#include "tm_configure.hpp"
#include "qt_utilities.hpp"
#include "file.hpp"
#include "sys_utils.hpp"
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QScreen>

QTMAbout::QTMAbout (QWidget* parent)
  : QDialog (parent)
{
  setWindowTitle ("About ATHENA");
  setMinimumWidth (500);

  layout = new QVBoxLayout (this);
  layout->setContentsMargins (30, 30, 30, 30);
  layout->setSpacing (20);

  logoLabel = new QLabel (this);
  
  string tm_path = get_env ("ATHENA_PATH");
  url logo_u1 = url_system (tm_path * "/misc/pictures/splash/splashscr.png");
  url logo_u2 = url_system (tm_path * "/../misc/pictures/splash/splashscr.png");
  url logo_u3 = url_system (tm_path * "/misc/images/ATHENA-512.png");
  
  QString logoPath;
  if (exists (logo_u1)) logoPath = to_qstring (as_string (logo_u1));
  else if (exists (logo_u2)) logoPath = to_qstring (as_string (logo_u2));
  else if (exists (logo_u3)) logoPath = to_qstring (as_string (logo_u3));

  if (!logoPath.isEmpty ()) {
    QPixmap pixmap (logoPath);
    if (!pixmap.isNull ()) {
      logoLabel->setPixmap (pixmap.scaled (160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
  }
  logoLabel->setAlignment (Qt::AlignCenter);
  layout->addWidget (logoLabel);

  infoLabel = new QLabel (this);
  infoLabel->setTextFormat (Qt::RichText);
  infoLabel->setAlignment (Qt::AlignCenter);
  infoLabel->setWordWrap (true);
  infoLabel->setOpenExternalLinks (true);
  
  QString info = "<h3>Advanced Typesetting and Hypertext Environment for Notes and Archives (ATHENA)</h3>"
                 "<p><b>Version " ATHENA_VERSION "</b></p>"
                 "<p>ATHENA is a fork based on <a href='https://www.texmacs.org'>GNU TeXmacs</a>.<br>"
                 "We gratefully acknowledge and credit the original authors of GNU TeXmacs, "
                 "primarily Joris van der Hoeven, for their foundational work.</p>"
                 "<p>Copyright &copy; 1999-2026 Joris van der Hoeven and others.<br>"
                 "Copyright &copy; 2026 Nuaptan F. Evalisk.</p>"
                 "<p style='font-size: small;'>This program is free software: you can redistribute it and/or modify it "
                 "under the terms of the GNU General Public License as published by the "
                 "Free Software Foundation, either version 3 of the License, or (at your option) any later version.</p>";
  infoLabel->setText (info);
  layout->addWidget (infoLabel);

  closeButton = new QPushButton ("Close", this);
  closeButton->setDefault (true);
  layout->addWidget (closeButton);

  connect (closeButton, SIGNAL (clicked ()), this, SLOT (accept ()));
}

QTMAbout::~QTMAbout () {}

void help_about_qt () {
  QTMAbout about (NULL);
  about.exec ();
}
#include "moc_QTMAbout.cpp"
