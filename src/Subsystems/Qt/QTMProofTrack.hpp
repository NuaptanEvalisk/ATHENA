#pragma once

#include "ATHENA/Data/prooftrack.hpp"
#include "actor_transport.hpp"
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTimer>
#include <functional>
#include <map>
#include <optional>
#include <vector>

class QPainter;
class QWidget;
class QMouseEvent;

struct QTMProofEndpoint {
  QString id, target, title, occurrence;
  QPointF position;
  QRectF bounds;
  athena_actor_id actor= 0;
  athena_view_id view= 0;
  bool linkable= false, writable= false;
  bool proof= false, transclusion= false, precise= true;
  int member= 0;
};
struct QTMProofMember { int index; double top; };

// GUI-owned screen overlay. Input consists solely of value snapshots published
// by the owning editor and the relation worker, never native trees or DB reads.
class QTMProofTrack: public QObject {
  struct Track {
    QTMProofEndpoint first, second;
    double x;
    bool connected;
    QString key;
  };
  QWidget* surface_;
  QTimer timer_;
  QTimer drag_scroll_;
  std::optional<QTMProofEndpoint> incoming_;
  QPointF drop_position_;
  std::shared_ptr<const athena::prooftrack::snapshot> index_;
  std::vector<Track> tracks_;
  std::vector<QTMProofEndpoint> visible_;
  int handle_= -1, drop_= -1;
  QTMProofEndpoint drag_source_;
  QPointF press_position_;
  bool handle_pressed_= false;
  QString hovered_, pressed_;
  std::map<QString, bool> next_;
  bool shown_= true, transclusions_= false;
  int hit (QPointF) const;
  int handleHit (QPointF) const;
  int dropHit (QPointF, const QTMProofEndpoint&) const;
  bool eventFilter (QObject*, QEvent*) override;
public:
  athena_actor_id actor= 0;
  athena_view_id view= 0;
  std::vector<QTMProofEndpoint> endpoints;
  std::function<void (const QTMProofEndpoint&)> navigate;
  std::function<void (int)> scroll;
  explicit QTMProofTrack (QWidget* surface);
  void accept (const std::string& json);
  void paint (QPainter&, std::vector<QTMProofEndpoint>,
              const std::map<std::string, QTMProofMember>& members= {});
  bool mouse (QMouseEvent*);
  void leave ();
  bool enabled () const { return shown_; }
};
