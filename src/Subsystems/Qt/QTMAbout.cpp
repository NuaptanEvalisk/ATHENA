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
#include "athena_build_info.hpp"
#include "tm_configure.hpp"
#include "qt_utilities.hpp"
#include "file.hpp"
#include "sys_utils.hpp"
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QSysInfo>
#include <QScreen>
#include <QStringList>
#include <QTabWidget>
#include <QUrl>

static QString
athenaCompilerDescription () {
  return QString::fromLatin1 (ATHENA_COMPILER);
}

static QString
athenaTechnicalInformation () {
  QStringList lines;
  lines << QStringLiteral ("ATHENA version: %1").arg (
             QString::fromLatin1 (ATHENA_APP_VERSION))
        << QStringLiteral ("Build configuration: %1").arg (
             QString::fromLatin1 (ATHENA_BUILD_TYPE))
        << QStringLiteral ("Build date: %1").arg (
             QString::fromLatin1 (BUILD_DATE))
        << QStringLiteral ("Built by: %1").arg (
             QString::fromLatin1 (BUILD_USER))
        << QStringLiteral ("Compiler: %1").arg (athenaCompilerDescription ())
        << QStringLiteral ("Qt: %1 (runtime %2)")
             .arg (QString::fromLatin1 (QT_VERSION_STR),
                   QString::fromLatin1 (qVersion ()))
        << QStringLiteral ("Target: %1 / %2 / %3")
             .arg (QString::fromLatin1 (HOST_OS),
                   QString::fromLatin1 (HOST_VENDOR),
                   QString::fromLatin1 (HOST_CPU))
        << QStringLiteral ("Runtime: %1 / %2")
             .arg (QSysInfo::prettyProductName (),
                   QSysInfo::currentCpuArchitecture ());

#if ATHENA_DEVELOPMENT_BUILD
  lines << QStringLiteral ("Development build: yes");
  if (QString::fromLatin1 (ATHENA_GIT_COMMIT).isEmpty ())
    lines << QStringLiteral ("Git commit: unavailable");
  else {
    QString commit= QString::fromLatin1 (ATHENA_GIT_COMMIT);
#  if ATHENA_GIT_DIRTY
    commit += QStringLiteral (" (modified worktree)");
#  endif
    lines << QStringLiteral ("Git commit: %1").arg (commit);
  }
  if (!QString::fromLatin1 (ATHENA_GIT_BRANCH).isEmpty ())
    lines << QStringLiteral ("Git branch: %1").arg (
               QString::fromLatin1 (ATHENA_GIT_BRANCH));
#else
  lines << QStringLiteral ("Development build: no");
#endif
  return lines.join (QLatin1Char ('\n'));
}

QTMAbout::QTMAbout (QWidget* parent)
  : QDialog (parent)
{
  setWindowTitle ("About ATHENA");
  setMinimumSize (620, 620);

  layout = new QVBoxLayout (this);
  layout->setContentsMargins (24, 20, 24, 20);
  layout->setSpacing (14);

  QTabWidget* tabs= new QTabWidget (this);
  QWidget* overview= new QWidget (tabs);
  QVBoxLayout* overviewLayout= new QVBoxLayout (overview);
  overviewLayout->setContentsMargins (18, 18, 18, 18);
  overviewLayout->setSpacing (14);

  logoLabel = new QLabel (this);
  
  string tm_path = get_env ("ATHENA_PATH");
  url logo_u1 = url_system (tm_path * "/misc/images/ATHENA-512.png");
  url logo_u2 = url_system (tm_path * "/misc/images/ATHENA.svg");
  url logo_u3 = url_system (tm_path * "/misc/pixmaps/ATHENA.xpm");
  url logo_u4 = url_system (tm_path * "/misc/pictures/splash/splashscr.png");
  
  QString logoPath;
  if (exists (logo_u1)) logoPath = to_qstring (as_string (logo_u1));
  else if (exists (logo_u2)) logoPath = to_qstring (as_string (logo_u2));
  else if (exists (logo_u3)) logoPath = to_qstring (as_string (logo_u3));
  else if (exists (logo_u4)) logoPath = to_qstring (as_string (logo_u4));

  if (!logoPath.isEmpty ()) {
    QPixmap pixmap (logoPath);
    if (!pixmap.isNull ()) {
      logoLabel->setPixmap (pixmap.scaled (160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
  }
  logoLabel->setAlignment (Qt::AlignCenter);
  overviewLayout->addWidget (logoLabel);

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
  overviewLayout->addWidget (infoLabel);
  overviewLayout->addStretch (1);

  QWidget* technical= new QWidget (tabs);
  QVBoxLayout* technicalLayout= new QVBoxLayout (technical);
  technicalLayout->setContentsMargins (18, 18, 18, 18);
  technicalLayout->setSpacing (10);
  QLabel* technicalIntro= new QLabel (
    "Build and runtime information for diagnostics and bug reports.",
    technical);
  technicalIntro->setWordWrap (true);
  technicalLayout->addWidget (technicalIntro);

  technicalInfo= new QPlainTextEdit (technical);
  technicalInfo->setReadOnly (true);
  technicalInfo->setLineWrapMode (QPlainTextEdit::NoWrap);
  technicalInfo->setPlainText (athenaTechnicalInformation ());
  technicalLayout->addWidget (technicalInfo, 1);

  tabs->addTab (overview, "About");
  tabs->addTab (technical, "Technical information");
  layout->addWidget (tabs, 1);

  QHBoxLayout* buttonLayout= new QHBoxLayout;
  copyButton= new QPushButton ("Copy technical information", this);
  closeButton = new QPushButton ("Close", this);
  closeButton->setDefault (true);
  buttonLayout->addWidget (copyButton);
  buttonLayout->addStretch (1);
  buttonLayout->addWidget (closeButton);
  layout->addLayout (buttonLayout);

  connect (copyButton, &QPushButton::clicked, this, [this] () {
    QApplication::clipboard ()->setText (technicalInfo->toPlainText ());
  });
  connect (closeButton, SIGNAL (clicked ()), this, SLOT (accept ()));
}

QTMAbout::~QTMAbout () {}

void help_about_qt () {
  if (qt_defer_to_main_thread (help_about_qt)) return;
  QTMAbout about (QApplication::activeWindow ());
  about.exec ();
}
