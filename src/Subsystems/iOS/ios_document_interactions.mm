/******************************************************************************
* MODULE     : ios_document_interactions.mm
* DESCRIPTION: Native touch context gestures over Qt document surfaces
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include "athena_ios.hpp"
#include "Subsystems/Qt/QTMWidget.hpp"
#include "Subsystems/Qt/QTMCompoundDocument.hpp"
#include <QEvent>
#include <QAbstractItemView>
#include <QApplication>
#include <QContextMenuEvent>
#include <QPointer>
#import <UIKit/UIKit.h>
#import <UIKit/UIGestureRecognizerSubclass.h>

namespace {
QTMWidget* canvas_at (QWidget* shell, CGPoint point, bool activate= false) {
  if (!shell) return nullptr;
  QPoint position (qRound (point.x), qRound (point.y));
  QWidget* hit= shell->childAt (position);
  for (QWidget* parent= hit; parent; parent= parent->parentWidget ()) {
    if (auto* canvas= qobject_cast<QTMWidget*> (parent)) {
      // Scrollbars, toolbars and popup widgets retain their own gestures.
      return hit == canvas->surface () ? canvas : nullptr;
    }
    if (auto* compound= dynamic_cast<QTMCompoundDocument*> (parent))
      return compound->canvasAtGlobalPosition (shell->mapToGlobal (position), activate);
  }
  return nullptr;
}

QAbstractItemView* item_view_at (QWidget* shell, CGPoint point) {
  if (!shell) return nullptr;
  QWidget* hit= shell->childAt (QPoint (qRound (point.x), qRound (point.y)));
  // Inline editors, headers and scrollbars keep their own input gestures.
  if (hit) {
    auto* view= qobject_cast<QAbstractItemView*> (hit->parentWidget ());
    if (view && hit == view->viewport () &&
        view->contextMenuPolicy () == Qt::CustomContextMenu) return view;
  }
  return nullptr;
}
}

@interface ATHENADocumentHold : UILongPressGestureRecognizer <UIGestureRecognizerDelegate> {
@public
  QPointer<QWidget> shell;
  QPointer<QTMWidget> candidate;
  QPointer<QAbstractItemView> itemView;
  CGPoint initialPoint;
}
@end

@implementation ATHENADocumentHold
- (instancetype)init {
  self= [super initWithTarget:nil action:nil];
  if (self) {
    [self addTarget:self action:@selector(held:)];
    self.delegate= self;
    self.allowedTouchTypes= @[@(UITouchTypeDirect)];
    self.numberOfTouchesRequired= 1;
    // Recognition must precede Qt's synthetic mouse press: a held link must
    // not navigate, nor may a held selection collapse before the menu opens.
    self.delaysTouchesBegan= YES;
    self.cancelsTouchesInView= YES;
  }
  return self;
}
- (BOOL)gestureRecognizer:(UIGestureRecognizer*)gesture shouldReceiveTouch:(UITouch*)touch {
  (void) gesture;
  if (candidate) candidate->cancelTouchContextMenu ();
  initialPoint= [touch locationInView:self.view];
  candidate= canvas_at (shell, initialPoint);
  itemView= item_view_at (shell, initialPoint);
  return candidate != nullptr || itemView != nullptr;
}
- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  if (event.allTouches.count > 1) {
    self.state= UIGestureRecognizerStateFailed;
    if (candidate) candidate->cancelTouchContextMenu ();
    return;
  }
  [super touchesBegan:touches withEvent:event];
}
- (void)held:(UILongPressGestureRecognizer*)gesture {
  if (!shell) return;
  if (itemView && gesture.state == UIGestureRecognizerStateBegan) {
    if (item_view_at (shell, initialPoint) != itemView) return;
    const QPoint global= shell->mapToGlobal (QPoint (qRound (initialPoint.x), qRound (initialPoint.y)));
    QWidget* viewport= itemView->viewport ();
    const QPoint local= viewport->mapFromGlobal (global);
    const QModelIndex index= itemView->indexAt (local);
    itemView->setFocus (Qt::OtherFocusReason);
    if (index.isValid ()) itemView->setCurrentIndex (index);
    QContextMenuEvent event (QContextMenuEvent::Mouse, local, global);
    QApplication::sendEvent (viewport, &event);
    return;
  }
  if (!candidate) return;
  if (gesture.state == UIGestureRecognizerStateBegan) {
    if (canvas_at (shell, initialPoint, true) != candidate) return;
    candidate->showTouchContextMenu (shell->mapToGlobal (
      QPoint (qRound (initialPoint.x), qRound (initialPoint.y))));
  }
  else if (gesture.state == UIGestureRecognizerStateCancelled)
    candidate->cancelTouchContextMenu ();
}
@end

@interface ATHENAPencilExit : UIHoverGestureRecognizer <UIGestureRecognizerDelegate> {
@public
  QPointer<QWidget> shell;
  QPointer<QTMWidget> hovered;
}
@end

@implementation ATHENAPencilExit
- (instancetype)init {
  self= [super initWithTarget:nil action:nil];
  if (self) {
    [self addTarget:self action:@selector(hovered:)];
    self.delegate= self;
    self.allowedTouchTypes= @[@(UITouchTypePencil)];
    self.cancelsTouchesInView= NO;
  }
  return self;
}
- (BOOL)gestureRecognizer:(UIGestureRecognizer*)gesture
    shouldRecognizeSimultaneouslyWithGestureRecognizer:(UIGestureRecognizer*)other {
  (void) gesture; (void) other;
  return YES;
}
- (void)hovered:(UIHoverGestureRecognizer*)gesture {
  QTMWidget* next= gesture.state == UIGestureRecognizerStateEnded ||
    gesture.state == UIGestureRecognizerStateCancelled ? nullptr :
      canvas_at (shell, [gesture locationInView:self.view]);
  // Qt retains contact/inking; UIKit provides the complete hover lifecycle.
  if (hovered && hovered != next) hovered->endPencilHover ();
  hovered= next;
  if (hovered && shell) {
    CGPoint point= [gesture locationInView:self.view];
    hovered->pencilHover (shell->mapToGlobal (QPoint (qRound (point.x), qRound (point.y))));
  }
}
@end

namespace {
class DocumentInteractions : public QObject {
  QPointer<QWidget> shell;
  __weak UIView* installed= nil;
public:
  explicit DocumentInteractions (QWidget* owner): QObject (owner), shell (owner) {
    owner->installEventFilter (this);
  }
  bool eventFilter (QObject*, QEvent* event) override {
    if (event->type () != QEvent::Show || !shell) return false;
    UIView* view= (__bridge UIView*) reinterpret_cast<void*> (shell->winId ());
    if (view == installed) return false;
    installed= view;
    ATHENADocumentHold* hold= [ATHENADocumentHold new];
    hold->shell= shell;
    [view addGestureRecognizer:hold];
    ATHENAPencilExit* exit= [ATHENAPencilExit new];
    exit->shell= shell;
    [view addGestureRecognizer:exit];
    return false;
  }
};
}

void athena_ios_install_document_interactions (QWidget* shell) {
  new DocumentInteractions (shell);
}
