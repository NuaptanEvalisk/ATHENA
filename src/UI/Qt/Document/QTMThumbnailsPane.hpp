/******************************************************************************
* MODULE     : QTMThumbnailsPane.hpp
* DESCRIPTION: Page-based presentation navigation backed by shared raster work
******************************************************************************/
#ifndef ATHENA_QTM_THUMBNAILS_PANE_HPP
#define ATHENA_QTM_THUMBNAILS_PANE_HPP

#include "App/Runtime/actor_transport.hpp"
#include "App/Runtime/thumbnail_snapshot.hpp"
#include "UI/Qt/Editor/QTMRenderService.hpp"

#include <QHash>
#include <QElapsedTimer>
#include <QImage>
#include <QSet>
#include <QSize>
#include <QVector>
#include <QWidget>

#include <memory>

class QListWidget;
class QLabel;
class QTimer;
class QHideEvent;
struct actor_viewport_snapshot;

class QTMThumbnailsPane final: public QWidget {
public:
  explicit QTMThumbnailsPane (QWidget* parent= nullptr);
  QSize sizeHint () const override;
  void acceptManifest (athena_view_id view, owned_actor_blob blob);

protected:
  void hideEvent (QHideEvent* event) override;

private:
  void refresh ();
  void updateVisibleThumbnails ();
  void highlightViewportPage (const actor_viewport_snapshot& viewport,
                               double zoom);
  void resetDocument (athena_actor_id actor, athena_view_id view);
  void activatePage (int page);

  QListWidget* list;
  QLabel* message;
  QTimer* timer;
  std::shared_ptr<QTMRenderConnection> raster;
  QHash<int, QImage> cache;
  QVector<thumbnail_page_bounds> pages;
  QSet<int> visibleIcons;
  int highlightedPage= -1;
  athena_actor_id actor= ATHENA_NO_ACTOR;
  athena_view_id view= ATHENA_NO_VIEW;
  std::uint64_t signature= 0;
  std::uint64_t styleSignature= 0;
  std::uint64_t contentSignature= 0;
  bool slideshow= false;
  int activeSlide= -1;
  bool available= false;
  int requestedPage= -1;
  QElapsedTimer requestAge;
  QElapsedTimer layoutSettled;
};

void thumbnails_pane_show ();
void thumbnails_pane_accept_manifest (athena_view_id view, athena_blob_id blob);

#endif
