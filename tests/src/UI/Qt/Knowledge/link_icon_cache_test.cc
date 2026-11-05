#include "QTMLinkIconCache.hpp"
#include <QImage>
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>
#include <thread>

int main (int argc, char** argv) {
  QApplication app (argc, argv);
  QTemporaryDir directory;
  if (!directory.isValid ()) return 1;
  QTMLinkIconCache cache;
  QString path= directory.filePath ("icon.png");
  int renders= 0;
  std::set<QTMLinkIconCache::observer> notified;
  auto render= [&] {
    if (QThread::currentThread () != app.thread ()) std::abort ();
    ++renders;
    QPixmap image (64, 64);
    image.fill (Qt::magenta);
    return image;
  };
  auto notify= [&] (QTMLinkIconCache::observer owner) {
    // Reentrant lookup must see a complete image and not hold the cache lock.
    if (cache.request (&app, path, owner, render, {}) != path)
      std::abort ();
    notified.insert (owner);
  };
  bool pending= true;
  std::thread actor ([&] {
    for (int i= 0; i < 100; ++i)
      pending &= cache.request (&app, path, {1, 2}, render, notify).isEmpty ();
    pending &= cache.request (&app, path, {3, 4}, render, notify).isEmpty ();
  });
  // Deliberately block the GUI on its actor, reproducing startup's wait order.
  // A blocking invokeMethod in request() deadlocks here (CTest has a timeout).
  actor.join ();
  if (!pending || renders != 0 || QFileInfo::exists (path)) return 2;
  QCoreApplication::processEvents ();
  if (renders != 1 || notified.size () != 2 || QImage (path).size () != QSize (64, 64))
    return 3;
  bool hit= false;
  std::thread cached_actor ([&] {
    hit= cache.request (&app, path, {1, 2}, render, notify) == path;
  });
  cached_actor.join ();
  if (!hit || renders != 1) return 4;

  QTMLinkIconCache disk_cache;
  if (disk_cache.request (&app, path, {0, 0}, render, {}) != path || renders != 1)
    return 5;
  QString missing= directory.filePath ("missing.png");
  auto missing_render= [&] { ++renders; return QPixmap (); };
  for (int i= 0; i < 10; ++i)
    if (!cache.request (&app, missing, {0, 0}, missing_render, {}).isEmpty ())
      return 6;
  if (renders != 2 || QFileInfo::exists (missing)) return 7;
  if (!cache.request (nullptr, path, {0, 0}, render, {}).isEmpty ()) return 8;
  std::cout << "PASS: GUI/actor wait order, request coalescing, atomic PNG, cache hits, failure fallback\n";
}
