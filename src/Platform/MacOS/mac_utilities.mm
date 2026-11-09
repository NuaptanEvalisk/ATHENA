/******************************************************************************
 * MODULE     : mac_utilities.mm
 * DESCRIPTION: Cocoa related utilites (also for TeXmacs/Qt)
 * COPYRIGHT  : (C) 2010  Massimiliano Gubinelli
 *******************************************************************************
 * This software falls under the GNU general public license version 3 or later.
 * It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
 * in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
 ******************************************************************************/

#include "boot.hpp"
#include "url.hpp"
#include "Platform/MacOS/mac_utilities.h"
#include "tm_timer.hpp"
#include "gui.hpp"
#include "analyze.hpp"
#include "hashset.hpp"
#include "iterator.hpp"

#undef FAILED // redefined by CARBON
#define extend CARBON_extends // avoid name collision
#include "Platform/MacOS/mac_cocoa.h"
#include <Carbon/Carbon.h>
#include <crt_externs.h>
#undef extend

#undef FAILED // restore TeXmacs definition
#ifdef USE_EXCEPTIONS
#define FAILED(msg) { tm_throw (msg); }
#else
#ifdef DEBUG_ASSERT
#define FAILED(msg) { tm_failure (msg); assert (false); }
#else
#define FAILED(msg) { tm_failure (msg); }
#endif
#endif

#ifdef QTTEXMACS
#include <QApplication>
#include <QKeyEvent>
#include <QString>
#include "UI/Qt/Widgets/QTMWidget.hpp"
#include "UI/Qt/Toolkit/qt_gui.hpp"
#include "UI/Qt/Toolkit/qt_utilities.hpp"
#endif

#if __MAC_OS_X_VERSION_MIN_REQUIRED >= 1012
#define NSKeyDown NSEventTypeKeyDown
#define NSKeyUp NSEventTypeKeyUp
#define NSControlKeyMask NSEventModifierFlagControl
#define NSAlternateKeyMask NSEventModifierFlagOption
#define NSCommandKeyMask NSEventModifierFlagCommand
#define NSKeyDownMask NSEventMaskKeyDown
#define NSKeyUpMask NSEventMaskKeyUp
#endif

bool 
mac_alternate_startup () {
#if __MAC_OS_X_VERSION_MIN_REQUIRED >= 1060
  NSUInteger nsmods = [NSEvent modifierFlags];
  return (nsmods &  NSAlternateKeyMask);
#else
  return ((CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState) 
           & NSDeviceIndependentModifierFlagsMask) == kCGEventFlagMaskAlternate);
#endif
}


#ifdef QTTEXMACS
#if 1
//HACK:
// the following code fixes a bug in Qt/Cocoa which do not correctly handle
// Ctrl+Tab key combination. In particular no QKeyDown event is generated for
// Shift+Tab and Ctrl+Tab. For this reason we intercept the event at the Cocoa
// level and just perform manually the translation to equivalent Qt event which
// is then sent directly to the focused widget. 
// It is rather simplistic approach but seems to work.
// Since it is an hack, the filter is  installed  only if we link againts the 
// bugged version of Qt.
// This filter is installed in qt_gui.cpp
// To use the API we need to compile in ObjC 2.0 since blocks are required

NSEvent *
mac_handler_body (NSEvent *event) {
  if (([event type] == NSKeyDown) || ([event type] == NSKeyUp)) {
    NSString *nss = [event charactersIgnoringModifiers];
    if ([nss length] > 0) {
      unichar key = [nss characterAtIndex:0];
      if ((key == NSTabCharacter) || (key == NSBackTabCharacter) ) {
        NSUInteger nsmods = [event modifierFlags];
        Qt::KeyboardModifiers modifs = Qt::NoModifier;
        if (key == NSBackTabCharacter) modifs |= Qt::ShiftModifier;
        if (nsmods &  NSControlKeyMask) modifs |= Qt::MetaModifier;
        if (nsmods &  NSAlternateKeyMask) modifs |= Qt::AltModifier;
        if (nsmods &  NSCommandKeyMask) modifs |= Qt::ControlModifier;

        if (!headless_mode) {
          QKeyEvent *qe = new QKeyEvent(([event type] == NSKeyDown) ? 
                                        QEvent::KeyPress : QEvent::KeyRelease, 
                                        Qt::Key_Tab, modifs);
          QApplication::postEvent(qApp->focusWidget(), qe);
	}
        return nil;
      }
      if (key == 0x0051 || key == 0x0071) {
        NSUInteger nsmods = [event modifierFlags];
        Qt::KeyboardModifiers modifs = Qt::NoModifier;
        if (key == NSBackTabCharacter) modifs |= Qt::ShiftModifier;
        if (nsmods &  NSControlKeyMask) modifs |= Qt::MetaModifier;
        if (nsmods &  NSAlternateKeyMask) modifs |= Qt::AltModifier;
        if (nsmods &  NSCommandKeyMask) modifs |= Qt::ControlModifier;
        if (nsmods & NSCommandKeyMask) {
          if (!headless_mode) {
            QKeyEvent *qe = new QKeyEvent(([event type] == NSKeyDown) ? 
                                          QEvent::KeyPress : QEvent::KeyRelease, 
                                          Qt::Key_Q, modifs);
            QApplication::postEvent(qApp->focusWidget(), qe);
	  }
          return nil;
        }
      }
    }
  }
  return event;
}
  
void 
mac_install_filter () {
#if NS_BLOCKS_AVAILABLE
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  
  NSEvent * (^mac_handler)(NSEvent * ) = ^ (NSEvent *event) {
    return mac_handler_body(event);
  };
  [NSEvent addLocalMonitorForEventsMatchingMask: NSKeyDownMask | NSKeyUpMask 
                                        handler:mac_handler];
  [pool release];
  
#endif
}

#endif
#endif

// scale factor detection
// from http://src.chromium.org/svn/trunk/src/ui/gfx/screen_mac.mm

#if !defined(MAC_OS_X_VERSION_10_7) || \
MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_7

@interface NSScreen (LionAPI)
- (CGFloat)backingScaleFactor;
@end

#endif


// end scale factor detection

/* A bug in OSX Yosemite inserts duplicate entries in the environment. This
 affects child processes: in particular, the PATH is not properly inherited
 unless we remove the duplicates and most plugins fail to start (since they are
 indirectly invoked through tm_* scripts.
 
 This code is adapted from Joe Cheng's https://github.com/jcheng5/envmunge
 */
void
mac_fix_yosemite_bug() {
#if MAC_OS_X_VERSION_MAX_ALLOWED > MAC_OS_X_VERSION_10_9
#if defined (MAC_OS_X_VERSION_10_10)
  hashset<string> entries;
  hashset<string> duplicates;
  
    // Find duplicate entries in the environment
  for (char** entry = *_NSGetEnviron(); *entry; ++entry) {
    array<string> pair = tokenize (string (*entry), "=");
    if (N(pair) < 2) continue;
    string key = pair[0];
    if (entries->contains(key)) duplicates->insert(key);
    else                        entries->insert(key);
  }
    // Remove duplicate entries
  iterator<string> it = iterate (duplicates);
  while (it->busy()) {
    c_string name (it->next());
    const char* val = getenv (name); // remember value (first one on the list?)
    if (val != NULL) {
      unsetenv (name);       // removes all instances of name
      setenv (name, val, 0); // restore name=val
    }
  }
#endif
#endif
}


static id background_activity= nil;

void
mac_begin_server () {
#if MAC_OS_X_VERSION_MAX_ALLOWED > MAC_OS_X_VERSION_10_8
#if defined (MAC_OS_X_VERSION_10_9)
  if (background_activity == nil) {
    id background_activity = [[NSProcessInfo processInfo]
                               beginActivityWithOptions: NSActivityBackground
                              reason: @"TeXmacs server running"];
    [background_activity retain];
  }
#endif
#endif
}

void
mac_end_server () {
#if MAC_OS_X_VERSION_MAX_ALLOWED > MAC_OS_X_VERSION_10_8
#if defined (MAC_OS_X_VERSION_10_9)
  if (background_activity) {
    [[NSProcessInfo processInfo] endActivity: background_activity];
    [background_activity release];
  }
#endif
#endif
}
