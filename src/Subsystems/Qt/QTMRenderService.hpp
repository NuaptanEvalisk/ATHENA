/******************************************************************************
* MODULE     : QTMRenderService.hpp
* DESCRIPTION: Zero-copy Qt command recording and shared frame presentation
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
******************************************************************************/

#ifndef QTM_RENDER_SERVICE_HPP
#define QTM_RENDER_SERVICE_HPP

#include "actor_transport.hpp"
#include "render_service.hpp"

#include <QImage>
#include <QPaintDevice>

#include <cstddef>
#include <cstdint>
#include <memory>

class QTMRenderConnection;

class QTMRenderRecording {
public:
  ~QTMRenderRecording ();

  QTMRenderRecording (const QTMRenderRecording&)= delete;
  QTMRenderRecording& operator = (const QTMRenderRecording&)= delete;

  QPaintDevice* device () noexcept;
  bool finish () noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> impl_;

  explicit QTMRenderRecording (std::unique_ptr<implementation> impl) noexcept;
  friend class QTMRenderConnection;
};

class QTMSharedFrame {
public:
  QTMSharedFrame () noexcept;
  QTMSharedFrame (QTMSharedFrame&&) noexcept;
  QTMSharedFrame& operator = (QTMSharedFrame&&) noexcept;
  ~QTMSharedFrame ();

  QTMSharedFrame (const QTMSharedFrame&)= delete;
  QTMSharedFrame& operator = (const QTMSharedFrame&)= delete;

  explicit operator bool () const noexcept;
  const QImage& image () const noexcept;
  std::uint64_t bufferGeneration () const noexcept;
  std::uint64_t frameGeneration () const noexcept;
  render_damage damage () const noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> impl_;

  explicit QTMSharedFrame (std::unique_ptr<implementation> impl) noexcept;
  friend class QTMRenderConnection;
};

class QTMRenderConnection: public std::enable_shared_from_this<QTMRenderConnection> {
public:
  struct processor_state;

  static std::shared_ptr<QTMRenderConnection> create (
    std::size_t slotCount= 4,
    std::size_t slotCapacity= 4 * 1024 * 1024);
  ~QTMRenderConnection ();

  QTMRenderConnection (const QTMRenderConnection&)= delete;
  QTMRenderConnection& operator = (const QTMRenderConnection&)= delete;

  athena_resource_id id () const noexcept;
  std::unique_ptr<QTMRenderRecording> beginRecording (
    int pixelWidth, int pixelHeight, double devicePixelRatio,
    std::uint32_t backgroundArgb, std::uint64_t bufferGeneration,
    std::uint64_t frameGeneration, render_damage damage);
  QTMSharedFrame acquireLatestFrame ();
  void retire () noexcept;

  static std::shared_ptr<QTMRenderConnection> lookup (
    athena_resource_id id) noexcept;

private:
  QTMRenderConnection (std::size_t slotCount, std::size_t slotCapacity);
  bool initialize ();

  const athena_resource_id id_;
  std::shared_ptr<processor_state> processor_;
  std::shared_ptr<render_connection> connection_;
  std::size_t slotCount_;
  std::size_t slotCapacity_;

  friend class QTMSharedFrame;
  friend class QTMRenderRecording;
};

std::unique_ptr<QTMRenderRecording> qtm_begin_render_recording (
  athena_resource_id connectionId, int pixelWidth, int pixelHeight,
  double devicePixelRatio, std::uint32_t backgroundArgb,
  std::uint64_t bufferGeneration, std::uint64_t frameGeneration,
  render_damage damage);

#endif // defined QTM_RENDER_SERVICE_HPP
