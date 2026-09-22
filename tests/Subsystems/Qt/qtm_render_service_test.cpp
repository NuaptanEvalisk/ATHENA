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
#include "ATHENA/Math/native_shape_recognizer.hpp"
#include "qt_font.hpp"
#include "qt_renderer.hpp"
#include "qt_simple_widget.hpp"
#include "QTMWidget.hpp"

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
  void preservesOpenPolylineMode ();
  void emojiFontUsesUprightCaretMetrics ();
  void rendersColorEmojiWithoutOutlinePath ();
  void givesEachProducerThreadItsOwnQtRenderer ();
  void retryDamageStaysInBackingPixels ();
  void nativeInkCommitsOneDetachedStroke ();
  void nativeDrawingGestureKeepsSelectedTool ();
  void nativeShapeGestureCommitsAsShapeTool ();
  void nativeTextAndMathClicksCommitSelectedTool ();
  void nativeRecognitionRoutesOneFinalCommit ();
  void nativeLassoDragCommitsOneMoveTransform ();
  void nativeInsertSpaceCommandsAreOneShotGestures ();
  void nativeTrimCommandCommitsOnce ();
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
TestQTMRenderService::preservesOpenPolylineMode () {
  auto connection= QTMRenderConnection::create (2, 4096);
  QVERIFY (connection != nullptr);

  render_damage damage {0, 0, 64, 64};
  auto recording= connection->beginRecording (
    64, 64, 1.0, qRgba (255, 255, 255, 255), 5, 11, damage);
  QVERIFY (recording != nullptr);
  QPainter painter (recording->device ());
  QPen pen (Qt::black);
  pen.setWidthF (3.0);
  pen.setCapStyle (Qt::RoundCap);
  pen.setJoinStyle (Qt::RoundJoin);
  painter.setPen (pen);
  painter.setBrush (Qt::NoBrush);
  QPolygonF stroke;
  stroke << QPointF (8.0, 48.0) << QPointF (32.0, 8.0)
         << QPointF (56.0, 48.0);
  painter.drawPolyline (stroke);
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

  const QImage& image= frame.image ();
  QVERIFY (image.pixelColor (32, 8) != QColor (255, 255, 255));
  QCOMPARE (image.pixelColor (32, 48), QColor (255, 255, 255));
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

class native_ink_test_widget: public qt_simple_widget_rep {
public:
  std::vector<native_ink_sample> committed;
  int commits= 0;
  native_drawing_tool activeTool= native_drawing_tool::pen;
  native_drawing_tool committedTool= native_drawing_tool::pen;
  native_drawing_shape activeShape= native_drawing_shape::line;
  native_drawing_shape committedShape= native_drawing_shape::line;
  int transformCommits= 0;
  native_drawing_transform committedTransform= native_drawing_transform::move;
  std::vector<native_ink_sample> transformed;
  std::vector<native_drawing_selection_box> selection;
  int insertSpaceCommits= 0;
  bool insertSpaceHorizontal= true;
  std::vector<native_ink_sample> insertedSpace;
  int trimCommits= 0;
  bool recognitionEnabled= false;
  int recognitionCommits= 0;
  native_shape_recognition_result recognized;

  void attachCanvas () { qwid= new QTMWidget (nullptr, this); }

  bool handle_native_ink_hit (
    SI, SI, native_ink_preview_style& style) override {
    style.rgba= 0xff2040a0U;
    style.line_width_pixels= 3.0;
    style.pressure_enabled= true;
    style.recognition_enabled= recognitionEnabled;
    style.tool= activeTool;
    style.shape= activeShape;
    return true;
  }

  bool handle_native_drawing_recognition_request (
    const native_ink_sample* samples, std::size_t count) override {
    native_shape_recognition_result result=
      recognize_native_shape (samples, count);
    if (result.kind != native_shape_recognition_kind::none) {
      ++recognitionCommits;
      recognized= result;
    }
    else {
      ++commits;
      committedTool= native_drawing_tool::pen;
      committedShape= native_drawing_shape::line;
      committed.assign (samples, samples + count);
    }
    return true;
  }

  bool handle_native_drawing_gesture (
    native_drawing_tool tool, native_drawing_shape shape,
    const native_ink_sample* samples,
    std::size_t count) override {
    ++commits;
    committedTool= tool;
    committedShape= shape;
    committed.assign (samples, samples + count);
    return true;
  }

  native_drawing_tool handle_native_drawing_tool () override {
    return activeTool;
  }

  std::vector<native_drawing_selection_box>
  handle_native_drawing_selection () override {
    return selection;
  }

  bool handle_native_drawing_transform (
    native_drawing_transform transform,
    const native_ink_sample* samples, std::size_t count) override {
    ++transformCommits;
    committedTransform= transform;
    transformed.assign (samples, samples + count);
    return true;
  }

  bool handle_native_drawing_insert_space (
    bool horizontal, const native_ink_sample* samples,
    std::size_t count) override {
    ++insertSpaceCommits;
    insertSpaceHorizontal= horizontal;
    insertedSpace.assign (samples, samples + count);
    return true;
  }

  bool handle_native_drawing_trim () override {
    ++trimCommits;
    return true;
  }
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

void
TestQTMRenderService::nativeInkCommitsOneDetachedStroke () {
  auto* rep= tm_new<native_ink_test_widget> ();
  widget owner (rep);
  rep->attachCanvas ();
  QTMWidget* canvas= rep->canvas ();
  QVERIFY (canvas != nullptr);
  canvas->resize (480, 320);
  QWidget* surface= canvas->surface ();
  QVERIFY (surface != nullptr);

  auto send= [&] (QEvent::Type type, QPointF pos, Qt::MouseButton button,
                  Qt::MouseButtons buttons) {
    QMouseEvent event (type, pos, pos, button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent (surface, &event);
  };
  send (QEvent::MouseButtonPress, QPointF (80, 100),
        Qt::LeftButton, Qt::LeftButton);
  send (QEvent::MouseMove, QPointF (120, 110),
        Qt::NoButton, Qt::LeftButton);
  send (QEvent::MouseMove, QPointF (180, 125),
        Qt::NoButton, Qt::LeftButton);
  send (QEvent::MouseMove, QPointF (240, 145),
        Qt::NoButton, Qt::LeftButton);
  send (QEvent::MouseButtonRelease, QPointF (280, 160),
        Qt::LeftButton, Qt::NoButton);

  QCOMPARE (rep->commits, 1);
  QCOMPARE (rep->committedTool, native_drawing_tool::pen);
  QVERIFY (rep->committed.size () >= 4);
  QCOMPARE (rep->committed.front ().pressure, 1.0);
  QCOMPARE (rep->committed.back ().pressure, 1.0);
  QVERIFY (rep->committed.back ().x > rep->committed.front ().x);

  delete canvas;
}

void
TestQTMRenderService::nativeDrawingGestureKeepsSelectedTool () {
  auto* rep= tm_new<native_ink_test_widget> ();
  widget owner (rep);
  rep->activeTool= native_drawing_tool::highlighter;
  rep->attachCanvas ();
  QTMWidget* canvas= rep->canvas ();
  QVERIFY (canvas != nullptr);
  QWidget* surface= canvas->surface ();
  QVERIFY (surface != nullptr);

  auto send= [&] (QEvent::Type type, QPointF pos, Qt::MouseButton button,
                  Qt::MouseButtons buttons) {
    QMouseEvent event (type, pos, pos, button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent (surface, &event);
  };
  send (QEvent::MouseButtonPress, QPointF (60, 80),
        Qt::LeftButton, Qt::LeftButton);
  send (QEvent::MouseMove, QPointF (120, 90),
        Qt::NoButton, Qt::LeftButton);
  send (QEvent::MouseButtonRelease, QPointF (180, 100),
        Qt::LeftButton, Qt::NoButton);

  QCOMPARE (rep->commits, 1);
  QCOMPARE (rep->committedTool, native_drawing_tool::highlighter);
  QVERIFY (rep->committed.size () >= 3);
  delete canvas;
}

void
TestQTMRenderService::nativeShapeGestureCommitsAsShapeTool () {
  auto* rep= tm_new<native_ink_test_widget> ();
  widget owner (rep);
  rep->activeTool= native_drawing_tool::shape;
  rep->activeShape= native_drawing_shape::hexagon;
  rep->attachCanvas ();
  QTMWidget* canvas= rep->canvas ();
  QVERIFY (canvas != nullptr);
  QWidget* surface= canvas->surface ();
  QVERIFY (surface != nullptr);

  auto send= [&] (QEvent::Type type, QPointF pos, Qt::MouseButton button,
                  Qt::MouseButtons buttons) {
    QMouseEvent event (type, pos, pos, button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent (surface, &event);
  };
  send (QEvent::MouseButtonPress, QPointF (70, 80),
        Qt::LeftButton, Qt::LeftButton);
  send (QEvent::MouseMove, QPointF (150, 140),
        Qt::NoButton, Qt::LeftButton);
  send (QEvent::MouseButtonRelease, QPointF (210, 170),
        Qt::LeftButton, Qt::NoButton);

  QCOMPARE (rep->commits, 1);
  QCOMPARE (rep->committedTool, native_drawing_tool::shape);
  QCOMPARE (rep->committedShape, native_drawing_shape::hexagon);
  QVERIFY (rep->committed.size () >= 3);
  delete canvas;
}

void
TestQTMRenderService::nativeTextAndMathClicksCommitSelectedTool () {
  auto* rep= tm_new<native_ink_test_widget> ();
  widget owner (rep);
  rep->activeTool= native_drawing_tool::text;
  rep->attachCanvas ();
  QTMWidget* canvas= rep->canvas ();
  QVERIFY (canvas != nullptr);
  QWidget* surface= canvas->surface ();
  QVERIFY (surface != nullptr);

  auto click= [&] (QPointF pos) {
    QMouseEvent press (QEvent::MouseButtonPress, pos, pos,
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent (surface, &press);
    QMouseEvent release (QEvent::MouseButtonRelease, pos, pos,
                         Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent (surface, &release);
  };

  click (QPointF (90, 95));
  QCOMPARE (rep->commits, 1);
  QCOMPARE (rep->committedTool, native_drawing_tool::text);
  QVERIFY (!rep->committed.empty ());

  rep->activeTool= native_drawing_tool::math;
  click (QPointF (145, 130));
  QCOMPARE (rep->commits, 2);
  QCOMPARE (rep->committedTool, native_drawing_tool::math);
  QVERIFY (!rep->committed.empty ());

  QMouseEvent hover (QEvent::MouseMove, QPointF (160, 140), QPointF (160, 140),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent (surface, &hover);
  QCOMPARE (surface->cursor ().shape (), Qt::IBeamCursor);

  delete canvas;
}

void
TestQTMRenderService::nativeRecognitionRoutesOneFinalCommit () {
  auto* rep= tm_new<native_ink_test_widget> ();
  widget owner (rep);
  rep->recognitionEnabled= true;
  rep->attachCanvas ();
  QTMWidget* canvas= rep->canvas ();
  QVERIFY (canvas != nullptr);
  QWidget* surface= canvas->surface ();
  QVERIFY (surface != nullptr);

  auto send= [&] (QEvent::Type type, QPointF pos, Qt::MouseButton button,
                   Qt::MouseButtons buttons) {
    QMouseEvent event (type, pos, pos, button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent (surface, &event);
  };
  send (QEvent::MouseButtonPress, QPointF (60, 80),
        Qt::LeftButton, Qt::LeftButton);
  send (QEvent::MouseMove, QPointF (100, 100),
        Qt::NoButton, Qt::LeftButton);
  send (QEvent::MouseMove, QPointF (150, 125),
        Qt::NoButton, Qt::LeftButton);
  send (QEvent::MouseMove, QPointF (205, 152),
        Qt::NoButton, Qt::LeftButton);
  send (QEvent::MouseButtonRelease, QPointF (260, 180),
        Qt::LeftButton, Qt::NoButton);

  QCOMPARE (rep->commits, 0);
  QCOMPARE (rep->recognitionCommits, 1);
  QCOMPARE (rep->commits, 0);
  QCOMPARE ((int) rep->recognized.kind,
            (int) native_shape_recognition_kind::line);
  QVERIFY (rep->recognized.confidence >= 0.72);

  // A clearly non-geometric open scribble must fall back to the original Pen
  // gesture, again as one final command rather than a provisional document edit.
  send (QEvent::MouseButtonPress, QPointF (70, 210),
        Qt::LeftButton, Qt::LeftButton);
  send (QEvent::MouseMove, QPointF (110, 245),
        Qt::NoButton, Qt::LeftButton);
  send (QEvent::MouseMove, QPointF (150, 205),
        Qt::NoButton, Qt::LeftButton);
  send (QEvent::MouseMove, QPointF (190, 250),
        Qt::NoButton, Qt::LeftButton);
  send (QEvent::MouseMove, QPointF (230, 200),
        Qt::NoButton, Qt::LeftButton);
  send (QEvent::MouseButtonRelease, QPointF (270, 240),
        Qt::LeftButton, Qt::NoButton);
  QCOMPARE (rep->commits, 1);
  QCOMPARE (rep->recognitionCommits, 1);
  QCOMPARE (rep->committedTool, native_drawing_tool::pen);

  delete canvas;
}

void
TestQTMRenderService::nativeLassoDragCommitsOneMoveTransform () {
  auto* rep= tm_new<native_ink_test_widget> ();
  widget owner (rep);
  rep->activeTool= native_drawing_tool::lasso;
  native_drawing_selection_box box;
  box.x1= 50 * PIXEL;
  box.y1= -150 * PIXEL;
  box.x2= 200 * PIXEL;
  box.y2= -50 * PIXEL;
  rep->selection.push_back (box);
  rep->attachCanvas ();
  QTMWidget* canvas= rep->canvas ();
  QVERIFY (canvas != nullptr);
  QWidget* surface= canvas->surface ();
  QVERIFY (surface != nullptr);

  auto send= [&] (QEvent::Type type, QPointF pos, Qt::MouseButton button,
                  Qt::MouseButtons buttons) {
    QMouseEvent event (type, pos, pos, button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent (surface, &event);
  };
  send (QEvent::MouseButtonPress, QPointF (100, 100),
        Qt::LeftButton, Qt::LeftButton);
  send (QEvent::MouseMove, QPointF (135, 120),
        Qt::NoButton, Qt::LeftButton);
  send (QEvent::MouseButtonRelease, QPointF (150, 125),
        Qt::LeftButton, Qt::NoButton);

  QCOMPARE (rep->transformCommits, 1);
  QCOMPARE (rep->committedTransform, native_drawing_transform::move);
  QCOMPARE ((int) rep->transformed.size (), 2);
  QVERIFY (rep->transformed[1].x > rep->transformed[0].x);
  QVERIFY (rep->transformed[1].y < rep->transformed[0].y);
  QCOMPARE (rep->commits, 0);
  delete canvas;
}

void
TestQTMRenderService::nativeInsertSpaceCommandsAreOneShotGestures () {
  auto* rep= tm_new<native_ink_test_widget> ();
  widget owner (rep);
  rep->attachCanvas ();
  QTMWidget* canvas= rep->canvas ();
  QVERIFY (canvas != nullptr);
  QWidget* surface= canvas->surface ();
  QVERIFY (surface != nullptr);

  auto send= [&] (QEvent::Type type, QPointF pos, Qt::MouseButton button,
                  Qt::MouseButtons buttons) {
    QMouseEvent event (type, pos, pos, button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent (surface, &event);
  };

  canvas->triggerNativeDrawingCanvasCommand (
    native_drawing_canvas_command::insert_horizontal_space);
  send (QEvent::MouseButtonPress, QPointF (80, 100),
        Qt::LeftButton, Qt::LeftButton);
  send (QEvent::MouseMove, QPointF (130, 100),
        Qt::NoButton, Qt::LeftButton);
  send (QEvent::MouseButtonRelease, QPointF (170, 100),
        Qt::LeftButton, Qt::NoButton);
  QCOMPARE (rep->insertSpaceCommits, 1);
  QVERIFY (rep->insertSpaceHorizontal);
  QCOMPARE ((int) rep->insertedSpace.size (), 2);
  QVERIFY (rep->insertedSpace[1].x > rep->insertedSpace[0].x);

  // The canvas command is one-shot: the next drag goes back to the current
  // drawing tool instead of inserting another gap.
  send (QEvent::MouseButtonPress, QPointF (90, 120),
        Qt::LeftButton, Qt::LeftButton);
  send (QEvent::MouseMove, QPointF (120, 130),
        Qt::NoButton, Qt::LeftButton);
  send (QEvent::MouseButtonRelease, QPointF (150, 140),
        Qt::LeftButton, Qt::NoButton);
  QCOMPARE (rep->insertSpaceCommits, 1);
  QCOMPARE (rep->commits, 1);

  canvas->triggerNativeDrawingCanvasCommand (
    native_drawing_canvas_command::insert_vertical_space);
  send (QEvent::MouseButtonPress, QPointF (110, 80),
        Qt::LeftButton, Qt::LeftButton);
  send (QEvent::MouseMove, QPointF (110, 125),
        Qt::NoButton, Qt::LeftButton);
  send (QEvent::MouseButtonRelease, QPointF (110, 165),
        Qt::LeftButton, Qt::NoButton);
  QCOMPARE (rep->insertSpaceCommits, 2);
  QVERIFY (!rep->insertSpaceHorizontal);
  QCOMPARE ((int) rep->insertedSpace.size (), 2);
  QVERIFY (rep->insertedSpace[1].y < rep->insertedSpace[0].y);

  delete canvas;
}

void
TestQTMRenderService::nativeTrimCommandCommitsOnce () {
  auto* rep= tm_new<native_ink_test_widget> ();
  widget owner (rep);
  rep->attachCanvas ();
  QTMWidget* canvas= rep->canvas ();
  QVERIFY (canvas != nullptr);

  canvas->triggerNativeDrawingCanvasCommand (
    native_drawing_canvas_command::trim);
  QCOMPARE (rep->trimCommits, 1);
  QCOMPARE (rep->insertSpaceCommits, 0);
  QCOMPARE (rep->commits, 0);

  delete canvas;
}

QTEST_MAIN (TestQTMRenderService)
#include "qtm_render_service_test.moc"
