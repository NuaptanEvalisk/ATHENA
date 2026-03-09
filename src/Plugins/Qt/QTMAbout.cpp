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
  setWindowTitle ("About TeXmacs Wyvern Edition");
  setMinimumWidth (400);

  layout = new QVBoxLayout (this);
  layout->setContentsMargins (30, 30, 30, 30);
  layout->setSpacing (20);

  logoLabel = new QLabel (this);
  
  string tm_path = get_env ("TEXMACS_PATH");
  url logo_u1 = url_system (tm_path * "/splash/splashscr.png");
  url logo_u2 = url_system (tm_path * "/../splash/splashscr.png");
  url logo_u3 = url_system (tm_path * "/misc/images/texmacs-512.png");
  
  QString logoPath;
  if (exists (logo_u1)) logoPath = to_qstring (as_string (logo_u1));
  else if (exists (logo_u2)) logoPath = to_qstring (as_string (logo_u2));
  else if (exists (logo_u3)) logoPath = to_qstring (as_string (logo_u3));

  if (!logoPath.isEmpty ()) {
    QPixmap pixmap (logoPath);
    if (!pixmap.isNull ()) {
      logoLabel->setPixmap (pixmap.scaled (128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
  }
  logoLabel->setAlignment (Qt::AlignCenter);
  layout->addWidget (logoLabel);

  infoLabel = new QLabel (this);
  infoLabel->setTextFormat (Qt::RichText);
  infoLabel->setAlignment (Qt::AlignCenter);
  infoLabel->setOpenExternalLinks (true);
  
  QString info = "<h2>TeXmacs Wyvern Edition</h2>"
                 "<p><b>Version " TEXMACS_VERSION "</b></p>"
                 "<p>A WYSIWYM Math Knowledge Management environment.</p>"
                 "<p>Forked from GNU TeXmacs.</p>"
                 "<p><a href='https://www.texmacs.org'>www.texmacs.org</a></p>";
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
