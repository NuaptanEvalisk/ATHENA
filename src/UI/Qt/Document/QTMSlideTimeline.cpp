// Event timing is independent of refresh rate and can be sought without replay.
#include "UI/Qt/Document/QTMSlideTimeline.hpp"
#include <QEasingCurve>
#include <algorithm>
#include <cmath>
#include <map>

QTMSlideTimeline::QTMSlideTimeline (std::shared_ptr<const QTMSlideScene> value): scene (std::move (value)) {
  const auto& frames= scene->motion.frames;
  starts.resize (frames.size ()); durations.resize (frames.size ()); batch_for_frame.resize (frames.size ());
  std::map<std::string, int> indices;
  for (int i=0; i<int (frames.size ()); ++i) indices[frames[i].id]= i;
  for (const auto& object: scene->motion.objects)
    for (const auto& e: object.effects) {
      int i= indices.at (e.frame);
      durations[i]= std::max (durations[i], e.type == "appear" ? 0 : e.duration);
    }
  for (int i=0; i<int (frames.size ()); ++i) {
    if (i == 0 || frames[i].trigger == "manual")
      batches_.push_back ({i, i+1, 0, i == 0 && frames[i].trigger != "manual"});
    else starts[i]= starts[i-1] + (frames[i].trigger == "after" ? durations[i-1] : 0);
    batch_for_frame[i]= int (batches_.size ())-1;
    batches_.back ().end= i+1;
    batches_.back ().duration= std::max (batches_.back ().duration, starts[i]+durations[i]);
  }
}

std::vector<QTMSlideLayerState> QTMSlideTimeline::state (int completed, int elapsed) const {
  std::vector<QTMSlideLayerState> objects (scene->motion.objects.size ());
  const auto& frames= scene->motion.frames;
  for (size_t n=0; n<objects.size (); ++n) {
    const auto& object= scene->motion.objects[n];
    auto& out= objects[n];
    bool first= true;
    for (int i=0; i<int (frames.size ()); ++i) {
      auto found= std::find_if (object.effects.begin (), object.effects.end (), [&] (const auto& e) { return e.frame == frames[i].id; });
      if (found == object.effects.end ()) continue;
      const auto& e= *found;
      if (first) { out.opacity= e.category == "entrance" ? 0 : 1; first= false; }
      int local= batch_for_frame[i] < completed ? e.duration :
        batch_for_frame[i] == completed && elapsed >= 0 ? elapsed-starts[i] : -1;
      if (local < 0) continue;
      qreal p= e.type == "appear" ? 1 : std::clamp (qreal (local)/e.duration, qreal (0), qreal (1));
      if (e.category == "emphasis") {
        const auto center= scene->object_bounds[n].center ();
        const qreal scale= 1 + 0.25*std::sin (p*3.14159265358979323846);
        QTransform grow;
        grow.translate (center.x (), center.y ()); grow.scale (scale, scale);
        grow.translate (-center.x (), -center.y ());
        out.transform= out.transform*grow;
      }
      else {
        const bool enter= e.category == "entrance";
        out.opacity= e.type == "fade" ? (enter ? p : 1-p) : (enter || p < 1 ? 1 : 0);
        if (e.type == "fly" && p < 1) {
          const auto bounds= scene->object_bounds[n];
          QPointF offset;
          if (e.direction == "left") offset.setX (-bounds.right ()-2);
          if (e.direction == "right") offset.setX (scene->full.width ()-bounds.left ()+2);
          if (e.direction == "up") offset.setY (-bounds.bottom ()-2);
          if (e.direction == "down") offset.setY (scene->full.height ()-bounds.top ()+2);
          const qreal distance= enter ? 1-QEasingCurve (QEasingCurve::OutCubic).valueForProgress (p) : QEasingCurve (QEasingCurve::InCubic).valueForProgress (p);
          out.transform.translate (offset.x ()*distance, offset.y ()*distance);
        }
      }
    }
  }
  std::vector<QTMSlideLayerState> result;
  for (const auto& layer: scene->layers) {
    QTMSlideLayerState value;
    for (auto owner= layer.owners.rbegin (); owner != layer.owners.rend (); ++owner) {
      value.opacity*= objects[*owner].opacity;
      value.transform= value.transform*objects[*owner].transform;
    }
    result.push_back (value);
  }
  return result;
}
