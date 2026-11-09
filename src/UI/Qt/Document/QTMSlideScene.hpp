// Immutable slideshow render layers. GPL-3.0-or-later.
#pragma once
#include "Document/Model/slide_motion.hpp"
#include "App/Runtime/actor_transport.hpp"
#include <QImage>
#include <QRectF>
#include <QSize>
#include <functional>
#include <memory>

class edit_typeset_rep;
struct QTMSlideLayer {
  QImage pixels;
  QRect rect;
  std::vector<int> owners;
};
struct QTMSlideScene {
  QImage full;
  std::vector<QTMSlideLayer> layers;
  std::vector<QRectF> object_bounds;
  athena::slides::motion::page motion;
};
// Called on BufferActor. Only immutable paint records reach the raster worker.
void qtm_record_slide_scene (edit_typeset_rep&, int index, QSize available,
  std::function<void (std::shared_ptr<const QTMSlideScene>)> complete);
// GUI-thread delivery, matched to the presentation session and request.
void qtm_slideshow_accept_scene (athena_actor_id, athena_view_id,
  std::uint64_t session, std::uint64_t serial, int index, QSize requested,
  std::shared_ptr<const QTMSlideScene>);
