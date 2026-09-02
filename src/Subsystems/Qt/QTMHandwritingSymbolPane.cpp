/******************************************************************************
* MODULE     : QTMHandwritingSymbolPane.cpp
* DESCRIPTION: Handwritten mathematical symbol recognition pane
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMHandwritingSymbolPane.hpp"

#include "QTMMainTabWindow.hpp"
#include "QTMVaultPreviewWidget.hpp"
#include "scheme.hpp"
#include "sys_utils.hpp"
#include "tree.hpp"

#include <DockManager.h>
#include <DockWidget.h>
#include <QApplication>
#include <QEventPoint>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QStyle>
#include <QTabletEvent>
#include <QThreadPool>
#include <QTimer>
#include <QTouchEvent>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>

namespace {

QTMHandwritingSymbolPane* handwriting_widget= nullptr;
ads::CDockWidget* handwriting_dock= nullptr;

std::string
std_string (string value) {
  return std::string (as_charp (value), (size_t) N(value));
}

string
tm_string (const QString& value) {
  QByteArray bytes= value.toUtf8 ();
  return string (bytes.constData (), bytes.size ());
}

QString
q_string (const std::string& value) {
  return QString::fromUtf8 (value.data (), (qsizetype) value.size ());
}

QString
input_description (const QString& command) {
  try {
    return q_string (std_string (as_string (
      call ("handwriting-symbol-input-description",
            object (tm_string (command))))));
  }
  catch (...) { return QString (); }
}

} // namespace

QTMHandwritingCanvas::QTMHandwritingCanvas (QWidget* parent)
  : QWidget (parent), drawingStroke (false), tabletStroke (false),
    touchId (-1) {
  setAttribute (Qt::WA_AcceptTouchEvents, true);
  setFocusPolicy (Qt::StrongFocus);
  setMouseTracking (true);
  setMinimumSize (320, 320);
}

const std::vector<athena_handwriting_stroke>&
QTMHandwritingCanvas::strokes () const {
  return drawing;
}

bool
QTMHandwritingCanvas::canUndo () const {
  return !drawing.empty ();
}

bool
QTMHandwritingCanvas::canRedo () const {
  return !redoDrawing.empty ();
}

void
QTMHandwritingCanvas::setChangedCallback (std::function<void ()> callback) {
  changedCallback= std::move (callback);
}

void
QTMHandwritingCanvas::undo () {
  if (drawing.empty ()) return;
  redoDrawing.push_back (std::move (drawing.back ()));
  drawing.pop_back ();
  changed ();
}

void
QTMHandwritingCanvas::redo () {
  if (redoDrawing.empty ()) return;
  drawing.push_back (std::move (redoDrawing.back ()));
  redoDrawing.pop_back ();
  changed ();
}

void
QTMHandwritingCanvas::clear () {
  if (drawing.empty () && currentStroke.empty ()) return;
  drawing.clear ();
  redoDrawing.clear ();
  currentStroke.clear ();
  drawingStroke= false;
  changed ();
}

QSize
QTMHandwritingCanvas::sizeHint () const {
  return QSize (460, 460);
}

void
QTMHandwritingCanvas::beginStroke (const QPointF& point) {
  redoDrawing.clear ();
  currentStroke.clear ();
  currentStroke.push_back ({(float) point.x (), (float) point.y ()});
  drawingStroke= true;
  update ();
}

void
QTMHandwritingCanvas::appendPoint (const QPointF& point) {
  if (!drawingStroke) return;
  athena_handwriting_point next= {(float) point.x (), (float) point.y ()};
  if (!currentStroke.empty ()) {
    const auto& previous= currentStroke.back ();
    if (std::abs (previous.x - next.x) < 0.25f &&
        std::abs (previous.y - next.y) < 0.25f) return;
  }
  currentStroke.push_back (next);
  update ();
}

void
QTMHandwritingCanvas::endStroke () {
  if (!drawingStroke) return;
  drawingStroke= false;
  if (!currentStroke.empty ()) drawing.push_back (std::move (currentStroke));
  currentStroke.clear ();
  changed ();
}

void
QTMHandwritingCanvas::changed () {
  update ();
  if (changedCallback) changedCallback ();
}

bool
QTMHandwritingCanvas::event (QEvent* event) {
  if (event->type () == QEvent::TabletPress ||
      event->type () == QEvent::TabletMove ||
      event->type () == QEvent::TabletRelease) {
    QTabletEvent* tablet= static_cast<QTabletEvent*> (event);
    if (event->type () == QEvent::TabletPress) {
      tabletStroke= true;
      beginStroke (tablet->position ());
    }
    else if (event->type () == QEvent::TabletMove)
      appendPoint (tablet->position ());
    else {
      appendPoint (tablet->position ());
      endStroke ();
      tabletStroke= false;
    }
    event->accept ();
    return true;
  }

  if (event->type () == QEvent::TouchBegin ||
      event->type () == QEvent::TouchUpdate ||
      event->type () == QEvent::TouchEnd ||
      event->type () == QEvent::TouchCancel) {
    QTouchEvent* touch= static_cast<QTouchEvent*> (event);
    if (event->type () == QEvent::TouchCancel) {
      currentStroke.clear ();
      drawingStroke= false;
      touchId= -1;
      update ();
      event->accept ();
      return true;
    }
    for (const QEventPoint& point: touch->points ()) {
      if (touchId < 0 && point.state () == QEventPoint::State::Pressed) {
        touchId= point.id ();
        beginStroke (point.position ());
      }
      if (point.id () != touchId) continue;
      if (point.state () == QEventPoint::State::Updated)
        appendPoint (point.position ());
      if (point.state () == QEventPoint::State::Released) {
        appendPoint (point.position ());
        endStroke ();
        touchId= -1;
      }
    }
    event->accept ();
    return true;
  }
  return QWidget::event (event);
}

void
QTMHandwritingCanvas::mousePressEvent (QMouseEvent* event) {
  if (event->button () == Qt::LeftButton && !tabletStroke && touchId < 0) {
    beginStroke (event->position ());
    event->accept ();
    return;
  }
  QWidget::mousePressEvent (event);
}

void
QTMHandwritingCanvas::mouseMoveEvent (QMouseEvent* event) {
  if (drawingStroke && !tabletStroke && touchId < 0) {
    appendPoint (event->position ());
    event->accept ();
    return;
  }
  QWidget::mouseMoveEvent (event);
}

void
QTMHandwritingCanvas::mouseReleaseEvent (QMouseEvent* event) {
  if (event->button () == Qt::LeftButton && drawingStroke &&
      !tabletStroke && touchId < 0) {
    appendPoint (event->position ());
    endStroke ();
    event->accept ();
    return;
  }
  QWidget::mouseReleaseEvent (event);
}

void
QTMHandwritingCanvas::paintEvent (QPaintEvent*) {
  QPainter painter (this);
  painter.setRenderHint (QPainter::Antialiasing, true);
  painter.fillRect (rect (), palette ().brush (QPalette::Base));
  painter.setPen (QPen (palette ().color (QPalette::Mid), 1));
  painter.drawRect (rect ().adjusted (0, 0, -1, -1));

  if (drawing.empty () && currentStroke.empty ()) {
    painter.setPen (palette ().color (QPalette::PlaceholderText));
    painter.drawText (rect (), Qt::AlignCenter, "Draw a symbol");
    return;
  }

  QPen pen (palette ().color (QPalette::Text), 3.5,
            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  painter.setPen (pen);
  auto paint_stroke= [&] (const athena_handwriting_stroke& stroke) {
    if (stroke.empty ()) return;
    QPainterPath path;
    path.moveTo (stroke.front ().x, stroke.front ().y);
    if (stroke.size () == 1)
      path.lineTo (stroke.front ().x + 0.5, stroke.front ().y + 0.5);
    else for (size_t i=1; i<stroke.size (); i++)
      path.lineTo (stroke[i].x, stroke[i].y);
    painter.drawPath (path);
  };
  for (const auto& stroke: drawing) paint_stroke (stroke);
  paint_stroke (currentStroke);
}

QTMHandwritingSymbolPane::QTMHandwritingSymbolPane (QWidget* parent)
  : QWidget (parent), canvas (new QTMHandwritingCanvas (this)),
    results (new QTreeWidget (this)), status (new QLabel (this)),
    undoButton (new QPushButton (this)), redoButton (new QPushButton (this)),
    clearButton (new QPushButton (this)), insertButton (new QPushButton ("Insert", this)),
    recognitionTimer (new QTimer (this)), previewHost (new QWidget (this)),
    previewPlaceholder (new QLabel ("Select a match to preview", previewHost)),
    preview (new WikilinkPreview (this)), generation (0) {
  std::string assets= std_string (get_env ("ATHENA_PATH")) +
    "/misc/models/handwriting";
  recognizer= std::make_shared<athena_handwriting_recognizer> (assets);

  QVBoxLayout* root= new QVBoxLayout (this);
  root->setContentsMargins (6, 6, 6, 6);
  root->setSpacing (6);

  QHBoxLayout* tools= new QHBoxLayout;
  undoButton->setIcon (style ()->standardIcon (QStyle::SP_ArrowBack));
  undoButton->setToolTip ("Undo stroke");
  redoButton->setIcon (style ()->standardIcon (QStyle::SP_ArrowForward));
  redoButton->setToolTip ("Redo stroke");
  clearButton->setIcon (style ()->standardIcon (QStyle::SP_DialogResetButton));
  clearButton->setToolTip ("Clear drawing");
  for (QPushButton* button: {undoButton, redoButton, clearButton}) {
    button->setText ("");
    button->setFixedSize (32, 32);
    button->setIconSize (QSize (20, 20));
    tools->addWidget (button);
  }
  tools->addStretch (1);
  tools->addWidget (status);
  root->addLayout (tools);

  QSplitter* splitter= new QSplitter (Qt::Horizontal, this);
  splitter->addWidget (canvas);
  QWidget* right= new QWidget (splitter);
  QVBoxLayout* rightLayout= new QVBoxLayout (right);
  rightLayout->setContentsMargins (0, 0, 0, 0);
  rightLayout->setSpacing (6);

  previewHost->setMinimumHeight (94);
  previewHost->setMaximumHeight (128);
  QVBoxLayout* previewLayout= new QVBoxLayout (previewHost);
  previewLayout->setContentsMargins (0, 0, 0, 0);
  previewPlaceholder->setAlignment (Qt::AlignCenter);
  previewPlaceholder->setForegroundRole (QPalette::PlaceholderText);
  previewLayout->addWidget (previewPlaceholder);
  rightLayout->addWidget (previewHost);

  results->setColumnCount (2);
  results->setHeaderLabels ({"ATHENA input", "Match"});
  results->setRootIsDecorated (false);
  results->setAlternatingRowColors (true);
  results->setSelectionMode (QAbstractItemView::SingleSelection);
  results->header ()->setSectionResizeMode (0, QHeaderView::Stretch);
  results->header ()->setSectionResizeMode (1, QHeaderView::ResizeToContents);
  rightLayout->addWidget (results, 1);
  rightLayout->addWidget (insertButton, 0, Qt::AlignRight);
  splitter->addWidget (right);
  splitter->setStretchFactor (0, 3);
  splitter->setStretchFactor (1, 2);
  root->addWidget (splitter, 1);

  recognitionTimer->setSingleShot (true);
  recognitionTimer->setInterval (180);
  connect (recognitionTimer, &QTimer::timeout, this,
           [this] () { startRecognition (); });
  canvas->setChangedCallback ([this] () { scheduleRecognition (); });
  connect (undoButton, &QPushButton::clicked, canvas,
           [this] () { canvas->undo (); });
  connect (redoButton, &QPushButton::clicked, canvas,
           [this] () { canvas->redo (); });
  connect (clearButton, &QPushButton::clicked, canvas,
           [this] () { canvas->clear (); });
  connect (results, &QTreeWidget::itemSelectionChanged, this,
           [this] () { selectionChanged (); });
  connect (results, &QTreeWidget::itemDoubleClicked, this,
           [this] (QTreeWidgetItem*, int) { insertCurrent (); });
  connect (insertButton, &QPushButton::clicked, this,
           [this] () { insertCurrent (); });
  for (Qt::Key key: {Qt::Key_Return, Qt::Key_Enter}) {
    QShortcut* shortcut= new QShortcut (QKeySequence (key), this);
    shortcut->setContext (Qt::WidgetWithChildrenShortcut);
    connect (shortcut, &QShortcut::activated, this,
             [this] () { insertCurrent (); });
  }
  QShortcut* closeShortcut=
    new QShortcut (QKeySequence (Qt::Key_Escape), this);
  closeShortcut->setContext (Qt::WidgetWithChildrenShortcut);
  connect (closeShortcut, &QShortcut::activated, this,
           [this] () { qtm_close_focused_ads_tool_pane (this); });
  status->setText ("Draw a symbol");
  refreshButtons ();
}

QSize
QTMHandwritingSymbolPane::sizeHint () const {
  return QSize (920, 540);
}

void
QTMHandwritingSymbolPane::focusCanvas () {
  canvas->setFocus (Qt::OtherFocusReason);
}

void
QTMHandwritingSymbolPane::scheduleRecognition () {
  generation++;
  refreshButtons ();
  if (canvas->strokes ().empty ()) {
    recognitionTimer->stop ();
    results->clear ();
    currentPredictions.clear ();
    insertButton->setEnabled (false);
    status->setText ("Draw a symbol");
    updatePreview ("", "");
    return;
  }
  status->setText ("Recognizing...");
  recognitionTimer->start ();
}

void
QTMHandwritingSymbolPane::startRecognition () {
  int requestGeneration= generation;
  auto strokes= canvas->strokes ();
  float width= (float) canvas->width ();
  float height= (float) canvas->height ();
  auto model= recognizer;
  QPointer<QTMHandwritingSymbolPane> self (this);
  QThreadPool::globalInstance ()->start (
    [self, model, strokes=std::move (strokes), width, height,
     requestGeneration] () mutable {
      std::string error;
      auto predictions= model->recognize (strokes, width, height, 80, error);
      if (self == nullptr) return;
      QMetaObject::invokeMethod (self,
        [self, requestGeneration, predictions=std::move (predictions),
         error=q_string (error)] () mutable {
          if (self != nullptr)
            self->applyRecognition (requestGeneration, std::move (predictions),
                                    error);
        }, Qt::QueuedConnection);
    });
}

void
QTMHandwritingSymbolPane::applyRecognition (
  int requestGeneration,
  std::vector<athena_handwriting_prediction> predictions,
  const QString& error) {
  if (requestGeneration != generation) return;
  results->clear ();
  currentPredictions.clear ();
  if (!error.isEmpty ()) {
    status->setText (error);
    insertButton->setEnabled (false);
    return;
  }

  for (const auto& prediction: predictions) {
    QString command= q_string (prediction.command);
    QString input= input_description (command);
    if (input.isEmpty ()) continue;
    int index= (int) currentPredictions.size ();
    currentPredictions.push_back (prediction);
    QTreeWidgetItem* item= new QTreeWidgetItem (results);
    item->setText (0, input);
    item->setText (1, QString::number (100.0f * prediction.confidence, 'f', 1) + "%");
    item->setData (0, Qt::UserRole, index);
    item->setToolTip (0, command);
    if (currentPredictions.size () >= 24) break;
  }
  if (results->topLevelItemCount () > 0) {
    results->setCurrentItem (results->topLevelItem (0));
    status->setText (QString ("%1 matches").arg (results->topLevelItemCount ()));
  }
  else {
    status->setText ("No supported ATHENA symbol found");
    insertButton->setEnabled (false);
    updatePreview ("", "");
  }
}

void
QTMHandwritingSymbolPane::refreshButtons () {
  undoButton->setEnabled (canvas->canUndo ());
  redoButton->setEnabled (canvas->canRedo ());
  clearButton->setEnabled (canvas->canUndo ());
}

void
QTMHandwritingSymbolPane::selectionChanged () {
  QTreeWidgetItem* item= results->currentItem ();
  if (item == nullptr) {
    insertButton->setEnabled (false);
    updatePreview ("", "");
    return;
  }
  int index= item->data (0, Qt::UserRole).toInt ();
  if (index < 0 || index >= (int) currentPredictions.size ()) return;
  insertButton->setEnabled (true);
  QString command= q_string (currentPredictions[(size_t) index].command);
  updatePreview (command, item->text (0));
}

void
QTMHandwritingSymbolPane::updatePreview (const QString& command,
                                         const QString& input) {
  if (command.isEmpty ()) {
    preview->destroyPreview ();
    previewPlaceholder->show ();
    return;
  }
  previewPlaceholder->hide ();
  tree body;
  if (input.startsWith ('\\')) {
    QString name= command.startsWith ('\\') ? command.mid (1) : command;
    body= tree ("<" * tm_string (name) * ">");
  }
  else body= tree (tm_string (input));
  preview->setBody (tree (DOCUMENT, compound ("math", body)));
  preview->ensureCreated (previewHost);
}

void
QTMHandwritingSymbolPane::insertCurrent () {
  QTreeWidgetItem* item= results->currentItem ();
  if (item == nullptr) return;
  int index= item->data (0, Qt::UserRole).toInt ();
  if (index < 0 || index >= (int) currentPredictions.size ()) return;
  QString command= q_string (currentPredictions[(size_t) index].command);
  try { call ("handwriting-symbol-insert", object (tm_string (command))); }
  catch (...) {
    QMessageBox::warning (this, "Handwritten Symbol",
                          "ATHENA could not insert " + command + ".");
  }
}

void
handwriting_symbol_pane_show () {
  QTMMainTabWindow* window= QTMMainTabWindow::topTabWindow ();
  if (window == nullptr || window->dockManager () == nullptr) {
    QMessageBox::warning (QApplication::activeWindow (), "Handwritten Symbol",
                          "No active ATHENA window.");
    return;
  }

  if (handwriting_widget == nullptr) {
    handwriting_widget= new QTMHandwritingSymbolPane;
    QObject::connect (handwriting_widget, &QObject::destroyed, [] () {
      handwriting_widget= nullptr;
      handwriting_dock= nullptr;
    });
  }
  if (handwriting_dock == nullptr) {
    handwriting_dock= new ads::CDockWidget ("Handwritten Symbol");
    handwriting_dock->setObjectName ("athena-handwritten-symbol");
    handwriting_dock->resize (920, 540);
    handwriting_dock->setWidget (handwriting_widget,
                                 ads::CDockWidget::ForceNoScrollArea);
    handwriting_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QObject::connect (handwriting_dock, &QObject::destroyed, [] () {
      handwriting_dock= nullptr;
    });
    window->dockManager ()->addDockWidgetFloating (handwriting_dock);
    window->scheduleAdsLayoutRestore (handwriting_dock);
  }
  else if (handwriting_dock->dockAreaWidget () == nullptr ||
           handwriting_dock->dockContainer () == nullptr)
    window->dockManager ()->addDockWidgetFloating (handwriting_dock);

  handwriting_dock->toggleView (true);
  handwriting_dock->show ();
  handwriting_dock->raise ();
  handwriting_widget->focusCanvas ();
}
