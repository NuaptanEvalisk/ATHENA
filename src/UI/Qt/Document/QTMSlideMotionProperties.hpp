// Slide transition and object animation controls. GPL-3.0-or-later.
#pragma once
#include "Document/Model/slide_motion.hpp"
#include <QWidget>
#include <QIcon>
#include <memory>

QIcon slide_motion_icon (const char* name);

class QTMSlideMotionProperties final: public QWidget {
public:
  QTMSlideMotionProperties (const tree& header,
    const std::vector<athena::slides::motion::event_frame>& frames,
    QWidget* parent);
  ~QTMSlideMotionProperties ();
  void write (athena::node::metadata&) const;
private:
  struct controls;
  std::unique_ptr<controls> d;
};

void animations_pane_show ();
