#include "QTMProofTrack.hpp"
#include "native_interfaces.hpp"
#include "boot.hpp"
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <QWidget>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <set>

QTMProofTrack::QTMProofTrack (QWidget* surface): QObject (surface), surface_ (surface) {
  index_= athena::prooftrack::read ();
  auto refresh= [this] {
    auto index= athena::prooftrack::read ();
    bool shown= get_user_preference ("show prooftrack", "on") == "on";
    bool transclusions= get_user_preference ("prooftrack in transclusions", "off") == "on";
    if (index != index_ || shown != shown_ || transclusions != transclusions_) {
      index_= std::move (index); shown_= shown; transclusions_= transclusions;
      if (!shown_) { tracks_.clear (); leave (); }
      surface_->update ();
    }
  };
  refresh ();
  timer_.setInterval (200);
  connect (&timer_, &QTimer::timeout, this, refresh);
  timer_.start ();
}

void QTMProofTrack::accept (const std::string& bytes) {
  endpoints.clear ();
  for (const auto& p: nlohmann::json::parse (bytes)) {
    QTMProofEndpoint endpoint;
    endpoint.id= QString::fromStdString (p.at ("id").get<std::string> ());
    endpoint.target= QString::fromStdString (p.at ("target").get<std::string> ());
    endpoint.title= QString::fromStdString (p.at ("title").get<std::string> ());
    endpoint.occurrence= QString::fromStdString (p.at ("occurrence").get<std::string> ());
    endpoint.position= QPointF (p.at ("x").get<double> (), p.at ("y").get<double> ());
    endpoint.proof= p.at ("proof").get<bool> ();
    endpoint.transclusion= p.at ("transclusion").get<bool> ();
    endpoints.push_back (std::move (endpoint));
  }
  surface_->update ();
}

void QTMProofTrack::paint (QPainter& painter, std::vector<QTMProofEndpoint> points,
                         const std::map<std::string, QTMProofMember>& members) {
  tracks_.clear ();
  if (!shown_) return;
  points.erase (std::remove_if (points.begin (), points.end (), [this] (const auto& p) {
    return p.transclusion && !transclusions_;
  }), points.end ());
  if (points.empty ()) return;
  std::map<QString, std::vector<QTMProofEndpoint>> by_id;
  for (const auto& p: points) by_id[p.id].push_back (p);
  auto counterpart= [&] (const QTMProofEndpoint& p, const QString& id) {
    auto found= by_id.find (id);
    if (found != by_id.end ()) {
      return *std::min_element (found->second.begin (), found->second.end (),
        [&] (const auto& a, const auto& b) {
          return std::abs (a.position.y ()-p.position.y ()) <
                 std::abs (b.position.y ()-p.position.y ());
        });
    }
    QTMProofEndpoint result;
    result.id= id; result.precise= false; result.member= -1;
    result.proof= !p.proof;
    result.title= result.proof ? tr ("Proof") : tr ("Statement");
    result.position= p.position + QPointF (0, p.proof ? -24 : 24);
    auto file= index_->files.find (id.toStdString ());
    if (file != index_->files.end ()) {
      auto member= members.find (file->second);
      if (member != members.end ()) {
        result.member= member->second.index;
        result.position.setY (member->second.top);
      }
      else result.title+= "\n" + QString::fromStdString (file->second);
    }
    return result;
  };
  std::set<QString> emitted;
  auto add= [&] (QTMProofEndpoint a, QTMProofEndpoint b) {
    if (a.proof) std::swap (a, b);
    const QString key= a.id + ":" + b.id + ":" + QString::number (a.member) + ":" +
      QString::number (b.member) + ":" + a.occurrence + ":" + b.occurrence;
    if (!emitted.insert (key).second) return;
    tracks_.push_back ({a, b, 0, a.member >= 0 && b.member >= 0, key});
  };
  for (const auto& p: points) {
    if (p.proof) {
      if (!p.target.isEmpty ()) add (p, counterpart (p, p.target));
    }
    else {
      auto found= index_->by_target.find (p.id.toStdString ());
      if (found == index_->by_target.end ()) continue;
      for (const auto& proof: found->second) {
        const auto id= QString::fromStdString (proof.id);
        // Live proof.target wins over yesterday's reverse index, including
        // an explicitly cleared target.
        auto live= by_id.find (id);
        if (live != by_id.end ()) continue;
        add (p, counterpart (p, id));
      }
    }
  }
  std::sort (tracks_.begin (), tracks_.end (), [] (const Track& a, const Track& b) {
    const auto ay= std::min (a.first.position.y (), a.second.position.y ());
    const auto by= std::min (b.first.position.y (), b.second.position.y ());
    return ay == by ? a.key < b.key : ay < by;
  });
  std::vector<double> lane_ends;
  std::vector<std::size_t> lanes;
  for (auto& t: tracks_) {
    double low= std::min (t.first.position.y (), t.second.position.y ());
    double high= std::max (t.first.position.y (), t.second.position.y ());
    std::size_t lane= 0;
    while (lane < lane_ends.size () && lane_ends[lane] >= low-4) ++lane;
    if (lane == lane_ends.size ()) lane_ends.push_back (high);
    else lane_ends[lane]= high;
    lanes.push_back (lane);
  }
  painter.save ();
  painter.setRenderHint (QPainter::Antialiasing);
  for (std::size_t i=0; i<tracks_.size (); ++i) {
    auto& t= tracks_[i];
    const double low= std::min (t.first.position.y (), t.second.position.y ());
    const double high= std::max (t.first.position.y (), t.second.position.y ());
    const double edge= std::min (t.first.position.x (), t.second.position.x ());
    const double gap= std::min (8.0, std::max (0.0, edge-8.0) / lane_ends.size ());
    t.x= edge-4.0-gap*(lanes[i]+1);
    if (high < 0 || low > surface_->height ()) continue;
    const bool hovered= hovered_ == t.key;
    painter.setPen (QPen (hovered ? QColor ("#9951c8") : QColor ("#857394"), hovered ? 2.5 : 1.5));
    painter.drawLine (QPointF (t.x, std::max (0.0, low)),
                      QPointF (t.x, std::min (double (surface_->height ()), high)));
    for (const auto* endpoint: {&t.first, &t.second}) {
      const double y= endpoint->position.y ();
      if (y < 0 || y > surface_->height ()) continue;
      if (endpoint->member >= 0) {
        painter.drawLine (QPointF (t.x, y), QPointF (endpoint->position.x ()-3, y));
        if (hovered) painter.drawEllipse (QPointF (endpoint->position.x ()-3, y), 3, 3);
      }
      else {
        painter.drawLine (QPointF (t.x-3, y-3), QPointF (t.x+3, y+3));
        painter.drawLine (QPointF (t.x-3, y+1), QPointF (t.x+3, y+7));
      }
    }
  }
  painter.restore ();
}

int QTMProofTrack::hit (QPointF p) const {
  int best= -1;
  double distance= 6;
  for (int i=int (tracks_.size ())-1; i>=0; --i) {
    const auto& t= tracks_[i];
    double low= std::min (t.first.position.y (), t.second.position.y ());
    double high= std::max (t.first.position.y (), t.second.position.y ());
    const double horizontal= std::abs (p.x ()-t.x);
    if (horizontal < distance && p.y () >= low-5 && p.y () <= high+5) {
      best= i; distance= horizontal;
    }
    for (const auto* endpoint: {&t.first, &t.second})
      if (std::abs (p.y ()-endpoint->position.y ()) < distance &&
          p.x () >= t.x-4 && p.x () <= endpoint->position.x ()) {
        best= i; distance= std::abs (p.y ()-endpoint->position.y ());
      }
  }
  return best;
}

bool QTMProofTrack::mouse (QMouseEvent* event) {
  if (!shown_) return false;
  const int index= hit (event->position ());
  if (event->type () == QEvent::MouseMove && event->buttons () == Qt::NoButton) {
    const auto key= index < 0 ? QString () : tracks_[index].key;
    if (key != hovered_) {
      hovered_= key;
      surface_->setCursor (key.isEmpty () ? Qt::ArrowCursor : Qt::PointingHandCursor);
      surface_->update ();
      if (index >= 0) QToolTip::showText (event->globalPosition ().toPoint (),
        tracks_[index].first.title + " / " + tracks_[index].second.title, surface_);
      else QToolTip::hideText ();
    }
    return index >= 0;
  }
  if ((event->type () == QEvent::MouseButtonPress || event->type () == QEvent::MouseButtonDblClick) &&
      event->button () == Qt::LeftButton && index >= 0) {
    pressed_= tracks_[index].key; return true;
  }
  if (event->type () == QEvent::MouseButtonRelease && event->button () == Qt::LeftButton &&
      !pressed_.isEmpty ()) {
    const auto pressed= pressed_; pressed_.clear ();
    if (index >= 0 && tracks_[index].key == pressed) {
      const auto& t= tracks_[index];
      bool second;
      if (!t.connected) second= t.second.member < 0;
      else if (std::abs (event->position ().y ()-t.first.position.y ()) <= 6) second= true;
      else if (std::abs (event->position ().y ()-t.second.position.y ()) <= 6) second= false;
      else { second= !next_[t.first.id+":"+t.second.id]; next_[t.first.id+":"+t.second.id]= second; }
      auto destination= second ? t.second : t.first;
      if (destination.member >= 0 && navigate) navigate (destination);
      else {
        const auto target= "tmfs://wikilink/" + destination.id.toStdString ();
        athena_node_reference_open (string (target.c_str ()));
      }
    }
    return true;
  }
  return !pressed_.isEmpty ();
}

void QTMProofTrack::leave () {
  if (hovered_.isEmpty ()) return;
  hovered_.clear (); surface_->unsetCursor (); QToolTip::hideText (); surface_->update ();
}
