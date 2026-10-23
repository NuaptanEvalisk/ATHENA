#include "QTMProofTrack.hpp"
#include "native_interfaces.hpp"
#include "boot.hpp"
#include "buffer_actor.hpp"
#include "actor_ui_bridge.hpp"
#include <QApplication>
#include <QDrag>
#include <QDropEvent>
#include <QMimeData>
#include <QPointer>
#include <QCursor>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <QWidget>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <limits>
#include <QLineF>

namespace {
constexpr const char* prooftrack_mime= "application/x-athena-prooftrack";
QPointF handle_position (const QTMProofEndpoint& p) {
  return QPointF (std::max (10.0, p.position.x ()-20), p.position.y ());
}
QTMProofEndpoint drag_endpoint (const QMimeData* mime) {
  const auto bytes= mime->data (prooftrack_mime);
  const auto json= nlohmann::json::parse (bytes.constData (), bytes.constData ()+bytes.size ());
  QTMProofEndpoint p;
  p.id= QString::fromStdString (json.at ("id").get<std::string> ());
  p.target= QString::fromStdString (json.at ("target").get<std::string> ());
  p.proof= json.at ("proof").get<bool> ();
  p.writable= json.at ("writable").get<bool> ();
  p.actor= json.at ("actor").get<athena_actor_id> ();
  p.view= json.at ("view").get<athena_view_id> ();
  return p;
}
}

QTMProofTrack::QTMProofTrack (QWidget* surface): QObject (surface), surface_ (surface) {
  surface_->installEventFilter (this);
  drag_scroll_.setInterval (40);
  connect (&drag_scroll_, &QTimer::timeout, this, [this] {
    if (!shown_ || !incoming_ || !scroll) return;
    const double y= drop_position_.y ();
    const int step= y < 32 ? -16 : (y > surface_->height ()-32 ? 16 : 0);
    if (step) scroll (step);
  });
  index_= athena::prooftrack::read ();
  auto refresh= [this] {
    auto index= athena::prooftrack::read ();
    bool shown= get_user_preference ("show prooftrack", "on") == "on";
    bool transclusions= get_user_preference ("prooftrack in transclusions", "off") == "on";
    if (index != index_ || shown != shown_ || transclusions != transclusions_) {
      index_= std::move (index); shown_= shown; transclusions_= transclusions;
      if (!shown_) {
        tracks_.clear (); visible_.clear (); handle_pressed_= false;
        incoming_.reset (); drag_scroll_.stop (); leave ();
      }
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
    endpoint.linkable= p.value ("linkable", false);
    endpoint.writable= p.value ("writable", false);
    endpoint.actor= actor; endpoint.view= view;
    if (p.contains ("bounds")) {
      const auto& r= p.at ("bounds");
      endpoint.bounds= QRectF (r[0].get<double> (), r[1].get<double> (),
                              r[2].get<double> (), r[3].get<double> ());
    }
    else endpoint.bounds= QRectF (endpoint.position-QPointF (0, 12), QSizeF (160, 24));
    endpoints.push_back (std::move (endpoint));
  }
  surface_->update ();
}

void QTMProofTrack::paint (QPainter& painter, std::vector<QTMProofEndpoint> points,
                         const std::map<std::string, QTMProofMember>& members) {
  tracks_.clear ();
  visible_.clear ();
  if (!shown_) return;
  points.erase (std::remove_if (points.begin (), points.end (), [this] (const auto& p) {
    return p.transclusion && !transclusions_;
  }), points.end ());
  visible_= points;
  if (incoming_) drop_= dropHit (drop_position_, *incoming_);
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
  handle_= handleHit (surface_->mapFromGlobal (QCursor::pos ()));
  for (int i=0; i<int (visible_.size ()); ++i) {
    const auto& endpoint= visible_[i];
    if (i == drop_) {
      painter.setPen (QPen (QColor ("#9951c8"), 2));
      painter.setBrush (QColor (153, 81, 200, 24));
      painter.drawRect (endpoint.bounds.adjusted (-3, -2, 3, 2));
    }
    if (i == handle_ || i == drop_) {
      painter.setPen (QPen (QColor ("#9951c8"), 2));
      painter.setBrush (Qt::white);
      painter.drawEllipse (handle_position (endpoint), 7, 7);
      painter.setBrush (QColor ("#9951c8"));
      painter.drawEllipse (handle_position (endpoint), 2, 2);
    }
  }
  painter.restore ();
}

int QTMProofTrack::handleHit (QPointF p) const {
  int best= -1;
  double distance= 20;
  for (int i=0; i<int (visible_.size ()); ++i) {
    const auto& endpoint= visible_[i];
    if (!endpoint.linkable || (endpoint.proof && !endpoint.writable) ||
        p.x () < 0 || p.x () >= endpoint.position.x ()) continue;
    const double dy= std::abs (p.y ()-endpoint.position.y ());
    if (dy < distance && p.x () >= handle_position (endpoint).x ()-20) {
      distance= dy; best= i;
    }
  }
  return best;
}

int QTMProofTrack::dropHit (QPointF p, const QTMProofEndpoint& source) const {
  int best= -1;
  double area= std::numeric_limits<double>::max ();
  for (int i=0; i<int (visible_.size ()); ++i) {
    const auto& endpoint= visible_[i];
    if (!endpoint.linkable || endpoint.proof == source.proof || endpoint.id == source.id ||
        (endpoint.proof ? !endpoint.writable : !source.writable)) continue;
    auto bounds= endpoint.bounds.adjusted (-28, -5, 5, 5);
    if (bounds.contains (p) && bounds.width ()*bounds.height () < area) {
      best= i; area= bounds.width ()*bounds.height ();
    }
  }
  return best;
}

bool QTMProofTrack::eventFilter (QObject*, QEvent* event) {
  if (event->type () == QEvent::DragLeave) {
    incoming_.reset (); drag_scroll_.stop ();
    drop_= -1; surface_->update (); return false;
  }
  if (event->type () != QEvent::DragEnter && event->type () != QEvent::DragMove &&
      event->type () != QEvent::Drop) return false;
  auto* drop= static_cast<QDropEvent*> (event);
  if (!drop->mimeData ()->hasFormat (prooftrack_mime)) return false;
  drop->ignore ();
  if (!shown_) return true;
  const auto source= drag_endpoint (drop->mimeData ());
  incoming_= source; drop_position_= drop->position ();
  drag_scroll_.start ();
  drop_= dropHit (drop->position (), source);
  if (event->type () == QEvent::DragEnter || drop_ >= 0) {
    drop->setDropAction (Qt::CopyAction); drop->accept ();
  }
  if (event->type () == QEvent::Drop && drop_ >= 0) {
    const auto& destination= visible_[drop_];
    const auto& proof= source.proof ? source : destination;
    const auto& statement= source.proof ? destination : source;
    const auto bytes= nlohmann::json {{"op", "prooftrack-link"},
      {"proof", proof.id.toStdString ()}, {"target", statement.id.toStdString ()},
      {"previous", proof.target.toStdString ()}}.dump ();
    const auto payload= actor_text_from_string (string (bytes.data (), bytes.size ()));
    if (!buffer_actor::try_submit_to (proof.actor, actor_command_kind::native_editor_action_json,
          proof.view, payload, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER,
          0, 0, ACTOR_EDITOR_COMMAND_STATE_READ_ONLY)) {
      actor_text_registry::instance ().discard (payload);
      drop->ignore ();
    }
    drop_= -1;
  }
  if (event->type () == QEvent::Drop) {
    incoming_.reset (); drag_scroll_.stop ();
  }
  surface_->update ();
  return true;
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
  if (handle_pressed_ && event->type () == QEvent::MouseMove) {
    if ((event->position ()-press_position_).manhattanLength () < QApplication::startDragDistance ())
      return true;
    handle_pressed_= false;
    const auto& p= drag_source_;
    const auto bytes= nlohmann::json {{"id", p.id.toStdString ()},
      {"target", p.target.toStdString ()}, {"proof", p.proof}, {"writable", p.writable},
      {"actor", p.actor}, {"view", p.view}}.dump ();
    auto* mime= new QMimeData;
    mime->setData (prooftrack_mime, QByteArray (bytes.data (), bytes.size ()));
    QPointer<QDrag> drag= new QDrag (surface_);
    drag->setMimeData (mime);
    QPixmap icon (24, 24); icon.fill (Qt::transparent);
    QPainter painter (&icon);
    painter.setRenderHint (QPainter::Antialiasing);
    painter.setPen (QPen (QColor ("#9951c8"), 2)); painter.setBrush (Qt::white);
    painter.drawEllipse (QPointF (12, 12), 7, 7); painter.end ();
    drag->setPixmap (icon); drag->setHotSpot (QPoint (12, 12));
    QToolTip::hideText ();
    QPointer<QTMProofTrack> guard (this);
    // Wayland has no LinkAction; copy the association request, not document content.
    drag->exec (Qt::CopyAction);
    if (drag) drag->deleteLater ();
    if (guard) {
      incoming_.reset (); drag_scroll_.stop (); drop_= -1; leave ();
    }
    return true;
  }
  if (handle_pressed_ && event->type () == QEvent::MouseButtonRelease) {
    handle_pressed_= false; return true;
  }
  const int handle= handleHit (event->position ());
  if (event->type () == QEvent::MouseMove && event->buttons () == Qt::NoButton && handle >= 0) {
    handle_= handle; hovered_.clear ();
    surface_->setCursor (Qt::OpenHandCursor); surface_->update ();
    QToolTip::showText (event->globalPosition ().toPoint (), tr ("Drag to link proof and statement"), surface_);
    return true;
  }
  if (event->type () == QEvent::MouseButtonPress && event->button () == Qt::LeftButton &&
      handle >= 0 && QLineF (event->position (), handle_position (visible_[handle])).length () <= 10) {
    drag_source_= visible_[handle]; handle_pressed_= true; press_position_= event->position ();
    pressed_.clear (); return true;
  }
  if (handle_ >= 0 && handle < 0) { handle_= -1; leave (); surface_->update (); }
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
  handle_= -1;
  hovered_.clear (); surface_->unsetCursor (); QToolTip::hideText (); surface_->update ();
}
