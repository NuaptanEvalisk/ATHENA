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
#include "qt_font.hpp"
#include "qt_renderer.hpp"
#include "qt_simple_widget.hpp"

#include <QFontDatabase>
#include <QPainter>
#include <condition_variable>
#include <mutex>
#include <thread>

bool headless_mode= true;

class TestQTMRenderService: public QObject {
  Q_OBJECT

private slots:
  void rendersDisplayListOffTheProducerThread ();
  void emojiFontUsesUprightCaretMetrics ();
  void rendersColorEmojiWithoutOutlinePath ();
  void givesEachProducerThreadItsOwnQtRenderer ();
  void retryDamageStaysInBackingPixels ();
};

void
TestQTMRenderService::rendersDisplayListOffTheProducerThread () {
  auto connection= QTMRenderConnection::create (2, 4096);
  QVERIFY (connection != nullptr);

  render_damage damage {4, 5, 12, 12};
  auto recording= connection->beginRecording (
    20, 20, 1.0, qRgba (255, 255, 255, 255), 3, 9, damage);
  QVERIFY (recording != nullptr);
  QPainter painter (recording->device ());
  painter.fillRect (QRect (4, 5, 8, 7), QColor (10, 120, 230));
  painter.end ();

  QVERIFY (recording->finish ());
  QTMSharedFrame frame;
  auto deadline= std::chrono::steady_clock::now () + std::chrono::seconds (1);
  do {
    frame= connection->acquireLatestFrame ();
    if (frame) break;
    std::this_thread::sleep_for (std::chrono::milliseconds (1));
  } while (std::chrono::steady_clock::now () < deadline);
  QVERIFY (static_cast<bool> (frame));
  QCOMPARE (frame.bufferGeneration (), std::uint64_t (3));
  QCOMPARE (frame.frameGeneration (), std::uint64_t (9));
  QCOMPARE (frame.image ().size (), QSize (20, 20));
  QCOMPARE (frame.image ().pixelColor (6, 7), QColor (10, 120, 230));
  QCOMPARE (frame.image ().pixelColor (0, 0), QColor (255, 255, 255));

  connection->retire ();
}

void
TestQTMRenderService::emojiFontUsesUprightCaretMetrics () {
  font emoji= qt_font ("emoji", 24, 600);
  QVERIFY (!is_nil (emoji));
  string melting= "<#1FAE0>";
  QCOMPARE (emoji->get_left_slope (melting), 0.0);
  QCOMPARE (emoji->get_right_slope (melting), 0.0);
}

void
TestQTMRenderService::rendersColorEmojiWithoutOutlinePath () {
  const QString family= QStringLiteral ("Noto Color Emoji");
  if (!QFontDatabase::families ().contains (family, Qt::CaseInsensitive))
    QSKIP ("Noto Color Emoji is not installed");

  QFont font (family);
  font.setPixelSize (32);
  const QString emoji= QString::fromUcs4 (U"🫠");

  auto connection= QTMRenderConnection::create (2, 64 * 1024);
  QVERIFY (connection != nullptr);
  render_damage damage {0, 0, 96, 64};
  auto recording= connection->beginRecording (
    96, 64, 1.0, qRgba (255, 255, 255, 255), 11, 17, damage);
  QVERIFY (recording != nullptr);

  QPainter painter (recording->device ());
  painter.setFont (font);
  painter.setPen (Qt::black);
  painter.drawText (QPointF (16, 44), emoji);
  painter.end ();
  QVERIFY (recording->finish ());

  QTMSharedFrame frame;
  auto deadline= std::chrono::steady_clock::now () + std::chrono::seconds (1);
  do {
    frame= connection->acquireLatestFrame ();
    if (frame) break;
    std::this_thread::sleep_for (std::chrono::milliseconds (1));
  } while (std::chrono::steady_clock::now () < deadline);
  QVERIFY (static_cast<bool> (frame));

  bool ink= false;
  const QImage& image= frame.image ();
  for (int y= 0; y < image.height () && !ink; ++y)
    for (int x= 0; x < image.width () && !ink; ++x)
      ink= image.pixelColor (x, y) != QColor (255, 255, 255);
  QVERIFY (ink);
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

class damage_test_widget: public qt_simple_widget_rep {
public:
  void attachCanvas () { qwid= new QTMWidget (nullptr, this); }
  void retry (renderer ren, SI x1, SI y1, SI x2, SI y2) {
    invalid_regions= rectangles ();
    invalidate_render_rect (ren, x1, y1, x2, y2);
  }
  rectangle damage () { return least_upper_bound (invalid_regions); }
};

void
TestQTMRenderService::retryDamageStaysInBackingPixels () {
  auto* rep= tm_new<damage_test_widget> ();
  widget owner (rep);
  rep->attachCanvas ();
  qt_renderer_rep* ren= the_qt_renderer (1.0);
  ren->set_origin (-123 * PIXEL, 4567 * PIXEL);
  int padding= (int) ceil (rep->canvas ()->devicePixelRatio () * 8.0);
  rectangle pixels (10, 20, 300, 180);
  for (int i= 0; i < 12; ++i) {
    SI x1= pixels->x1, y1= pixels->y1;
    SI x2= pixels->x2, y2= pixels->y2;
    ren->encode (x1, y1);
    ren->encode (x2, y2);
    rep->retry (ren, x1, y2, x2, y1);
    pixels= rep->damage ();
    // Each failed attempt adds only the existing text damage halo, never
    // another internal-unit scale factor or scroll offset.
    QCOMPARE (pixels->x1, 10 - (i + 1) * padding);
    QCOMPARE (pixels->y1, 20 - (i + 1) * padding);
    QCOMPARE (pixels->x2, 300 + (i + 1) * padding);
    QCOMPARE (pixels->y2, 180 + (i + 1) * padding);
  }
  delete rep->canvas ();
}

QTEST_MAIN (TestQTMRenderService)
#include "qtm_render_service_test.moc"
