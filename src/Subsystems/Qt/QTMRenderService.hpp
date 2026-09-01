/******************************************************************************
* MODULE     : QTMRenderService.hpp
* DESCRIPTION: Qt display-list connection to the shared RenderService
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTM_RENDER_SERVICE_HPP
#define QTM_RENDER_SERVICE_HPP

#include "render_service.hpp"

#include <QImage>
#include <QPicture>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

struct QTMRenderedFrame {
  QImage image;
  std::uint64_t bufferGeneration= 0;
  std::uint64_t frameGeneration= 0;
  render_damage damage;
};

class QTMRenderSurface {
public:
  QTMRenderedFrame latestFrame () const;
  bool waitForFrame (std::uint64_t generation,
                     std::chrono::milliseconds timeout);

private:
  mutable std::mutex lock_;
  std::condition_variable changed_;
  QTMRenderedFrame latest_;

  bool publish (QTMRenderedFrame frame);

  friend class QTMRenderConnection;
};

class QTMRenderConnection {
public:
  using completion_callback= std::function<void ()>;

  static std::shared_ptr<QTMRenderConnection> create (
    completion_callback completed= completion_callback (),
    std::size_t slotCount= 4,
    std::size_t slotCapacity= 4 * 1024 * 1024);
  ~QTMRenderConnection ();

  QTMRenderConnection (const QTMRenderConnection&)= delete;
  QTMRenderConnection& operator = (const QTMRenderConnection&)= delete;

  bool submit (const QPicture& picture, int pixelWidth, int pixelHeight,
               double devicePixelRatio, std::uint32_t backgroundArgb,
               std::uint64_t bufferGeneration,
               std::uint64_t frameGeneration, render_damage damage);
  void retire () noexcept;
  std::shared_ptr<QTMRenderSurface> surface () const noexcept;

private:
  struct processor_state;

  QTMRenderConnection (completion_callback completed, std::size_t slotCount,
                       std::size_t slotCapacity);
  bool initialize ();

  completion_callback completed_;
  std::shared_ptr<QTMRenderSurface> surface_;
  std::shared_ptr<processor_state> processor_;
  std::shared_ptr<render_connection> connection_;
  std::size_t slotCount_;
  std::size_t slotCapacity_;
};

#endif // defined QTM_RENDER_SERVICE_HPP
