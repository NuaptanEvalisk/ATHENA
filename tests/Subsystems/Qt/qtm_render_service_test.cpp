/******************************************************************************
* MODULE     : qtm_render_service_test.cpp
* DESCRIPTION: Qt display-list RenderService round-trip test
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>

#include "QTMRenderService.hpp"
#include "qt_renderer.hpp"

#include <QPainter>
#include <condition_variable>
#include <mutex>
#include <thread>

bool headless_mode= true;

class TestQTMRenderService: public QObject {
  Q_OBJECT

private slots:
  void rendersDisplayListOffTheProducerThread ();
  void givesEachProducerThreadItsOwnQtRenderer ();
};

void
TestQTMRenderService::rendersDisplayListOffTheProducerThread () {
  auto connection= QTMRenderConnection::create ({}, 2, 128);
  QVERIFY (connection != nullptr);

  QPicture picture;
  QPainter painter (&picture);
  painter.fillRect (QRect (4, 5, 8, 7), QColor (10, 120, 230));
  painter.end ();

  render_damage damage {4, 5, 12, 12};
  QVERIFY (connection->submit (picture, 20, 20, 1.0,
                               qRgba (255, 255, 255, 255),
                               3, 9, damage));
  auto surface= connection->surface ();
  QVERIFY (surface->waitForFrame (9, std::chrono::seconds (1)));
  QTMRenderedFrame frame= surface->latestFrame ();
  QCOMPARE (frame.bufferGeneration, std::uint64_t (3));
  QCOMPARE (frame.frameGeneration, std::uint64_t (9));
  QCOMPARE (frame.image.size (), QSize (20, 20));
  QCOMPARE (frame.image.pixelColor (6, 7), QColor (10, 120, 230));
  QCOMPARE (frame.image.pixelColor (0, 0), QColor (255, 255, 255));

  connection->retire ();
}

void
TestQTMRenderService::givesEachProducerThreadItsOwnQtRenderer () {
  qt_renderer_rep* renderers[2]= {nullptr, nullptr};
  std::mutex lock;
  std::condition_variable changed;
  int ready= 0;
  bool release= false;
  auto inspect= [&] (int index, double pixelRatio) {
    renderers[index]= the_qt_renderer (pixelRatio);
    std::unique_lock<std::mutex> guard (lock);
    ++ready;
    changed.notify_all ();
    changed.wait (guard, [&] { return release; });
  };

  std::thread first (inspect, 0, 1.0);
  std::thread second (inspect, 1, 1.5);
  bool distinct= false;
  {
    std::unique_lock<std::mutex> guard (lock);
    changed.wait (guard, [&] { return ready == 2; });
    distinct= renderers[0] != nullptr && renderers[1] != nullptr &&
              renderers[0] != renderers[1];
    release= true;
  }
  changed.notify_all ();
  first.join ();
  second.join ();
  QVERIFY (distinct);
}

QTEST_MAIN (TestQTMRenderService)
#include "qtm_render_service_test.moc"
