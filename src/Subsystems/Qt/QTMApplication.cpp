#include "QTMApplication.hpp"
#include "QTMCommandPalette.hpp"
#include "QTMCommandRegistry.hpp"
#include "QTMDocumentPersistence.hpp"
#include "QTMDocumentHistory.hpp"
#include "QTMProgressWindow.hpp"
#include "QTMUpdateChecker.hpp"
#include "QTMVaultBackupDispatcher.hpp"
#include "qt_utilities.hpp"
#include "tm_timer.hpp"

#include <QKeyEvent>
#include <QTouchEvent>
#include <QNativeGestureEvent>
  
QTMApplication::QTMApplication (int& argc, char** argv) :
  QApplication (argc, argv), mStartupWindow (nullptr) { }

namespace {

static string
gestureEventTypeName (QEvent::Type type) {
  if (type == QEvent::TouchBegin) return "TouchBegin";
  if (type == QEvent::TouchUpdate) return "TouchUpdate";
  if (type == QEvent::TouchEnd) return "TouchEnd";
  if (type == QEvent::TouchCancel) return "TouchCancel";
  if (type == QEvent::NativeGesture) return "NativeGesture";
  if (type == QEvent::Gesture) return "Gesture";
  if (type == QEvent::GestureOverride) return "GestureOverride";
  return "type_" * as_string ((int) type);
}

static string
nativeGestureTypeName (Qt::NativeGestureType type) {
  if (type == Qt::BeginNativeGesture) return "BeginNativeGesture";
  if (type == Qt::EndNativeGesture) return "EndNativeGesture";
  if (type == Qt::ZoomNativeGesture) return "ZoomNativeGesture";
  if (type == Qt::RotateNativeGesture) return "RotateNativeGesture";
  if (type == Qt::SwipeNativeGesture) return "SwipeNativeGesture";
  if (type == Qt::SmartZoomNativeGesture) return "SmartZoomNativeGesture";
  return "native-type_" * as_string ((int) type);
}

static bool gestureDebugEnabled () {
  QByteArray value= qgetenv ("ATHENA_GESTURE_DEBUG");
  return !value.isEmpty () && value != "0";
}

static QKeySequence
eventKeySequence (const QKeyEvent* event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return QKeySequence (event->keyCombination ());
#else
  return QKeySequence (event->key () | int (event->modifiers ()));
#endif
}

static const QTMCommandDefinition*
nativeShortcutCommand (const QKeyEvent* event) {
  if (QApplication::activeModalWidget () != nullptr) return nullptr;
  QTMCommandRegistry& registry= QTMCommandRegistry::instance ();
  if (!registry.initialize ()) return nullptr;
  return registry.commandForShortcut (eventKeySequence (event));
}

static QTMCommandContext
nativeShortcutContext (QObject* receiver) {
  QTMCommandRegistry& registry= QTMCommandRegistry::instance ();
  QTMMainTabWindow* shell= QTMMainTabWindow::topTabWindow ();
  return registry.captureContext (shell, qobject_cast<QWidget*> (receiver));
}

static bool
dispatchApplicationShortcut (QObject* receiver, QEvent* event) {
  if (event == nullptr ||
      (event->type () != QEvent::ShortcutOverride &&
       event->type () != QEvent::KeyPress))
    return false;

  QKeyEvent* keyEvent= static_cast<QKeyEvent*> (event);
  const QTMCommandDefinition* command= nativeShortcutCommand (keyEvent);
  if (command == nullptr || command->scope != QTMCommandScope::Application)
    return false;

  QTMCommandRegistry& registry= QTMCommandRegistry::instance ();
  QTMCommandContext context= nativeShortcutContext (receiver);
  QTMCommandState state= registry.state (command->id, context);
  if (!state.available || !state.enabled) return false;
  if (event->type () == QEvent::ShortcutOverride) {
    event->accept ();
    return true;
  }
  (void) registry.execute (command->id, context);
  event->accept ();
  return true;
}

}

void QTMApplication::show_splash () {
  if (headless_mode) return;
  if (mStartupWindow != nullptr) return;
  mStartupWindow= new QTMProgressWindow ("Starting ATHENA");
  set_splash_progress (2, "Preparing application");
  mStartupWindow->show ();
  mStartupWindow->centerOnScreen ();
  mStartupWindow->raise ();
  mStartupWindow->activateWindow ();
  qApp->processEvents (QEventLoop::AllEvents);
}

void QTMApplication::set_splash_progress (int progress, string message) {
  if (mStartupWindow == nullptr) return;
  mStartupWindow->setMessage (to_qstring (message));
  mStartupWindow->setProgress (progress);
  mStartupWindow->repaint ();
  qApp->processEvents (QEventLoop::ExcludeUserInputEvents);
}

void QTMApplication::set_splash_busy (string message) {
  if (mStartupWindow == nullptr) return;
  mStartupWindow->setMessage (to_qstring (message));
  mStartupWindow->setBusy (true);
  mStartupWindow->repaint ();
  qApp->processEvents (QEventLoop::ExcludeUserInputEvents);
}

void QTMApplication::hide_splash () {
  if (mStartupWindow != nullptr) {
    mStartupWindow->hide ();
    delete mStartupWindow;
    mStartupWindow= nullptr;
  }
}

void QTMApplication::load() {
  mUseNewToolbar = get_user_preference ("new toolbar") != "off";

  mPixmapManagerInitialized = false;

  bench_start ("initialize qt theme");
  init_theme ();
  bench_cumul ("initialize qt theme");

  bench_start ("construct qt tab shell");
  // UIKit can connect the first Scene while startup processes splash events.
  // Reuse that Scene's shell instead of covering it with a second window.
  if (QTMMainTabWindow::topTabWindow () == nullptr)
    new QTMMainTabWindow ();
  bench_cumul ("construct qt tab shell");
  bench_start ("initialize native commands");
  (void) QTMCommandRegistry::instance ().initialize ();
  bench_cumul ("initialize native commands");
  bench_start ("initialize background services");
  qtm_document_persistence_initialize ();
  qtm_document_history_initialize ();
  qtm_vault_backup_dispatcher_initialize ();
  qtm_schedule_update_check ();
  bench_cumul ("initialize background services");
}
  

void QTMApplication::init_theme () {
#if defined(OS_MINGW64)
  setStyle(QStyleFactory::create("Windows"));
#endif
  tm_style_sheet= "$ATHENA_PATH/misc/themes/native-light.css";

  init_palette (this);
  init_style_sheet (this);
}

void QTMApplication::set_window_icon (string icon_path) {
  url icon_url= url_system (get_env ("ATHENA_PATH") * icon_path);
  if (exists (icon_url)) {
    const c_string _icon (as_string (icon_url));
    setWindowIcon (QIcon ((const char*) _icon));
  }
  else {
    std_warning << "Could not find TeXmacs icon file: " << as_string (icon_url) << LF;
  }
}

bool QTMApplication::notify (QObject* receiver, QEvent* event)
{
  try {
    // Application commands win before pane-specific input-owner exemptions.
    // Modal dialogs remain an explicit boundary through nativeShortcutCommand.
    if (dispatchApplicationShortcut (receiver, event)) return true;
    if (event && (event->type () == QEvent::KeyPress ||
                  event->type () == QEvent::KeyRelease ||
                  event->type () == QEvent::ShortcutOverride)) {
      for (auto* widget= qobject_cast<QWidget*> (receiver); widget; widget= widget->parentWidget ())
        if (widget->property ("athenaOwnsKeyInput").toBool ()) {
          if (event->type () == QEvent::ShortcutOverride) { event->accept (); return true; }
          return QApplication::notify (receiver, event);
        }
    }
    qtm_vault_backup_dispatcher_note_activity (event);
    if (receiver != NULL && event != NULL &&
        event->type () == QEvent::ShortcutOverride) {
      QKeyEvent* keyEvent= static_cast<QKeyEvent*> (event);
      const QTMCommandDefinition* command= nativeShortcutCommand (keyEvent);
      if (command != nullptr) {
        QTMCommandState state= QTMCommandRegistry::instance ().state (
          command->id, nativeShortcutContext (receiver));
        if (!state.available) return QApplication::notify (receiver, event);
        event->accept ();
        return true;
      }
    }

    if (receiver != NULL && event != NULL &&
        event->type () == QEvent::KeyPress) {
      QKeyEvent* keyEvent= static_cast<QKeyEvent*> (event);
      Qt::KeyboardModifiers modifiers= keyEvent->modifiers ();
      bool closePaneShortcut=
        keyEvent->key () == Qt::Key_W &&
        (modifiers & Qt::ControlModifier) != 0 &&
        (modifiers & (Qt::ShiftModifier | Qt::AltModifier |
                      Qt::MetaModifier)) == 0;
      if (closePaneShortcut && qtm_close_focused_ads_tool_pane (
            qobject_cast<QWidget*> (receiver))) {
        event->accept ();
        return true;
      }
      const QTMCommandDefinition* command= nativeShortcutCommand (keyEvent);
      if (command != nullptr) {
        QTMCommandRegistry& registry= QTMCommandRegistry::instance ();
        QTMCommandContext context= nativeShortcutContext (receiver);
        QTMCommandState state= registry.state (command->id, context);
        if (!state.available) return QApplication::notify (receiver, event);
        (void) registry.execute (command->id, context);
        event->accept ();
        return true;
      }
    }

    if (receiver != NULL && event != NULL &&
        (event->type () == QEvent::Polish ||
         event->type () == QEvent::Show ||
         event->type () == QEvent::FontChange))
      qt_sync_wayland_logical_widget_font (qobject_cast<QWidget*> (receiver));

    if (gestureDebugEnabled () && event != NULL && (
          event->type () == QEvent::TouchBegin ||
          event->type () == QEvent::TouchUpdate ||
          event->type () == QEvent::TouchEnd ||
          event->type () == QEvent::TouchCancel ||
          event->type () == QEvent::NativeGesture ||
          event->type () == QEvent::Gesture ||
          event->type () == QEvent::GestureOverride)) {
      string receiver_name = "(null)";
      string receiver_class = "(null)";
      if (receiver != NULL) {
        if (!receiver->objectName().isEmpty ())
          receiver_name = from_qstring (receiver->objectName ());
        if (receiver->metaObject() != NULL)
          receiver_class= receiver->metaObject()->className ();
      }

      cout << "[gesture-app] type=" << gestureEventTypeName (event->type ())
           << " receiver=" << receiver_class << "/" << receiver_name
           << " accepted=" << (event->isAccepted () ? "yes" : "no");

      if (event->type () == QEvent::NativeGesture) {
        QNativeGestureEvent* native_event = static_cast<QNativeGestureEvent*> (event);
        cout << " native-type="
             << nativeGestureTypeName (native_event->gestureType ())
             << " fingers=" << native_event->fingerCount ()
             << " value=" << as_string ((double) native_event->value ())
             << " delta=" << as_string ((double) native_event->delta ().x ())
             << "," << as_string ((double) native_event->delta ().y ());
      }
      if (event->type () == QEvent::TouchBegin ||
          event->type () == QEvent::TouchUpdate ||
          event->type () == QEvent::TouchEnd ||
          event->type () == QEvent::TouchCancel) {
        QTouchEvent* touch_event = static_cast<QTouchEvent*> (event);
        cout << " points=" << touch_event->points ().size ();
      }
      cout << LF;
    }
    return QApplication::notify (receiver, event);
  }
  catch (string s) {
    //c_string cs (s);
    //tm_failure (cs);
    //qt_error << "Thrown " << s << LF;
    the_exception= s;
  }
  return false;
}
