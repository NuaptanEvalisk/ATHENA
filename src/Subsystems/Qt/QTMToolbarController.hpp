/******************************************************************************
* MODULE     : QTMToolbarController.hpp
* DESCRIPTION: Main window toolbar layout and hover reveal policy
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#ifndef QTMTOOLBARCONTROLLER_HPP
#define QTMTOOLBARCONTROLLER_HPP

#include <QObject>
#include <QPointer>
#include <QTimer>

class QAction;
class QLabel;
class QMainWindow;
class QToolBar;
class QWidget;

class QTMToolbarController: public QObject {
public:
  QTMToolbarController (QMainWindow* window, QToolBar* mainToolbar,
                        QToolBar* modeToolbar, QToolBar* focusToolbar,
                        QToolBar* userToolbar);

  void setRequestedVisibility (bool mainVisible, bool modeVisible,
                               bool focusVisible, bool userVisible);
  void refreshPreference ();
  void setAutoHideEnabled (bool enabled);
  void refreshLayout ();
  void expand ();
  void collapse ();

  bool isCollapsed () const { return collapsed; }
  bool isModeMerged () const { return modeMerged; }
  QToolBar* revealToolbar () const { return reveal; }
  QWidget* overlayWidget () const { return overlay; }

protected:
  bool eventFilter (QObject* watched, QEvent* event) override;

private:
  void applyVisibility ();
  void scheduleCollapseCheck ();
  bool pointerOverToolbar () const;
  void ensureJoinSeparator ();
  void enterOverlayMode ();
  void leaveOverlayMode ();
  void positionOverlay ();
  int positionOverlayToolbar (QToolBar* toolbar, int y, int width);

private:
  QPointer<QMainWindow> window;
  QPointer<QToolBar> mainToolbar;
  QPointer<QToolBar> modeToolbar;
  QPointer<QToolBar> focusToolbar;
  QPointer<QToolBar> userToolbar;
  QPointer<QToolBar> reveal;
  QPointer<QWidget> overlay;
  QPointer<QLabel> dots;
  QPointer<QAction> joinSeparator;
  QTimer collapseTimer;
  bool requestedMain= false;
  bool requestedMode= false;
  bool requestedFocus= false;
  bool requestedUser= false;
  bool autoHide= false;
  bool collapsed= false;
  bool modeMerged= false;
  bool overlayMode= false;
  bool applying= false;
};

#endif // QTMTOOLBARCONTROLLER_HPP
