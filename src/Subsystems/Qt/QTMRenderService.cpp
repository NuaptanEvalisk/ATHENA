/******************************************************************************
* MODULE     : QTMRenderService.cpp
* DESCRIPTION: Qt display-list connection to the shared RenderService
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMRenderService.hpp"

#include <QPainter>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace {

constexpr std::uint32_t picture_chunk_magic= 0x41544852; // ATHR
constexpr std::uint16_t picture_chunk_version= 1;

struct picture_chunk_header {
  std::uint32_t magic;
  std::uint16_t version;
  std::uint16_t header_size;
  std::uint32_t pixel_width;
  std::uint32_t pixel_height;
  double device_pixel_ratio;
  std::uint32_t background_argb;
  std::uint32_t picture_size;
  std::uint32_t picture_offset;
  std::uint32_t chunk_size;
};

static_assert (std::is_trivially_copyable<picture_chunk_header>::value,
               "render chunk header must remain POD");

} // namespace

bool
QTMRenderSurface::publish (QTMRenderedFrame frame) {
  {
    std::lock_guard<std::mutex> guard (lock_);
    if (frame.bufferGeneration < latest_.bufferGeneration ||
        (frame.bufferGeneration == latest_.bufferGeneration &&
         frame.frameGeneration < latest_.frameGeneration))
      return false;
    latest_= std::move (frame);
  }
  changed_.notify_all ();
  return true;
}

QTMRenderedFrame
QTMRenderSurface::latestFrame () const {
  std::lock_guard<std::mutex> guard (lock_);
  return latest_;
}

bool
QTMRenderSurface::waitForFrame (std::uint64_t generation,
                                std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> guard (lock_);
  return changed_.wait_for (guard, timeout, [this, generation] {
    return latest_.frameGeneration >= generation;
  });
}

struct QTMRenderConnection::processor_state {
  std::weak_ptr<QTMRenderSurface> surface;
  completion_callback completed;
  QByteArray picture_data;
  std::uint64_t buffer_generation= 0;
  std::uint64_t frame_generation= 0;
  render_damage damage;
  picture_chunk_header header {};

  void abandon_frame () {
    picture_data.clear ();
    buffer_generation= 0;
    frame_generation= 0;
    header= picture_chunk_header {};
  }

  void reset (const render_chunk_descriptor& descriptor,
              const picture_chunk_header& next) {
    picture_data.clear ();
    picture_data.reserve (static_cast<qsizetype> (next.picture_size));
    buffer_generation= descriptor.buffer_generation;
    frame_generation= descriptor.frame_generation;
    damage= descriptor.damage;
    header= next;
  }

  void render_picture (const char* data, std::uint32_t size,
                       const picture_chunk_header& frame_header) {
    QPicture picture;
    picture.setData (data, static_cast<uint> (size));
    QImage image (static_cast<int> (frame_header.pixel_width),
                  static_cast<int> (frame_header.pixel_height),
                  QImage::Format_ARGB32_Premultiplied);
    if (image.isNull ()) {
      abandon_frame ();
      return;
    }
    image.setDevicePixelRatio (frame_header.device_pixel_ratio);
    image.fill (frame_header.background_argb);
    QPainter painter (&image);
    picture.play (&painter);
    painter.end ();

    if (auto target= surface.lock ()) {
      QTMRenderedFrame frame;
      frame.image= std::move (image);
      frame.bufferGeneration= buffer_generation;
      frame.frameGeneration= frame_generation;
      frame.damage= damage;
      if (target->publish (std::move (frame)) && completed) completed ();
    }
    abandon_frame ();
  }

  void process (const render_chunk_descriptor& descriptor,
                const std::byte* payload) {
    if (payload == nullptr || descriptor.used < sizeof (picture_chunk_header)) {
      abandon_frame ();
      return;
    }
    picture_chunk_header next;
    std::memcpy (&next, payload, sizeof (next));
    if (next.magic != picture_chunk_magic ||
        next.version != picture_chunk_version ||
        next.header_size != sizeof (picture_chunk_header) ||
        next.pixel_width == 0 || next.pixel_height == 0 ||
        next.picture_size > std::numeric_limits<int>::max () ||
        next.chunk_size > descriptor.used - sizeof (picture_chunk_header) ||
        next.picture_offset > next.picture_size ||
        next.chunk_size > next.picture_size - next.picture_offset) {
      abandon_frame ();
      return;
    }

    if (descriptor.buffer_generation != buffer_generation ||
        descriptor.frame_generation != frame_generation ||
        next.picture_offset == 0)
      reset (descriptor, next);
    if (next.picture_size != header.picture_size ||
        next.picture_offset != static_cast<std::uint32_t> (picture_data.size ())) {
      abandon_frame ();
      return;
    }

    const char* bytes= reinterpret_cast<const char*> (
      payload + sizeof (picture_chunk_header));
    if (next.picture_offset == 0 && descriptor.final_chunk &&
        next.chunk_size == next.picture_size) {
      render_picture (bytes, next.chunk_size, next);
      return;
    }
    picture_data.append (bytes, static_cast<qsizetype> (next.chunk_size));
    if (!descriptor.final_chunk ||
        picture_data.size () != static_cast<qsizetype> (next.picture_size))
      return;

    render_picture (picture_data.constData (),
                    static_cast<std::uint32_t> (picture_data.size ()), header);
  }
};

std::shared_ptr<QTMRenderConnection>
QTMRenderConnection::create (completion_callback completed,
                             std::size_t slotCount,
                             std::size_t slotCapacity) {
  auto connection= std::shared_ptr<QTMRenderConnection> (
    new QTMRenderConnection (std::move (completed), slotCount, slotCapacity));
  if (!connection->initialize ()) return nullptr;
  return connection;
}

QTMRenderConnection::QTMRenderConnection (
  completion_callback completed, std::size_t slotCount,
  std::size_t slotCapacity):
  completed_ (std::move (completed)),
  surface_ (std::make_shared<QTMRenderSurface> ()),
  processor_ (std::make_shared<processor_state> ()), connection_ (),
  slotCount_ (slotCount), slotCapacity_ (slotCapacity) {
  processor_->surface= surface_;
  processor_->completed= completed_;
}

bool
QTMRenderConnection::initialize () {
  std::weak_ptr<processor_state> weak= processor_;
  connection_= render_service::instance ().connect (
    [weak] (const render_chunk_descriptor& descriptor,
            const std::byte* payload) {
      if (auto processor= weak.lock ()) processor->process (descriptor, payload);
    }, slotCount_, slotCapacity_);
  return connection_ != nullptr;
}

QTMRenderConnection::~QTMRenderConnection () {
  retire ();
}

bool
QTMRenderConnection::submit (
  const QPicture& picture, int pixelWidth, int pixelHeight,
  double devicePixelRatio, std::uint32_t backgroundArgb,
  std::uint64_t bufferGeneration, std::uint64_t frameGeneration,
  render_damage damage) {
  if (connection_ == nullptr || pixelWidth <= 0 || pixelHeight <= 0 ||
      !std::isfinite (devicePixelRatio) || devicePixelRatio <= 0.0 ||
      slotCapacity_ <= sizeof (picture_chunk_header))
    return false;
  std::size_t total= static_cast<std::size_t> (picture.size ());
  if (total > std::numeric_limits<std::uint32_t>::max ()) return false;
  std::size_t offset= 0;

  do {
    render_chunk_arena::writable_chunk chunk;
    if (!connection_->acquire (chunk))
      return false;
    if (chunk.capacity <= sizeof (picture_chunk_header)) {
      (void) connection_->discard (chunk.slot);
      return false;
    }
    std::size_t count= std::min (
      total - offset, chunk.capacity - sizeof (picture_chunk_header));
    picture_chunk_header header {
      picture_chunk_magic, picture_chunk_version,
      static_cast<std::uint16_t> (sizeof (picture_chunk_header)),
      static_cast<std::uint32_t> (pixelWidth),
      static_cast<std::uint32_t> (pixelHeight), devicePixelRatio,
      backgroundArgb, static_cast<std::uint32_t> (total),
      static_cast<std::uint32_t> (offset), static_cast<std::uint32_t> (count)
    };
    std::memcpy (chunk.data, &header, sizeof (header));
    if (count != 0)
      std::memcpy (chunk.data + sizeof (header), picture.data () + offset,
                   count);

    render_chunk_descriptor descriptor;
    descriptor.slot= chunk.slot;
    descriptor.used= static_cast<std::uint32_t> (sizeof (header) + count);
    descriptor.buffer_generation= bufferGeneration;
    descriptor.frame_generation= frameGeneration;
    descriptor.damage= damage;
    descriptor.final_chunk= offset + count == total;
    if (!connection_->publish (descriptor)) {
      (void) connection_->discard (chunk.slot);
      return false;
    }
    offset += count;
  } while (offset < total);
  return true;
}

void
QTMRenderConnection::retire () noexcept {
  if (connection_ != nullptr) connection_->retire ();
  connection_.reset ();
}

std::shared_ptr<QTMRenderSurface>
QTMRenderConnection::surface () const noexcept {
  return surface_;
}
