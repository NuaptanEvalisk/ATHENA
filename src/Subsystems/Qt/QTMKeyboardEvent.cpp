/******************************************************************************
* MODULE     : QTMKeyboardEvent.cpp
* DESCRIPTION: Qt TeXmacs keyboard event handling class, that converts
*              Qt key events into TeXmacs key combinations.
* COPYRIGHT  : (C) 2024 Massimiliano Gubinelli, Grégoire Lecerf, Liza Belos
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMKeyboardEvent.hpp"

#include <QApplication>
#include <QLocale>
#include <QVector>
#include "analyze.hpp"
#include "basic.hpp"

inline bool inputMethodIsNotAnyTerritory() {
  return QApplication::inputMethod()->locale().territory()
         != QLocale::AnyTerritory;
}

void QTMKeyboardEvent::printDebugInformations() const {
  debug_qt << "keypressed" << LF;
  int key = mEvent.key();
  debug_qt << "key  : " << key << LF;
  debug_qt << "text : " << mEvent.text().toUtf8().data() << LF;
  debug_qt << "count: " << mEvent.text().size() << LF;
  QVector<uint> ucs4= mEvent.text().toUcs4 ();
  debug_qt << "unic : " << (ucs4.size () == 0 ? 0 : ucs4[0]) << LF;

#ifdef OS_MINGW
  debug_qt << "nativeScanCode: " << mEvent.nativeScanCode() << LF; 
  debug_qt << "nativeVirtualKey: " << mEvent.nativeVirtualKey() << LF;
  debug_qt << "nativeModifiers: " << mEvent.nativeModifiers() << LF;
#endif
  if (isShift()) debug_qt << "shift" << LF;
  if (isMeta()) debug_qt << "meta" << LF;
  if (isControl()) debug_qt << "control" << LF;
  if (isKeyPad()) debug_qt << "keypad" << LF;
  if (isAlt()) debug_qt << "alt" << LF;
}

void QTMKeyboardEvent::patchForMingw() {
#ifdef OS_MINGW
  /* "Qt::Key_AltGr On Windows, when the KeyDown event for this key is sent,
  * the Ctrl+Alt modifiers are also set." (excerpt from Qt doc)
  * However the AltGr key is used to obtain many symbols 
  * which should not be regarded as C-A- shortcuts.
  * (e.g. \ or @ on a French keyboard) 
  * 
  * Hence, when "native modifiers" are (ControlLeft | AltRight) 
  * we clear Qt's Ctrl+Alt modifiers
  */
  quint32 controlAlt = ControlLeft | AltRight;
  if ((mEvent.nativeModifiers() & controlAlt) == controlAlt) {
    if (DEBUG_QT && DEBUG_KEYBOARD) {
      debug_qt << "assuming it's an AltGr key code" << LF;
    }
    removeAlt();
    removeControl();
  }
#endif
}

bool QTMKeyboardEvent::patchForShift() {
  bool shifted = unic < 32 && isAscii();
#ifdef Q_OS_WIN
  shifted = (shifted && unic > 0) 
          || (unic > 0 && unic < 255 && mKey > 32 && isShift() && isControl());
#endif

  if (!shifted) {
    return false;
  }
  // NOTE: For some reason, the 'shift' modifier key is not applied
  // to 'key' when 'control' is pressed as well.  We perform some
  // dirty hacking to figure out the right shifted variant of a key
  // by ourselves...
  bool upcase = is_upcase((char) mKey);

  if (upcase && !isShift()) {
      mKey= (int) locase ((char) mKey);
  }

  if (!upcase && mKeyboard.hasShiftPreference (kc) && isShift() && isControl()) {
    string pref= mKeyboard.getShiftPreference (kc);
    if (N(pref) > 0) {
      mTexmacsKeyCombination= pref;
      removeShift ();
      return true;
    }
    if (DEBUG_QT && DEBUG_KEYBOARD) {
      debug_qt << "Control+Shift " << kc << " -> " << mKey << LF;
    }
  }
  removeShift();
  mTexmacsKeyCombination= string ((char) mKey);
  return true;
  
}

void QTMKeyboardEvent::computeUnicodeText() {
  // Actual dead keys are handled by qtdeadmap, not inferred from typed text.
  QByteArray buf= nss.toUtf8 ();
  mTexmacsKeyCombination= string (buf.constData (), buf.size ());
}

void QTMKeyboardEvent::patchForMac() {
  bool modifiersAlt = isAlt();
  modifiersAlt = modifiersAlt && inputMethodIsNotAnyTerritory();
  if (!modifiersAlt) {
    return;
  }

  bool mustAlter = mKey >= 32 && mKey < 128 && (
                      N(mTexmacsKeyCombination) != 1 
                      || ((int) (unsigned char) mTexmacsKeyCombination[0]) < 32 
                      || ((int) (unsigned char) mTexmacsKeyCombination[0]) >= 128
                   );
  // todo : check commit 14560
  if (mustAlter) {
    if (!isShift() && isUpcase()) {
      toLower();
    }
    mKeyboard.composemap()(mKey) = mTexmacsKeyCombination;
    mTexmacsKeyCombination= string ((char) mKey);
  }
  else {
    removeAlt();
  }
}

void
QTMKeyboardEvent::handleKeyboardByTexmacs() {
    // We need to use text(): Alt-{5,6,7,8,9} are []|{} under MacOS, etc.
  nss = mEvent.text();
  kc  = mEvent.nativeVirtualKey();
  QVector<uint> ucs4= nss.toUcs4 ();
  if (ucs4.size() == 0) {
    unic = 0;
  } else {
    unic= ucs4[0];
  }

  if (unic > 32 &&
      isShift() && !isControl() && !isAlt() && !isMeta()) {
    const QByteArray bytes= nss.toUtf8 ();
    mKeyboard.setShiftPreference (kc, string (bytes.constData (), bytes.size ()));
  }

  if (patchForShift()) {
    return;
  }

  computeUnicodeText();

#ifdef Q_OS_MAC
  patchForMac();
#endif

  removeShift();
      
}

void QTMKeyboardEvent::computeModifiers() {
  if (isShift()) {
    mTexmacsKeyCombination= "S-" * mTexmacsKeyCombination;
  }
#if defined(Q_OS_MAC)
  if (inputMethodIsNotAnyTerritory()) {
    if (isAlt()) {
      mTexmacsKeyCombination= "A-" * mTexmacsKeyCombination;
    }
  }
#else
  if (isAlt()) {
    mTexmacsKeyCombination= "A-" * mTexmacsKeyCombination;
  }
#endif
  //if (isKeyPad()) mTexmacsKeyCombination= "K-" * mTexmacsKeyCombination;
#ifdef Q_OS_MAC
  if (isMeta()) {
    // The "Control" key
    mTexmacsKeyCombination= "C-" * mTexmacsKeyCombination;
  }
  if (isControl()) {
    // The "Command" key
    mTexmacsKeyCombination= "M-" * mTexmacsKeyCombination;
  }
#else
  if (isControl()) {
    mTexmacsKeyCombination= "C-" * mTexmacsKeyCombination;
  }
  if (isMeta()) {
    // The "Windows" key
    mTexmacsKeyCombination= "M-" * mTexmacsKeyCombination;
  }
#endif
}
    

void QTMKeyboardEvent::handleKeyboardEvent() {
  if (DEBUG_QT && DEBUG_KEYBOARD) {
    printDebugInformations();
  }

  patchForMingw();

  if (!handleQtKeyAny()) {
    handleKeyboardByTexmacs();
  }

  if (mTexmacsKeyCombination == "") return;

  computeModifiers();
}


bool QTMKeyboardEvent::handleQtKeyAny () {
    if (handleQtKeyMap ()) {
        return true;
    }
    return handleQtDeadMap ();
}

bool QTMKeyboardEvent::handleQtKeyMap () {
  if (!mKeyboard.keymap()->contains (mKey)) {
    return false;
  }
  mTexmacsKeyCombination = mKeyboard.keymap()[mKey];
#if defined(OS_MINGW)
  // e.g. azerty keyboard: AltGr tilde followed by Space
  if (mKey == 32 && mEvent.text() != " ") {
    QByteArray buf = mEvent.text().toUtf8();
    mTexmacsKeyCombination = string (buf.data(), buf.size());
  }
#endif
  return true;
}

bool QTMKeyboardEvent::handleQtDeadMap () {
  if (!mKeyboard.deadmap()->contains(mKey)) {
    return false;
  }
  removeShift();
  mTexmacsKeyCombination = mKeyboard.deadmap()[mKey];
  return true;
}
