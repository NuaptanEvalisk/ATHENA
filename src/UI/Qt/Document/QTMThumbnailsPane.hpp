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
#include <QSize>
#include <QVector>
#include <QWidget>

#include <memory>
#include <string>

class QListWidget;
class QLabel;
class QPushButton;
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
  bool thumbnailIsCurrent (int index) const;

  struct cached_thumbnail {
    QImage image;
    std::uint64_t style_signature= 0;
    std::uint64_t content_signature= 0;
    std::uint64_t manifest_signature= 0;
  };

  QListWidget* list;
  QLabel* message;
  QPushButton* newScreen;
  QTimer* timer;
  std::shared_ptr<QTMRenderConnection> raster;
  QHash<int, cached_thumbnail> cache;
  QVector<thumbnail_page_bounds> pages;
  int highlightedPage= -1;
  athena_actor_id actor= ATHENA_NO_ACTOR;
  athena_view_id view= ATHENA_NO_VIEW;
  std::uint64_t signature= 0;
  std::uint64_t styleSignature= 0;
  std::uint64_t contentSignature= 0;
  bool slideshow= false;
  int activeSlide= -1;
  bool available= false;
  bool writable= false;
  int requestedPage= -1;
  QElapsedTimer requestAge;
  QElapsedTimer layoutSettled;
};

void thumbnails_pane_show ();
void thumbnails_pane_accept_manifest (athena_view_id view, athena_blob_id blob);
bool thumbnails_screen_action (athena_actor_id actor, athena_view_id view,
                               screen_edit_operation operation, int index= -1,
                               std::uint64_t confirmed= 0);
void thumbnails_screen_set_clipboard (const std::string& xml);
void thumbnails_screen_confirm_delete (athena_actor_id actor, athena_view_id view,
                                       int index, std::uint64_t stamp);

#endif
