#pragma once

#include "ATHENA/Data/prooftrack.hpp"
#include <QObject>
#include <QPointF>
#include <QString>
#include <QTimer>
#include <functional>
#include <map>
#include <vector>

class QPainter;
class QWidget;
class QMouseEvent;

struct QTMProofEndpoint {
  QString id, target, title, occurrence;
  QPointF position;
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
  std::shared_ptr<const athena::prooftrack::snapshot> index_;
  std::vector<Track> tracks_;
  QString hovered_, pressed_;
  std::map<QString, bool> next_;
  bool shown_= true, transclusions_= false;
  int hit (QPointF) const;
public:
  std::vector<QTMProofEndpoint> endpoints;
  std::function<void (const QTMProofEndpoint&)> navigate;
  explicit QTMProofTrack (QWidget* surface);
  void accept (const std::string& json);
  void paint (QPainter&, std::vector<QTMProofEndpoint>,
              const std::map<std::string, QTMProofMember>& members= {});
  bool mouse (QMouseEvent*);
  void leave ();
  bool enabled () const { return shown_; }
};
