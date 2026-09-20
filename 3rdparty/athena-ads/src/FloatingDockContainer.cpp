/*******************************************************************************
 ** Qt Advanced Docking System
 ** Copyright (C) 2017 Uwe Kindler
 **
 ** This library is free software; you can redistribute it and/or
 ** modify it under the terms of the GNU Lesser General Public
 ** License as published by the Free Software Foundation; either
 ** version 2.1 of the License, or (at your option) any later version.
 **
 ** This library is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 ** Lesser General Public License for more details.
 **
 ** You should have received a copy of the GNU Lesser General Public
 ** License along with this library; If not, see <http://www.gnu.org/licenses/>.
 ******************************************************************************/

//============================================================================
/// \file   FloatingDockContainer.cpp
/// \author Uwe Kindler
/// \date   01.03.2017
/// \brief  Implementation of CFloatingDockContainer class
//============================================================================

//============================================================================
//                                   INCLUDES
//============================================================================
#include "FloatingDockContainer.h"

#include <iostream>

#include <QBoxLayout>
#include <QApplication>
#include <QMouseEvent>
#include <QPointer>
#include <QAction>
#include <QDebug>
#include <QAbstractButton>
#include <QElapsedTimer>
#include <QTime>
#include <QScreen>
#include <QTimer>
#include <sstream>
#include <fstream>
#include <string>
#include <QWindow>
#include <QVector>
#include <QMimeData>
#include <QDropEvent>
#include <QDragLeaveEvent>
#include <QDragEnterEvent>
#include <QDrag>
#include <QDataStream>
#include <QByteArray>

#include "DockContainerWidget.h"
#include "DockAreaWidget.h"
#include "DockManager.h"
#include "DockWidget.h"
#include "DockOverlay.h"

#ifdef Q_OS_WIN
#include <windows.h>
#ifdef _MSC_VER
#pragma comment(lib, "User32.lib")
#endif
#endif
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
#include "linux/FloatingWidgetTitleBar.h"
#include <xcb/xcb.h>
#endif

namespace ads
{
static QWidget*
athenaFloatingContainerParent(CDockManager* dockManager)
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS) && (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	if (QApplication::platformName().startsWith(QStringLiteral("wayland")))
	{
		return nullptr;
	}
#endif
	return dockManager;
}

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
static void
athenaSnapFloatingContainerToScreenEdge(QWidget* widget)
{
    if (widget == nullptr || !widget->isVisible())
    {
        return;
    }

    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (screen == nullptr)
    {
        screen = widget->screen();
    }
    if (screen == nullptr)
    {
        return;
    }

    QRect area = screen->availableGeometry();
    QPoint cursor = QCursor::pos();
    int threshold = qMax(24, area.width() / 80);
    QRect target;
    if (qAbs(cursor.x() - area.left()) <= threshold)
    {
        target = QRect(area.left(), area.top(),
                       area.width() / 2, area.height());
    }
    else if (qAbs(cursor.x() - area.right()) <= threshold)
    {
        target = QRect(area.left() + area.width() / 2, area.top(),
                       area.width() - area.width() / 2, area.height());
    }
    else
    {
        return;
    }

    widget->setGeometry(target);
}
#endif

#ifdef Q_OS_WIN
#if 0 // set to 1 if you need this function for debugging
/**
 * Just for debugging to convert windows message identifiers to strings
 */
static const char* windowsMessageString(int MessageId)
{
	switch (MessageId)
	{
    case 0: return "WM_NULL";
    case 1: return "WM_CREATE";
    case 2: return "WM_DESTROY";
    case 3: return "WM_MOVE";
    case 5: return "WM_SIZE";
    case 6: return "WM_ACTIVATE";
    case 7: return "WM_SETFOCUS";
    case 8: return "WM_KILLFOCUS";
    case 10: return "WM_ENABLE";
    case 11: return "WM_SETREDRAW";
    case 12: return "WM_SETTEXT";
    case 13: return "WM_GETTEXT";
    case 14: return "WM_GETTEXTLENGTH";
    case 15: return "WM_PAINT";
    case 16: return "WM_CLOSE";
    case 17: return "WM_QUERYENDSESSION";
    case 18: return "WM_QUIT";
    case 19: return "WM_QUERYOPEN";
    case 20: return "WM_ERASEBKGND";
    case 21: return "WM_SYSCOLORCHANGE";
    case 22: return "WM_ENDSESSION";
    case 24: return "WM_SHOWWINDOW";
    case 25: return "WM_CTLCOLOR";
    case 26: return "WM_WININICHANGE";
    case 27: return "WM_DEVMODECHANGE";
    case 28: return "WM_ACTIVATEAPP";
    case 29: return "WM_FONTCHANGE";
    case 30: return "WM_TIMECHANGE";
    case 31: return "WM_CANCELMODE";
    case 32: return "WM_SETCURSOR";
    case 33: return "WM_MOUSEACTIVATE";
    case 34: return "WM_CHILDACTIVATE";
    case 35: return "WM_QUEUESYNC";
    case 36: return "WM_GETMINMAXINFO";
    case 38: return "WM_PAINTICON";
    case 39: return "WM_ICONERASEBKGND";
    case 40: return "WM_NEXTDLGCTL";
    case 42: return "WM_SPOOLERSTATUS";
    case 43: return "WM_DRAWITEM";
    case 44: return "WM_MEASUREITEM";
    case 45: return "WM_DELETEITEM";
    case 46: return "WM_VKEYTOITEM";
    case 47: return "WM_CHARTOITEM";
    case 48: return "WM_SETFONT";
    case 49: return "WM_GETFONT";
    case 50: return "WM_SETHOTKEY";
    case 51: return "WM_GETHOTKEY";
    case 55: return "WM_QUERYDRAGICON";
    case 57: return "WM_COMPAREITEM";
    case 61: return "WM_GETOBJECT";
    case 65: return "WM_COMPACTING";
    case 68: return "WM_COMMNOTIFY";
    case 70: return "WM_WINDOWPOSCHANGING";
    case 71: return "WM_WINDOWPOSCHANGED";
    case 72: return "WM_POWER";
    case 73: return "WM_COPYGLOBALDATA";
    case 74: return "WM_COPYDATA";
    case 75: return "WM_CANCELJOURNAL";
    case 78: return "WM_NOTIFY";
    case 80: return "WM_INPUTLANGCHANGEREQUEST";
    case 81: return "WM_INPUTLANGCHANGE";
    case 82: return "WM_TCARD";
    case 83: return "WM_HELP";
    case 84: return "WM_USERCHANGED";
    case 85: return "WM_NOTIFYFORMAT";
    case 123: return "WM_CONTEXTMENU";
    case 124: return "WM_STYLECHANGING";
    case 125: return "WM_STYLECHANGED";
    case 126: return "WM_DISPLAYCHANGE";
    case 127: return "WM_GETICON";
    case 128: return "WM_SETICON";
    case 129: return "WM_NCCREATE";
    case 130: return "WM_NCDESTROY";
    case 131: return "WM_NCCALCSIZE";
    case 132: return "WM_NCHITTEST";
    case 133: return "WM_NCPAINT";
    case 134: return "WM_NCACTIVATE";
    case 135: return "WM_GETDLGCODE";
    case 136: return "WM_SYNCPAINT";
    case 160: return "WM_NCMOUSEMOVE";
    case 161: return "WM_NCLBUTTONDOWN";
    case 162: return "WM_NCLBUTTONUP";
    case 163: return "WM_NCLBUTTONDBLCLK";
    case 164: return "WM_NCRBUTTONDOWN";
    case 165: return "WM_NCRBUTTONUP";
    case 166: return "WM_NCRBUTTONDBLCLK";
    case 167: return "WM_NCMBUTTONDOWN";
    case 168: return "WM_NCMBUTTONUP";
    case 169: return "WM_NCMBUTTONDBLCLK";
    case 171: return "WM_NCXBUTTONDOWN";
    case 172: return "WM_NCXBUTTONUP";
    case 173: return "WM_NCXBUTTONDBLCLK";
    case 176: return "EM_GETSEL";
    case 177: return "EM_SETSEL";
    case 178: return "EM_GETRECT";
    case 179: return "EM_SETRECT";
    case 180: return "EM_SETRECTNP";
    case 181: return "EM_SCROLL";
    case 182: return "EM_LINESCROLL";
    case 183: return "EM_SCROLLCARET";
    case 185: return "EM_GETMODIFY";
    case 187: return "EM_SETMODIFY";
    case 188: return "EM_GETLINECOUNT";
    case 189: return "EM_LINEINDEX";
    case 190: return "EM_SETHANDLE";
    case 191: return "EM_GETHANDLE";
    case 192: return "EM_GETTHUMB";
    case 193: return "EM_LINELENGTH";
    case 194: return "EM_REPLACESEL";
    case 195: return "EM_SETFONT";
    case 196: return "EM_GETLINE";
    case 197: return "EM_LIMITTEXT / EM_SETLIMITTEXT";
    case 198: return "EM_CANUNDO";
    case 199: return "EM_UNDO";
    case 200: return "EM_FMTLINES";
    case 201: return "EM_LINEFROMCHAR";
    case 202: return "EM_SETWORDBREAK";
    case 203: return "EM_SETTABSTOPS";
    case 204: return "EM_SETPASSWORDCHAR";
    case 205: return "EM_EMPTYUNDOBUFFER";
    case 206: return "EM_GETFIRSTVISIBLELINE";
    case 207: return "EM_SETREADONLY";
    case 209: return "EM_SETWORDBREAKPROC / EM_GETWORDBREAKPROC";
    case 210: return "EM_GETPASSWORDCHAR";
    case 211: return "EM_SETMARGINS";
    case 212: return "EM_GETMARGINS";
    case 213: return "EM_GETLIMITTEXT";
    case 214: return "EM_POSFROMCHAR";
    case 215: return "EM_CHARFROMPOS";
    case 216: return "EM_SETIMESTATUS";
    case 217: return "EM_GETIMESTATUS";
    case 224: return "SBM_SETPOS";
    case 225: return "SBM_GETPOS";
    case 226: return "SBM_SETRANGE";
    case 227: return "SBM_GETRANGE";
    case 228: return "SBM_ENABLE_ARROWS";
    case 230: return "SBM_SETRANGEREDRAW";
    case 233: return "SBM_SETSCROLLINFO";
    case 234: return "SBM_GETSCROLLINFO";
    case 235: return "SBM_GETSCROLLBARINFO";
    case 240: return "BM_GETCHECK";
    case 241: return "BM_SETCHECK";
    case 242: return "BM_GETSTATE";
    case 243: return "BM_SETSTATE";
    case 244: return "BM_SETSTYLE";
    case 245: return "BM_CLICK";
    case 246: return "BM_GETIMAGE";
    case 247: return "BM_SETIMAGE";
    case 248: return "BM_SETDONTCLICK";
    case 255: return "WM_INPUT";
    case 256: return "WM_KEYDOWN";
    case 257: return "WM_KEYUP";
    case 258: return "WM_CHAR";
    case 259: return "WM_DEADCHAR";
    case 260: return "WM_SYSKEYDOWN";
    case 261: return "WM_SYSKEYUP";
    case 262: return "WM_SYSCHAR";
    case 263: return "WM_SYSDEADCHAR";
    case 265: return "WM_UNICHAR / WM_WNT_CONVERTREQUESTEX";
    case 266: return "WM_CONVERTREQUEST";
    case 267: return "WM_CONVERTRESULT";
    case 268: return "WM_INTERIM";
    case 269: return "WM_IME_STARTCOMPOSITION";
    case 270: return "WM_IME_ENDCOMPOSITION";
    case 272: return "WM_INITDIALOG";
    case 273: return "WM_COMMAND";
    case 274: return "WM_SYSCOMMAND";
    case 275: return "WM_TIMER";
    case 276: return "WM_HSCROLL";
    case 277: return "WM_VSCROLL";
    case 278: return "WM_INITMENU";
    case 279: return "WM_INITMENUPOPUP";
    case 280: return "WM_SYSTIMER";
    case 287: return "WM_MENUSELECT";
    case 288: return "WM_MENUCHAR";
    case 289: return "WM_ENTERIDLE";
    case 290: return "WM_MENURBUTTONUP";
    case 291: return "WM_MENUDRAG";
    case 292: return "WM_MENUGETOBJECT";
    case 293: return "WM_UNINITMENUPOPUP";
    case 294: return "WM_MENUCOMMAND";
    case 295: return "WM_CHANGEUISTATE";
    case 296: return "WM_UPDATEUISTATE";
    case 297: return "WM_QUERYUISTATE";
    case 306: return "WM_CTLCOLORMSGBOX";
    case 307: return "WM_CTLCOLOREDIT";
    case 308: return "WM_CTLCOLORLISTBOX";
    case 309: return "WM_CTLCOLORBTN";
    case 310: return "WM_CTLCOLORDLG";
    case 311: return "WM_CTLCOLORSCROLLBAR";
    case 312: return "WM_CTLCOLORSTATIC";
    case 512: return "WM_MOUSEMOVE";
    case 513: return "WM_LBUTTONDOWN";
    case 514: return "WM_LBUTTONUP";
    case 515: return "WM_LBUTTONDBLCLK";
    case 516: return "WM_RBUTTONDOWN";
    case 517: return "WM_RBUTTONUP";
    case 518: return "WM_RBUTTONDBLCLK";
    case 519: return "WM_MBUTTONDOWN";
    case 520: return "WM_MBUTTONUP";
    case 521: return "WM_MBUTTONDBLCLK";
    case 522: return "WM_MOUSEWHEEL";
    case 523: return "WM_XBUTTONDOWN";
    case 524: return "WM_XBUTTONUP";
    case 525: return "WM_XBUTTONDBLCLK";
    case 528: return "WM_PARENTNOTIFY";
    case 529: return "WM_ENTERMENULOOP";
    case 530: return "WM_EXITMENULOOP";
    case 531: return "WM_NEXTMENU";
    case 532: return "WM_SIZING";
    case 533: return "WM_CAPTURECHANGED";
    case 534: return "WM_MOVING";
    case 536: return "WM_POWERBROADCAST";
    case 537: return "WM_DEVICECHANGE";
    case 544: return "WM_MDICREATE";
    case 545: return "WM_MDIDESTROY";
    case 546: return "WM_MDIACTIVATE";
    case 547: return "WM_MDIRESTORE";
    case 548: return "WM_MDINEXT";
    case 549: return "WM_MDIMAXIMIZE";
    case 550: return "WM_MDITILE";
    case 551: return "WM_MDICASCADE";
    case 552: return "WM_MDIICONARRANGE";
    case 553: return "WM_MDIGETACTIVE";
    case 560: return "WM_MDISETMENU";
    case 561: return "WM_ENTERSIZEMOVE";
    case 562: return "WM_EXITSIZEMOVE";
    case 563: return "WM_DROPFILES";
    case 564: return "WM_MDIREFRESHMENU";
    case 640: return "WM_IME_REPORT";
    case 641: return "WM_IME_SETCONTEXT";
    case 642: return "WM_IME_NOTIFY";
    case 643: return "WM_IME_CONTROL";
    case 644: return "WM_IME_COMPOSITIONFULL";
    case 645: return "WM_IME_SELECT";
    case 646: return "WM_IME_CHAR";
    case 648: return "WM_IME_REQUEST";
    case 656: return "WM_IME_KEYDOWN";
    case 657: return "WM_IME_KEYUP";
    case 672: return "WM_NCMOUSEHOVER";
    case 673: return "WM_MOUSEHOVER";
    case 674: return "WM_NCMOUSELEAVE";
    case 675: return "WM_MOUSELEAVE";
    case 768: return "WM_CUT";
    case 769: return "WM_COPY";
    case 770: return "WM_PASTE";
    case 771: return "WM_CLEAR";
    case 772: return "WM_UNDO";
    case 773: return "WM_RENDERFORMAT";
    case 774: return "WM_RENDERALLFORMATS";
    case 775: return "WM_DESTROYCLIPBOARD";
    case 776: return "WM_DRAWCLIPBOARD";
    case 777: return "WM_PAINTCLIPBOARD";
    case 778: return "WM_VSCROLLCLIPBOARD";
    case 779: return "WM_SIZECLIPBOARD";
    case 780: return "WM_ASKCBFORMATNAME";
    case 781: return "WM_CHANGECBCHAIN";
    case 782: return "WM_HSCROLLCLIPBOARD";
    case 783: return "WM_QUERYNEWPALETTE";
    case 784: return "WM_PALETTEISCHANGING";
    case 785: return "WM_PALETTECHANGED";
    case 786: return "WM_HOTKEY";
    case 791: return "WM_PRINT";
    case 792: return "WM_PRINTCLIENT";
    case 793: return "WM_APPCOMMAND";
    case 856: return "WM_HANDHELDFIRST";
    case 863: return "WM_HANDHELDLAST";
    case 864: return "WM_AFXFIRST";
    case 895: return "WM_AFXLAST";
    case 896: return "WM_PENWINFIRST";
    case 897: return "WM_RCRESULT";
    case 898: return "WM_HOOKRCRESULT";
    case 899: return "WM_GLOBALRCCHANGE / WM_PENMISCINFO";
    case 900: return "WM_SKB";
    case 901: return "WM_HEDITCTL / WM_PENCTL";
    case 902: return "WM_PENMISC";
    case 903: return "WM_CTLINIT";
    case 904: return "WM_PENEVENT";
    case 911: return "WM_PENWINLAST";
    default:
    	return "unknown WM_ message";
	}

	return "unknown WM_ message";
}
#endif
#endif


#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
static const char* const AthenaAdsFloatingDockMime = "application/x-athena-ads-floating-dock";
static const char* const AthenaQtMainWindowDragWindowMime = "application/x-qt-mainwindowdrag-window";
static const char* const AthenaQtMainWindowDragPositionMime = "application/x-qt-mainwindowdrag-position";

static QPointer<CFloatingDockContainer> AthenaActiveWaylandDockDrag;

void athenaSetWaylandDockOverlayCursorPosition(const QPoint& globalPos);
void athenaClearWaylandDockOverlayCursorPosition();

struct AthenaDropTargetState
{
	QPointer<QWidget> Widget;
	bool AcceptDrops = false;
};

static QVector<AthenaDropTargetState> AthenaDropTargetStates;

static bool
athenaIsNativeWaylandPlatform()
{
	return QApplication::platformName().startsWith(QStringLiteral("wayland"));
}

static bool
athenaAdsWaylandDebugEnabled()
{
	static const bool enabled = qEnvironmentVariableIsSet("ATHENA_SCALE_DEBUG")
	                         || qEnvironmentVariableIsSet("ATHENA_ADS_WAYLAND_DEBUG");
	return enabled;
}

static void
athenaAdsGiantLog(const std::string& line)
{
	static bool initialized = false;
	std::ofstream out;
	if (!initialized)
	{
		out.open("/tmp/athena-ads-giant-rendering.log",
		         std::ios::out | std::ios::trunc);
		initialized = true;
		out << "ATHENA_GIANT_LOG start" << std::endl;
	}
	else
	{
		out.open("/tmp/athena-ads-giant-rendering.log",
		         std::ios::out | std::ios::app);
	}
	out << line << std::endl;
}

static void
athenaAdsDockLog(const std::string& line, bool reset = false)
{
	std::ofstream out;
	out.open("/tmp/athena-ads-wayland-dock.log",
	         reset ? std::ios::out : (std::ios::out | std::ios::app));
	out << line << std::endl;
	if (athenaAdsWaylandDebugEnabled())
	{
		std::cerr << line << std::endl;
	}
}

static std::string
athenaAdsSizeText(const QSize& size)
{
	return std::to_string(size.width()) + "x" + std::to_string(size.height());
}

static std::string
athenaAdsRectText(const QRect& rect)
{
	return std::to_string(rect.x()) + "," + std::to_string(rect.y())
	     + " " + std::to_string(rect.width()) + "x" + std::to_string(rect.height());
}

static const char*
athenaDockAreaName(DockWidgetArea area)
{
	switch (area)
	{
	case TopDockWidgetArea: return "Top";
	case RightDockWidgetArea: return "Right";
	case BottomDockWidgetArea: return "Bottom";
	case LeftDockWidgetArea: return "Left";
	case CenterDockWidgetArea: return "Center";
	case LeftAutoHideArea: return "LeftAutoHide";
	case RightAutoHideArea: return "RightAutoHide";
	case TopAutoHideArea: return "TopAutoHide";
	case BottomAutoHideArea: return "BottomAutoHide";
	case InvalidDockWidgetArea: return "Invalid";
	default: return "Other";
	}
}

static const char*
athenaEventTypeName(QEvent::Type type)
{
	switch (type)
	{
	case QEvent::DragEnter: return "DragEnter";
	case QEvent::DragMove: return "DragMove";
	case QEvent::DragLeave: return "DragLeave";
	case QEvent::Drop: return "Drop";
	default: return "Other";
	}
}

static void
athenaLogWindowSurface(const char* label, QWidget* widget)
{
	QWindow* window = widget == nullptr ? nullptr : widget->windowHandle();
	QScreen* screen = window == nullptr ? nullptr : window->screen();
	std::ostringstream line;
	line << "ATHENA_GIANT " << label
	     << " widget=" << widget
	     << " widgetClass=" << (widget == nullptr ? "" : widget->metaObject()->className())
	     << " object=" << (widget == nullptr ? std::string() : widget->objectName().toStdString())
	     << " title=" << (widget == nullptr ? std::string() : widget->windowTitle().toStdString())
	     << " isWindow=" << (widget != nullptr && widget->isWindow())
	     << " visible=" << (widget != nullptr && widget->isVisible())
	     << " active=" << (widget != nullptr && widget->isActiveWindow())
	     << " size=" << athenaAdsSizeText(widget == nullptr ? QSize() : widget->size())
	     << " geometry=" << athenaAdsRectText(widget == nullptr ? QRect() : widget->geometry())
	     << " frame=" << athenaAdsRectText(widget == nullptr ? QRect() : widget->frameGeometry())
	     << " minimum=" << athenaAdsSizeText(widget == nullptr ? QSize() : widget->minimumSize())
	     << " minimumHint=" << athenaAdsSizeText(widget == nullptr ? QSize() : widget->minimumSizeHint())
	     << " maximum=" << athenaAdsSizeText(widget == nullptr ? QSize() : widget->maximumSize())
	     << " sizeHint=" << athenaAdsSizeText(widget == nullptr ? QSize() : widget->sizeHint())
	     << " sizePolicy=" << (widget == nullptr ? -1 : int(widget->sizePolicy().horizontalPolicy()))
	     << "," << (widget == nullptr ? -1 : int(widget->sizePolicy().verticalPolicy()))
	     << " windowFlags=" << (widget == nullptr ? 0 : quint64(widget->windowFlags()))
	     << " windowState=" << (widget == nullptr ? 0 : int(widget->windowState()))
	     << " widgetDpr=" << (widget == nullptr ? 0.0 : widget->devicePixelRatioF())
	     << " win=" << window
	     << " winVisible=" << (window != nullptr && window->isVisible())
	     << " winSize=" << athenaAdsSizeText(window == nullptr ? QSize() : window->size())
	     << " winGeometry=" << athenaAdsRectText(window == nullptr ? QRect() : window->geometry())
	     << " winMinimum=" << athenaAdsSizeText(window == nullptr ? QSize() : window->minimumSize())
	     << " winMaximum=" << athenaAdsSizeText(window == nullptr ? QSize() : window->maximumSize())
	     << " winFlags=" << (window == nullptr ? 0 : quint64(window->flags()))
	     << " winState=" << (window == nullptr ? 0 : int(window->windowState()))
	     << " winDpr=" << (window == nullptr ? 0.0 : window->devicePixelRatio())
	     << " screenGeometry=" << athenaAdsRectText(screen == nullptr ? QRect() : screen->geometry())
	     << " screenAvailable=" << athenaAdsRectText(screen == nullptr ? QRect() : screen->availableGeometry());
	athenaAdsGiantLog(line.str());
	if (athenaAdsWaylandDebugEnabled())
	{
		std::cerr << line.str() << std::endl;
	}
}

static void
athenaLogTopLevelWindows(const char* label)
{
	const auto windows = QGuiApplication::topLevelWindows();
	{
		std::ostringstream line;
		line << "ATHENA_GIANT " << label
		     << " topLevelWindowCount=" << windows.size()
		     << " topLevelWidgetCount=" << QApplication::topLevelWidgets().size();
		athenaAdsGiantLog(line.str());
		if (athenaAdsWaylandDebugEnabled())
		{
			std::cerr << line.str() << std::endl;
		}
	}
	for (QWindow* window : windows)
	{
		std::ostringstream line;
		line << "ATHENA_GIANT top-level-window"
		     << " label=" << label
		     << " win=" << window
		     << " class=" << (window == nullptr ? "" : window->metaObject()->className())
		     << " title=" << (window == nullptr ? std::string() : window->title().toStdString())
		     << " visible=" << (window != nullptr && window->isVisible())
		     << " size=" << athenaAdsSizeText(window == nullptr ? QSize() : window->size())
		     << " geometry=" << athenaAdsRectText(window == nullptr ? QRect() : window->geometry())
		     << " dpr=" << (window == nullptr ? 0.0 : window->devicePixelRatio());
		athenaAdsGiantLog(line.str());
		if (athenaAdsWaylandDebugEnabled())
		{
			std::cerr << line.str() << std::endl;
		}
	}
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		athenaLogWindowSurface("top-level-widget", widget);
	}
}

static QByteArray
athenaDataStreamPayload(qintptr value)
{
	QByteArray data;
	QDataStream stream(&data, QIODevice::WriteOnly);
	stream << value;
	return data;
}

static QByteArray
athenaDataStreamPayload(const QPoint& value)
{
	QByteArray data;
	QDataStream stream(&data, QIODevice::WriteOnly);
	stream << value;
	return data;
}

static void
athenaRememberDropTarget(QWidget* widget)
{
	if (widget == nullptr)
	{
		return;
	}
	for (const auto& state : AthenaDropTargetStates)
	{
		if (state.Widget == widget)
		{
			return;
		}
	}
	AthenaDropTargetStates.push_back({widget, widget->acceptDrops()});
	widget->setAcceptDrops(true);
}

static void
athenaRememberDropTargetTree(QWidget* widget)
{
	if (widget == nullptr)
	{
		return;
	}
	athenaRememberDropTarget(widget);
	const auto children = widget->findChildren<QWidget*>();
	for (QWidget* child : children)
	{
		athenaRememberDropTarget(child);
	}
}

static void
athenaPrepareWaylandDockDropTargets(CDockManager* dockManager,
                                    CFloatingDockContainer* activeFloatingContainer)
{
	AthenaDropTargetStates.clear();
	if (dockManager != nullptr)
	{
		for (CDockContainerWidget* container : dockManager->dockContainers())
		{
			if (activeFloatingContainer != nullptr
			    && container == activeFloatingContainer->dockContainer())
			{
				continue;
			}
			athenaRememberDropTargetTree(container);
			athenaRememberDropTargetTree(container->window());
		}
	}
}

static void
athenaRestoreWaylandDockDropTargets()
{
	for (const auto& state : AthenaDropTargetStates)
	{
		if (state.Widget)
		{
			state.Widget->setAcceptDrops(state.AcceptDrops);
		}
	}
	AthenaDropTargetStates.clear();
}

static const QMimeData*
athenaDockDragMimeData(QEvent* event)
{
	switch (event->type())
	{
	case QEvent::DragEnter:
		return static_cast<QDragEnterEvent*>(event)->mimeData();
	case QEvent::DragMove:
		return static_cast<QDragMoveEvent*>(event)->mimeData();
	case QEvent::Drop:
		return static_cast<QDropEvent*>(event)->mimeData();
	default:
		return nullptr;
	}
}

static QPoint
athenaDockDragEventPosition(QEvent* event)
{
	switch (event->type())
	{
	case QEvent::DragEnter:
		return static_cast<QDragEnterEvent*>(event)->position().toPoint();
	case QEvent::DragMove:
		return static_cast<QDragMoveEvent*>(event)->position().toPoint();
	case QEvent::Drop:
		return static_cast<QDropEvent*>(event)->position().toPoint();
	default:
		return QCursor::pos();
	}
}

static QPoint
athenaDockDragGlobalPosition(QObject* target, QEvent* event)
{
	const QPoint localPos = athenaDockDragEventPosition(event);
	const QPoint cursorPos = QCursor::pos();
	QPoint globalPos = cursorPos;
	const char* source = "cursor-fallback";

	if (QWidget* widget = qobject_cast<QWidget*>(target))
	{
		globalPos = widget->mapToGlobal(localPos);
		source = "widget";
		std::ostringstream line;
		line << "ATHENA_DOCK_POS"
		     << " event=" << athenaEventTypeName(event->type())
		     << " source=" << source
		     << " target=" << target
		     << " class=" << widget->metaObject()->className()
		     << " object=" << widget->objectName().toStdString()
		     << " local=" << localPos.x() << "," << localPos.y()
		     << " global=" << globalPos.x() << "," << globalPos.y()
		     << " cursor=" << cursorPos.x() << "," << cursorPos.y()
		     << " widgetGeom=" << athenaAdsRectText(widget->geometry())
		     << " widgetFrame=" << athenaAdsRectText(widget->frameGeometry())
		     << " windowGeom=" << athenaAdsRectText(widget->window()->geometry())
		     << " windowFrame=" << athenaAdsRectText(widget->window()->frameGeometry());
		athenaAdsDockLog(line.str());
		return globalPos;
	}

	if (QWindow* window = qobject_cast<QWindow*>(target))
	{
		globalPos = window->mapToGlobal(localPos);
		source = "window";
		std::ostringstream line;
		line << "ATHENA_DOCK_POS"
		     << " event=" << athenaEventTypeName(event->type())
		     << " source=" << source
		     << " target=" << target
		     << " class=" << window->metaObject()->className()
		     << " title=" << window->title().toStdString()
		     << " local=" << localPos.x() << "," << localPos.y()
		     << " global=" << globalPos.x() << "," << globalPos.y()
		     << " cursor=" << cursorPos.x() << "," << cursorPos.y()
		     << " winGeom=" << athenaAdsRectText(window->geometry())
		     << " winSize=" << athenaAdsSizeText(window->size())
		     << " dpr=" << window->devicePixelRatio();
		athenaAdsDockLog(line.str());
		return globalPos;
	}

	std::ostringstream line;
	line << "ATHENA_DOCK_POS"
	     << " event=" << athenaEventTypeName(event->type())
	     << " source=" << source
	     << " target=" << target
	     << " targetClass=" << (target == nullptr ? "" : target->metaObject()->className())
	     << " local=" << localPos.x() << "," << localPos.y()
	     << " global=" << globalPos.x() << "," << globalPos.y()
	     << " cursor=" << cursorPos.x() << "," << cursorPos.y();
	athenaAdsDockLog(line.str());
	return globalPos;
}

class AthenaAdsWaylandDockDragFilter : public QObject
{
public:
	bool eventFilter(QObject* target, QEvent* event) override
	{
		if (!AthenaActiveWaylandDockDrag)
		{
			return QObject::eventFilter(target, event);
		}

		if (athenaAdsWaylandDebugEnabled())
		{
			const QEvent::Type type = event->type();
			if (type == QEvent::Show || type == QEvent::Hide || type == QEvent::Resize
			    || type == QEvent::Move || type == QEvent::Expose || type == QEvent::Paint)
			{
				if (auto* window = qobject_cast<QWindow*>(target))
				{
					qDebug() << "ATHENA_ADS_WAYLAND event-window"
					         << "type" << int(type)
					         << "target" << window
					         << "class" << window->metaObject()->className()
					         << "title" << window->title()
					         << "visible" << window->isVisible()
					         << "size" << window->size()
					         << "geometry" << window->geometry()
					         << "dpr" << window->devicePixelRatio();
				}
				else if (auto* widget = qobject_cast<QWidget*>(target))
				{
					if (widget->isWindow() || widget->window() == AthenaActiveWaylandDockDrag)
					{
						qDebug() << "ATHENA_ADS_WAYLAND event-widget"
						         << "type" << int(type)
						         << "target" << widget
						         << "class" << widget->metaObject()->className()
						         << "object" << widget->objectName()
						         << "title" << widget->windowTitle()
						         << "visible" << widget->isVisible()
						         << "size" << widget->size()
						         << "geometry" << widget->geometry()
						         << "frame" << widget->frameGeometry();
					}
				}
			}
		}

		if (event->type() == QEvent::DragLeave)
		{
			AthenaActiveWaylandDockDrag->athenaHideWaylandDockOverlays();
			return QObject::eventFilter(target, event);
		}

		const QMimeData* mimeData = athenaDockDragMimeData(event);
		if (mimeData == nullptr || !mimeData->hasFormat(AthenaAdsFloatingDockMime))
		{
			return QObject::eventFilter(target, event);
		}

		const QPoint globalPos = athenaDockDragGlobalPosition(target, event);
		{
			std::ostringstream line;
			line << "ATHENA_DOCK_EVENT"
			     << " event=" << athenaEventTypeName(event->type())
			     << " target=" << target
			     << " targetClass=" << (target == nullptr ? "" : target->metaObject()->className())
			     << " global=" << globalPos.x() << "," << globalPos.y()
			     << " cursor=" << QCursor::pos().x() << "," << QCursor::pos().y();
			athenaAdsDockLog(line.str());
		}
		AthenaActiveWaylandDockDrag->athenaUpdateWaylandDockDrag(globalPos);

		switch (event->type())
		{
		case QEvent::DragEnter:
		{
			auto* dragEvent = static_cast<QDragEnterEvent*>(event);
			dragEvent->setDropAction(Qt::MoveAction);
			dragEvent->accept();
			return true;
		}
		case QEvent::DragMove:
		{
			auto* dragEvent = static_cast<QDragMoveEvent*>(event);
			dragEvent->setDropAction(Qt::MoveAction);
			dragEvent->accept();
			return true;
		}
		case QEvent::Drop:
		{
			auto* dropEvent = static_cast<QDropEvent*>(event);
			const bool docked = AthenaActiveWaylandDockDrag->athenaFinishWaylandDockDrag(true);
			if (docked)
			{
				dropEvent->setDropAction(Qt::MoveAction);
				dropEvent->accept();
			}
			else
			{
				dropEvent->ignore();
			}
			return true;
		}
		default:
			break;
		}
		return QObject::eventFilter(target, event);
	}
};

static AthenaAdsWaylandDockDragFilter*
athenaWaylandDockDragFilter()
{
	static auto* filter = new AthenaAdsWaylandDockDragFilter();
	return filter;
}
#endif


static unsigned int zOrderCounterFloating = 0;
/**
 * Private data class of CFloatingDockContainer class (pimpl)
 */
struct FloatingDockContainerPrivate
{
	CFloatingDockContainer *_this;
	CDockContainerWidget *DockContainer;
	unsigned int zOrderIndex = ++zOrderCounterFloating;
	QPointer<CDockManager> DockManager;
	eDragState DraggingState = DraggingInactive;
	bool AthenaWaylandDockDragStarted = false;
	QPoint DragStartMousePosition;
	QPointer<CDockContainerWidget> DropContainer;
	QPoint AthenaWaylandLastGlobalPos;
	bool AthenaWaylandHasLastGlobalPos = false;
	CDockAreaWidget *SingleDockArea = nullptr;
	QPoint DragStartPos;
	bool Hiding = false;
	bool AutoHideChildren = true;
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    QWidget* MouseEventHandler = nullptr;
    CFloatingWidgetTitleBar* TitleBar = nullptr;
	bool IsResizing = false;
    bool MousePressed = false;
#endif

	/**
	 * Private data constructor
	 */
	FloatingDockContainerPrivate(CFloatingDockContainer *_public);

	void titleMouseReleaseEvent();
	void updateDropOverlays(const QPoint &GlobalPos);

	/**
	 * Returns true if the given config flag is set
	 */
	static bool testConfigFlag(CDockManager::eConfigFlag Flag)
	{
		return CDockManager::testConfigFlag(Flag);
	}

	/**
	 * Tests is a certain state is active
	 */
	bool isState(eDragState StateId) const
	{
		return StateId == DraggingState;
	}

	/**
	 * Sets the dragging state and posts a FloatingWidgetDragStartEvent
	 * if dragging starts
	 */
	void setState(eDragState StateId)
	{
		if (DraggingState == StateId)
		{
			return;
		}

		DraggingState = StateId;
        if (DraggingFloatingWidget == DraggingState)
        {
            qApp->postEvent(_this, new QEvent((QEvent::Type)internal::FloatingWidgetDragStartEvent));
        }
	}

	void setWindowTitle(const QString &Text)
	{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
		if (TitleBar)
		{
			TitleBar->setTitle(Text);
		}
#endif
		_this->setWindowTitle(Text);
	}

	/**
	 * Reflect the current dock widget title in the floating widget windowTitle()
	 * depending on the CDockManager::FloatingContainerHasWidgetTitle flag
	 */
	void reflectCurrentWidget(CDockWidget* CurrentWidget)
	{
		// reflect CurrentWidget's title if configured to do so, otherwise display application name as window title
		if (testConfigFlag(CDockManager::FloatingContainerHasWidgetTitle))
		{
			setWindowTitle(CurrentWidget->windowTitle());
		}
		else
		{
			setWindowTitle(floatingContainersTitle());
		}

		// reflect CurrentWidget's icon if configured to do so, otherwise display application icon as window icon
		QIcon CurrentWidgetIcon = CurrentWidget->icon();
		if (testConfigFlag(CDockManager::FloatingContainerHasWidgetIcon)
				&& !CurrentWidgetIcon.isNull())
		{
			_this->setWindowIcon(CurrentWidget->icon());
		}
		else
		{
			_this->setWindowIcon(QApplication::windowIcon());
		}
	}

	/**
	 * Handles escape key press when dragging around the floating widget
	 */
	void handleEscapeKey();

	/**
	 * Returns the title used by all FloatingContainer that does not
	 * reflect the title of the current dock widget.
	 *
	 * If not title was set with CDockManager::setFloatingContainersTitle(),
	 * it returns QGuiApplication::applicationDisplayName().
	 */
	static QString floatingContainersTitle()
	{
		return CDockManager::floatingContainersTitle();
	}
};
// struct FloatingDockContainerPrivate

//============================================================================
FloatingDockContainerPrivate::FloatingDockContainerPrivate(
    CFloatingDockContainer *_public) :
	_this(_public)
{

}

//============================================================================
void FloatingDockContainerPrivate::titleMouseReleaseEvent()
{
	setState(DraggingInactive);
	if (!DropContainer)
	{
		return;
	}

	if (DockManager->dockAreaOverlay()->dropAreaUnderCursor() != InvalidDockWidgetArea
	 || DockManager->containerOverlay()->dropAreaUnderCursor() != InvalidDockWidgetArea)
	{
		CDockOverlay *Overlay = DockManager->containerOverlay();
		if (!Overlay->dropOverlayRect().isValid())
		{
			Overlay = DockManager->dockAreaOverlay();
		}

		// Do not resize if we drop into an autohide sidebar area to preserve
		// the dock area size for the initial size of the auto hide area
		if (!ads::internal::isSideBarArea(Overlay->dropAreaUnderCursor()))
		{
			// Resize the floating widget to the size of the highlighted drop area
			// rectangle
			QRect Rect = Overlay->dropOverlayRect();
			int FrameWidth = (_this->frameSize().width() - _this->rect().width())
				/ 2;
			int TitleBarHeight = _this->frameSize().height()
				- _this->rect().height() - FrameWidth;
			if (Rect.isValid())
			{
				QPoint TopLeft = Overlay->mapToGlobal(Rect.topLeft());
				TopLeft.ry() += TitleBarHeight;
				_this->setGeometry(
					QRect(TopLeft,
						QSize(Rect.width(), Rect.height() - TitleBarHeight)));
				QApplication::processEvents();
			}
		}
		DropContainer->dropFloatingWidget(_this, QCursor::pos());
	}

	DockManager->containerOverlay()->hideOverlay();
	DockManager->dockAreaOverlay()->hideOverlay();
}


//============================================================================
void FloatingDockContainerPrivate::updateDropOverlays(const QPoint &GlobalPos)
{
	if (!_this->isVisible() || !DockManager)
	{
		return;
	}

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
	// Prevent display of drop overlays and docking as long as a model dialog
	// is active
    if (qApp->activeModalWidget())
    {
        return;
    }
#endif

	auto Containers = DockManager->dockContainers();
	CDockContainerWidget *TopContainer = nullptr;
	for (auto ContainerWidget : Containers)
	{
		if (!ContainerWidget->isVisible())
		{
			continue;
		}

		if (DockContainer == ContainerWidget)
		{
			continue;
		}

		QPoint MappedPos = ContainerWidget->mapFromGlobal(GlobalPos);
		if (ContainerWidget->rect().contains(MappedPos))
		{
			if (!TopContainer || ContainerWidget->isInFrontOf(TopContainer))
			{
				TopContainer = ContainerWidget;
			}
		}
	}

	DropContainer = TopContainer;
	auto ContainerOverlay = DockManager->containerOverlay();
	auto DockAreaOverlay = DockManager->dockAreaOverlay();

	if (!TopContainer)
	{
		ContainerOverlay->hideOverlay();
		DockAreaOverlay->hideOverlay();
		return;
	}

	int VisibleDockAreas = TopContainer->visibleDockAreaCount();
	DockWidgetAreas AllowedContainerAreas = (VisibleDockAreas > 1) ? OuterDockAreas : AllDockAreas;
	auto DockArea = TopContainer->dockAreaAt(GlobalPos);
	// If the dock container contains only one single DockArea, then we need
	// to respect the allowed areas - only the center area is relevant here because
	// all other allowed areas are from the container
	if (VisibleDockAreas == 1 && DockArea)
	{
		AllowedContainerAreas.setFlag(CenterDockWidgetArea, DockArea->allowedAreas().testFlag(CenterDockWidgetArea));
	}

	if (DockContainer->features().testFlag(CDockWidget::DockWidgetPinnable))
	{
		AllowedContainerAreas |= AutoHideDockAreas;
	}

	ContainerOverlay->setAllowedAreas(AllowedContainerAreas);

	DockWidgetArea ContainerArea = ContainerOverlay->showOverlay(TopContainer);
	ContainerOverlay->enableDropPreview(ContainerArea != InvalidDockWidgetArea);
	if (DockArea && DockArea->isVisible() && VisibleDockAreas > 0)
	{
		DockAreaOverlay->enableDropPreview(true);
		DockAreaOverlay->setAllowedAreas(
		    (VisibleDockAreas == 1) ? NoDockWidgetArea : DockArea->allowedAreas());
		DockWidgetArea Area = DockAreaOverlay->showOverlay(DockArea);

		// A CenterDockWidgetArea for the dockAreaOverlay() indicates that
		// the mouse is in the title bar. If the ContainerArea is valid
		// then we ignore the dock area of the dockAreaOverlay() and disable
		// the drop preview
		if ((Area == CenterDockWidgetArea)
		    && (ContainerArea != InvalidDockWidgetArea))
		{
			DockAreaOverlay->enableDropPreview(false);
			ContainerOverlay->enableDropPreview(true);
		}
		else
		{
			ContainerOverlay->enableDropPreview(InvalidDockWidgetArea == Area);
		}
	}
	else
	{
		DockAreaOverlay->hideOverlay();
	}
}


//============================================================================
void FloatingDockContainerPrivate::handleEscapeKey()
{
	ADS_PRINT("FloatingDockContainerPrivate::handleEscapeKey()");
	setState(DraggingInactive);
	DockManager->containerOverlay()->hideOverlay();
	DockManager->dockAreaOverlay()->hideOverlay();
}


//============================================================================
CFloatingDockContainer::CFloatingDockContainer(CDockManager *DockManager) :
	tFloatingWidgetBase(athenaFloatingContainerParent(DockManager)),
	d(new FloatingDockContainerPrivate(this))
{
	d->DockManager = DockManager;
	d->DockContainer = new CDockContainerWidget(DockManager, this);
	connect(d->DockContainer, SIGNAL(dockAreasAdded()), this,
	    SLOT(onDockAreasAddedOrRemoved()));
	connect(d->DockContainer, SIGNAL(dockAreasRemoved()), this,
	    SLOT(onDockAreasAddedOrRemoved()));

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
	QDockWidget::setWidget(d->DockContainer);
	QDockWidget::setFeatures(QDockWidget::DockWidgetClosable
		| QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

	bool native_window = true;

	// FloatingContainerForce*TitleBar is overwritten by the "ADS_UseNativeTitle" environment variable if set.
	auto env = qgetenv("ADS_UseNativeTitle").toUpper();
	if (env == "1")
	{
		native_window = true;
	}
	else if (env == "0")
	{
		native_window = false;
	}
	else if (DockManager->testConfigFlag(CDockManager::FloatingContainerForceNativeTitleBar))
	{
		native_window = true;
	}
	else if (DockManager->testConfigFlag(CDockManager::FloatingContainerForceQWidgetTitleBar))
	{
		native_window = false;
	}
	else
	{
		// KDE doesn't seem to fire MoveEvents while moving windows, so for now no native titlebar for everything using KWin.
		QString window_manager = internal::windowManager().toUpper().split(" ")[0];
                native_window = window_manager != "KWIN";
	}
	if (native_window)
	{
		setTitleBarWidget(new QWidget());
		setWindowFlags(Qt::Window | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint | Qt::CustomizeWindowHint | Qt::WindowCloseButtonHint);
	}
	else
	{
		d->TitleBar = new CFloatingWidgetTitleBar(this);
		setTitleBarWidget(d->TitleBar);
		setWindowFlags(Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::FramelessWindowHint);
		d->TitleBar->enableCloseButton(isClosable());
		connect(d->TitleBar, SIGNAL(closeRequested()), SLOT(close()));
		connect(d->TitleBar, &CFloatingWidgetTitleBar::maximizeRequested,
				this, &CFloatingDockContainer::onMaximizeRequest);
	}
#else
	setWindowFlags(
	    Qt::Window | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
	QBoxLayout *l = new QBoxLayout(QBoxLayout::TopToBottom);
	l->setContentsMargins(0, 0, 0, 0);
	l->setSpacing(0);
	setLayout(l);
	l->addWidget(d->DockContainer);
#endif

	DockManager->registerFloatingWidget(this);
}

//============================================================================
CFloatingDockContainer::CFloatingDockContainer(CDockAreaWidget *DockArea) :
	CFloatingDockContainer(DockArea->dockManager())
{
	d->DockContainer->addDockArea(DockArea);

    auto TopLevelDockWidget = topLevelDockWidget();
    if (TopLevelDockWidget)
    {
    	TopLevelDockWidget->emitTopLevelChanged(true);
    }

    d->DockManager->notifyWidgetOrAreaRelocation(DockArea);
}

//============================================================================
CFloatingDockContainer::CFloatingDockContainer(CDockWidget *DockWidget) :
	CFloatingDockContainer(DockWidget->dockManager())
{
	d->DockContainer->addDockWidget(CenterDockWidgetArea, DockWidget);
    auto TopLevelDockWidget = topLevelDockWidget();
    if (TopLevelDockWidget)
    {
    	TopLevelDockWidget->emitTopLevelChanged(true);
    }

    d->DockManager->notifyWidgetOrAreaRelocation(DockWidget);
}


//============================================================================
CFloatingDockContainer::~CFloatingDockContainer()
{
	ADS_PRINT("~CFloatingDockContainer");
	if (d->DockManager)
	{
		d->DockManager->removeFloatingWidget(this);
	}
	delete d;
}


//============================================================================
void CFloatingDockContainer::deleteContent()
{
	std::vector<QPointer<ads::CDockAreaWidget>> areas;
	for (int i = 0; i != dockContainer()->dockAreaCount(); ++i)
	{
		areas.push_back( dockContainer()->dockArea(i) );
	}
	for (auto area : areas)
	{
		if (!area)
		{
			continue;
		}

		// QPointer delete safety - just in case some dock widget in destruction
		// deletes another related/twin or child dock widget.
		std::vector<QPointer<QWidget>> deleteWidgets;
		for (auto widget : area->dockWidgets())
		{
			deleteWidgets.push_back(widget);
		}
		for (auto ptrWdg : deleteWidgets)
		{
			delete ptrWdg;
		}
	}
}

//============================================================================
CDockContainerWidget* CFloatingDockContainer::dockContainer() const
{
	return d->DockContainer;
}

//============================================================================
void CFloatingDockContainer::changeEvent(QEvent *event)
{
	Super::changeEvent(event);
	switch (event->type())
	{
	case QEvent::ActivationChange:
		if (isActiveWindow())
		{
			ADS_PRINT("FloatingWidget::changeEvent QEvent::ActivationChange ");
			d->zOrderIndex = ++zOrderCounterFloating;

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
			if (d->DraggingState == DraggingFloatingWidget
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
			    && !athenaIsNativeWaylandPlatform()
#endif
			)
			{
				d->titleMouseReleaseEvent();
				d->DraggingState = DraggingInactive;
			}
#endif
		}
		break;

	case QEvent::WindowStateChange:
	    // If the DockManager window is restored from minimized on Windows
		// then the FloatingWidgets are not properly restored to maximized but
		// to normal state.
		// We simply check here, if the FloatingWidget was maximized before
		// and if the DockManager is just leaving the minimized state. In this
		// case, we restore the maximized state of this floating widget
		if (d->DockManager->isLeavingMinimizedState())
		{
			QWindowStateChangeEvent* ev = static_cast<QWindowStateChangeEvent*>(event);
			if (ev->oldState().testFlag(Qt::WindowMaximized))
			{
				this->showMaximized();
			}
		}
		break;

	default:
		break; // do nothing
	}
}


#ifdef Q_OS_WIN
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
bool CFloatingDockContainer::nativeEvent(const QByteArray &eventType, void *message, long *result)
#else
bool CFloatingDockContainer::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
#endif
{
	QWidget::nativeEvent(eventType, message, result);
	MSG *msg = static_cast<MSG*>(message);
	switch (msg->message)
	{
		case WM_MOVING:
		{
			if (d->isState(DraggingFloatingWidget))
			{
				d->updateDropOverlays(QCursor::pos());
			}
		}
		break;

		case WM_NCLBUTTONDOWN:
			 if (msg->wParam == HTCAPTION && d->isState(DraggingInactive))
			 {
				ADS_PRINT("CFloatingDockContainer::nativeEvent WM_NCLBUTTONDOWN");
				d->DragStartPos = pos();
				d->setState(DraggingMousePressed);
			 }
			 break;

		case WM_NCLBUTTONDBLCLK:
			 d->setState(DraggingInactive);
			 break;

		case WM_ENTERSIZEMOVE:
			 if (d->isState(DraggingMousePressed))
			 {
				ADS_PRINT("CFloatingDockContainer::nativeEvent WM_ENTERSIZEMOVE");
				d->setState(DraggingFloatingWidget);
				d->updateDropOverlays(QCursor::pos());
			 }
			 break;

		case WM_EXITSIZEMOVE:
			 if (d->isState(DraggingFloatingWidget))
			 {
				ADS_PRINT("CFloatingDockContainer::nativeEvent WM_EXITSIZEMOVE");
				if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
				{
					d->handleEscapeKey();
				}
				else
				{
					d->titleMouseReleaseEvent();
				}
			 }
			 break;
	}
	return false;
}
#endif


//============================================================================
void CFloatingDockContainer::closeEvent(QCloseEvent *event)
{
	ADS_PRINT("CFloatingDockContainer closeEvent");
	d->setState(DraggingInactive);
	event->ignore();
	if (!isClosable())
	{
		return;
	}

	bool HasOpenDockWidgets = false;
	for (auto DockWidget : d->DockContainer->openedDockWidgets())
	{
		if (DockWidget->features().testFlag(CDockWidget::DockWidgetDeleteOnClose) || DockWidget->features().testFlag(CDockWidget::CustomCloseHandling))
		{
			bool Closed = DockWidget->closeDockWidgetInternal();
			if (!Closed)
			{
				HasOpenDockWidgets = true;
			}
		}
		else
		{
			DockWidget->toggleView(false);
		}
	}

	if (HasOpenDockWidgets)
	{
		return;
	}

	// In Qt version after 5.9.2 there seems to be a bug that causes the
	// QWidget::event() function to not receive any NonClientArea mouse
	// events anymore after a close/show cycle. The bug is reported here:
	// https://bugreports.qt.io/browse/QTBUG-73295
	// The following code is a workaround for Qt versions > 5.9.2 that seems
	// to work
	// Starting from Qt version 5.12.2 this seems to work again. But
	// now the QEvent::NonClientAreaMouseButtonPress function returns always
	// Qt::RightButton even if the left button was pressed
	this->hide();
}

//============================================================================
void CFloatingDockContainer::hideEvent(QHideEvent *event)
{
	Super::hideEvent(event);
    if (event->spontaneous())
    {
        return;
    }

    // Prevent toogleView() events during restore state
    if (d->DockManager->isRestoringState())
    {
        return;
    }

	if ( d->AutoHideChildren )
	{
		d->Hiding = true;
		for ( auto DockArea : d->DockContainer->openedDockAreas() )
		{
			for ( auto DockWidget : DockArea->openedDockWidgets() )
			{
				DockWidget->toggleView( false );
			}
		}
		d->Hiding = false;
	}
}


//============================================================================
void CFloatingDockContainer::showEvent(QShowEvent *event)
{
	Super::showEvent(event);
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    if (CDockManager::testConfigFlag(CDockManager::FocusHighlighting))
    {
        this->window()->activateWindow();
    }
#endif
}


//============================================================================
bool CFloatingDockContainer::athenaWaylandDockDragActive() const
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
	return AthenaActiveWaylandDockDrag == this;
#else
	return false;
#endif
}

bool CFloatingDockContainer::athenaWaylandDockDragStarted() const
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
	return d->AthenaWaylandDockDragStarted;
#else
	return false;
#endif
}

bool CFloatingDockContainer::athenaTryStartWaylandDockDrag(const QPoint& hotSpot, QWidget* sourceWidget)
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
	d->AthenaWaylandDockDragStarted = false;
	if (!athenaIsNativeWaylandPlatform())
	{
		return false;
	}

	{
		std::ostringstream line;
		line << "ATHENA_DOCK_START"
		     << " floating=" << this
		     << " sourceWidget=" << sourceWidget
		     << " hotSpot=" << hotSpot.x() << "," << hotSpot.y()
		     << " cursor=" << QCursor::pos().x() << "," << QCursor::pos().y();
		athenaAdsDockLog(line.str(), true);
	}

	athenaLogWindowSurface("try-start-entry", this);
	athenaLogWindowSurface("try-start-source-widget", sourceWidget);
	athenaLogTopLevelWindows("before-floating-show");

	if (!isVisible())
	{
		show();
	}
	athenaLogWindowSurface("after-floating-show", this);
	QWindow* floatingWindow = windowHandle();
	if (floatingWindow == nullptr)
	{
		return false;
	}

	// Match Qt's own QMainWindow platform drag contract: the QDrag source is
	// the detached top-level that is named in the private MIME payload.  Using
	// the main window as the source while asking QtWayland to move this floating
	// toplevel puts the main window into the drag's Qt-side surface path.
	QWidget* dragSource = this;
	athenaLogWindowSurface("drag-source", dragSource);
	if (dragSource == nullptr)
	{
		return false;
	}

	QDrag drag(dragSource);
	{
		std::ostringstream line;
		line << "ATHENA_GIANT qdrag-created"
		     << " dragSource=" << dragSource
		     << " dragSourceWindow=" << dragSource->window()
		     << " floating=" << this
		     << " floatingWindow=" << floatingWindow
		     << " hotSpot=" << hotSpot.x() << "," << hotSpot.y()
		     << " dragPixmapNull=" << drag.pixmap().isNull()
		     << " dragPixmapSize=" << athenaAdsSizeText(drag.pixmap().size())
		     << " dragPixmapDpr=" << drag.pixmap().devicePixelRatio()
		     << " floatingWindowSize=" << athenaAdsSizeText(floatingWindow->size())
		     << " floatingWindowGeometry=" << athenaAdsRectText(floatingWindow->geometry())
		     << " floatingWindowDpr=" << floatingWindow->devicePixelRatio();
		athenaAdsGiantLog(line.str());
	}
	if (athenaAdsWaylandDebugEnabled())
	{
		std::cerr << "ATHENA_ADS_WAYLAND qdrag-created"
		          << " dragSource=" << dragSource
		          << " dragSourceWindow=" << dragSource->window()
		          << " floating=" << this
		          << " floatingWindow=" << floatingWindow
		          << " hotSpot=" << hotSpot.x() << "," << hotSpot.y()
		          << " dragPixmapNull=" << drag.pixmap().isNull()
		          << " dragPixmapSize=" << athenaAdsSizeText(drag.pixmap().size())
		          << " dragPixmapDpr=" << drag.pixmap().devicePixelRatio()
		          << " floatingWindowSize=" << athenaAdsSizeText(floatingWindow->size())
		          << " floatingWindowGeometry=" << athenaAdsRectText(floatingWindow->geometry())
		          << " floatingWindowDpr=" << floatingWindow->devicePixelRatio()
		          << std::endl;
	}
	auto* mimeData = new QMimeData();
	mimeData->setData(AthenaAdsFloatingDockMime, QByteArrayLiteral("1"));
	mimeData->setData(AthenaQtMainWindowDragWindowMime,
	                  athenaDataStreamPayload(reinterpret_cast<qintptr>(floatingWindow)));
	mimeData->setData(AthenaQtMainWindowDragPositionMime,
	                  athenaDataStreamPayload(hotSpot));
	drag.setMimeData(mimeData);

	AthenaActiveWaylandDockDrag = this;
	d->AthenaWaylandDockDragStarted = true;
	d->AthenaWaylandHasLastGlobalPos = false;
	athenaClearWaylandDockOverlayCursorPosition();
	athenaPrepareWaylandDockDropTargets(d->DockManager, this);
	qApp->installEventFilter(athenaWaylandDockDragFilter());
	athenaLogTopLevelWindows("before-qdrag-exec");
	drag.exec(Qt::MoveAction, Qt::MoveAction);
	athenaLogTopLevelWindows("after-qdrag-exec");
	athenaLogWindowSurface("after-qdrag-floating", this);
	QPointer<CFloatingDockContainer> delayedFloating(this);
	QTimer::singleShot(0, this, [delayedFloating]() {
		if (delayedFloating)
		{
			athenaLogWindowSurface("after-qdrag-floating-0ms", delayedFloating);
		}
	});
	QTimer::singleShot(100, this, [delayedFloating]() {
		if (delayedFloating)
		{
			athenaLogWindowSurface("after-qdrag-floating-100ms", delayedFloating);
		}
	});
	QTimer::singleShot(500, this, [delayedFloating]() {
		if (delayedFloating)
		{
			athenaLogWindowSurface("after-qdrag-floating-500ms", delayedFloating);
		}
	});
	if (athenaWaylandDockDragActive())
	{
		athenaFinishWaylandDockDrag(false);
	}
	qApp->removeEventFilter(athenaWaylandDockDragFilter());
	athenaRestoreWaylandDockDropTargets();
	athenaClearWaylandDockOverlayCursorPosition();
	return true;
#else
	Q_UNUSED(hotSpot)
	Q_UNUSED(sourceWidget)
	return false;
#endif
}

void CFloatingDockContainer::athenaUpdateWaylandDockDrag(const QPoint& globalPos)
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
	if (athenaWaylandDockDragActive())
	{
		d->AthenaWaylandLastGlobalPos = globalPos;
		d->AthenaWaylandHasLastGlobalPos = true;
		athenaSetWaylandDockOverlayCursorPosition(globalPos);
		d->updateDropOverlays(globalPos);
		if (d->DockManager)
		{
			DockWidgetArea containerArea =
				d->DockManager->containerOverlay()->dropAreaUnderCursor();
			DockWidgetArea dockArea =
				d->DockManager->dockAreaOverlay()->dropAreaUnderCursor();
			QWidget* dropWidget = d->DropContainer;
			QPoint localPos = dropWidget == nullptr
				? QPoint()
				: dropWidget->mapFromGlobal(globalPos);
			std::ostringstream line;
			line << "ATHENA_DOCK_OVERLAY"
			     << " global=" << globalPos.x() << "," << globalPos.y()
			     << " cursor=" << QCursor::pos().x() << "," << QCursor::pos().y()
			     << " dropContainer=" << dropWidget
			     << " dropClass=" << (dropWidget == nullptr ? "" : dropWidget->metaObject()->className())
			     << " dropObject=" << (dropWidget == nullptr ? std::string() : dropWidget->objectName().toStdString())
			     << " dropIsFloating=" << (d->DropContainer && d->DropContainer->isFloating())
			     << " dropGeom=" << athenaAdsRectText(dropWidget == nullptr ? QRect() : dropWidget->geometry())
			     << " dropFrame=" << athenaAdsRectText(dropWidget == nullptr ? QRect() : dropWidget->frameGeometry())
			     << " local=" << localPos.x() << "," << localPos.y()
			     << " containerArea=" << athenaDockAreaName(containerArea)
			     << "(" << int(containerArea) << ")"
			     << " dockArea=" << athenaDockAreaName(dockArea)
			     << "(" << int(dockArea) << ")";
			athenaAdsDockLog(line.str());
		}
	}
#else
	Q_UNUSED(globalPos)
#endif
}

bool CFloatingDockContainer::athenaHasWaylandDockTarget() const
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
	if (!d->DockManager || d->DropContainer == nullptr)
	{
		return false;
	}
	return d->DockManager->dockAreaOverlay()->dropAreaUnderCursor() != InvalidDockWidgetArea
	    || d->DockManager->containerOverlay()->dropAreaUnderCursor() != InvalidDockWidgetArea;
#else
	return false;
#endif
}

bool CFloatingDockContainer::athenaFinishWaylandDockDrag(bool dropped)
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
	if (!athenaWaylandDockDragActive())
	{
		return false;
	}

	const QPoint dropPos = d->AthenaWaylandHasLastGlobalPos
		? d->AthenaWaylandLastGlobalPos
		: QCursor::pos();
	athenaSetWaylandDockOverlayCursorPosition(dropPos);
	const bool shouldDock = dropped && athenaHasWaylandDockTarget();
	QPointer<CDockManager> dockManager = d->DockManager;
	QPointer<CDockContainerWidget> dropContainer = d->DropContainer;
	{
		DockWidgetArea containerArea = dockManager
			? dockManager->containerOverlay()->dropAreaUnderCursor()
			: InvalidDockWidgetArea;
		DockWidgetArea dockArea = dockManager
			? dockManager->dockAreaOverlay()->dropAreaUnderCursor()
			: InvalidDockWidgetArea;
		QPoint localPos = dropContainer
			? dropContainer->mapFromGlobal(dropPos)
			: QPoint();
		std::ostringstream line;
		line << "ATHENA_DOCK_FINISH"
		     << " dropped=" << dropped
		     << " shouldDock=" << shouldDock
		     << " dropPos=" << dropPos.x() << "," << dropPos.y()
		     << " cursor=" << QCursor::pos().x() << "," << QCursor::pos().y()
		     << " dropContainer=" << dropContainer.data()
		     << " dropIsFloating=" << (dropContainer && dropContainer->isFloating())
		     << " local=" << localPos.x() << "," << localPos.y()
		     << " containerArea=" << athenaDockAreaName(containerArea)
		     << "(" << int(containerArea) << ")"
		     << " dockArea=" << athenaDockAreaName(dockArea)
		     << "(" << int(dockArea) << ")";
		athenaAdsDockLog(line.str());
	}
	d->setState(DraggingInactive);
	if (AthenaActiveWaylandDockDrag == this)
	{
		AthenaActiveWaylandDockDrag.clear();
	}
	if (shouldDock)
	{
		if (!dropContainer)
		{
			if (dockManager)
			{
				dockManager->containerOverlay()->hideOverlay();
				dockManager->dockAreaOverlay()->hideOverlay();
			}
			d->DropContainer = nullptr;
			d->AthenaWaylandHasLastGlobalPos = false;
			athenaClearWaylandDockOverlayCursorPosition();
			return false;
		}
		dropContainer->dropFloatingWidget(this, dropPos);
	}
	else
	{
		athenaHideWaylandDockOverlays();
	}
	if (dockManager)
	{
		dockManager->containerOverlay()->hideOverlay();
		dockManager->dockAreaOverlay()->hideOverlay();
	}
	d->DropContainer = nullptr;
	d->AthenaWaylandHasLastGlobalPos = false;
	athenaClearWaylandDockOverlayCursorPosition();
	return shouldDock;
#else
	Q_UNUSED(dropped)
	return false;
#endif
}

void CFloatingDockContainer::athenaHideWaylandDockOverlays()
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
	if (d->DockManager)
	{
		d->DockManager->containerOverlay()->hideOverlay();
		d->DockManager->dockAreaOverlay()->hideOverlay();
	}
	d->DropContainer = nullptr;
	d->AthenaWaylandHasLastGlobalPos = false;
	athenaClearWaylandDockOverlayCursorPosition();
#endif
}

void CFloatingDockContainer::startFloating(const QPoint &DragStartMousePos,
    const QSize &Size, eDragState DragState, QWidget *MouseEventHandler)
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    if (!isMaximized())
    {
		resize(Size);
		d->DragStartMousePosition = DragStartMousePos;
    }
	d->setState(DragState);
	if (DraggingFloatingWidget == DragState)
	{
		show();
		if (athenaTryStartWaylandDockDrag(DragStartMousePos, MouseEventHandler))
		{
			return;
		}
		d->MouseEventHandler = MouseEventHandler;
		if (d->MouseEventHandler)
		{
			d->MouseEventHandler->grabMouse();
		}
	}

	if (!isMaximized())
	{
		moveFloating();
	}
	show();
#else
    Q_UNUSED(MouseEventHandler)
	resize(Size);
	d->DragStartMousePosition = DragStartMousePos;
	d->setState(DragState);
	moveFloating();
	show();
#endif
}

//============================================================================
void CFloatingDockContainer::moveFloating()
{
	if (athenaWaylandDockDragActive())
	{
		return;
	}
	int BorderSize = (frameSize().width() - size().width()) / 2;
	const QPoint moveToPos = QCursor::pos() - d->DragStartMousePosition
	    - QPoint(BorderSize, 0);
	move(moveToPos);
	switch (d->DraggingState)
	{
	case DraggingMousePressed:
		d->setState(DraggingFloatingWidget);
		d->updateDropOverlays(QCursor::pos());
		break;

	case DraggingFloatingWidget:
		d->updateDropOverlays(QCursor::pos());
#ifdef Q_OS_MACOS
		// In OSX when hiding the DockAreaOverlay the application would set
		// the main window as the active window for some reason. This fixes
		// that by resetting the active window to the floating widget after
		// updating the overlays.
		activateWindow();
#endif
		break;
	default:
		break;
	}
}

//============================================================================
bool CFloatingDockContainer::isClosable() const
{
	return d->DockContainer->features().testFlag(
	    CDockWidget::DockWidgetClosable);
}

//============================================================================
void CFloatingDockContainer::onDockAreasAddedOrRemoved()
{
	ADS_PRINT("CFloatingDockContainer::onDockAreasAddedOrRemoved()");
	auto TopLevelDockArea = d->DockContainer->topLevelDockArea();
	if (TopLevelDockArea)
	{
		d->SingleDockArea = TopLevelDockArea;
		CDockWidget* CurrentWidget = d->SingleDockArea->currentDockWidget();
		d->reflectCurrentWidget(CurrentWidget);
		connect(d->SingleDockArea, SIGNAL(currentChanged(int)), this,
		    SLOT(onDockAreaCurrentChanged(int)));
	}
	else
	{
		if (d->SingleDockArea)
		{
			disconnect(d->SingleDockArea, SIGNAL(currentChanged(int)), this,
			    SLOT(onDockAreaCurrentChanged(int)));
			d->SingleDockArea = nullptr;
		}
		d->setWindowTitle(d->floatingContainersTitle());
		setWindowIcon(QApplication::windowIcon());
	}
}

//============================================================================
void CFloatingDockContainer::updateWindowTitle()
{
	// If this floating container will be hidden, then updating the window
	// tile is not required anymore
	if (d->Hiding)
	{
		return;
	}


	auto TopLevelDockArea = d->DockContainer->topLevelDockArea();
	if (TopLevelDockArea)
	{
		CDockWidget* CurrentWidget = TopLevelDockArea->currentDockWidget();
		if (CurrentWidget)
		{
			d->reflectCurrentWidget(CurrentWidget);
		}
	}
	else
	{
		d->setWindowTitle(d->floatingContainersTitle());
		setWindowIcon(QApplication::windowIcon());
	}
}

//============================================================================
void CFloatingDockContainer::onDockAreaCurrentChanged(int Index)
{
	Q_UNUSED(Index);
	CDockWidget* CurrentWidget = d->SingleDockArea->currentDockWidget();
	d->reflectCurrentWidget(CurrentWidget);
}

//============================================================================
bool CFloatingDockContainer::restoreState(CDockingStateReader &Stream,
    bool Testing)
{
	if (!d->DockContainer->restoreState(Stream, Testing))
	{
		return false;
	}
	onDockAreasAddedOrRemoved();
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
	if(d->TitleBar)
	{
		d->TitleBar->setMaximizedIcon(windowState() == Qt::WindowMaximized);
	}
#endif
	return true;
}


//============================================================================
bool CFloatingDockContainer::hasTopLevelDockWidget() const
{
	return d->DockContainer->hasTopLevelDockWidget();
}

//============================================================================
CDockWidget* CFloatingDockContainer::topLevelDockWidget() const
{
	return d->DockContainer->topLevelDockWidget();
}

//============================================================================
QList<CDockWidget*> CFloatingDockContainer::dockWidgets() const
{
	return d->DockContainer->dockWidgets();
}

//============================================================================
void CFloatingDockContainer::finishDropOperation()
{
	// Widget has been redocked, so it must be hidden right way (see 
	// https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System/issues/351)
	// but AutoHideChildren must be set to false because "this" still contains
	// dock widgets that shall not be toggled hidden.
	d->AutoHideChildren = false;
	hide();
	// The floating widget will be deleted now. Ensure, that the destructor
	// of the floating widget does not delete any dock areas that have been
	// moved to a new container - simply remove all dock areas before deleting
	// the floating widget
	d->DockContainer->removeAllDockAreas();
	deleteLater();
	if (d->DockManager)
	{
		d->DockManager->removeFloatingWidget(this);
		d->DockManager->removeDockContainer(this->dockContainer());
	}
}

//============================================================================
void CFloatingDockContainer::finishDragging()
{
	ADS_PRINT("CFloatingDockContainer::finishDragging");
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
	setWindowOpacity(1);
	activateWindow();
	if (d->MouseEventHandler)
	{
	   d->MouseEventHandler->releaseMouse();
	   d->MouseEventHandler = nullptr;
	}
	if (athenaWaylandDockDragActive())
	{
		return;
	}
#endif
	d->titleMouseReleaseEvent();
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
	athenaSnapFloatingContainerToScreenEdge(this);
#endif
}

#ifdef Q_OS_MACOS
//============================================================================
bool CFloatingDockContainer::event(QEvent *e)
{
	switch (d->DraggingState)
	{
	case DraggingInactive:
	{
		// Normally we would check here, if the left mouse button is pressed.
		// But from QT version 5.12.2 on the mouse events from
		// QEvent::NonClientAreaMouseButtonPress return the wrong mouse button
		// The event always returns Qt::RightButton even if the left button
		// is clicked.
		// It is really great to work around the whole NonClientMouseArea
		// bugs
#if (QT_VERSION >= QT_VERSION_CHECK(5, 12, 2))
		if (e->type() == QEvent::NonClientAreaMouseButtonPress /*&& QGuiApplication::mouseButtons().testFlag(Qt::LeftButton)*/)
#else
		if (e->type() == QEvent::NonClientAreaMouseButtonPress && QGuiApplication::mouseButtons().testFlag(Qt::LeftButton))
#endif
		{
			ADS_PRINT("FloatingWidget::event Event::NonClientAreaMouseButtonPress" << e->type());
			d->DragStartPos = pos();
			d->setState(DraggingMousePressed);
		}
	}
	break;

	case DraggingMousePressed:
		switch (e->type())
		{
		case QEvent::NonClientAreaMouseButtonDblClick:
			ADS_PRINT("FloatingWidget::event QEvent::NonClientAreaMouseButtonDblClick");
			d->setState(DraggingInactive);
			break;

		case QEvent::Resize:
			// If the first event after the mouse press is a resize event, then
			// the user resizes the window instead of dragging it around.
			// But there is one exception. If the window is maximized,
			// then dragging the window via title bar will cause the widget to
			// leave the maximized state. This in turn will trigger a resize event.
			// To know, if the resize event was triggered by user via moving a
			// corner of the window frame or if it was caused by a windows state
			// change, we check, if we are not in maximized state.
			if (!isMaximized())
			{
				d->setState(DraggingInactive);
			}
			break;

		default:
			break;
		}
		break;

	case DraggingFloatingWidget:
		if (e->type() == QEvent::NonClientAreaMouseButtonRelease)
		{
			ADS_PRINT("FloatingWidget::event QEvent::NonClientAreaMouseButtonRelease");
			d->titleMouseReleaseEvent();
		}
		break;

	default:
		break;
	}

#if (ADS_DEBUG_LEVEL > 0)
	qDebug() << QTime::currentTime() << "CFloatingDockContainer::event " << e->type();
#endif
	return QWidget::event(e);
}


//============================================================================
void CFloatingDockContainer::moveEvent(QMoveEvent *event)
{
	QWidget::moveEvent(event);
	switch (d->DraggingState)
	{
	case DraggingMousePressed:
		d->setState(DraggingFloatingWidget);
		d->updateDropOverlays(QCursor::pos());
		break;

	case DraggingFloatingWidget:
		d->updateDropOverlays(QCursor::pos());
		// In OSX when hiding the DockAreaOverlay the application would set
		// the main window as the active window for some reason. This fixes
		// that by resetting the active window to the floating widget after
		// updating the overlays.
		activateWindow();
		break;
	default:
		break;
	}


}
#endif


#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
//============================================================================
void CFloatingDockContainer::onMaximizeRequest()
{
	if (windowState() == Qt::WindowMaximized)
	{
		showNormal();
	}
	else
	{
		showMaximized();
	}
}


//============================================================================
void CFloatingDockContainer::showNormal(bool fixGeometry)
{
    if ( (windowState() & Qt::WindowMaximized) != 0 ||
         (windowState() & Qt::WindowFullScreen) != 0)
	{
		QRect oldNormal = normalGeometry();
		Super::showNormal();
		if(fixGeometry)
		{
			setGeometry(oldNormal);
		}
	}
	if(d->TitleBar)
	{
		d->TitleBar->setMaximizedIcon(false);
	}
}


//============================================================================
void CFloatingDockContainer::showMaximized()
{
	Super::showMaximized();
	if (d->TitleBar)
	{
		d->TitleBar->setMaximizedIcon(true);
	}
}


//============================================================================
bool CFloatingDockContainer::isMaximized() const
{
	return windowState() == Qt::WindowMaximized;
}


//============================================================================
void CFloatingDockContainer::show()
{
	// These XCB properties are X11-only. Calling winId() here on native
	// Wayland forces Qt to create native child surfaces while ADS is
	// reparenting the dock tree.
	if (QApplication::platformName() == QLatin1String("xcb"))
	{
		internal::xcb_add_prop(true, winId(), "_NET_WM_STATE", "_NET_WM_STATE_SKIP_TASKBAR");
		internal::xcb_add_prop(true, winId(), "_NET_WM_STATE", "_NET_WM_STATE_SKIP_PAGER");
	}
	Super::show();
}


//============================================================================
void CFloatingDockContainer::resizeEvent(QResizeEvent *event)
{
	d->IsResizing = true;
	Super::resizeEvent(event);
}


//============================================================================
void CFloatingDockContainer::moveEvent(QMoveEvent *event)
{
	Super::moveEvent(event);
    if (!d->IsResizing && event->spontaneous() && d->MousePressed
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        && !athenaIsNativeWaylandPlatform()
#endif
        )
	{
        d->setState(DraggingFloatingWidget);
		d->updateDropOverlays(QCursor::pos());
	}
	d->IsResizing = false;
}


//============================================================================
bool CFloatingDockContainer::event(QEvent *e)
{
	bool result = Super::event(e);
	switch (e->type())
	{
	case QEvent::WindowActivate:
        d->MousePressed = false;
		break;
	case QEvent::WindowDeactivate:
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        d->MousePressed = !athenaIsNativeWaylandPlatform();
#else
        d->MousePressed = true;
#endif
		break;
	default:
		break;
	}
	return result;
}

//============================================================================
bool CFloatingDockContainer::hasNativeTitleBar()
{
	return d->TitleBar == nullptr;
}
#endif

} // namespace ads

//---------------------------------------------------------------------------
// EOF FloatingDockContainer.cpp
