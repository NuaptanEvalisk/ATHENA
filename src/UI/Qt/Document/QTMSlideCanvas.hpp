// Qt Quick scene compositor for retained slide layers. GPL-3.0-or-later.
#pragma once
#include "UI/Qt/Document/QTMSlideTimeline.hpp"
#include <QQuickWidget>
class QTMSlideCanvas final: public QQuickWidget {
public:
  explicit QTMSlideCanvas (QWidget* parent);
  void setScene (std::shared_ptr<const QTMSlideScene>);
  void setStates (const std::vector<QTMSlideLayerState>&);
  void setTransition (const QImage& previous, const athena::slides::motion::transition&);
  void setProgress (qreal);
  void setView (qreal zoom, QPointF pan, bool obscured);
  QImage currentImage () const;
private:
  class Images;
  Images* images;
  std::shared_ptr<const QTMSlideScene> scene;
  std::vector<QTMSlideLayerState> states;
  quint64 generation= 0;
};
