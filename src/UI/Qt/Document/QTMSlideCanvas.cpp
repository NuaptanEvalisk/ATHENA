// Qt Quick owns texture composition; ATHENA owns semantic motion state.
#include "UI/Qt/Document/QTMSlideCanvas.hpp"
#include "UI/Qt/Toolkit/qt_utilities.hpp"
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QMatrix4x4>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QVariantMap>

class QTMSlideCanvas::Images final: public QQuickImageProvider {
  QMutex mutex;
  QHash<QString, QImage> values;
public:
  Images (): QQuickImageProvider (QQuickImageProvider::Image) {}
  void put (const QString& key, const QImage& image) { QMutexLocker lock (&mutex); values.insert (key, image); }
  void clear () {
    QMutexLocker lock (&mutex);
    const QImage previous= values.value ("previous");
    values.clear ();
    values.insert ("previous", previous);
  }
  QImage requestImage (const QString& id, QSize* size, const QSize&) override {
    QMutexLocker lock (&mutex);
    auto image= values.value (id.section ('?', 0, 0));
    if (size) *size= image.size ();
    return image;
  }
};
QTMSlideCanvas::QTMSlideCanvas (QWidget* parent): QQuickWidget (parent), images (new Images) {
  setAttribute (Qt::WA_TransparentForMouseEvents);
  setFocusPolicy (Qt::NoFocus);
  setClearColor (Qt::transparent);
  setResizeMode (QQuickWidget::SizeRootObjectToView);
  engine ()->addImageProvider ("slides", images);
  setSource (QUrl::fromLocalFile (to_qstring (concretize (url ("$ATHENA_PATH/misc/slideshow/Playback.qml")))));
}
void QTMSlideCanvas::setScene (std::shared_ptr<const QTMSlideScene> value) {
  scene= std::move (value);
  ++generation;
  images->clear ();
  QVariantList layers;
  for (size_t i=0; i<scene->layers.size (); ++i) {
    const auto& layer= scene->layers[i];
    const QString id= QString::number (i);
    images->put (id, layer.pixels);
    layers.append (QVariantMap {
      {"source", QString ("image://slides/%1?%2").arg (id).arg (generation)},
      {"x", layer.rect.x ()}, {"y", layer.rect.y ()},
      {"width", layer.rect.width ()}, {"height", layer.rect.height ()}});
  }
  rootObject ()->setProperty ("slideWidth", scene->full.width ());
  rootObject ()->setProperty ("slideHeight", scene->full.height ());
  rootObject ()->setProperty ("layers", layers);
}
void QTMSlideCanvas::setStates (const std::vector<QTMSlideLayerState>& value) {
  states= value;
  QVariantList result;
  for (const auto& state: states)
    result.append (QVariantMap {{"opacity", state.opacity}, {"matrix", QVariant::fromValue (QMatrix4x4 (state.transform))}});
  rootObject ()->setProperty ("layerStates", result);
}
void QTMSlideCanvas::setTransition (const QImage& previous, const athena::slides::motion::transition& t) {
  images->put ("previous", previous);
  rootObject ()->setProperty ("previousSource", previous.isNull () ? QString () : QString ("image://slides/previous?%1").arg (generation));
  rootObject ()->setProperty ("transition", QString::fromStdString (t.type));
  rootObject ()->setProperty ("direction", QString::fromStdString (t.direction));
}
void QTMSlideCanvas::setProgress (qreal value) { rootObject ()->setProperty ("progress", value); }
void QTMSlideCanvas::setView (qreal zoom, QPointF pan, bool obscured) {
  rootObject ()->setProperty ("zoom", zoom);
  rootObject ()->setProperty ("panX", pan.x ());
  rootObject ()->setProperty ("panY", pan.y ());
  rootObject ()->setProperty ("obscured", obscured);
}
QImage QTMSlideCanvas::currentImage () const {
  if (!scene) return {};
  QImage image (scene->full.size (), QImage::Format_ARGB32_Premultiplied);
  image.fill (Qt::transparent);
  QPainter painter (&image);
  for (size_t i=0; i<scene->layers.size (); ++i) {
    const auto& layer= scene->layers[i];
    painter.save ();
    if (i<states.size ()) {
      painter.setOpacity (states[i].opacity);
      painter.setTransform (states[i].transform);
    }
    painter.drawImage (layer.rect.topLeft (), layer.pixels);
    painter.restore ();
  }
  return image;
}
