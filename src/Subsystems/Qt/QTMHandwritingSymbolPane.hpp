/******************************************************************************
* MODULE     : QTMHandwritingSymbolPane.hpp
* DESCRIPTION: Handwritten mathematical symbol recognition pane
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMHANDWRITINGSYMBOLPANE_HPP
#define QTMHANDWRITINGSYMBOLPANE_HPP

#include "ATHENA/Math/handwriting_recognizer.hpp"

#include <QPointF>
#include <QWidget>

#include <functional>
#include <memory>
#include <vector>

class QLabel;
class QPushButton;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class WikilinkPreview;

class QTMHandwritingCanvas : public QWidget {
public:
  explicit QTMHandwritingCanvas (QWidget* parent= nullptr);

  const std::vector<athena_handwriting_stroke>& strokes () const;
  bool canUndo () const;
  bool canRedo () const;
  void undo ();
  void redo ();
  void clear ();
  void setChangedCallback (std::function<void ()> callback);

protected:
  bool event (QEvent* event) override;
  void mousePressEvent (QMouseEvent* event) override;
  void mouseMoveEvent (QMouseEvent* event) override;
  void mouseReleaseEvent (QMouseEvent* event) override;
  void paintEvent (QPaintEvent* event) override;
  QSize sizeHint () const override;

private:
  void beginStroke (const QPointF& point);
  void appendPoint (const QPointF& point);
  void endStroke ();
  void changed ();

  std::vector<athena_handwriting_stroke> drawing;
  std::vector<athena_handwriting_stroke> redoDrawing;
  athena_handwriting_stroke currentStroke;
  std::function<void ()> changedCallback;
  bool drawingStroke;
  bool tabletStroke;
  int touchId;
};

class QTMHandwritingSymbolPane : public QWidget {
public:
  explicit QTMHandwritingSymbolPane (QWidget* parent= nullptr);
  QSize sizeHint () const override;
  void focusCanvas ();

private:
  void scheduleRecognition ();
  void startRecognition ();
  void applyRecognition (
    int generation, std::vector<athena_handwriting_prediction> predictions,
    const QString& error);
  void refreshButtons ();
  void selectionChanged ();
  void updatePreview (const QString& command, const QString& input);
  void insertCurrent ();

  QTMHandwritingCanvas* canvas;
  QTreeWidget* results;
  QLabel* status;
  QPushButton* undoButton;
  QPushButton* redoButton;
  QPushButton* clearButton;
  QPushButton* insertButton;
  QTimer* recognitionTimer;
  QWidget* previewHost;
  QLabel* previewPlaceholder;
  WikilinkPreview* preview;
  std::shared_ptr<athena_handwriting_recognizer> recognizer;
  std::vector<athena_handwriting_prediction> currentPredictions;
  int generation;
};

void handwriting_symbol_pane_show ();

#endif // QTMHANDWRITINGSYMBOLPANE_HPP
