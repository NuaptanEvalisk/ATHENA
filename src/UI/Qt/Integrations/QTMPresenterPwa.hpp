/******************************************************************************
* MODULE     : QTMPresenterPwa.hpp
* DESCRIPTION: Device-admitted local presenter controller (PWA)
* COPYRIGHT  : (C) 2026 ATHENA contributors
******************************************************************************/
#ifndef ATHENA_QTM_PRESENTER_PWA_HPP
#define ATHENA_QTM_PRESENTER_PWA_HPP

#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// All of these functions except the HTTP library's internal callbacks are
// invoked from the Qt GUI thread. The HTTP daemon consumes immutable copies
// of presentation state; it never reaches into BufferActor or live box trees.
void qtm_presenter_pwa_initialize ();
void qtm_presenter_pwa_configure ();
QString qtm_presenter_pwa_status ();
QStringList qtm_presenter_pwa_urls ();
void qtm_presenter_pwa_publish (const QJsonObject& status,
                                const QImage& current,
                                const QImage& next);
void qtm_presenter_pwa_clear ();

#endif
