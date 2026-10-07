/******************************************************************************
* MODULE     : ios_application.mm
* DESCRIPTION: UIKit scenes and system menus over the Qt/ADS application shell
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "athena_ios.hpp"
#include "Subsystems/Qt/QTMMainTabWindow.hpp"
#include "Subsystems/Qt/QTMDocumentPersistence.hpp"
#include "string.hpp"
#include "tm_ostream.hpp"
#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QEvent>
#include <QHash>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>
#include <QUuid>
#include <QWindow>
#include <cstdio>
#include <DockWidget.h>
#import <UIKit/UIKit.h>

namespace {
QString qt (NSString* text) { return QString::fromUtf8 (text.UTF8String); }
NSString* ns (const QString& text) { return [NSString stringWithUTF8String:text.toUtf8 ().constData ()]; }
UIView* native_view (WId id) { return (__bridge UIView*) reinterpret_cast<void*> (id); }
QPointer<QTMMainTabWindow> primary;
QHash<QString, QPointer<QTMMainTabWindow>> shells;
struct PendingPane {
  QPointer<QTMMainTabWindow> origin;
  QPointer<ads::CDockWidget> pane;
};
QHash<QString, PendingPane> pending;
id<UIApplicationDelegate> qt_application_delegate ();
UIBackgroundTaskIdentifier backgroundTask= UIBackgroundTaskInvalid;

void checkpoint () {
  if (!qApp || backgroundTask != UIBackgroundTaskInvalid) return;
  backgroundTask= [UIApplication.sharedApplication beginBackgroundTaskWithName:@"Save ATHENA documents"
    expirationHandler:^{
      UIBackgroundTaskIdentifier expired= backgroundTask;
      backgroundTask= UIBackgroundTaskInvalid;
      if (expired != UIBackgroundTaskInvalid) {
        std_warning << "iPad background save time expired; pending saves will resume with ATHENA" << LF;
        [UIApplication.sharedApplication endBackgroundTask:expired];
      }
    }];
  const UIBackgroundTaskIdentifier task= backgroundTask;
  qtm_document_persistence_flush_realtime_async ([task] (bool saved) {
    if (!saved) std_warning << "Some realtime documents could not be saved before iPad suspension" << LF;
    if (backgroundTask == task && task != UIBackgroundTaskInvalid) {
      backgroundTask= UIBackgroundTaskInvalid;
      [UIApplication.sharedApplication endBackgroundTask:task];
    }
  });
}

QString actionTitle (QAction* action) {
  QString source= action->text ().section ('\t', 0, 0), title;
  for (int i= 0; i < source.size (); ++i) {
    if (source[i] == '&') {
      if (i + 1 < source.size () && source[i + 1] == '&') ++i;
      else continue;
    }
    title += source[i];
  }
  return title;
}
}

// Keep Qt's delegate and view controller implementations in charge of QPA,
// input and the run loop. Forward rather than copying or swizzling Qt internals.
@interface ATHENASceneDelegate : NSObject<UIWindowSceneDelegate> {
@public
  QPointer<QTMMainTabWindow> shell;
}
@property(nonatomic, strong) id<UIWindowSceneDelegate> downstream;
@end

@implementation ATHENASceneDelegate
- (BOOL)respondsToSelector:(SEL)selector {
  return [super respondsToSelector:selector] || [self.downstream respondsToSelector:selector];
}
- (id)forwardingTargetForSelector:(SEL)selector {
  return [self.downstream respondsToSelector:selector] ? self.downstream : [super forwardingTargetForSelector:selector];
}
- (UIWindow*)window { return self.downstream.window; }
- (void)setWindow:(UIWindow*)window { self.downstream.window= window; }
- (void)scene:(UIScene*)scene willConnectToSession:(UISceneSession*)session options:(UISceneConnectionOptions*)options {
  // UIKit may restore a persisted scene delegate without asking the app for a
  // new configuration. Resolve Qt's delegate here, not from a transient map
  // populated only by configurationForConnectingSceneSession.
  UISceneConfiguration* config= [qt_application_delegate ()
    application:UIApplication.sharedApplication
    configurationForConnectingSceneSession:session options:options];
  Class delegateClass= config.delegateClass;
  config.delegateClass= ATHENASceneDelegate.class;
  self.downstream= [[delegateClass alloc] init];
  [self.downstream scene:scene willConnectToSession:session options:options];
  if (![scene isKindOfClass:UIWindowScene.class] || !self.window) return;

  QString id= qt (session.persistentIdentifier);
  shell= shells.value (id);
  PendingPane move;
  for (NSUserActivity* activity in options.userActivities) {
    if ([activity.activityType isEqualToString:@"org.athena.workspace"]) {
      move= pending.take (qt (activity.userInfo[@"request"]));
      break;
    }
  }
  if (!shell) {
    bool primaryAssigned= primary && shells.values ().contains (primary);
    shell= !primaryAssigned && primary ? primary.data () : new QTMMainTabWindow;
    shells.insert (id, shell);
  }
  shell->setProperty ("athena.ios.scene", id);
  shell->setProperty ("athena.layoutKey", QString::fromLatin1 (
    QCryptographicHash::hash (id.toUtf8 (), QCryptographicHash::Sha256).toHex ()));
  if (move.pane) shell->adoptPane (move.pane);

  UIViewController* root= self.window.rootViewController;
  // QUIView resolves its Qt controller through the native responder chain.
  // An intermediate UIViewController breaks that lookup and Qt's root layout.
  UIView* qtView= native_view (shell->winId ());
  [root.view addSubview:qtView];
  shell->showMaximized ();
  [root.view setNeedsLayout];
  [self.window makeKeyAndVisible];
}
- (void)sceneDidBecomeActive:(UIScene*)scene {
  if ([self.downstream respondsToSelector:_cmd]) [self.downstream sceneDidBecomeActive:scene];
  if (shell) {
    shell->show ();
    shell->activateWindow ();
    QEvent activated (QEvent::WindowActivate);
    QCoreApplication::sendEvent (shell, &activated);
    [UIMenuSystem.mainSystem setNeedsRebuild];
  }
}
- (void)sceneDidEnterBackground:(UIScene*)scene {
  if (shell) shell->saveAdsLayoutState ();
  checkpoint ();
  if ([self.downstream respondsToSelector:_cmd]) [self.downstream sceneDidEnterBackground:scene];
}
- (void)sceneDidDisconnect:(UIScene*)scene {
  // Disconnection is not document close. A suspended scene may reconnect; a
  // discarded scene is handled separately by didDiscardSceneSessions.
  if (shell) {
    shell->saveAdsLayoutState ();
    shell->hide ();
    [native_view (shell->winId ()) removeFromSuperview];
  }
  checkpoint ();
  if ([self.downstream respondsToSelector:_cmd]) [self.downstream sceneDidDisconnect:scene];
}
@end

@interface ATHENAApplicationDelegate : UIResponder<UIApplicationDelegate> {
  QHash<QString, QPointer<QAction>> actions;
}
@property(nonatomic, strong) id<UIApplicationDelegate> downstream;
@end

@implementation ATHENAApplicationDelegate
- (BOOL)respondsToSelector:(SEL)selector {
  return [super respondsToSelector:selector] || [self.downstream respondsToSelector:selector];
}
- (id)forwardingTargetForSelector:(SEL)selector {
  return [self.downstream respondsToSelector:selector] ? self.downstream : [super forwardingTargetForSelector:selector];
}
- (UISceneConfiguration*)application:(UIApplication*)application configurationForConnectingSceneSession:(UISceneSession*)session options:(UISceneConnectionOptions*)options {
  UISceneConfiguration* config= [self.downstream application:application
    configurationForConnectingSceneSession:session options:options];
  if ([session.role isEqualToString:UIWindowSceneSessionRoleApplication]) {
    config.delegateClass= ATHENASceneDelegate.class;
  }
  return config;
}
- (void)application:(UIApplication*)application didDiscardSceneSessions:(NSSet<UISceneSession*>*)sessions {
  for (UISceneSession* session in sessions) {
    QPointer<QTMMainTabWindow> closing= shells.take (qt (session.persistentIdentifier));
    if (!closing) continue;
    QTMMainTabWindow* destination= nullptr;
    for (const auto& candidate: shells)
      if (candidate && candidate != closing) { destination= candidate; break; }
    if (!destination) {
      // Last window closed: retain the shell and unsaved actors for reopening.
      primary= closing;
      closing->setProperty ("athena.ios.scene", QVariant ());
      closing->hide ();
      continue;
    }
    const auto panes= closing->dockManager ()->dockWidgetsMap ();
    for (auto* pane: panes) destination->adoptPane (pane);
    if (primary == closing) primary= destination;
    QEvent activated (QEvent::WindowActivate);
    QCoreApplication::sendEvent (destination, &activated);
    closing->deleteLater ();
  }
  if ([self.downstream respondsToSelector:_cmd])
    [self.downstream application:application didDiscardSceneSessions:sessions];
}
- (void)executeAthenaCommand:(UICommand*)command {
  QPointer<QAction> action= actions.value (qt (command.propertyList));
  if (action && action->isEnabled ()) action->trigger ();
}
- (BOOL)canPerformAction:(SEL)selector withSender:(id)sender {
  if (selector == @selector(executeAthenaCommand:) && [sender isKindOfClass:UICommand.class]) {
    QPointer<QAction> action= actions.value (qt ([(UICommand*)sender propertyList]));
    return action && action->isVisible () && action->isEnabled ();
  }
  return [super canPerformAction:selector withSender:sender];
}
- (NSArray<UIMenuElement*>*)elementsForActions:(const QList<QAction*>&)source {
  NSMutableArray<UIMenuElement*>* result= [NSMutableArray new];
  NSMutableArray<UIMenuElement*>* group= [NSMutableArray new];
  auto flush= [&] {
    if (group.count) {
      [result addObject:[UIMenu menuWithTitle:@"" image:nil identifier:nil
        options:UIMenuOptionsDisplayInline children:[group copy]]];
      [group removeAllObjects];
    }
  };
  for (QAction* action: source) {
    if (!action->isVisible ()) continue;
    if (action->isSeparator ()) { flush (); continue; }
    if (QMenu* submenu= action->menu ()) {
      [group addObject:[UIMenu menuWithTitle:ns (actionTitle (action)) image:nil identifier:nil
        options:0 children:[self elementsForActions:submenu->actions ()]]];
      continue;
    }
    const QString token= QString::number (reinterpret_cast<quintptr> (action), 16);
    actions.insert (token, action);
    QKeySequence shortcut= action->property ("athena.shortcut").value<QKeySequence> ();
    UIMenuElementAttributes attributes= action->isEnabled () ? 0 : UIMenuElementAttributesDisabled;
    UIMenuElementState state= action->isChecked () ? UIMenuElementStateOn : UIMenuElementStateOff;
    if (shortcut.count () == 1) {
      QKeyCombination key= shortcut[0];
      Qt::KeyboardModifiers mods= key.keyboardModifiers ();
      UIKeyModifierFlags flags= 0;
      if (mods & Qt::ControlModifier) flags |= UIKeyModifierControl;
      if (mods & Qt::MetaModifier) flags |= UIKeyModifierCommand;
      if (mods & Qt::AltModifier) flags |= UIKeyModifierAlternate;
      if (mods & Qt::ShiftModifier) flags |= UIKeyModifierShift;
      QString input= QKeySequence (key.key ()).toString (QKeySequence::PortableText).toLower ();
      if (input.size () == 1) {
        UIKeyCommand* command= [UIKeyCommand commandWithTitle:ns (actionTitle (action)) image:nil
          action:@selector(executeAthenaCommand:) input:ns (input) modifierFlags:flags propertyList:ns (token)];
        command.attributes= attributes;
        command.state= state;
        [group addObject:command];
        continue;
      }
    }
    QPointer<QAction> guarded= action;
    UIAction* item= [UIAction actionWithTitle:ns (actionTitle (action)) image:nil identifier:ns (token)
      handler:^(UIAction*) { if (guarded && guarded->isEnabled ()) guarded->trigger (); }];
    item.attributes= attributes;
    item.state= state;
    [group addObject:item];
  }
  flush ();
  return result;
}
- (void)buildMenuWithBuilder:(id<UIMenuBuilder>)builder {
  if (builder.system != UIMenuSystem.mainSystem) return;
  QTMMainTabWindow* shell= QTMMainTabWindow::topTabWindow ();
  if (!shell) return;
  shell->prepareNativeMenus ();
  actions.clear ();
  // Keep Apple's application/window/help integration; replace only the roots
  // actually supplied by ATHENA's existing presentation registry.
  for (QAction* root: shell->menuBar ()->actions ()) {
    if (!root->isVisible () || !root->menu ()) continue;
    QString key= root->menu ()->property ("athena.menuId").toString ();
    NSString* identifier= ns ("org.athena.menu." + key);
    if (key == "file") identifier= UIMenuFile;
    else if (key == "edit") identifier= UIMenuEdit;
    else if (key == "view") identifier= UIMenuView;
    else if (key == "help") identifier= UIMenuHelp;
    UIMenu* menu= [UIMenu menuWithTitle:ns (actionTitle (root)) image:nil identifier:identifier
      options:0 children:[self elementsForActions:root->menu ()->actions ()]];
    if ([builder menuForIdentifier:identifier]) [builder replaceMenuForIdentifier:identifier withMenu:menu];
    else [builder insertChildMenu:menu atEndOfMenuForIdentifier:UIMenuRoot];
  }
}
@end

namespace {
ATHENAApplicationDelegate* applicationDelegate;

id<UIApplicationDelegate> qt_application_delegate () {
  return applicationDelegate.downstream;
}

class ScenePopupRouter: public QObject {
public:
  using QObject::QObject;
  bool eventFilter (QObject* object, QEvent* event) override {
    QWidget* widget= qobject_cast<QWidget*> (object);
    if (event->type () != QEvent::Show || !widget || !widget->isWindow () ||
        qobject_cast<QTMMainTabWindow*> (widget)) return false;
    QWindow* window= widget->windowHandle ();
    QTMMainTabWindow* shell= QTMMainTabWindow::topTabWindow ();
    if (!window || !shell) return false;
    // Qt 6.11's iOS QPA initially attaches top-level views by screen, not by
    // scene. Route dialogs/popups to their transient parent's actual UIWindow.
    QWindow* parent= window->transientParent ();
    UIView* owner= native_view (parent ? parent->winId () : shell->winId ());
    UIView* view= native_view (window->winId ());
    if (owner.window && view.window != owner.window)
      [owner.window.rootViewController.view addSubview:view];
    return false;
  }
};
}

void athena_ios_install_application_bridge () {
  applicationDelegate= [ATHENAApplicationDelegate new];
  applicationDelegate.downstream= UIApplication.sharedApplication.delegate;
  UIApplication.sharedApplication.delegate= applicationDelegate;
}

void athena_ios_register_shell (QTMMainTabWindow* shell) {
  if (!primary) primary= shell;
  // Temporary snapshot after startup, without changing the native hierarchy.
  QTimer::singleShot (5000, shell, [shell] {
    UIView* view= native_view (shell->winId ());
    std::fprintf (stderr, "ATHENA-WINDOW shell=%p visible=%d size=%dx%d native=%p window=%p frame=%s\n",
      static_cast<void*> (shell), shell->isVisible (), shell->width (), shell->height (),
      (__bridge void*) view, (__bridge void*) view.window,
      NSStringFromCGRect (view.frame).UTF8String);
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
      std::fprintf (stderr, "ATHENA-SCENE class=%s delegate=%s state=%ld\n",
        NSStringFromClass (scene.class).UTF8String,
        NSStringFromClass ([scene.delegate class]).UTF8String,
        (long) scene.activationState);
      if (![scene isKindOfClass:UIWindowScene.class]) continue;
      for (UIWindow* window in ((UIWindowScene*) scene).windows) {
        std::fprintf (stderr, "ATHENA-UIWINDOW %p class=%s hidden=%d key=%d frame=%s root=%s\n",
          (__bridge void*) window, NSStringFromClass (window.class).UTF8String,
          window.hidden, window.isKeyWindow, NSStringFromCGRect (window.frame).UTF8String,
          NSStringFromClass (window.rootViewController.class).UTF8String);
        for (UIView* child in window.rootViewController.view.subviews)
          std::fprintf (stderr, "ATHENA-UIVIEW %p class=%s hidden=%d frame=%s\n",
            (__bridge void*) child, NSStringFromClass (child.class).UTF8String,
            child.hidden, NSStringFromCGRect (child.frame).UTF8String);
      }
    }
  });
  static bool routingInstalled= false;
  if (!routingInstalled) {
    routingInstalled= true;
    qApp->setQuitOnLastWindowClosed (false);
    qApp->installEventFilter (new ScenePopupRouter (qApp));
  }
}

void athena_ios_menus_changed (QTMMainTabWindow* shell) {
  if (shell == QTMMainTabWindow::topTabWindow ())
    [UIMenuSystem.mainSystem setNeedsRebuild];
}

bool athena_ios_new_scene (QTMMainTabWindow* origin, ads::CDockWidget* pane) {
  if (!UIApplication.sharedApplication.supportsMultipleScenes) {
    std_warning << "iPadOS did not enable multiple scenes for ATHENA" << LF;
    return false;
  }
  const QString request= QUuid::createUuid ().toString (QUuid::WithoutBraces);
  pending.insert (request, {origin, pane});
  NSUserActivity* activity= [[NSUserActivity alloc] initWithActivityType:@"org.athena.workspace"];
  activity.userInfo= @{@"request": ns (request)};
  activity.title= pane ? ns (pane->windowTitle ()) : @"ATHENA";
  [UIApplication.sharedApplication requestSceneSessionActivation:nil userActivity:activity options:nil
    errorHandler:^(NSError* error) {
      pending.remove (request);
      std_warning << "Could not open iPad window: " << string (error.localizedDescription.UTF8String) << LF;
    }];
  return true;
}

bool athena_ios_can_return_pane (QTMMainTabWindow* shell) {
  for (const auto& candidate: shells)
    if (candidate && candidate != shell) return true;
  return false;
}

bool athena_ios_return_pane (QTMMainTabWindow* shell, QWidget* widget) {
  if (!shell || !widget) return false;
  ads::CDockWidget* pane= nullptr;
  for (auto* dock: shell->dockManager ()->dockWidgetsMap ())
    if (dock->widget () == widget) { pane= dock; break; }
  if (!pane) return false;
  QTMMainTabWindow* destination= primary != shell ? primary.data () : nullptr;
  if (!destination)
    for (const auto& candidate: shells)
      if (candidate && candidate != shell) { destination= candidate; break; }
  if (!destination) return false;
  destination->adoptPane (pane);
  NSString* id= ns (destination->property ("athena.ios.scene").toString ());
  for (UISceneSession* session in UIApplication.sharedApplication.openSessions)
    if ([session.persistentIdentifier isEqualToString:id]) {
      [UIApplication.sharedApplication requestSceneSessionActivation:session userActivity:nil options:nil errorHandler:nil];
      break;
    }
  return true;
}

void athena_ios_close_scene (QTMMainTabWindow* shell) {
  NSString* id= ns (shell->property ("athena.ios.scene").toString ());
  for (UISceneSession* session in UIApplication.sharedApplication.openSessions)
    if ([session.persistentIdentifier isEqualToString:id]) {
      [UIApplication.sharedApplication requestSceneSessionDestruction:session options:nil
        errorHandler:^(NSError* error) {
          std_warning << "Could not close iPad window: " << string (error.localizedDescription.UTF8String) << LF;
        }];
      return;
    }
}
