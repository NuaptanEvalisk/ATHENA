/******************************************************************************
* MODULE     : QTMSlideshowWindow.hpp
* DESCRIPTION: Independent read-only slideshow window backed by BufferActor
******************************************************************************/
#ifndef ATHENA_QTM_SLIDESHOW_WINDOW_HPP
#define ATHENA_QTM_SLIDESHOW_WINDOW_HPP

#include "App/Runtime/actor_transport.hpp"
#include <QJsonObject>
#include <QString>

// Called on the GUI thread from an actor-published UI continuation. Only IDs
// and page indices cross threads; typesetting and recording remain actor-owned.
void qtm_slideshow_open (athena_actor_id actor, athena_view_id view,
                         int page_count, int initial_page);

// GUI-thread presenter commands shared by the slideshow UI and admitted PWA.
void qtm_slideshow_presenter_action (const QString& command,
                                     const QJsonObject& arguments);

#endif
