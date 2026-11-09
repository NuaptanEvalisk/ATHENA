// Retain paint order and transparent animated objects without screenshot crops.
// GPL-3.0-or-later.
#include "UI/Qt/Document/QTMSlideScene.hpp"
#include "UI/Qt/Rendering/qt_renderer.hpp"
#include "UI/Qt/Toolkit/qt_utilities.hpp"
#include "Editor/Engine/edit_typeset.hpp"
#include "Editor/Commands/edit_interface.hpp"
#include <QPaintEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPicture>
#include <QRunnable>
#include <QThreadPool>
#include <algorithm>
#include <cmath>

namespace {
struct record {
  QPicture picture;
  std::vector<int> owners;
};

// QPainter retains all save/restore and clipping state on the source device.
// Each consecutive ownership run gets a fresh recording, in original z order.
class layer_engine final: public QPaintEngine {
  QPainter output;
  std::vector<int> owners;
  std::vector<record> records;
  const athena::slides::motion::page& motion;
public:
  explicit layer_engine (const athena::slides::motion::page& p): QPaintEngine (AllFeatures), motion (p) {}
  bool begin (QPaintDevice*) override { return true; }
  bool end () override { if (output.isActive ()) output.end (); return true; }
  Type type () const override { return User; }
  void updateState (const QPaintEngineState&) override {}
  void source (const path& inverse) {
    std::vector<int> source;
    for (path p= inverse; !is_nil (p); p= p->next) source.push_back (p->item);
    std::reverse (source.begin (), source.end ());
    owners.clear ();
    for (size_t i=0; i<motion.objects.size (); ++i) {
      const auto& prefix= motion.objects[i].source_path;
      if (prefix.size () <= source.size () && std::equal (prefix.begin (), prefix.end (), source.begin ()))
        owners.push_back (int (i));
    }
  }
  std::vector<record> take () { end (); return std::move (records); }
  QPainter& paint () {
    if (records.empty () || records.back ().owners != owners) {
      if (output.isActive ()) output.end ();
      records.push_back ({QPicture (), owners});
      output.begin (&records.back ().picture);
    }
    const auto* p= painter ();
    output.setWorldTransform (p->combinedTransform ());
    output.setPen (p->pen ()); output.setBrush (p->brush ());
    output.setFont (p->font ()); output.setOpacity (p->opacity ());
    output.setBrushOrigin (p->brushOrigin ());
    output.setBackground (p->background ()); output.setBackgroundMode (p->backgroundMode ());
    output.setCompositionMode (p->compositionMode ());
    output.setRenderHints (output.renderHints (), false);
    output.setRenderHints (p->renderHints ());
    output.setClipping (false);
    if (p->hasClipping ()) output.setClipPath (p->clipPath ());
    return output;
  }
  void drawRects (const QRectF* r, int n) override { paint ().drawRects (r, n); }
  void drawLines (const QLineF* r, int n) override { paint ().drawLines (r, n); }
  void drawEllipse (const QRectF& r) override { paint ().drawEllipse (r); }
  void drawPath (const QPainterPath& p) override { paint ().drawPath (p); }
  void drawPolygon (const QPointF* p, int n, PolygonDrawMode mode) override {
    if (mode == PolylineMode) paint ().drawPolyline (p, n);
    else if (mode == ConvexMode) paint ().drawConvexPolygon (p, n);
    else paint ().drawPolygon (p, n, mode == WindingMode ? Qt::WindingFill : Qt::OddEvenFill);
  }
  void drawImage (const QRectF& r, const QImage& image, const QRectF& source, Qt::ImageConversionFlags flags) override {
    paint ().drawImage (r, image, source, flags);
  }
  void drawPixmap (const QRectF& r, const QPixmap& image, const QRectF& source) override {
    paint ().drawImage (r, image.toImage (), source);
  }
  void drawTextItem (const QPointF& p, const QTextItem& text) override { paint ().drawTextItem (p, text); }
};
class layer_device final: public QPaintDevice {
  QSize size;
public:
  layer_engine engine;
  layer_device (QSize s, const athena::slides::motion::page& p): size (s), engine (p) {}
  QPaintEngine* paintEngine () const override { return const_cast<layer_engine*> (&engine); }
protected:
  int metric (PaintDeviceMetric m) const override {
    switch (m) {
    case PdmWidth: return size.width ();
    case PdmHeight: return size.height ();
    case PdmWidthMM: return qRound (size.width ()*25.4/96.0);
    case PdmHeightMM: return qRound (size.height ()*25.4/96.0);
    case PdmDepth: return 32;
    case PdmDpiX: case PdmDpiY: case PdmPhysicalDpiX: case PdmPhysicalDpiY: return 96;
    case PdmDevicePixelRatio: return 1;
    case PdmDevicePixelRatioScaled: return devicePixelRatioFScale ();
    default: return 0;
    }
  }
};
}

void qtm_record_slide_scene (edit_typeset_rep& typeset, int index, QSize available,
  std::function<void (std::shared_ptr<const QTMSlideScene>)> complete) {
  const auto target= typeset.slideshow_page_box (index);
  const box page= target.page;
  if (is_nil (page)) throw std::runtime_error ("Slideshow page has no layout");
  const double w= page->x2-page->x1, h= page->y2-page->y1;
  const double scale= std::min (available.width ()/w, available.height ()/h);
  const QSize size (std::max (1, int (std::ceil (w*scale))), std::max (1, int (std::ceil (h*scale))));
  auto scene= std::make_shared<QTMSlideScene> ();
  const tree background= effective_document_background (typeset.get_init_value ("bg-color"));
  brush background_brush (background);
  int red, green, blue, alpha;
  get_rgb_color (background_brush->get_color (), red, green, blue, alpha);
  const QColor background_color (red, green, blue, alpha);
  const auto& pages= typeset.slideshow_motion_pages ();
  if (index < int (pages.size ())) scene->motion= pages[index];
  layer_device device (size, scene->motion);
  QPainter painter;
  qt_renderer_rep renderer (&painter, 1.0, size.width (), size.height (), true);
  renderer.begin (&device);
  renderer.set_zoom_factor (scale*std_shrinkf*PIXEL, false);
  renderer.set_background (background);
  renderer.source_paint= [&] (const path& ip) { device.engine.source (ip); };
  typeset.paint_thumbnail_page (&renderer, target);
  renderer.end ();
  auto records= device.engine.take ();
  QThreadPool::globalInstance ()->start (QRunnable::create (
    [scene, records= std::move (records), size, background_color, complete= std::move (complete)] () mutable {
      scene->full= QImage (size, QImage::Format_ARGB32_Premultiplied);
      scene->full.fill (background_color);
      scene->layers.push_back ({scene->full, QRect (QPoint (), size), {}});
      scene->object_bounds.resize (scene->motion.objects.size ());
      QPainter full (&scene->full);
      for (auto& record: records) {
        QRect bounds= record.picture.boundingRect ().adjusted (-2, -2, 2, 2).intersected (QRect (QPoint (), size));
        if (bounds.isEmpty ()) continue;
        QImage pixels (bounds.size (), QImage::Format_ARGB32_Premultiplied);
        pixels.setDotsPerMeterX (qRound (record.picture.logicalDpiX ()/0.0254));
        pixels.setDotsPerMeterY (qRound (record.picture.logicalDpiY ()/0.0254));
        pixels.fill (Qt::transparent);
        QPainter p (&pixels);
        p.translate (-bounds.topLeft ());
        p.drawPicture (0, 0, record.picture);
        p.end ();
        full.drawImage (bounds.topLeft (), pixels);
        for (int owner: record.owners)
          scene->object_bounds[owner]= scene->object_bounds[owner].united (bounds);
        scene->layers.push_back ({std::move (pixels), bounds, std::move (record.owners)});
      }
      full.end ();
      qt_post_to_main_thread ([scene, complete= std::move (complete)] { complete (scene); });
    }));
}
