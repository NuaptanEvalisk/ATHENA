/******************************************************************************
* MODULE     : QTMRenderService.cpp
* DESCRIPTION: Zero-copy Qt command recording and shared frame presentation
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
******************************************************************************/

#include "QTMRenderService.hpp"

#include <QGradient>
#include <QPaintEngine>
#include <QPaintEngineState>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t stream_magic= 0x41544852; // ATHR
constexpr std::uint16_t stream_version= 3;
constexpr std::uint32_t no_gradient= std::numeric_limits<std::uint32_t>::max ();
constexpr std::size_t frame_slot_count= 3;

enum class render_opcode: std::uint16_t {
  frame_begin= 1,
  state_pen,
  state_brush,
  state_transform,
  state_clip,
  state_opacity,
  state_composition,
  state_hints,
  draw_rects,
  draw_lines,
  draw_ellipse,
  draw_path,
  draw_polygon,
  draw_image
};

struct render_command_header {
  render_opcode opcode;
  std::uint16_t reserved= 0;
  std::uint32_t size= 0;
};

struct frame_begin_command {
  std::uint32_t magic;
  std::uint16_t version;
  std::uint16_t reserved= 0;
  std::uint32_t width;
  std::uint32_t height;
  double pixel_ratio;
  std::uint32_t background_argb;
};

struct transform_record { double value[9]; };

struct brush_record {
  std::uint32_t argb;
  std::uint32_t style;
  std::uint32_t gradient_type;
  std::uint32_t spread;
  std::uint32_t coordinate_mode;
  std::uint32_t interpolation_mode;
  std::uint32_t stop_count;
  std::uint32_t reserved= 0;
  athena_resource_id texture;
  transform_record transform;
  double parameter[8];
};

struct gradient_stop_record {
  double position;
  std::uint32_t argb;
  std::uint32_t reserved= 0;
};

struct pen_record {
  double width;
  double miter_limit;
  double dash_offset;
  std::uint32_t style;
  std::uint32_t cap;
  std::uint32_t join;
  std::uint32_t cosmetic;
  std::uint32_t dash_count;
  std::uint32_t brush_size;
};

struct clip_record {
  std::uint32_t enabled;
  std::uint32_t operation;
  std::uint32_t fill_rule;
  std::uint32_t element_count;
};

struct path_record {
  std::uint32_t fill_rule;
  std::uint32_t element_count;
};

struct path_element_record {
  double x;
  double y;
  std::uint32_t type;
  std::uint32_t reserved= 0;
};

struct array_record { std::uint32_t count; std::uint32_t mode; };
struct point_record { double x; double y; };
struct rect_record { double x; double y; double width; double height; };

struct image_record {
  athena_resource_id resource;
  rect_record target;
  rect_record source;
  std::uint32_t conversion_flags;
  std::uint32_t reserved= 0;
};

struct scalar_record { double value; };
struct integer_record { std::uint32_t value; std::uint32_t reserved= 0; };

static_assert (std::is_trivially_copyable<render_command_header>::value);
static_assert (std::is_trivially_copyable<brush_record>::value);
static_assert (std::is_trivially_copyable<image_record>::value);

std::size_t
aligned_size (std::size_t size) noexcept {
  constexpr std::size_t alignment= alignof (std::uint64_t);
  return (size + alignment - 1) & ~(alignment - 1);
}

transform_record
encode_transform (const QTransform& transform) noexcept {
  return transform_record {{
    transform.m11 (), transform.m12 (), transform.m13 (),
    transform.m21 (), transform.m22 (), transform.m23 (),
    transform.m31 (), transform.m32 (), transform.m33 ()
  }};
}

QTransform
decode_transform (const transform_record& transform) {
  return QTransform (
    transform.value[0], transform.value[1], transform.value[2],
    transform.value[3], transform.value[4], transform.value[5],
    transform.value[6], transform.value[7], transform.value[8]);
}

rect_record
encode_rect (const QRectF& rect) noexcept {
  return rect_record {rect.x (), rect.y (), rect.width (), rect.height ()};
}

QRectF
decode_rect (const rect_record& rect) {
  return QRectF (rect.x, rect.y, rect.width, rect.height);
}

std::mutex connection_registry_lock;
std::unordered_map<athena_resource_id, std::weak_ptr<QTMRenderConnection>>
  connection_registry;
athena_resource_id next_connection_id= 1;

athena_resource_id
allocate_connection_id () {
  std::lock_guard<std::mutex> guard (connection_registry_lock);
  athena_resource_id id= next_connection_id++;
  if (id == 0) throw std::overflow_error ("render connection id exhausted");
  return id;
}

} // namespace

struct QTMRenderConnection::processor_state final: render_processor {
  enum frame_status: std::uint32_t {
    frame_idle= 0,
    frame_writing,
    frame_ready,
    frame_displaying
  };

  struct image_resource {
    std::unique_ptr<uchar[]> pixels;
    int width= 0;
    int height= 0;
    int stride= 0;
    QImage::Format format= QImage::Format_Invalid;
    double pixel_ratio= 1.0;
  };

  struct frame_storage {
    std::unique_ptr<uchar[]> pixels;
    std::size_t capacity= 0;
    int width= 0;
    int height= 0;
    int stride= 0;
    double pixel_ratio= 1.0;
    std::uint64_t buffer_generation= 0;
    std::uint64_t frame_generation= 0;
    render_damage damage;
  };

  std::mutex resource_lock;
  std::unordered_map<athena_resource_id, std::unique_ptr<image_resource>> images;
  std::unordered_map<qint64, athena_resource_id> image_cache;
  athena_resource_id next_resource_id= 1;

  std::array<frame_storage, frame_slot_count> frames;
  std::array<std::atomic<std::uint32_t>, frame_slot_count> frame_states;

  int active_frame= -1;
  std::unique_ptr<QImage> active_image;
  std::unique_ptr<QPainter> painter;
  std::uint64_t active_buffer_generation= 0;
  std::uint64_t active_frame_generation= 0;

  processor_state () {
    for (auto& state: frame_states)
      state.store (frame_idle, std::memory_order_relaxed);
  }

  athena_resource_id install_image (const QImage& source) {
    if (source.isNull ()) return 0;
    qint64 key= source.cacheKey ();
    std::lock_guard<std::mutex> guard (resource_lock);
    auto cached= image_cache.find (key);
    if (cached != image_cache.end ()) return cached->second;

    QImage image= source.format () == QImage::Format_ARGB32_Premultiplied ?
      source : source.convertToFormat (QImage::Format_ARGB32_Premultiplied);
    if (image.isNull ()) return 0;
    auto resource= std::make_unique<image_resource> ();
    resource->width= image.width ();
    resource->height= image.height ();
    resource->stride= image.width () * 4;
    resource->format= QImage::Format_ARGB32_Premultiplied;
    resource->pixel_ratio= image.devicePixelRatio ();
    std::size_t size= static_cast<std::size_t> (resource->stride) *
                      static_cast<std::size_t> (resource->height);
    resource->pixels= std::make_unique<uchar[]> (size);
    for (int row= 0; row < resource->height; ++row)
      std::memcpy (resource->pixels.get () + row * resource->stride,
                   image.constScanLine (row), resource->stride);

    athena_resource_id id= next_resource_id++;
    if (id == 0) throw std::overflow_error ("render resource id exhausted");
    images.emplace (id, std::move (resource));
    image_cache.emplace (key, id);
    return id;
  }

  image_resource* image (athena_resource_id id) noexcept {
    std::lock_guard<std::mutex> guard (resource_lock);
    auto found= images.find (id);
    return found == images.end () ? nullptr : found->second.get ();
  }

  int acquire_frame () noexcept {
    for (std::size_t i= 0; i < frame_slot_count; ++i) {
      std::uint32_t expected= frame_idle;
      if (frame_states[i].compare_exchange_strong (
            expected, frame_writing, std::memory_order_acq_rel))
        return static_cast<int> (i);
    }
    int oldest= -1;
    std::uint64_t generation= std::numeric_limits<std::uint64_t>::max ();
    for (std::size_t i= 0; i < frame_slot_count; ++i) {
      if (frame_states[i].load (std::memory_order_acquire) == frame_ready &&
          frames[i].frame_generation < generation) {
        oldest= static_cast<int> (i);
        generation= frames[i].frame_generation;
      }
    }
    if (oldest >= 0) {
      std::uint32_t expected= frame_ready;
      if (frame_states[oldest].compare_exchange_strong (
            expected, frame_writing, std::memory_order_acq_rel))
        return oldest;
    }
    return -1;
  }

  bool begin_frame (const frame_begin_command& begin,
                    const render_chunk_descriptor& descriptor) {
    abandon_frame ();
    if (begin.magic != stream_magic || begin.version != stream_version ||
        begin.width == 0 || begin.height == 0 ||
        !std::isfinite (begin.pixel_ratio) || begin.pixel_ratio <= 0.0)
      return false;
    std::size_t stride= static_cast<std::size_t> (begin.width) * 4;
    if (stride / 4 != begin.width ||
        begin.height > std::numeric_limits<std::size_t>::max () / stride)
      return false;
    std::size_t size= stride * begin.height;
    int slot= acquire_frame ();
    if (slot < 0) return false;
    frame_storage& frame= frames[slot];
    if (frame.capacity < size) {
      frame.pixels= std::make_unique<uchar[]> (size);
      frame.capacity= size;
    }
    frame.width= static_cast<int> (begin.width);
    frame.height= static_cast<int> (begin.height);
    frame.stride= static_cast<int> (stride);
    frame.pixel_ratio= begin.pixel_ratio;
    active_frame= slot;
    active_buffer_generation= descriptor.buffer_generation;
    active_frame_generation= descriptor.frame_generation;
    active_image= std::make_unique<QImage> (
      frame.pixels.get (), frame.width, frame.height, frame.stride,
      QImage::Format_ARGB32_Premultiplied);
    active_image->fill (begin.background_argb);
    painter= std::make_unique<QPainter> (active_image.get ());
    if (!painter->isActive ()) {
      abandon_frame ();
      return false;
    }
    return true;
  }

  void abandon_frame () noexcept {
    if (painter != nullptr && painter->isActive ()) painter->end ();
    painter.reset ();
    active_image.reset ();
    if (active_frame >= 0)
      frame_states[active_frame].store (frame_idle, std::memory_order_release);
    active_frame= -1;
    active_buffer_generation= 0;
    active_frame_generation= 0;
  }

  void finish_frame (const render_chunk_descriptor& descriptor) {
    if (active_frame < 0 || painter == nullptr ||
        descriptor.buffer_generation != active_buffer_generation ||
        descriptor.frame_generation != active_frame_generation) {
      abandon_frame ();
      return;
    }
    painter->end ();
    painter.reset ();
    active_image.reset ();
    frame_storage& frame= frames[active_frame];
    frame.buffer_generation= descriptor.buffer_generation;
    frame.frame_generation= descriptor.frame_generation;
    frame.damage= descriptor.damage;
    int published= active_frame;
    active_frame= -1;
    active_buffer_generation= 0;
    active_frame_generation= 0;
    frame_states[published].store (frame_ready, std::memory_order_release);
  }

  bool decode_brush (const std::byte* data, std::size_t size,
                     QBrush& result, std::size_t& consumed) {
    if (size < sizeof (brush_record)) return false;
    brush_record fixed;
    std::memcpy (&fixed, data, sizeof (fixed));
    std::size_t stops_size= static_cast<std::size_t> (fixed.stop_count) *
                            sizeof (gradient_stop_record);
    if (fixed.stop_count > (size - sizeof (fixed)) /
                           sizeof (gradient_stop_record))
      return false;
    const auto* stops= reinterpret_cast<const gradient_stop_record*> (
      data + sizeof (fixed));
    QGradientStops gradient_stops;
    gradient_stops.reserve (static_cast<int> (fixed.stop_count));
    for (std::uint32_t i= 0; i < fixed.stop_count; ++i)
      gradient_stops.append (
        QGradientStop (stops[i].position, QColor::fromRgba (stops[i].argb)));

    if (fixed.gradient_type == QGradient::LinearGradient) {
      QLinearGradient gradient (
        QPointF (fixed.parameter[0], fixed.parameter[1]),
        QPointF (fixed.parameter[2], fixed.parameter[3]));
      gradient.setStops (gradient_stops);
      gradient.setSpread (static_cast<QGradient::Spread> (fixed.spread));
      gradient.setCoordinateMode (
        static_cast<QGradient::CoordinateMode> (fixed.coordinate_mode));
      result= QBrush (gradient);
    }
    else if (fixed.gradient_type == QGradient::RadialGradient) {
      QRadialGradient gradient (
        QPointF (fixed.parameter[0], fixed.parameter[1]), fixed.parameter[2],
        QPointF (fixed.parameter[3], fixed.parameter[4]), fixed.parameter[5]);
      gradient.setStops (gradient_stops);
      gradient.setSpread (static_cast<QGradient::Spread> (fixed.spread));
      gradient.setCoordinateMode (
        static_cast<QGradient::CoordinateMode> (fixed.coordinate_mode));
      result= QBrush (gradient);
    }
    else if (fixed.gradient_type == QGradient::ConicalGradient) {
      QConicalGradient gradient (
        QPointF (fixed.parameter[0], fixed.parameter[1]), fixed.parameter[2]);
      gradient.setStops (gradient_stops);
      gradient.setCoordinateMode (
        static_cast<QGradient::CoordinateMode> (fixed.coordinate_mode));
      result= QBrush (gradient);
    }
    else if (fixed.texture != 0) {
      image_resource* resource= image (fixed.texture);
      if (resource == nullptr) return false;
      QImage texture (
        resource->pixels.get (), resource->width, resource->height,
        resource->stride, resource->format);
      texture.setDevicePixelRatio (resource->pixel_ratio);
      result= QBrush (texture);
    }
    else
      result= QBrush (
        QColor::fromRgba (fixed.argb),
        static_cast<Qt::BrushStyle> (fixed.style));
    result.setTransform (decode_transform (fixed.transform));
    consumed= sizeof (fixed) + stops_size;
    return true;
  }

  bool replay_path (const std::byte* data, std::size_t size,
                    QPainterPath& path, std::size_t& consumed) {
    if (size < sizeof (path_record)) return false;
    path_record fixed;
    std::memcpy (&fixed, data, sizeof (fixed));
    if (fixed.element_count > (size - sizeof (fixed)) /
                              sizeof (path_element_record))
      return false;
    const auto* elements= reinterpret_cast<const path_element_record*> (
      data + sizeof (fixed));
    path.setFillRule (static_cast<Qt::FillRule> (fixed.fill_rule));
    for (std::uint32_t i= 0; i < fixed.element_count; ++i) {
      if (elements[i].type == QPainterPath::MoveToElement)
        path.moveTo (elements[i].x, elements[i].y);
      else if (elements[i].type == QPainterPath::LineToElement)
        path.lineTo (elements[i].x, elements[i].y);
      else if (elements[i].type == QPainterPath::CurveToElement) {
        if (i + 2 >= fixed.element_count) return false;
        path.cubicTo (
          elements[i].x, elements[i].y,
          elements[i + 1].x, elements[i + 1].y,
          elements[i + 2].x, elements[i + 2].y);
        i += 2;
      }
    }
    consumed= sizeof (fixed) +
              static_cast<std::size_t> (fixed.element_count) *
                sizeof (path_element_record);
    return true;
  }

  bool replay (render_opcode opcode, const std::byte* data, std::size_t size,
               const render_chunk_descriptor& descriptor) {
    if (opcode == render_opcode::frame_begin) {
      if (size != sizeof (frame_begin_command)) return false;
      frame_begin_command begin;
      std::memcpy (&begin, data, sizeof (begin));
      return begin_frame (begin, descriptor);
    }
    if (painter == nullptr || !painter->isActive ()) return false;

    switch (opcode) {
    case render_opcode::state_pen: {
      if (size < sizeof (pen_record)) return false;
      pen_record fixed;
      std::memcpy (&fixed, data, sizeof (fixed));
      if (fixed.brush_size > size - sizeof (fixed)) return false;
      QBrush brush;
      std::size_t brush_used= 0;
      if (!decode_brush (
            data + sizeof (fixed), fixed.brush_size, brush, brush_used) ||
          brush_used != fixed.brush_size)
        return false;
      std::size_t dash_size= static_cast<std::size_t> (fixed.dash_count) *
                             sizeof (double);
      if (dash_size != size - sizeof (fixed) - fixed.brush_size) return false;
      QPen pen (brush, fixed.width,
                static_cast<Qt::PenStyle> (fixed.style),
                static_cast<Qt::PenCapStyle> (fixed.cap),
                static_cast<Qt::PenJoinStyle> (fixed.join));
      pen.setMiterLimit (fixed.miter_limit);
      pen.setCosmetic (fixed.cosmetic != 0);
      pen.setDashOffset (fixed.dash_offset);
      if (fixed.dash_count != 0) {
        const double* values= reinterpret_cast<const double*> (
          data + sizeof (fixed) + fixed.brush_size);
        QVector<qreal> pattern;
        pattern.reserve (static_cast<int> (fixed.dash_count));
        for (std::uint32_t i= 0; i < fixed.dash_count; ++i)
          pattern.append (values[i]);
        pen.setDashPattern (pattern);
      }
      painter->setPen (pen);
      return true;
    }
    case render_opcode::state_brush: {
      QBrush brush;
      std::size_t used= 0;
      if (!decode_brush (data, size, brush, used) || used != size) return false;
      painter->setBrush (brush);
      return true;
    }
    case render_opcode::state_transform: {
      if (size != sizeof (transform_record)) return false;
      transform_record transform;
      std::memcpy (&transform, data, sizeof (transform));
      painter->setTransform (decode_transform (transform));
      return true;
    }
    case render_opcode::state_clip: {
      if (size < sizeof (clip_record)) return false;
      clip_record fixed;
      std::memcpy (&fixed, data, sizeof (fixed));
      if (!fixed.enabled) {
        painter->setClipping (false);
        return size == sizeof (fixed);
      }
      path_record path_fixed {fixed.fill_rule, fixed.element_count};
      std::vector<std::byte> path_bytes (
        sizeof (path_fixed) + size - sizeof (fixed));
      std::memcpy (path_bytes.data (), &path_fixed, sizeof (path_fixed));
      std::memcpy (path_bytes.data () + sizeof (path_fixed),
                   data + sizeof (fixed), size - sizeof (fixed));
      QPainterPath path;
      std::size_t used= 0;
      if (!replay_path (path_bytes.data (), path_bytes.size (), path, used) ||
          used != path_bytes.size ())
        return false;
      painter->setClipPath (
        path, static_cast<Qt::ClipOperation> (fixed.operation));
      return true;
    }
    case render_opcode::state_opacity: {
      if (size != sizeof (scalar_record)) return false;
      scalar_record value;
      std::memcpy (&value, data, sizeof (value));
      painter->setOpacity (value.value);
      return true;
    }
    case render_opcode::state_composition: {
      if (size != sizeof (integer_record)) return false;
      integer_record value;
      std::memcpy (&value, data, sizeof (value));
      painter->setCompositionMode (
        static_cast<QPainter::CompositionMode> (value.value));
      return true;
    }
    case render_opcode::state_hints: {
      if (size != sizeof (integer_record)) return false;
      integer_record value;
      std::memcpy (&value, data, sizeof (value));
      painter->setRenderHints (QPainter::RenderHints (value.value));
      return true;
    }
    case render_opcode::draw_rects: {
      if (size < sizeof (array_record)) return false;
      array_record fixed;
      std::memcpy (&fixed, data, sizeof (fixed));
      if (fixed.count > (size - sizeof (fixed)) / sizeof (rect_record) ||
          sizeof (fixed) + fixed.count * sizeof (rect_record) != size)
        return false;
      const auto* rects= reinterpret_cast<const rect_record*> (
        data + sizeof (fixed));
      for (std::uint32_t i= 0; i < fixed.count; ++i)
        painter->drawRect (decode_rect (rects[i]));
      return true;
    }
    case render_opcode::draw_lines: {
      if (size < sizeof (array_record)) return false;
      array_record fixed;
      std::memcpy (&fixed, data, sizeof (fixed));
      if (fixed.count > (size - sizeof (fixed)) / (2 * sizeof (point_record)) ||
          sizeof (fixed) + fixed.count * 2 * sizeof (point_record) != size)
        return false;
      const auto* points= reinterpret_cast<const point_record*> (
        data + sizeof (fixed));
      for (std::uint32_t i= 0; i < fixed.count; ++i)
        painter->drawLine (
          QPointF (points[2 * i].x, points[2 * i].y),
          QPointF (points[2 * i + 1].x, points[2 * i + 1].y));
      return true;
    }
    case render_opcode::draw_ellipse: {
      if (size != sizeof (rect_record)) return false;
      rect_record rect;
      std::memcpy (&rect, data, sizeof (rect));
      painter->drawEllipse (decode_rect (rect));
      return true;
    }
    case render_opcode::draw_path: {
      QPainterPath path;
      std::size_t used= 0;
      if (!replay_path (data, size, path, used) || used != size) return false;
      painter->drawPath (path);
      return true;
    }
    case render_opcode::draw_polygon: {
      if (size < sizeof (array_record)) return false;
      array_record fixed;
      std::memcpy (&fixed, data, sizeof (fixed));
      if (fixed.count > (size - sizeof (fixed)) / sizeof (point_record) ||
          sizeof (fixed) + fixed.count * sizeof (point_record) != size)
        return false;
      const auto* points= reinterpret_cast<const point_record*> (
        data + sizeof (fixed));
      QPolygonF polygon;
      polygon.reserve (static_cast<int> (fixed.count));
      for (std::uint32_t i= 0; i < fixed.count; ++i)
        polygon.append (QPointF (points[i].x, points[i].y));
      painter->drawPolygon (
        polygon, static_cast<Qt::FillRule> (fixed.mode));
      return true;
    }
    case render_opcode::draw_image: {
      if (size != sizeof (image_record)) return false;
      image_record fixed;
      std::memcpy (&fixed, data, sizeof (fixed));
      image_resource* resource= image (fixed.resource);
      if (resource == nullptr) return false;
      QImage image (
        resource->pixels.get (), resource->width, resource->height,
        resource->stride, resource->format);
      image.setDevicePixelRatio (resource->pixel_ratio);
      painter->drawImage (
        decode_rect (fixed.target), image, decode_rect (fixed.source),
        Qt::ImageConversionFlags (fixed.conversion_flags));
      return true;
    }
    default:
      return false;
    }
  }

  void process (const render_chunk_descriptor& descriptor,
                const std::byte* payload) override {
    if (payload == nullptr || descriptor.used == 0) {
      abandon_frame ();
      return;
    }
    if (active_frame >= 0 &&
        (descriptor.buffer_generation != active_buffer_generation ||
         descriptor.frame_generation != active_frame_generation))
      abandon_frame ();

    std::size_t offset= 0;
    while (offset < descriptor.used) {
      if (descriptor.used - offset < sizeof (render_command_header)) {
        abandon_frame ();
        return;
      }
      render_command_header header;
      std::memcpy (&header, payload + offset, sizeof (header));
      std::size_t command_size= sizeof (header) + header.size;
      std::size_t padded= aligned_size (command_size);
      if (padded > descriptor.used - offset ||
          !replay (header.opcode, payload + offset + sizeof (header),
                   header.size, descriptor)) {
        abandon_frame ();
        return;
      }
      offset += padded;
    }
    if (descriptor.final_chunk) finish_frame (descriptor);
  }
};

namespace {

class render_stream_writer {
public:
  render_stream_writer (
    std::shared_ptr<render_connection> connection,
    std::shared_ptr<QTMRenderConnection::processor_state> processor,
    std::uint64_t buffer_generation, std::uint64_t frame_generation,
    render_damage damage):
    connection_ (std::move (connection)), processor_ (std::move (processor)),
    buffer_generation_ (buffer_generation), frame_generation_ (frame_generation),
    damage_ (damage) {}

  ~render_stream_writer () {
    if (has_chunk_ && !finished_)
      (void) connection_->discard (chunk_.slot);
  }

  std::byte* command (render_opcode opcode, std::size_t payload_size) {
    if (failed_ || finished_ ||
        payload_size > std::numeric_limits<std::uint32_t>::max ())
      return nullptr;
    std::size_t total= aligned_size (
      sizeof (render_command_header) + payload_size);
    if (!ensure (total)) return nullptr;
    auto* header= reinterpret_cast<render_command_header*> (
      chunk_.data + used_);
    *header= render_command_header {
      opcode, 0, static_cast<std::uint32_t> (payload_size)};
    std::byte* payload= chunk_.data + used_ + sizeof (*header);
    std::memset (payload + payload_size, 0,
                 total - sizeof (*header) - payload_size);
    used_ += total;
    return payload;
  }

  athena_resource_id install_image (const QImage& image) {
    return failed_ ? 0 : processor_->install_image (image);
  }

  bool finish () noexcept {
    if (finished_) return !failed_;
    finished_= true;
    if (failed_ || !has_chunk_) return false;
    render_chunk_descriptor descriptor;
    descriptor.slot= chunk_.slot;
    descriptor.used= static_cast<std::uint32_t> (used_);
    descriptor.buffer_generation= buffer_generation_;
    descriptor.frame_generation= frame_generation_;
    descriptor.damage= damage_;
    descriptor.final_chunk= true;
    if (!connection_->publish (descriptor)) {
      (void) connection_->discard (chunk_.slot);
      failed_= true;
    }
    has_chunk_= false;
    return !failed_;
  }

private:
  bool ensure (std::size_t required) {
    if (!has_chunk_) {
      if (!connection_->acquire (chunk_)) {
        failed_= true;
        return false;
      }
      has_chunk_= true;
      used_= 0;
    }
    if (required > chunk_.capacity) {
      failed_= true;
      return false;
    }
    if (required <= chunk_.capacity - used_) return true;
    render_chunk_descriptor descriptor;
    descriptor.slot= chunk_.slot;
    descriptor.used= static_cast<std::uint32_t> (used_);
    descriptor.buffer_generation= buffer_generation_;
    descriptor.frame_generation= frame_generation_;
    descriptor.damage= damage_;
    descriptor.final_chunk= false;
    if (!connection_->publish (descriptor) || !connection_->acquire (chunk_)) {
      failed_= true;
      has_chunk_= false;
      return false;
    }
    used_= 0;
    return true;
  }

  std::shared_ptr<render_connection> connection_;
  std::shared_ptr<QTMRenderConnection::processor_state> processor_;
  render_chunk_arena::writable_chunk chunk_;
  std::size_t used_= 0;
  std::uint64_t buffer_generation_;
  std::uint64_t frame_generation_;
  render_damage damage_;
  bool has_chunk_= false;
  bool failed_= false;
  bool finished_= false;
};

std::size_t
brush_payload_size (const QBrush& brush) {
  const QGradient* gradient= brush.gradient ();
  std::size_t stops= gradient == nullptr ? 0 :
    static_cast<std::size_t> (gradient->stops ().size ());
  return sizeof (brush_record) + stops * sizeof (gradient_stop_record);
}

bool
write_brush (render_stream_writer& writer, const QBrush& brush,
             std::byte* destination, std::size_t size) {
  if (size != brush_payload_size (brush)) return false;
  brush_record fixed {};
  fixed.argb= brush.color ().rgba ();
  fixed.style= static_cast<std::uint32_t> (brush.style ());
  fixed.gradient_type= no_gradient;
  fixed.transform= encode_transform (brush.transform ());
  const QGradient* gradient= brush.gradient ();
  QGradientStops stops;
  if (gradient != nullptr) {
    fixed.gradient_type= static_cast<std::uint32_t> (gradient->type ());
    fixed.spread= static_cast<std::uint32_t> (gradient->spread ());
    fixed.coordinate_mode=
      static_cast<std::uint32_t> (gradient->coordinateMode ());
    fixed.interpolation_mode=
      static_cast<std::uint32_t> (gradient->interpolationMode ());
    stops= gradient->stops ();
    fixed.stop_count= static_cast<std::uint32_t> (stops.size ());
    if (gradient->type () == QGradient::LinearGradient) {
      const auto* linear= static_cast<const QLinearGradient*> (gradient);
      fixed.parameter[0]= linear->start ().x ();
      fixed.parameter[1]= linear->start ().y ();
      fixed.parameter[2]= linear->finalStop ().x ();
      fixed.parameter[3]= linear->finalStop ().y ();
    }
    else if (gradient->type () == QGradient::RadialGradient) {
      const auto* radial= static_cast<const QRadialGradient*> (gradient);
      fixed.parameter[0]= radial->center ().x ();
      fixed.parameter[1]= radial->center ().y ();
      fixed.parameter[2]= radial->radius ();
      fixed.parameter[3]= radial->focalPoint ().x ();
      fixed.parameter[4]= radial->focalPoint ().y ();
      fixed.parameter[5]= radial->focalRadius ();
    }
    else if (gradient->type () == QGradient::ConicalGradient) {
      const auto* conical= static_cast<const QConicalGradient*> (gradient);
      fixed.parameter[0]= conical->center ().x ();
      fixed.parameter[1]= conical->center ().y ();
      fixed.parameter[2]= conical->angle ();
    }
  }
  else if (brush.style () == Qt::TexturePattern)
    fixed.texture= writer.install_image (brush.textureImage ());
  std::memcpy (destination, &fixed, sizeof (fixed));
  auto* encoded_stops= reinterpret_cast<gradient_stop_record*> (
    destination + sizeof (fixed));
  for (int i= 0; i < stops.size (); ++i)
    encoded_stops[i]= gradient_stop_record {
      stops[i].first, stops[i].second.rgba (), 0};
  return gradient == nullptr || fixed.stop_count == stops.size ();
}

class recording_paint_engine final: public QPaintEngine {
public:
  explicit recording_paint_engine (render_stream_writer& writer):
    QPaintEngine (AllFeatures), writer_ (writer) {}

  bool begin (QPaintDevice*) override { return true; }
  bool end () override { return true; }
  Type type () const override { return User; }

  void updateState (const QPaintEngineState& state) override {
    DirtyFlags dirty= state.state ();
    if (dirty.testFlag (DirtyPen)) record_pen (state.pen ());
    if (dirty.testFlag (DirtyBrush)) record_brush (state.brush ());
    if (dirty.testFlag (DirtyTransform))
      write_pod (render_opcode::state_transform,
                 encode_transform (state.transform ()));
    if (dirty.testFlag (DirtyClipEnabled) || dirty.testFlag (DirtyClipPath) ||
        dirty.testFlag (DirtyClipRegion))
      record_clip (state);
    if (dirty.testFlag (DirtyOpacity))
      write_pod (render_opcode::state_opacity,
                 scalar_record {state.opacity ()});
    if (dirty.testFlag (DirtyCompositionMode))
      write_pod (render_opcode::state_composition,
                 integer_record {
                   static_cast<std::uint32_t> (state.compositionMode ()), 0});
    if (dirty.testFlag (DirtyHints))
      write_pod (render_opcode::state_hints,
                 integer_record {
                   static_cast<std::uint32_t> (state.renderHints ()), 0});
  }

  void drawRects (const QRectF* rects, int count) override {
    if (count <= 0) return;
    std::size_t size= sizeof (array_record) +
                      static_cast<std::size_t> (count) * sizeof (rect_record);
    std::byte* payload= writer_.command (render_opcode::draw_rects, size);
    if (payload == nullptr) return;
    array_record fixed {static_cast<std::uint32_t> (count), 0};
    std::memcpy (payload, &fixed, sizeof (fixed));
    auto* encoded= reinterpret_cast<rect_record*> (payload + sizeof (fixed));
    for (int i= 0; i < count; ++i) encoded[i]= encode_rect (rects[i]);
  }

  void drawLines (const QLineF* lines, int count) override {
    if (count <= 0) return;
    std::size_t size= sizeof (array_record) +
      static_cast<std::size_t> (count) * 2 * sizeof (point_record);
    std::byte* payload= writer_.command (render_opcode::draw_lines, size);
    if (payload == nullptr) return;
    array_record fixed {static_cast<std::uint32_t> (count), 0};
    std::memcpy (payload, &fixed, sizeof (fixed));
    auto* points= reinterpret_cast<point_record*> (payload + sizeof (fixed));
    for (int i= 0; i < count; ++i) {
      points[2 * i]= point_record {lines[i].x1 (), lines[i].y1 ()};
      points[2 * i + 1]= point_record {lines[i].x2 (), lines[i].y2 ()};
    }
  }

  void drawEllipse (const QRectF& rect) override {
    write_pod (render_opcode::draw_ellipse, encode_rect (rect));
  }

  void drawPath (const QPainterPath& path) override {
    record_path (render_opcode::draw_path, path);
  }

  void drawPolygon (const QPointF* points, int count,
                    PolygonDrawMode mode) override {
    if (count <= 0) return;
    std::size_t size= sizeof (array_record) +
                      static_cast<std::size_t> (count) * sizeof (point_record);
    std::byte* payload= writer_.command (render_opcode::draw_polygon, size);
    if (payload == nullptr) return;
    Qt::FillRule fill= mode == WindingMode ? Qt::WindingFill : Qt::OddEvenFill;
    array_record fixed {
      static_cast<std::uint32_t> (count), static_cast<std::uint32_t> (fill)};
    std::memcpy (payload, &fixed, sizeof (fixed));
    auto* encoded= reinterpret_cast<point_record*> (payload + sizeof (fixed));
    for (int i= 0; i < count; ++i)
      encoded[i]= point_record {points[i].x (), points[i].y ()};
  }

  void drawImage (const QRectF& target, const QImage& image,
                  const QRectF& source,
                  Qt::ImageConversionFlags flags) override {
    athena_resource_id resource= writer_.install_image (image);
    if (resource == 0) return;
    write_pod (render_opcode::draw_image,
      image_record {resource, encode_rect (target), encode_rect (source),
                    static_cast<std::uint32_t> (flags), 0});
  }

  void drawPixmap (const QRectF& target, const QPixmap& pixmap,
                   const QRectF& source) override {
    drawImage (target, pixmap.toImage (), source, Qt::AutoColor);
  }

  void drawTextItem (const QPointF& position,
                     const QTextItem& text) override {
    QPainterPath path;
    path.addText (position, text.font (), text.text ());
    record_path (render_opcode::draw_path, path);
  }

private:
  template<typename T>
  void write_pod (render_opcode opcode, const T& value) {
    static_assert (std::is_trivially_copyable<T>::value);
    std::byte* payload= writer_.command (opcode, sizeof (T));
    if (payload != nullptr) std::memcpy (payload, &value, sizeof (T));
  }

  void record_brush (const QBrush& brush) {
    std::size_t size= brush_payload_size (brush);
    std::byte* payload= writer_.command (render_opcode::state_brush, size);
    if (payload != nullptr) (void) write_brush (writer_, brush, payload, size);
  }

  void record_pen (const QPen& pen) {
    std::size_t brush_size= brush_payload_size (pen.brush ());
    QVector<qreal> dash= pen.dashPattern ();
    std::size_t size= sizeof (pen_record) + brush_size +
                      static_cast<std::size_t> (dash.size ()) * sizeof (double);
    std::byte* payload= writer_.command (render_opcode::state_pen, size);
    if (payload == nullptr) return;
    pen_record fixed {
      pen.widthF (), pen.miterLimit (), pen.dashOffset (),
      static_cast<std::uint32_t> (pen.style ()),
      static_cast<std::uint32_t> (pen.capStyle ()),
      static_cast<std::uint32_t> (pen.joinStyle ()), pen.isCosmetic () ? 1U : 0U,
      static_cast<std::uint32_t> (dash.size ()),
      static_cast<std::uint32_t> (brush_size)};
    std::memcpy (payload, &fixed, sizeof (fixed));
    if (!write_brush (
          writer_, pen.brush (), payload + sizeof (fixed), brush_size))
      return;
    double* encoded= reinterpret_cast<double*> (
      payload + sizeof (fixed) + brush_size);
    for (int i= 0; i < dash.size (); ++i) encoded[i]= dash[i];
  }

  void record_path (render_opcode opcode, const QPainterPath& path) {
    int count= path.elementCount ();
    if (count < 0) return;
    std::size_t size= sizeof (path_record) +
      static_cast<std::size_t> (count) * sizeof (path_element_record);
    std::byte* payload= writer_.command (opcode, size);
    if (payload == nullptr) return;
    path_record fixed {
      static_cast<std::uint32_t> (path.fillRule ()),
      static_cast<std::uint32_t> (count)};
    std::memcpy (payload, &fixed, sizeof (fixed));
    auto* elements= reinterpret_cast<path_element_record*> (
      payload + sizeof (fixed));
    for (int i= 0; i < count; ++i) {
      QPainterPath::Element element= path.elementAt (i);
      elements[i]= path_element_record {
        element.x, element.y, static_cast<std::uint32_t> (element.type), 0};
    }
  }

  void record_clip (const QPaintEngineState& state) {
    if (!state.isClipEnabled ()) {
      write_pod (render_opcode::state_clip, clip_record {});
      return;
    }
    QPainterPath path= state.clipPath ();
    if (path.isEmpty () && !state.clipRegion ().isEmpty ())
      path.addRegion (state.clipRegion ());
    int count= path.elementCount ();
    std::size_t size= sizeof (clip_record) +
      static_cast<std::size_t> (count) * sizeof (path_element_record);
    std::byte* payload= writer_.command (render_opcode::state_clip, size);
    if (payload == nullptr) return;
    clip_record fixed {
      1, static_cast<std::uint32_t> (state.clipOperation ()),
      static_cast<std::uint32_t> (path.fillRule ()),
      static_cast<std::uint32_t> (count)};
    std::memcpy (payload, &fixed, sizeof (fixed));
    auto* elements= reinterpret_cast<path_element_record*> (
      payload + sizeof (fixed));
    for (int i= 0; i < count; ++i) {
      QPainterPath::Element element= path.elementAt (i);
      elements[i]= path_element_record {
        element.x, element.y, static_cast<std::uint32_t> (element.type), 0};
    }
  }

  render_stream_writer& writer_;
};

class recording_paint_device final: public QPaintDevice {
public:
  recording_paint_device (render_stream_writer& writer, int width, int height,
                          double pixel_ratio):
    engine_ (writer), width_ (width), height_ (height) {
    (void) pixel_ratio;
  }

  QPaintEngine* paintEngine () const override {
    return const_cast<recording_paint_engine*> (&engine_);
  }

protected:
  int metric (PaintDeviceMetric metric) const override {
    switch (metric) {
    case PdmWidth: return width_;
    case PdmHeight: return height_;
    case PdmWidthMM: return qRound (width_ * 25.4 / 96.0);
    case PdmHeightMM: return qRound (height_ * 25.4 / 96.0);
    case PdmNumColors: return std::numeric_limits<int>::max ();
    case PdmDepth: return 32;
    case PdmDpiX:
    case PdmPhysicalDpiX: return 96;
    case PdmDpiY:
    case PdmPhysicalDpiY: return 96;
    case PdmDevicePixelRatio: return 1;
    case PdmDevicePixelRatioScaled:
      return devicePixelRatioFScale ();
    default: return 0;
    }
  }

private:
  mutable recording_paint_engine engine_;
  int width_;
  int height_;
};

} // namespace

struct QTMRenderRecording::implementation {
  render_stream_writer writer;
  recording_paint_device device;
  bool finished= false;

  implementation (
    std::shared_ptr<render_connection> connection,
    std::shared_ptr<QTMRenderConnection::processor_state> processor,
    int width, int height, double pixel_ratio,
    std::uint32_t background_argb, std::uint64_t buffer_generation,
    std::uint64_t frame_generation, render_damage damage):
    writer (std::move (connection), std::move (processor), buffer_generation,
            frame_generation, damage),
    device (writer, width, height, pixel_ratio) {
    std::byte* payload= writer.command (
      render_opcode::frame_begin, sizeof (frame_begin_command));
    if (payload != nullptr) {
      frame_begin_command begin {
        stream_magic, stream_version, 0, static_cast<std::uint32_t> (width),
        static_cast<std::uint32_t> (height), pixel_ratio, background_argb};
      std::memcpy (payload, &begin, sizeof (begin));
    }
  }
};

struct QTMSharedFrame::implementation {
  std::shared_ptr<QTMRenderConnection::processor_state> processor;
  int slot;
  QImage image;

  implementation (
    std::shared_ptr<QTMRenderConnection::processor_state> processor2,
    int slot2): processor (std::move (processor2)), slot (slot2) {
    auto& frame= processor->frames[slot];
    image= QImage (frame.pixels.get (), frame.width, frame.height, frame.stride,
                   QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio (frame.pixel_ratio);
  }

  ~implementation () {
    processor->frame_states[slot].store (
      QTMRenderConnection::processor_state::frame_idle,
      std::memory_order_release);
  }
};

QTMRenderRecording::QTMRenderRecording (
  std::unique_ptr<implementation> impl) noexcept:
  impl_ (std::move (impl)) {}

QTMRenderRecording::~QTMRenderRecording ()= default;

QPaintDevice*
QTMRenderRecording::device () noexcept {
  return impl_ == nullptr ? nullptr : &impl_->device;
}

bool
QTMRenderRecording::finish () noexcept {
  if (impl_ == nullptr || impl_->finished) return false;
  impl_->finished= true;
  return impl_->writer.finish ();
}

QTMSharedFrame::QTMSharedFrame () noexcept= default;
QTMSharedFrame::QTMSharedFrame (QTMSharedFrame&&) noexcept= default;
QTMSharedFrame& QTMSharedFrame::operator = (QTMSharedFrame&&) noexcept= default;
QTMSharedFrame::~QTMSharedFrame ()= default;

QTMSharedFrame::QTMSharedFrame (
  std::unique_ptr<implementation> impl) noexcept:
  impl_ (std::move (impl)) {}

QTMSharedFrame::operator bool () const noexcept {
  return impl_ != nullptr && !impl_->image.isNull ();
}

const QImage&
QTMSharedFrame::image () const noexcept {
  static const QImage empty;
  return impl_ == nullptr ? empty : impl_->image;
}

std::uint64_t
QTMSharedFrame::bufferGeneration () const noexcept {
  return impl_ == nullptr ? 0 :
    impl_->processor->frames[impl_->slot].buffer_generation;
}

std::uint64_t
QTMSharedFrame::frameGeneration () const noexcept {
  return impl_ == nullptr ? 0 :
    impl_->processor->frames[impl_->slot].frame_generation;
}

render_damage
QTMSharedFrame::damage () const noexcept {
  return impl_ == nullptr ? render_damage {} :
    impl_->processor->frames[impl_->slot].damage;
}

std::shared_ptr<QTMRenderConnection>
QTMRenderConnection::create (std::size_t slotCount,
                             std::size_t slotCapacity) {
  auto result= std::shared_ptr<QTMRenderConnection> (
    new QTMRenderConnection (slotCount, slotCapacity));
  if (!result->initialize ()) return nullptr;
  {
    std::lock_guard<std::mutex> guard (connection_registry_lock);
    connection_registry[result->id_]= result;
  }
  return result;
}

QTMRenderConnection::QTMRenderConnection (
  std::size_t slotCount, std::size_t slotCapacity):
  id_ (allocate_connection_id ()),
  processor_ (std::make_shared<processor_state> ()), connection_ (),
  slotCount_ (slotCount), slotCapacity_ (slotCapacity) {}

bool
QTMRenderConnection::initialize () {
  connection_= render_service::instance ().connect (
    processor_, slotCount_, slotCapacity_);
  return connection_ != nullptr;
}

QTMRenderConnection::~QTMRenderConnection () {
  retire ();
}

athena_resource_id
QTMRenderConnection::id () const noexcept {
  return id_;
}

std::unique_ptr<QTMRenderRecording>
QTMRenderConnection::beginRecording (
  int pixelWidth, int pixelHeight, double devicePixelRatio,
  std::uint32_t backgroundArgb, std::uint64_t bufferGeneration,
  std::uint64_t frameGeneration, render_damage damage) {
  if (connection_ == nullptr || pixelWidth <= 0 || pixelHeight <= 0 ||
      !std::isfinite (devicePixelRatio) || devicePixelRatio <= 0.0)
    return nullptr;
  auto impl= std::make_unique<QTMRenderRecording::implementation> (
    connection_, processor_, pixelWidth, pixelHeight, devicePixelRatio,
    backgroundArgb, bufferGeneration, frameGeneration, damage);
  return std::unique_ptr<QTMRenderRecording> (
    new QTMRenderRecording (std::move (impl)));
}

QTMSharedFrame
QTMRenderConnection::acquireLatestFrame () {
  int latest= -1;
  std::uint64_t buffer_generation= 0;
  std::uint64_t frame_generation= 0;
  for (std::size_t i= 0; i < frame_slot_count; ++i) {
    if (processor_->frame_states[i].load (std::memory_order_acquire) !=
        processor_state::frame_ready)
      continue;
    const auto& frame= processor_->frames[i];
    if (latest < 0 || frame.buffer_generation > buffer_generation ||
        (frame.buffer_generation == buffer_generation &&
         frame.frame_generation > frame_generation)) {
      latest= static_cast<int> (i);
      buffer_generation= frame.buffer_generation;
      frame_generation= frame.frame_generation;
    }
  }
  if (latest < 0) return QTMSharedFrame ();
  std::uint32_t expected= processor_state::frame_ready;
  if (!processor_->frame_states[latest].compare_exchange_strong (
        expected, processor_state::frame_displaying,
        std::memory_order_acq_rel))
    return QTMSharedFrame ();
  for (std::size_t i= 0; i < frame_slot_count; ++i) {
    if (static_cast<int> (i) == latest) continue;
    expected= processor_state::frame_ready;
    if (processor_->frame_states[i].compare_exchange_strong (
          expected, processor_state::frame_idle, std::memory_order_acq_rel)) {}
  }
  return QTMSharedFrame (
    std::make_unique<QTMSharedFrame::implementation> (processor_, latest));
}

void
QTMRenderConnection::retire () noexcept {
  {
    std::lock_guard<std::mutex> guard (connection_registry_lock);
    connection_registry.erase (id_);
  }
  if (connection_ != nullptr) connection_->retire ();
  connection_.reset ();
}

std::shared_ptr<QTMRenderConnection>
QTMRenderConnection::lookup (athena_resource_id id) noexcept {
  if (id == 0) return nullptr;
  std::lock_guard<std::mutex> guard (connection_registry_lock);
  auto found= connection_registry.find (id);
  if (found == connection_registry.end ()) return nullptr;
  return found->second.lock ();
}

std::unique_ptr<QTMRenderRecording>
qtm_begin_render_recording (
  athena_resource_id connectionId, int pixelWidth, int pixelHeight,
  double devicePixelRatio, std::uint32_t backgroundArgb,
  std::uint64_t bufferGeneration, std::uint64_t frameGeneration,
  render_damage damage) {
  auto connection= QTMRenderConnection::lookup (connectionId);
  return connection == nullptr ? nullptr : connection->beginRecording (
    pixelWidth, pixelHeight, devicePixelRatio, backgroundArgb,
    bufferGeneration, frameGeneration, damage);
}
