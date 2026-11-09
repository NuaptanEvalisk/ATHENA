// Deterministic event-frame scheduling, shared by forward/backward playback.
#pragma once
#include "UI/Qt/Document/QTMSlideScene.hpp"
#include <QTransform>

struct QTMSlideLayerState {
  QTransform transform;
  qreal opacity= 1;
};
class QTMSlideTimeline {
public:
  struct batch { int first, end, duration; bool automatic; };
  explicit QTMSlideTimeline (std::shared_ptr<const QTMSlideScene>);
  const std::vector<batch>& batches () const { return batches_; }
  std::vector<QTMSlideLayerState> state (int completed, int elapsed= -1) const;
private:
  std::shared_ptr<const QTMSlideScene> scene;
  std::vector<batch> batches_;
  std::vector<int> starts, durations, batch_for_frame;
};
